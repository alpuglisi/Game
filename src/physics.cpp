#include "physics.hpp"
#include <algorithm>
#include <cmath>

namespace {
constexpr float PI = 3.14159265f;
constexpr int SUBSTEPS = 4;
constexpr int ITERATIONS = 8;
constexpr float SLOP = 0.4f;
constexpr float BAUMGARTE = 0.2f;
constexpr float MAX_BIAS = 60.f;

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
    b.mass = b.density * b.area;
    float inertia = b.shape == SHAPE_BOX ? b.mass * (4 * b.half.x * b.half.x + 4 * b.half.y * b.half.y) / 12.f
                                         : 0.5f * b.mass * b.radius * b.radius;
    if (b.isStatic) { b.invMass = 0; b.invI = 0; }
    else { b.invMass = 1.f / b.mass; b.invI = 1.f / inertia; }
}

int Physics::addBox(Vec2 c, Vec2 half, float angle, float density, bool stat) {
    int id = allocBody();
    Body& b = bodies[id];
    b.shape = SHAPE_BOX; b.pos = c; b.half = half; b.angle = angle; b.density = density; b.isStatic = stat;
    finalize(b);
    return id;
}

int Physics::addCircle(Vec2 c, float r, float density, bool stat, bool wheel) {
    int id = allocBody();
    Body& b = bodies[id];
    b.shape = SHAPE_CIRCLE; b.pos = c; b.radius = r; b.density = density; b.isStatic = stat; b.isWheel = wheel;
    finalize(b);
    return id;
}

int Physics::addRocket(Vec2 c, float angle) {
    int id = addBox(c, Vec2(4.f, 8.f), angle, 1.5f, false);
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
        if (!j.alive || j.type == J_MOUSE) continue;
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
    static std::vector<std::pair<int, int>> offsets;
    if (offsets.empty()) {
        const int R = 14;
        for (int dy = -R; dy <= R; ++dy)
            for (int dx = -R; dx <= R; ++dx)
                if (dx || dy) offsets.push_back({dx, dy});
        std::sort(offsets.begin(), offsets.end(), [](auto& a, auto& b) {
            return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second;
        });
    }
    auto& mask = world->bodyMask;
    std::fill(mask.begin(), mask.end(), (int16_t)-1);
    for (auto& b : bodies) {
        if (!b.alive) continue;
        int x0, y0, x1, y1;
        if (b.shape == SHAPE_CIRCLE) {
            x0 = (int)std::floor(b.pos.x - b.radius); x1 = (int)std::ceil(b.pos.x + b.radius);
            y0 = (int)std::floor(b.pos.y - b.radius); y1 = (int)std::ceil(b.pos.y + b.radius);
        } else {
            x0 = (int)std::floor(b.pos.x - b.bound); x1 = (int)std::ceil(b.pos.x + b.bound);
            y0 = (int)std::floor(b.pos.y - b.bound); y1 = (int)std::ceil(b.pos.y + b.bound);
        }
        for (int y = std::max(0, y0); y <= std::min(World::H - 1, y1); ++y) {
            for (int x = std::max(0, x0); x <= std::min(World::W - 1, x1); ++x) {
                if (!b.contains(Vec2(x + 0.5f, y + 0.5f))) continue;
                int i = y * World::W + x;
                mask[i] = (int16_t)b.id;
                Cell& c = world->cells[i];
                if (c.t == M_EMPTY) continue;
                Kind k = MATS[c.t].kind;
                if (k == K_SOLID) continue;
                // shove the particle to the nearest free cell
                bool placed = false;
                for (auto& o : offsets) {
                    int nx = x + o.first, ny = y + o.second;
                    if (world->isFree(nx, ny)) {
                        world->cells[ny * World::W + nx] = c;
                        placed = true;
                        break;
                    }
                }
                (void)placed;
                c.t = M_EMPTY;
            }
        }
    }
}

void Physics::sampleFluids() {
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
                for (int i = 0; i < n; ++i) pts.push_back(b.toWorld(p0 + (p1 - p0) * ((float)i / n)));
            }
        }
        int wet = 0, gran = 0;
        float rho = 0;
        Vec2 cen;
        for (Vec2 p : pts) {
            int ix = (int)std::floor(p.x), iy = (int)std::floor(p.y);
            if (!world->inb(ix, iy)) continue;
            uint8_t t = world->at(ix, iy).t;
            Kind k = MATS[t].kind;
            if (k == K_LIQUID) { ++wet; rho += MATS[t].density; cen += p; }
            else if (k == K_POWDER) ++gran;
        }
        float n = (float)pts.size();
        b.wetFrac = wet / n;
        b.granFrac = gran / n;
        if (wet) { b.fluidRho = rho / wet; b.wetCentroid = cen / (float)wet; }
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
    auto test = [&](Vec2 p) {
        int ix = (int)std::floor(p.x), iy = (int)std::floor(p.y);
        if (!world->isTerrain(ix, iy)) return;
        Vec2 n = surfaceNormal(ix, iy);
        if (lengthSq(n) < 1e-6f) n = normalize(b.pos - p);
        if (lengthSq(n) < 1e-6f) n = Vec2(0, -1);
        float depth = 12.f;
        for (float t = 0.5f; t <= 12.f; t += 0.5f) {
            Vec2 q = p + n * t;
            if (!world->isTerrain((int)std::floor(q.x), (int)std::floor(q.y))) { depth = t - 0.25f; break; }
        }
        contacts.emplace_back(-1, b.id, p, n, depth, b.isWheel ? 1.0f : 0.6f);
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
            int n = std::max(1, (int)std::ceil(length(p1 - p0) / 2.5f));
            for (int i = 0; i < n; ++i) test(b.toWorld(p0 + (p1 - p0) * ((float)i / n)));
        }
        test(b.pos);
    }
}

void Physics::collidePair(Body& A, Body& B2) {
    Body* a = &A;
    Body* b = &B2;
    if (a->shape > b->shape) std::swap(a, b);  // boxes first
    float mu = (a->isWheel || b->isWheel) ? 1.0f : 0.5f;

    if (a->shape == SHAPE_CIRCLE) {  // circle-circle
        Vec2 d = b->pos - a->pos;
        float dist = length(d), rr = a->radius + b->radius;
        if (dist > rr) return;
        Vec2 n = dist > 1e-5f ? d / dist : Vec2(0, 1);
        float depth = rr - dist;
        contacts.emplace_back(a->id, b->id, a->pos + n * (a->radius - depth * 0.5f), n, depth, mu);
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
        contacts.emplace_back(a->id, b->id, b->pos - n * (b->radius - depth * 0.5f), n, depth, mu);
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
        if (sep <= 0.05f) contacts.emplace_back(a->id, b->id, seg2[i], n, std::max(0.f, -sep), mu);
    }
}

bool Physics::connected(int a, int b) const {
    return std::binary_search(noCollide.begin(), noCollide.end(), pairKey(a, b));
}

void Physics::buildContacts() {
    contacts.clear();
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
    float bounce = vn < -60.f ? -0.2f * vn : 0.f;
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

void Physics::substep(float h) {
    // external forces
    float g = length(gravity);
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
        if (b.isRocket && thrustOn) {
            Vec2 dir = rotate(Vec2(0, -1), b.angle);
            b.vel += dir * (g * 6.f * h);
            if (world->chance(0.7f)) {
                int len = 2 + world->rint(8);
                Vec2 p = b.pos - dir * (b.half.y + (float)len) + Vec2((float)world->rint(3) - 1.f, (float)world->rint(3) - 1.f);
                int ix = (int)std::floor(p.x), iy = (int)std::floor(p.y);
                if (world->isFree(ix, iy)) world->spawn(ix, iy, M_FIRE, (uint8_t)(3 + world->rint(5)));
            }
        }
    }

    for (auto& j : joints) if (j.alive) prestepJoint(j, h);
    buildContacts();
    for (auto& c : contacts) prestepContact(c, h);
    for (int it = 0; it < ITERATIONS; ++it) {
        for (auto& j : joints) if (j.alive) solveJoint(j, h);
        for (auto& c : contacts) solveContact(c);
    }

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
        else if (lengthSq(b.vel) < 0.04f && b.w * b.w < 0.0004f) { b.vel = Vec2(); b.w = 0; }
    }
}

void Physics::step(float dt) {
    applyBlasts();
    sampleFluids();
    noCollide.clear();
    for (auto& j : joints)
        if (j.alive && (j.type == J_PIN || j.type == J_MOTOR || j.type == J_DISTANCE) && j.b >= 0)
            noCollide.push_back(pairKey(j.a, j.b));
    std::sort(noCollide.begin(), noCollide.end());
    float h = dt / SUBSTEPS;
    for (int s = 0; s < SUBSTEPS; ++s) substep(h);
    stampBodies();
}
