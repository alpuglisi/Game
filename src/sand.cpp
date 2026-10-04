#include "sand.hpp"
#include <algorithm>
#include <cmath>

namespace {
const int DX4[4] = {1, -1, 0, 0};
const int DY4[4] = {0, 0, 1, -1};

uint8_t initialLife(World& w, uint8_t t) {
    switch (t) {
        case M_FIRE:  return (uint8_t)(25 + w.rint(35));
        case M_SMOKE: return (uint8_t)(90 + w.rint(110));
        case M_STEAM: return (uint8_t)(160 + w.rint(90));
        default:      return 0;
    }
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
    c.t = t;
    c.var = (uint8_t)rnd();
    c.clock = clk;
    c.life = initialLife(*this, t);
}

void World::spawn(int x, int y, uint8_t t, uint8_t life) {
    setCell(x, y, t);
    at(x, y).life = life;
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

bool World::canRise(int x, int y, float dens) const {
    if (!inb(x, y)) return false;
    int i = y * W + x;
    if (bodyMask[i] >= 0) return false;
    uint8_t t = cells[i].t;
    if (t == M_EMPTY) return true;
    Kind k = MATS[t].kind;
    return (k == K_LIQUID || k == K_GAS) && dens < MATS[t].density;
}

void World::moveTo(int x, int y, int nx, int ny) {
    std::swap(cells[y * W + x], cells[ny * W + nx]);
    cells[y * W + x].clock = clk;
    cells[ny * W + nx].clock = clk;
}

void World::step() {
    clk ^= 1;
    ++tick;
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
}

void World::updateCell(int x, int y) {
    switch (at(x, y).t) {
        case M_SAND: case M_ASH: case M_GUNPOWDER: powder(x, y); break;
        case M_WATER: liquid(x, y, 6); break;
        case M_OIL:   liquid(x, y, 4); break;
        case M_LAVA:  lavaCell(x, y); break;
        case M_ACID:  acidCell(x, y); break;
        case M_FIRE:  fireCell(x, y); break;
        case M_SMOKE: case M_STEAM: gas(x, y); break;
        case M_PLANT: plantCell(x, y); break;
        case M_ICE:   iceCell(x, y); break;
        case M_TNT:   tntCell(x, y); break;
        case M_VOID:  voidCell(x, y); break;
        default: break;
    }
}

void World::powder(int x, int y) {
    float d = MATS[at(x, y).t].density;
    if (canDisplace(x, y + 1, d)) {
        // sink through liquids more slowly than through air
        if (at(x, y + 1).t == M_EMPTY || rint(3)) { moveTo(x, y, x, y + 1); return; }
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
    float d = MATS[at(x, y).t].density;
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

void World::gas(int x, int y) {
    Cell& c = at(x, y);
    if (c.life > 0) {
        --c.life;
    } else {
        if (c.t == M_STEAM) setCell(x, y, M_WATER); else c.t = M_EMPTY;
        return;
    }
    uint8_t t = c.t;
    float d = MATS[t].density;
    int dir = (rnd() & 1) ? 1 : -1;
    if (rint(5) && canRise(x, y - 1, d)) { moveTo(x, y, x, y - 1); return; }
    if (canRise(x + dir, y - 1, d)) { moveTo(x, y, x + dir, y - 1); return; }
    if (canRise(x - dir, y - 1, d)) { moveTo(x, y, x - dir, y - 1); return; }
    if (rint(2) && isFree(x + dir, y)) moveTo(x, y, x + dir, y);
    // steam condenses against cold surfaces
    if (t == M_STEAM) {
        for (int k = 0; k < 4; ++k) {
            int nx = x + DX4[k], ny = y + DY4[k];
            if (inb(nx, ny) && at(nx, ny).t == M_ICE) { setCell(x, y, M_WATER); return; }
        }
    }
}

bool World::ignite(int x, int y, float mult) {
    Cell& c = at(x, y);
    switch (c.t) {
        case M_WOOD:
            if (chance(0.012f * mult)) { spawn(x, y, M_FIRE, (uint8_t)(50 + rint(60))); return true; }
            break;
        case M_PLANT:
            if (chance(0.06f * mult)) { spawn(x, y, M_FIRE, (uint8_t)(20 + rint(20))); return true; }
            break;
        case M_OIL:
            if (chance(0.25f * mult)) { spawn(x, y, M_FIRE, (uint8_t)(25 + rint(30))); return true; }
            break;
        case M_GUNPOWDER:
            if (chance(0.6f * mult)) {
                spawn(x, y, M_FIRE, 8);
                explode(x, y, 3.5f, 45.f);
                return true;
            }
            break;
        case M_TNT:
            if (c.life == 0 && chance(0.2f * mult)) c.life = (uint8_t)(3 + rint(6));
            break;
        case M_ICE:
            if (chance(0.05f * mult)) { setCell(x, y, M_WATER); return true; }
            break;
        default: break;
    }
    return false;
}

void World::fireCell(int x, int y) {
    Cell& c = at(x, y);
    if (c.life == 0) {
        int r = rint(40);
        if (r < 8) setCell(x, y, M_SMOKE);
        else if (r == 8) setCell(x, y, M_ASH);
        else c.t = M_EMPTY;
        return;
    }
    --c.life;
    for (int k = 0; k < 4; ++k) {
        int nx = x + DX4[k], ny = y + DY4[k];
        if (!inb(nx, ny)) continue;
        uint8_t nt = at(nx, ny).t;
        if (nt == M_WATER) {
            setCell(nx, ny, M_STEAM);
            at(x, y).t = M_EMPTY;
            return;
        }
        if (nt != M_EMPTY) ignite(nx, ny, 1.f);
    }
    if (chance(0.04f) && isFree(x, y - 1)) setCell(x, y - 1, M_SMOKE);
    if (chance(0.5f)) {
        int nx = x + rint(3) - 1;
        int ny = y - (chance(0.7f) ? 1 : 0);
        if ((nx != x || ny != y) && isFree(nx, ny)) moveTo(x, y, nx, ny);
    }
}

void World::lavaCell(int x, int y) {
    for (int k = 0; k < 4; ++k) {
        int nx = x + DX4[k], ny = y + DY4[k];
        if (!inb(nx, ny)) continue;
        uint8_t nt = at(nx, ny).t;
        if (nt == M_WATER) {
            setCell(nx, ny, M_STEAM);
            if (chance(0.4f)) { setCell(x, y, M_STONE); return; }
        } else if (nt != M_EMPTY && nt != M_LAVA) {
            ignite(nx, ny, 6.f);
        }
    }
    if (chance(0.004f) && isFree(x, y - 1)) spawn(x, y - 1, M_FIRE, (uint8_t)(6 + rint(6)));
    if (rint(3) == 0) liquid(x, y, 2);
}

void World::acidCell(int x, int y) {
    for (int k = 0; k < 4; ++k) {
        int nx = x + DX4[k], ny = y + DY4[k];
        if (!inb(nx, ny)) continue;
        switch (at(nx, ny).t) {
            case M_SAND: case M_STONE: case M_WOOD: case M_ICE: case M_PLANT:
            case M_ASH: case M_GUNPOWDER: case M_TNT:
                if (chance(0.08f)) {
                    if (chance(0.3f)) setCell(nx, ny, M_SMOKE); else at(nx, ny).t = M_EMPTY;
                    if (chance(0.35f)) { at(x, y).t = M_EMPTY; return; }
                }
                break;
            default: break;
        }
    }
    liquid(x, y, 4);
}

void World::plantCell(int x, int y) {
    int k = rint(4);
    int nx = x + DX4[k], ny = y + DY4[k];
    if (inb(nx, ny) && at(nx, ny).t == M_WATER && chance(0.05f)) setCell(nx, ny, M_PLANT);
}

void World::iceCell(int x, int y) {
    int k = rint(4);
    int nx = x + DX4[k], ny = y + DY4[k];
    if (inb(nx, ny) && at(nx, ny).t == M_WATER && chance(0.02f)) setCell(nx, ny, M_ICE);
}

void World::tntCell(int x, int y) {
    Cell& c = at(x, y);
    if (c.life > 0 && --c.life == 0) explode(x, y, 24.f, 260.f);
}

void World::voidCell(int x, int y) {
    for (int k = 0; k < 4; ++k) {
        int nx = x + DX4[k], ny = y + DY4[k];
        if (!inb(nx, ny)) continue;
        uint8_t nt = at(nx, ny).t;
        if (nt != M_EMPTY && nt != M_WALL && nt != M_VOID) at(nx, ny).t = M_EMPTY;
    }
}

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
            if (t == M_WALL || t == M_VOID) continue;
            if (t == M_TNT && (dx || dy)) {
                if (c.life == 0) c.life = (uint8_t)(2 + rint(5));
                continue;
            }
            if (t == M_EMPTY) {
                if (d < r * 0.7f && chance(0.35f)) spawn(x, y, M_FIRE, (uint8_t)(4 + rint(12)));
                continue;
            }
            if (d < r * 0.55f) {
                if (chance(0.6f)) spawn(x, y, M_FIRE, (uint8_t)(6 + rint(18)));
                else c.t = M_EMPTY;
                continue;
            }
            Kind k = MATS[t].kind;
            if (k == K_POWDER || k == K_LIQUID || k == K_GAS) {
                float len = std::max(d, 0.5f);
                float push = r - d + 1.f + (float)rint(4);
                int tx = x + (int)std::lround(dx / len * push);
                int ty = y + (int)std::lround(dy / len * push);
                throws.push_back({y * W + x, ty * W + tx});
                if (!inb(tx, ty)) throws.back().to = -1;
            } else if (d < r * 0.85f && chance(0.5f)) {
                if (t == M_WOOD && chance(0.4f)) spawn(x, y, M_FIRE, (uint8_t)(20 + rint(30)));
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

void World::paint(int cx, int cy, int r, uint8_t t, float amount) {
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy > r * r + r) continue;
            int x = cx + dx, y = cy + dy;
            if (!inb(x, y)) continue;
            int i = y * W + x;
            if (t == M_EMPTY) { cells[i].t = M_EMPTY; continue; }
            if (bodyMask[i] >= 0 || cells[i].t != M_EMPTY) continue;
            if (MATS[t].kind == K_SOLID || chance(amount)) setCell(x, y, t);
        }
    }
}

void World::paintLine(int x0, int y0, int x1, int y1, int r, uint8_t t, float amount) {
    int n = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    int step = std::max(1, r / 2);
    for (int i = 0; i <= n; i += step) {
        float f = n ? (float)i / n : 0.f;
        paint((int)std::lround(x0 + (x1 - x0) * f), (int)std::lround(y0 + (y1 - y0) * f), r, t, amount);
    }
    paint(x1, y1, r, t, amount);
}

void World::fillRect(int x0, int y0, int x1, int y1, uint8_t t) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (inb(x, y)) { if (t == M_EMPTY) at(x, y).t = M_EMPTY; else setCell(x, y, t); }
}
