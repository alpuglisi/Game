#include "sand.hpp"
#include <algorithm>
#include <cmath>

namespace {
const int DX4[4] = {1, -1, 0, 0};
const int DY4[4] = {0, 0, 1, -1};

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
    blasts.clear();
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

    electricity();
    thermalPass();

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

    gasFlux();
    liquidPressure();
}

void World::thermalPass() {
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
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

            if (x + 1 < W && cells[i + 1].t != M_EMPTY) exchange(a, cells[i + 1]);
            if (y + 1 < H && cells[i + W].t != M_EMPTY) exchange(a, cells[i + W]);

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
    if (c.burn > 0 && m.kind != K_GAS) burnTick(x, y);
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
        float add = std::fabs(c.temp - edge) * cellCap(c) * 0.5f;
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

// A spark: ignites a flammable cell if it is above its flash point.
void World::sparkAt(int x, int y) {
    Cell& c = at(x, y);
    const MatInfo* m = &MATS[c.t];
    if (c.t == M_VAPOR) m = &MATS[c.life < M_COUNT ? c.life : (uint8_t)M_GASOLINE];
    if (m->ignT <= 0.f || c.burn > 0 || (c.t == M_TNT && c.life > 0)) return;
    if (c.temp < m->flashT) return;
    if (m->kind != K_GAS && c.t != M_VAPOR) {
        bool open = false;
        for (int k = 0; k < 4 && !open; ++k) {
            int nx = x + DX4[k], ny = y + DY4[k];
            if (!inb(nx, ny)) continue;
            uint8_t nt = cells[ny * W + nx].t;
            open = nt == M_EMPTY || MATS[nt].kind == K_GAS;
        }
        if (!open) return;
    }
    burn(x, y, *m);
}

bool World::tryIgnite(int x, int y) {
    Cell& c = at(x, y);
    const MatInfo* m = &MATS[c.t];
    if (c.t == M_VAPOR) m = &MATS[c.life < M_COUNT ? c.life : (uint8_t)M_GASOLINE];
    if (m->ignT <= 0.f) return false;
    if (c.t == M_TNT && c.life > 0) return false;

    bool gasFuel = m->kind == K_GAS || c.t == M_VAPOR;
    if (!gasFuel) {  // needs air: an open face
        bool open = false;
        for (int k = 0; k < 4 && !open; ++k) {
            int nx = x + DX4[k], ny = y + DY4[k];
            if (!inb(nx, ny)) continue;
            uint8_t nt = cells[ny * W + nx].t;
            open = nt == M_EMPTY || MATS[nt].kind == K_GAS;
        }
        if (!open) return false;
    }
    bool hot = c.temp >= m->ignT;
    // a surface far above the ignition temperature lights the fuel on contact (glow plugs, lava, red-hot metal)
    for (int k = 0; k < 4 && !hot; ++k) {
        int nx = x + DX4[k], ny = y + DY4[k];
        if (!inb(nx, ny)) continue;
        const Cell& nc = cells[ny * W + nx];
        if (MATS[nc.t].kind != K_GAS && nc.t != M_EMPTY && nc.t != M_FIRE && nc.temp >= m->ignT + 120.f) hot = true;
    }
    bool flame = false;
    if (!hot && c.temp >= m->flashT) {
        for (int k = 0; k < 4 && !flame; ++k) {
            int nx = x + DX4[k], ny = y + DY4[k];
            if (inb(nx, ny) && cells[ny * W + nx].t == M_FIRE) flame = true;
        }
    }
    if (hot || (flame && chance(m->burnSpeed))) {
        burn(x, y, *m);
        return true;
    }
    return false;
}

void World::burn(int x, int y, const MatInfo& m) {
    ++burnEvents;
    Cell& c = at(x, y);
    if (c.t == M_TNT) {
        if (c.life == 0) c.life = (uint8_t)(2 + rint(5));
        return;
    }
    if (m.blastR > 0.f) explode(x, y, m.blastR, m.blastP);
    Cell& cc = at(x, y);
    if (MATS[cc.t].kind == K_GAS) {  // gas-phase combustion: the whole mixture becomes flame
        cc.var = 0;
        cc.life = (uint8_t)(cc.t == M_VAPOR ? 8 : std::min(255, m.burnTime));
        cc.temp = std::max(cc.temp, m.burnT);
        cc.t = M_FIRE;
        cc.burn = 0;
        cc.clock = clk;
        return;
    }
    cc.burn = (uint8_t)std::min(255, std::max(1, m.burnTime));
    cc.temp = std::max(cc.temp, m.burnT);
}

void World::burnTick(int x, int y) {
    Cell& c = at(x, y);
    const MatInfo& m = MATS[c.t];
    // water puts fires out
    for (int k = 0; k < 4; ++k) {
        int nx = x + DX4[k], ny = y + DY4[k];
        if (inb(nx, ny) && cells[ny * W + nx].t == M_WATER) { c.burn = 0; return; }
    }
    c.temp = std::max(c.temp, m.burnT * 0.8f);
    if (chance(0.5f)) {
        int k = rint(4);
        int nx = x + DX4[k], ny = y + DY4[k];
        if (isFree(nx, ny)) {
            spawn(nx, ny, M_FIRE, (uint8_t)(4 + rint(7)), m.burnT);
            at(nx, ny).amt = 0.6f;
        }
    }
    if (--c.burn == 0) {
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
    int dir = (rnd() & 1) ? 1 : -1;
    if (canDisplace(x + dir, y + 1, d) && isFree(x + dir, y)) { moveTo(x, y, x + dir, y + 1); return; }
    if (canDisplace(x - dir, y + 1, d) && isFree(x - dir, y)) { moveTo(x, y, x - dir, y + 1); return; }
    for (int pass = 0; pass < 2; ++pass, dir = -dir) {
        int cx = x;
        for (int s = 1; s <= disp; ++s) {
            if (isFree(x + dir * s, y)) cx = x + dir * s; else break;
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
    if (canRise(x, y + dy) && (isFree(x, y + dy) || rint(2))) { moveTo(x, y, x, y + dy); return; }
    if (isFree(x + dir, y + dy)) { moveTo(x, y, x + dir, y + dy); return; }
    if (isFree(x - dir, y + dy)) { moveTo(x, y, x - dir, y + dy); return; }
}

void World::fireCell(int x, int y) {
    Cell& c = at(x, y);
    if (c.life == 0 || c.temp < 350.f) {
        float amt = c.amt, temp = c.temp;
        uint8_t res = c.var;
        if (res != M_EMPTY && res < M_COUNT && chance(0.15f)) { setCell(x, y, res); at(x, y).temp = temp; return; }
        convert(x, y, chance(0.3f) ? M_SMOKE : M_EXHAUST, 0, temp, std::max(0.1f, amt));
        return;
    }
    --c.life;
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
    static std::vector<int> active;
    active.clear();
    for (int i = 0; i < W * H; ++i)
        if (MATS[cells[i].t].kind == K_GAS && cells[i].t != M_FIRE) active.push_back(i);
    if (active.empty()) return;

    // Moves gas between cells ia (a gas cell) and ib. Returns the index of a newly created cell, or -1.
    auto flux = [&](int ia, int ib) -> int {
        if (bodyMask[ia] >= 0 || bodyMask[ib] >= 0) return -1;
        Cell& a = cells[ia];
        Cell& b = cells[ib];
        bool gb = MATS[b.t].kind == K_GAS && b.t != M_FIRE;
        if (gb && !sameFluid(a, b)) {
            if (rint(16) == 0) std::swap(a, b);  // different gases slowly mix
            return -1;
        }
        if (b.t == M_EMPTY) {
            float f = a.amt * 0.4f;
            if (f < 0.005f) return -1;
            Cell n;
            n.t = a.t; n.clock = clk; n.life = a.t == M_VAPOR ? a.life : (uint8_t)0; n.var = a.var; n.temp = a.temp; n.amt = f;
            b = n;
            a.amt -= f;
            return ib;
        }
        if (gb) {
            float ta = a.temp + 273.f, tb = b.temp + 273.f;
            float psiA = a.amt * ta, psiB = b.amt * tb;
            Cell& hi = psiA > psiB ? a : b;
            Cell& lo = psiA > psiB ? b : a;
            float f = 0.8f * std::fabs(psiA - psiB) / (ta + tb);
            f = std::min(f, hi.amt * 0.9f);
            if (f <= 0.f) return -1;
            float nb = lo.amt + f;
            lo.temp = (lo.temp * lo.amt + hi.temp * f) / nb;
            lo.amt = nb;
            hi.amt -= f;
        }
        return -1;
    };
    for (int pass = 0; pass < 24; ++pass) {
        size_t n = active.size();
        bool rev = (tick + pass) & 1;
        for (size_t k = 0; k < n; ++k) {
            int i = active[rev ? n - 1 - k : k];
            Cell& a = cells[i];
            if (MATS[a.t].kind != K_GAS || a.t == M_FIRE) continue;
            int x = i % W, y = i / W;
            int nbs[4] = {x + 1 < W ? i + 1 : -1, x > 0 ? i - 1 : -1, y + 1 < H ? i + W : -1, y > 0 ? i - W : -1};
            for (int d = 0; d < 4; ++d) {
                int j = nbs[d];
                if (j < 0) continue;
                // gas-gas pairs are visited from both ends: handle each once
                if (MATS[cells[j].t].kind == K_GAS && cells[j].t != M_FIRE && j < i) continue;
                if (cells[i].t == M_EMPTY || MATS[cells[i].t].kind != K_GAS) break;
                int made = flux(i, j);
                if (made >= 0) active.push_back(made);
            }
        }
    }
    for (int i : active)
        if (MATS[cells[i].t].kind == K_GAS && cells[i].amt < 0.004f) cells[i].t = M_EMPTY;
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
            Cell nc = cells[members[rint((int)members.size())]];
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
            if (mark[j] == stamp || bodyMask[j] >= 0) continue;
            mark[j] = stamp;
            Cell& n = cells[j];
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
