#include "sand.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cmath>

namespace {
const int DX4[4] = {1, -1, 0, 0};
const int DY4[4] = {0, 0, 1, -1};

// combustion with oxygen (World::needAir)
const float AIR_MIN = 0.05f;      // air thinner than this does not keep a flame going (a fan intake makes air at 0.25)
const float AIR_IGN_MIN = 0.02f;  // the least air a gas fuel cell can light from
const float SOLID_AIR = 0.02f;    // a burning solid or liquid draws airNeed * this of air a frame (airNeed / 50)

uint8_t initialLife(World& w, uint8_t t) {
    return t == M_FIRE ? (uint8_t)(25 + w.rint(35)) : (uint8_t)0;
}


// Conduct heat between two adjacent cells (energy conserving, unconditionally stable).
inline void exchange(Cell& a, Cell& b) {
    float dT = a.temp - b.temp;
    if (dT > -0.02f && dT < 0.02f) return;
    float ka = cellCond(a), kb = cellCond(b);
    float k = 2.f * ka * kb / (ka + kb + 1e-6f);
    float Ca = cellCap(a), Cb = cellCap(b);
    float q = k * dT;
    float lim = 0.24f * std::min(Ca, Cb) * dT;
    if (std::fabs(q) > std::fabs(lim)) q = lim;
    a.temp -= q / Ca;
    b.temp += q / Cb;
}

inline bool sameFluid(const Cell& a, const Cell& b) {
    return a.t == b.t && ((a.t != M_VAPOR && a.t != M_MOLTEN) || a.life == b.life);
}
}  // namespace

World::World() { clear(); }

void World::clear() {
    cells.assign(W * H, Cell{});
    bodyMask.assign(W * H, -1);
    gasVX.assign((size_t)W * H, 0.f); gasVY.assign((size_t)W * H, 0.f);
    blasts.clear();
}

void World::save(Writer& w) const {
    static_assert(offsetof(Cell, temp) == 8, "Cell layout");
    std::vector<Cell> tmp = cells;
    for (Cell& c : tmp) { unsigned char* p = reinterpret_cast<unsigned char*>(&c); p[6] = 0; p[7] = 0; }   // the padding bytes: identical worlds save identical bytes
    w.vec(tmp);
    w.pod(clk); w.pod(tick); w.pod(sparkTimer); w.pod(rng);
    w.pod(battV); w.pod(battA); w.pod(sparkPeriod); w.pod((int64_t)burnEvents);
}

bool World::load(Reader& r) {
    std::vector<Cell> c;
    r.vec(c, (size_t)W * H);
    if (!r.ok || (int)c.size() != W * H) return false;
    for (const Cell& cc : c) if (cc.t >= M_COUNT) return false;   // a cell of a material that does not exist would index past the table
    cells = c;
    clk = r.pod<uint8_t>(); tick = r.pod<uint32_t>(); sparkTimer = r.pod<uint32_t>(); rng = r.pod<uint32_t>();
    battV = r.pod<float>(); battA = r.pod<float>(); sparkPeriod = r.pod<int>(); burnEvents = (long)r.pod<int64_t>();
    bodyMask.assign(W * H, -1);
    gasVX.assign((size_t)W * H, 0.f); gasVY.assign((size_t)W * H, 0.f);   // (the flow field is not saved: a loaded world starts still)
    blasts.clear(); arcs.clear();
    volt.clear(); curr.clear(); elecCool.clear(); hadElec = false; vMax = iSource = 0.f; arcCount = 0;
    return r.ok;
}

uint32_t World::rnd() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

bool World::isTerrain(int x, int y) const {
    if (!inb(x, y)) return true;
    uint8_t t = cells[y * W + x].t;
    if (t == M_VOID) return false;
    Kind k = MATS[t].kind;
    return k == K_SOLID || k == K_POWDER;
}

bool World::isFree(int x, int y) const {
    if (!inb(x, y)) return false;
    int i = y * W + x;
    return bodyMask[i] < 0 && cells[i].t == M_EMPTY;
}

void World::setCell(int x, int y, uint8_t t) {
    Cell& c = at(x, y);
    c = Cell{};
    c.t = t;
    c.var = (uint8_t)rnd();
    c.clock = clk;
    c.life = initialLife(*this, t);
    if (t == M_VAPOR) c.life = M_GASOLINE;
    if (t == M_BATT_POS || t == M_BATT_NEG) { c.life = encV(battV); c.aux = encA(battA); }
    if (t == M_SOURCE) c.amt = sourceAmt;
    c.temp = MATS[t].initT;
}

void World::spawn(int x, int y, uint8_t t, uint8_t life, float temp) {
    setCell(x, y, t);
    Cell& c = at(x, y);
    c.life = life;
    if (temp > -1e8f) c.temp = temp;
}

void World::convert(int x, int y, uint8_t to, uint8_t origin, float temp, float amt) {
    Cell& c = at(x, y);
    c = Cell{};
    c.t = to;
    c.temp = temp;
    c.amt = amt;
    c.life = origin;
    c.var = (uint8_t)rnd();
    c.clock = clk;
}

bool World::canDisplace(int x, int y, float dens) const {
    if (!inb(x, y)) return false;
    int i = y * W + x;
    if (bodyMask[i] >= 0) return false;
    uint8_t t = cells[i].t;
    if (t == M_EMPTY) return true;
    Kind k = MATS[t].kind;
    return (k == K_LIQUID || k == K_GAS) && dens > MATS[t].density;
}

bool World::canRise(int x, int y) const {
    if (!inb(x, y)) return false;
    int i = y * W + x;
    if (bodyMask[i] >= 0) return false;
    uint8_t t = cells[i].t;
    return t == M_EMPTY || MATS[t].kind == K_LIQUID;
}

void World::moveTo(int x, int y, int nx, int ny) {
    std::swap(cells[y * W + x], cells[ny * W + nx]);
    cells[y * W + x].clock = clk;
    cells[ny * W + nx].clock = clk;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void World::step() {
    clk ^= 1;
    ++tick;
    ++sparkTimer;
    sparkNow = sparkHeld || (sparkPeriod > 0 && (int)(sparkTimer % (uint32_t)sparkPeriod) < 2);

    auto tn = [] { return (double)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    double t0 = tn();
    electricity();
    double t1 = tn();
    thermalPass();
    double t2 = tn();

    bool ltr = tick & 1;
    for (int y = H - 1; y >= 0; --y) {
        for (int i = 0; i < W; ++i) {
            int x = ltr ? i : W - 1 - i;
            Cell& c = cells[y * W + x];
            if (c.t == M_EMPTY || c.clock == clk) continue;
            c.clock = clk;
            updateCell(x, y);
        }
    }

    double t3 = tn();
    gasFlux();
    double t4 = tn();
    liquidPressure();
    double t5 = tn();
    prof[0] += t1 - t0; prof[1] += t2 - t1; prof[2] += t3 - t2; prof[3] += t4 - t3; prof[4] += t5 - t4; ++profN;
}

void World::thermalPass() {
    const bool rev = tick & 1;   // alternate the sweep direction so heat does not creep one way
    const int sd = rev ? -1 : 1;
    for (int yy = 0; yy < H; ++yy) {
        const int y = rev ? H - 1 - yy : yy;
        for (int xx = 0; xx < W; ++xx) {
            const int x = rev ? W - 1 - xx : xx;
            int i = y * W + x;
            Cell& a = cells[i];
            uint8_t t = a.t;
            if (t == M_EMPTY) continue;
            if (t == M_HEATER) a.temp = 900.f;
            else if (t == M_COOLER) a.temp = -60.f;
            else if (t == M_IGNITER && sparkNow) {
                for (int k = 0; k < 4; ++k) {
                    int nx = x + DX4[k], ny = y + DY4[k];
                    if (inb(nx, ny) && bodyMask[ny * W + nx] < 0) sparkAt(nx, ny);
                }
            }

            if (x + sd >= 0 && x + sd < W && cells[i + sd].t != M_EMPTY) exchange(a, cells[i + sd]);
            if (y + sd >= 0 && y + sd < H && cells[i + sd * W].t != M_EMPTY) exchange(a, cells[i + sd * W]);

            // exposure to the (infinite, ambient) outside
            int e = 0;
            // (gases do not exchange with empty cells: inside a machine those are vacuum, not open air)
            if (MATS[t].kind != K_GAS)
            for (int k = 0; k < 4; ++k) {
                int nx = x + DX4[k], ny = y + DY4[k];
                if (inb(nx, ny) && cells[ny * W + nx].t == M_EMPTY) ++e;
            }
            if (e) {
                float C = cellCap(a);
                float dT = a.temp - AMBIENT_T;
                float q = (MATS[t].kind == K_GAS ? 0.02f : 0.0008f) * e * dT;
                float lim = 0.24f * C * dT;
                if (std::fabs(q) > std::fabs(lim)) q = lim;
                a.temp -= q / C;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Per-cell behaviour
// ---------------------------------------------------------------------------

void World::updateCell(int x, int y) {
    if (phase(x, y)) return;
    Cell& c = at(x, y);
    const MatInfo& m = MATS[c.t];
    if (c.burn > 0 && m.kind != K_GAS) { const uint8_t before = c.t; burnTick(x, y); if (c.t != before) return; }   // (it may have burned away: `m` would be stale)
    if (c.t == M_EMPTY) return;
    if (m.burstP > 0.f && m.kind == K_SOLID) { uint8_t t0 = c.t; burstCheck(x, y); if (c.t != t0) return; }
    if ((m.ignT > 0 || c.t == M_VAPOR) && c.burn == 0 && tryIgnite(x, y)) return;

    switch (c.t) {
        case M_SAND: case M_ASH: case M_GUNPOWDER: case M_COAL: powder(x, y); break;
        case M_LAVA: if (rint(3) == 0) liquid(x, y, 2); break;
        case M_MOLTEN: if (rint(2) == 0) liquid(x, y, 2); break;
        case M_ACID: acidCell(x, y); break;
        case M_FIRE: fireCell(x, y); break;
        case M_PLANT: plantCell(x, y); break;
        case M_TNT: tntCell(x, y); break;
        case M_PRIMER: primerCell(x, y); break;
        case M_AIR: {
            // ambient air thins out where it meets the empty void around a machine, so a fan in the open makes a
            // plume rather than slowly filling the whole world; inside a closed volume nothing is lost
            int empty = 0;
            for (int k = 0; k < 4; ++k) {
                int nx = x + DX4[k], ny = y + DY4[k];
                if (!inb(nx, ny) || bodyMask[ny * W + nx] >= 0) continue;
                const Cell& n = cells[ny * W + nx];
                if (n.t == M_EMPTY || (MATS[n.t].kind == K_GAS && n.amt < 0.12f)) ++empty;   // void, or gas too thin to matter
            }
            if (empty && c.amt < 0.6f && c.life == 1) {   // ambient air from a fan intake (life 1) only; dense air is contained (a duct, a chamber); only thin edge air leaks away
                c.amt *= 1.f - 0.05f * (float)empty;
                if (c.amt < 0.03f) { c.t = M_EMPTY; return; }
            }
            gasMove(x, y);
            break;
        }
        case M_VOID: voidCell(x, y); break;
        case M_SOURCE: sourceCell(x, y); break;
        default:
            if (m.kind == K_LIQUID) liquid(x, y, m.density < 0.9f ? 5 : 6);
            else if (m.kind == K_GAS) gasMove(x, y);
            break;
    }
}

// Heating / cooling driven state changes. Returns true when the cell was converted.
bool World::phase(int x, int y) {
    Cell& c = at(x, y);
    if (c.t == M_MOLTEN) {
        uint8_t o = c.life < M_COUNT ? c.life : (uint8_t)M_IRON;
        if (c.temp < MATS[o].hiT - 80.f) {
            uint8_t to = MATS[o].freezeAs ? MATS[o].freezeAs : o;
            convert(x, y, to, 0, c.temp, 1.f);
            return true;
        }
        return false;
    }
    const MatInfo& m = MATS[c.t];
    // a small dead-band: a cell parked exactly on a transition temperature must not creep across it
    const float band = m.latent > 0.f ? 0.05f : 0.f;
    bool up = c.temp >= m.hiT + band && m.hiTo;
    bool down = c.temp <= m.loT - band && m.loTo;
    if (!up && !down) {
        if (m.latent > 0.f && c.life > 0) --c.life;  // phase-change progress fades when the cell is not being pushed
        return false;
    }

    uint8_t to = up ? m.hiTo : m.loTo;
    float edge = up ? m.hiT : m.loT;
    if (m.latent > 0.f) {
        float add = std::fabs(c.temp - edge) * cellCap(c);   // all the heat beyond the transition goes into the latent heat, none is lost
        int p = c.life + (int)std::ceil(add);
        c.temp = edge;
        if (p < (int)m.latent) { c.life = (uint8_t)std::min(p, 255); return false; }
    }
    const MatInfo& tm = MATS[to];
    uint8_t origin = (to == M_MOLTEN || to == M_VAPOR) ? c.t : (uint8_t)0;
    float amt = 1.f;
    if (m.kind == K_LIQUID && tm.kind == K_GAS) amt = m.expand;
    else if (m.kind == K_GAS && tm.kind == K_LIQUID) {
        // a cell of gas holds amt/expand cells' worth of liquid: thin gas condenses to droplets stochastically
        float liquid = c.amt / std::max(1.f, tm.expand);
        if (liquid < 1.f) {
            if (!chance(liquid)) { c.t = M_EMPTY; return true; }
            liquid = 1.f;
        }
        amt = liquid;
    }
    convert(x, y, to, origin, c.temp, amt);
    return true;
}

// ---------------------------------------------------------------------------
// Combustion. Burning needs oxygen (unless the fuel carries its own, selfOx): the open air, which is unlimited, or AIR cells,
// which are used up at airNeed units of air per unit of fuel. Empty cells inside a sealed machine are vacuum and feed nothing.
// ---------------------------------------------------------------------------

// Which empty and gas cells are open air (connected to the edge of the world); inside a sealed machine the empty cells are
// vacuum. gasFlux refreshes this map every 20 frames; combustion wants it fresher (a valve that has just closed must not leave the
// cylinder counting as open air for long) and asks for it first, so there is one in the very first frame too. About 2 ms a time.
void World::refreshOutside() {
    if (outside.size() == (size_t)W * H && tick - outsideTick < 6u && outsideTick <= tick) return;
    outside.assign((size_t)W * H, 0);
    outsideTick = tick;
    std::vector<int> st;
    auto open_ = [&](int i) { return bodyMask[i] < 0 && (cells[i].t == M_EMPTY || MATS[cells[i].t].kind == K_GAS); };
    auto seed = [&](int i) { if (!outside[i] && open_(i)) { outside[i] = 1; st.push_back(i); } };
    for (int x = 0; x < W; ++x) { seed(x); seed((H - 1) * W + x); }
    for (int y = 0; y < H; ++y) { seed(y * W); seed(y * W + W - 1); }
    while (!st.empty()) {
        int p = st.back(); st.pop_back();
        int px = p % W, py = p / W;
        if (px + 1 < W) seed(p + 1);
        if (px > 0) seed(p - 1);
        if (py + 1 < H) seed(p + W);
        if (py > 0) seed(p - W);
    }
}

// The open air is any empty cell, or any gas but a fuel (smoke, flame, steam, air), that is connected to the edge of the world: it has
// the atmosphere mixed in, so a fire in the open never wants for oxygen. Inside a sealed volume only AIR cells count, and they run out.
World::AirNear World::airNear(int x, int y) {
    refreshOutside();
    AirNear a;
    for (int k = 0; k < 4; ++k) {
        int nx = x + DX4[k], ny = y + DY4[k];
        if (!inb(nx, ny)) continue;
        int i = ny * W + nx;
        if (bodyMask[i] >= 0) continue;
        const Cell& n = cells[i];
        bool fuelGas = n.t == M_VAPOR || (MATS[n.t].kind == K_GAS && MATS[n.t].ignT > 0.f);
        if (outside[i] && (n.t == M_EMPTY || (MATS[n.t].kind == K_GAS && !fuelGas))) a.open = true;
        else if (n.t == M_EMPTY) a.vacuum = true;
        else if (n.t == M_AIR && n.amt > 0.f) { a.idx[a.n++] = i; a.total += n.amt; }
    }
    return a;
}

bool World::oxygenFace(int x, int y) {
    if (!needAir) {   // the old rule: any open face, empty or gas
        for (int k = 0; k < 4; ++k) {
            int nx = x + DX4[k], ny = y + DY4[k];
            if (!inb(nx, ny)) continue;
            uint8_t nt = cells[ny * W + nx].t;
            if (nt == M_EMPTY || MATS[nt].kind == K_GAS) return true;
        }
        return false;
    }
    AirNear a = airNear(x, y);
    if (a.open) return true;
    for (int i = 0; i < a.n; ++i) if (cells[a.idx[i]].amt >= AIR_MIN) return true;
    return false;
}

// A spark: ignites a flammable cell if it is above its flash point.
void World::sparkAt(int x, int y) {
    Cell& c = at(x, y);
    const MatInfo* m = &MATS[c.t];
    if (c.t == M_VAPOR) m = &MATS[c.life < M_COUNT ? c.life : (uint8_t)M_GASOLINE];
    if (m->ignT <= 0.f || c.burn > 0 || (c.t == M_TNT && c.life > 0)) return;
    if (c.temp < m->flashT) return;
    if (m->kind != K_GAS && c.t != M_VAPOR && !m->selfOx && !oxygenFace(x, y)) return;
    burn(x, y, *m);
}

bool World::tryIgnite(int x, int y) {
    Cell& c = at(x, y);
    const MatInfo* m = &MATS[c.t];
    if (c.t == M_VAPOR) m = &MATS[c.life < M_COUNT ? c.life : (uint8_t)M_GASOLINE];
    if (m->ignT <= 0.f) return false;
    if (c.t == M_TNT && c.life > 0) return false;

    bool gasFuel = m->kind == K_GAS || c.t == M_VAPOR;
    if (!gasFuel && !m->selfOx && !oxygenFace(x, y)) return false;   // a solid or liquid burns at a face with oxygen at it
    bool hot = c.temp >= m->ignT;
    // a surface far above the ignition temperature lights the fuel on contact (glow plugs, lava, red-hot metal)
    for (int k = 0; k < 4 && !hot; ++k) {
        int nx = x + DX4[k], ny = y + DY4[k];
        if (!inb(nx, ny)) continue;
        const Cell& nc = cells[ny * W + nx];
        if (MATS[nc.t].kind != K_GAS && nc.t != M_EMPTY && nc.t != M_FIRE && nc.burn == 0 && nc.temp >= m->ignT + 120.f) hot = true;   // (a burning neighbour spreads fire at the fuel's own burn speed, below)
    }
    bool flame = false;
    if (!hot && c.temp >= m->flashT) {
        for (int k = 0; k < 4 && !flame; ++k) {
            int nx = x + DX4[k], ny = y + DY4[k];
            if (inb(nx, ny) && (cells[ny * W + nx].t == M_FIRE || cells[ny * W + nx].burn > 0)) flame = true;
        }
    }
    if (hot || (flame && chance(m->burnSpeed))) return burn(x, y, *m);
    return false;
}

bool World::burn(int x, int y, const MatInfo& m) {
    Cell& c = at(x, y);
    if (c.t == M_TNT) {
        ++burnEvents;
        if (c.life == 0) c.life = (uint8_t)(2 + rint(5));
        return true;
    }
    if (MATS[c.t].kind == K_GAS) {  // gas-phase combustion: the mixture becomes flame, as far as the oxygen about it allows
        float extra = 0.f;   // the air that burns with the fuel joins the flame, and later the exhaust: no gas is lost
        if (needAir && !m.selfOx) {
            AirNear a = airNear(x, y);
            if (!a.open) {
                const float need = c.amt * m.airNeed;
                if (a.total < AIR_IGN_MIN || need <= 0.f) return false;   // nothing to burn with
                const float used = std::min(need, a.total);
                int richest = -1;
                for (int i = 0; i < a.n; ++i) {   // each air cell gives its share
                    Cell& n = cells[a.idx[i]];
                    if (richest < 0 || n.amt > cells[richest].amt) richest = a.idx[i];
                    n.amt -= used * n.amt / a.total;
                    if (n.amt < 0.01f) n.t = M_EMPTY;
                }
                extra = used;
                if (used < need) {   // too little air: only the part it supports burns, where the air was; the rest stays as fuel for later
                    const float frac = used / need;
                    Cell& f = cells[richest];   // (all of its air just went into the flame)
                    f = Cell{};
                    f.t = M_FIRE; f.life = (uint8_t)(c.t == M_VAPOR ? 8 : std::min(255, m.burnTime)); f.temp = m.burnT; f.clock = clk;
                    f.amt = c.amt * frac + used;
                    c.amt -= c.amt * frac;
                    ++burnEvents;
                    return true;
                }
            }
        }
        ++burnEvents;
        c.var = 0;
        c.life = (uint8_t)(c.t == M_VAPOR ? 8 : std::min(255, m.burnTime));
        c.temp = std::max(c.temp, m.burnT);
        c.t = M_FIRE;
        c.burn = 0;
        c.clock = clk;
        c.amt += extra;
        return true;
    }
    ++burnEvents;
    c.burn = c.aux ? c.aux : (uint8_t)std::min(255, std::max(1, m.burnTime));   // a relit cell carries on from where its fire was starved
    c.aux = 0;
    c.temp = std::max(c.temp, m.burnT);
    return true;
}

void World::burnTick(int x, int y) {
    Cell& c = at(x, y);
    const MatInfo& m = MATS[c.t];
    // water puts fires out
    for (int k = 0; k < 4; ++k) {
        int nx = x + DX4[k], ny = y + DY4[k];
        if (inb(nx, ny) && cells[ny * W + nx].t == M_WATER) { c.burn = 0; c.temp = std::min(c.temp, std::max(AMBIENT_T, m.ignT - 20.f)); return; }   // doused: cooled below ignition, or it would relight next frame
    }
    if (needAir && !m.selfOx) {   // a solid or liquid burns at its surface with the oxygen there; starved of it the fire goes out, but the fuel
        AirNear a = airNear(x, y);  // stays hot and relights, with the fuel it had left (kept in aux), when air returns
        if (!a.open) {
            int best = -1;
            for (int i = 0; i < a.n; ++i) if (best < 0 || cells[a.idx[i]].amt > cells[best].amt) best = a.idx[i];
            if (best < 0 || cells[best].amt < AIR_MIN) { c.aux = c.burn; c.burn = 0; return; }
            Cell& n = cells[best];
            n.amt -= std::min(n.amt, m.airNeed * SOLID_AIR);
            if (n.amt < 0.01f) n.t = M_EMPTY;
        }
    }
    c.temp = std::max(c.temp, m.burnT * 0.8f);
    if (chance(0.5f)) {
        int k = rint(4);
        int nx = x + DX4[k], ny = y + DY4[k];
        if (isFree(nx, ny)) {
            spawn(nx, ny, M_FIRE, (uint8_t)(4 + rint(7)), m.burnT);
            at(nx, ny).amt = 0.6f;
        } else if (needAir && inb(nx, ny) && bodyMask[ny * W + nx] < 0 && at(nx, ny).t == M_AIR && at(nx, ny).amt <= 0.35f) {
            float amt = at(nx, ny).amt;   // thin air at the flame front becomes flame
            spawn(nx, ny, M_FIRE, (uint8_t)(4 + rint(7)), m.burnT);
            at(nx, ny).amt = amt;
        }
    }
    if (m.selfOx) {   // a burning grain lights its neighbours directly, even with no air about
        for (int k = 0; k < 4; ++k) {
            int nx = x + DX4[k], ny = y + DY4[k];
            if (!inb(nx, ny)) continue;
            Cell& n = cells[ny * W + nx];
            if (n.t == c.t && n.burn == 0 && chance(m.burnSpeed)) burn(nx, ny, m);
        }
    }
    if (--c.burn == 0) {
        if (m.gasYield > 0.f) { convert(x, y, M_EXHAUST, 0, m.burnT, m.gasYield); return; }   // the powder becomes a lot of very hot gas
        if (m.burnRes != M_EMPTY && chance(0.7f)) { setCell(x, y, m.burnRes); at(x, y).temp = 300.f; }
        else if (chance(0.4f)) { convert(x, y, M_SMOKE, 0, 300.f, 0.8f); }
        else c.t = M_EMPTY;
    }
}

void World::powder(int x, int y) {
    float d = MATS[at(x, y).t].density;
    if (canDisplace(x, y + 1, d)) {
        if (at(x, y + 1).t == M_EMPTY || rint(3)) moveTo(x, y, x, y + 1);
        return;
    }
    int dir = (rnd() & 1) ? 1 : -1;
    for (int k = 0; k < 2; ++k, dir = -dir) {
        int nx = x + dir;
        if (canDisplace(nx, y + 1, d) && canDisplace(nx, y, d)) {
            if (at(nx, y + 1).t == M_EMPTY || rint(3)) moveTo(x, y, nx, y + 1);
            return;
        }
    }
}

void World::liquid(int x, int y, int disp) {
    Cell& c = at(x, y);
    const MatInfo& m = MATS[c.t];
    if (m.volatility > 0.f) {
        float p = m.volatility * std::exp(std::min((c.temp - AMBIENT_T) / 35.f, 6.f));
        if (chance(p)) {
            int k = rint(4);
            int nx = x + DX4[k], ny = y + DY4[k];
            if (isFree(nx, ny)) {
                convert(nx, ny, M_VAPOR, c.t, std::max(c.temp, AMBIENT_T), 0.7f);
                c.t = M_EMPTY;
                return;
            }
        }
    }
    float d = m.density;
    if (canDisplace(x, y + 1, d)) { moveTo(x, y, x, y + 1); return; }
    // sideways a liquid flows through empty cells and shoves aside gas (the steam in a pipe is no wall to the water in it)
    auto flowable = [&](int fx, int fy) {
        if (isFree(fx, fy)) return true;
        if (!inb(fx, fy) || bodyMask[fy * W + fx] >= 0) return false;
        const Cell& n = cells[fy * W + fx];
        return MATS[n.t].kind == K_GAS && n.t != M_FIRE;
    };
    // Gas pressure shoves liquid: the end of a column of liquid facing a free or gas cell moves into it when the gas
    // behind the column is at a clearly higher pressure (a slug of water blown out of a pipe by the steam behind it).
    {
        auto psiAt = [&](int px, int py) -> float {
            const Cell& n = cells[py * W + px];
            return MATS[n.t].kind == K_GAS ? n.amt * (n.temp + 273.f) / 293.f : 0.f;
        };
        const int k0 = (int)(rnd() & 3u);
        for (int kk = 0; kk < 4; ++kk) {
            const int k = (kk + k0) & 3;
            int fx = x + DX4[k], fy = y + DY4[k];
            if (!inb(fx, fy) || bodyMask[fy * W + fx] >= 0) continue;
            const Cell& f = cells[fy * W + fx];
            if (f.t != M_EMPTY && !(MATS[f.t].kind == K_GAS && f.t != M_FIRE)) continue;   // the front must be free or gas
            // look back through the liquid for the gas that is pushing on it
            int bx = x - DX4[k], by = y - DY4[k], steps = 0;
            while (inb(bx, by) && bodyMask[by * W + bx] < 0 && MATS[cells[by * W + bx].t].kind == K_LIQUID && steps < 40) { bx -= DX4[k]; by -= DY4[k]; ++steps; }
            if (!inb(bx, by) || bodyMask[by * W + bx] >= 0 || MATS[cells[by * W + bx].t].kind != K_GAS) continue;
            float dp = psiAt(bx, by) - psiAt(fx, fy);
            if (DY4[k] < 0) dp -= 0.8f;   // pushing it up against gravity needs more
            if (DY4[k] > 0) dp += 0.8f;   // downwards gravity helps
            if (dp < 0.25f || !chance(std::min(1.f, dp * 0.12f))) continue;
            
            moveTo(x, y, fx, fy);
            return;
        }
    }
    int dir = (rnd() & 1) ? 1 : -1;
    if (canDisplace(x + dir, y + 1, d) && flowable(x + dir, y)) { moveTo(x, y, x + dir, y + 1); return; }
    if (canDisplace(x - dir, y + 1, d) && flowable(x - dir, y)) { moveTo(x, y, x - dir, y + 1); return; }
    for (int pass = 0; pass < 2; ++pass, dir = -dir) {
        int cx = x;
        for (int s = 1; s <= disp; ++s) {
            if (flowable(x + dir * s, y)) cx = x + dir * s; else break;
        }
        if (cx != x) { moveTo(x, y, cx, y); return; }
    }
}

// Gases mostly spread by pressure (gasFlux); here they only drift by buoyancy.
void World::gasMove(int x, int y) {
    Cell& c = at(x, y);
    const MatInfo& m = MATS[c.t];
    float p = m.buoy > 0.f ? m.buoy * (0.3f + (c.temp - AMBIENT_T) / 500.f) : m.buoy;
    if (p == 0.f) return;
    int dy = p > 0 ? -1 : 1;
    if (!chance(std::min(std::fabs(p), 0.9f))) return;
    int dir = (rnd() & 1) ? 1 : -1;
    if ((dy < 0 ? canRise(x, y + dy) : isFree(x, y + dy)) && (isFree(x, y + dy) || rint(2))) { moveTo(x, y, x, y + dy); return; }   // gas bubbles up through liquid but never sinks into it
    if (isFree(x + dir, y + dy)) { moveTo(x, y, x + dir, y + dy); return; }
    if (isFree(x - dir, y + dy)) { moveTo(x, y, x - dir, y + dy); return; }
}

void World::fireCell(int x, int y) {
    Cell& c = at(x, y);
    if (c.life == 0 || c.temp < 350.f) {
        float amt = c.amt, temp = c.temp;
        uint8_t res = c.aux;  // residue material, set explicitly by whoever spawned the flame (var is just colour noise)
        if (res != M_EMPTY && res < M_COUNT && chance(0.15f)) { setCell(x, y, res); at(x, y).temp = temp; return; }
        convert(x, y, chance(0.3f) ? M_SMOKE : M_EXHAUST, 0, temp, std::max(0.1f, amt));
        return;
    }
    --c.life;
    if (needAir) {   // a flame in vacuum has nothing to feed on and dies out quickly
        AirNear a = airNear(x, y);
        bool fed = a.open;
        for (int i = 0; i < a.n && !fed; ++i) fed = cells[a.idx[i]].amt >= AIR_MIN;
        if (!fed && a.vacuum) c.life = c.life > 2 ? (uint8_t)(c.life - 2) : (uint8_t)0;
    }
    if (chance(0.3f)) {
        int dir = rint(3) - 1;
        if (isFree(x + dir, y - 1)) moveTo(x, y, x + dir, y - 1);
    }
}

void World::acidCell(int x, int y) {
    for (int k = 0; k < 4; ++k) {
        int nx = x + DX4[k], ny = y + DY4[k];
        if (!inb(nx, ny)) continue;
        Cell& n = at(nx, ny);
        const MatInfo& nm = MATS[n.t];
        if ((nm.kind == K_SOLID || nm.kind == K_POWDER) && nm.acidK > 0.f && n.t != M_VOID && n.t != M_SOURCE) {
            if (chance(0.08f * nm.acidK)) {
                if (chance(0.3f)) convert(nx, ny, M_SMOKE, 0, n.temp, 0.8f); else n.t = M_EMPTY;
                if (chance(0.35f)) { at(x, y).t = M_EMPTY; return; }
            }
        }
    }
    liquid(x, y, 4);
}

void World::plantCell(int x, int y) {
    int k = rint(4);
    int nx = x + DX4[k], ny = y + DY4[k];
    if (inb(nx, ny) && at(nx, ny).t == M_WATER && chance(0.05f)) setCell(nx, ny, M_PLANT);
}

void World::tntCell(int x, int y) {
    Cell& c = at(x, y);
    if (c.life > 0 && --c.life == 0) explode(x, y, MATS[M_TNT].blastR, MATS[M_TNT].blastP);
}

// ---------------------------------------------------------------------------
// Frangible solids and impact-sensitive primer
// ---------------------------------------------------------------------------

static float cellPressure(const Cell& c) {
    const MatInfo& m = MATS[c.t];
    if (c.t == M_EMPTY) return 0.f;
    if (m.kind == K_GAS) return c.amt * (c.temp + 273.15f) / 293.15f;
    if (m.kind == K_LIQUID) return c.amt > 1.f ? 1.f + (c.amt - 1.f) * 8.f : 1.f;
    return -1.f;  // solid or powder: no fluid pressure
}

// A frangible plug (wax) fails layer by layer when the pressure difference across it exceeds burstP per cell of
// thickness: a thicker plug holds more, and once the face goes the rest is weaker.
void World::burstCheck(int x, int y) {
    Cell& c = at(x, y);
    const MatInfo& m = MATS[c.t];
    for (int axis = 0; axis < 2; ++axis) {
        int dx = axis == 0 ? 1 : 0, dy = axis == 0 ? 0 : 1;
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            int fx = x + dx * sgn, fy = y + dy * sgn;  // the exposed face
            if (!inb(fx, fy) || bodyMask[fy * W + fx] >= 0) continue;
            float pf = cellPressure(at(fx, fy));
            if (pf < 0.f) continue;
            int n = 1, bx = x, by = y;  // thickness of the plug behind this cell
            while (n < 8) {
                int nx = bx - dx * sgn, ny = by - dy * sgn;
                if (!inb(nx, ny) || at(nx, ny).t != c.t) break;
                bx = nx; by = ny; ++n;
            }
            int ox = bx - dx * sgn, oy = by - dy * sgn;  // what is behind the plug
            float pb = 0.f;
            if (inb(ox, oy)) {
                if (bodyMask[oy * W + ox] >= 0) continue;  // propped by a rigid body: it holds
                pb = cellPressure(at(ox, oy));
                if (pb < 0.f) continue;                    // propped by a wall
            }
            if (pf - pb > m.burstP * (float)n) {
                convert(x, y, m.hiTo, 0, c.temp, 1.f);
                return;
            }
        }
    }
}

// A primer cell lights when it is hot, touches flame, or has been struck (primerStrike sets its fuse).
// It then flashes: the flash runs through neighbouring primer in a frame or two and lights whatever it touches.
void World::primerCell(int x, int y) {
    Cell& c = at(x, y);
    if (c.life == 0) {
        bool lit = c.temp >= 230.f;
        for (int k = 0; k < 4 && !lit; ++k) {
            int nx = x + DX4[k], ny = y + DY4[k];
            if (inb(nx, ny) && at(nx, ny).t == M_FIRE) lit = true;
        }
        if (!lit) return;
        c.life = 1;
        return;
    }
    // detonate
    for (int k = 0; k < 4; ++k) {
        int nx = x + DX4[k], ny = y + DY4[k];
        if (!inb(nx, ny)) continue;
        Cell& n = at(nx, ny);
        if (n.t == M_PRIMER) { if (n.life == 0) n.life = 1; continue; }
        flashAt(nx, ny);
    }
    convert(x, y, M_FIRE, 0, 1800.f, 1.5f);
    at(x, y).life = 8;
}

void World::primerStrike(int x, int y) {
    if (inb(x, y) && at(x, y).t == M_PRIMER && at(x, y).life == 0) at(x, y).life = 1;
}

// A burst of flame at one cell: fire in free space, ignition (and a hot spot) in fuel.
void World::flashAt(int x, int y) {
    if (!inb(x, y) || bodyMask[y * W + x] >= 0) return;
    Cell& c = at(x, y);
    Kind k = MATS[c.t].kind;
    if (c.t == M_EMPTY || (k == K_GAS && c.t != M_FIRE && MATS[c.t].ignT <= 0.f)) {
        spawn(x, y, M_FIRE, (uint8_t)(6 + rint(8)), 1800.f);
        at(x, y).amt = 1.5f;
        return;
    }
    if (MATS[c.t].ignT > 0.f || c.t == M_VAPOR) {
        c.temp = std::max(c.temp, 1200.f);
        if (c.t != M_VAPOR && k != K_GAS && c.burn == 0) burn(x, y, MATS[c.t]);  // the flash lights fuel even without an open face
        else sparkAt(x, y);
    }
}

void World::voidCell(int x, int y) {
    for (int k = 0; k < 4; ++k) {
        int nx = x + DX4[k], ny = y + DY4[k];
        if (!inb(nx, ny)) continue;
        uint8_t nt = at(nx, ny).t;
        if (nt != M_EMPTY && nt != M_WALL && nt != M_VOID && nt != M_SOURCE) at(nx, ny).t = M_EMPTY;
    }
}

// Emits its payload into neighbouring cells. For gases the cell's own amt is the supply density: it fills
// empty neighbours to that density and tops up neighbouring gas of the same kind (a constant-pressure supply).
void World::sourceCell(int x, int y) {
    Cell& c = at(x, y);
    uint8_t p = c.life;
    if (p == 0 || p >= M_COUNT || !chance(0.3f)) return;
    bool gas = MATS[p].kind == K_GAS;
    for (int tries = 0; tries < 4; ++tries) {
        int k = rint(4);
        int nx = x + DX4[k], ny = y + DY4[k];
        if (!inb(nx, ny) || bodyMask[ny * W + nx] >= 0) continue;
        Cell& n = at(nx, ny);
        if (n.t == M_EMPTY) {
            setCell(nx, ny, p);
            if (gas) at(nx, ny).amt = c.amt;
            if (p == M_VAPOR && c.aux) at(nx, ny).life = c.aux;  // a source's aux field names the fuel the vapour came from
            return;
        }
        if (gas && n.t == p && n.amt < c.amt && (p != M_VAPOR || !c.aux || n.life == c.aux)) {
            n.amt = c.amt;
            return;
        }
        if (gas && MATS[n.t].kind == K_GAS && n.t != p && n.t != M_FIRE) {
            setCell(nx, ny, p);  // fresh charge scavenges thin burnt gas
            at(nx, ny).amt = c.amt;
            if (p == M_VAPOR && c.aux) at(nx, ny).life = c.aux;
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Pressure-driven flow
// ---------------------------------------------------------------------------

// Gas moves from high to low pressure (amt * absolute temperature). Several relaxation passes per frame
// let pressure waves outrun slow-moving pistons.
void World::gasFlux() {
    static std::vector<int> cur, next, all;
    static std::vector<uint32_t> mark;
    static std::vector<uint32_t> inList;   // which pass's list a cell is in (a running counter, so stale entries never match)
    static uint32_t listId = 0;
    static uint32_t gen = 0;
    if (mark.size() != (size_t)W * H) { mark.assign((size_t)W * H, 0); inList.assign((size_t)W * H, 0); }
    cur.clear();
    for (int i = 0; i < W * H; ++i) {
        const bool g = MATS[cells[i].t].kind == K_GAS && cells[i].t != M_FIRE;
        if (g) cur.push_back(i);
        if (!g || bodyMask[i] >= 0) gasVX[i] = gasVY[i] = 0.f;   // only open gas carries a flow: anything else forgets the velocity that was here
    }
    if (cur.empty()) return;
    all = cur;                   // every gas cell of the frame, including those the passes create: the bulk-flow step works on these
    const uint32_t gen0 = gen;   // a cell with mark > gen0 moved gas in some pass this frame

    // Moves gas between cells ia (a gas cell) and ib. Returns true when a worthwhile amount moved; *made gets a newly created cell, or -1.
    auto flux = [&](int ia, int ib, int& made) -> bool {
        made = -1;
        if (bodyMask[ia] >= 0 || bodyMask[ib] >= 0) return false;
        Cell& a = cells[ia];
        Cell& b = cells[ib];
        bool gb = MATS[b.t].kind == K_GAS && b.t != M_FIRE;
        if (gb && !sameFluid(a, b)) {
            // different gases cannot share a cell, so pressure crosses the boundary by swapping whole cells: rarely when the
            // pressures match, often when they differ
            float pa = a.amt * (a.temp + 273.f), pb = b.amt * (b.temp + 273.f);
            float dp = std::fabs(pa - pb) / (pa + pb + 1e-3f);
            if (chance(dp < 0.12f ? 0.0625f : std::min(0.9f, dp * 0.9f))) { std::swap(a, b); return false; }
            return false;
        }
        if (b.t == M_EMPTY) {
            float f = a.amt * 0.4f;
            if (f < 0.005f) return false;
            Cell n;
            n.t = a.t; n.clock = clk; n.life = a.t == M_VAPOR ? a.life : (uint8_t)0; n.var = a.var; n.temp = a.temp; n.amt = f;
            b = n;
            a.amt -= f;
            made = ib;
            return true;
        }
        if (gb) {
            float ta = a.temp + 273.f, tb = b.temp + 273.f;
            float psiA = a.amt * ta, psiB = b.amt * tb;
            Cell& hi = psiA > psiB ? a : b;
            Cell& lo = psiA > psiB ? b : a;
            float f = 0.8f * std::fabs(psiA - psiB) / (ta + tb);
            f = std::min(f, hi.amt * 0.9f);
            if (f <= 0.f) return false;
            float nb = lo.amt + f;
            lo.temp = (lo.temp * lo.amt + hi.temp * f) / nb;
            lo.amt = nb;
            hi.amt -= f;
            return f > 0.003f;
        }
        return false;
    };
    // Pressure waves travel up to 24 cells a frame, but only where something is still changing: after the first pass
    // a cell takes part only if it or a neighbour moved a noticeable amount of gas in the pass before. A large still
    // cloud (the air round a fan) therefore costs one pass, not twenty-four.
    for (int pass = 0; pass < 24; ++pass) {
        next.clear();
        ++gen;
        ++listId;
        for (int i : cur) inList[i] = listId;
        auto touch = [&](int i) { if (mark[i] != gen) { mark[i] = gen; next.push_back(i); } };
        size_t n = cur.size();
        bool rev = (tick + pass) & 1;
        for (size_t k = 0; k < n; ++k) {
            int i = cur[rev ? n - 1 - k : k];
            Cell& a = cells[i];
            if (MATS[a.t].kind != K_GAS || a.t == M_FIRE) continue;
            int x = i % W, y = i / W;
            int nbs[4] = {x + 1 < W ? i + 1 : -1, x > 0 ? i - 1 : -1, y + 1 < H ? i + W : -1, y > 0 ? i - W : -1};
            const int off = (int)(rnd() & 3u);   // a random starting side, so gas does not lean one way
            for (int dd = 0; dd < 4; ++dd) {
                int j = nbs[(dd + off) & 3];
                if (j < 0) continue;
                // gas-gas pairs are visited from both ends: handle each once
                bool gj = MATS[cells[j].t].kind == K_GAS && cells[j].t != M_FIRE;
                if (gj && j < i && inList[j] == listId) continue;   // that end is in this pass's list and handles the pair itself
                if (cells[i].t == M_EMPTY || MATS[cells[i].t].kind != K_GAS) break;
                int made;
                bool moved = flux(i, j, made);
                if (made >= 0) all.push_back(made);
                if (moved) {
                    touch(i);
                    touch(j);
                    // the neighbours of both ends must be in the next pass too: they now see a different pressure
                    for (int e : {i, j}) {
                        int ex = e % W, ey = e / W;
                        if (ex + 1 < W && MATS[cells[e + 1].t].kind == K_GAS) touch(e + 1);
                        if (ex > 0 && MATS[cells[e - 1].t].kind == K_GAS) touch(e - 1);
                        if (ey + 1 < H && MATS[cells[e + W].t].kind == K_GAS) touch(e + W);
                        if (ey > 0 && MATS[cells[e - W].t].kind == K_GAS) touch(e - W);
                    }
                }
            }
        }
        // cells that thinned out or became empty stay harmlessly in the list: they are skipped when visited
        if (next.empty()) break;
        cur.swap(next);
        // gas cells created this pass are in 'next' already via touch(j)
    }
    if (gasMomentum) gasMomentumStep(all, mark, gen0);
    // the open air: gas does not pile up against the edge of the world or hang on at the fringes of a cloud
    for (int x = 0; x < W; ++x) { for (int y : {0, H - 1}) { Cell& c = cells[y * W + x]; if (MATS[c.t].kind == K_GAS && c.t != M_FIRE && bodyMask[y * W + x] < 0) c = Cell{}; } }
    for (int y = 0; y < H; ++y) { for (int x : {0, W - 1}) { Cell& c = cells[y * W + x]; if (MATS[c.t].kind == K_GAS && c.t != M_FIRE && bodyMask[y * W + x] < 0) c = Cell{}; } }
    // which empty and gas cells are open air (connected to the edge of the world); inside a sealed machine the empty cells are vacuum
    if (outside.size() != (size_t)W * H || tick - outsideTick >= 20u || outsideTick > tick) {
        outside.assign((size_t)W * H, 0);
        outsideTick = tick;
        std::vector<int> st;
        auto open_ = [&](int i) { return bodyMask[i] < 0 && (cells[i].t == M_EMPTY || MATS[cells[i].t].kind == K_GAS); };
        auto seed = [&](int i) { if (!outside[i] && open_(i)) { outside[i] = 1; st.push_back(i); } };
        for (int x = 0; x < W; ++x) { seed(x); seed((H - 1) * W + x); }
        for (int y = 0; y < H; ++y) { seed(y * W); seed(y * W + W - 1); }
        while (!st.empty()) {
            int p = st.back(); st.pop_back();
            int px = p % W, py = p / W;
            if (px + 1 < W) seed(p + 1);
            if (px > 0) seed(p - 1);
            if (py + 1 < H) seed(p + W);
            if (py > 0) seed(p - W);
        }
    }
    for (int i = 0; i < W * H; ++i) {
        Cell& c = cells[i];
        if (MATS[c.t].kind != K_GAS || c.t == M_FIRE) continue;
        if (c.amt < 0.004f) { c.t = M_EMPTY; continue; }
        if (c.amt < 0.03f && bodyMask[i] < 0 && outside[i]) {   // thin gas bordering open void disperses
            int x = i % W, y = i / W;
            bool open = (x + 1 < W && cells[i + 1].t == M_EMPTY && bodyMask[i + 1] < 0) || (x > 0 && cells[i - 1].t == M_EMPTY && bodyMask[i - 1] < 0) ||
                        (y + 1 < H && cells[i + W].t == M_EMPTY && bodyMask[i + W] < 0) || (y > 0 && cells[i - W].t == M_EMPTY && bodyMask[i - W] < 0);
            if (open) { c.amt *= 0.9f; if (c.amt < 0.008f) c.t = M_EMPTY; }
        }
    }
}

// ---------------------------------------------------------------------------
// Bulk flow
// ---------------------------------------------------------------------------

// Pressure equalisation alone spreads gas like treacle: nothing travels faster than the pressure difference across one cell
// carries it, and the moment the difference is gone the gas stops. Real gas has inertia. So every open gas cell also carries a
// velocity (cells per frame): the pressure gradient accelerates it, drag and walls slow it, neighbours trade momentum, and the
// gas is then carried along it (first-order upwind: a share of the cell's content moves to the downwind neighbour and takes its
// heat and momentum with it). A stream therefore keeps going when the push stops, drags the gas beside it along, is thinner
// where it is fast, and the pressure drop across a restriction grows with the flow through it, since the gas must be
// accelerated into the gap and that speed is lost again in the jet beyond. The pressure solver still does the equalising, and
// through a one-cell gap it still carries most of the flow, so these effects sit on top of it rather than replacing it.
void World::gasMomentumStep(const std::vector<int>& gas, const std::vector<uint32_t>& touched, uint32_t gen0) {
    constexpr float K_ACC = 0.8f;      // acceleration per unit of pressure difference across a cell (relative above one atmosphere)
    constexpr float DRAG = 0.004f;     // share of the velocity lost per frame in free gas (a jet keeps its core for a few diameters)
    constexpr float WALL_DRAG = 0.06f; // ... and per neighbouring wall, grain, liquid or body
    constexpr float VISC = 0.04f;      // share of the velocity difference to a neighbour evened out per frame
    constexpr float VMAX = 0.9f;       // cells per frame: never more than most of a cell's content per frame
    constexpr float STILL = 0.02f;     // below this speed (about a cell a second) a cell in still surroundings is left alone
    constexpr float WISP = 0.03f;      // thinner gas carries no momentum worth the work (the thin-gas rules deal with it)
    static std::vector<int> act;
    static std::vector<float> base;
    static std::vector<uint32_t> stamp;
    static uint32_t stampId = 0;
    if (stamp.size() != (size_t)W * H) stamp.assign((size_t)W * H, 0);
    ++stampId;
    auto psi = [](const Cell& c) { return c.amt * (c.temp + 273.f) / 293.f; };
    auto openGas = [&](int i) { return bodyMask[i] < 0 && MATS[cells[i].t].kind == K_GAS && cells[i].t != M_FIRE; };
    // the active cells: open gas that is moving, or whose surroundings changed pressure this frame; a still cloud costs nothing
    act.clear();
    for (int i : gas) {
        if (!openGas(i) || stamp[i] == stampId) continue;
        if (cells[i].amt < WISP) { gasVX[i] = gasVY[i] = 0.f; continue; }
        if (std::fabs(gasVX[i]) + std::fabs(gasVY[i]) < STILL) {
            if (touched[i] <= gen0) continue;                            // still gas in still surroundings
            if (((i % W) + (i / W) + (int)tick) & 1) continue;          // still gas in changing surroundings is looked at every other frame: half the work on a slowly spreading cloud
        }
        stamp[i] = stampId;
        act.push_back(i);
    }
    if (act.empty()) return;
    base.resize(act.size());

    // 1. accelerate, drag, walls
    for (size_t n = 0; n < act.size(); ++n) {
        const int i = act[n], x = i % W, y = i / W;
        const Cell& a = cells[i];
        const float pa = psi(a);
        base[n] = a.amt;
        // an empty cell inside a machine is vacuum and pulls the gas in; in the open air it stands for ambient air, which does
        // not suck a plume outwards (the plume still spreads and thins by the pressure solver and the open-world rules)
        const bool vacuum = outside.empty() || !outside[i];
        float dp[4]; bool blk[4]; int nBlk = 0;
        for (int k = 0; k < 4; ++k) {
            const int nx = x + DX4[k], ny = y + DY4[k];
            dp[k] = 0.f; blk[k] = true;
            if (!inb(nx, ny) || bodyMask[ny * W + nx] >= 0) { ++nBlk; continue; }
            const Cell& b = cells[ny * W + nx];
            if (b.t == M_EMPTY) { blk[k] = false; if (vacuum) dp[k] = -pa; }
            else if (MATS[b.t].kind == K_GAS) { blk[k] = false; if (b.t != M_FIRE) dp[k] = psi(b) - pa; }   // (a flame is left to its own devices)
            else ++nBlk;                                                                                       // wall, grains, liquid
        }
        // the push on a parcel is the pressure difference over the pressure it is at, so hot or compressed gas does not
        // accelerate without limit; below one atmosphere the difference itself counts (the pressure solver damps the rest)
        const float inv = K_ACC / std::max(1.f, pa);
        const float gx = (dp[0] - dp[1]) * (blk[0] || blk[1] ? 1.f : 0.5f);
        const float gy = (dp[2] - dp[3]) * (blk[2] || blk[3] ? 1.f : 0.5f);
        float vx = gasVX[i] - gx * inv, vy = gasVY[i] - gy * inv;   // (buoyancy stays with gasMove: a lift here would keep every plume cell busy)
        const float keep = std::max(0.f, 1.f - DRAG - WALL_DRAG * (float)nBlk);
        vx *= keep; vy *= keep;
        if ((vx > 0.f && blk[0]) || (vx < 0.f && blk[1])) vx = 0.f;   // nothing flows into a wall
        if ((vy > 0.f && blk[2]) || (vy < 0.f && blk[3])) vy = 0.f;
        const float s2 = vx * vx + vy * vy;
        if (s2 > VMAX * VMAX) { const float s = VMAX / std::sqrt(s2); vx *= s; vy *= s; }
        gasVX[i] = vx; gasVY[i] = vy;
    }
    // 2. viscosity: neighbouring parcels even out their velocities, weighted by how much gas each holds (momentum is conserved).
    // Each pair once, from a moving end: the lower-numbered one when both move. This is what lets a stream drag still gas along
    // and start it moving.
    auto moving = [&](int i) { return std::fabs(gasVX[i]) + std::fabs(gasVY[i]) >= STILL; };
    for (int i : act) {
        if (!moving(i)) continue;
        const int x = i % W, y = i / W;
        for (int k = 0; k < 4; ++k) {
            const int nx = x + DX4[k], ny = y + DY4[k];
            if (!inb(nx, ny)) continue;
            const int j = ny * W + nx;
            if (!openGas(j) || (stamp[j] == stampId && j < i && moving(j))) continue;
            const float mi = std::max(cells[i].amt, 0.01f), mj = std::max(cells[j].amt, 0.01f);
            const float cx = (mi * gasVX[i] + mj * gasVX[j]) / (mi + mj), cy = (mi * gasVY[i] + mj * gasVY[j]) / (mi + mj);
            gasVX[i] += VISC * (cx - gasVX[i]); gasVY[i] += VISC * (cy - gasVY[i]);
            gasVX[j] += VISC * (cx - gasVX[j]); gasVY[j] += VISC * (cy - gasVY[j]);
        }
    }
    // 3. advection: upwind, from the amount the cell held before this step, so what arrives this frame is not passed straight on
    const bool rev = tick & 1;
    for (size_t nn = 0; nn < act.size(); ++nn) {
        const size_t n = rev ? act.size() - 1 - nn : nn;
        const int i = act[n], x = i % W, y = i / W;
        Cell& a = cells[i];
        if (!openGas(i) || a.amt < 0.002f) continue;   // (it may have been swapped away or emptied by an earlier cell)
        float fx = std::fabs(gasVX[i]), fy = std::fabs(gasVY[i]);
        const float tot = fx + fy;
        if (tot < 0.005f) continue;
        if (tot > VMAX) { fx *= VMAX / tot; fy *= VMAX / tot; }
        for (int axis = 0; axis < 2; ++axis) {
            const float f = axis ? fy : fx;
            if (f <= 0.f) continue;
            float& vel = axis ? gasVY[i] : gasVX[i];
            const int sgn = vel > 0.f ? 1 : -1;
            const int nx = x + (axis ? 0 : sgn), ny = y + (axis ? sgn : 0);
            if (!inb(nx, ny) || bodyMask[ny * W + nx] >= 0) { vel = 0.f; continue; }
            const int j = ny * W + nx;
            Cell& b = cells[j];
            const float move = std::min(a.amt, f * base[n]);
            if (b.t == M_EMPTY) {
                if (move < 0.005f) continue;
                Cell c;
                c.t = a.t; c.clock = clk; c.var = a.var; c.temp = a.temp; c.amt = move;
                c.life = (a.t == M_VAPOR || a.t == M_AIR) ? a.life : (uint8_t)0;   // the fuel a vapour came from; intake air stays intake air
                b = c;
                a.amt -= move;
                gasVX[j] = gasVX[i]; gasVY[j] = gasVY[i];
            } else if (MATS[b.t].kind != K_GAS) {
                vel = 0.f;                                                          // wall, grains or liquid: the flow stops here
            } else if (b.t != M_FIRE && sameFluid(a, b)) {
                if (move < 0.001f) continue;
                const float nb = b.amt + move;
                b.temp = (b.temp * b.amt + a.temp * move) / nb;
                gasVX[j] = (gasVX[j] * b.amt + gasVX[i] * move) / nb;
                gasVY[j] = (gasVY[j] * b.amt + gasVY[i] * move) / nb;
                b.amt = nb;
                a.amt -= move;
            } else {
                // a different gas (or a flame) ahead: the two cannot share a cell, so the parcel shoves it along and now and then
                // they change places, as the pressure solver does, so a stream still works its way through foreign gas
                const float share = move / (b.amt + move);
                gasVX[j] += (gasVX[i] - gasVX[j]) * share;
                gasVY[j] += (gasVY[i] - gasVY[j]) * share;
                vel *= 0.5f;
                if (chance(f)) { std::swap(a, b); std::swap(gasVX[i], gasVX[j]); std::swap(gasVY[i], gasVY[j]); break; }
            }
        }
    }
}

// Liquids are incompressible but may be squeezed (amt > 1) when a rigid body displaces them in a sealed
// space. Pressure equalises across a whole connected body of liquid at once (Pascal), and any excess
// volume spills into free space next to it as whole cells.
void World::liquidPressure() {
    static std::vector<int> comp;
    comp.assign(W * H, -1);
    std::vector<int> stack, members, voids;
    static std::vector<int> voidMark;
    static int vstamp = 0;
    if (voidMark.empty()) voidMark.assign(W * H, 0);
    int nextId = 0;
    for (int i0 = 0; i0 < W * H; ++i0) {
        if (comp[i0] >= 0 || bodyMask[i0] >= 0 || MATS[cells[i0].t].kind != K_LIQUID) continue;
        int id = nextId++;
        ++vstamp;
        stack.clear(); members.clear(); voids.clear();
        comp[i0] = id;
        stack.push_back(i0);
        float excess = 0.f;
        while (!stack.empty()) {
            int p = stack.back();
            stack.pop_back();
            members.push_back(p);
            excess += std::max(0.f, cells[p].amt - 1.f);
            int px = p % W, py = p / W;
            for (int d = 0; d < 4; ++d) {
                int nx = px + DX4[d], ny = py + DY4[d];
                if (!inb(nx, ny)) continue;
                int j = ny * W + nx;
                if (bodyMask[j] >= 0) continue;
                Kind k = MATS[cells[j].t].kind;
                if (k == K_LIQUID) {
                    if (comp[j] < 0) { comp[j] = id; stack.push_back(j); }
                } else if (k == K_EMPTY && voidMark[j] != vstamp) {
                    voidMark[j] = vstamp;
                    voids.push_back(j);
                }
            }
        }
        if (excess <= 1e-4f) continue;
        int spill = std::min((int)excess, (int)voids.size());
        for (int n = 0; n < spill; ++n) {
            int vi = voids[rint((int)voids.size())];
            if (cells[vi].t != M_EMPTY) continue;
            // the new cell is a copy of a liquid cell that borders the gap (so a mixed body keeps its mix), never a burning one
            int src = -1, cnt = 0;
            for (int d = 0; d < 4; ++d) {
                int nx = vi % W + DX4[d], ny = vi / W + DY4[d];
                if (!inb(nx, ny) || bodyMask[ny * W + nx] >= 0 || MATS[cells[ny * W + nx].t].kind != K_LIQUID) continue;
                if (rint(++cnt) == 0) src = ny * W + nx;
            }
            Cell nc = cells[src >= 0 ? src : members[rint((int)members.size())]];
            nc.burn = 0;
            nc.amt = 1.f;
            nc.clock = clk;
            cells[vi] = nc;
            excess -= 1.f;
        }
        float amt = 1.f + std::max(0.f, excess) / (float)members.size();
        for (int m : members) cells[m].amt = amt;
    }
}

// ---------------------------------------------------------------------------
// Displacement by rigid bodies
// ---------------------------------------------------------------------------

namespace {
const std::vector<std::pair<int, int>>& offsets() {
    static std::vector<std::pair<int, int>> o;
    if (o.empty()) {
        const int R = 14;
        for (int dy = -R; dy <= R; ++dy)
            for (int dx = -R; dx <= R; ++dx)
                if (dx || dy) o.push_back({dx, dy});
        std::sort(o.begin(), o.end(), [](const auto& a, const auto& b) {
            return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second;
        });
    }
    return o;
}
}  // namespace

void World::displace(int x, int y) {
    int i = y * W + x;
    Cell c = cells[i];
    Kind k = MATS[c.t].kind;
    if (k == K_SOLID || k == K_EMPTY) return;
    cells[i].t = M_EMPTY;
    auto place = [&](int idx) {
        cells[idx] = c;
        cells[idx].clock = clk;
    };

    if (k == K_POWDER) {
        for (const auto& o : offsets()) {
            int nx = x + o.first, ny = y + o.second;
            if (isFree(nx, ny)) { place(ny * W + nx); return; }
        }
        return;
    }

    // Fluids move through connected fluid / free cells, so a piston never pushes them through its own seal.
    // Gases squash into a same-gas neighbour (compression); liquids look for free space and compress only when sealed in.
    static std::vector<int> mark;
    static int stamp = 0;
    if (mark.empty()) mark.assign(W * H, 0);
    ++stamp;
    std::vector<int> queue;
    queue.push_back(i);
    mark[i] = stamp;
    int firstSame = -1;
    const size_t limit = k == K_GAS ? 300 : 6000;
    for (size_t qi = 0; qi < queue.size() && queue.size() < limit; ++qi) {
        int p = queue[qi], px = p % W, py = p / W;
        for (int d = 0; d < 4; ++d) {
            int nx = px + DX4[d], ny = py + DY4[d];
            if (!inb(nx, ny)) continue;
            int j = ny * W + nx;
            if (mark[j] == stamp) continue;
            mark[j] = stamp;
            Cell& n = cells[j];
            if (bodyMask[j] >= 0) {
                // under a body: never a destination. A covered cell still holding the same fluid is one the body has just
                // moved onto and is about to clear as well, so the fluid may pass through it to the open cells beyond.
                if (n.t != M_EMPTY && n.t != M_FIRE && sameFluid(n, c)) queue.push_back(j);
                continue;
            }
            if (n.t == M_EMPTY) { place(j); return; }
            if (n.t != M_FIRE && sameFluid(n, c)) {
                if (k == K_GAS) {
                    float tot = n.amt + c.amt;
                    n.temp = (n.temp * n.amt + c.temp * c.amt) / tot;
                    n.amt = tot;
                    return;
                }
                if (firstSame < 0) firstSame = j;
                queue.push_back(j);
            }
        }
    }
    if (k == K_LIQUID && firstSame >= 0) cells[firstSame].amt += c.amt;  // sealed in: compression
}

// ---------------------------------------------------------------------------
// Explosions and painting
// ---------------------------------------------------------------------------

void World::explode(int cx, int cy, float r, float power) {
    struct Throw { int from, to; };
    std::vector<Throw> throws;
    int R = (int)std::ceil(r);
    for (int dy = -R; dy <= R; ++dy) {
        for (int dx = -R; dx <= R; ++dx) {
            float d = std::sqrt((float)(dx * dx + dy * dy));
            if (d > r) continue;
            int x = cx + dx, y = cy + dy;
            if (!inb(x, y)) continue;
            Cell& c = at(x, y);
            uint8_t t = c.t;
            if (t == M_WALL || t == M_VOID || t == M_SOURCE || t == M_HEATER || t == M_COOLER || t == M_IGNITER) continue;
            if (MATS[t].kind == K_SOLID && MATS[t].density > 6.f) continue;  // metals survive
            if (t == M_TNT && (dx || dy)) {
                if (c.life == 0) c.life = (uint8_t)(2 + rint(5));
                continue;
            }
            if (t == M_EMPTY) {
                if (d < r * 0.7f && chance(0.35f)) { spawn(x, y, M_FIRE, (uint8_t)(4 + rint(12)), 1400.f); at(x, y).amt = 1.5f; }
                continue;
            }
            if (d < r * 0.55f) {
                if (chance(0.6f)) { spawn(x, y, M_FIRE, (uint8_t)(6 + rint(18)), 1400.f); at(x, y).amt = 1.5f; }
                else c.t = M_EMPTY;
                continue;
            }
            Kind k = MATS[t].kind;
            if (k == K_POWDER || k == K_LIQUID || k == K_GAS) {
                float len = std::max(d, 0.5f);
                float push = r - d + 1.f + (float)rint(4);
                int tx = x + (int)std::lround(dx / len * push);
                int ty = y + (int)std::lround(dy / len * push);
                throws.push_back({y * W + x, inb(tx, ty) ? ty * W + tx : -1});
            } else if (d < r * 0.85f && chance(0.5f)) {
                if (MATS[t].ignT > 0.f && chance(0.4f)) spawn(x, y, M_FIRE, (uint8_t)(20 + rint(30)), 900.f);
                else c.t = M_EMPTY;
            }
        }
    }
    for (const Throw& th : throws) {
        Cell& src = cells[th.from];
        if (src.t == M_EMPTY) continue;
        if (th.to >= 0 && cells[th.to].t == M_EMPTY && bodyMask[th.to] < 0) {
            cells[th.to] = src;
            cells[th.to].clock = clk;
        }
        else if (th.to >= 0) continue;   // the landing place is taken: the material stays where it was
        if (th.to >= 0 || chance(0.5f)) src.t = M_EMPTY;
    }
    blasts.push_back({(float)cx, (float)cy, r, power});
}

void World::paint(int cx, int cy, int r, uint8_t t, float amount, uint8_t payload) {
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy > r * r + r) continue;
            int x = cx + dx, y = cy + dy;
            if (!inb(x, y)) continue;
            int i = y * W + x;
            if (t == M_EMPTY) { cells[i] = Cell{}; continue; }
            bool batt = t == M_BATT_POS || t == M_BATT_NEG;
            if (batt && (cells[i].t == M_BATT_POS || cells[i].t == M_BATT_NEG) && bodyMask[i] < 0) { setCell(x, y, t); continue; }  // repaint = new settings
            if (bodyMask[i] >= 0 || cells[i].t != M_EMPTY) continue;
            if (MATS[t].kind == K_SOLID || chance(amount)) {
                setCell(x, y, t);
                if (t == M_SOURCE) cells[i].life = payload;
            }
        }
    }
}

void World::paintLine(int x0, int y0, int x1, int y1, int r, uint8_t t, float amount, uint8_t payload) {
    int n = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    int step = std::max(1, r / 2);
    for (int i = 0; i <= n; i += step) {
        float f = n ? (float)i / n : 0.f;
        paint((int)std::lround(x0 + (x1 - x0) * f), (int)std::lround(y0 + (y1 - y0) * f), r, t, amount, payload);
    }
    paint(x1, y1, r, t, amount, payload);
}

void World::fillRect(int x0, int y0, int x1, int y1, uint8_t t, uint8_t payload) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            if (!inb(x, y)) continue;
            if (t == M_EMPTY) { at(x, y) = Cell{}; continue; }
            setCell(x, y, t);
            if (t == M_SOURCE) at(x, y).life = payload;
        }
}
