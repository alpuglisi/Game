#include <functional>
#include <cstdlib>
#include <cstdio>
#include "physics.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr float PI = 3.14159265f;
constexpr int SUBSTEPS = 4;
constexpr int ITERATIONS = 8;
constexpr float SLOP = 0.4f;
constexpr float BAUMGARTE = 0.2f;
constexpr float MAX_BIAS = 60.f;
constexpr float GAS_PRESSURE = 20000.f;     // force per cell face for one unit of overpressure
constexpr float LIQUID_PRESSURE = 600000.f;
constexpr float LIQUID_DAMPING = 1500.f;
constexpr float ROCKET_THRUST = 1.4e5f;
constexpr float PRIMER_SPEED = 70.f;      // closing speed and energy needed to fire a primer
constexpr float PRIMER_ENERGY = 3.0e4f;

float cellFriction(const World& w, int ix, int iy) {
    return w.inb(ix, iy) ? MATS[w.at(ix, iy).t].friction : MATS[M_WALL].friction;
}
float cellRestitution(const World& w, int ix, int iy) {
    return w.inb(ix, iy) ? MATS[w.at(ix, iy).t].restitution : MATS[M_WALL].restitution;
}

uint64_t pairKey(int a, int b) {
    if (a > b) std::swap(a, b);
    return ((uint64_t)(uint32_t)a << 32) | (uint32_t)b;
}

void boxPoly(const Body& b, Vec2 v[4], Vec2 n[4]) {
    Vec2 c[4] = {{-b.half.x, -b.half.y}, {b.half.x, -b.half.y}, {b.half.x, b.half.y}, {-b.half.x, b.half.y}};
    for (int i = 0; i < 4; ++i) v[i] = b.pos + rotate(c[i], b.angle);
    for (int i = 0; i < 4; ++i) {
        Vec2 e = v[(i + 1) & 3] - v[i];
        n[i] = normalize(Vec2(e.y, -e.x));  // outward
    }
}

// Largest separation of B along any face normal of A (negative = penetrating).
float findAxis(const Vec2 av[4], const Vec2 an[4], const Vec2 bv[4], int& edge) {
    float best = -1e9f;
    for (int i = 0; i < 4; ++i) {
        float minD = 1e9f;
        for (int j = 0; j < 4; ++j) minD = std::min(minD, dot(an[i], bv[j] - av[i]));
        if (minD > best) { best = minD; edge = i; }
    }
    return best;
}

// Keep the part of segment (p0,p1) where dot(nrm,p) - c <= 0.
int clipSegment(Vec2 out[2], Vec2 p0, Vec2 p1, Vec2 nrm, float c) {
    int n = 0;
    float d0 = dot(nrm, p0) - c, d1 = dot(nrm, p1) - c;
    if (d0 <= 0) out[n++] = p0;
    if (d1 <= 0) out[n++] = p1;
    if (d0 * d1 < 0 && n < 2) {
        float t = d0 / (d0 - d1);
        out[n++] = p0 + (p1 - p0) * t;
    }
    return n;
}
}  // namespace

bool Body::contains(Vec2 p) const {
    if (shape == SHAPE_CIRCLE) return lengthSq(p - pos) <= radius * radius;
    Vec2 l = toLocal(p);
    return std::fabs(l.x) <= half.x && std::fabs(l.y) <= half.y;
}

float Body::distanceTo(Vec2 p) const {
    if (shape == SHAPE_CIRCLE) return std::max(0.f, length(p - pos) - radius);
    Vec2 l = toLocal(p);
    float dx = std::max(0.f, std::fabs(l.x) - half.x), dy = std::max(0.f, std::fabs(l.y) - half.y);
    return std::sqrt(dx * dx + dy * dy);
}

Physics::Physics(World* w) : world(w) {
    worldBody.alive = true;
    worldBody.isStatic = true;
}

void Physics::clear() {
    bodies.clear();
    joints.clear();
    contacts.clear();
    std::fill(world->bodyMask.begin(), world->bodyMask.end(), (int16_t)-1);
}

static_assert(std::is_trivially_copyable<Body>::value && std::is_trivially_copyable<Joint>::value, "serialisable");

void Physics::save(Writer& w) const {
    w.vec(bodies);
    w.vec(joints);
    w.pod(gravity); w.pod(seqCounter); w.pod(groupCounter); w.pod(bondCounter);
}

bool Physics::load(Reader& r) {
    std::vector<Body> b;
    std::vector<Joint> j;
    r.vec(b, 20000);
    r.vec(j, 200000);
    Vec2 g = r.pod<Vec2>();
    int sc = r.pod<int>(), gc = r.pod<int>(), bc = r.pod<int>();
    if (!r.ok) return false;
    bodies = b; joints = j; gravity = g; seqCounter = sc; groupCounter = gc; bondCounter = bc;
    contacts.clear(); hydro.clear(); noCollide.clear(); hits.clear(); primerStrikes.clear(); flashes.clear();
    eventFrames = 0; lastEvent.clear();
    for (auto& jt : joints) if (jt.alive && jt.type == J_MOUSE) jt.alive = false;
    stampBodies();
    return true;
}

int Physics::allocBody() {
    for (auto& b : bodies)
        if (!b.alive) { int id = b.id; b = Body{}; b.id = id; b.alive = true; b.seq = ++seqCounter; return id; }
    Body b;
    b.id = (int)bodies.size();
    b.alive = true;
    b.seq = ++seqCounter;
    bodies.push_back(b);
    return b.id;
}

int Physics::allocJoint() {
    for (auto& j : joints)
        if (!j.alive) { int id = j.id; j = Joint{}; j.id = id; j.alive = true; return id; }
    Joint j;
    j.id = (int)joints.size();
    j.alive = true;
    joints.push_back(j);
    return j.id;
}

void Physics::finalize(Body& b) {
    if (b.shape == SHAPE_BOX) {
        b.area = 4.f * b.half.x * b.half.y;
        b.bound = length(b.half);
    } else {
        b.area = PI * b.radius * b.radius;
        b.bound = b.radius;
    }
    b.density = std::min(MATS[b.mat].density, 20.f);
    if (!b.isStatic && MATS[b.mat].density > 20.f) b.density = 8.f;  // devices
    b.mass = b.density * b.area;
    float inertia = b.shape == SHAPE_BOX ? b.mass * (4 * b.half.x * b.half.x + 4 * b.half.y * b.half.y) / 12.f
                                         : 0.5f * b.mass * b.radius * b.radius;
    if (b.isStatic) { b.invMass = 0; b.invI = 0; }
    else { b.invMass = 1.f / b.mass; b.invI = 1.f / inertia; }
}

int Physics::addBox(Vec2 c, Vec2 half, float angle, uint8_t mat, bool stat) {
    int id = allocBody();
    Body& b = bodies[id];
    b.shape = SHAPE_BOX; b.pos = c; b.half = half; b.angle = angle; b.mat = mat; b.isStatic = stat;
    b.color = MATS[mat].color; b.temp = MATS[mat].initT;
    finalize(b);
    return id;
}

int Physics::addCircle(Vec2 c, float r, uint8_t mat, bool stat, bool wheel) {
    int id = allocBody();
    Body& b = bodies[id];
    b.shape = SHAPE_CIRCLE; b.pos = c; b.radius = r; b.mat = mat; b.isStatic = stat; b.isWheel = wheel;
    b.color = MATS[mat].color; b.temp = MATS[mat].initT;
    finalize(b);
    return id;
}

int Physics::addBodyCopy(const Body& src) {
    int id = allocBody();
    Body& b = bodies[id];
    int seq = b.seq;
    b = src;
    b.id = id; b.seq = seq; b.alive = true;
    b.vel = Vec2(); b.w = 0.f;
    b.group = -1;
    b.fluidF = Vec2(); b.fluidT = b.fluidC = 0.f;
    b.touching = b.hasJoint = false;
    b.wetFrac = b.granFrac = b.fluidRho = 0.f;
    b.src.accum = 0.f;
    finalize(b);
    return id;
}

int Physics::addJointCopy(const Joint& src) {
    int id = allocJoint();
    Joint& j = joints[id];
    j = src;
    j.id = id; j.alive = true;
    j.accP = Vec2(); j.accImp = 0.f; j.peak = 0.f; j.fAvg = 0.f;
    return id;
}

// Move bodies by delta. A joint reaching a body that stays behind keeps its world anchor (so nothing snaps when the
// simulation next runs); a pin to the world travels with its body. Groups split by the move are re-welded.
void Physics::translateBodies(const std::vector<int>& ids, Vec2 delta) {
    std::vector<char> in(bodies.size(), 0);
    std::vector<int> groups;
    for (int id : ids) {
        if (id < 0 || id >= (int)bodies.size() || !bodies[id].alive) continue;
        in[id] = 1;
        Body& b = bodies[id];
        b.pos += delta;
        b.vel = Vec2(); b.w = 0.f;
        if (b.group >= 0 && std::find(groups.begin(), groups.end(), b.group) == groups.end()) groups.push_back(b.group);
    }
    for (auto& j : joints) {
        if (!j.alive || j.group >= 0 || j.type == J_MOUSE) continue;
        bool ia = j.a >= 0 && in[j.a], ib = j.b >= 0 && in[j.b];
        if (!ia && !ib) continue;
        if (ia && ib) continue;
        if (j.type == J_SLIDER && j.b < 0) { if (ia) j.lb += delta; continue; }
        if (ia) {
            if (j.b < 0) j.lb += delta;
            else { const Body& B2 = bodies[j.a]; j.la = B2.toLocal((B2.pos - delta) + rotate(j.la, B2.angle)); }
        }
        if (ib) {
            const Body& B2 = bodies[j.b];
            j.lb = B2.toLocal((B2.pos - delta) + rotate(j.lb, B2.angle));
        }
        if (j.type == J_DISTANCE && j.a >= 0) {
            Vec2 pa = jointAnchorA(j), pb = jointAnchorB(j);
            j.length = length(pb - pa);
        }
    }
    for (int g : groups) {
        bool whole = true;
        for (auto& b : bodies) if (b.alive && b.group == g && !in[b.id]) { whole = false; break; }
        if (!whole) rebuildGroup(g);
    }
}

int Physics::addRocket(Vec2 c, float angle) {
    int id = addBox(c, Vec2(4.f, 8.f), angle, M_ALUMINUM, false);
    bodies[id].isRocket = true;
    bodies[id].color = 0xd04a3a;
    return id;
}

int Physics::addPin(Vec2 anchor, int a, int b, bool motor, bool keyed) {
    if (a < 0) std::swap(a, b);
    if (a < 0) return -1;
    int id = allocJoint();
    Joint& j = joints[id];
    j.type = motor ? J_MOTOR : J_PIN;
    j.a = a; j.b = b; j.keyed = keyed;
    j.la = bodies[a].toLocal(anchor);
    j.lb = b >= 0 ? bodies[b].toLocal(anchor) : anchor;
    return id;
}

int Physics::addDistance(int a, Vec2 pa, int b, Vec2 pb, float freq) {
    if (a < 0) { std::swap(a, b); std::swap(pa, pb); }
    if (a < 0 || a == b) return -1;
    int id = allocJoint();
    Joint& j = joints[id];
    j.type = J_DISTANCE;
    j.a = a; j.b = b;
    j.la = bodies[a].toLocal(pa);
    j.lb = b >= 0 ? bodies[b].toLocal(pb) : pb;
    j.length = length(pb - pa);
    j.freq = freq;
    j.damping = 0.35f;
    return id;
}

int Physics::addMouse(int body, Vec2 anchor) {
    int id = allocJoint();
    Joint& j = joints[id];
    j.type = J_MOUSE;
    j.a = body; j.b = -1;
    j.la = bodies[body].toLocal(anchor);
    j.lb = anchor;
    j.maxForce = bodies[body].mass * 6000.f;
    return id;
}

int Physics::addSlider(int body, Vec2 axis) {
    if (body < 0 || length(axis) < 1e-3f) return -1;
    int id = allocJoint();
    Joint& j = joints[id];
    j.type = J_SLIDER;
    j.a = body; j.b = -1;
    j.u = normalize(axis);
    j.lb = bodies[body].pos;       // a point on the line
    j.length = bodies[body].angle; // the locked angle
    return id;
}

int Physics::addSliderRel(int a, int b, Vec2 anchor, Vec2 axis) {
    if (a < 0 || b < 0 || length(axis) < 1e-3f) return -1;
    if (bodies[a].isStatic && !bodies[b].isStatic) std::swap(a, b);   // the moving body slides along the other
    int id = allocJoint();
    Joint& j = joints[id];
    j.type = J_SLIDER;
    j.a = a; j.b = b;
    j.la = bodies[a].toLocal(anchor);
    j.lb = bodies[b].toLocal(anchor);
    j.u = rotate(normalize(axis), -bodies[b].angle);   // the line, in the frame of the body it slides along
    j.length = bodies[a].angle - bodies[b].angle;      // the locked relative angle
    return id;
}

void Physics::setMouseTarget(int joint, Vec2 target) {
    if (joint >= 0 && joint < (int)joints.size() && joints[joint].alive) joints[joint].lb = target;
}

void Physics::removeJoint(int id) {
    if (id >= 0 && id < (int)joints.size()) joints[id].alive = false;
}

void Physics::removeBody(int id) {
    if (id < 0 || id >= (int)bodies.size() || !bodies[id].alive) return;
    bodies[id].alive = false;
    for (auto& j : joints)
        if (j.alive && (j.a == id || j.b == id)) j.alive = false;
    if (bodies[id].group >= 0) rebuildGroup(bodies[id].group);
}

int Physics::bodyCount() const {
    int n = 0;
    for (auto& b : bodies) n += b.alive;
    return n;
}

std::vector<int> Physics::bodiesAt(Vec2 p, int exclude) const {
    std::vector<int> ids;
    for (auto& b : bodies)
        if (b.alive && b.id != exclude && b.contains(p)) ids.push_back(b.id);
    std::sort(ids.begin(), ids.end(), [&](int x, int y) { return bodies[x].seq < bodies[y].seq; });
    return ids;
}

int Physics::pickBody(Vec2 p, bool includeStatic, int exclude) const {
    int best = -1;
    for (auto& b : bodies) {
        if (!b.alive || b.id == exclude || (b.isStatic && !includeStatic) || !b.contains(p)) continue;
        if (best < 0 || b.seq > bodies[best].seq) best = b.id;
    }
    return best;
}

Vec2 Physics::jointAnchorA(const Joint& j) const { return bodies[j.a].toWorld(j.la); }
Vec2 Physics::jointAnchorB(const Joint& j) const { return j.b >= 0 ? bodies[j.b].toWorld(j.lb) : j.lb; }

int Physics::nearestJoint(Vec2 p, float maxDist) const {
    int best = -1;
    float bd = maxDist;
    for (auto& j : joints) {
        if (!j.alive || j.type == J_MOUSE || j.group >= 0) continue;
        Vec2 a = jointAnchorA(j), bb = jointAnchorB(j);
        float d = length(p - a);
        if (j.type == J_DISTANCE) {
            Vec2 ab = bb - a;
            float t = std::clamp(dot(p - a, ab) / std::max(1e-3f, dot(ab, ab)), 0.f, 1.f);
            d = length(p - (a + ab * t));
        }
        if (d < bd) { bd = d; best = j.id; }
    }
    return best;
}

// ---------------------------------------------------------------------------
// World <-> bodies coupling
// ---------------------------------------------------------------------------

void Physics::stampBodies() {
    auto& mask = world->bodyMask;
    world->bodySigma.assign(bodies.size(), 0.f);
    for (auto& b : bodies) if (b.alive) world->bodySigma[b.id] = MATS[b.mat].elec;
    std::fill(mask.begin(), mask.end(), (int16_t)-1);
    auto bounds = [&](const Body& b, int& x0, int& y0, int& x1, int& y1) {
        float r = b.shape == SHAPE_CIRCLE ? b.radius : b.bound;
        x0 = std::max(0, (int)std::floor(b.pos.x - r) - 1); x1 = std::min(World::W - 1, (int)std::ceil(b.pos.x + r) + 1);
        y0 = std::max(0, (int)std::floor(b.pos.y - r) - 1); y1 = std::min(World::H - 1, (int)std::ceil(b.pos.y + r) + 1);
    };
    // A cell counts as covered when the body reaches into it noticeably, so pistons seal their bores.
    auto nearSolid = [&](int x, int y) {
        static const int DXs[4] = {1, -1, 0, 0}, DYs[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; ++d) {
            int nx = x + DXs[d], ny = y + DYs[d];
            if (world->inb(nx, ny)) {
                uint8_t t = world->cells[ny * World::W + nx].t;
                if (MATS[t].kind == K_SOLID && t != M_VOID) return true;
            }
        }
        return false;
    };
    // Cells next to a wall count as covered when the body is within a cell of them, so pistons seal their bores.
    auto covers = [&](const Body& b, int x, int y) {
        const float o = nearSolid(x, y) ? 0.95f : 0.45f;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (b.contains(Vec2(x + 0.5f + dx * o, y + 0.5f + dy * o))) return true;
        return false;
    };
    for (auto& b : bodies) {
        if (!b.alive) continue;
        int x0, y0, x1, y1;
        bounds(b, x0, y0, x1, y1);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                int i = y * World::W + x;
                if (mask[i] >= 0 || !covers(b, x, y)) continue;
                if (MATS[world->cells[i].t].kind == K_SOLID && world->cells[i].t != M_EMPTY) continue;
                mask[i] = (int16_t)b.id;
                if (world->cells[i].t != M_EMPTY) world->displace(x, y);
            }
        }
    }
}

// Gas / liquid pressure on every body, measured on the relaxed grid at the start of a step.
void Physics::fluidForces() {
    auto& mask = world->bodyMask;
    // Connected bodies of liquid. A region with (almost) no free surface is closed, hence incompressible.
    static std::vector<int> compId;
    const int N = World::W * World::H;
    compId.assign(N, -1);
    std::vector<char> closed;
    {
        std::vector<int> stack;
        for (int i = 0; i < N; ++i) {
            if (compId[i] >= 0 || mask[i] >= 0 || MATS[world->cells[i].t].kind != K_LIQUID) continue;
            int id = (int)closed.size();
            int openFaces = 0, size = 0;
            compId[i] = id;
            stack.push_back(i);
            while (!stack.empty()) {
                int p = stack.back();
                stack.pop_back();
                ++size;
                int px = p % World::W, py = p / World::W;
                static const int DXs[4] = {1, -1, 0, 0}, DYs[4] = {0, 0, 1, -1};
                for (int d = 0; d < 4; ++d) {
                    int nx = px + DXs[d], ny = py + DYs[d];
                    if (!world->inb(nx, ny)) continue;
                    int j = ny * World::W + nx;
                    if (mask[j] >= 0) continue;
                    Kind k = MATS[world->cells[j].t].kind;
                    if (k == K_LIQUID) {
                        if (compId[j] < 0) { compId[j] = id; stack.push_back(j); }
                    } else if (k == K_EMPTY || k == K_GAS) {
                        ++openFaces;
                    }
                }
            }
            closed.push_back(openFaces < 6 && size >= 40);  // droplets and puddles are not hydraulic circuits
        }
    }
    hydro.clear();
    groupMem.clear();
    for (auto& b : bodies)
        if (b.alive && b.group >= 0) {
            if ((int)groupMem.size() <= b.group) groupMem.resize(b.group + 1);
            groupMem[b.group].push_back(b.id);
        }
    std::vector<int> groupOf(closed.size(), -1);
    // A machine built around a powered fan (a jet engine) gets its push from the fan model, which already accounts for
    // the gas flowing through it. Counting the grid's pressure on its casing as well would add a second, noisy and
    // off-axis force from the same flow, so those groups feel no gas pressure.
    std::vector<char> fanGroup(groupMem.size(), 0);
    for (size_t g = 0; g < groupMem.size(); ++g)
        for (int m : groupMem[g]) if (bodies[m].fan.strength != 0.f) fanGroup[g] = 1;

    // Pressure of the gas / liquid touching each body, applied as force next frame. Sampled along the
    // body's true outline (outward normal, weighted by length) rather than the grid's coverage cells.
    for (auto& b : bodies) {
        b.fluidF = Vec2();
        b.fluidT = 0;
        b.fluidC = 0;
        if (!b.alive || b.isStatic) continue;
        if (b.group >= 0 && b.group < (int)fanGroup.size() && fanGroup[b.group]) continue;
        struct Sample { Vec2 p, n; float w; int edge; };
        std::vector<Sample> samples;
        if (b.shape == SHAPE_CIRCLE) {
            int n = std::max(8, (int)std::ceil(2 * PI * b.radius));
            for (int i = 0; i < n; ++i) {
                float ang = 2 * PI * (i + 0.5f) / n;
                Vec2 d(std::cos(ang), std::sin(ang));
                samples.push_back({b.pos + d * b.radius, d, 2 * PI * b.radius / n, -1});
            }
        } else {
            Vec2 c[4] = {{-b.half.x, -b.half.y}, {b.half.x, -b.half.y}, {b.half.x, b.half.y}, {-b.half.x, b.half.y}};
            Vec2 nl[4] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
            for (int e = 0; e < 4; ++e) {
                Vec2 p0 = c[e], p1 = c[(e + 1) & 3];
                float len = length(p1 - p0);
                int n = std::max(1, (int)std::ceil(len));
                Vec2 nw = rotate(nl[e], b.angle);
                for (int i = 0; i < n; ++i)
                    samples.push_back({b.toWorld(p0 + (p1 - p0) * ((i + 0.5f) / n)), nw, len / n, e});
            }
        }
        // pressure of the gas at each sample; gas equalises across a flat face almost instantly, so average it
        // over the part of the face that touches gas
        std::vector<float> gasP(samples.size(), -1.f);
        float faceSum[4] = {0, 0, 0, 0};
        int faceCnt[4] = {0, 0, 0, 0};
        // The fluid cell in front of a sample. A face that is flush against a sibling in the same rigid group is
        // interior and carries no load. Otherwise step outward past the body's own cover margin and that of its
        // siblings (stair steps) to the first free cell; a sibling's real body in the way (a narrow pocket) or any
        // other body means no fluid touches this part of the outline.
        const std::vector<int>* sibs = (b.group >= 0 && b.group < (int)groupMem.size()) ? &groupMem[b.group] : nullptr;
        auto insideSibling = [&](Vec2 q) {
            if (!sibs) return false;
            for (int m : *sibs) if (m != b.id && bodies[m].contains(q)) return true;
            return false;
        };
        auto probe = [&](const Sample& sm) -> int {
            if (insideSibling(sm.p + sm.n * 0.3f)) return -1;
            static const float reach[4] = {1.2f, 1.6f, 2.0f, 2.4f};
            for (float d : reach) {
                Vec2 q = sm.p + sm.n * d;
                int ix = (int)std::floor(q.x), iy = (int)std::floor(q.y);
                if (!world->inb(ix, iy)) return -1;
                int j = iy * World::W + ix;
                int m = mask[j];
                if (m == b.id) continue;
                if (m >= 0 && sibs && bodies[m].group == b.group) {
                    if (bodies[m].contains(q)) return -1;  // the far wall of a narrow pocket
                    continue;                              // only a sibling's cover margin
                }
                return m >= 0 ? -1 : j;
            }
            return -1;
        };
        for (size_t i = 0; i < samples.size(); ++i) {
            const Sample& sm = samples[i];
            int pj = probe(sm);
            if (pj < 0) continue;
            const Cell& n = world->cells[pj];
            if (MATS[n.t].kind != K_GAS) continue;
            float psi = n.amt * (n.temp + 273.f) / 293.f;
            gasP[i] = std::max(0.f, psi - 0.15f) * GAS_PRESSURE;
            if (sm.edge >= 0) { faceSum[sm.edge] += gasP[i]; ++faceCnt[sm.edge]; }
        }
        for (size_t i = 0; i < samples.size(); ++i) {
            const Sample& sm = samples[i];
            int j = probe(sm);
            if (j < 0) continue;
            Cell& n = world->cells[j];
            Kind k = MATS[n.t].kind;
            float p;
            if (k == K_GAS) {
                p = (sm.edge >= 0 && faceCnt[sm.edge] > 0) ? faceSum[sm.edge] / faceCnt[sm.edge] : gasP[i];
            } else if (k == K_LIQUID) {
                p = std::max(0.f, n.amt - 1.f) * LIQUID_PRESSURE * MATS[n.t].bulk;
                int cid = compId[j];
                if (cid >= 0 && closed[cid]) {
                    if (groupOf[cid] < 0) { groupOf[cid] = (int)hydro.size(); hydro.emplace_back(); }
                    HydroGroup& g = hydro[groupOf[cid]];
                    // the members of a rigid group sweep volume together: one link per group
                    int key = b.id;
                    float invM = b.invMass;
                    if (b.group >= 0 && b.group < (int)groupMem.size()) {
                        const std::vector<int>& mem = groupMem[b.group];
                        key = mem[0];
                        double mass = 0; bool fixedMember = false;
                        for (int m : mem) { mass += bodies[m].mass; fixedMember |= bodies[m].isStatic; }
                        invM = (fixedMember || mass <= 0) ? 0.f : (float)(1.0 / mass);
                    }
                    HydroLink* link = nullptr;
                    for (auto& l : g.links) if (l.body == key) link = &l;
                    if (!link) { g.links.push_back({key, Vec2(), invM}); link = &g.links.back(); }
                    link->a += sm.n * sm.w;  // pointing from the body into the liquid
                }
                if (n.amt > 1.0001f) b.fluidC += LIQUID_DAMPING * sm.w;
            } else if (k == K_POWDER && sm.edge >= 0 && faceCnt[sm.edge] > 0) {
                p = faceSum[sm.edge] / faceCnt[sm.edge];   // gas pressure reaches the part of a face that lies against loose grains too (a charge of powder burning against a wad)
            } else {
                continue;
            }
            Vec2 r = sm.p - b.pos;
            if (k == K_GAS && n.amt > 0.05f) {
                // work done on the gas heats it (and expansion cools it): pressure x motion, so it only matters
                // in a confined, pressurised chamber and is negligible for a body moving through free gas
                float vn = dot(b.vel + cross(b.w, r), sm.n);  // >0: moving into the gas
                float over = std::max(0.f, n.amt * (n.temp + 273.f) / 293.f - 0.15f);
                n.temp = std::max(-100.f, n.temp + std::clamp(over * vn * 0.08f * sm.w, -30.f, 30.f));
            }
            Vec2 f = sm.n * -(std::min(p, 3e5f) * sm.w);
            b.fluidF += f;
            b.fluidT += cross(r, f);
        }
    }
}

// Heat exchange between bodies and the cells / bodies around them, plus melting and burning.
void Physics::thermalStep() {
    for (auto& b : bodies) {
        if (!b.alive) continue;
        const MatInfo& bm = MATS[b.mat];
        if (b.mat == M_HEATER) b.temp = 900.f;
        else if (b.mat == M_COOLER) b.temp = -60.f;
        float Cb = bm.cap * b.area;
        if (Cb <= 0) continue;

        // sample points just outside the outline; each stands for the stretch of outline it sits on
        struct TP { Vec2 p; float w; };
        std::vector<TP> pts;
        if (b.shape == SHAPE_CIRCLE) {
            int n = std::clamp((int)(2 * PI * (b.radius + 1.5f) / 3.f), 8, 64);
            float w = 2 * PI * b.radius / n;
            for (int i = 0; i < n; ++i) {
                float a = 2 * PI * (i + 0.5f) / n;
                pts.push_back({b.pos + Vec2(std::cos(a), std::sin(a)) * (b.radius + 1.5f), w});
            }
        } else {
            float hx = b.half.x + 1.5f, hy = b.half.y + 1.5f;
            Vec2 c[4] = {{-hx, -hy}, {hx, -hy}, {hx, hy}, {-hx, hy}};
            for (int e = 0; e < 4; ++e) {
                Vec2 p0 = c[e], p1 = c[(e + 1) & 3];
                float len = (e & 1) ? 2.f * b.half.y : 2.f * b.half.x;   // the body's own edge length
                int n = std::max(1, (int)std::ceil(len / 3.f));
                for (int i = 0; i < n; ++i) pts.push_back({b.toWorld(p0 + (p1 - p0) * ((i + 0.5f) / n)), len / n});
            }
        }
        for (const TP& tp : pts) {
            Vec2 p = tp.p;
            const float spacing = tp.w;
            int ix = (int)std::floor(p.x), iy = (int)std::floor(p.y);
            if (!world->inb(ix, iy) || world->bodyMask[iy * World::W + ix] >= 0) continue;
            if (b.mat == M_IGNITER && world->sparkNow) world->ignitePoint(ix, iy);
            Cell& c = world->at(ix, iy);
            float dT, q, lim;
            if (c.t == M_EMPTY) {
                dT = b.temp - AMBIENT_T;
                q = 0.0008f * spacing * dT;
                lim = 0.24f * Cb * dT;
                if (std::fabs(q) > std::fabs(lim)) q = lim;
                b.temp -= q / Cb;
            } else {
                float Cc = cellCap(c);
                float ka = bm.cond, kb = cellCond(c);
                float k = 2.f * ka * kb / (ka + kb + 1e-6f);
                dT = b.temp - c.temp;
                q = k * spacing * dT;
                lim = 0.24f * std::min(Cb, Cc) * dT;
                if (std::fabs(q) > std::fabs(lim)) q = lim;
                b.temp -= q / Cb;
                c.temp += q / Cc;
            }
        }
    }
    // conduction between bodies that touch: every pair of adjacent covered cells carries heat, so welded pieces,
    // pipe segments and bodies resting on each other share temperature in proportion to the contact length
    {
        const int Wd = World::W, Hd = World::H;
        std::vector<std::pair<uint64_t, int>> shared;
        auto note = [&](int a, int b) {
            if (a < 0 || b < 0 || a == b) return;
            if (a > b) std::swap(a, b);
            shared.push_back({((uint64_t)a << 32) | (uint32_t)b, 1});
        };
        const auto& mask = world->bodyMask;
        for (int y = 0; y < Hd; ++y)
            for (int x = 0; x < Wd; ++x) {
                int a = mask[y * Wd + x];
                if (a < 0) continue;
                if (x + 1 < Wd) note(a, mask[y * Wd + x + 1]);
                if (y + 1 < Hd) note(a, mask[(y + 1) * Wd + x]);
            }
        std::sort(shared.begin(), shared.end());
        for (size_t i = 0; i < shared.size();) {
            size_t j = i;
            while (j < shared.size() && shared[j].first == shared[i].first) ++j;
            int cnt = (int)(j - i);
            Body& A = bodies[(int)(shared[i].first >> 32)];
            Body& B2 = bodies[(int)(shared[i].first & 0xffffffffu)];
            i = j;
            if (!A.alive || !B2.alive) continue;
            const MatInfo& ma = MATS[A.mat];
            const MatInfo& mb = MATS[B2.mat];
            float Ca = ma.cap * A.area, Cb = mb.cap * B2.area;
            if (Ca <= 0 || Cb <= 0) continue;
            float k = 2.f * ma.cond * mb.cond / (ma.cond + mb.cond + 1e-6f);
            float dT = A.temp - B2.temp;
            float q = k * (float)cnt * dT;
            float lim = 0.15f * std::min(Ca, Cb) * dT;
            if (std::fabs(q) > std::fabs(lim)) q = lim;
            A.temp -= q / Ca;
            B2.temp += q / Cb;
        }
    }
    for (auto& b : bodies) {
        if (!b.alive) continue;
        const MatInfo& bm = MATS[b.mat];
        bool melt = bm.hiTo != M_EMPTY && b.temp >= bm.hiT;
        bool ignite = bm.ignT > 0.f && b.temp >= bm.ignT;
        if (melt || ignite) dissolve(b);
    }
}

// A body that melts or catches fire turns back into grid cells.
void Physics::dissolve(Body& b) {
    const MatInfo& bm = MATS[b.mat];
    bool melt = bm.hiTo != M_EMPTY && b.temp >= bm.hiT;
    float r = b.shape == SHAPE_CIRCLE ? b.radius : b.bound;
    int x0 = std::max(0, (int)std::floor(b.pos.x - r)), x1 = std::min(World::W - 1, (int)std::ceil(b.pos.x + r));
    int y0 = std::max(0, (int)std::floor(b.pos.y - r)), y1 = std::min(World::H - 1, (int)std::ceil(b.pos.y + r));
    int spawned = 0;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (!b.contains(Vec2(x + 0.5f, y + 0.5f)) || world->at(x, y).t != M_EMPTY) continue;
            ++spawned;
            if (melt) {
                uint8_t to = bm.hiTo;
                world->spawn(x, y, to, (to == M_MOLTEN || to == M_VAPOR) ? b.mat : (uint8_t)0, b.temp);
            } else {
                world->spawn(x, y, b.mat, 0, b.temp);
                world->at(x, y).burn = (uint8_t)std::min(255, std::max(1, bm.burnTime));
            }
        }
    }
    if (!spawned && b.area > 0.2f) {  // a sliver covers no cell centre: its material still goes somewhere
        int x = (int)std::floor(b.pos.x), y = (int)std::floor(b.pos.y);
        if (world->inb(x, y) && world->at(x, y).t == M_EMPTY && world->bodyMask[y * World::W + x] < 0) {
            if (melt) world->spawn(x, y, bm.hiTo, (bm.hiTo == M_MOLTEN || bm.hiTo == M_VAPOR) ? b.mat : (uint8_t)0, b.temp);
            else { world->spawn(x, y, b.mat, 0, b.temp); world->at(x, y).burn = (uint8_t)std::min(255, std::max(1, bm.burnTime)); }
        }
    }
    removeBody(b.id);
}

void Physics::sampleFluids() {
    // Exposure of a body to liquid / grains is read from points just outside its outline. Members of a rigid
    // group are one object: points that fall inside a sibling's cover are interior and ignored, and the group's
    // totals are shared by every member, so a welded plate floats like the single box it replaces.
    struct Acc { int wet = 0, gran = 0, n = 0; float rho = 0; Vec2 cen; };
    std::vector<Acc> groupAcc;
    std::vector<Acc> own(bodies.size());
    for (auto& b : bodies) {
        b.wetFrac = b.granFrac = b.fluidRho = 0;
        if (!b.alive || b.isStatic) continue;
        std::vector<Vec2> pts;
        if (b.shape == SHAPE_CIRCLE) {
            int n = std::clamp((int)(2 * PI * (b.radius + 1.5f) / 3.f), 8, 48);
            for (int i = 0; i < n; ++i) {
                float a = 2 * PI * i / n;
                pts.push_back(b.pos + Vec2(std::cos(a), std::sin(a)) * (b.radius + 1.5f));
            }
        } else {
            float hx = b.half.x + 1.5f, hy = b.half.y + 1.5f;
            Vec2 c[4] = {{-hx, -hy}, {hx, -hy}, {hx, hy}, {-hx, hy}};
            for (int e = 0; e < 4; ++e) {
                Vec2 p0 = c[e], p1 = c[(e + 1) & 3];
                int n = std::max(1, (int)(length(p1 - p0) / 3.f));
                for (int i = 0; i < n; ++i) pts.push_back(b.toWorld(p0 + (p1 - p0) * ((i + 0.5f) / n)));
            }
        }
        Acc& ac = own[b.id];
        for (Vec2 p : pts) {
            int ix = (int)std::floor(p.x), iy = (int)std::floor(p.y);
            if (!world->inb(ix, iy)) continue;
            if (b.group >= 0) {
                int m = world->bodyMask[iy * World::W + ix];
                if (m >= 0 && m < (int)bodies.size() && bodies[m].group == b.group) continue;  // interior to the group
            }
            ++ac.n;
            uint8_t t = world->at(ix, iy).t;
            Kind k = MATS[t].kind;
            if (k == K_LIQUID) { ++ac.wet; ac.rho += MATS[t].density; ac.cen += p; }
            else if (k == K_POWDER) ++ac.gran;
        }
        if (b.group >= 0) {
            if ((int)groupAcc.size() <= b.group) groupAcc.resize(b.group + 1);
            Acc& g = groupAcc[b.group];
            g.wet += ac.wet; g.gran += ac.gran; g.n += ac.n; g.rho += ac.rho; g.cen += ac.cen;
        }
    }
    for (auto& b : bodies) {
        if (!b.alive || b.isStatic) continue;
        const Acc& ac = b.group >= 0 ? groupAcc[b.group] : own[b.id];
        if (ac.n <= 0) continue;
        b.wetFrac = (float)ac.wet / ac.n;
        b.granFrac = (float)ac.gran / ac.n;
        if (ac.wet) { b.fluidRho = ac.rho / ac.wet; b.wetCentroid = ac.cen / (float)ac.wet; }
    }
}

void Physics::applyBlasts() {
    for (const Blast& bl : world->blasts) {
        float range = bl.r * 2.2f;
        for (auto& b : bodies) {
            if (!b.alive || b.isStatic) continue;
            Vec2 d = b.pos - Vec2(bl.x, bl.y);
            float dist = length(d);
            if (dist > range) continue;
            float f = 1.f - dist / range;
            Vec2 dir = dist > 1e-3f ? d / dist : Vec2(0, -1);
            b.vel += dir * (bl.power * f);
            b.w += (dir.x > 0 ? 1.f : -1.f) * f * bl.power * 0.02f;
        }
    }
    world->blasts.clear();
}

// ---------------------------------------------------------------------------
// Collision detection
// ---------------------------------------------------------------------------

Vec2 Physics::surfaceNormal(int ix, int iy) const {
    Vec2 g;
    for (int dy = -3; dy <= 3; ++dy)
        for (int dx = -3; dx <= 3; ++dx)
            if (!world->isTerrain(ix + dx, iy + dy)) g += Vec2((float)dx, (float)dy);
    return normalize(g);
}

void Physics::terrainContacts(Body& b) {
    auto test = [&](Vec2 p, Vec2 faceN = Vec2()) {
        int ix = (int)std::floor(p.x), iy = (int)std::floor(p.y);
        if (!world->isTerrain(ix, iy)) return;
        Vec2 n = surfaceNormal(ix, iy);
        // a flat box face resting on a ledge must not be pushed sideways by the ledge's corner
        if (lengthSq(faceN) > 0.f && dot(n, -faceN) < 0.7f) n = -faceN;
        if (lengthSq(n) < 1e-6f) n = normalize(b.pos - p);
        if (lengthSq(n) < 1e-6f) n = Vec2(0, -1);
        float depth = 12.f;
        for (float t = 0.5f; t <= 12.f; t += 0.5f) {
            Vec2 q = p + n * t;
            if (!world->isTerrain((int)std::floor(q.x), (int)std::floor(q.y))) { depth = t - 0.25f; break; }
        }
        if (world->at(ix, iy).t == M_PRIMER) {
            float closing = -dot(b.vel + cross(b.w, p - b.pos), n);
            if (closing > PRIMER_SPEED && 0.5f * b.mass * closing * closing >= PRIMER_ENERGY) primerStrikes.push_back({ix, iy});
        }
        float mu = std::sqrt(MATS[b.mat].friction * cellFriction(*world, ix, iy)) * (b.isWheel ? 1.25f : 1.f);
        float e = std::max(MATS[b.mat].restitution, cellRestitution(*world, ix, iy));
        contacts.emplace_back(-1, b.id, p, n, depth, std::min(mu, 1.5f), e);
    };
    if (b.shape == SHAPE_CIRCLE) {
        int n = std::clamp((int)(2 * PI * b.radius / 2.5f), 12, 72);
        for (int i = 0; i < n; ++i) {
            float a = 2 * PI * i / n;
            test(b.pos + Vec2(std::cos(a), std::sin(a)) * b.radius);
        }
        test(b.pos);
    } else {
        Vec2 c[4] = {{-b.half.x, -b.half.y}, {b.half.x, -b.half.y}, {b.half.x, b.half.y}, {-b.half.x, b.half.y}};
        for (int e = 0; e < 4; ++e) {
            Vec2 p0 = c[e], p1 = c[(e + 1) & 3];
            static const Vec2 nl[4] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
            Vec2 faceN = rotate(nl[e], b.angle);
            int n = std::max(1, (int)std::ceil(length(p1 - p0) / 2.5f));
            for (int i = 0; i < n; ++i) test(b.toWorld(p0 + (p1 - p0) * ((float)i / n)), i == 0 ? Vec2() : faceN);
        }
        test(b.pos);
    }
}

void Physics::collidePair(Body& A, Body& B2) {
    Body* a = &A;
    Body* b = &B2;
    if (a->shape > b->shape) std::swap(a, b);  // boxes first
    float mu = std::sqrt(MATS[a->mat].friction * MATS[b->mat].friction) * ((a->isWheel || b->isWheel) ? 1.25f : 1.f);
    mu = std::min(mu, 1.5f);
    float e = std::max(MATS[a->mat].restitution, MATS[b->mat].restitution);

    if (a->shape == SHAPE_CIRCLE) {  // circle-circle
        Vec2 d = b->pos - a->pos;
        float dist = length(d), rr = a->radius + b->radius;
        if (dist > rr) return;
        Vec2 n = dist > 1e-5f ? d / dist : Vec2(0, 1);
        float depth = rr - dist;
        contacts.emplace_back(a->id, b->id, a->pos + n * (a->radius - depth * 0.5f), n, depth, mu, e);
        return;
    }
    if (b->shape == SHAPE_CIRCLE) {  // box-circle
        Vec2 l = a->toLocal(b->pos);
        Vec2 cl(std::clamp(l.x, -a->half.x, a->half.x), std::clamp(l.y, -a->half.y, a->half.y));
        Vec2 delta = l - cl;
        float dist2 = lengthSq(delta);
        Vec2 nl;
        float depth;
        if (dist2 > 1e-8f) {
            float dist = std::sqrt(dist2);
            if (dist > b->radius) return;
            nl = delta / dist;
            depth = b->radius - dist;
        } else {
            float dx = a->half.x - std::fabs(l.x), dy = a->half.y - std::fabs(l.y);
            if (dx < dy) { nl = Vec2(l.x < 0 ? -1.f : 1.f, 0); depth = dx + b->radius; }
            else { nl = Vec2(0, l.y < 0 ? -1.f : 1.f); depth = dy + b->radius; }
        }
        Vec2 n = rotate(nl, a->angle);
        contacts.emplace_back(a->id, b->id, b->pos - n * (b->radius - depth * 0.5f), n, depth, mu, e);
        return;
    }
    // box-box via SAT + reference-face clipping
    Vec2 av[4], an[4], bv[4], bn[4];
    boxPoly(*a, av, an);
    boxPoly(*b, bv, bn);
    int ea = 0, eb = 0;
    float sA = findAxis(av, an, bv, ea);
    if (sA > 0) return;
    float sB = findAxis(bv, bn, av, eb);
    if (sB > 0) return;
    bool flip = sB > sA + 0.05f;
    const Vec2 *rv = flip ? bv : av, *rn = flip ? bn : an, *iv = flip ? av : bv, *in = flip ? an : bn;
    int re = flip ? eb : ea;
    Vec2 refN = rn[re];
    int inc = 0;
    float minDot = 1e9f;
    for (int i = 0; i < 4; ++i) {
        float d = dot(in[i], refN);
        if (d < minDot) { minDot = d; inc = i; }
    }
    Vec2 v1 = rv[re], v2 = rv[(re + 1) & 3];
    Vec2 tangent = normalize(v2 - v1);
    Vec2 seg[2], seg2[2];
    if (clipSegment(seg, iv[inc], iv[(inc + 1) & 3], -tangent, -dot(tangent, v1)) < 2) return;
    if (clipSegment(seg2, seg[0], seg[1], tangent, dot(tangent, v2)) < 2) return;
    Vec2 n = flip ? -refN : refN;
    for (int i = 0; i < 2; ++i) {
        float sep = dot(refN, seg2[i] - v1);
        if (sep <= 0.05f) contacts.emplace_back(a->id, b->id, seg2[i], n, std::max(0.f, -sep), mu, e);
    }
}

bool Physics::connected(int a, int b) const {
    return std::binary_search(noCollide.begin(), noCollide.end(), pairKey(a, b));
}

void Physics::buildContacts() {
    contacts.clear();
    for (auto& b : bodies) b.touching = false;
    for (auto& b : bodies)
        if (b.alive && !b.isStatic) terrainContacts(b);
    for (size_t i = 0; i < bodies.size(); ++i) {
        Body& a = bodies[i];
        if (!a.alive) continue;
        for (size_t k = i + 1; k < bodies.size(); ++k) {
            Body& b = bodies[k];
            if (!b.alive || (a.isStatic && b.isStatic)) continue;
            float rr = a.bound + b.bound;
            if (lengthSq(a.pos - b.pos) > rr * rr) continue;
            if (connected(a.id, b.id)) continue;
            collidePair(a, b);
        }
    }
}

// ---------------------------------------------------------------------------
// Solver
// ---------------------------------------------------------------------------

void Physics::prestepContact(Contact& c, float h) {
    Body& A = B(c.a);
    Body& Bb = B(c.b);
    c.rA = c.p - A.pos;
    c.rB = c.p - Bb.pos;
    Vec2 t(-c.n.y, c.n.x);
    float rnA = cross(c.rA, c.n), rnB = cross(c.rB, c.n);
    float rtA = cross(c.rA, t), rtB = cross(c.rB, t);
    float kn = A.invMass + Bb.invMass + A.invI * rnA * rnA + Bb.invI * rnB * rnB;
    float kt = A.invMass + Bb.invMass + A.invI * rtA * rtA + Bb.invI * rtB * rtB;
    c.massN = kn > 0 ? 1.f / kn : 0.f;
    c.massT = kt > 0 ? 1.f / kt : 0.f;
    Vec2 vrel = Bb.vel + cross(Bb.w, c.rB) - A.vel - cross(A.w, c.rA);
    float vn = dot(vrel, c.n);
    if (vn < -PRIMER_SPEED && 0.5f * c.massN * vn * vn >= PRIMER_ENERGY) {
        if (c.a >= 0 && A.mat == M_PRIMER && !A.spent) hits.push_back({c.a, c.p});
        if (c.b >= 0 && Bb.mat == M_PRIMER && !Bb.spent) hits.push_back({c.b, c.p});
    }
    float bounce = vn < -60.f ? -c.e * vn : 0.f;
    float pen = std::min(BAUMGARTE / h * std::max(c.depth - SLOP, 0.f), MAX_BIAS);
    c.vt = std::max(bounce, pen);
    c.jn = c.jt = 0;
}

void Physics::solveContact(Contact& c) {
    Body& A = B(c.a);
    Body& Bb = B(c.b);
    auto apply = [&](Vec2 P) {
        A.vel -= P * A.invMass;
        A.w -= A.invI * cross(c.rA, P);
        Bb.vel += P * Bb.invMass;
        Bb.w += Bb.invI * cross(c.rB, P);
    };
    Vec2 vrel = Bb.vel + cross(Bb.w, c.rB) - A.vel - cross(A.w, c.rA);
    float dj = c.massN * (c.vt - dot(vrel, c.n));
    float jn = std::max(c.jn + dj, 0.f);
    dj = jn - c.jn;
    c.jn = jn;
    apply(c.n * dj);

    vrel = Bb.vel + cross(Bb.w, c.rB) - A.vel - cross(A.w, c.rA);
    Vec2 t(-c.n.y, c.n.x);
    float djt = c.massT * -dot(vrel, t);
    float maxF = c.mu * c.jn;
    float jt = std::clamp(c.jt + djt, -maxF, maxF);
    djt = jt - c.jt;
    c.jt = jt;
    apply(t * djt);
}

void Physics::prestepJoint(Joint& j, float h) {
    Body& A = B(j.a);
    Body& Bb = B(j.b);
    j.rA = rotate(j.la, A.angle);
    j.rB = rotate(j.lb, Bb.angle);
    j.accImp = 0;
    j.accP = Vec2();
    if (j.type == J_SLIDER && j.b >= 0) {   // a slider between two bodies: A keeps to a line fixed in B's frame, and to its angle relative to B
        Vec2 ax = rotate(j.u, Bb.angle), n(-ax.y, ax.x);
        j.rA = rotate(j.la, A.angle); j.rB = rotate(j.lb, Bb.angle);
        Vec2 d = (A.pos + j.rA) - (Bb.pos + j.rB);
        j.k11 = dot(ax, d);   // along-line separation: couples B's rotation into the perpendicular constraint
        float cA = cross(j.rA, n), cB = cross(j.rB, n) + j.k11;
        j.effMass = A.invMass + Bb.invMass + A.invI * cA * cA + Bb.invI * cB * cB;
        j.effMass = j.effMass > 0.f ? 1.f / j.effMass : 0.f;
        j.beta = std::clamp(dot(n, d) * 0.2f / h, -MAX_BIAS, MAX_BIAS);
        float ang = A.angle - Bb.angle - j.length;
        j.gamma = std::clamp(ang * 0.2f / h, -20.f, 20.f);
        return;
    }
    if (j.type == J_SLIDER) {
        Vec2 n(-j.u.y, j.u.x);
        j.beta = std::clamp(dot(n, A.pos - j.lb) * 0.2f / h, -MAX_BIAS, MAX_BIAS);       // perpendicular drift
        j.gamma = std::clamp((A.angle - j.length) * 0.2f / h, -20.f, 20.f);               // angle drift
        return;
    }

    if (j.type == J_PIN || j.type == J_MOTOR) {
        float iM = A.invMass + Bb.invMass;
        j.k11 = iM + A.invI * j.rA.y * j.rA.y + Bb.invI * j.rB.y * j.rB.y;
        j.k12 = -A.invI * j.rA.x * j.rA.y - Bb.invI * j.rB.x * j.rB.y;
        j.k22 = iM + A.invI * j.rA.x * j.rA.x + Bb.invI * j.rB.x * j.rB.x;
        Vec2 err = (Bb.pos + j.rB) - (A.pos + j.rA);
        j.bias = err * (-0.25f / h);
        float l = length(j.bias);
        if (l > 150.f) j.bias = j.bias * (150.f / l);
        if (j.type == J_MOTOR) {
            float iI = A.invI + Bb.invI;
            float p = j.power;
            if (j.keyed && std::fabs(motorInput) < 0.01f) p *= 0.15f;  // light brake
            j.maxImp = iI > 0 ? p / iI * h : 0.f;
        }
    } else if (j.type == J_DISTANCE) {
        Vec2 d = (Bb.pos + j.rB) - (A.pos + j.rA);
        float len = length(d);
        j.u = len > 1e-4f ? d / len : Vec2(1, 0);
        float crA = cross(j.rA, j.u), crB = cross(j.rB, j.u);
        float invM = A.invMass + Bb.invMass + A.invI * crA * crA + Bb.invI * crB * crB;
        float C = len - j.length;
        if (j.freq > 0) {
            float m = invM > 0 ? 1.f / invM : 0.f;
            float omega = 2 * PI * j.freq;
            float dmp = 2 * m * j.damping * omega;
            float k = m * omega * omega;
            j.gamma = h * (dmp + h * k);
            j.gamma = j.gamma > 0 ? 1.f / j.gamma : 0.f;
            float beta = h * k * j.gamma;
            j.beta = C * beta;
        } else {
            j.gamma = 0;
            j.beta = std::clamp(C * 0.2f / h, -MAX_BIAS * 2, MAX_BIAS * 2);
        }
        j.effMass = (invM + j.gamma) > 0 ? 1.f / (invM + j.gamma) : 0.f;
    } else {  // mouse
        float m = A.mass;
        float omega = 2 * PI * 5.f;
        float dmp = 2 * m * 0.8f * omega;
        float k = m * omega * omega;
        j.gamma = h * (dmp + h * k);
        j.gamma = j.gamma > 0 ? 1.f / j.gamma : 0.f;
        j.beta = h * k * j.gamma;
        j.k11 = A.invMass + A.invI * j.rA.y * j.rA.y + j.gamma;
        j.k12 = -A.invI * j.rA.x * j.rA.y;
        j.k22 = A.invMass + A.invI * j.rA.x * j.rA.x + j.gamma;
        j.maxImp = j.maxForce * h;
        j.bias = (A.pos + j.rA) - j.lb;  // C
    }
}

void Physics::solveJoint(Joint& j, float h) {
    Body& A = B(j.a);
    Body& Bb = B(j.b);
    if (j.type == J_SLIDER && j.b >= 0) {
        Vec2 ax = rotate(j.u, Bb.angle), n(-ax.y, ax.x);
        float iI = A.invI + Bb.invI;
        if (iI > 0.f) {   // keep the angle relative to B
            float imp = -((A.w - Bb.w) + j.gamma) / iI;
            A.w += A.invI * imp; Bb.w -= Bb.invI * imp;
        }
        float cA = cross(j.rA, n), cB = cross(j.rB, n) + j.k11;
        float cdot = dot(n, A.vel - Bb.vel) + A.w * cA - Bb.w * cB;
        float P = -(cdot + j.beta) * j.effMass;
        A.vel += n * (P * A.invMass); A.w += A.invI * P * cA;
        Bb.vel -= n * (P * Bb.invMass); Bb.w -= Bb.invI * P * cB;
        return;
    }
    if (j.type == J_SLIDER) {
        if (A.invI > 0.f) A.w -= j.gamma + A.w;          // hold the angle
        if (A.invMass > 0.f) {
            Vec2 n(-j.u.y, j.u.x);
            A.vel -= n * (dot(n, A.vel) + j.beta);        // keep to the line
        }
        return;
    }
    if (j.type == J_PIN || j.type == J_MOTOR) {
        if (j.type == J_MOTOR) {
            float iI = A.invI + Bb.invI;
            if (iI > 0) {
                float target = j.keyed ? motorInput * j.speed : j.speed;
                float imp = (target - (Bb.w - A.w)) / iI;
                float old = j.accImp;
                j.accImp = std::clamp(old + imp, -j.maxImp, j.maxImp);
                imp = j.accImp - old;
                A.w -= A.invI * imp;
                Bb.w += Bb.invI * imp;
            }
        }
        Vec2 dv = Bb.vel + cross(Bb.w, j.rB) - A.vel - cross(A.w, j.rA);
        Vec2 rhs = j.bias - dv;
        float det = j.k11 * j.k22 - j.k12 * j.k12;
        if (std::fabs(det) < 1e-20f) return;
        det = 1.f / det;
        Vec2 P((j.k22 * rhs.x - j.k12 * rhs.y) * det, (j.k11 * rhs.y - j.k12 * rhs.x) * det);
        j.accP += P;  // total impulse this substep: its magnitude / h is the load a frangible bond feels
        A.vel -= P * A.invMass;
        A.w -= A.invI * cross(j.rA, P);
        Bb.vel += P * Bb.invMass;
        Bb.w += Bb.invI * cross(j.rB, P);
    } else if (j.type == J_DISTANCE) {
        Vec2 vrel = Bb.vel + cross(Bb.w, j.rB) - A.vel - cross(A.w, j.rA);
        float Cdot = dot(j.u, vrel);
        float imp = -j.effMass * (Cdot + j.beta + j.gamma * j.accImp);
        j.accImp += imp;
        Vec2 P = j.u * imp;
        A.vel -= P * A.invMass;
        A.w -= A.invI * cross(j.rA, P);
        Bb.vel += P * Bb.invMass;
        Bb.w += Bb.invI * cross(j.rB, P);
    } else {  // mouse
        Vec2 Cdot = A.vel + cross(A.w, j.rA);
        Vec2 rhs = -(Cdot + j.bias * j.beta + j.accP * j.gamma);
        float det = j.k11 * j.k22 - j.k12 * j.k12;
        if (std::fabs(det) < 1e-20f) return;
        det = 1.f / det;
        Vec2 imp((j.k22 * rhs.x - j.k12 * rhs.y) * det, (j.k11 * rhs.y - j.k12 * rhs.x) * det);
        Vec2 old = j.accP;
        j.accP += imp;
        float l = length(j.accP);
        if (l > j.maxImp) j.accP = j.accP * (j.maxImp / l);
        imp = j.accP - old;
        A.vel += imp * A.invMass;
        A.w += A.invI * cross(j.rA, imp);
    }
}

void Physics::solveHydro() {
    for (auto& g : hydro) {
        float D = 0.f, K = 0.f;
        for (const HydroLink& l : g.links) {
            const Body& b = bodies[l.body];
            D += dot(l.a, b.vel);
            K += dot(l.a, l.a) * l.invM;
        }
        if (K < 1e-9f) continue;
        float np = std::max(g.acc + D / K, 0.f);  // pressure can push but never pull
        float dp = np - g.acc;
        g.acc = np;
        for (const HydroLink& l : g.links) {
            Vec2 dv = l.a * (dp * l.invM);
            const Body& rep = bodies[l.body];
            if (rep.group >= 0 && rep.group < (int)groupMem.size())
                for (int m : groupMem[rep.group]) bodies[m].vel -= dv;
            else
                bodies[l.body].vel -= dv;
        }
    }
}

void Physics::substep(float h) {
    // external forces
        for (auto& b : bodies) {
        if (!b.alive || b.isStatic) continue;
        b.vel += gravity * h;
        if (b.wetFrac > 0) {
            // buoyancy: force of displaced fluid applied at the wetted centroid
            Vec2 F = -gravity * (b.fluidRho * b.area * b.wetFrac);
            b.vel += F * (b.invMass * h);
            b.w += b.invI * cross(b.wetCentroid - b.pos, F) * h;
            b.vel *= 1.f / (1.f + 2.5f * b.wetFrac * h);
            b.w *= 1.f / (1.f + 4.0f * b.wetFrac * h);
        }
        if (b.granFrac > 0) {
            b.vel *= 1.f / (1.f + 8.f * b.granFrac * h);
            b.w *= 1.f / (1.f + 10.f * b.granFrac * h);
        }
        b.vel *= 1.f / (1.f + 0.03f * h);
        b.vel += b.fluidF * (b.invMass * h);
        if (b.fluidC > 0.f) {  // implicit damping while pressurised liquid pushes on the body
            float k = 1.f / (1.f + b.fluidC * b.invMass * h);
            b.vel *= k;
            b.w *= k;
        }
        b.w += b.fluidT * b.invI * h;
        if (b.isRocket && thrustOn) {
            Vec2 dir = rotate(Vec2(0, -1), b.angle);
            b.vel += dir * (ROCKET_THRUST * b.invMass * h);
            if (world->chance(0.7f)) {
                int len = 2 + world->rint(8);
                Vec2 p = b.pos - dir * (b.half.y + (float)len) + Vec2((float)world->rint(3) - 1.f, (float)world->rint(3) - 1.f);
                int ix = (int)std::floor(p.x), iy = (int)std::floor(p.y);
                if (world->isFree(ix, iy)) world->spawn(ix, iy, M_FIRE, (uint8_t)(3 + world->rint(5)), 1100.f);
            }
        }
    }

    for (auto& j : joints) if (j.alive) prestepJoint(j, h);
    for (auto& g : hydro) g.acc = 0.f;
    buildContacts();
    for (auto& c : contacts) {
        prestepContact(c, h);
        if (c.a >= 0) bodies[c.a].touching = true;
        if (c.b >= 0) bodies[c.b].touching = true;
    }
    for (int it = 0; it < ITERATIONS; ++it) {
        for (auto& j : joints) if (j.alive) solveJoint(j, h);
        solveHydro();
        for (auto& c : contacts) solveContact(c);
    }
    for (auto& j : joints)
        if (j.alive && j.bondId >= 0) j.peak = std::max(j.peak, length(j.accP) / h);

    for (auto& b : bodies) {
        if (!b.alive || b.isStatic) continue;
        float sp = length(b.vel);
        if (sp > 900.f) b.vel *= 900.f / sp;
        b.w = std::clamp(b.w, -60.f, 60.f);
        b.pos += b.vel * h;
        b.angle += b.w * h;
        if (!std::isfinite(b.pos.x) || !std::isfinite(b.pos.y) || b.pos.y > World::H + 200 || b.pos.y < -400 ||
            b.pos.x < -200 || b.pos.x > World::W + 200)
            removeBody(b.id);
        else if (b.touching && !b.hasJoint && lengthSq(b.vel) < 0.04f && b.w * b.w < 0.0004f) { b.vel = Vec2(); b.w = 0; }
    }
}

void Physics::step(float dt) {
    emitSources(dt);
    applyFans(dt);
    applyBlasts();
    fluidForces();
    sampleFluids();
    noCollide.clear();
    for (auto& b : bodies) b.hasJoint = false;
    for (auto& j : joints)
        if (j.alive && j.type != J_MOUSE) {
            if (j.a >= 0) bodies[j.a].hasJoint = true;
            if (j.b >= 0) bodies[j.b].hasJoint = true;
        }
    std::vector<std::vector<int>> groups;
    for (auto& b : bodies)
        if (b.alive && b.group >= 0) {
            if ((int)groups.size() <= b.group) groups.resize(b.group + 1);
            groups[b.group].push_back(b.id);
        }
    for (auto& g : groups)  // members of one group never collide with each other
        for (size_t i = 0; i < g.size(); ++i)
            for (size_t k = i + 1; k < g.size(); ++k) noCollide.push_back(pairKey(g[i], g[k]));
    auto expand = [&](int id) {
        std::vector<int> r;
        if (bodies[id].group >= 0) r = groups[bodies[id].group]; else r.push_back(id);
        return r;
    };
    for (auto& j : joints)
        if (j.alive && j.group < 0 && (j.type == J_PIN || j.type == J_MOTOR || j.type == J_DISTANCE) && j.b >= 0) {
            // a joint between members of two groups frees the whole groups from colliding (hose joints)
            for (int x : expand(j.a)) for (int y : expand(j.b)) noCollide.push_back(pairKey(x, y));
        }
    std::sort(noCollide.begin(), noCollide.end());
    noCollide.erase(std::unique(noCollide.begin(), noCollide.end()), noCollide.end());
    float h = dt / SUBSTEPS;
    hits.clear(); primerStrikes.clear();
    for (auto& j : joints) j.peak = 0.f;
    for (int s = 0; s < SUBSTEPS; ++s) substep(h);
    for (auto& j : joints) if (j.alive && j.bondId >= 0) j.fAvg += 0.2f * (j.peak - j.fAvg);   // ~0.1 s of memory: a jolt is not a failure
    processEvents(dt);
    thermalStep();
    stampBodies();
}

// ------------------------------------------------------------------ bonds, primers
float Physics::groupMass(int id) const {
    const Body& b = bodies[id];
    if (b.group < 0) return b.mass;
    double m = 0;
    for (auto& o : bodies) if (o.alive && o.group == b.group) m += o.mass;
    return (float)m;
}
float Physics::bondMass(const Joint& j) const {
    auto mass = [&](int i) { return (i < 0 || !bodies[i].alive || bodies[i].isStatic) ? 1e12f : groupMass(i); };
    return std::min(mass(j.a), mass(j.b));
}

int Physics::addBond(Vec2 anchor, int a, int b, float breakT, float breakF, float loadG) {
    if (a < 0) std::swap(a, b);
    if (a < 0) return -1;
    Vec2 d = rotate(Vec2(4.f, 0.f), bodies[a].angle);
    int id = bondCounter++;
    for (int k = -1; k <= 1; k += 2) {
        int j = addPin(anchor + d * (float)k, a, b, false, false);
        if (j < 0) continue;
        joints[j].bondId = id; joints[j].breakT = breakT; joints[j].breakF = breakF; joints[j].loadG = loadG;
    }
    return id;
}

void Physics::processEvents(float dt) {
    if (eventFrames > 0) --eventFrames;
    // primers: a hard enough blow fires the body once; the flash leaves from the end opposite the strike
    for (auto& h : hits) {
        Body& b = bodies[h.body];
        if (!b.alive || b.spent) continue;
        b.spent = true;
        b.color = 0x6a5a40;
        flashes.push_back({b.id, b.toLocal(h.p), 8});
        lastEvent = "PRIMER FIRED"; eventFrames = 240;
    }
    for (auto& s : primerStrikes) { world->primerStrike(s.first, s.second); lastEvent = "PRIMER STRUCK"; eventFrames = 240; }
    for (auto& f : flashes) {
        if (f.body < 0 || f.body >= (int)bodies.size() || !bodies[f.body].alive) { f.frames = 0; continue; }
        const Body& b = bodies[f.body];
        --f.frames;
        Vec2 outDir;
        float reach;
        if (b.shape == SHAPE_BOX) {
            int axis = b.half.x >= b.half.y ? 0 : 1;
            float sgn = (axis == 0 ? f.lp.x : f.lp.y) >= 0 ? -1.f : 1.f;
            outDir = axis == 0 ? Vec2(sgn, 0) : Vec2(0, sgn);
            reach = axis == 0 ? b.half.x : b.half.y;
            Vec2 perp = axis == 0 ? Vec2(0, 1) : Vec2(1, 0);
            float ph = axis == 0 ? b.half.y : b.half.x;
            for (int o = -2; o <= 2; ++o)
                for (float d : {1.2f, 2.4f}) {
                    Vec2 w = b.toWorld(outDir * (reach + d) + perp * (ph * 0.45f * (float)o));
                    world->flashAt((int)std::floor(w.x), (int)std::floor(w.y));
                }
        } else {
            outDir = f.lp.x * f.lp.x + f.lp.y * f.lp.y > 1e-4f ? normalize(-f.lp) : Vec2(0, -1);
            for (int o = -2; o <= 2; ++o) {
                Vec2 dir = rotate(outDir, 0.3f * (float)o);
                for (float d : {1.2f, 2.4f}) {
                    Vec2 w = b.toWorld(dir * (b.radius + d));
                    world->flashAt((int)std::floor(w.x), (int)std::floor(w.y));
                }
            }
        }
    }
    flashes.erase(std::remove_if(flashes.begin(), flashes.end(), [](const Flash& f) { return f.frames <= 0; }), flashes.end());

    // frangible bonds: they let go when the seam gets too hot, or when the sustained load exceeds what they can hold.
    // A bond rated in g holds that many times the weight it carries; it also softens as it nears its melting point.
    std::vector<int> broken;
    const float grav = std::max(60.f, std::fabs(gravity.y));
    for (auto& j : joints) {
        if (!j.alive || j.bondId < 0) continue;
        if (std::find(broken.begin(), broken.end(), j.bondId) != broken.end()) continue;
        float F = 0.f, T = bodies[j.a].temp;
        for (auto& k : joints)
            if (k.alive && k.bondId == j.bondId) F += k.fAvg;
        if (j.b >= 0) T = std::max(T, bodies[j.b].temp);
        // the seam itself: flame, hot gas or molten metal within a few cells of either pin warms it directly
        {
            Vec2 w = jointAnchorA(j);
            for (int dy = -3; dy <= 3; ++dy)
                for (int dx = -3; dx <= 3; ++dx) {
                    int ix = (int)std::floor(w.x) + dx, iy = (int)std::floor(w.y) + dy;
                    if (!world->inb(ix, iy)) continue;
                    if (world->bodyMask[iy * World::W + ix] >= 0 || world->at(ix, iy).t == M_EMPTY) continue;
                    T = std::max(T, world->at(ix, iy).temp);
                }
        }
        float allowed = j.loadG > 0.f ? j.loadG * bondMass(j) * grav : j.breakF;
        const float band = std::max(5.f, 0.5f * (j.breakT - 20.f));
        allowed *= std::clamp((j.breakT - T) / band, 0.f, 1.f);                           // softens towards the melting point
        if (F > allowed || T >= j.breakT) {
            broken.push_back(j.bondId);
            lastEvent = T >= j.breakT ? "BOND MELTED" : (T > j.breakT - band ? "BOND SOFTENED AND GAVE WAY" : "BOND BROKE (LOAD)");
            eventFrames = 240;
        }
    }
    for (int id : broken) {
        for (auto& k : joints) if (k.alive && k.bondId == id) k.alive = false;
        ++bondsBroken;
    }
}

// ------------------------------------------------------------------ fans
// Gas has no velocity of its own in the grid model, so a fan moves gas explicitly: along lanes of cells that run
// out of the body's exhaust face and back from its intake face, each frame some gas hops one cell down the lane
// (through the fan itself). Pressure dynamics come from the rest of the gas model: the moved gas piles up
// ahead of the fan and drains from behind it, and a fan curve lowers the flow as the pressure it works
// against approaches its stall pressure. The intake draws in fresh ambient air.
// Air an intake draws in is thin, like the ambient gas the grid actually holds: a dense intake builds a pressure cloud that shoves the machine back
static float intakeAmt() { return 0.25f; }
void Physics::applyFans(float dt) {
    constexpr float THRUST_K = 300.f, WIND_K = 40.f;
    World& w = *world;
    auto psi = [&](const Cell& c) { return MATS[c.t].kind == K_GAS ? c.amt * (c.temp + 273.f) / 293.f : 0.f; };
    for (size_t bi = 0; bi < bodies.size(); ++bi) {
        Body& b = bodies[bi];
        if (!b.alive || b.shape != SHAPE_BOX || b.fan.strength == 0.f) continue;
        const float s = b.fan.strength, mag = std::fabs(s);
        const Vec2 axis = rotate(Vec2(s > 0 ? 1.f : -1.f, 0.f), b.angle);
        const Vec2 lat(-axis.y, axis.x);
        const float R = std::clamp(10.f + mag * 0.25f, 10.f, 60.f);   // reach of the stream
        const bool vac = b.fan.vacuum != 0;
        const int Rin = vac ? std::clamp((int)(R * 1.5f), 12, 60) : std::clamp((int)(R * 0.6f), 6, 36);   // suction reaches back behind the fan
        const float tFace = b.half.x + 0.5f;
        const int lanes = std::max(1, (int)std::floor(2.f * b.half.y));

        // lane cells, behind-most first, ahead-most last
        std::vector<std::vector<int>> lane(lanes);
        std::vector<int> split(lanes, 0);  // index of the first cell ahead of the fan
        for (int l = 0; l < lanes; ++l) {
            float off = ((float)l + 0.5f) - (float)lanes * 0.5f;
            auto cellAt = [&](float t) { Vec2 p = b.pos + axis * t + lat * off; return Vec2(std::floor(p.x), std::floor(p.y)); };
            std::vector<int> behind, ahead;
            auto walk = [&](bool fwd, int count, std::vector<int>& out) {
                int last = -1;
                for (int j = 0; j < count; ++j) {
                    Vec2 c = cellAt(fwd ? tFace + j : -(tFace + j));
                    int ix = (int)c.x, iy = (int)c.y;
                    if (!w.inb(ix, iy)) break;
                    int i = iy * World::W + ix;
                    if (i == last) continue;
                    int m = w.bodyMask[i];
                    if (m >= 0 && m != b.id) break;                       // another body blocks the stream
                    Kind k = MATS[w.cells[i].t].kind;
                    if (m < 0 && k != K_EMPTY && k != K_GAS) break;       // solids, powders and liquids block it
                    if (m == b.id) continue;                              // the fan's own cover cells
                    out.push_back(i);
                    last = i;
                }
            };
            walk(true, (int)R, ahead);
            walk(false, Rin, behind);
            for (int i = (int)behind.size() - 1; i >= 0; --i) lane[l].push_back(behind[i]);
            split[l] = (int)lane[l].size();
            for (int i : ahead) lane[l].push_back(i);
        }

        // fan curve: the flow falls to zero as the pressure rise it works against reaches the stall pressure
        float pIn = 0, pOut = 0; int nIn = 0, nOut = 0;
        for (int l = 0; l < lanes; ++l) {
            for (int k = std::max(0, split[l] - 3); k < split[l]; ++k) { pIn += psi(w.cells[lane[l][k]]); ++nIn; }
            for (int k = split[l]; k < std::min((int)lane[l].size(), split[l] + 4); ++k) { pOut += psi(w.cells[lane[l][k]]); ++nOut; }
        }
        pIn = nIn ? pIn / nIn : 0.f;
        pOut = nOut ? pOut / nOut : 0.f;
        const float stall = 0.02f * mag;
        const float flow = std::clamp(1.f - (pOut - pIn) / std::max(0.05f, stall), 0.f, 1.f);

        const float v = mag / 60.f * flow;                 // cells per frame
        const float peak = vac ? 1.9f : 1.f;               // a vacuum fan accelerates the gas through the throat
        const int hops = std::max(1, (int)std::ceil(v * peak));
        const float pHop = std::min(1.f, v / hops);
        for (int l = 0; l < lanes; ++l) {
            std::vector<int>& L = lane[l];
            if (L.empty()) continue;
            for (int h = 0; h < hops; ++h) {
                // intake (blower mode): ambient air appears at the back end of the lane and is drawn towards the fan;
                // a vacuum fan only draws what is already there
                if (!vac && split[l] >= Rin - 2 && w.chance(pHop)) {   // (an obstructed intake draws nothing in)
                    int i = L[0];
                    bool nearBody = false;   // no air appears right against a body (it would shove it away)
                    for (int dy = -3; dy <= 3 && !nearBody; ++dy)
                        for (int dx = -3; dx <= 3 && !nearBody; ++dx) {
                            int nx = i % World::W + dx, ny = i / World::W + dy;
                            if (w.inb(nx, ny)) { int m = w.bodyMask[ny * World::W + nx]; nearBody = m >= 0 && m != b.id && !(b.group >= 0 && bodies[m].group == b.group); }
                        }
                    Cell& in = w.cells[i];
                    if (nearBody) {
                    } else if (in.t == M_EMPTY) {
                        w.setCell(i % World::W, i / World::W, M_AIR);
                        w.cells[i].amt = intakeAmt();
                        w.cells[i].life = 1;   // ambient air from an intake: it may thin out at the open void
                    } else if (in.t == M_AIR && in.amt < intakeAmt()) {   // the intake is open to ambient air: it tops the cell up
                        in.temp = (in.temp * in.amt + AMBIENT_T * (1.f - in.amt)) ;
                        in.temp = std::clamp(in.temp, -100.f, 2000.f);
                        in.amt = std::max(in.amt, intakeAmt());
                    }
                }
                for (int k = (int)L.size() - 2; k >= 0; --k) {
                    Cell& a = w.cells[L[k]];
                    float prof = 1.f;   // vacuum: slow far upstream, quickening towards the fan, fastest leaving it
                    if (vac) {
                        if (k < split[l]) prof = 0.35f + 0.65f * (1.f - std::min(1.f, (float)(split[l] - 1 - k) / (float)Rin));
                        else prof = 1.f + 0.9f * std::max(0.f, 1.f - (float)(k - split[l]) / (R * 0.5f));
                    }
                    if (MATS[a.t].kind != K_GAS) continue;
                    int zone = k >= split[l] ? 2 : (split[l] - 1 - k) * 2 < Rin ? 1 : 0;
                    ++fanHopTries[zone];
                    if (!w.chance(std::min(1.f, pHop * prof))) continue;
                    ++fanHopMoves[zone];
                    Cell& c = w.cells[L[k + 1]];
                    if (MATS[c.t].kind == K_GAS && psi(c) > psi(a) + 0.12f) continue;   // the stream cannot be pumped uphill: gas only moves on while the way ahead is not at a higher pressure
                    if (c.t == M_EMPTY) { c = a; a = Cell{}; }
                    else if (MATS[c.t].kind == K_GAS) {
                        if (c.t == a.t) {   // same gas: carry half over, mixing the temperatures
                            float m = a.amt * 0.5f;
                            c.temp = (c.temp * c.amt + a.temp * m) / std::max(1e-4f, c.amt + m);
                            c.amt += m; a.amt -= m;
                            if (a.amt < 0.02f) a = Cell{};
                        } else std::swap(a, c);
                    }
                }
            }
        }

        // the stream does not stop dead at the end of its lane: the last cell sheds gas forwards and to the sides, like the
        // start of a free jet (a closed end is a wall, so the gas still piles up there as before)
        for (int l = 0; l < lanes; ++l) {
            const std::vector<int>& L = lane[l];
            if (L.size() < 3) continue;
            int last = L.back(), prev = L[L.size() - 2];
            int fx = last % World::W - prev % World::W, fy = last / World::W - prev / World::W;
            if (fx == 0 && fy == 0) continue;
            fx = (fx > 0) - (fx < 0); fy = (fy > 0) - (fy < 0);
            Cell& e = w.cells[last];
            if (MATS[e.t].kind != K_GAS || e.amt < 0.05f || !w.chance(std::min(1.f, v * 0.9f))) continue;
            const int pick = (int)(w.rnd() % 3u) - 1;   // -1, 0, +1: left, straight, right
            int nx = last % World::W + fx - fy * pick, ny = last / World::W + fy + fx * pick;
            if (!w.inb(nx, ny) || w.bodyMask[ny * World::W + nx] >= 0) continue;
            Cell& t = w.cells[ny * World::W + nx];
            if (t.t == M_EMPTY) { t = e; t.amt = e.amt * 0.5f; e.amt *= 0.5f; if (t.amt < 0.02f) t = Cell{}; }
            else if (t.t == e.t && t.amt < e.amt) { float m = (e.amt - t.amt) * 0.5f; t.temp = (t.temp * t.amt + e.temp * m) / std::max(1e-4f, t.amt + m); t.amt += m; e.amt -= m; }
        }

        // reaction on the fan, and wind on bodies in the stream
        // Gas carries no momentum in the grid, so the reaction on the fan is modelled from what the fan does: its throughput,
        // times how fast the exhaust leaves. Heat added downstream (a burner in the duct) speeds the exhaust up, as
        // v ~ sqrt(T), so a lit engine pushes harder than a cold fan: thrust = throughput x sqrt(exhaust T / ambient T).
        float exhaustBoost = 1.f, hotT = AMBIENT_T;
        {
            // temperature of the hottest gas in the stream (the flame): the mean of its 12 hottest cells
            std::vector<float> temps;
            for (int l = 0; l < lanes; ++l)
                for (size_t k = (size_t)split[l] + 2; k < lane[l].size(); ++k) {
                    const Cell& c = w.cells[lane[l][k]];
                    if (MATS[c.t].kind == K_GAS && c.amt > 0.05f) temps.push_back(c.temp);
                }
            const size_t topN = std::min<size_t>(12, temps.size());
            if (topN >= 3) {
                std::partial_sort(temps.begin(), temps.begin() + (long)topN, temps.end(), std::greater<float>());
                double sum = 0; for (size_t i = 0; i < topN; ++i) sum += temps[i];
                hotT = (float)(sum / topN);
                exhaustBoost = std::clamp(std::sqrt((hotT + 273.f) / (AMBIENT_T + 273.f)), 1.f, 3.f);
            }
        }
        if (!b.isStatic) b.vel += axis * (-THRUST_K * mag * flow * exhaustBoost) * (b.invMass * dt);
        for (auto& o : bodies) {
            if (!o.alive || o.isStatic || o.id == b.id || (b.group >= 0 && o.group == b.group)) continue;   // a fan doesn't blow on its own machine
            Vec2 rel = o.pos - b.pos;
            float t = dot(rel, axis), u = dot(rel, lat), reach = o.bound;
            if (std::fabs(u) > b.half.y + reach) continue;
            float d, strength;
            bool behind = false;
            const float backReach = vac ? (float)Rin : R * 0.5f;
            if (t > b.half.x && t < b.half.x + R) { d = (t - b.half.x) / R; strength = 1.f; }
            else if (t < -b.half.x && t > -b.half.x - backReach) { d = (-t - b.half.x) / backReach; strength = vac ? 0.9f : 0.35f; behind = true; }  // suction draws bodies in
            else continue;
            Vec2 p = o.pos - axis * (behind ? -(reach + 1.5f) : (reach + 1.5f));
            int ix = (int)std::floor(p.x), iy = (int)std::floor(p.y);
            float dens = 0.2f;
            if (w.inb(ix, iy) && MATS[w.cells[iy * World::W + ix].t].kind == K_GAS) dens = std::clamp(w.cells[iy * World::W + ix].amt, 0.2f, 1.5f);
            // sideways drift towards the axis keeps light bodies in the stream
            float F = WIND_K * mag * flow * (1.f - d) * 2.f * reach * dens * strength;
            o.vel += (axis * F + lat * (-u * 0.4f * F * 0.05f)) * (o.invMass * dt);
        }
    }
}

// ------------------------------------------------------------------ emitters
bool Physics::emitOne(Body& b) {
    const Emitter& e = b.src;
    for (int tries = 0; tries < 20; ++tries) {
        auto rf = [&]() { return (float)(world->rnd() & 0xFFFF) / 65535.f; };
        Vec2 lp;
        if (b.shape == SHAPE_BOX) {
            int f = e.face;
            if (f == 0) {  // choose a face in proportion to its length
                float px = b.half.y * 2, py = b.half.x * 2, r = rf() * (2 * px + 2 * py);
                f = r < px ? 1 : r < 2 * px ? 2 : r < 2 * px + py ? 3 : 4;
            }
            float u = (rf() * 2.f - 1.f) * 0.9f, off = 1.1f;
            if (f == 1) lp = Vec2(b.half.x + off, u * b.half.y);
            else if (f == 2) lp = Vec2(-b.half.x - off, u * b.half.y);
            else if (f == 3) lp = Vec2(u * b.half.x, b.half.y + off);
            else lp = Vec2(u * b.half.x, -b.half.y - off);
        } else {
            float base = e.face == 1 ? 0.f : e.face == 2 ? PI : e.face == 3 ? PI * 0.5f : -PI * 0.5f;
            float a = e.face == 0 ? rf() * 2 * PI : base + (rf() - 0.5f) * 1.2f;
            lp = Vec2(std::cos(a), std::sin(a)) * (b.radius + 1.1f);
        }
        Vec2 w = b.toWorld(lp);
        int x = (int)std::floor(w.x), y = (int)std::floor(w.y);
        if (!world->inb(x, y) || world->bodyMask[y * World::W + x] >= 0) continue;
        Cell& c = world->at(x, y);
        if (c.t == M_EMPTY) {
            world->setCell(x, y, e.mat);
            if (MATS[e.mat].kind == K_GAS) world->at(x, y).amt = 1.f;
            return true;
        }
        if (MATS[e.mat].kind == K_GAS && c.t == e.mat && c.amt < 1.f) { c.amt = std::min(1.f, c.amt + 0.5f); return true; }
        if (MATS[c.t].kind == K_GAS && c.t != e.mat && c.t != M_FIRE) {   // an outlet in a gas-filled space (a duct full of air) still pushes its material out, displacing the gas
            world->setCell(x, y, e.mat);
            if (MATS[e.mat].kind == K_GAS) world->at(x, y).amt = 1.f;
            return true;
        }
    }
    return false;
}

void Physics::emitSources(float dt) {
    for (auto& b : bodies) {
        if (!b.alive || !b.src.on) continue;
        Emitter& e = b.src;
        e.accum = std::min(e.accum + e.rate * dt, 3.f);   // a blocked outlet does not store up a burst
        while (e.accum >= 1.f) {
            if (!emitOne(b)) break;
            e.accum -= 1.f;
        }
    }
}

// ------------------------------------------------------------------ scale / cut
void Physics::scaleBodies(const std::vector<int>& ids, float s, Vec2 pivot) {
    s = std::clamp(s, 0.05f, 10.f);
    std::vector<Body> old(bodies.size());
    std::vector<char> in(bodies.size(), 0);
    std::vector<int> groups;
    for (int id : ids) {
        if (id < 0 || id >= (int)bodies.size() || !bodies[id].alive || in[id]) continue;
        in[id] = 1;
        old[id] = bodies[id];
        Body& b = bodies[id];
        b.pos = pivot + (b.pos - pivot) * s;
        if (b.shape == SHAPE_BOX) b.half = b.half * s; else b.radius *= s;
        b.vel = Vec2(); b.w = 0;
        finalize(b);
        if (b.group >= 0 && std::find(groups.begin(), groups.end(), b.group) == groups.end()) groups.push_back(b.group);
    }
    for (auto& j : joints) {
        if (!j.alive || j.group >= 0 || j.type == J_MOUSE) continue;
        bool ia = j.a >= 0 && in[j.a], ib = j.b >= 0 && in[j.b];
        if (!ia && !ib) continue;
        if (j.type == J_SLIDER && j.b < 0) { if (ia) j.lb += bodies[j.a].pos - old[j.a].pos; continue; }
        if (ia && ib) {  // both ends scale: anchors scale with them
            j.la = j.la * s; j.lb = j.lb * s;
            if (j.type == J_DISTANCE) j.length *= s;
            continue;
        }
        if (ia) j.la = bodies[j.a].toLocal(old[j.a].toWorld(j.la));   // one end fixed: keep the joint in place
        if (ib) j.lb = bodies[j.b].toLocal(old[j.b].toWorld(j.lb));
    }
    for (int g : groups) rebuildGroup(g);
}

int Physics::cutBody(int target, const std::vector<int>& cutters) {
    if (target < 0 || target >= (int)bodies.size() || !bodies[target].alive) return -1;
    const Body tc = bodies[target];
    if (tc.isWheel || tc.isRocket || tc.src.on || tc.fan.strength != 0.f) return -1;
    std::vector<const Body*> cut;
    for (int c : cutters)
        if (c >= 0 && c < (int)bodies.size() && bodies[c].alive && c != target) cut.push_back(&bodies[c]);
    if (cut.empty()) return -1;
    auto inCutter = [&](Vec2 lp) {
        Vec2 w = tc.toWorld(lp);
        for (auto* c : cut) if (c->contains(w)) return true;
        return false;
    };
    struct R { int i0, i1, j0, j1; };
    Vec2 ext = tc.shape == SHAPE_BOX ? tc.half : Vec2(tc.radius, tc.radius);
    std::vector<R> rects;
    long removed = 0;
    for (float res : {0.5f, 1.f, 2.f, 4.f, 8.f}) {
        int nx = std::max(1, (int)std::ceil(ext.x * 2 / res)), ny = std::max(1, (int)std::ceil(ext.y * 2 / res));
        float cx = ext.x * 2 / nx, cy = ext.y * 2 / ny;
        std::vector<char> keep((size_t)nx * ny, 0);
        removed = 0;
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                float x0 = -ext.x + i * cx, y0 = -ext.y + j * cy;
                bool inside = true, hit = false;
                for (int k = 0; k < 9; ++k) {   // corners, edge midpoints and centre
                    Vec2 p(x0 + (k % 3) * cx * 0.5f, y0 + (k / 3) * cy * 0.5f);
                    if (tc.shape == SHAPE_CIRCLE && lengthSq(p) > tc.radius * tc.radius + 1e-3f) { inside = false; break; }
                    if (inCutter(p)) hit = true;
                }
                if (!inside) continue;
                if (hit) { ++removed; continue; }
                keep[(size_t)j * nx + i] = 1;
            }
        if (removed == 0) return -1;
        // merge: horizontal runs, then identical runs on consecutive rows
        rects.clear();
        std::vector<int> open;  // indices into rects still growing
        for (int j = 0; j < ny; ++j) {
            std::vector<int> nextOpen;
            int i = 0;
            while (i < nx) {
                if (!keep[(size_t)j * nx + i]) { ++i; continue; }
                int a = i;
                while (i < nx && keep[(size_t)j * nx + i]) ++i;
                int found = -1;
                for (int r : open) if (rects[r].i0 == a && rects[r].i1 == i && rects[r].j1 == j) { found = r; break; }
                if (found >= 0) { rects[found].j1 = j + 1; nextOpen.push_back(found); }
                else { rects.push_back({a, i, j, j + 1}); nextOpen.push_back((int)rects.size() - 1); }
            }
            open = nextOpen;
        }
        if (rects.size() <= 300) {
            // convert cell rects to local boxes below
            std::vector<int> pieces;
            for (const R& r : rects) {
                Vec2 lc(-ext.x + (r.i0 + r.i1) * 0.5f * cx, -ext.y + (r.j0 + r.j1) * 0.5f * cy);
                Vec2 h((r.i1 - r.i0) * cx * 0.5f, (r.j1 - r.j0) * cy * 0.5f);
                int id = addBox(tc.toWorld(lc), h, tc.angle, tc.mat, tc.isStatic);
                Body& nb = bodies[id];
                nb.temp = tc.temp; nb.color = tc.color;
                nb.vel = tc.vel + cross(tc.w, nb.pos - tc.pos);
                nb.w = tc.w;
                pieces.push_back(id);
            }
            // joints that were attached to the target move to the piece nearest their anchor
            for (auto& j : joints) {
                if (!j.alive || j.group >= 0 || (j.a != target && j.b != target)) continue;
                if (j.type == J_MOUSE) { j.alive = false; continue; }
                if (pieces.empty()) { j.alive = false; continue; }
                bool isA = j.a == target;
                Vec2 anchor = tc.toWorld(isA ? j.la : j.lb);
                if (j.type == J_SLIDER && j.b < 0) anchor = tc.pos;
                int best = pieces[0];
                float bd = 1e30f;
                for (int pid : pieces) {
                    const Body& pb = bodies[pid];
                    float d = pb.contains(anchor) ? -1.f : lengthSq(pb.pos - anchor);
                    if (d < bd) { bd = d; best = pid; }
                }
                if (j.type == J_SLIDER && j.b < 0) { j.lb += bodies[best].pos - tc.pos; j.a = best; continue; }
                (isA ? j.a : j.b) = best;
                (isA ? j.la : j.lb) = bodies[best].toLocal(anchor);
            }
            bodies[target].alive = false;
            for (auto& j : joints) if (j.alive && (j.a == target || j.b == target)) j.alive = false;
            std::vector<int> all = pieces;
            if (tc.group >= 0) for (int m : groupMembers(tc.group)) all.push_back(m);
            if (all.size() >= 2) groupBodies(all);
            else if (tc.group >= 0) rebuildGroup(tc.group);
            return (int)pieces.size();
        }
    }
    return -1;  // too intricate even at the coarsest step
}

// ------------------------------------------------------------------ groups
std::vector<int> Physics::groupMembers(int g) const {
    std::vector<int> r;
    if (g < 0) return r;
    for (auto& b : bodies) if (b.alive && b.group == g) r.push_back(b.id);
    return r;
}

// A weld = two pins at distinct points, which together lock all three degrees of freedom.
void Physics::weldPair(int root, int member) {
    Vec2 p1 = bodies[root].pos, p2 = bodies[member].pos;
    if (length(p2 - p1) < 4.f) p2 = p1 + rotate(Vec2(4.f, 0.f), bodies[root].angle);
    int j1 = addPin(p1, root, member, false, false);
    int j2 = addPin(p2, root, member, false, false);
    if (j1 >= 0) joints[j1].group = bodies[root].group;
    if (j2 >= 0) joints[j2].group = bodies[root].group;
}

void Physics::rebuildGroup(int g) {
    for (auto& j : joints) if (j.alive && j.group == g) j.alive = false;
    std::vector<int> m = groupMembers(g);
    if (m.size() < 2) { for (int id : m) bodies[id].group = -1; return; }
    for (size_t i = 1; i < m.size(); ++i) weldPair(m[0], m[i]);
}

int Physics::groupBodies(const std::vector<int>& ids) {
    std::vector<int> gs;
    int target = -1;
    for (int id : ids) {
        if (id < 0 || id >= (int)bodies.size() || !bodies[id].alive) continue;
        if (bodies[id].group >= 0) { target = target < 0 ? bodies[id].group : std::min(target, bodies[id].group); }
    }
    if (target < 0) target = groupCounter++;
    for (int id : ids) {
        if (id < 0 || id >= (int)bodies.size() || !bodies[id].alive) continue;
        int old = bodies[id].group;
        if (old >= 0 && old != target)  // merge the whole old group
            for (auto& b : bodies) if (b.alive && b.group == old) b.group = target;
        bodies[id].group = target;
    }
    for (auto& j : joints) if (j.alive && j.group >= 0 && bodies[j.a].group != j.group) j.alive = false;
    rebuildGroup(target);
    return target;
}

void Physics::ungroup(int g) {
    for (auto& j : joints) if (j.alive && j.group == g) j.alive = false;
    for (auto& b : bodies) if (b.alive && b.group == g) b.group = -1;
}

void Physics::reshape(int id, Vec2 pos, Vec2 half, float radius, float angle, uint8_t mat, bool stat) {
    if (id < 0 || id >= (int)bodies.size() || !bodies[id].alive) return;
    Body old = bodies[id];
    Body& b = bodies[id];
    b.pos = pos; b.angle = angle;
    if (b.shape == SHAPE_BOX) b.half = Vec2(std::max(0.5f, half.x), std::max(0.5f, half.y));
    else b.radius = std::max(0.5f, radius);
    if (mat != b.mat) { b.mat = mat; b.color = MATS[mat].color; b.temp = MATS[mat].initT; }
    b.isStatic = stat;
    b.vel = Vec2(); b.w = 0;
    finalize(b);
    for (auto& j : joints) {
        if (!j.alive || j.group >= 0 || j.type == J_MOUSE) continue;
        if (j.type == J_SLIDER && j.b < 0) {
            if (j.a == id) { j.lb += pos - old.pos; j.length += angle - old.angle; }
            continue;
        }
        if (j.type == J_SLIDER) { if (j.a == id) j.length += angle - old.angle; if (j.b == id) j.length -= angle - old.angle; }
        if (j.a == id) j.la = b.toLocal(old.toWorld(j.la));   // keep the joint where it was in the world
        if (j.b == id) j.lb = b.toLocal(old.toWorld(j.lb));
    }
    if (b.group >= 0) rebuildGroup(b.group);
}

void Physics::transformGroup(int primary, Vec2 newPos, float newAngle) {
    if (primary < 0 || primary >= (int)bodies.size() || !bodies[primary].alive) return;
    Vec2 oldPos = bodies[primary].pos;
    float dA = newAngle - bodies[primary].angle;
    std::vector<int> m = bodies[primary].group >= 0 ? groupMembers(bodies[primary].group) : std::vector<int>{primary};
    for (int id : m) {
        Body& b = bodies[id];
        b.pos = newPos + rotate(b.pos - oldPos, dA);
        b.angle += dA;
        b.vel = Vec2(); b.w = 0;
    }
    for (auto& j : joints)  // pins to the world travel with their body
        if (j.alive && j.group < 0 && j.b < 0 && j.a >= 0 && j.type != J_SLIDER && j.type != J_MOUSE &&
            std::find(m.begin(), m.end(), j.a) != m.end())
            j.lb = bodies[j.a].toWorld(j.la);
}

int Physics::addPipe(Vec2 a, Vec2 b, float outerD, float wall, uint8_t mat, bool stat) {
    Vec2 d = b - a;
    float L = length(d);
    if (L < 2.f) return -1;
    float ang = std::atan2(d.y, d.x);
    Vec2 mid = (a + b) * 0.5f, nrm(-d.y / L, d.x / L);
    wall = std::clamp(wall, 0.5f, outerD * 0.5f);
    float off = outerD * 0.5f - wall * 0.5f;
    int w1 = addBox(mid + nrm * off, Vec2(L * 0.5f, wall * 0.5f), ang, mat, stat);
    int w2 = addBox(mid - nrm * off, Vec2(L * 0.5f, wall * 0.5f), ang, mat, stat);
    return groupBodies({w1, w2});
}

int Physics::addHose(Vec2 a, Vec2 b, float outerD, float wall, int segments, uint8_t mat, bool stat) {
    Vec2 d = b - a;
    float L = length(d);
    if (L < 4.f) return -1;
    segments = std::clamp(segments, 2, 60);
    float seg = L / segments, overlap = outerD * 0.5f;
    Vec2 dir = d / L;
    int first = -1, prevBody = -1;
    for (int i = 0; i < segments; ++i) {
        Vec2 s0 = a + dir * (seg * i), s1 = a + dir * (seg * (i + 1));
        // extend every segment (except at the hose ends) so neighbours overlap and a bend leaves no gap
        if (i > 0) s0 -= dir * (overlap * 0.5f);
        if (i < segments - 1) s1 += dir * (overlap * 0.5f);
        int g = addPipe(s0, s1, outerD, wall, mat, stat);
        if (g < 0) continue;
        if (first < 0) first = g;
        int body = groupMembers(g)[0];
        if (prevBody >= 0) addPin(a + dir * (seg * i), prevBody, body, false, false);
        prevBody = body;
    }
    return first;
}

void Physics::dumpContacts(int body) const {
    for (const Contact& c : contacts)
        if (c.a == body || c.b == body)
            std::printf("   contact a=%d b=%d p=(%.1f,%.1f) n=(%.2f,%.2f) depth=%.2f jn=%.0f\n", c.a, c.b, c.p.x, c.p.y, c.n.x, c.n.y, c.depth, c.jn);
}
