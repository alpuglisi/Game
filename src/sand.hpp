#pragma once
#include <cstdint>
#include <vector>
#include "materials.hpp"

struct Cell {
    uint8_t t = M_EMPTY;  // material
    uint8_t clock = 0;    // parity of last frame this cell was updated
    uint8_t life = 0;     // fire/smoke/steam lifetime, TNT fuse
    uint8_t var = 0;      // random colour variation
};

// An explosion event handed to the rigid-body world so bodies get pushed.
struct Blast {
    float x, y, r, power;
};

class World {
public:
    static constexpr int W = 400;
    static constexpr int H = 240;

    std::vector<Cell> cells;
    std::vector<int16_t> bodyMask;  // id of rigid body covering the cell, or -1
    std::vector<Blast> blasts;

    World();
    void clear();
    void step();

    bool inb(int x, int y) const { return (unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H; }
    Cell& at(int x, int y) { return cells[y * W + x]; }
    const Cell& at(int x, int y) const { return cells[y * W + x]; }

    // Rigid bodies collide with solids and powders (and the world border).
    bool isTerrain(int x, int y) const;
    bool isFree(int x, int y) const;

    void setCell(int x, int y, uint8_t t);
    void spawn(int x, int y, uint8_t t, uint8_t life);
    void paint(int cx, int cy, int r, uint8_t t, float amount);
    void paintLine(int x0, int y0, int x1, int y1, int r, uint8_t t, float amount);
    void fillRect(int x0, int y0, int x1, int y1, uint8_t t);
    void explode(int cx, int cy, float r, float power);

    uint32_t rnd();
    int rint(int n) { return (int)(rnd() % (uint32_t)n); }
    bool chance(float p) { return (rnd() & 0xFFFF) < p * 65536.f; }

private:
    uint8_t clk = 0;
    uint32_t tick = 0;
    uint32_t rng = 0x9E3779B9u;

    void moveTo(int x, int y, int nx, int ny);
    bool canDisplace(int x, int y, float dens) const;  // moving down / sideways
    bool canRise(int x, int y, float dens) const;      // gas moving up
    void updateCell(int x, int y);
    void powder(int x, int y);
    void liquid(int x, int y, int disp);
    void gas(int x, int y);
    void fireCell(int x, int y);
    void lavaCell(int x, int y);
    void acidCell(int x, int y);
    void plantCell(int x, int y);
    void iceCell(int x, int y);
    void tntCell(int x, int y);
    void voidCell(int x, int y);
    bool ignite(int x, int y, float mult);
};
