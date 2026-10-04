// SandBots: a physics sandbox (rigid bodies, joints, motors) fused with a
// thermal falling-sand / fluid simulation. Build engines, boilers and hydraulics.
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "font.hpp"
#include "physics.hpp"
#include "sand.hpp"

namespace {

constexpr int S = 3;  // screen pixels per sand cell
constexpr int SIM_W = World::W * S;
constexpr int SIM_H = World::H * S;
constexpr int COLS = 9, ROWS = 5, BTN_H = 24, BTN_GAP = 3;
constexpr int UI_H = ROWS * (BTN_H + BTN_GAP) + 40;
constexpr int WIN_W = SIM_W, WIN_H = SIM_H + UI_H;
constexpr float PI = 3.14159265f;

enum Tool {
    T_MAT, T_BOX, T_CIRCLE, T_WHEEL, T_ROCKET, T_PIN, T_MOTOR, T_AUTOMOTOR, T_ROD, T_SPRING, T_GRAB, T_DELETE, T_SLIDER
};
enum Tab { TAB_POWDER, TAB_LIQUID, TAB_GAS, TAB_METAL, TAB_STRUCT, TAB_DEVICE, TAB_SCENE, TAB_COUNT };

const char* TOOL_NAMES[] = {"PARTICLES", "BOX", "CIRCLE", "WHEEL", "ROCKET", "PIN JOINT", "MOTOR (ARROWS)",
                            "AUTO MOTOR", "ROD", "SPRING", "GRAB", "DELETE", "SLIDER"};
const char* TOOL_HINTS[] = {
    "LMB: PAINT  RMB: ERASE  WHEEL: BRUSH SIZE",
    "DRAG TO SIZE A BOX OF THE SELECTED SOLID (CLICK = DEFAULT)",
    "DRAG FROM CENTRE TO SET RADIUS",
    "DRAG TO SIZE. AUTO-MOTORS ONTO A BODY UNDER ITS CENTRE (ARROWS DRIVE)",
    "DRAG TO SET THRUST DIRECTION. HOLD UP/W TO FIRE",
    "CLICK WHERE TWO BODIES OVERLAP (OR ONE BODY = PIN TO WORLD)",
    "CLICK ON A JOINT SPOT. LEFT/RIGHT ARROWS (A/D) SPIN IT",
    "LIKE MOTOR BUT SPINS ALL THE TIME",
    "DRAG FROM ONE BODY/POINT TO ANOTHER",
    "DRAG FROM ONE BODY/POINT TO ANOTHER",
    "DRAG BODIES AROUND",
    "CLICK A BODY OR JOINT TO REMOVE IT",
    "PRESS ON A BODY AND DRAG ALONG THE LINE IT MAY SLIDE ON (PISTONS, VALVES). ROTATION IS LOCKED",
};

const uint8_t PALETTE[TAB_COUNT][18] = {
    {M_SAND, M_ASH, M_GUNPOWDER, M_COAL},
    {M_WATER, M_OIL, M_GASOLINE, M_DIESEL, M_KEROSENE, M_JETFUEL, M_ETHANOL, M_HYDRAULIC, M_ACID, M_LAVA},
    {M_STEAM, M_FIRE, M_SMOKE, M_EXHAUST, M_VAPOR, M_PROPANE, M_HYDROGEN},
    {M_STEEL, M_IRON, M_COPPER, M_ALUMINUM, M_LEAD, M_GOLD, M_TITANIUM, M_TUNGSTEN},
    {M_WALL, M_STONE, M_CONCRETE, M_BRICK, M_CERAMIC, M_GLASS, M_WOOD, M_RUBBER, M_PLASTIC, M_ICE, M_PLANT, M_TNT},
    {M_HEATER, M_COOLER, M_IGNITER, M_SOURCE, M_VOID, M_EMPTY},
    {},
};
const char* TAB_NAMES[TAB_COUNT] = {"POWDER", "LIQUID", "GAS", "METAL", "STRUCT", "DEVICE", "SCENES"};
const int SPARK_RATES[] = {0, 120, 60, 40, 30, 20, 12};

SDL_Color rgb(uint32_t c, uint8_t a = 255) {
    return SDL_Color{(uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c, a};
}
uint32_t shade(uint32_t c, float f) {
    auto ch = [&](int s) { return (uint32_t)std::clamp((int)(((c >> s) & 255) * f), 0, 255); };
    return 0xFF000000u | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}
uint32_t mix(uint32_t a, uint32_t b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    auto ch = [&](int s) {
        float va = (float)((a >> s) & 255), vb = (float)((b >> s) & 255);
        return (uint32_t)std::clamp((int)(va + (vb - va) * t), 0, 255);
    };
    return 0xFF000000u | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}
// temperature (deg C) -> false-colour heat map
uint32_t heatColor(float T) {
    struct Stop { float t; uint32_t c; };
    static const Stop stops[] = {{-60, 0x1e3cff}, {0, 0x1ea0ff}, {20, 0x1a1c26}, {150, 0xaa281e},
                                 {400, 0xff7814}, {800, 0xffdc3c}, {1500, 0xffffff}};
    if (T <= stops[0].t) return 0xFF000000u | stops[0].c;
    for (size_t i = 1; i < sizeof(stops) / sizeof(stops[0]); ++i)
        if (T <= stops[i].t) return mix(stops[i - 1].c, stops[i].c, (T - stops[i - 1].t) / (stops[i].t - stops[i - 1].t));
    return 0xFFFFFFFFu;
}
uint32_t glowColor(uint32_t base, float T) {
    if (T < 450.f) return base;
    float g = std::clamp((T - 450.f) / 800.f, 0.f, 1.f);
    return mix(base, g > 0.6f ? 0xffd070 : 0xff5a14, std::min(1.f, g * 1.2f));
}

struct Button {
    SDL_Rect r;
    std::function<std::string()> label;
    std::function<void()> action;
    std::function<bool()> active;
    std::function<bool()> visible;
    uint32_t swatch = 0;
    bool hasSwatch = false;
};

struct Label { Vec2 p; std::string s; };

struct Game {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    SDL_Texture* tex = nullptr;
    World world;
    Physics phys{&world};
    std::vector<uint32_t> pixels = std::vector<uint32_t>(World::W * World::H);
    std::vector<Button> buttons;
    std::vector<Label> labels;

    Tool tool = T_MAT;
    Tab tab = TAB_POWDER;
    uint8_t mat = M_SAND;
    uint8_t bodyMat = M_STEEL;
    uint8_t payload = M_WATER;  // what SOURCE blocks emit
    int brush = 3;
    bool paused = false, stepOnce = false, anchored = false, running = true, heatView = false;
    int sparkIdx = 2;

    bool lmb = false, rmb = false;
    Vec2 mouse, lastMouse, dragStart;
    int dragBody = -1;
    int grabJoint = -1;
    bool inSim = false;
    float fps = 60.f;

    // ---------------------------------------------------------------- setup
    bool init(bool headless) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return false;
        }
        win = SDL_CreateWindow("SandBots", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, 0);
        if (!win) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return false; }
        ren = SDL_CreateRenderer(win, -1, headless ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        if (!ren) { std::fprintf(stderr, "renderer: %s\n", SDL_GetError()); return false; }
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, World::W, World::H);
        buildButtons();
        buildDemo();
        return true;
    }

    void selectMaterial(uint8_t m) {
        tool = T_MAT;
        mat = m;
        Kind k = MATS[m].kind;
        if (k == K_SOLID && m != M_VOID && m != M_SOURCE) bodyMat = m;
        if (k == K_POWDER || k == K_LIQUID || k == K_GAS) payload = m;
    }

    void buildButtons() {
        int bw = (WIN_W - (COLS + 1) * BTN_GAP) / COLS;
        auto slot = [&](int row, int col) {
            return SDL_Rect{BTN_GAP + col * (bw + BTN_GAP), SIM_H + BTN_GAP + row * (BTN_H + BTN_GAP), bw, BTN_H};
        };
        auto always = [] { return true; };
        auto add = [&](int row, int col, std::function<std::string()> label, std::function<void()> act,
                       std::function<bool()> active, std::function<bool()> visible = nullptr) -> Button& {
            Button b;
            b.r = slot(row, col);
            b.label = label; b.action = act; b.active = active;
            b.visible = visible ? visible : always;
            buttons.push_back(b);
            return buttons.back();
        };

        // row 0: category tabs
        for (int t = 0; t < TAB_COUNT; ++t) {
            std::string nm = TAB_NAMES[t];
            add(0, t, [nm] { return nm; }, [this, t] { tab = (Tab)t; }, [this, t] { return tab == t; });
        }
        add(0, 7, [this] { return std::string(heatView ? "HEAT VIEW:ON" : "HEAT VIEW"); }, [this] { heatView = !heatView; },
            [this] { return heatView; });
        add(0, 8, [this] { return std::string("SPARK:") + (SPARK_RATES[sparkIdx] ? std::to_string(SPARK_RATES[sparkIdx]) + "F" : "OFF"); },
            [this] { sparkIdx = (sparkIdx + 1) % 7; world.sparkPeriod = SPARK_RATES[sparkIdx]; }, [] { return false; });

        // rows 1-2: material palette (per tab)
        for (int t = 0; t < TAB_SCENE; ++t) {
            for (int i = 0; i < 18 && (PALETTE[t][i] || (t == TAB_DEVICE && i < 6)); ++i) {
                uint8_t m = PALETTE[t][i];
                if (m == M_EMPTY && t != TAB_DEVICE) break;
                Button& b = add(1 + i / COLS, i % COLS, nullptr, nullptr, nullptr);
                b.hasSwatch = true;
                b.swatch = m == M_EMPTY ? 0xff5050 : MATS[m].color;
                std::string name = m == M_EMPTY ? "ERASER" : MATS[m].name;
                b.label = [name] { return name; };
                b.action = [this, m] { selectMaterial(m); };
                b.active = [this, m] { return tool == T_MAT && mat == m; };
                b.visible = [this, t] { return tab == t; };
            }
        }
        // scenes
        struct SceneDef { const char* name; void (Game::*fn)(); };
        static const SceneDef scenes[] = {
            {"DEMO", &Game::buildDemo}, {"STEAM ENG", &Game::buildSteamEngine}, {"GAS ENGINE", &Game::buildGasEngine},
            {"HYDRAULIC", &Game::buildHydraulics}, {"CONDUCT", &Game::buildConduction}, {"FUELS", &Game::buildFuels},
            {"DIESEL ENG", &Game::buildDieselEngine},
        };
        for (int i = 0; i < 7; ++i) {
            auto fn = scenes[i].fn;
            std::string nm = scenes[i].name;
            add(1 + i / COLS, i % COLS, [nm] { return nm; }, [this, fn] { (this->*fn)(); }, [] { return false; },
                [this] { return tab == TAB_SCENE; });
        }

        // rows 3-4: body tools and actions
        const Tool tools[] = {T_BOX, T_CIRCLE, T_WHEEL, T_ROCKET, T_PIN, T_MOTOR, T_AUTOMOTOR, T_ROD, T_SPRING};
        const char* names[] = {"BOX", "CIRCLE", "WHEEL", "ROCKET", "PIN", "MOTOR", "AUTOMOTOR", "ROD", "SPRING"};
        for (int i = 0; i < 9; ++i) {
            Tool t = tools[i];
            std::string nm = names[i];
            add(3, i, [nm] { return nm; }, [this, t] { tool = t; }, [this, t] { return tool == t; });
        }
        add(4, 0, [] { return std::string("GRAB"); }, [this] { tool = T_GRAB; }, [this] { return tool == T_GRAB; });
        add(4, 1, [] { return std::string("DELETE"); }, [this] { tool = T_DELETE; }, [this] { return tool == T_DELETE; });
        add(4, 2, [this] { return std::string(anchored ? "ANCHOR:ON" : "ANCHOR:OFF"); }, [this] { anchored = !anchored; },
            [this] { return anchored; });
        add(4, 3, [this] { return std::string(paused ? "RESUME" : "PAUSE"); }, [this] { paused = !paused; },
            [this] { return paused; });
        add(4, 4, [] { return std::string("STEP"); }, [this] { stepOnce = true; }, [] { return false; });
        add(4, 5, [] { return std::string("CLR BODIES"); }, [this] { clearBodies(); }, [] { return false; });
        add(4, 6, [] { return std::string("CLR CELLS"); }, [this] { world.clear(); phys.stampBodies(); }, [] { return false; });
        add(4, 7, [this] { return std::string("BODY:") + MATS[bodyMat].name; }, [this] { cycleBodyMat(); }, [] { return false; });
        add(4, 8, [] { return std::string("SLIDER"); }, [this] { tool = T_SLIDER; }, [this] { return tool == T_SLIDER; });
    }

    void cycleBodyMat() {
        static const uint8_t order[] = {M_STEEL, M_ALUMINUM, M_COPPER, M_IRON, M_LEAD, M_GOLD, M_TITANIUM, M_TUNGSTEN,
                                        M_WOOD, M_RUBBER, M_PLASTIC, M_GLASS, M_CONCRETE, M_BRICK, M_CERAMIC, M_STONE, M_ICE};
        size_t i = 0;
        for (; i < sizeof(order); ++i) if (order[i] == bodyMat) break;
        bodyMat = order[(i + 1) % sizeof(order)];
    }

    void clearBodies() {
        phys.clear();
        grabJoint = -1;
        dragBody = -1;
    }

    void resetWorld() {
        clearBodies();
        world.clear();
        labels.clear();
        phys.gravity = Vec2(0, 260.f);
        phys.motorInput = 0;
        heatView = false;
    }

    // ---------------------------------------------------------------- scenes
    int spawnCar(Vec2 c, float scale = 1.f) {
        int chassis = phys.addBox(c, Vec2(24 * scale, 5 * scale), 0, M_ALUMINUM, false);
        for (int i = -1; i <= 1; i += 2) {
            Vec2 wc = c + Vec2(16.f * scale * i, 6.f * scale);
            int w = phys.addCircle(wc, 9.f * scale, M_RUBBER, false, true);
            phys.addPin(wc, chassis, w, true, true);
        }
        return chassis;
    }

    void rect(int x0, int y0, int x1, int y1, uint8_t m, uint8_t pl = 0) { world.fillRect(x0, y0, x1, y1, m, pl); }
    void gasRect(int x0, int y0, int x1, int y1, uint8_t m, float amt, float temp = -1e9f) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                if (world.at(x, y).t != M_EMPTY) continue;
                world.setCell(x, y, m);
                world.at(x, y).amt = amt;
                if (temp > -1e8f) world.at(x, y).temp = temp;
            }
    }
    void warm(int x0, int y0, int x1, int y1, float T) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                if (world.inb(x, y) && world.at(x, y).t != M_EMPTY) world.at(x, y).temp = T;
    }
    void label(float x, float y, const std::string& s) { labels.push_back({Vec2(x, y), s}); }

    void buildDemo() {
        resetWorld();
        rect(10, 160, 13, 239, M_WALL);
        rect(10, 236, 100, 239, M_WALL);
        rect(97, 160, 100, 239, M_WALL);
        rect(14, 190, 96, 235, M_WATER);
        for (int x = 0; x < 70; ++x) rect(120 + x, 235 - x / 4, 120 + x, 239, M_STONE);
        rect(190, 217, 399, 239, M_STONE);
        rect(330, 180, 345, 216, M_WOOD);
        rect(350, 205, 358, 216, M_TNT);
        rect(150, 40, 175, 70, M_SAND);
        rect(20, 120, 50, 140, M_GASOLINE);
        spawnCar(Vec2(235, 205));
        for (int i = 0; i < 3; ++i) phys.addBox(Vec2(300, 210.f - i * 12.f), Vec2(6, 6), 0, M_WOOD, false);
        phys.addCircle(Vec2(60, 90), 8, M_PLASTIC, false, false);
        label(14, 150, "SCENES TAB: STEAM / GAS ENGINES, HYDRAULICS, CONDUCTION, FUELS");
        label(330, 170, "TNT");
        phys.stampBodies();
    }

    // Horizontal cylinder: bore interior x headX..headX+len-1, y 150..161, walls of the given material.
    // The rear end is open (atmosphere behind the piston).
    void cylinder(int headX, int len, uint8_t wall) {
        rect(headX - 3, 146, headX + len - 1, 165, wall);
        rect(headX, 150, headX + len - 1, 161, M_EMPTY);
        rect(headX + len - 12, 146, headX + len - 1, 149, wall);   // keep the ceiling over the piston's travel
        rect(headX + len, 150, headX + len + 1, 161, M_VOID);       // drain: keeps the space behind the piston at vacuum
    }

    // Piston in the bore plus a rod to a crank on a flywheel. The crank is placed so that the piston stops
    // 1 cell short of the head at top dead centre. Returns the piston id.
    int lastWheel = -1;
    Vec2 lastCrank;
    int crankSlider(int headX, float pistonLen, float rod, float crankY, float crankR, float wheelR, uint8_t wheelMat) {
        const float boreY0 = 150, boreH = 12;
        float axisY = boreY0 + boreH * 0.5f;
        float crankX = headX + 1 + pistonLen * 0.5f + rod + crankR;
        float th = PI + 0.7f;
        Vec2 pin = Vec2(crankX, crankY) + Vec2(std::cos(th), std::sin(th)) * crankR;
        float px = pin.x - std::sqrt(rod * rod - (pin.y - axisY) * (pin.y - axisY));
        int piston = phys.addBox(Vec2(px, axisY), Vec2(pistonLen * 0.5f, boreH * 0.5f - 0.5f), 0, M_ALUMINUM, false);
        int wheel = phys.addCircle(Vec2(crankX, crankY), wheelR, wheelMat, false, false);
        phys.addPin(Vec2(crankX, crankY), wheel, -1, false, true);
        phys.addDistance(piston, Vec2(px, axisY), wheel, pin, 0.f);
        phys.addSlider(piston, Vec2(1, 0));
        phys.bodies[wheel].w = 7.f;  // starter motor: a kick to carry the crank past dead centre
        lastWheel = wheel;
        lastCrank = Vec2(crankX, crankY);
        return piston;
    }

    // Valve chest above the cylinder head. A sliding gate in it, driven by an eccentric on the flywheel, opens a
    // port into the front of the cylinder for part of every revolution (the inlet valve timing).
    //   chest interior: x 76..116, y 140..145;  port through the ceiling: x 93..96
    void gateValve(uint8_t wall, float phase = 0.f) {
        const float gateHalf = 10.f, ecc = 8.f, restX = 96.f;
        rect(74, 134, 122, 149, wall);
        rect(76, 136, 120, 145, M_EMPTY);       // tall chest: steam flows over the gate, so the gate is pressure-balanced
        rect(92, 146, 97, 149, M_EMPTY);        // port: x 92..97
        float crankX = lastCrank.x, crankY = lastCrank.y;
        float rod = crankX - restX;
        float ang = phase;   // eccentric phase: 0 = open from top dead centre to ~80 degrees after it; PI = open around bottom dead centre
        Vec2 pin = Vec2(crankX, crankY) + Vec2(std::cos(ang), std::sin(ang)) * ecc;
        float gy = 145.0f;
        float gx = pin.x - std::sqrt(rod * rod - (pin.y - gy) * (pin.y - gy));
        int gate = phys.addBox(Vec2(gx, gy), Vec2(gateHalf, 1.0f), 0, M_STEEL, false);
        phys.addDistance(gate, Vec2(gx, gy), lastWheel, pin, 0.f);
        phys.addSlider(gate, Vec2(1, 0));
    }

    // Exhaust gate valve in a chest below the cylinder, driven by a second eccentric: open for most of the
    // return stroke, closed during admission and the start of the power stroke. Gas leaving through it is drained.
    void exhaustValve(uint8_t wall) {
        const float gateHalf = 9.f, ecc = 8.f, restX = 102.f;
        rect(74, 166, 122, 175, wall);
        rect(76, 168, 120, 170, M_EMPTY);       // chest interior (3 high)
        rect(96, 162, 101, 167, M_EMPTY);       // port through the cylinder floor: x 96..101
        rect(74, 168, 75, 170, M_VOID);         // drains at both ends of the chest
        rect(121, 168, 122, 170, M_VOID);
        float crankX = lastCrank.x, crankY = lastCrank.y;
        float rod = crankX - restX;
        float ang = 2.58f;                      // window centred about 250 degrees after top dead centre
        Vec2 pin = Vec2(crankX, crankY) + Vec2(std::cos(ang), std::sin(ang)) * ecc;
        float gy = 170.0f;
        float gx = pin.x - std::sqrt(rod * rod - (pin.y - gy) * (pin.y - gy));
        int gate = phys.addBox(Vec2(gx, gy), Vec2(gateHalf, 1.0f), 0, M_STEEL, false);
        phys.addDistance(gate, Vec2(gx, gy), lastWheel, pin, 0.f);
        phys.addSlider(gate, Vec2(1, 0));
    }

    void buildSteamEngine() {
        resetWorld();
        // boiler (steel) with water, heated from below through a copper floor
        rect(20, 150, 72, 205, M_STEEL);
        rect(23, 153, 69, 202, M_EMPTY);
        rect(23, 172, 69, 202, M_WATER);
        rect(24, 208, 68, 216, M_HEATER);
        rect(23, 203, 69, 207, M_COPPER);
        // insulated (ceramic) steam line: boiler -> riser -> valve chest
        rect(70, 150, 85, 164, M_CERAMIC);
        cylinder(91, 60, M_CERAMIC);
        rect(118, 162, 129, 165, M_EMPTY);       // exhaust port in the floor, draining into a VOID sink
        rect(118, 166, 129, 167, M_VOID);
        crankSlider(91, 16, 100, 156, 18, 24, M_IRON);
        rect(74, 134, 85, 164, M_CERAMIC);
        gateValve(M_CERAMIC);
        rect(76, 136, 83, 162, M_EMPTY);          // riser into the chest
        rect(70, 153, 83, 162, M_EMPTY);          // steam line (carved last so nothing walls it off)
        warm(18, 136, 160, 206, 105.f);              // a warmed-up engine: cold walls would just condense the steam
        warm(23, 172, 69, 202, 96.f);
        for (auto& b : phys.bodies) if (b.alive) b.temp = 105.f;
        rect(0, 230, 399, 239, M_CONCRETE);
        label(22, 140, "BOILER");
        label(78, 124, "VALVE CHEST + GATE");
        label(112, 170, "EXHAUST PORT + DRAIN");
        label(196, 118, "FLYWHEEL");
        label(24, 222, "HEATER");
        phys.stampBodies();
    }

    // Spark-ignition engine: the gate admits a fuel/air charge just after top dead centre and the spark plug in
    // the head lights it where it enters, so the burn stays in step with the piston.
    void buildGasEngine() {
        resetWorld();
        cylinder(91, 60, M_STEEL);
        crankSlider(91, 16, 100, 156, 18, 24, M_IRON);
        gateValve(M_STEEL);
        exhaustValve(M_STEEL);
        rect(76, 136, 76, 145, M_SOURCE, M_VAPOR);            // fuel/air supply on the left of the chest
        float dens = 0.8f;
        for (int y = 136; y <= 145; ++y) world.at(76, y).amt = dens;   // (gasoline vapour by default)
        rect(90, 152, 90, 156, M_IGNITER);                     // spark plug set into the head wall
        sparkIdx = 5;
        world.sparkPeriod = SPARK_RATES[sparkIdx];
        rect(0, 230, 399, 239, M_CONCRETE);
        label(60, 124, "GASOLINE VAPOUR SUPPLY");
        label(78, 118, "GATE VALVE (DRIVEN BY ECCENTRIC)");
        label(60, 168, "SPARK PLUG");
        label(196, 118, "FLYWHEEL (STARTED SPINNING)");
        label(76, 178, "EXHAUST GATE VALVE + DRAIN");
        phys.stampBodies();
    }

    // Compression-ignition engine: no spark, the charge heats as the piston squeezes it and fires itself.
    void buildDieselEngine() {
        resetWorld();
        cylinder(91, 60, M_STEEL);
        crankSlider(91, 16, 100, 156, 18, 24, M_IRON);
        gateValve(M_STEEL);
        exhaustValve(M_STEEL);
        rect(76, 136, 76, 145, M_SOURCE, M_VAPOR);            // diesel vapour supply on the left of the chest
        float dens = 0.8f;
        for (int y = 136; y <= 145; ++y) { world.at(76, y).amt = dens; world.at(76, y).aux = M_DIESEL; }
        rect(90, 152, 90, 156, M_HEATER);                      // glow plug: diesel needs heat, not a spark, to light
        sparkIdx = 0;
        world.sparkPeriod = 0;
        rect(0, 230, 399, 239, M_CONCRETE);
        label(60, 124, "DIESEL VAPOUR SUPPLY");
        label(78, 118, "GATE VALVE (DRIVEN BY ECCENTRIC)");
        label(60, 168, "GLOW PLUG");
        label(196, 118, "FLYWHEEL (STARTED SPINNING)");
        label(76, 178, "EXHAUST GATE VALVE + DRAIN");
        phys.stampBodies();
    }

    void buildHydraulics() {
        resetWorld();
        // master (narrow) and slave (wide) cylinders joined by a pipe, filled with hydraulic fluid
        rect(96, 90, 111, 225, M_STEEL);
        rect(156, 90, 187, 225, M_STEEL);
        rect(96, 210, 187, 225, M_STEEL);
        rect(100, 90, 107, 218, M_EMPTY);
        rect(160, 90, 183, 218, M_EMPTY);
        rect(100, 211, 183, 218, M_EMPTY);
        rect(100, 190, 107, 218, M_HYDRAULIC);
        rect(160, 190, 183, 218, M_HYDRAULIC);
        rect(108, 211, 159, 218, M_HYDRAULIC);
        phys.addBox(Vec2(104, 184), Vec2(3.6f, 6), 0, M_STEEL, false);          // master piston
        int slave = phys.addBox(Vec2(172, 184), Vec2(11.6f, 6), 0, M_STEEL, false);
        int load = phys.addBox(Vec2(172, 166), Vec2(10, 12), 0, M_LEAD, false);
        (void)slave; (void)load;
        // motor-driven crank on the master piston
        int wheel = phys.addCircle(Vec2(104, 130), 14, M_IRON, false, false);
        phys.addPin(Vec2(104, 130), wheel, -1, true, false);
        phys.joints.back().speed = 1.5f;
        int mp = 0;
        for (auto& b : phys.bodies) if (b.alive && b.half.x > 3.5f && b.half.x < 3.7f) mp = b.id;
        phys.addDistance(wheel, Vec2(118, 130), mp, Vec2(104, 178), 0.f);
        phys.addSlider(mp, Vec2(0, 1));
        phys.addSlider(slave, Vec2(0, 1));
        rect(0, 230, 399, 239, M_CONCRETE);
        label(70, 76, "MASTER 8 WIDE");
        label(150, 76, "SLAVE 24 WIDE: 3X FORCE");
        label(112, 206, "HYDRAULIC FLUID");
        phys.stampBodies();
    }

    void buildConduction() {
        resetWorld();
        heatView = true;
        static const uint8_t mats[] = {M_COPPER, M_ALUMINUM, M_GOLD, M_IRON, M_STEEL, M_LEAD, M_TITANIUM, M_GLASS, M_BRICK, M_WOOD, M_CERAMIC};
        int n = sizeof(mats);
        rect(20, 20, 29, 20 + n * 14 - 5, M_HEATER);
        for (int i = 0; i < n; ++i) {
            int y = 20 + i * 14;
            rect(30, y, 190, y + 8, mats[i]);
            label(196, (float)y + 1, MATS[mats[i]].name);
        }
        rect(0, 230, 399, 239, M_CONCRETE);
        label(10, 8, "THERMAL CONDUCTIVITY: HEATER AT LEFT, TOGGLE HEAT VIEW");
        phys.stampBodies();
    }

    void buildFuels() {
        resetWorld();
        static const uint8_t fuels[] = {M_GASOLINE, M_ETHANOL, M_KEROSENE, M_JETFUEL, M_DIESEL, M_OIL, M_HYDRAULIC};
        rect(10, 170, 395, 175, M_COPPER);       // shared plate
        rect(10, 176, 24, 190, M_HEATER);          // heats the plate from the left
        for (int i = 0; i < 7; ++i) {
            int x0 = 40 + i * 50;
            rect(x0, 130, x0 + 2, 169, M_STEEL);
            rect(x0 + 33, 130, x0 + 35, 169, M_STEEL);
            rect(x0 + 3, 150, x0 + 32, 169, fuels[i]);
            rect(x0 + 2, 146, x0 + 2, 152, M_IGNITER);  // spark plug in the wall at the fuel surface
            label((float)x0, 180, MATS[fuels[i]].name);
        }
        rect(0, 230, 399, 239, M_CONCRETE);
        label(10, 100, "FLASH POINT ORDER: GASOLINE < ETHANOL < KEROSENE/JET < DIESEL < OIL < HYDRAULIC");
        label(10, 112, "PLATE WARMS FROM THE LEFT; SPARK PLUGS FIRE PERIODICALLY (SPARK BUTTON)");
        world.sparkPeriod = 60;
        sparkIdx = 2;
        phys.stampBodies();
    }

    // Sealed cylinder pre-filled with hot gas to test pressure on a piston.
    void buildPressureTest() {
        resetWorld();
        rect(109, 146, 172, 165, M_STEEL);
        rect(112, 150, 170, 161, M_EMPTY);
        gasRect(112, 150, 120, 161, M_STEAM, 4.f, 300.f);
        int p = phys.addBox(Vec2(128, 155.5f), Vec2(6, 5.5f), 0, M_STEEL, false);
        (void)p;
        phys.stampBodies();
    }

    // Developer test scenes (used with --shot / --scene).
    void buildTestScene(int which) {
        resetWorld();
        rect(0, 200, 399, 203, M_WALL);
        if (which == 1) {
            for (int c = 0; c < 6; ++c) {
                int x0 = 10 + c * 65;
                rect(x0 - 2, 120, x0 - 1, 199, M_WALL);
                rect(x0 + 55, 120, x0 + 56, 199, M_WALL);
            }
            rect(20, 185, 40, 199, M_WOOD); rect(20, 170, 40, 184, M_OIL);
            rect(28, 165, 30, 168, M_FIRE);
            rect(75, 170, 125, 199, M_WATER); rect(90, 100, 100, 108, M_LAVA);
            rect(140, 190, 180, 199, M_STONE); rect(145, 175, 175, 189, M_SAND);
            rect(150, 150, 170, 160, M_ACID);
            rect(205, 190, 235, 199, M_PLANT); rect(205, 160, 235, 189, M_WATER);
            rect(270, 198, 320, 199, M_GUNPOWDER); rect(271, 190, 273, 197, M_FIRE);
            rect(335, 185, 370, 199, M_ICE); rect(340, 120, 345, 126, M_LAVA);
        } else {
            int bob = phys.addCircle(Vec2(80, 100), 8, M_LEAD, false, false);
            int anchor = phys.addBox(Vec2(300, 20), Vec2(2, 2), 0, M_STEEL, true);
            (void)anchor;
            phys.addDistance(bob, Vec2(80, 100), -1, Vec2(40, 40), 0.f);
            int box = phys.addBox(Vec2(150, 120), Vec2(10, 8), 0, M_WOOD, false);
            phys.addDistance(box, Vec2(150, 112), -1, Vec2(150, 30), 2.5f);
            int arm = phys.addBox(Vec2(230, 150), Vec2(25, 2), 0, M_ALUMINUM, false);
            phys.addPin(Vec2(230, 150), arm, -1, true, false);
            int rb = phys.addBox(Vec2(320, 190), Vec2(10, 6), 0, M_PLASTIC, false);
            int rk = phys.addRocket(Vec2(320, 178), 0);
            phys.addPin(Vec2(320, 184), rb, rk, false, true);
            phys.thrustOn = true;
            spawnCar(Vec2(370, 150));
        }
        phys.stampBodies();
    }

    void simDrag(Tool t, Vec2 a, Vec2 b) {
        tool = t;
        mouse = a; lastMouse = a; handleSimDown(SDL_BUTTON_LEFT);
        mouse = b; handleSimUp(SDL_BUTTON_LEFT);
    }
    void buildScriptedScene() {
        resetWorld();
        rect(0, 200, 399, 203, M_WALL);
        simDrag(T_BOX, Vec2(40, 150), Vec2(100, 165));
        simDrag(T_WHEEL, Vec2(48, 175), Vec2(48, 183));
        simDrag(T_WHEEL, Vec2(48, 160), Vec2(48, 168));
        simDrag(T_WHEEL, Vec2(92, 158), Vec2(92, 167));
        simDrag(T_CIRCLE, Vec2(150, 100), Vec2(150, 110));
        simDrag(T_BOX, Vec2(200, 100), Vec2(215, 112));
        simDrag(T_BOX, Vec2(240, 100), Vec2(255, 112));
        simDrag(T_ROD, Vec2(207, 106), Vec2(247, 106));
        simDrag(T_BOX, Vec2(300, 100), Vec2(320, 110));
        simDrag(T_SPRING, Vec2(310, 105), Vec2(310, 20));
        simDrag(T_ROCKET, Vec2(350, 150), Vec2(350, 120));
        simDrag(T_PIN, Vec2(350, 150), Vec2(350, 150));
        anchored = true;
        simDrag(T_BOX, Vec2(120, 130), Vec2(180, 135));
        anchored = false;
        simDrag(T_AUTOMOTOR, Vec2(150, 130), Vec2(150, 130));
        tool = T_GRAB; mouse = Vec2(150, 100); lastMouse = mouse; handleSimDown(SDL_BUTTON_LEFT);
        mouse = Vec2(170, 90); continuousInput(); handleSimUp(SDL_BUTTON_LEFT);
        simDrag(T_DELETE, Vec2(207, 106), Vec2(207, 106));
        phys.stampBodies();
    }

    // ---------------------------------------------------------------- input
    Vec2 toWorld(int mx, int my) const { return Vec2((float)mx / S, (float)my / S); }

    void createShape(Vec2 a, Vec2 b) {
        Vec2 d = b - a;
        switch (tool) {
            case T_BOX: {
                Vec2 half = Vec2(std::fabs(d.x), std::fabs(d.y)) * 0.5f;
                Vec2 c = (a + b) * 0.5f;
                if (half.x < 2 || half.y < 2) { half = Vec2(7, 7); c = a; }
                phys.addBox(c, half, 0, bodyMat, anchored);
                break;
            }
            case T_CIRCLE: case T_WHEEL: {
                float r = length(d);
                if (r < 3) r = 8;
                bool wheel = tool == T_WHEEL;
                int id = phys.addCircle(a, r, wheel && bodyMat == M_STEEL ? (uint8_t)M_RUBBER : bodyMat, anchored, wheel);
                if (wheel) {
                    int under = phys.pickBody(a, true, id);
                    if (under >= 0) phys.addPin(a, under, id, true, true);
                }
                break;
            }
            case T_ROCKET: {
                float ang = length(d) > 4 ? std::atan2(d.x, -d.y) : 0.f;
                phys.addRocket(a, ang);
                break;
            }
            default: break;
        }
    }

    void clickJoint(Vec2 p) {
        std::vector<int> ids = phys.bodiesAt(p);
        if (ids.empty()) return;
        int a = ids.back(), b = -1;
        if (ids.size() >= 2) { a = ids[ids.size() - 2]; b = ids.back(); }
        bool motor = tool == T_MOTOR || tool == T_AUTOMOTOR;
        phys.addPin(p, a, b, motor, tool != T_AUTOMOTOR);
    }

    void handleSimDown(int button) {
        if (button == SDL_BUTTON_RIGHT) { rmb = true; return; }
        lmb = true;
        dragStart = mouse;
        switch (tool) {
            case T_PIN: case T_MOTOR: case T_AUTOMOTOR: clickJoint(mouse); break;
            case T_ROD: case T_SPRING: case T_SLIDER: {
                std::vector<int> ids = phys.bodiesAt(mouse);
                dragBody = ids.empty() ? -1 : ids.back();
                break;
            }
            case T_GRAB: {
                int id = phys.pickBody(mouse, false);
                if (id >= 0) { dragBody = id; grabJoint = phys.addMouse(id, mouse); }
                break;
            }
            case T_DELETE: {
                int id = phys.pickBody(mouse, true);
                if (id >= 0) phys.removeBody(id);
                else {
                    int j = phys.nearestJoint(mouse, 6.f);
                    if (j >= 0) phys.removeJoint(j);
                }
                phys.stampBodies();
                break;
            }
            default: break;
        }
    }

    void handleSimUp(int button) {
        if (button == SDL_BUTTON_RIGHT) { rmb = false; return; }
        if (!lmb) return;
        lmb = false;
        switch (tool) {
            case T_BOX: case T_CIRCLE: case T_WHEEL: case T_ROCKET: createShape(dragStart, mouse); break;
            case T_ROD: case T_SPRING: {
                std::vector<int> ids = phys.bodiesAt(mouse);
                int endBody = ids.empty() ? -1 : ids.back();
                if (length(mouse - dragStart) > 3.f && (dragBody >= 0 || endBody >= 0) && dragBody != endBody)
                    phys.addDistance(dragBody, dragStart, endBody, mouse, tool == T_SPRING ? 2.5f : 0.f);
                dragBody = -1;
                break;
            }
            case T_SLIDER:
                if (dragBody >= 0 && !phys.bodies[dragBody].isStatic) {
                    Vec2 axis = mouse - dragStart;
                    phys.addSlider(dragBody, length(axis) > 3.f ? axis : Vec2(1, 0));
                }
                dragBody = -1;
                break;
            case T_GRAB:
                if (grabJoint >= 0) phys.removeJoint(grabJoint);
                grabJoint = -1;
                dragBody = -1;
                break;
            default: break;
        }
    }

    void handleEvents() {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_MOUSEMOTION:
                    mouse = toWorld(e.motion.x, e.motion.y);
                    inSim = e.motion.y < SIM_H;
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    mouse = toWorld(e.button.x, e.button.y);
                    if (e.button.y >= SIM_H) {
                        if (e.button.button == SDL_BUTTON_LEFT)
                            for (auto& b : buttons)
                                if (b.visible() && e.button.x >= b.r.x && e.button.x < b.r.x + b.r.w && e.button.y >= b.r.y &&
                                    e.button.y < b.r.y + b.r.h)
                                    b.action();
                    } else {
                        lastMouse = mouse;
                        handleSimDown(e.button.button);
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    mouse = toWorld(e.button.x, e.button.y);
                    handleSimUp(e.button.button);
                    break;
                case SDL_MOUSEWHEEL:
                    brush = std::clamp(brush + (e.wheel.y > 0 ? 1 : -1), 1, 24);
                    break;
                case SDL_KEYDOWN: handleKey(e.key.keysym.sym); break;
                default: break;
            }
        }
    }

    void handleKey(SDL_Keycode k) {
        switch (k) {
            case SDLK_ESCAPE: running = false; break;
            case SDLK_SPACE: paused = !paused; break;
            case SDLK_n: stepOnce = true; break;
            case SDLK_c: world.clear(); phys.stampBodies(); break;
            case SDLK_x: clearBodies(); break;
            case SDLK_r: buildDemo(); break;
            case SDLK_t: anchored = !anchored; break;
            case SDLK_f: cycleBodyMat(); break;
            case SDLK_h: heatView = !heatView; break;
            case SDLK_v: spawnCar(mouse); break;
            case SDLK_COMMA: sparkIdx = (sparkIdx + 6) % 7; world.sparkPeriod = SPARK_RATES[sparkIdx]; break;
            case SDLK_PERIOD: sparkIdx = (sparkIdx + 1) % 7; world.sparkPeriod = SPARK_RATES[sparkIdx]; break;
            case SDLK_LEFTBRACKET: brush = std::max(1, brush - 1); break;
            case SDLK_RIGHTBRACKET: brush = std::min(24, brush + 1); break;
            case SDLK_TAB: {
                const uint8_t* list = PALETTE[tab == TAB_SCENE ? TAB_POWDER : tab];
                int n = 0;
                while (n < 18 && (list[n] || (tab == TAB_DEVICE && n < 6))) ++n;
                int idx = 0;
                for (int i = 0; i < n; ++i) if (list[i] == mat) idx = i + 1;
                selectMaterial(list[idx % n]);
                break;
            }
            case SDLK_DELETE: case SDLK_BACKSPACE: {
                int id = phys.pickBody(mouse, true);
                if (id >= 0) { phys.removeBody(id); phys.stampBodies(); }
                break;
            }
            case SDLK_g: phys.gravity.y = phys.gravity.y > 0 ? -260.f : 260.f; break;
            default: break;
        }
    }

    void continuousInput() {
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        float m = 0;
        if (ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_D]) m += 1;
        if (ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_A]) m -= 1;
        phys.motorInput = m;
        phys.thrustOn = ks[SDL_SCANCODE_UP] || ks[SDL_SCANCODE_W];
        world.sparkHeld = ks[SDL_SCANCODE_E];

        if (!inSim) return;
        int cx = (int)mouse.x, cy = (int)mouse.y, lx = (int)lastMouse.x, ly = (int)lastMouse.y;
        if (lmb && tool == T_MAT)
            world.paintLine(lx, ly, cx, cy, brush, mat, mat == M_EMPTY ? 1.f : 0.45f, payload);
        if (rmb) world.paintLine(lx, ly, cx, cy, brush, M_EMPTY, 1.f);
        if (lmb && tool == T_GRAB && grabJoint >= 0) phys.setMouseTarget(grabJoint, mouse);
        lastMouse = mouse;
    }

    // ---------------------------------------------------------------- update
    void update() {
        continuousInput();
        if (!paused || stepOnce) {
            phys.step(1.f / 60.f);
            world.step();
            stepOnce = false;
        }
    }

    // ---------------------------------------------------------------- render
    uint32_t cellColor(const Cell& c, uint32_t bg) const {
        const MatInfo& m = MATS[c.t];
        if (heatView) return heatColor(c.temp);
        switch (c.t) {
            case M_EMPTY: return bg;
            case M_FIRE: {
                float t = std::clamp((c.temp - 500.f) / 1100.f, 0.f, 1.f);
                uint32_t col = mix(0xc82814, 0xfff2a0, t);
                return (c.var & 4) ? shade(col, 0.9f) : col;
            }
            case M_SMOKE: case M_STEAM: case M_EXHAUST: case M_VAPOR: case M_PROPANE: case M_HYDROGEN: {
                float a = std::clamp(0.12f + c.amt * 0.35f, 0.1f, 0.95f);
                return mix(bg, shade(m.color, 0.9f + 0.1f * (c.var / 255.f)), a);
            }
            case M_MOLTEN: {
                float t = std::clamp((c.temp - 500.f) / 1200.f, 0.f, 1.f);
                return mix(0xff4a10, 0xfff0b0, t);
            }
            case M_LAVA: return shade(m.color, 0.8f + 0.35f * (c.var / 255.f));
            case M_IGNITER: return world.sparkNow ? 0xFFFFFFC0u : shade(m.color, 0.7f);
            default: {
                float f = 0.86f + 0.14f * (c.var / 255.f);
                uint32_t col = shade(m.color, f);
                if (c.burn > 0) col = mix(col, (c.var & 1) ? 0xffb030 : 0xff6a14, 0.6f);
                else col = glowColor(col, c.temp);
                if (m.kind == K_LIQUID && c.amt > 1.02f) col = mix(col, 0xffffff, std::min(0.4f, (c.amt - 1.f) * 4.f));
                return col;
            }
        }
    }

    void renderParticles() {
        const uint32_t bg = 0xFF000000u | MATS[M_EMPTY].color;
        for (int i = 0; i < World::W * World::H; ++i) pixels[i] = cellColor(world.cells[i], bg);
        SDL_UpdateTexture(tex, nullptr, pixels.data(), World::W * 4);
        SDL_Rect dst{0, 0, SIM_W, SIM_H};
        SDL_RenderCopy(ren, tex, nullptr, &dst);
    }

    static SDL_FPoint sp(Vec2 p) { return SDL_FPoint{p.x * S, p.y * S}; }

    void fillPoly(const std::vector<Vec2>& pts, uint32_t color) {
        std::vector<SDL_Vertex> v;
        SDL_Color c = rgb(color);
        Vec2 cen;
        for (auto& p : pts) cen += p;
        cen = cen / (float)pts.size();
        v.push_back({sp(cen), c, {0, 0}});
        for (auto& p : pts) v.push_back({sp(p), c, {0, 0}});
        std::vector<int> idx;
        int n = (int)pts.size();
        for (int i = 0; i < n; ++i) { idx.push_back(0); idx.push_back(1 + i); idx.push_back(1 + (i + 1) % n); }
        SDL_RenderGeometry(ren, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
    }

    void lineWorld(Vec2 a, Vec2 b, SDL_Color c, int thick = 1) {
        SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
        SDL_FPoint pa = sp(a), pb = sp(b);
        for (int i = 0; i < thick; ++i) {
            float o = (float)i - (thick - 1) * 0.5f;
            SDL_RenderDrawLineF(ren, pa.x + o, pa.y, pb.x + o, pb.y);
            if (thick > 1) SDL_RenderDrawLineF(ren, pa.x, pa.y + o, pb.x, pb.y + o);
        }
    }

    void outlinePoly(const std::vector<Vec2>& pts, SDL_Color c) {
        for (size_t i = 0; i < pts.size(); ++i) lineWorld(pts[i], pts[(i + 1) % pts.size()], c, 2);
    }

    std::vector<Vec2> circlePts(Vec2 c, float r, int n = 28) {
        std::vector<Vec2> pts;
        for (int i = 0; i < n; ++i) {
            float a = 2 * PI * i / n;
            pts.push_back(c + Vec2(std::cos(a), std::sin(a)) * r);
        }
        return pts;
    }

    void renderBodies() {
        for (auto& b : phys.bodies) {
            if (!b.alive) continue;
            uint32_t fill = heatView ? heatColor(b.temp) : glowColor(0xFF000000u | b.color, b.temp);
            SDL_Color edge = rgb(shade(fill, 0.55f) & 0xFFFFFF);
            if (b.shape == SHAPE_BOX) {
                std::vector<Vec2> pts;
                Vec2 c[4] = {{-b.half.x, -b.half.y}, {b.half.x, -b.half.y}, {b.half.x, b.half.y}, {-b.half.x, b.half.y}};
                for (auto& p : c) pts.push_back(b.toWorld(p));
                fillPoly(pts, fill);
                outlinePoly(pts, edge);
                if (b.isRocket) {
                    Vec2 tip = b.toWorld(Vec2(0, -b.half.y - 5));
                    fillPoly({b.toWorld(Vec2(-b.half.x, -b.half.y)), b.toWorld(Vec2(b.half.x, -b.half.y)), tip}, 0xf0e0d0);
                    fillPoly({b.toWorld(Vec2(-b.half.x, b.half.y)), b.toWorld(Vec2(b.half.x, b.half.y)),
                              b.toWorld(Vec2(0, b.half.y + 3))}, 0x404048);
                }
                if (b.isStatic) {
                    lineWorld(b.toWorld(c[0]), b.toWorld(c[2]), SDL_Color{255, 255, 255, 50});
                    lineWorld(b.toWorld(c[1]), b.toWorld(c[3]), SDL_Color{255, 255, 255, 50});
                }
            } else {
                std::vector<Vec2> pts = circlePts(b.pos, b.radius);
                fillPoly(pts, fill);
                outlinePoly(pts, edge);
                int spokes = b.isWheel ? 4 : 1;
                for (int i = 0; i < spokes; ++i) {
                    float a = b.angle + i * (2 * PI / spokes);
                    lineWorld(b.pos, b.pos + Vec2(std::cos(a), std::sin(a)) * b.radius * 0.9f, edge, b.isWheel ? 2 : 1);
                }
                if (b.isWheel) fillPoly(circlePts(b.pos, 2.f, 10), 0xc8ccd4);
            }
        }
    }

    void renderJoints() {
        for (auto& j : phys.joints) {
            if (!j.alive) continue;
            Vec2 a = phys.jointAnchorA(j);
            if (j.type == J_MOUSE) {
                lineWorld(a, j.lb, SDL_Color{255, 255, 255, 160});
                continue;
            }
            if (j.type == J_SLIDER) {
                Vec2 p = phys.bodies[j.a].pos;
                for (int i = -2; i <= 2; ++i)
                    lineWorld(p + j.u * (float)(i * 8) - j.u * 2.f, p + j.u * (float)(i * 8) + j.u * 2.f, SDL_Color{255, 255, 255, 140}, 1);
                continue;
            }
            if (j.type == J_DISTANCE) {
                Vec2 bb = phys.jointAnchorB(j);
                if (j.freq <= 0) {
                    lineWorld(a, bb, SDL_Color{230, 230, 235, 255}, 3);
                } else {
                    Vec2 d = bb - a;
                    float len = length(d);
                    Vec2 dir = len > 1e-3f ? d / len : Vec2(1, 0), nrm(-dir.y, dir.x);
                    Vec2 prev = a;
                    const int segs = 10;
                    for (int i = 1; i <= segs; ++i) {
                        Vec2 p = a + d * ((float)i / segs);
                        if (i < segs) p += nrm * ((i & 1) ? 3.5f : -3.5f);
                        lineWorld(prev, p, SDL_Color{240, 200, 90, 255}, 2);
                        prev = p;
                    }
                }
                fillPoly(circlePts(a, 2.f, 8), 0x303038);
                fillPoly(circlePts(bb, 2.f, 8), 0x303038);
                continue;
            }
            bool motor = j.type == J_MOTOR;
            fillPoly(circlePts(a, motor ? 4.f : 3.f, 14), motor ? 0xff9a2e : 0xf2f2f6);
            outlinePoly(circlePts(a, motor ? 4.f : 3.f, 14), SDL_Color{30, 30, 40, 255});
            if (j.b < 0) lineWorld(a + Vec2(-4, 4), a + Vec2(4, 4), SDL_Color{255, 255, 255, 200}, 1);
        }
    }

    void renderLabels() {
        for (auto& l : labels) font::draw(ren, l.s, (int)(l.p.x * S), (int)(l.p.y * S), 1, SDL_Color{200, 210, 230, 200});
    }

    void renderGhost() {
        if (!inSim) return;
        SDL_Color white{255, 255, 255, 170};
        if (lmb) {
            Vec2 d = mouse - dragStart;
            switch (tool) {
                case T_BOX: {
                    Vec2 a = dragStart, b = mouse;
                    outlinePoly({a, Vec2(b.x, a.y), b, Vec2(a.x, b.y)}, white);
                    break;
                }
                case T_CIRCLE: case T_WHEEL: outlinePoly(circlePts(dragStart, length(d)), white); break;
                case T_ROCKET: case T_ROD: case T_SPRING: case T_SLIDER: lineWorld(dragStart, mouse, white, 2); break;
                default: break;
            }
        }
        if (tool == T_MAT) {
            outlinePoly(circlePts(mouse, (float)brush + 0.5f, 24), SDL_Color{255, 255, 255, 110});
        } else {
            lineWorld(mouse + Vec2(-3, 0), mouse + Vec2(3, 0), white);
            lineWorld(mouse + Vec2(0, -3), mouse + Vec2(0, 3), white);
        }
    }

    std::string hoverText() const {
        int x = (int)mouse.x, y = (int)mouse.y;
        if (!inSim || !world.inb(x, y)) return "";
        char buf[96];
        int bid = world.bodyMask[y * World::W + x];
        if (bid >= 0 && bid < (int)phys.bodies.size() && phys.bodies[bid].alive) {
            const Body& b = phys.bodies[bid];
            std::snprintf(buf, sizeof buf, "BODY %s %dC", MATS[b.mat].name, (int)b.temp);
            return buf;
        }
        const Cell& c = world.at(x, y);
        if (c.t == M_EMPTY) return "";
        const MatInfo& m = MATS[c.t];
        if (m.kind == K_GAS || (m.kind == K_LIQUID && c.amt > 1.01f))
            std::snprintf(buf, sizeof buf, "%s %dC AMT %.2f", m.name, (int)c.temp, c.amt);
        else
            std::snprintf(buf, sizeof buf, "%s %dC%s", m.name, (int)c.temp, c.burn ? " BURNING" : "");
        return buf;
    }

    void renderUI() {
        SDL_SetRenderDrawColor(ren, 22, 26, 34, 255);
        SDL_Rect panel{0, SIM_H, WIN_W, UI_H};
        SDL_RenderFillRect(ren, &panel);
        for (auto& b : buttons) {
            if (!b.visible()) continue;
            bool act = b.active();
            SDL_SetRenderDrawColor(ren, act ? 70 : 40, act ? 90 : 46, act ? 130 : 58, 255);
            SDL_RenderFillRect(ren, &b.r);
            SDL_SetRenderDrawColor(ren, act ? 255 : 90, act ? 255 : 98, act ? 255 : 112, 255);
            SDL_RenderDrawRect(ren, &b.r);
            int tx = b.r.x + 6;
            if (b.hasSwatch) {
                SDL_Rect sw{b.r.x + 4, b.r.y + 4, 10, BTN_H - 8};
                SDL_Color sc = rgb(b.swatch);
                SDL_SetRenderDrawColor(ren, sc.r, sc.g, sc.b, 255);
                SDL_RenderFillRect(ren, &sw);
                tx = b.r.x + 18;
            }
            font::draw(ren, b.label(), tx, b.r.y + (BTN_H - 14) / 2, 2, SDL_Color{235, 238, 245, 255});
        }
        int sy = SIM_H + ROWS * (BTN_H + BTN_GAP) + 4;
        std::string status = std::string("TOOL: ") + (tool == T_MAT ? (mat == M_EMPTY ? "ERASER" : MATS[mat].name) : TOOL_NAMES[tool]);
        status += "  BODY: " + std::string(MATS[bodyMat].name);
        if (mat == M_SOURCE) status += "  EMITS: " + std::string(MATS[payload].name);
        status += "  BRUSH " + std::to_string(brush);
        status += "  FPS " + std::to_string((int)fps);
        if (paused) status += "  [PAUSED]";
        font::draw(ren, status, 8, sy, 2, SDL_Color{255, 220, 120, 255});
        std::string hover = hoverText();
        if (!hover.empty()) font::draw(ren, hover, 8, sy + 17, 2, SDL_Color{150, 230, 255, 255});
        font::draw(ren, TOOL_HINTS[tool], 8 + (hover.empty() ? 0 : font::textWidth(hover, 2) + 20), sy + 20, 1, SDL_Color{170, 180, 200, 255});
        font::draw(ren,
                   "SPACE PAUSE  N STEP  C CLEAR CELLS  X CLEAR BODIES  R DEMO  T ANCHOR  F BODY MATERIAL  H HEAT VIEW  E HOLD = SPARK  , . SPARK RATE  V CAR  G GRAVITY  ARROWS DRIVE  UP ROCKETS",
                   8, sy + 32, 1, SDL_Color{120, 130, 150, 255});
    }

    void render() {
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        renderParticles();
        renderBodies();
        renderJoints();
        renderLabels();
        renderGhost();
        renderUI();
    }

    void shutdown() {
        if (tex) SDL_DestroyTexture(tex);
        if (ren) SDL_DestroyRenderer(ren);
        if (win) SDL_DestroyWindow(win);
        SDL_Quit();
    }

    void screenshot(const char* path) {
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, WIN_W, WIN_H, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
        SDL_SaveBMP(s, path);
        SDL_FreeSurface(s);
    }
};

}  // namespace

int main(int argc, char** argv) {
    // Headless self-test: sandbots --shot out.bmp [frames] [--scene N] [--heat] [--trace]
    const char* shot = nullptr;
    int shotFrames = 300, scene = 0;
    bool heat = false, trace = false, g0 = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) {
            shot = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') shotFrames = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--scene") && i + 1 < argc) {
            scene = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--heat")) heat = true;
        else if (!std::strcmp(argv[i], "--trace")) trace = true;
        else if (!std::strcmp(argv[i], "--g0")) g0 = true;
    }
    if (shot) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);

    Game g;
    if (!g.init(shot != nullptr)) return 1;

    if (shot) {
        switch (scene) {
            case 0: break;
            case 3: g.buildScriptedScene(); break;
            case 4: g.buildSteamEngine(); break;
            case 5: g.buildGasEngine(); break;
            case 6: g.buildHydraulics(); break;
            case 7: g.buildConduction(); break;
            case 8: g.buildFuels(); break;
            case 9: g.buildPressureTest(); break;
            case 10: g.buildDieselEngine(); break;
            default: g.buildTestScene(scene); break;
        }
        if (heat) g.heatView = true;
        if (g0) g.phys.gravity = Vec2(0, 0);
        for (int f = 0; f < shotFrames; ++f) {
            g.phys.motorInput = (scene == 0 && f > 40) ? 1.f : 0.f;
            if (scene == 1 || scene == 2) g.phys.thrustOn = scene == 2;
            g.phys.step(1.f / 60.f);
            g.world.step();
            if (!scene && f == 120) g.world.explode(352, 210, 24.f, 260.f);
            if (trace && f % 120 == 0) {
                double gasTot = 0; int water = 0, gcnt = 0; float tmax = 0;
                for (auto& c : g.world.cells) {
                    if (MATS[c.t].kind == K_GAS && c.t != M_FIRE) { gasTot += c.amt; ++gcnt; tmax = std::max(tmax, c.temp); }
                    if (c.t == M_WATER) ++water;
                }
                std::printf("f=%4d water=%d gas cells=%d amt=%.1f Tmax=%.0f", f, water, gcnt, gasTot, tmax);
                for (auto& b : g.phys.bodies)
                    if (b.alive && !b.isStatic) std::printf(" [%d x=%.1f y=%.1f vx=%.1f F=%.0f w=%.2f]", b.id, b.pos.x, b.pos.y, b.vel.x, b.fluidF.x, b.w);
                std::printf("\n");
            }
        }
        std::printf("burn events: %ld\n", g.world.burnEvents);
        for (auto& b : g.phys.bodies) if (b.alive && b.shape == SHAPE_CIRCLE) std::printf("wheel w=%.2f angle=%.1f\n", b.w, b.angle);
        g.render();
        g.screenshot(shot);
        g.shutdown();
        return 0;
    }

    Uint64 last = SDL_GetPerformanceCounter();
    double acc = 0;
    const double dt = 1.0 / 60.0;
    while (g.running) {
        g.handleEvents();
        Uint64 now = SDL_GetPerformanceCounter();
        double frame = (double)(now - last) / (double)SDL_GetPerformanceFrequency();
        last = now;
        if (frame > 0) g.fps = g.fps * 0.95f + (float)(1.0 / frame) * 0.05f;
        acc = std::min(acc + frame, 0.1);
        while (acc >= dt) { g.update(); acc -= dt; }
        g.render();
        SDL_RenderPresent(g.ren);
        SDL_Delay(1);
    }
    g.shutdown();
    return 0;
}
