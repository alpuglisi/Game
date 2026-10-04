#pragma once
#include <cstdint>
#include <utility>
#include <vector>
#include "state.hpp"
#include "materials.hpp"

struct Cell {
    uint8_t t = M_EMPTY;   // material
    uint8_t clock = 0;     // parity of last frame this cell was updated
    uint8_t life = 0;      // multipurpose: flame life, phase-change progress, origin material, TNT fuse, source payload
    uint8_t var = 0;       // colour variation (flames: residue material)
    uint8_t burn = 0;      // >0: a burning solid/powder/liquid, frames of fuel left
    uint8_t aux = 0;       // spare: a SOURCE cell keeps the fuel its vapour comes from here
    float temp = AMBIENT_T;
    float amt = 1.f;       // gases: amount of gas in the cell (pressure = amt * T); liquids: >1 means compressed
};

inline float cellCap(const Cell& c) {
    const MatInfo& m = MATS[c.t];
    return m.kind == K_GAS ? m.cap * (c.amt > 0.05f ? c.amt : 0.05f) : m.cap;
}
inline float cellCond(const Cell& c) {
    const MatInfo& m = MATS[c.t];
    return m.kind == K_GAS ? m.cond * (c.amt < 2.f ? c.amt : 2.f) : m.cond;
}

// An explosion event handed to the rigid-body world so bodies get pushed.
struct Blast {
    float x, y, r, power;
};

class World {
public:
    static constexpr int W = 1200;
    static constexpr int H = 240;

    std::vector<Cell> cells;
    std::vector<int16_t> bodyMask;  // id of rigid body covering the cell, or -1
    std::vector<Blast> blasts;
    long burnEvents = 0;                            // dev: number of ignitions so far

    // spark plugs: pulse every sparkPeriod frames, or continuously while sparkHeld
    int sparkPeriod = 60;
    bool sparkHeld = false;
    bool sparkNow = false;

    // ---- electricity (see elec.cpp): battery cells fix a potential, conductors form a resistor network
    float battV = 12.f, battA = 20.f;            // settings stamped into battery cells as they are painted
    std::vector<float> volt, curr;               // per-cell potential (V) and current (A), for display and arcs
    std::vector<float> bodySigma;                // conductance of each rigid body (set by the physics step)
    struct Arc { int x0, y0, x1, y1, frames; };
    std::vector<Arc> arcs;                       // sparks that jumped an air gap recently
    long arcCount = 0;
    float vMax = 0.f, iSource = 0.f;             // dev/UI: highest potential, total source current
    static uint8_t encV(float v);
    static float decV(uint8_t c);
    static uint8_t encA(float a);
    static float decA(uint8_t c);

    // impact-sensitive primer cells and the flash a spent primer body throws out
    void primerStrike(int x, int y);
    void flashAt(int x, int y);

    World();
    void clear();
    void save(Writer& w) const;
    bool load(Reader& r);
    void step();

    bool inb(int x, int y) const { return (unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H; }
    Cell& at(int x, int y) { return cells[y * W + x]; }
    const Cell& at(int x, int y) const { return cells[y * W + x]; }

    // Rigid bodies collide with solids and powders (and the world border).
    bool isTerrain(int x, int y) const;
    bool isFree(int x, int y) const;

    void setCell(int x, int y, uint8_t t);
    void spawn(int x, int y, uint8_t t, uint8_t life, float temp = -1e9f);
    void ignitePoint(int x, int y) { if (inb(x, y)) sparkAt(x, y); }
    void paint(int cx, int cy, int r, uint8_t t, float amount, uint8_t payload = 0);
    void paintLine(int x0, int y0, int x1, int y1, int r, uint8_t t, float amount, uint8_t payload = 0);
    void fillRect(int x0, int y0, int x1, int y1, uint8_t t, uint8_t payload = 0);
    void explode(int cx, int cy, float r, float power);
    // A rigid body has just covered this cell: move its contents out of the way.
    void displace(int x, int y);

    uint32_t rnd();
    int rint(int n) { return (int)(rnd() % (uint32_t)n); }
    bool chance(float p) { return (rnd() & 0xFFFF) < p * 65536.f; }

private:
    uint8_t clk = 0;
    uint32_t tick = 0;
    uint32_t sparkTimer = 0;
    uint32_t rng = 0x9E3779B9u;

    void moveTo(int x, int y, int nx, int ny);
    bool canDisplace(int x, int y, float dens) const;  // moving down / sideways
    bool canRise(int x, int y) const;                  // a gas bubble rising
    void convert(int x, int y, uint8_t to, uint8_t origin, float temp, float amt);

    void thermalPass();
    void gasFlux();
    void liquidPressure();

    void updateCell(int x, int y);
    bool phase(int x, int y);
    bool tryIgnite(int x, int y);
    void sparkAt(int x, int y);
    void burn(int x, int y, const MatInfo& m);
    void burnTick(int x, int y);
    void powder(int x, int y);
    void liquid(int x, int y, int disp);
    void gasMove(int x, int y);
    void fireCell(int x, int y);
    void acidCell(int x, int y);
    void plantCell(int x, int y);
    void tntCell(int x, int y);
    void voidCell(int x, int y);
    void sourceCell(int x, int y);
    void primerCell(int x, int y);
    void burstCheck(int x, int y);
    void electricity();
    std::vector<int> elecIdx, elecCool;
    std::vector<float> elecRaw;
    bool hadElec = false;
};
