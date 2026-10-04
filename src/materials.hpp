#pragma once
#include <cstdint>

enum Mat : uint8_t {
    M_EMPTY = 0, M_WALL, M_SAND, M_WATER, M_OIL, M_FIRE, M_SMOKE, M_STEAM, M_PLANT,
    M_LAVA, M_STONE, M_ICE, M_ACID, M_GUNPOWDER, M_WOOD, M_TNT, M_ASH, M_VOID,
    M_COUNT
};

enum Kind : uint8_t { K_EMPTY, K_SOLID, K_POWDER, K_LIQUID, K_GAS };

struct MatInfo {
    const char* name;
    uint32_t color;  // 0xRRGGBB
    Kind kind;
    float density;   // used for sinking/floating; also fluid density for buoyancy
};

inline const MatInfo MATS[M_COUNT] = {
    {"EMPTY",     0x0b0e14, K_EMPTY,  0.0f},
    {"WALL",      0x8a9099, K_SOLID,  100.f},
    {"SAND",      0xe0c070, K_POWDER, 3.0f},
    {"WATER",     0x2f6fe0, K_LIQUID, 1.0f},
    {"OIL",       0x6b4a1e, K_LIQUID, 0.8f},
    {"FIRE",      0xff7a18, K_GAS,    -0.5f},
    {"SMOKE",     0x555a60, K_GAS,    -0.2f},
    {"STEAM",     0xcfd8e3, K_GAS,    -0.3f},
    {"PLANT",     0x2fa84f, K_SOLID,  100.f},
    {"LAVA",      0xff4500, K_LIQUID, 2.5f},
    {"STONE",     0x6e6e73, K_SOLID,  100.f},
    {"ICE",       0xa5e3ff, K_SOLID,  100.f},
    {"ACID",      0x9dff2e, K_LIQUID, 1.2f},
    {"GUNPOWDER", 0x4a4a4a, K_POWDER, 2.8f},
    {"WOOD",      0x8b5a2b, K_SOLID,  100.f},
    {"TNT",       0xc0302a, K_SOLID,  100.f},
    {"ASH",       0x9a9a9a, K_POWDER, 0.6f},
    {"VOID",      0x2a0a3a, K_SOLID,  100.f},
};
