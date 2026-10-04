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
constexpr int COLS = 9, ROWS = 7, BTN_H = 24, BTN_GAP = 3;
constexpr int UI_H = ROWS * (BTN_H + BTN_GAP) + 48;
constexpr int WIN_W = SIM_W, WIN_H = SIM_H + UI_H;
constexpr float PI = 3.14159265f;

enum Tool {
    T_MAT, T_BOX, T_CIRCLE, T_WHEEL, T_ROCKET, T_PIN, T_MOTOR, T_AUTOMOTOR, T_ROD, T_SPRING, T_GRAB, T_DELETE, T_SLIDER, T_SELECT, T_PIPE, T_HOSE, T_EMITTER, T_BOND
};
enum Tab { TAB_POWDER, TAB_LIQUID, TAB_GAS, TAB_METAL, TAB_STRUCT, TAB_DEVICE, TAB_SCENE, TAB_COUNT };

const char* TOOL_NAMES[] = {"PARTICLES", "BOX", "CIRCLE", "WHEEL", "ROCKET", "PIN JOINT", "MOTOR (ARROWS)",
                            "AUTO MOTOR", "ROD", "SPRING", "GRAB", "DELETE", "SLIDER", "SELECT", "PIPE", "HOSE", "EMITTER", "BOND"};
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
    "CLICK = SELECT (SHIFT ADDS, CTRL = ONE PART OF A GROUP). DRAG = BOX SELECT. ENTER = EDIT EXACT VALUES",
    "DRAG ALONG THE PIPE. WHEEL = DIAMETER. ENTER = TYPE EXACT ENDS/DIAMETER/WALL",
    "DRAG ALONG THE HOSE. WHEEL = DIAMETER. ENTER = TYPE EXACT ENDS/DIAMETER/SEGMENTS",
    "DRAG A SMALL BOX THAT ENDLESSLY PRODUCES THE SELECTED POWDER/LIQUID/GAS. PIN IT, OR LEAVE IT FREE TO TRAVEL. ENTER = RATE",
    "CLICK WHERE TWO BODIES OVERLAP (OR ONE = BOND TO WORLD): A TEMPORARY WELD THAT LETS GO ABOVE ITS MELT TEMPERATURE OR BREAKING FORCE. ENTER = SET BOTH",
};

const uint8_t PALETTE[TAB_COUNT][18] = {
    {M_SAND, M_ASH, M_GUNPOWDER, M_COAL},
    {M_WATER, M_OIL, M_GASOLINE, M_DIESEL, M_KEROSENE, M_JETFUEL, M_ETHANOL, M_HYDRAULIC, M_ACID, M_LAVA},
    {M_STEAM, M_FIRE, M_SMOKE, M_EXHAUST, M_VAPOR, M_PROPANE, M_HYDROGEN},
    {M_STEEL, M_IRON, M_COPPER, M_ALUMINUM, M_LEAD, M_GOLD, M_TITANIUM, M_TUNGSTEN, M_SOLDER},
    {M_WALL, M_STONE, M_CONCRETE, M_BRICK, M_CERAMIC, M_GLASS, M_WOOD, M_RUBBER, M_PLASTIC, M_ICE, M_PLANT, M_TNT, M_PARAFFIN},
    {M_HEATER, M_COOLER, M_IGNITER, M_SOURCE, M_VOID, M_EMPTY, M_BATT_POS, M_BATT_NEG, M_PRIMER},
    {},
};
const char* TAB_NAMES[TAB_COUNT] = {"POWDER", "LIQUID", "GAS", "METAL", "STRUCT", "DEVICE", "SCENES"};
const int SPARK_RATES[] = {0, 120, 60, 40, 30, 20, 12};
const int SNAPS[] = {0, 1, 2, 5, 10};
// bond presets: melting temperature (deg C) and breaking force (engine units)
const char* BOND_NAMES[] = {"PARAFFIN", "SOLDER", "EPOXY", "SHEAR PIN"};
const float BOND_TEMP[] = {55.f, 190.f, 260.f, 5000.f};
const float BOND_FORCE[] = {100000.f, 1000000.f, 3000000.f, 300000.f};

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

    // ---- precision tools: selection, groups, snapping and the numeric entry form
    std::vector<int> sel;      // selected body ids (a group is always selected whole unless in part mode)
    int primary = -1;          // the body the form edits
    bool partMode = false;     // true = the selection is one component of a group
    int snapIdx = 0;
    float pipeD = 12.f, pipeWall = 2.f;
    int hoseSegs = 0;          // 0 = automatic
    Tool lastTool = T_MAT;
    enum FormKind { FK_NONE, FK_BOX, FK_CIRCLE, FK_PIPE, FK_HOSE, FK_EMITTER, FK_SCALE, FK_BATTERY, FK_BOND, FK_EDIT_BOX, FK_EDIT_CIRCLE, FK_EDIT_GROUP };
    struct Field { std::string name, text; };
    FormKind formKind = FK_NONE;
    std::vector<Field> fields;
    int fActive = 0;
    bool fFresh = true;
    uint8_t fMat = M_STEEL;
    bool fStatic = false;
    std::string formMsg;
    bool wheelForm = false;
    int bondType = 0;
    float bondT = 55.f, bondF = 100000.f;
    bool elecView = false;
    uint8_t fPayload = M_WATER;
    uint8_t fFace = 0;
    float lastRate = 30.f, lastScale = 98.f;
    std::string note;
    int noteFrames = 0;
    void notify(const std::string& s) { note = s; noteFrames = 240; }

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

    bool emitterFormActive() const {
        if (formKind == FK_EMITTER) return true;
        if (formKind == FK_EDIT_BOX) return fields.size() > 5 && std::atof(fields[5].text.c_str()) > 0;
        if (formKind == FK_EDIT_CIRCLE) return fields.size() > 4 && std::atof(fields[4].text.c_str()) > 0;
        return false;
    }
    void selectMaterial(uint8_t m) {
        Kind kk = MATS[m].kind;
        if ((kk == K_POWDER || kk == K_LIQUID || kk == K_GAS) && emitterFormActive()) {  // pick what the emitter makes
            fPayload = m;
            formMsg = std::string("EMITS ") + MATS[m].name;
            return;
        }
        tool = T_MAT;
        mat = m;
        Kind k = MATS[m].kind;
        if (k == K_SOLID && m != M_VOID && m != M_SOURCE && m != M_BATT_POS && m != M_BATT_NEG) bodyMat = m;
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
            {"DIESEL ENG", &Game::buildDieselEngine}, {"ELECTRIC", &Game::buildElectricTest},
            {"BONDS", &Game::buildBondTest}, {"PRIMER", &Game::buildPrimerTest},
        };
        for (int i = 0; i < 10; ++i) {
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
        // row 5: selection, groups, pipes and exact numeric entry
        add(5, 0, [] { return std::string("SELECT"); }, [this] { tool = T_SELECT; }, [this] { return tool == T_SELECT; });
        add(5, 1, [] { return std::string("GROUP"); }, [this] { groupSelection(); }, [] { return false; });
        add(5, 2, [] { return std::string("UNGROUP"); }, [this] { ungroupSelection(); }, [] { return false; });
        add(5, 3, [] { return std::string("PIPE"); }, [this] { tool = T_PIPE; }, [this] { return tool == T_PIPE; });
        add(5, 4, [] { return std::string("HOSE"); }, [this] { tool = T_HOSE; }, [this] { return tool == T_HOSE; });
        add(5, 5, [] { return std::string("EXACT"); }, [this] { if (formKind == FK_NONE) openForm(); else closeForm(); },
            [this] { return formKind != FK_NONE; });
        add(5, 6, [this] { return std::string("SNAP:") + (SNAPS[snapIdx] ? std::to_string(SNAPS[snapIdx]) : "OFF"); },
            [this] { snapIdx = (snapIdx + 1) % 5; }, [this] { return snapIdx > 0; });
        // row 6: boolean cut, scale and self-replenishing sources
        add(6, 0, [] { return std::string("CUT"); }, [this] { cutSelection(); }, [] { return false; });
        add(6, 1, [] { return std::string("SCALE"); }, [this] { openScaleForm(); }, [this] { return formKind == FK_SCALE; });
        add(6, 2, [] { return std::string("EMITTER"); }, [this] { tool = T_EMITTER; }, [this] { return tool == T_EMITTER; });
        add(6, 3, [this] { return std::string("BATT ") + fmt(world.battV) + "V"; }, [this] { openBatteryForm(); }, [this] { return formKind == FK_BATTERY; });
        add(6, 4, [this] { return std::string(elecView ? "ELEC VIEW:ON" : "ELEC VIEW"); }, [this] { elecView = !elecView; }, [this] { return elecView; });
        add(6, 5, [] { return std::string("BOND"); }, [this] { tool = T_BOND; }, [this] { return tool == T_BOND; });
        add(6, 6, [this] { return std::string(BOND_NAMES[bondType]); }, [this] { cycleBond(); }, [] { return false; });
    }

    void cycleBodyMat() {
        static const uint8_t order[] = {M_STEEL, M_ALUMINUM, M_COPPER, M_IRON, M_LEAD, M_GOLD, M_TITANIUM, M_TUNGSTEN,
                                        M_WOOD, M_RUBBER, M_PLASTIC, M_GLASS, M_CONCRETE, M_BRICK, M_CERAMIC, M_STONE, M_ICE, M_SOLDER, M_PARAFFIN, M_PRIMER};
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


    // ---------------------------------------------------------------- selection / groups / numeric form
    static constexpr int SNAPS_N = 5;
    Vec2 snap(Vec2 p) const {
        int g = SNAPS[snapIdx];
        if (g <= 0) return p;
        return Vec2(std::round(p.x / g) * g, std::round(p.y / g) * g);
    }
    bool shapeTool(Tool t) const { return t == T_BOX || t == T_CIRCLE || t == T_WHEEL || t == T_ROCKET || t == T_PIPE || t == T_HOSE || t == T_EMITTER; }
    Vec2 smouse() const { return shapeTool(tool) ? snap(mouse) : mouse; }

    static std::string fmt(float v) {
        char b[32];
        std::snprintf(b, sizeof b, "%g", std::round(v * 100.f) / 100.f);
        return b;
    }
    float fv(int i) const { return i < (int)fields.size() ? (float)std::atof(fields[i].text.c_str()) : 0.f; }

    void pruneSelection() {
        sel.erase(std::remove_if(sel.begin(), sel.end(), [&](int id) { return id < 0 || id >= (int)phys.bodies.size() || !phys.bodies[id].alive; }), sel.end());
        if (std::find(sel.begin(), sel.end(), primary) == sel.end()) primary = sel.empty() ? -1 : sel[0];
    }
    void clearSelection() { sel.clear(); primary = -1; partMode = false; }
    void selectBody(int id, bool add, bool part) {
        if (id < 0) { if (!add) clearSelection(); return; }
        std::vector<int> add_;
        const Body& b = phys.bodies[id];
        if (b.group >= 0 && !part) add_ = phys.groupMembers(b.group); else add_.push_back(id);
        bool already = std::find(sel.begin(), sel.end(), id) != sel.end();
        if (!add) sel.clear();
        if (add && already) {
            for (int m : add_) sel.erase(std::remove(sel.begin(), sel.end(), m), sel.end());
        } else {
            for (int m : add_) if (std::find(sel.begin(), sel.end(), m) == sel.end()) sel.push_back(m);
            primary = id;
        }
        partMode = part && b.group >= 0;
        pruneSelection();
        if (formKind >= FK_EDIT_BOX) openForm();
    }
    void boxSelect(Vec2 a, Vec2 b, bool add, bool part) {
        float x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x), y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
        if (!add) sel.clear();
        for (auto& bd : phys.bodies) {
            if (!bd.alive || bd.pos.x < x0 || bd.pos.x > x1 || bd.pos.y < y0 || bd.pos.y > y1) continue;
            std::vector<int> m = bd.group >= 0 && !part ? phys.groupMembers(bd.group) : std::vector<int>{bd.id};
            for (int id : m) if (std::find(sel.begin(), sel.end(), id) == sel.end()) sel.push_back(id);
        }
        partMode = false;
        primary = -1;
        pruneSelection();
        if (formKind >= FK_EDIT_BOX) openForm();
    }
    void groupSelection() {
        pruneSelection();
        if (sel.size() < 2) { formMsg = "SELECT 2+ BODIES FIRST"; return; }
        int g = phys.groupBodies(sel);
        sel = phys.groupMembers(g);
        partMode = false;
        formMsg = "GROUPED " + std::to_string(sel.size()) + " BODIES";
        if (formKind >= FK_EDIT_BOX) openForm();
    }
    void ungroupSelection() {
        pruneSelection();
        int n = 0;
        std::vector<int> gs;
        for (int id : sel) { int g = phys.bodies[id].group; if (g >= 0 && std::find(gs.begin(), gs.end(), g) == gs.end()) gs.push_back(g); }
        for (int g : gs) { phys.ungroup(g); ++n; }
        partMode = false;
        formMsg = n ? "UNGROUPED" : "NOTHING GROUPED";
        if (formKind >= FK_EDIT_BOX) openForm();
    }

    // ---- the numeric form
    void openForm() {
        pruneSelection();
        fields.clear();
        fActive = 0; fFresh = true;
        Vec2 m = snap(mouse);
        if (primary >= 0) {
            const Body& b = phys.bodies[primary];
            float deg = std::fmod(b.angle * 180.f / PI, 360.f);
            if (b.group >= 0 && !partMode) {
                formKind = FK_EDIT_GROUP;
                fields = {{"X", fmt(b.pos.x)}, {"Y", fmt(b.pos.y)}, {"ANGLE", fmt(deg)}};
            } else if (b.shape == SHAPE_BOX) {
                formKind = FK_EDIT_BOX;
                fields = {{"X", fmt(b.pos.x)}, {"Y", fmt(b.pos.y)}, {"WIDTH", fmt(b.half.x * 2)}, {"HEIGHT", fmt(b.half.y * 2)}, {"ANGLE", fmt(deg)}};
            } else {
                formKind = FK_EDIT_CIRCLE;
                fields = {{"X", fmt(b.pos.x)}, {"Y", fmt(b.pos.y)}, {"RADIUS", fmt(b.radius)}, {"ANGLE", fmt(deg)}};
            }
            fMat = b.mat; fStatic = b.isStatic;
            fields.push_back({"RATE", fmt(b.src.on ? b.src.rate : 0.f)});
            fPayload = b.src.on ? b.src.mat : payload; fFace = b.src.face;
            return;
        }
        switch (tool) {
            case T_CIRCLE: case T_WHEEL:
                formKind = FK_CIRCLE; wheelForm = tool == T_WHEEL;
                fields = {{"X", fmt(m.x)}, {"Y", fmt(m.y)}, {"RADIUS", fmt(lastR)}};
                break;
            case T_BOND: openBondForm(); return;
            case T_EMITTER:
                formKind = FK_EMITTER; fPayload = payload;
                fields = {{"X", fmt(m.x)}, {"Y", fmt(m.y)}, {"WIDTH", "6"}, {"HEIGHT", "6"}, {"RATE", fmt(lastRate)}};
                break;
            case T_PIPE: case T_HOSE:
                formKind = tool == T_PIPE ? FK_PIPE : FK_HOSE;
                fields = {{"X1", fmt(m.x)}, {"Y1", fmt(m.y)}, {"X2", fmt(m.x + lastLen)}, {"Y2", fmt(m.y)},
                          {"DIAMETER", fmt(pipeD)}, {"WALL", fmt(pipeWall)}};
                if (tool == T_HOSE) fields.push_back({"SEGMENTS", std::to_string(hoseSegs)});
                break;
            default:
                if (tool != T_BOX) tool = T_BOX;
                formKind = FK_BOX;
                fields = {{"X", fmt(m.x)}, {"Y", fmt(m.y)}, {"WIDTH", fmt(lastW)}, {"HEIGHT", fmt(lastH)}, {"ANGLE", "0"}};
                break;
        }
        fMat = bodyMat; fStatic = anchored;
    }
    float lastW = 40.f, lastH = 10.f, lastR = 8.f, lastLen = 60.f;
    void closeForm() { formKind = FK_NONE; fields.clear(); formMsg.clear(); }

    void applyForm() {
        pruneSelection();
        switch (formKind) {
            case FK_BOX: {
                lastW = std::max(1.f, fv(2)); lastH = std::max(1.f, fv(3));
                int id = phys.addBox(Vec2(fv(0), fv(1)), Vec2(lastW, lastH) * 0.5f, fv(4) * PI / 180.f, bodyMat, anchored);
                sel = {id}; primary = id; partMode = false;
                formMsg = "CREATED BOX " + fmt(lastW) + " X " + fmt(lastH);
                break;
            }
            case FK_CIRCLE: {
                lastR = std::max(0.5f, fv(2));
                createCircle(Vec2(fv(0), fv(1)), lastR, wheelForm);
                formMsg = "CREATED CIRCLE R " + fmt(lastR);
                break;
            }
            case FK_PIPE: case FK_HOSE: {
                pipeD = std::max(2.f, fv(4)); pipeWall = std::clamp(fv(5), 0.5f, pipeD * 0.5f);
                Vec2 a(fv(0), fv(1)), b(fv(2), fv(3));
                lastLen = std::max(4.f, length(b - a));
                int g;
                if (formKind == FK_PIPE) g = phys.addPipe(a, b, pipeD, pipeWall, bodyMat, anchored);
                else { hoseSegs = (int)fv(6); g = phys.addHose(a, b, pipeD, pipeWall, hoseSegs > 0 ? hoseSegs : autoSegs(a, b), bodyMat, anchored); }
                if (g >= 0) { sel = phys.groupMembers(g); primary = sel.empty() ? -1 : sel[0]; partMode = false; }
                formMsg = g >= 0 ? (formKind == FK_PIPE ? "CREATED PIPE" : "CREATED HOSE") : "TOO SHORT";
                break;
            }
            case FK_EMITTER: {
                lastRate = std::clamp(fv(4), 0.f, 1000.f);
                int id = phys.addBox(Vec2(fv(0), fv(1)), Vec2(std::max(1.f, fv(2)), std::max(1.f, fv(3))) * 0.5f, 0, bodyMat, anchored);
                phys.bodies[id].src = Emitter{lastRate > 0, fPayload, lastRate, 0.f, fFace};
                sel = {id}; primary = id; partMode = false;
                formMsg = std::string("CREATED EMITTER OF ") + MATS[fPayload].name;
                break;
            }
            case FK_BATTERY:
                world.battV = std::clamp(fv(0), 1.f, 70000.f);
                world.battA = std::clamp(fv(1), 0.001f, 400.f);
                formMsg = "BATTERY CELLS YOU PAINT NOW: " + fmt(world.battV) + "V " + fmt(world.battA) + "A (REPAINT TO UPDATE)";
                selectMaterial(M_BATT_POS);
                break;
            case FK_BOND:
                bondT = std::clamp(fv(0), -50.f, 5000.f); bondF = std::clamp(fv(1), 1.f, 1e5f) * 1000.f;
                formMsg = "NEXT BOND: MELTS " + fmt(bondT) + "C / BREAKS " + fmt(bondF / 1000.f) + " KN";
                break;
            case FK_SCALE: {
                if (sel.empty()) { formMsg = "NOTHING SELECTED"; break; }
                float pct = std::clamp(fv(0), 5.f, 1000.f);
                double wsum = 0, cx = 0, cy = 0;
                for (int id : sel) { const Body& b = phys.bodies[id]; wsum += b.area; cx += b.pos.x * b.area; cy += b.pos.y * b.area; }
                phys.scaleBodies(sel, pct / 100.f, wsum > 0 ? Vec2((float)(cx / wsum), (float)(cy / wsum)) : phys.bodies[primary].pos);
                lastScale = pct;
                formMsg = "SCALED TO " + fmt(pct) + "% (" + std::to_string(sel.size()) + " BODIES)";
                break;
            }
            case FK_EDIT_GROUP:
                phys.transformGroup(primary, Vec2(fv(0), fv(1)), fv(2) * PI / 180.f);
                formMsg = "MOVED GROUP";
                break;
            case FK_EDIT_BOX:
                phys.reshape(primary, Vec2(fv(0), fv(1)), Vec2(fv(2), fv(3)) * 0.5f, 0, fv(4) * PI / 180.f, fMat, fStatic);
                setEmitter(fv(5));
                formMsg = "UPDATED";
                break;
            case FK_EDIT_CIRCLE:
                phys.reshape(primary, Vec2(fv(0), fv(1)), Vec2(), fv(2), fv(3) * PI / 180.f, fMat, fStatic);
                setEmitter(fv(4));
                formMsg = "UPDATED";
                break;
            default: break;
        }
        phys.stampBodies();
        fFresh = true;
    }
    void setEmitter(float rate) {
        Body& b = phys.bodies[primary];
        rate = std::clamp(rate, 0.f, 1000.f);
        b.src.on = rate > 0; b.src.rate = rate; b.src.mat = fPayload; b.src.face = fFace;
        if (rate > 0) lastRate = rate;
    }
    static const char* faceName(int f) { static const char* n[] = {"ALL SIDES", "+X SIDE", "-X SIDE", "+Y SIDE", "-Y SIDE"}; return n[f % 5]; }

    void cycleBond() {
        bondType = (bondType + 1) % 4;
        bondT = BOND_TEMP[bondType]; bondF = BOND_FORCE[bondType];
        notify(std::string("BOND: ") + BOND_NAMES[bondType] + " LETS GO ABOVE " + fmt(bondT) + "C OR " + fmt(bondF / 1000.f) + " KN");
    }
    void openBatteryForm() {
        formKind = FK_BATTERY; fActive = 0; fFresh = true;
        fields = {{"VOLTS", fmt(world.battV)}, {"AMPS", fmt(world.battA)}};
        formMsg = "";
    }
    void openBondForm() {
        formKind = FK_BOND; fActive = 0; fFresh = true;
        fields = {{"MELT C", fmt(bondT)}, {"BREAK KN", fmt(bondF / 1000.f)}};
        formMsg = "";
    }
    void openScaleForm() {
        pruneSelection();
        if (sel.empty()) { notify("SELECT BODIES TO SCALE FIRST"); return; }
        formKind = FK_SCALE; fActive = 0; fFresh = true;
        fields = {{"PERCENT", fmt(lastScale)}};
        formMsg = "";
    }

    // Boolean subtract. The last-clicked (white-outlined) body is the cutter; every other selected body is cut by it.
    void cutSelection() {
        pruneSelection();
        if (sel.size() < 2 || primary < 0) { notify("SELECT THE TARGET(S), THEN CLICK THE CUTTER LAST"); return; }
        std::vector<int> cutters;
        const Body& pb = phys.bodies[primary];
        if (pb.group >= 0 && !partMode) cutters = phys.groupMembers(pb.group); else cutters = {primary};
        int cutN = 0, goneN = 0, pieces = 0, skipped = 0;
        for (int t : std::vector<int>(sel)) {
            if (std::find(cutters.begin(), cutters.end(), t) != cutters.end()) continue;
            int r = phys.cutBody(t, cutters);
            if (r < 0) { ++skipped; continue; }
            ++cutN; pieces += r;
            if (r == 0) ++goneN;
        }
        sel = cutters;
        phys.stampBodies();
        if (cutN) notify("CUT " + std::to_string(cutN) + " BODIES INTO " + std::to_string(pieces) + " PIECES. CUTTER KEPT: DELETE OR SCALE IT");
        else notify("NOTHING CUT (NO OVERLAP, OR WHEEL/ROCKET/EMITTER)");
        (void)goneN; (void)skipped;
        if (formKind >= FK_EDIT_BOX) openForm();
    }
    int autoSegs(Vec2 a, Vec2 b) const { return std::clamp((int)std::ceil(length(b - a) / std::max(4.f, pipeD * 1.2f)), 2, 60); }

    // returns true if the key was consumed by the form
    bool formKey(SDL_Keycode k, Uint16 mod) {
        if (formKind == FK_NONE) return false;
        int n = (int)fields.size();
        auto typeChar = [&](char c) {
            std::string& t = fields[fActive].text;
            if (fFresh) { t.clear(); fFresh = false; }
            if (t.size() < 9) t += c;
        };
        if (k >= SDLK_0 && k <= SDLK_9) typeChar((char)('0' + (k - SDLK_0)));
        else if (k >= SDLK_KP_1 && k <= SDLK_KP_9) typeChar((char)('1' + (k - SDLK_KP_1)));
        else if (k == SDLK_KP_0) typeChar('0');
        else if (k == SDLK_PERIOD || k == SDLK_KP_PERIOD) typeChar('.');
        else if (k == SDLK_MINUS || k == SDLK_KP_MINUS) typeChar('-');
        else if (k == SDLK_BACKSPACE) { std::string& t = fields[fActive].text; if (!t.empty()) t.pop_back(); fFresh = false; }
        else if (k == SDLK_TAB || k == SDLK_DOWN) { fActive = (fActive + ((mod & KMOD_SHIFT) ? n - 1 : 1)) % n; fFresh = true; }
        else if (k == SDLK_UP) { fActive = (fActive + n - 1) % n; fFresh = true; }
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) applyForm();
        else if (k == SDLK_ESCAPE) closeForm();
        else if (k == SDLK_m && formKind >= FK_EDIT_BOX && formKind != FK_EDIT_GROUP) { bodyMat = fMat; cycleBodyMat(); fMat = bodyMat; }
        else if (k == SDLK_s && formKind >= FK_EDIT_BOX) fStatic = !fStatic;
        else if (k == SDLK_f && (formKind == FK_EMITTER || (formKind >= FK_EDIT_BOX && formKind != FK_EDIT_GROUP))) fFace = (fFace + 1) % 5;
        else if (k == SDLK_g) groupSelection();
        else if (k == SDLK_u) ungroupSelection();
        return true;
    }
    SDL_Rect formRect() const { return SDL_Rect{8, 8, 300, 56 + (int)fields.size() * 20 + (formKind >= FK_EDIT_BOX || formKind == FK_EMITTER ? 52 : 20)}; }
    SDL_Rect fieldRect(int i) const { return SDL_Rect{16, 36 + i * 20, 284, 18}; }
    bool formClick(int mx, int my) {
        if (formKind == FK_NONE) return false;
        SDL_Rect r = formRect();
        if (mx < r.x || my < r.y || mx >= r.x + r.w || my >= r.y + r.h) return false;
        for (int i = 0; i < (int)fields.size(); ++i) {
            SDL_Rect f = fieldRect(i);
            if (mx >= f.x && mx < f.x + f.w && my >= f.y && my < f.y + f.h) { fActive = i; fFresh = true; }
        }
        return true;
    }

    // exercises the numeric form and group tools the way a user would (headless test)
    void typeInto(const std::vector<std::string>& vals) {
        for (size_t i = 0; i < vals.size(); ++i) {
            fActive = (int)i; fFresh = true;
            for (char c : vals[i]) formKey(c == '.' ? SDLK_PERIOD : (c == '-' ? SDLK_MINUS : (SDL_Keycode)c), 0);
        }
    }
    void buildPrecisionTest() {
        resetWorld();
        rect(0, 200, 399, 203, M_WALL);
        anchored = true;
        tool = T_BOX; clearSelection(); openForm(); typeInto({"40", "120", "60", "6", "0"}); applyForm();   // exact shelf 60 x 6
        tool = T_PIPE; closeForm(); clearSelection(); openForm(); typeInto({"40", "100", "100", "100", "12", "2"}); applyForm();
        std::vector<int> pipeSel = sel;
        anchored = false;
        tool = T_BOX; closeForm(); clearSelection(); openForm(); typeInto({"200", "60", "30", "8", "30"}); applyForm();
        int a = primary;
        tool = T_CIRCLE; closeForm(); clearSelection(); openForm(); typeInto({"200", "60", "10"}); applyForm();
        int b = primary;
        sel = {a, b}; groupSelection();
        tool = T_HOSE; closeForm(); clearSelection(); openForm(); typeInto({"100", "100", "100", "160", "10", "1.5", "0"}); applyForm();
        std::vector<int> hoseSel = sel;
        closeForm();
        // hang the hose from the end of the pipe
        if (!pipeSel.empty() && !hoseSel.empty()) phys.addPin(Vec2(100, 100), pipeSel[0], hoseSel[0], false, false);
        sel = {a}; primary = a; partMode = true; openForm();
        typeInto({"205", "62", "30", "8", "10"}); applyForm();
        closeForm();
        clearSelection();
        sel = phys.groupMembers(phys.bodies[a].group); primary = a;
        phys.stampBodies();
    }

    void buildCutTest() {
        resetWorld();
        rect(0, 200, 399, 203, M_WALL);
        anchored = true;
        tool = T_BOX; clearSelection(); openForm(); typeInto({"100", "150", "70", "24", "0"}); applyForm();
        int plate = primary;
        anchored = false;
        tool = T_CIRCLE; clearSelection(); closeForm(); openForm(); typeInto({"100", "150", "9"}); applyForm();
        int ball = primary;
        // a second, rotated target for the cut: a block with a rectangular notch
        tool = T_BOX; clearSelection(); closeForm(); openForm(); typeInto({"250", "150", "50", "30", "20"}); applyForm();
        int block = primary;
        tool = T_BOX; clearSelection(); closeForm(); openForm(); typeInto({"250", "150", "12", "50", "20"}); applyForm();
        int slot = primary;
        closeForm();
        sel = {plate, ball}; primary = ball; cutSelection();
        sel = {block, slot}; primary = slot; cutSelection();
        // scale the cutters to 98% to make tight-fitting plugs
        sel = {ball}; primary = ball; openScaleForm(); typeInto({"98"}); applyForm();
        sel = {slot}; primary = slot; typeInto({"98"}); applyForm();
        closeForm();
        clearSelection();
        phys.stampBodies();
    }
    void buildElectricTest() {
        resetWorld();
        rect(0, 200, 399, 203, M_WALL);
        // A: 12 V / 20 A battery heats a tungsten filament, which lights gasoline vapour
        world.battV = 12.f; world.battA = 20.f;
        rect(20, 60, 22, 62, M_BATT_POS); rect(20, 80, 22, 82, M_BATT_NEG);
        rect(23, 61, 59, 61, M_COPPER); rect(60, 61, 79, 61, M_TUNGSTEN); rect(80, 61, 100, 61, M_COPPER);
        rect(100, 61, 100, 81, M_COPPER); rect(23, 81, 100, 81, M_COPPER);
        gasRect(60, 56, 80, 60, M_VAPOR, 0.6f);
        label(24, 48, "12V 20A BATTERY, TUNGSTEN FILAMENT IN FUEL VAPOUR");
        // B: 20 kV / 50 mA through a 2-cell air gap (a spark plug) in vapour
        world.battV = 20000.f; world.battA = 0.05f;
        rect(150, 60, 152, 62, M_BATT_POS); rect(150, 80, 152, 82, M_BATT_NEG);
        rect(153, 61, 190, 61, M_COPPER); rect(190, 61, 190, 70, M_COPPER); rect(190, 70, 192, 70, M_COPPER);
        rect(195, 70, 197, 70, M_COPPER); rect(197, 70, 197, 81, M_COPPER); rect(153, 81, 197, 81, M_COPPER);
        gasRect(191, 66, 196, 70, M_VAPOR, 0.6f);
        label(150, 48, "20KV, 2 CELL GAP = SPARK PLUG");
        // C: a lead wire in the loop acts as a fuse when the circuit is shorted by a copper bar
        world.battV = 12.f; world.battA = 60.f;
        rect(250, 60, 252, 62, M_BATT_POS); rect(250, 80, 252, 82, M_BATT_NEG);
        rect(253, 61, 280, 61, M_LEAD); rect(281, 61, 300, 61, M_COPPER); rect(300, 61, 300, 81, M_COPPER); rect(253, 81, 300, 81, M_COPPER);
        label(250, 48, "SHORT CIRCUIT BLOWS THE LEAD FUSE WIRE");
        // D: a loose copper bar falls across a gap in the wire, completing the circuit through a filament
        world.battV = 12.f; world.battA = 30.f;
        rect(320, 108, 322, 110, M_BATT_POS); rect(320, 130, 322, 132, M_BATT_NEG);
        rect(323, 110, 355, 110, M_COPPER); rect(365, 110, 380, 110, M_COPPER);
        rect(380, 110, 380, 116, M_COPPER); rect(380, 117, 380, 124, M_TUNGSTEN); rect(380, 125, 380, 131, M_COPPER);
        rect(323, 131, 380, 131, M_COPPER);
        rect(349, 96, 349, 109, M_WALL); rect(371, 96, 371, 109, M_WALL);
        label(318, 86, "FALLING BAR CLOSES THE SWITCH");
        phys.addBox(Vec2(360, 90), Vec2(8, 2), 0, M_COPPER, false);
        phys.stampBodies();
    }
    void buildBondTest() {
        resetWorld();
        rect(0, 200, 399, 203, M_WALL);
        // 1: a steel block glued under a ledge with paraffin; a heater beside it warms it until the wax lets go
        phys.addBox(Vec2(50, 90), Vec2(30, 3), 0, M_STEEL, true);
        int w1 = phys.addBox(Vec2(50, 100), Vec2(8, 8), 0, M_STEEL, false);
        phys.addBond(Vec2(50, 93), w1, 0, BOND_TEMP[0], BOND_FORCE[0] * 8.f);
        rect(59, 94, 66, 106, M_HEATER);
        label(20, 78, "PARAFFIN BOND MELTS WHEN HEATED (55C)");
        // 2: platforms pinned in mid-air by shear pins; a heavy ball dropped on one snaps its pin (the twin stays)
        int p1 = phys.addBox(Vec2(135, 160), Vec2(14, 2), 0, M_STEEL, false);
        phys.addBond(Vec2(135, 160), p1, -1, BOND_TEMP[3], 3.2e5f);
        int p2 = phys.addBox(Vec2(185, 160), Vec2(14, 2), 0, M_STEEL, false);
        phys.addBond(Vec2(185, 160), p2, -1, BOND_TEMP[3], 3.2e5f);
        phys.addCircle(Vec2(135, 110), 5.f, M_LEAD, false, false);
        label(110, 100, "SHEAR PIN SNAPS UNDER THE BALL (TWIN STAYS)");
        // 3: a pressure-release plug: gas builds up behind a bonded plug in a tube until the bond breaks
        phys.addPipe(Vec2(220, 150), Vec2(290, 150), 14.f, 2.f, M_STEEL, true);
        rect(219, 143, 220, 157, M_WALL);
        int plug = phys.addBox(Vec2(285, 150), Vec2(3, 4.8f), 0, M_PLASTIC, false);
        phys.addBond(Vec2(285, 150), plug, -1, 55.f + 300.f, 8.0e4f);
        phys.addBox(Vec2(224, 150), Vec2(2, 3), 0, M_STEEL, true);
        phys.bodies.back().src = Emitter{true, M_STEAM, 300.f, 0.f, 1};
        label(212, 128, "PLUG BOND LETS GO UNDER GAS PRESSURE");
        // 4: a paraffin cell plug holds water back until warmed
        rect(330, 120, 360, 122, M_WALL); rect(330, 120, 331, 180, M_WALL); rect(359, 120, 360, 180, M_WALL);
        rect(332, 123, 358, 150, M_WATER); rect(344, 151, 346, 153, M_PARAFFIN); rect(332, 151, 343, 153, M_WALL); rect(347, 151, 358, 153, M_WALL);
        rect(343, 154, 347, 158, M_HEATER);
        label(318, 108, "WAX CELL PLUG MELTS, WATER DRAINS");
        phys.stampBodies();
    }
    void buildPrimerTest() {
        resetWorld();
        rect(0, 200, 399, 203, M_WALL);
        // a steel firing pin on a slider strikes a primer cartridge; the flash from the far end lights gunpowder
        rect(92, 141, 130, 143, M_WALL); rect(92, 159, 130, 161, M_WALL);   // chamber roof and floor
        rect(126, 144, 130, 158, M_WALL);
        rect(109, 150, 125, 158, M_GUNPOWDER);
        phys.addBox(Vec2(100, 155), Vec2(8, 3.f), 0, M_PRIMER, true);       // seated primer, flash hole to the right
        rect(92, 144, 108, 151, M_WALL);
        int pin = phys.addBox(Vec2(40, 155), Vec2(10, 1.5f), 0, M_STEEL, false);
        phys.bodies[pin].vel = Vec2(160, 0);
        phys.addSlider(pin, Vec2(1, 0));
        label(30, 132, "FIRING PIN STRIKES THE PRIMER (LEFT END)");
        label(90, 124, "FLASH EXITS THE FAR END: IGNITES THE POWDER");
        // a primer cell rod, struck by a dropped weight, lights gasoline vapour at the other end
        rect(250, 190, 270, 192, M_WALL);
        rect(255, 185, 255, 189, M_PRIMER); rect(255, 189, 262, 189, M_PRIMER);
        rect(256, 170, 264, 188, M_EMPTY);
        gasRect(262, 184, 268, 188, M_VAPOR, 0.7f);
        phys.addBox(Vec2(255, 150), Vec2(5, 5), 0, M_STEEL, false);
        label(230, 138, "PRIMER CELLS: IMPACT FLASHES THROUGH THE ROD");
        phys.stampBodies();
    }
    void buildEmitterTest() {
        resetWorld();
        rect(0, 200, 399, 203, M_WALL);
        rect(40, 100, 41, 199, M_WALL); rect(160, 100, 161, 199, M_WALL);
        tool = T_EMITTER; anchored = true; payload = M_WATER; clearSelection(); openForm(); typeInto({"100", "40", "6", "6", "60"}); applyForm();
        anchored = false;
        // a free-falling emitter box carrying gasoline vapour, and one pinned to a swinging arm
        payload = M_GASOLINE; closeForm(); clearSelection(); openForm(); typeInto({"250", "40", "6", "6", "40"}); applyForm();
        int free_ = primary;
        (void)free_;
        payload = M_SAND; closeForm(); clearSelection(); openForm(); typeInto({"320", "60", "6", "6", "30"}); applyForm();
        int sandE = primary;
        anchored = true; tool = T_BOX; closeForm(); clearSelection(); openForm(); typeInto({"320", "30", "6", "6", "0"}); applyForm();
        anchored = false;
        int pivot = primary;
        phys.addPin(Vec2(320, 30), pivot, sandE, false, false);
        closeForm(); clearSelection();
        phys.stampBodies();
    }

    // ---------------------------------------------------------------- input
    Vec2 toWorld(int mx, int my) const { return Vec2((float)mx / S, (float)my / S); }

    void createCircle(Vec2 c, float r, bool wheel) {
        int id = phys.addCircle(c, r, wheel && bodyMat == M_STEEL ? (uint8_t)M_RUBBER : bodyMat, anchored, wheel);
        if (wheel) {
            int under = phys.pickBody(c, true, id);
            if (under >= 0) phys.addPin(c, under, id, true, true);
        }
        sel = {id}; primary = id; partMode = false;
    }

    void createShape(Vec2 a, Vec2 b) {
        Vec2 d = b - a;
        switch (tool) {
            case T_BOX: {
                Vec2 half = Vec2(std::fabs(d.x), std::fabs(d.y)) * 0.5f;
                Vec2 c = (a + b) * 0.5f;
                if (half.x < 2 || half.y < 2) { half = Vec2(7, 7); c = a; }
                int id = phys.addBox(c, half, 0, bodyMat, anchored);
                sel = {id}; primary = id; partMode = false;
                lastW = half.x * 2; lastH = half.y * 2;
                break;
            }
            case T_CIRCLE: case T_WHEEL: {
                float r = length(d);
                if (r < 3) r = 8;
                createCircle(a, r, tool == T_WHEEL);
                lastR = r;
                break;
            }
            case T_EMITTER: {
                Vec2 half = Vec2(std::fabs(d.x), std::fabs(d.y)) * 0.5f;
                Vec2 c = (a + b) * 0.5f;
                if (half.x < 1.5f || half.y < 1.5f) { half = Vec2(3, 3); c = a; }
                int id = phys.addBox(c, half, 0, bodyMat, anchored);
                phys.bodies[id].src = Emitter{true, payload, lastRate, 0.f, 0};
                sel = {id}; primary = id; partMode = false;
                notify(std::string("EMITTER OF ") + MATS[payload].name + " " + fmt(lastRate) + "/S. SELECT IT + ENTER TO ADJUST");
                break;
            }
            case T_ROCKET: {
                float ang = length(d) > 4 ? std::atan2(d.x, -d.y) : 0.f;
                phys.addRocket(a, ang);
                break;
            }
            case T_PIPE: case T_HOSE: {
                if (length(d) < 4) break;
                int g = tool == T_PIPE ? phys.addPipe(a, b, pipeD, pipeWall, bodyMat, anchored)
                                       : phys.addHose(a, b, pipeD, pipeWall, hoseSegs > 0 ? hoseSegs : autoSegs(a, b), bodyMat, anchored);
                if (g >= 0) { sel = phys.groupMembers(g); primary = sel.empty() ? -1 : sel[0]; partMode = false; }
                lastLen = length(d);
                break;
            }
            default: break;
        }
        if (formKind != FK_NONE && !sel.empty()) {}  // form keeps its current mode
    }

    void clickJoint(Vec2 p) {
        std::vector<int> ids = phys.bodiesAt(p);
        if (ids.empty()) return;
        int a = ids.back(), b = -1;
        if (ids.size() >= 2) { a = ids[ids.size() - 2]; b = ids.back(); }
        bool motor = tool == T_MOTOR || tool == T_AUTOMOTOR;
        phys.addPin(p, a, b, motor, tool != T_AUTOMOTOR);
    }

    void clickBond(Vec2 p) {
        std::vector<int> ids = phys.bodiesAt(p);
        if (ids.empty()) { notify("CLICK ON A BODY (OR WHERE TWO OVERLAP)"); return; }
        int a = ids.back(), b = ids.size() >= 2 ? ids[ids.size() - 2] : -1;
        phys.addBond(p, a, b, bondT, bondF);
        notify(std::string("BONDED: LETS GO ABOVE ") + fmt(bondT) + "C OR " + fmt(bondF / 1000.f) + " KN");
    }

    void handleSimDown(int button) {
        if (button == SDL_BUTTON_RIGHT) { rmb = true; return; }
        lmb = true;
        dragStart = smouse();
        switch (tool) {
            case T_BOND: clickBond(mouse); break;
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
            case T_SELECT: break;
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
            case T_BOX: case T_CIRCLE: case T_WHEEL: case T_ROCKET: case T_PIPE: case T_HOSE: case T_EMITTER: createShape(dragStart, smouse()); break;
            case T_SELECT: {
                const Uint8* ks = SDL_GetKeyboardState(nullptr);
                bool add = ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT];
                bool part = ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_RCTRL];
                if (length(mouse - dragStart) < 3.f) selectBody(phys.pickBody(mouse, true), add, part);
                else boxSelect(dragStart, mouse, add, part);
                break;
            }
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
                    if (e.button.button == SDL_BUTTON_LEFT && formClick(e.button.x, e.button.y)) break;
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
                    if (lmb || e.button.button == SDL_BUTTON_RIGHT) handleSimUp(e.button.button);
                    break;
                case SDL_MOUSEWHEEL:
                    if (tool == T_PIPE || tool == T_HOSE) {
                        pipeD = std::clamp(pipeD + (e.wheel.y > 0 ? 1.f : -1.f), 3.f, 60.f);
                        pipeWall = std::min(pipeWall, pipeD * 0.5f);
                    } else brush = std::clamp(brush + (e.wheel.y > 0 ? 1 : -1), 1, 24);
                    break;
                case SDL_KEYDOWN: handleKey(e.key.keysym.sym); break;
                default: break;
            }
        }
    }

    void handleKey(SDL_Keycode k) {
        if (formKey(k, SDL_GetModState())) return;
        switch (k) {
            case SDLK_RETURN: case SDLK_KP_ENTER: openForm(); break;
            case SDLK_ESCAPE: if (!sel.empty()) clearSelection(); else running = false; break;
            case SDLK_g: if (SDL_GetModState() & KMOD_CTRL) { groupSelection(); break; } phys.gravity.y = phys.gravity.y > 0 ? -260.f : 260.f; break;
            case SDLK_u: if (SDL_GetModState() & KMOD_CTRL) ungroupSelection(); break;
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
                if (!sel.empty()) {
                    for (int id : std::vector<int>(sel)) phys.removeBody(id);
                    clearSelection(); phys.stampBodies();
                    break;
                }
                int id = phys.pickBody(mouse, true);
                if (id >= 0) { phys.removeBody(id); phys.stampBodies(); }
                break;
            }
            default: break;
        }
    }

    void continuousInput() {
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        float m = 0;
        if (ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_D]) m += 1;
        if (ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_A]) m -= 1;
        phys.motorInput = formKind != FK_NONE ? 0.f : m;
        phys.thrustOn = formKind == FK_NONE && (ks[SDL_SCANCODE_UP] || ks[SDL_SCANCODE_W]);
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
        if (tool != lastTool) {
            lastTool = tool;
            if (shapeTool(tool)) { clearSelection(); }
            if (formKind != FK_NONE) { closeForm(); if (shapeTool(tool)) openForm(); }
        }
        pruneSelection();
        if (noteFrames > 0) --noteFrames;
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
        if (elecView && (int)world.volt.size() == World::W * World::H) {
            for (int i = 0; i < World::W * World::H; ++i) {
                int b = world.bodyMask[i];
                float sg = b >= 0 ? (b < (int)world.bodySigma.size() ? world.bodySigma[b] : 0.f) : MATS[world.cells[i].t].elec;
                if (sg <= 0.f) { pixels[i] = mix(pixels[i], bg, 0.65f); continue; }
                float v = world.volt[i], f = world.vMax > 0.5f ? std::clamp(std::log1p(std::fabs(v)) / std::log1p(world.vMax), 0.f, 1.f) : 0.f;
                uint32_t col = v > 0.01f ? mix(0x1c4a78, 0xfff060, f) : 0x203040;
                float cur = std::min(1.f, world.curr[i] * 0.05f);
                pixels[i] = mix(col, 0xff9040, cur * 0.6f);
            }
        }
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
            if (b.src.on) fill = mix(fill, 0xFF000000u | MATS[b.src.mat].color, 0.5f);
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
                if (b.src.on) {
                    fillPoly(circlePts(b.pos, std::min(2.f, std::min(b.half.x, b.half.y) * 0.6f), 10), 0xFF000000u | MATS[b.src.mat].color);
                    static const Vec2 fd[5] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                    if (b.src.face) lineWorld(b.pos, b.toWorld(Vec2(fd[b.src.face].x * b.half.x, fd[b.src.face].y * b.half.y)), SDL_Color{255, 255, 255, 220}, 2);
                }
                if (b.isStatic) {
                    lineWorld(b.toWorld(c[0]), b.toWorld(c[2]), SDL_Color{255, 255, 255, 50});
                    lineWorld(b.toWorld(c[1]), b.toWorld(c[3]), SDL_Color{255, 255, 255, 50});
                }
            } else {
                std::vector<Vec2> pts = circlePts(b.pos, b.radius);
                fillPoly(pts, fill);
                outlinePoly(pts, edge);
                if (b.src.on) fillPoly(circlePts(b.pos, std::min(2.f, b.radius * 0.5f), 10), 0xFF000000u | MATS[b.src.mat].color);
                int spokes = b.isWheel ? 4 : 1;
                for (int i = 0; i < spokes; ++i) {
                    float a = b.angle + i * (2 * PI / spokes);
                    lineWorld(b.pos, b.pos + Vec2(std::cos(a), std::sin(a)) * b.radius * 0.9f, edge, b.isWheel ? 2 : 1);
                }
                if (b.isWheel) fillPoly(circlePts(b.pos, 2.f, 10), 0xc8ccd4);
            }
        }
    }

    std::vector<Vec2> bodyOutline(const Body& b, float grow = 0.f) {
        if (b.shape == SHAPE_CIRCLE) return circlePts(b.pos, b.radius + grow);
        Vec2 h = b.half + Vec2(grow, grow);
        return {b.toWorld(Vec2(-h.x, -h.y)), b.toWorld(Vec2(h.x, -h.y)), b.toWorld(Vec2(h.x, h.y)), b.toWorld(Vec2(-h.x, h.y))};
    }

    void renderSelection() {
        for (auto& b : phys.bodies)
            if (b.alive && b.group >= 0) outlinePoly(bodyOutline(b, 0.7f), SDL_Color{70, 220, 255, 55});
        for (int id : sel) {
            if (id < 0 || id >= (int)phys.bodies.size() || !phys.bodies[id].alive) continue;
            bool pri = id == primary;
            outlinePoly(bodyOutline(phys.bodies[id], 1.2f), pri ? SDL_Color{255, 255, 255, 255} : SDL_Color{255, 220, 80, 230});
        }
        if (primary >= 0) {
            Vec2 p = phys.bodies[primary].pos;
            lineWorld(p + Vec2(-3, 0), p + Vec2(3, 0), SDL_Color{255, 255, 255, 255});
            lineWorld(p + Vec2(0, -3), p + Vec2(0, 3), SDL_Color{255, 255, 255, 255});
        }
        if (tool == T_SELECT && lmb && length(mouse - dragStart) >= 3.f) {
            Vec2 a = dragStart, b = mouse;
            outlinePoly({a, Vec2(b.x, a.y), b, Vec2(a.x, b.y)}, SDL_Color{255, 220, 80, 200});
        }
    }

    void renderJoints() {
        for (auto& j : phys.joints) {
            if (!j.alive || j.group >= 0) continue;
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

    void renderArcs() {
        for (auto& a : world.arcs) {
            Vec2 p0(a.x0 + 0.5f, a.y0 + 0.5f), p1(a.x1 + 0.5f, a.y1 + 0.5f);
            lineWorld(p0, p1, SDL_Color{110, 150, 255, 140}, 7);
            lineWorld(p0, p1, SDL_Color{230, 240, 255, 255}, 3);
        }
    }

    void renderLabels() {
        for (auto& l : labels) font::draw(ren, l.s, (int)(l.p.x * S), (int)(l.p.y * S), 1, SDL_Color{200, 210, 230, 200});
    }

    void ghostLabel(Vec2 at, const std::string& t) {
        font::draw(ren, t, (int)(at.x * S) + 8, (int)(at.y * S) - 14, 2, SDL_Color{255, 240, 150, 255});
    }

    void renderGhost() {
        if (!inSim) return;
        SDL_Color white{255, 255, 255, 170};
        Vec2 m = smouse();
        if (lmb) {
            Vec2 d = m - dragStart;
            switch (tool) {
                case T_BOX: case T_EMITTER: {
                    Vec2 a = dragStart, b = m;
                    outlinePoly({a, Vec2(b.x, a.y), b, Vec2(a.x, b.y)}, white);
                    ghostLabel(b, fmt(std::fabs(d.x)) + " X " + fmt(std::fabs(d.y)));
                    break;
                }
                case T_CIRCLE: case T_WHEEL:
                    outlinePoly(circlePts(dragStart, length(d)), white);
                    ghostLabel(m, "R " + fmt(length(d)));
                    break;
                case T_PIPE: case T_HOSE: {
                    if (length(d) < 1.f) break;
                    Vec2 n = Vec2(-d.y, d.x) / length(d) * (pipeD * 0.5f);
                    lineWorld(dragStart + n, m + n, white, 1);
                    lineWorld(dragStart - n, m - n, white, 1);
                    ghostLabel(m, "L " + fmt(length(d)) + " D " + fmt(pipeD));
                    break;
                }
                case T_ROCKET: case T_ROD: case T_SPRING: case T_SLIDER: lineWorld(dragStart, m, white, 2); break;
                default: break;
            }
        }
        if (formKind == FK_BOX) {
            float ang = fv(4) * PI / 180.f;
            Vec2 c(fv(0), fv(1)), h = Vec2(fv(2), fv(3)) * 0.5f;
            std::vector<Vec2> pts;
            for (Vec2 q : {Vec2(-h.x, -h.y), Vec2(h.x, -h.y), Vec2(h.x, h.y), Vec2(-h.x, h.y)}) pts.push_back(c + rotate(q, ang));
            outlinePoly(pts, SDL_Color{120, 255, 160, 220});
        } else if (formKind == FK_CIRCLE) {
            outlinePoly(circlePts(Vec2(fv(0), fv(1)), fv(2)), SDL_Color{120, 255, 160, 220});
        } else if (formKind == FK_PIPE || formKind == FK_HOSE) {
            Vec2 a(fv(0), fv(1)), b(fv(2), fv(3)), d = b - a;
            if (length(d) > 1.f) {
                Vec2 n = Vec2(-d.y, d.x) / length(d) * (std::max(2.f, fv(4)) * 0.5f);
                lineWorld(a + n, b + n, SDL_Color{120, 255, 160, 220}, 1);
                lineWorld(a - n, b - n, SDL_Color{120, 255, 160, 220}, 1);
            }
        }
        if (tool == T_MAT) {
            outlinePoly(circlePts(mouse, (float)brush + 0.5f, 24), SDL_Color{255, 255, 255, 110});
        } else {
            lineWorld(m + Vec2(-3, 0), m + Vec2(3, 0), white);
            lineWorld(m + Vec2(0, -3), m + Vec2(0, 3), white);
        }
    }

    void renderForm() {
        if (formKind == FK_NONE) return;
        SDL_Rect r = formRect();
        SDL_SetRenderDrawColor(ren, 14, 18, 28, 235);
        SDL_RenderFillRect(ren, &r);
        SDL_SetRenderDrawColor(ren, 120, 255, 160, 255);
        SDL_RenderDrawRect(ren, &r);
        const char* title = "";
        switch (formKind) {
            case FK_BOX: title = "NEW BOX (CELLS)"; break;
            case FK_CIRCLE: title = wheelForm ? "NEW WHEEL (CELLS)" : "NEW CIRCLE (CELLS)"; break;
            case FK_PIPE: title = "NEW PIPE (CELLS)"; break;
            case FK_BATTERY: title = "BATTERY (VOLTS / AMPS)"; break;
            case FK_BOND: title = "BOND SETTINGS"; break;
            case FK_EMITTER: title = "NEW EMITTER (CELLS, RATE = CELLS/S)"; break;
            case FK_SCALE: title = "SCALE SELECTION"; break;
            case FK_HOSE: title = "NEW HOSE (CELLS)"; break;
            case FK_EDIT_BOX: title = "EDIT BOX PART"; break;
            case FK_EDIT_CIRCLE: title = "EDIT CIRCLE PART"; break;
            case FK_EDIT_GROUP: title = "EDIT GROUP (MOVE/ROTATE)"; break;
            default: break;
        }
        font::draw(ren, title, 16, 16, 2, SDL_Color{120, 255, 160, 255});
        for (int i = 0; i < (int)fields.size(); ++i) {
            SDL_Rect f = fieldRect(i);
            bool act = i == fActive;
            SDL_SetRenderDrawColor(ren, act ? 50 : 28, act ? 66 : 34, act ? 100 : 46, 255);
            SDL_RenderFillRect(ren, &f);
            font::draw(ren, fields[i].name, f.x + 4, f.y + 2, 2, SDL_Color{170, 185, 210, 255});
            std::string t = fields[i].text + (act && !fFresh ? "_" : "");
            font::draw(ren, t, f.x + 130, f.y + 2, 2, act ? SDL_Color{255, 255, 255, 255} : SDL_Color{220, 230, 245, 255});
        }
        int y = 36 + (int)fields.size() * 20 + 4;
        if (formKind == FK_SCALE) font::draw(ren, "SCALES ALL SELECTED ABOUT THEIR CENTRE", 16, y, 1, SDL_Color{255, 220, 120, 255}), y += 12;
        if (emitterFormActive()) {
            font::draw(ren, std::string("EMITS: ") + MATS[fPayload].name + " (CLICK PALETTE)  FACE: " + faceName(fFace) + " (F)", 16, y + (formKind >= FK_EDIT_BOX ? 12 : 0), 1, SDL_Color{255, 160, 255, 255});
            if (formKind == FK_EMITTER) y += 12;
        }
        if (formKind >= FK_EDIT_BOX) {
            std::string ms = formKind == FK_EDIT_GROUP ? "" : std::string("M: ") + MATS[fMat].name + "  ";
            ms += std::string("S: ") + (fStatic ? "STATIC" : "DYNAMIC");
            if (formKind == FK_EDIT_GROUP) ms = "PARTS: " + std::to_string(sel.size()) + "  CTRL+CLICK EDITS ONE PART";
            font::draw(ren, ms, 16, y, 1, SDL_Color{255, 220, 120, 255});
            y += 12;
        }
        font::draw(ren, "TAB/CLICK: NEXT FIELD  ENTER: APPLY  ESC: CLOSE", 16, y + (formKind >= FK_EDIT_BOX && emitterFormActive() ? 12 : 0), 1, SDL_Color{150, 160, 180, 255});
        font::draw(ren, formMsg, 16, y + 12, 1, SDL_Color{150, 230, 255, 255});
    }

    std::string hoverText() const {
        int x = (int)mouse.x, y = (int)mouse.y;
        if (!inSim || !world.inb(x, y)) return "";
        char buf[96];
        int bid = world.bodyMask[y * World::W + x];
        if (bid >= 0 && bid < (int)phys.bodies.size() && phys.bodies[bid].alive) {
            const Body& b = phys.bodies[bid];
            if (b.src.on) std::snprintf(buf, sizeof buf, "EMITTER %s %g/S", MATS[b.src.mat].name, b.src.rate);
            else std::snprintf(buf, sizeof buf, "BODY %s %dC", MATS[b.mat].name, (int)b.temp);
            return buf;
        }
        const Cell& c = world.at(x, y);
        if (c.t == M_BATT_POS || c.t == M_BATT_NEG) {
            std::snprintf(buf, sizeof buf, "%s %gV %gA", MATS[c.t].name, std::round(World::decV(c.life) * 10) / 10, std::round(World::decA(c.aux) * 1000) / 1000);
            return buf;
        }
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
        if (tool == T_MAT && (mat == M_BATT_POS || mat == M_BATT_NEG)) status += "  PAINTS " + fmt(world.battV) + "V " + fmt(world.battA) + "A";
        if (mat == M_SOURCE) status += "  EMITS: " + std::string(MATS[payload].name);
        if (tool == T_PIPE || tool == T_HOSE) status += "  DIA " + fmt(pipeD) + " WALL " + fmt(pipeWall);
        else status += "  BRUSH " + std::to_string(brush);
        if (!sel.empty()) status += "  SEL " + std::to_string(sel.size());
        status += "  X " + std::to_string((int)smouse().x) + " Y " + std::to_string((int)smouse().y);
        status += "  FPS " + std::to_string((int)fps);
        if (paused) status += "  [PAUSED]";
        font::draw(ren, status, 8, sy, 2, SDL_Color{255, 220, 120, 255});
        if (noteFrames > 0) font::draw(ren, note, 8, SIM_H - 14, 2, SDL_Color{255, 200, 120, 255});
        else if (phys.eventFrames > 0) font::draw(ren, phys.lastEvent, 8, SIM_H - 14, 2, SDL_Color{255, 120, 90, 255});
        if (world.vMax > 0.f) {
            char eb[96];
            std::snprintf(eb, sizeof eb, "ELEC: PEAK %.4g V  SOURCE %.3g A  ARCS %ld", world.vMax, world.iSource, world.arcCount);
            font::draw(ren, eb, WIN_W - font::textWidth(eb, 1) - 6, 6, 1, SDL_Color{255, 240, 140, 255});
        }
        std::string hover = hoverText();
        if (!hover.empty()) font::draw(ren, hover, 8, sy + 17, 2, SDL_Color{150, 230, 255, 255});
        font::draw(ren, TOOL_HINTS[tool], 8 + (hover.empty() ? 0 : font::textWidth(hover, 2) + 20), sy + 20, 1, SDL_Color{170, 180, 200, 255});
        font::draw(ren,
                   "SPACE PAUSE  N STEP  C CLEAR CELLS  X CLEAR BODIES  R DEMO  T ANCHOR  F BODY MATERIAL  ENTER EXACT VALUES  CTRL+G GROUP  CTRL+U UNGROUP  H HEAT VIEW  E HOLD = SPARK  , . SPARK RATE  V CAR  G GRAVITY  ARROWS DRIVE  UP ROCKETS",
                   8, sy + 32, 1, SDL_Color{120, 130, 150, 255});
    }

    void render() {
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        renderParticles();
        renderBodies();
        renderJoints();
        renderArcs();
        renderSelection();
        renderLabels();
        renderGhost();
        renderForm();
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

int runSelfTests();

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) if (!std::strcmp(argv[i], "--selftest")) return runSelfTests();
    // Headless self-test: sandbots --shot out.bmp [frames] [--scene N] [--heat] [--trace]
    const char* shot = nullptr;
    int shotFrames = 300, scene = 0;
    bool heat = false, trace = false, g0 = false, elecFlag = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) {
            shot = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') shotFrames = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--scene") && i + 1 < argc) {
            scene = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--heat")) heat = true;
        else if (!std::strcmp(argv[i], "--trace")) trace = true;
        else if (!std::strcmp(argv[i], "--elec")) elecFlag = true;
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
            case 11: g.buildPrecisionTest(); break;
            case 12: g.buildPrecisionTest(); g.selectBody(g.sel.empty() ? -1 : g.sel[0], false, true); g.openForm(); g.formMsg = "UPDATED"; break;
            case 15: g.buildCutTest(); break;
            case 16: g.buildEmitterTest(); break;
            case 17: g.buildElectricTest(); break;
            case 18: g.buildBondTest(); break;
            case 19: g.buildPrimerTest(); break;
            case 13: g.buildPrecisionTest(); g.tool = Tool::T_HOSE; g.clearSelection(); g.openForm(); break;
            default: g.buildTestScene(scene); break;
        }
        if (heat) g.heatView = true;
        if (elecFlag) g.elecView = true;
        if (g0) g.phys.gravity = Vec2(0, 0);
        for (int f = 0; f < shotFrames; ++f) {
            g.phys.motorInput = (scene == 0 && f > 40) ? 1.f : 0.f;
            if (scene == 1 || scene == 2) g.phys.thrustOn = scene == 2;
            g.phys.step(1.f / 60.f);
            g.world.step();
            if (!scene && f == 120) g.world.explode(352, 210, 24.f, 260.f);
            if (trace && f % (std::getenv("TRACE_EVERY") ? std::atoi(std::getenv("TRACE_EVERY")) : 120) == 0) {
                double gasTot = 0; int water = 0, gcnt = 0, gp = 0; float tmax = 0;
                for (auto& c : g.world.cells) {
                    if (MATS[c.t].kind == K_GAS && c.t != M_FIRE) { gasTot += c.amt; ++gcnt; tmax = std::max(tmax, c.temp); }
                    if (c.t == M_WATER) ++water;
                    if (c.t == M_GUNPOWDER) ++gp;
                }
                std::printf("f=%4d water=%d powder=%d gas cells=%d amt=%.1f Tmax=%.0f", f, water, gp, gcnt, gasTot, tmax);
                for (auto& b : g.phys.bodies)
                    if (b.alive && !b.isStatic) std::printf(" [%d x=%.1f y=%.1f vx=%.1f F=%.0f w=%.2f]", b.id, b.pos.x, b.pos.y, b.vel.x, b.fluidF.x, b.w);
                std::printf("\n");
            }
        }
        std::printf("burn events: %ld\n", g.world.burnEvents);
        std::printf("elec: vMax=%.4g iSource=%.3g arcs=%ld\n", g.world.vMax, g.world.iSource, g.world.arcCount);
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
