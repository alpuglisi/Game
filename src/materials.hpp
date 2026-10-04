#pragma once
#include <array>
#include <cstdint>

enum Mat : uint8_t {
    M_EMPTY = 0,
    // powders
    M_SAND, M_ASH, M_GUNPOWDER, M_COAL,
    // liquids
    M_WATER, M_OIL, M_GASOLINE, M_DIESEL, M_KEROSENE, M_JETFUEL, M_ETHANOL, M_HYDRAULIC, M_ACID, M_LAVA, M_MOLTEN,
    // gases
    M_STEAM, M_FIRE, M_SMOKE, M_EXHAUST, M_VAPOR, M_PROPANE, M_HYDROGEN, M_AIR,
    // metals
    M_STEEL, M_IRON, M_COPPER, M_ALUMINUM, M_LEAD, M_GOLD, M_TITANIUM, M_TUNGSTEN,
    // structural / other solids
    M_WALL, M_STONE, M_CONCRETE, M_BRICK, M_CERAMIC, M_GLASS, M_WOOD, M_RUBBER, M_PLASTIC, M_ICE, M_PLANT, M_TNT,
    // devices
    M_HEATER, M_COOLER, M_IGNITER, M_SOURCE, M_VOID,
    // electrical, frangible and impact-sensitive
    M_BATT_POS, M_BATT_NEG, M_SOLDER, M_PARAFFIN, M_WAX, M_PRIMER,
    M_COUNT
};

enum Kind : uint8_t { K_EMPTY, K_SOLID, K_POWDER, K_LIQUID, K_GAS };

constexpr float AMBIENT_T = 20.f;

struct MatInfo {
    const char* name = "?";
    uint32_t color = 0;  // 0xRRGGBB
    Kind kind = K_EMPTY;
    float density = 1.f;      // relative to water; drives sinking, buoyancy and body mass
    float cond = 0.02f;       // thermal conductivity (game units)
    float cap = 1.f;          // volumetric heat capacity (per cell, per unit of gas amount)
    float friction = 0.5f;    // surface friction for rigid bodies
    float restitution = 0.1f; // bounciness for rigid bodies
    // phase changes (heating / cooling). latent > 0 means the change takes energy.
    float hiT = 1e9f; uint8_t hiTo = M_EMPTY;
    float loT = -1e9f; uint8_t loTo = M_EMPTY;
    float latent = 0.f;
    float expand = 1.f;       // gas volume produced per liquid cell when boiled
    // combustion
    float ignT = 0.f;         // auto-ignition temperature (0 = not flammable)
    float flashT = 0.f;       // ignites from an adjacent flame above this temperature
    float burnT = 0.f;        // flame temperature when burning
    int burnTime = 0;         // frames of flame produced
    float burnSpeed = 0.f;    // chance per frame of catching from a neighbouring flame
    uint8_t burnRes = M_EMPTY;// residue after burning
    float volatility = 0.f;   // evaporation rate at ambient temperature
    float blastR = 0.f, blastP = 0.f;  // explosive properties
    float buoy = 0.f;         // gases: >0 rises when hot, <0 sinks
    float acidK = 1.f;        // susceptibility to acid
    float bulk = 1.f;         // liquids: stiffness against compression
    float elec = 0.f;         // electrical conductance of one cell edge (siemens, game units); 0 = insulator
    float burstP = 0.f;       // solids: cracks when the pressure on one side exceeds this (0 = never)
    float initT = AMBIENT_T;  // temperature of freshly created cells
    uint8_t freezeAs = M_EMPTY; // what a molten version of this solidifies into (default: itself)
};

inline std::array<MatInfo, M_COUNT> buildMats() {
    std::array<MatInfo, M_COUNT> t{};
    auto def = [&](Mat id, const char* name, uint32_t col, Kind k, float dens) -> MatInfo& {
        MatInfo& m = t[id];
        m.name = name; m.color = col; m.kind = k; m.density = dens;
        return m;
    };
    auto th = [](MatInfo& m, float cond, float cap) -> MatInfo& { m.cond = cond; m.cap = cap; return m; };
    auto fr = [](MatInfo& m, float f, float r) -> MatInfo& { m.friction = f; m.restitution = r; return m; };
    auto hi = [](MatInfo& m, float T, Mat to, float latent = 0) -> MatInfo& { m.hiT = T; m.hiTo = to; m.latent = latent; return m; };
    auto lo = [](MatInfo& m, float T, Mat to) -> MatInfo& { m.loT = T; m.loTo = to; return m; };
    auto burn = [](MatInfo& m, float ign, float flash, float flameT, int time, float speed, Mat res = M_EMPTY) -> MatInfo& {
        m.ignT = ign; m.flashT = flash; m.burnT = flameT; m.burnTime = time; m.burnSpeed = speed; m.burnRes = res; return m;
    };

    def(M_EMPTY, "EMPTY", 0x0b0e14, K_EMPTY, 0.f);

    // ---- powders
    { auto& m = def(M_SAND, "SAND", 0xe0c070, K_POWDER, 1.6f); th(m, 0.03f, 1.4f); fr(m, 0.5f, 0.05f); hi(m, 1700, M_MOLTEN); m.freezeAs = M_GLASS; }
    { auto& m = def(M_ASH, "ASH", 0x9a9a9a, K_POWDER, 0.5f); th(m, 0.02f, 0.8f); fr(m, 0.5f, 0.05f); }
    { auto& m = def(M_GUNPOWDER, "GUNPOWDER", 0x4a4a4a, K_POWDER, 1.7f); th(m, 0.02f, 1.2f);
      burn(m, 300, 200, 1500, 6, 0.8f); m.blastR = 3.5f; m.blastP = 45.f; }
    { auto& m = def(M_COAL, "COAL", 0x26262b, K_POWDER, 1.3f); th(m, 0.05f, 1.0f); fr(m, 0.6f, 0.05f);
      burn(m, 450, 400, 1100, 220, 0.05f, M_ASH); }

    // ---- liquids
    { auto& m = def(M_WATER, "WATER", 0x2f6fe0, K_LIQUID, 1.0f); th(m, 0.06f, 4.2f); m.elec = 0.4f;
      hi(m, 100, M_STEAM, 120); lo(m, 0, M_ICE); m.expand = 6.f; }
    { auto& m = def(M_OIL, "OIL", 0x6b4a1e, K_LIQUID, 0.85f); th(m, 0.03f, 2.0f);
      burn(m, 330, 140, 900, 220, 0.15f); hi(m, 300, M_VAPOR, 60); m.expand = 6.f; m.volatility = 0.00005f; }
    { auto& m = def(M_GASOLINE, "GASOLINE", 0xe8d36a, K_LIQUID, 0.74f); th(m, 0.02f, 1.6f);
      burn(m, 280, -40, 1250, 45, 0.6f); hi(m, 120, M_VAPOR, 40); m.expand = 8.f; m.volatility = 0.006f; m.bulk = 0.8f; }
    { auto& m = def(M_DIESEL, "DIESEL", 0xc58a24, K_LIQUID, 0.83f); th(m, 0.03f, 1.9f);
      burn(m, 210, 55, 1050, 200, 0.2f); hi(m, 280, M_VAPOR, 60); m.expand = 6.f; m.volatility = 0.0002f; m.bulk = 0.8f; }
    { auto& m = def(M_KEROSENE, "KEROSENE", 0xbfe3d0, K_LIQUID, 0.80f); th(m, 0.03f, 1.9f);
      burn(m, 220, 40, 1100, 140, 0.3f); hi(m, 200, M_VAPOR, 50); m.expand = 7.f; m.volatility = 0.0008f; m.bulk = 0.8f; }
    { auto& m = def(M_JETFUEL, "JET FUEL", 0xd9d2a4, K_LIQUID, 0.805f); th(m, 0.03f, 1.9f);
      burn(m, 225, 38, 1180, 150, 0.35f); hi(m, 190, M_VAPOR, 50); m.expand = 7.f; m.volatility = 0.0007f; m.bulk = 0.8f; }
    { auto& m = def(M_ETHANOL, "ETHANOL", 0xcfe8ff, K_LIQUID, 0.79f); th(m, 0.05f, 2.4f);
      burn(m, 365, 13, 1000, 80, 0.4f); hi(m, 78, M_VAPOR, 50); m.expand = 7.f; m.volatility = 0.003f; }
    { auto& m = def(M_HYDRAULIC, "HYDRAULIC", 0xe0452c, K_LIQUID, 0.87f); th(m, 0.03f, 1.9f);
      burn(m, 400, 200, 800, 150, 0.05f); hi(m, 380, M_SMOKE, 60); m.bulk = 1.8f; }
    { auto& m = def(M_ACID, "ACID", 0x9dff2e, K_LIQUID, 1.2f); th(m, 0.05f, 3.0f); }
    { auto& m = def(M_LAVA, "LAVA", 0xff4500, K_LIQUID, 2.6f); th(m, 0.1f, 2.5f); lo(m, 800, M_STONE); m.initT = 1250.f; }
    { auto& m = def(M_MOLTEN, "MOLTEN", 0xff8a2a, K_LIQUID, 5.0f); th(m, 0.15f, 3.0f); m.initT = 1500.f; }

    // ---- gases
    { auto& m = def(M_STEAM, "STEAM", 0xcfd8e3, K_GAS, 0.001f); th(m, 0.0006f, 0.25f); lo(m, 100, M_WATER); m.latent = 150; m.buoy = 0.3f; m.initT = 110.f; }
    { auto& m = def(M_FIRE, "FIRE", 0xff7a18, K_GAS, 0.001f); th(m, 0.08f, 0.05f); m.buoy = 0.5f; m.initT = 900.f; }
    { auto& m = def(M_SMOKE, "SMOKE", 0x555a60, K_GAS, 0.001f); th(m, 0.0006f, 0.25f); m.buoy = 0.5f; }
    { auto& m = def(M_EXHAUST, "EXHAUST", 0x7a7f88, K_GAS, 0.001f); th(m, 0.0006f, 0.25f); m.buoy = 0.35f; }
    { auto& m = def(M_VAPOR, "VAPOR", 0xc9b86a, K_GAS, 0.001f); th(m, 0.0006f, 0.25f); m.buoy = -0.08f; }
    { auto& m = def(M_PROPANE, "PROPANE", 0xa8c0ff, K_GAS, 0.001f); th(m, 0.0006f, 0.25f);
      burn(m, 470, -100, 1900, 8, 0.9f); m.buoy = -0.12f; }
    { auto& m = def(M_HYDROGEN, "HYDROGEN", 0xe6ffe6, K_GAS, 0.0005f); th(m, 0.0015f, 0.25f);
      burn(m, 500, -100, 2000, 8, 1.0f); m.buoy = 1.2f; }

    { auto& m = def(M_AIR, "AIR", 0xa8c8e8, K_GAS, 0.001f); th(m, 0.0006f, 0.25f); }

    // ---- metals
    { auto& m = def(M_STEEL, "STEEL", 0x9aa4b2, K_SOLID, 7.8f); th(m, 0.06f, 3.8f); fr(m, 0.12f, 0.15f); hi(m, 1450, M_MOLTEN); m.acidK = 0.4f;  m.elec = 40.f;}
    { auto& m = def(M_IRON, "IRON", 0x6f6862, K_SOLID, 7.0f); th(m, 0.07f, 3.5f); fr(m, 0.2f, 0.1f); hi(m, 1530, M_MOLTEN); m.acidK = 0.6f;  m.elec = 45.f;}
    { auto& m = def(M_COPPER, "COPPER", 0xd2733c, K_SOLID, 8.9f); th(m, 0.24f, 3.4f); fr(m, 0.3f, 0.1f); hi(m, 1085, M_MOLTEN); m.acidK = 0.5f;  m.elec = 400.f;}
    { auto& m = def(M_ALUMINUM, "ALUMINUM", 0xc9d1d9, K_SOLID, 2.7f); th(m, 0.22f, 2.4f); fr(m, 0.2f, 0.15f); hi(m, 660, M_MOLTEN); m.acidK = 0.8f;  m.elec = 250.f;}
    { auto& m = def(M_LEAD, "LEAD", 0x4c5560, K_SOLID, 11.3f); th(m, 0.03f, 1.4f); fr(m, 0.6f, 0.02f); hi(m, 327, M_MOLTEN); m.acidK = 0.5f;  m.elec = 20.f;}
    { auto& m = def(M_GOLD, "GOLD", 0xffd24a, K_SOLID, 19.f); th(m, 0.2f, 2.4f); fr(m, 0.4f, 0.1f); hi(m, 1064, M_MOLTEN); m.acidK = 0.f;  m.elec = 280.f;}
    { auto& m = def(M_TITANIUM, "TITANIUM", 0x8d96a6, K_SOLID, 4.5f); th(m, 0.015f, 2.4f); fr(m, 0.5f, 0.2f); hi(m, 1668, M_MOLTEN); m.acidK = 0.f;  m.elec = 12.f;}
    { auto& m = def(M_TUNGSTEN, "TUNGSTEN", 0x5b5b66, K_SOLID, 19.3f); th(m, 0.1f, 2.5f); fr(m, 0.5f, 0.1f); hi(m, 3400, M_MOLTEN); m.acidK = 0.f;  m.elec = 70.f;}

    // ---- structural and other solids
    { auto& m = def(M_WALL, "WALL", 0x8a9099, K_SOLID, 100.f); th(m, 0.1f, 3.0f); fr(m, 0.6f, 0.1f); m.acidK = 0.f; }
    { auto& m = def(M_STONE, "STONE", 0x6e6e73, K_SOLID, 2.7f); th(m, 0.04f, 2.2f); fr(m, 0.7f, 0.1f); hi(m, 1300, M_LAVA); }
    { auto& m = def(M_CONCRETE, "CONCRETE", 0x8d8d86, K_SOLID, 2.4f); th(m, 0.01f, 2.0f); fr(m, 0.7f, 0.05f); }
    { auto& m = def(M_BRICK, "BRICK", 0xa8503c, K_SOLID, 1.9f); th(m, 0.01f, 1.6f); fr(m, 0.7f, 0.05f); }
    { auto& m = def(M_CERAMIC, "CERAMIC", 0xe8e0d0, K_SOLID, 2.3f); th(m, 0.003f, 1.8f); fr(m, 0.15f, 0.05f); hi(m, 2500, M_LAVA); m.acidK = 0.f; }
    { auto& m = def(M_GLASS, "GLASS", 0x9fd4e0, K_SOLID, 2.5f); th(m, 0.01f, 1.8f); fr(m, 0.3f, 0.05f); hi(m, 1400, M_MOLTEN); m.acidK = 0.f; }
    { auto& m = def(M_WOOD, "WOOD", 0x8b5a2b, K_SOLID, 0.6f); th(m, 0.004f, 1.0f); fr(m, 0.5f, 0.2f);
      burn(m, 300, 260, 800, 90, 0.02f, M_ASH); }
    { auto& m = def(M_RUBBER, "RUBBER", 0x2b2b30, K_SOLID, 1.1f); th(m, 0.002f, 2.0f); fr(m, 1.3f, 0.6f);
      burn(m, 400, 350, 900, 100, 0.02f); m.acidK = 0.f; }
    { auto& m = def(M_PLASTIC, "PLASTIC", 0x4fa3d8, K_SOLID, 0.95f); th(m, 0.002f, 1.9f); fr(m, 0.3f, 0.3f);
      hi(m, 200, M_MOLTEN); burn(m, 420, 380, 950, 60, 0.02f); m.acidK = 0.f; }
    { auto& m = def(M_ICE, "ICE", 0xa5e3ff, K_SOLID, 0.92f); th(m, 0.05f, 1.9f); fr(m, 0.05f, 0.05f); hi(m, 0, M_WATER, 100); m.initT = -10.f; }
    { auto& m = def(M_PLANT, "PLANT", 0x2fa84f, K_SOLID, 0.5f); th(m, 0.01f, 3.0f); burn(m, 250, 200, 700, 30, 0.06f); }
    { auto& m = def(M_TNT, "TNT", 0xc0302a, K_SOLID, 1.6f); th(m, 0.01f, 1.4f);
      burn(m, 250, 230, 1500, 6, 0.3f); m.blastR = 24.f; m.blastP = 260.f; }

    // ---- devices
    { auto& m = def(M_HEATER, "HEATER", 0xff6a3a, K_SOLID, 100.f); th(m, 0.2f, 50.f); m.initT = 900.f; m.acidK = 0.f; }
    { auto& m = def(M_COOLER, "COOLER", 0x4aa8ff, K_SOLID, 100.f); th(m, 0.2f, 50.f); m.initT = -60.f; m.acidK = 0.f; }
    { auto& m = def(M_IGNITER, "IGNITER", 0xffe94a, K_SOLID, 100.f); th(m, 0.2f, 0.3f); m.acidK = 0.f; }
    { auto& m = def(M_SOURCE, "SOURCE", 0xff40ff, K_SOLID, 100.f); th(m, 0.02f, 3.0f); m.acidK = 0.f; }
    { auto& m = def(M_VOID, "VOID", 0x2a0a3a, K_SOLID, 100.f); m.acidK = 0.f; }

    // ---- electrical, frangible and impact-sensitive
    { auto& m = def(M_BATT_POS, "BATTERY+", 0xd23a3a, K_SOLID, 100.f); th(m, 0.05f, 10.f); m.elec = 400.f; m.acidK = 0.f; }
    { auto& m = def(M_BATT_NEG, "BATTERY-", 0x2d2f3a, K_SOLID, 100.f); th(m, 0.05f, 10.f); m.elec = 400.f; m.acidK = 0.f; }
    { auto& m = def(M_SOLDER, "SOLDER", 0xb9bdc6, K_SOLID, 8.5f); th(m, 0.12f, 2.6f); fr(m, 0.3f, 0.05f);
      hi(m, 190, M_MOLTEN); m.elec = 120.f; m.acidK = 0.6f; }
    { auto& m = def(M_PARAFFIN, "PARAFFIN", 0xefe6c8, K_SOLID, 0.9f); th(m, 0.004f, 2.5f); fr(m, 0.2f, 0.05f);
      hi(m, 55, M_WAX, 60); burn(m, 250, 200, 900, 60, 0.1f); m.burstP = 5.f; }
    { auto& m = def(M_WAX, "WAX", 0xe8dca8, K_LIQUID, 0.8f); th(m, 0.004f, 2.5f);
      lo(m, 48, M_PARAFFIN); m.latent = 60; burn(m, 250, 200, 900, 60, 0.1f); m.bulk = 1.f; }
    { auto& m = def(M_PRIMER, "PRIMER", 0xc08a30, K_SOLID, 1.7f); th(m, 0.01f, 1.2f); fr(m, 0.3f, 0.05f); m.acidK = 0.5f; }
    return t;
}

inline const std::array<MatInfo, M_COUNT> MATS = buildMats();
