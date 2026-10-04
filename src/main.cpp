// SandBots: a physics sandbox (rigid bodies, joints, motors) fused with a
// thermal falling-sand / fluid simulation. Build engines, boilers and hydraulics.
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
constexpr int VIEW_W = 360;                 // cells visible across the view; the world is wider and the camera scrolls
constexpr int SIM_W = VIEW_W * S;
constexpr int SIM_H = World::H * S;
// window layout: tool panel | simulation view | properties panel, a toolbar above and a status bar below
constexpr int LEFT_W = 200, RIGHT_W = 252, TOP_H = 46, BOT_H = 74;
constexpr int SIM_X = LEFT_W, SIM_Y = TOP_H;
constexpr int WIN_W = LEFT_W + SIM_W + RIGHT_W, WIN_H = TOP_H + SIM_H + BOT_H;
constexpr float PI = 3.14159265f;

enum Tool {
    T_MAT, T_BOX, T_CIRCLE, T_WHEEL, T_ROCKET, T_PIN, T_MOTOR, T_AUTOMOTOR, T_ROD, T_SPRING, T_GRAB, T_DELETE, T_SLIDER, T_SELECT, T_PIPE, T_HOSE, T_EMITTER, T_BOND, T_FAN, T_CUT
};
const char* TOOL_NAMES[] = {"PARTICLES", "BOX", "CIRCLE", "WHEEL", "ROCKET", "PIN JOINT", "MOTOR (ARROWS)",
                            "AUTO MOTOR", "ROD", "SPRING", "GRAB", "DELETE", "SLIDER", "SELECT", "PIPE", "HOSE", "EMITTER", "BOND", "FAN", "CUT"};
const char* TOOL_HINTS[] = {
    "LMB: PAINT  RMB: ERASE  WHEEL: BRUSH SIZE",
    "DRAG TO SIZE A BOX OF THE SELECTED SOLID (CLICK = DEFAULT)",
    "DRAG FROM CENTRE TO SET RADIUS",
    "DRAG TO SIZE. AUTO-MOTORS ONTO A BODY UNDER ITS CENTRE (ARROWS DRIVE)",
    "DRAG TO SET THRUST DIRECTION. HOLD UP/W TO FIRE",
    "CLICK WHERE TWO BODIES OVERLAP OR TOUCH (WITH ONLY ONE NEARBY, IT PINS TO THE WORLD)",
    "CLICK ON A JOINT SPOT. LEFT/RIGHT ARROWS (A/D) SPIN IT",
    "LIKE MOTOR BUT SPINS ALL THE TIME",
    "DRAG FROM ONE BODY/POINT TO ANOTHER",
    "DRAG FROM ONE BODY/POINT TO ANOTHER",
    "DRAG BODIES AROUND",
    "CLICK A BODY OR JOINT TO REMOVE IT",
    "PRESS ON THE SLIDING BODY (A PISTON) AND DRAG ALONG ITS LINE, ENDING ON THE BODY IT SLIDES IN (THE CYLINDER) - OR ON EMPTY SPACE TO FIX THE LINE IN THE WORLD. ROTATION IS LOCKED",
    "CLICK A BODY TO SELECT IT (CLICK AGAIN TO REACH THE ONE UNDER IT, SHIFT ADDS), OR CLICK A JOINT (SPRING, ROD, MOTOR...) TO EDIT IT. DRAG SELECTED BODIES TO MOVE THEM. DRAG EMPTY SPACE TO BOX-SELECT",
    "DRAG ALONG THE PIPE. WHEEL = DIAMETER. ENTER = TYPE EXACT ENDS/DIAMETER/WALL",
    "DRAG ALONG THE HOSE. WHEEL = DIAMETER. ENTER = TYPE EXACT ENDS/DIAMETER/SEGMENTS",
    "DRAG A SMALL BOX THAT ENDLESSLY PRODUCES THE SELECTED POWDER/LIQUID/GAS. PIN IT, OR LEAVE IT FREE TO TRAVEL. ENTER = RATE",
    "CLICK WHERE TWO BODIES OVERLAP OR TOUCH (ONE NEARBY = BOND TO WORLD): A TEMPORARY WELD THAT LETS GO WHEN TOO HOT OR OVERLOADED. ENTER = SET BOTH",
    "DRAG A FAN: THE ARROW SHOWS WHICH WAY IT BLOWS. SELECT IT, THEN + / - CHANGE STRENGTH AND \\ FLIPS IT. ENTER = EXACT VALUES",
    "DRAG A BOX OR CIRCLE OVER BODIES: THE AREA UNDER IT IS CUT OUT OF EVERY BODY IT TOUCHES. CTRL+Z UNDOES",
};

// materials a rigid body can be made of (shown as swatches, never cycled through)
const uint8_t BODY_MATS[] = {M_STEEL, M_IRON, M_COPPER, M_ALUMINUM, M_LEAD, M_GOLD, M_TITANIUM, M_TUNGSTEN,
                             M_WOOD, M_RUBBER, M_PLASTIC, M_GLASS, M_CONCRETE, M_BRICK, M_CERAMIC, M_STONE,
                             M_ICE, M_SOLDER, M_PARAFFIN, M_PRIMER};
// what an emitter body can produce
const uint8_t EMIT_MATS[] = {M_SAND, M_ASH, M_GUNPOWDER, M_COAL,
                             M_WATER, M_OIL, M_GASOLINE, M_DIESEL, M_KEROSENE, M_JETFUEL, M_ETHANOL, M_HYDRAULIC, M_ACID, M_LAVA,
                             M_STEAM, M_SMOKE, M_EXHAUST, M_VAPOR, M_PROPANE, M_HYDROGEN, M_AIR};
struct PaintGroup { const char* name; std::vector<uint8_t> mats; };
const std::vector<PaintGroup> PAINT_GROUPS = {
    {"POWDERS", {M_SAND, M_ASH, M_GUNPOWDER, M_COAL}},
    {"LIQUIDS", {M_WATER, M_OIL, M_GASOLINE, M_DIESEL, M_KEROSENE, M_JETFUEL, M_ETHANOL, M_HYDRAULIC, M_ACID, M_LAVA}},
    {"GASES", {M_STEAM, M_FIRE, M_SMOKE, M_EXHAUST, M_VAPOR, M_PROPANE, M_HYDROGEN, M_AIR}},
    {"METALS", {M_STEEL, M_IRON, M_COPPER, M_ALUMINUM, M_LEAD, M_GOLD, M_TITANIUM, M_TUNGSTEN, M_SOLDER}},
    {"BUILDING", {M_WALL, M_STONE, M_CONCRETE, M_BRICK, M_CERAMIC, M_GLASS, M_WOOD, M_RUBBER, M_PLASTIC, M_ICE, M_PLANT, M_TNT, M_PARAFFIN}},
    {"DEVICES", {M_HEATER, M_COOLER, M_IGNITER, M_VOID, M_BATT_POS, M_BATT_NEG, M_PRIMER, M_EMPTY}},
};
const int SPARK_RATES[] = {0, 120, 60, 40, 30, 20, 12};
const int SNAPS[] = {0, 1, 2, 5, 10};
// bond presets: melting temperature (deg C) and breaking force (engine units)
const char* BOND_NAMES[] = {"PARAFFIN", "SOLDER", "EPOXY", "SHEAR PIN"};
const float BOND_TEMP[] = {55.f, 190.f, 260.f, 5000.f};
const float BOND_G[] = {10.f, 40.f, 120.f, 30.f};   // load each bond holds, as a multiple of the weight it carries

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
    SDL_Rect r{0, 0, 0, 0};
    std::function<std::string()> label;
    std::function<void()> action;
    std::function<bool()> active;
    std::function<bool()> enabled;   // greyed out and inert when false
    std::string tip;                 // shown in the status bar while hovered
    int zone = 0;                    // 0 toolbar (flowed), 1 tool panel (fixed rectangle), 2 header text
    int gap = 0;                     // extra space before a flow button (separates groups)
    int minW = 0;
    bool right = false;              // flow from the right edge
    int style = 0;                   // 0 normal, 1 run (green when active), 2 stop (red), 3 header
};

// one element of the properties panel, rebuilt every frame from the current tool and selection
struct PItem {
    int kind = 0;                    // 0 header, 1 swatch, 2 button, 3 text line, 4 value box
    SDL_Rect r{0, 0, 0, 0};
    std::string text;
    uint32_t color = 0;
    bool active = false, enabled = true;
    std::function<void()> act;
    std::string tip;
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
    // ---- editing: undo, clipboard, moving, cutting
    std::vector<std::vector<uint8_t>> undoStack, redoStack;
    size_t undoBytes = 0;
    std::string lastUndoKey;
    Uint32 lastUndoTick = 0;
    struct Clip { std::vector<Body> bodies; std::vector<Joint> joints; Vec2 center; } clip;
    bool moving = false, moveArmed = false;
    Vec2 moveApplied;
    int moveHit = -1;
    int cycleIdx = 0;
    Vec2 lastClickPos;
    Uint32 lastClickTick = 0;
    size_t lastClickCount = 0;
    bool cutCircle = false, keepCutter = false;
    float pipeD = 12.f, pipeWall = 2.f;
    int hoseSegs = 0;          // 0 = automatic
    Tool lastTool = T_MAT;
    enum FormKind { FK_NONE, FK_BOX, FK_CIRCLE, FK_PIPE, FK_HOSE, FK_EMITTER, FK_FAN, FK_SCALE, FK_BATTERY, FK_BOND, FK_SAVE, FK_LOAD, FK_EDIT_BOX, FK_EDIT_CIRCLE, FK_EDIT_GROUP };
    struct Field { std::string name, text; };
    FormKind formKind = FK_NONE;
    std::vector<Field> fields;
    int fActive = 0;
    bool fFresh = true;
    uint8_t fMat = M_STEEL;
    bool fStatic = false;
    std::string formMsg;
    bool wheelForm = false;
    // ---- run mode: the drawing is edited while stopped; PLAY snapshots it and STOP restores the snapshot
    bool playing = false;
    std::vector<uint8_t> snapshot;
    std::string currentFile;
    std::vector<std::string> fileList;
    int fileIdx = -1;
    int newArmed = 0;
    int bondType = 0;
    float bondT = 55.f, bondG = 10.f;
    // ---- camera: the window shows VIEW_W cells of the wider world
    float camXf = 0.f, camYf = 0.f;   // top-left of the view, in cells
    int camX = 0, camY = 0;
    float zoom = 1.f;                 // 1 = the whole height of the world fits; higher shows fewer, bigger cells
    bool gridOn = true;               // reference grid and rulers
    float sc() const { return 3.f * zoom; }                 // screen pixels per cell
    float viewW() const { return SIM_W / sc(); }            // cells visible across / down
    float viewH() const { return SIM_H / sc(); }
    static constexpr float ZOOMS[7] = {1.f, 1.5f, 2.f, 3.f, 4.f, 6.f, 8.f};
    int scrX(float x) const { return SIM_X + (int)std::lround((x - camXf) * sc()); }   // world -> window pixels
    int scrY(float y) const { return SIM_Y + (int)std::lround((y - camYf) * sc()); }
    int focusBody = -1;        // the camera follows this body when >= 0
    bool panning = false, scrubbing = false;
    int panStartPx = 0, panStartPy = 0;
    float panStartCam = 0.f, panStartCamY = 0.f;
    bool scrubbingV = false;
    bool fanVacuumDefault = false;
    bool elecView = false, pressureView = false;
    float fanPhase = 0.f;
    float lastFan = 60.f;
    uint8_t fPayload = M_WATER;
    uint8_t fFace = 0;
    uint8_t emitFace = 1;   // outlet side of new emitters: 0 all sides, 1 +x, 2 -x, 3 +y (down), 4 -y (up), in the emitter's own frame
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
        notify("EDIT MODE: DRAW FREELY, THEN PRESS PLAY (SPACE). STOP RESTORES YOUR DRAWING");
        return true;
    }

    bool emitterFormActive() const {
        if (formKind == FK_EMITTER) return true;
        if (formKind == FK_EDIT_BOX) return fields.size() > 5 && std::atof(fields[5].text.c_str()) > 0;
        if (formKind == FK_EDIT_CIRCLE) return fields.size() > 4 && std::atof(fields[4].text.c_str()) > 0;
        return false;
    }
    void selectMaterial(uint8_t m) { tool = T_MAT; mat = m; }

    bool helpOn = false, scenesOpen = false;
    int hoverBtn = -1, hoverItem = -1;
    int mousePx = 0, mousePy = 0;                 // window pixel coordinates of the pointer
    std::vector<PItem> rp, modal;                 // properties panel items, scene-picker items

    // ---------------------------------------------------------------- what the panels act on
    std::vector<int> selectedWith(const std::function<bool(const Body&)>& pred) {
        pruneSelection();
        std::vector<int> r;
        for (int id : sel) if (pred(phys.bodies[id])) r.push_back(id);
        return r;
    }
    std::vector<int> selFans() { return selectedWith([](const Body& b) { return b.fan.strength != 0.f; }); }
    std::vector<int> selEmitters() { return selectedWith([](const Body& b) { return b.src.on; }); }

    void setBodyMaterial(uint8_t m) {
        bodyMat = m;
        pruneSelection();
        if (sel.empty()) return;
        pushUndo("material");
        for (int id : sel) { const Body b = phys.bodies[id]; phys.reshape(id, b.pos, b.half, b.radius, b.angle, m, b.isStatic); }
        phys.stampBodies();
    }
    void setFixed(bool fixed) {
        anchored = fixed;
        pruneSelection();
        if (sel.empty()) return;
        pushUndo("fixed");
        for (int id : sel) { const Body b = phys.bodies[id]; phys.reshape(id, b.pos, b.half, b.radius, b.angle, b.mat, fixed); }
        phys.stampBodies();
    }
    void adjustFan(float d) {
        std::vector<int> fans = selFans();
        if (fans.empty()) { lastFan = std::clamp(lastFan + d, 5.f, 300.f); return; }
        pushUndo("fan");
        for (int id : fans) {
            float& st = phys.bodies[id].fan.strength;
            float mag = std::clamp(std::fabs(st) + d, 5.f, 300.f);
            st = st < 0 ? -mag : mag;
        }
        lastFan = std::fabs(phys.bodies[fans[0]].fan.strength);
    }
    void flipFan() {
        std::vector<int> fans = selFans();
        if (fans.empty()) return;
        pushUndo("fan");
        for (int id : fans) phys.bodies[id].fan.strength = -phys.bodies[id].fan.strength;
    }
    void setFanVacuum(bool v) {
        fanVacuumDefault = v;
        std::vector<int> fans = selFans();
        if (!fans.empty()) pushUndo("fan");
        for (int id : fans) phys.bodies[id].fan.vacuum = v ? 1 : 0;
    }
    void adjustRate(float d) {
        std::vector<int> em = selEmitters();
        if (em.empty()) { lastRate = std::clamp(lastRate + d, 5.f, 1000.f); return; }
        pushUndo("rate");
        for (int id : em) phys.bodies[id].src.rate = std::clamp(phys.bodies[id].src.rate + d, 5.f, 1000.f);
        lastRate = phys.bodies[em[0]].src.rate;
    }
    void setEmitMaterial(uint8_t m) {
        payload = m;
        std::vector<int> em = selEmitters();
        if (!em.empty()) pushUndo("emits");
        for (int id : em) phys.bodies[id].src.mat = m;
    }
    void setSelectionFace(int f) {
        emitFace = (uint8_t)f;
        std::vector<int> em = selEmitters();
        if (!em.empty()) pushUndo("face");
        for (int id : em) phys.bodies[id].src.face = (uint8_t)f;
    }

    // ---------------------------------------------------------------- toolbar and tool panel
    void buildButtons() {
        auto always = [] { return true; };
        auto lit = [](std::string s) { return [s] { return s; }; };
        auto top = [&](std::function<std::string()> label, std::string tip, std::function<void()> act,
                       std::function<bool()> active = nullptr) -> Button& {
            Button b;
            b.zone = 0; b.label = label; b.tip = tip; b.action = act;
            b.active = active ? active : [] { return false; };
            b.enabled = always;
            buttons.push_back(b);
            return buttons.back();
        };
        // ---- toolbar: run control | files | undo | views | focus, scenes | help
        {
            Button& b = top([this] { return std::string(playing && !paused ? "PLAYING" : "PLAY"); },
                            "RUN THE SIMULATION (SPACE). YOUR DRAWING IS SNAPSHOT FIRST, SO STOP CAN RESTORE IT",
                            [this] { play(); }, [this] { return playing && !paused; });
            b.style = 1; b.minW = 76;
        }
        top([this] { return std::string(paused ? "RESUME" : "PAUSE"); }, "FREEZE / RESUME WHILE PLAYING (SPACE)",
            [this] { togglePause(); }, [this] { return playing && paused; }).enabled = [this] { return playing; };
        top(lit("STEP"), "ADVANCE ONE FRAME (N)", [this] { stepFrame(); });
        {
            Button& b = top(lit("STOP"), "STOP AND RESTORE THE DRAWING EXACTLY AS IT WAS BEFORE PLAY", [this] { stopPlay(); });
            b.style = 2; b.enabled = [this] { return playing; };
        }
        top([this] { return std::string(newArmed > 0 ? "SURE?" : "NEW"); }, "CLEAR EVERYTHING (PRESS TWICE) - CTRL+N",
            [this] { newFile(); }, [this] { return newArmed > 0; }).gap = 14;
        top(lit("OPEN"), "OPEN A SAVED FILE (CTRL+O)", [this] { openFileForm(false); }, [this] { return formKind == FK_LOAD; });
        top(lit("SAVE"), "SAVE TO THE CURRENT FILE (CTRL+S)", [this] { saveQuick(); });
        top(lit("SAVE AS"), "SAVE UNDER A NEW NAME", [this] { openFileForm(true); }, [this] { return formKind == FK_SAVE; });
        top(lit("UNDO"), "UNDO THE LAST EDIT (CTRL+Z)", [this] { undo(); }).gap = 14;
        top(lit("REDO"), "REDO (CTRL+Y OR CTRL+SHIFT+Z)", [this] { redo(); });
        top(lit("HEAT"), "COLOUR EVERYTHING BY TEMPERATURE (H)", [this] { heatView = !heatView; }, [this] { return heatView; }).gap = 14;
        top(lit("PRESSURE"), "COLOUR GAS BY PRESSURE: BLUE BELOW AMBIENT, WHITE ABOUT 1, RED HIGH (P)", [this] { pressureView = !pressureView; }, [this] { return pressureView; });
        top(lit("ELECTRIC"), "SHOW VOLTAGE AND CURRENT ON CONDUCTORS", [this] { elecView = !elecView; }, [this] { return elecView; });
        top(lit("GRID"), "A REFERENCE GRID WITH RULERS (CELL NUMBERS) AND A SCALE BAR (K)", [this] { gridOn = !gridOn; }, [this] { return gridOn; }).gap = 14;
        { Button& b = top(lit("-"), "ZOOM OUT (CTRL+WHEEL OR CTRL + MINUS)", [this] { zoomCentre(-1); }, nullptr); b.enabled = [this] { return zoom > 1.01f; }; b.minW = 30; }
        top([this] { return fmt(zoom) + "X"; }, "THE ZOOM. CLICK TO RESET TO 1X (CTRL+0). MIDDLE-DRAG PANS", [this] { zoomReset(); }, [this] { return zoom > 1.01f; });
        { Button& b = top(lit("+"), "ZOOM IN TO WORK ON SMALL STRUCTURES (CTRL+WHEEL OR CTRL + PLUS)", [this] { zoomCentre(1); }, nullptr); b.enabled = [this] { return zoom < 7.9f; }; b.minW = 30; }
        top(lit("FOCUS"), "CAMERA FOLLOWS THE SELECTED BODY (F): SELECT A VEHICLE, PRESS FOCUS, PLAY. MIDDLE-DRAG OR THE STRIP AT THE BOTTOM OF THE VIEW PANS",
            [this] { toggleFocus(); }, [this] { return focusBody >= 0; }).gap = 14;
        top(lit("SCENES"), "READY-MADE MACHINES AND TESTS", [this] { scenesOpen = !scenesOpen; }, [this] { return scenesOpen; });
        { Button& b = top(lit("HELP"), "SHORTCUTS AND A QUICK TOUR (F1)", [this] { helpOn = !helpOn; }, [this] { return helpOn; }); b.right = true; }

        // ---- tool panel: always visible, grouped
        const int colW = (LEFT_W - 16 - 4) / 2;
        int ly = TOP_H + 8, col = 0;
        auto header = [&](const char* t) {
            if (col) { ly += 30; col = 0; }
            Button b;
            b.zone = 2; b.style = 3; b.label = lit(t);
            b.r = SDL_Rect{8, ly + 2, LEFT_W - 16, 16};
            b.enabled = always; b.active = [] { return false; };
            buttons.push_back(b);
            ly += 22;
        };
        auto place = [&](Button& b, bool full) {
            if (full && col) { ly += 30; col = 0; }
            b.r = SDL_Rect{8 + (full ? 0 : col * (colW + 4)), ly, full ? LEFT_W - 16 : colW, 26};
            if (full) ly += 30;
            else if (++col == 2) { col = 0; ly += 30; }
        };
        auto tool_ = [&](const char* name, Tool tl) {
            Button b;
            b.zone = 1; b.label = lit(name); b.tip = TOOL_HINTS[tl]; b.action = [this, tl] { tool = tl; };
            b.active = [this, tl] { return tool == tl; }; b.enabled = always;
            place(b, false);
            buttons.push_back(b);
        };
        auto act_ = [&](const char* name, std::string tip, std::function<void()> act, std::function<bool()> active = nullptr, bool full = false) {
            Button b;
            b.zone = 1; b.label = lit(name); b.tip = tip; b.action = act;
            b.active = active ? active : [] { return false; }; b.enabled = always;
            place(b, full);
            buttons.push_back(b);
        };
        header("TOOLS");
        tool_("SELECT", T_SELECT);
        tool_("GRAB", T_GRAB);
        header("SHAPES");
        tool_("BOX", T_BOX); tool_("CIRCLE", T_CIRCLE); tool_("WHEEL", T_WHEEL); tool_("ROCKET", T_ROCKET);
        tool_("PIPE", T_PIPE); tool_("HOSE", T_HOSE);
        header("MACHINES");
        tool_("FAN", T_FAN); tool_("EMITTER", T_EMITTER);
        header("JOINTS");
        tool_("PIN", T_PIN); tool_("MOTOR", T_MOTOR); tool_("SPINNER", T_AUTOMOTOR); tool_("ROD", T_ROD);
        tool_("SPRING", T_SPRING); tool_("SLIDER", T_SLIDER); tool_("BOND", T_BOND);
        header("EDIT");
        tool_("CUT", T_CUT);
        act_("SCALE", "RESIZE THE SELECTION BY A PERCENTAGE", [this] { openScaleForm(); }, [this] { return formKind == FK_SCALE; });
        act_("EXACT", "TYPE EXACT SIZES, POSITIONS AND ANGLES (ENTER)", [this] { if (formKind == FK_NONE) openForm(); else closeForm(); }, [this] { return formKind != FK_NONE; });
        tool_("DELETE", T_DELETE);
        act_("GROUP", "WELD THE SELECTED BODIES INTO ONE RIGID OBJECT (CTRL+G)", [this] { groupSelection(); });
        act_("UNGROUP", "SPLIT A GROUP BACK INTO BODIES (CTRL+U)", [this] { ungroupSelection(); });
        act_("COPY", "COPY THE SELECTED BODIES (CTRL+C)", [this] { copySelection(); });
        act_("PASTE", "PASTE AT THE CURSOR (CTRL+V)", [this] { pasteClipboard(); });
        act_("SUBTRACT", "CUT THE LAST-CLICKED (RED) BODY OUT OF THE OTHER SELECTED BODIES", [this] { cutSelection(); }, nullptr, true);
        header("PARTICLES");
        act_("PAINT", "PAINT SAND, LIQUIDS, GASES AND SOLIDS AS CELLS (RMB ERASES)", [this] { if (mat == M_EMPTY) mat = M_SAND; tool = T_MAT; },
             [this] { return tool == T_MAT && mat != M_EMPTY; });
        act_("ERASER", "ERASE PARTICLES UNDER THE BRUSH", [this] { tool = T_MAT; mat = M_EMPTY; }, [this] { return tool == T_MAT && mat == M_EMPTY; });
        header("CLEAR");
        act_("WIPE PARTICLES", "REMOVE ALL SAND, LIQUID, GAS AND SOLID CELLS (CTRL+Z BRINGS THEM BACK)",
             [this] { pushUndo(); world.clear(); phys.stampBodies(); }, nullptr, true);
        act_("WIPE BODIES", "REMOVE ALL RIGID BODIES AND JOINTS (CTRL+Z BRINGS THEM BACK)", [this] { pushUndo(); clearBodies(); }, nullptr, true);
    }

    // ---------------------------------------------------------------- the properties panel (rebuilt each frame)
    void buildRightPanel() {
        rp.clear();
        const int x0 = SIM_X + SIM_W + 12, w = RIGHT_W - 24;
        int y = TOP_H + 10;
        auto header = [&](const std::string& t) {
            PItem it; it.kind = 0; it.text = t; it.r = SDL_Rect{x0, y, w, 16}; rp.push_back(it); y += 22;
        };
        auto line = [&](const std::string& t, uint32_t color = 0xb0bcd4) {
            PItem it; it.kind = 3; it.text = t; it.color = color; it.r = SDL_Rect{x0, y, w, 14}; rp.push_back(it); y += 15;
        };
        auto gap = [&](int g) { y += g; };
        auto button = [&](const std::string& t, bool active, std::function<void()> act, const std::string& tip, int x, int bw, bool enabled = true) {
            PItem it; it.kind = 2; it.text = t; it.active = active; it.act = act; it.tip = tip; it.enabled = enabled;
            it.r = SDL_Rect{x, y, bw, 26}; rp.push_back(it);
        };
        auto row2 = [&](const std::string& ta, bool aa, std::function<void()> fa, const std::string& tipA,
                        const std::string& tb, bool ab, std::function<void()> fb, const std::string& tipB) {
            int bw = (w - 4) / 2;
            button(ta, aa, fa, tipA, x0, bw);
            button(tb, ab, fb, tipB, x0 + bw + 4, bw);
            y += 30;
        };
        auto toggle = [&](const std::string& t, bool on, std::function<void()> act, const std::string& tip) {
            button(t, on, act, tip, x0, w);
            y += 30;
        };
        auto stepper = [&](const std::string& label, const std::string& value, std::function<void()> minus, std::function<void()> plus, const std::string& tip) {
            line(label, 0x8fa0c0);
            PItem m; m.kind = 2; m.text = "-"; m.act = minus; m.tip = tip; m.r = SDL_Rect{x0, y, 34, 26}; rp.push_back(m);
            PItem v; v.kind = 4; v.text = value; v.r = SDL_Rect{x0 + 38, y, w - 76, 26}; rp.push_back(v);
            PItem p; p.kind = 2; p.text = "+"; p.act = plus; p.tip = tip; p.r = SDL_Rect{x0 + w - 34, y, 34, 26}; rp.push_back(p);
            y += 32;
        };
        auto swatches = [&](const std::vector<uint8_t>& list, std::function<bool(uint8_t)> active, std::function<void(uint8_t)> click, int sz = 34) {
            int per = std::max(1, (w + 3) / (sz + 3));
            for (size_t i = 0; i < list.size(); ++i) {
                uint8_t m = list[i];
                PItem it; it.kind = 1; it.color = m == M_EMPTY ? 0xff5050 : MATS[m].color;
                it.text = m == M_EMPTY ? "ERASER" : MATS[m].name;
                it.active = active(m);
                it.act = [click, m] { click(m); };
                it.tip = std::string(it.text) + (m == M_EMPTY ? ": ERASES PARTICLES" : "");
                it.r = SDL_Rect{x0 + (int)(i % per) * (sz + 3), y + (int)(i / per) * (sz + 3), sz, sz};
                rp.push_back(it);
            }
            y += (int)((list.size() + per - 1) / per) * (sz + 3) + 2;
        };
        auto bodyInfo = [&](uint8_t m) {
            const MatInfo& mi = MATS[m];
            line(std::string(mi.name) + "  DENSITY " + fmt(mi.density), 0xe8eefc);
            std::string melt = mi.hiT < 1e8f ? "MELTS " + fmt(mi.hiT) + "C" : "DOES NOT MELT";
            line(melt + (mi.elec > 0.f ? "  CONDUCTS" : ""), 0x8fa0c0);
        };

        pruneSelection();
        const bool haveSel = primary >= 0 && primary < (int)phys.bodies.size() && phys.bodies[primary].alive;
        const Body* pb = haveSel ? &phys.bodies[primary] : nullptr;
        const bool creating = tool == T_BOX || tool == T_CIRCLE || tool == T_WHEEL || tool == T_ROCKET || tool == T_PIPE || tool == T_HOSE ||
                              tool == T_FAN || tool == T_EMITTER;
        const bool jointSel = tool == T_SELECT && jointValid(selJoint);
        const bool editing = tool == T_SELECT && haveSel;
        const bool fanCtx = tool == T_FAN || (editing && pb->fan.strength != 0.f);
        const bool emitCtx = tool == T_EMITTER || (editing && pb->src.on);

        if (tool == T_MAT) {
            header(mat == M_EMPTY ? "ERASER" : "PAINT PARTICLES");
            for (auto& g : PAINT_GROUPS) {
                if (std::string(g.name) == "DEVICES") header("DEVICES");
                else line(g.name, 0x8fa0c0);
                swatches(g.mats, [this](uint8_t m) { return mat == m; }, [this](uint8_t m) { selectMaterial(m); }, 30);
            }
            line(std::string("SELECTED: ") + (mat == M_EMPTY ? "ERASER" : MATS[mat].name), 0xe8eefc);
            gap(4);
            stepper("BRUSH SIZE", std::to_string(brush), [this] { brush = std::max(1, brush - 1); }, [this] { brush = std::min(24, brush + 1); }, "WHEEL ALSO CHANGES THE BRUSH SIZE");
            if (mat == M_BATT_POS || mat == M_BATT_NEG)
                button("BATTERY " + fmt(world.battV) + "V " + fmt(world.battA) + "A", formKind == FK_BATTERY, [this] { openBatteryForm(); },
                       "SET THE VOLTS AND AMPS STAMPED INTO BATTERY CELLS YOU PAINT NEXT", x0, w), y += 30;
        } else if (tool == T_CUT) {
            header("CUT TOOL");
            line("DRAG A SHAPE OVER BODIES:", 0xe8eefc);
            line("THE AREA UNDER IT IS CUT OUT", 0xb0bcd4);
            line("OF EVERY BODY IT TOUCHES.", 0xb0bcd4);
            gap(6);
            row2("BOX", !cutCircle, [this] { cutCircle = false; }, "CUT A RECTANGLE (DRAG CORNER TO CORNER)",
                 "CIRCLE", cutCircle, [this] { cutCircle = true; }, "CUT A CIRCLE (DRAG FROM THE CENTRE OUT)");
            gap(8);
            line("TO CUT ONE BODY WITH ANOTHER:", 0xe8eefc);
            line("SELECT BOTH; THE LAST CLICKED", 0xb0bcd4);
            line("(RED) IS CUT OUT OF THE REST.", 0xb0bcd4);
            line("THEN PRESS SUBTRACT.", 0xb0bcd4);
            gap(4);
            toggle("KEEP THE CUTTER", keepCutter, [this] { keepCutter = !keepCutter; },
                   "KEEP THE RED BODY AFTER SUBTRACT, FOR EXAMPLE TO SCALE IT INTO A PLUG");
            toggle("SUBTRACT SELECTION NOW", false, [this] { cutSelection(); }, "CUT THE LAST-CLICKED (RED) BODY OUT OF THE OTHER SELECTED BODIES");
        } else if (tool == T_BOND) {
            header("BOND: A TEMPORARY WELD");
            line("CLICK WHERE TWO BODIES OVERLAP.", 0xb0bcd4);
            line("IT LETS GO WHEN TOO HOT OR TOO", 0xb0bcd4);
            line("STRONGLY LOADED.", 0xb0bcd4);
            gap(4);
            for (int i = 0; i < 4; i += 2)
                row2(BOND_NAMES[i], bondType == i, [this, i] { bondType = i; bondT = BOND_TEMP[i]; bondG = BOND_G[i]; }, "BOND MATERIAL",
                     BOND_NAMES[i + 1], bondType == i + 1, [this, i] { bondType = i + 1; bondT = BOND_TEMP[i + 1]; bondG = BOND_G[i + 1]; }, "BOND MATERIAL");
            gap(4);
            stepper("MELTS AT (C)", fmt(bondT), [this] { bondT = std::max(-50.f, bondT - 5.f); }, [this] { bondT = std::min(5000.f, bondT + 5.f); }, "TEMPERATURE AT WHICH THE BOND GIVES WAY");
            stepper("HOLDS (X WEIGHT)", fmt(bondG), [this] { bondG = std::max(1.f, bondG - (bondG > 20.f ? 10.f : 1.f)); }, [this] { bondG = std::min(1000.f, bondG + (bondG >= 20.f ? 10.f : 1.f)); }, "HOW MANY TIMES THE WEIGHT IT CARRIES THE BOND CAN HOLD BEFORE IT GIVES WAY");
        } else if (jointSel) {
            const Joint& J = phys.joints[selJoint];
            const bool spring = J.type == J_DISTANCE && J.freq > 0.f;
            const char* nm = J.bondId >= 0 ? "BOND" : J.type == J_DISTANCE ? (spring ? "SPRING" : "ROD") : J.type == J_MOTOR ? "MOTOR" : J.type == J_SLIDER ? "SLIDER" : "PIN";
            header(nm);
            if (J.type == J_DISTANCE) {
                float cur = length(phys.jointAnchorB(J) - phys.jointAnchorA(J));
                line(std::string("LENGTH NOW ") + fmt(cur) + "  REST " + fmt(J.length), 0xe8eefc);
                gap(2);
                if (spring) {
                    stepper("STIFFNESS (BOUNCES PER SEC)", fmt(J.freq), [this] { editJoint([](Joint& k) { k.freq = std::max(0.2f, k.freq - (k.freq > 10.f ? 2.f : 0.5f)); }); },
                            [this] { editJoint([](Joint& k) { k.freq = std::min(60.f, k.freq + (k.freq >= 10.f ? 2.f : 0.5f)); }); }, "HIGHER = STIFFER. THE SPRING PULLS HARDER IN PROPORTION TO THE MASS IT CARRIES");
                    stepper("DAMPING (0 BOUNCY - 1 DEAD)", fmt(J.damping), [this] { editJoint([](Joint& k) { k.damping = std::max(0.f, k.damping - 0.05f); }); },
                            [this] { editJoint([](Joint& k) { k.damping = std::min(2.f, k.damping + 0.05f); }); }, "HOW QUICKLY THE BOUNCING DIES AWAY");
                }
                stepper("REST LENGTH (CELLS)", fmt(J.length), [this] { editJoint([](Joint& k) { k.length = std::max(1.f, k.length - 1.f); }); },
                        [this] { editJoint([](Joint& k) { k.length = std::min(400.f, k.length + 1.f); }); }, "THE LENGTH AT WHICH IT PULLS NEITHER WAY");
                button("SET REST = NOW", false, [this] { float cur = length(phys.jointAnchorB(phys.joints[selJoint]) - phys.jointAnchorA(phys.joints[selJoint])); editJoint([cur](Joint& k) { k.length = std::max(1.f, cur); }); },
                       "MAKE THE CURRENT LENGTH THE NATURAL ONE", x0, w); y += 30;
                row2("SPRING", spring, [this] { editJoint([this](Joint& k) { if (k.freq <= 0.f) k.freq = springFreq; }); }, "A SOFT LINK THAT STRETCHES",
                     "ROD", !spring, [this] { editJoint([](Joint& k) { k.freq = 0.f; }); }, "A RIGID LINK OF FIXED LENGTH");
            } else if (J.type == J_MOTOR) {
                stepper("SPEED (RADIANS PER SEC)", fmt(J.speed), [this] { editJoint([](Joint& k) { k.speed = k.speed - 0.5f; }); }, [this] { editJoint([](Joint& k) { k.speed = k.speed + 0.5f; }); },
                        "TARGET TURNING SPEED; NEGATIVE TURNS THE OTHER WAY");
                stepper("POWER (TORQUE)", fmt(J.power), [this] { editJoint([](Joint& k) { k.power = std::max(5.f, k.power - (k.power > 200.f ? 50.f : 10.f)); }); },
                        [this] { editJoint([](Joint& k) { k.power = std::min(5000.f, k.power + (k.power >= 200.f ? 50.f : 10.f)); }); }, "HOW HARD THE MOTOR CAN TURN");
                row2("ARROW KEYS", J.keyed, [this] { editJoint([](Joint& k) { k.keyed = true; }); }, "THE LEFT / RIGHT ARROW KEYS (A / D) DRIVE IT",
                     "ALWAYS ON", !J.keyed, [this] { editJoint([](Joint& k) { k.keyed = false; }); }, "SPINS ALL THE TIME");
            } else if (J.bondId >= 0) {
                stepper("MELTS AT (C)", fmt(J.breakT), [this] { editJoint([](Joint& k) { k.breakT = std::max(-50.f, k.breakT - 5.f); }); }, [this] { editJoint([](Joint& k) { k.breakT = std::min(5000.f, k.breakT + 5.f); }); },
                        "TEMPERATURE AT WHICH THE BOND GIVES WAY");
                stepper("HOLDS (X WEIGHT)", fmt(J.loadG), [this] { editJoint([](Joint& k) { k.loadG = std::max(1.f, k.loadG - (k.loadG > 20.f ? 10.f : 1.f)); }); },
                        [this] { editJoint([](Joint& k) { k.loadG = std::min(1000.f, k.loadG + (k.loadG >= 20.f ? 10.f : 1.f)); }); }, "HOW MANY TIMES THE WEIGHT IT CARRIES IT CAN HOLD");
            } else if (J.type == J_SLIDER) {
                line("KEEPS ONE BODY ON A LINE,", 0xb0bcd4);
                line("ROTATION LOCKED.", 0xb0bcd4);
            } else {
                line("A HINGE BETWEEN TWO BODIES", 0xb0bcd4);
                line("(OR A BODY AND THE WORLD).", 0xb0bcd4);
            }
            gap(6);
            button("DELETE THIS JOINT", false, [this] { removeSelectedJoint(); }, "REMOVE THE SELECTED JOINT (DEL)", x0, w); y += 30;
        } else if (tool == T_PIN || tool == T_MOTOR || tool == T_AUTOMOTOR || tool == T_ROD || tool == T_SPRING || tool == T_SLIDER || tool == T_GRAB || tool == T_DELETE) {
            header(TOOL_NAMES[tool]);
            // plain-language help for the tool, wrapped to the panel
            std::string h = TOOL_HINTS[tool];
            size_t pos = 0;
            while (pos < h.size()) {
                size_t len = std::min<size_t>(34, h.size() - pos);
                if (pos + len < h.size()) { size_t sp = h.rfind(' ', pos + len); if (sp != std::string::npos && sp > pos) len = sp - pos; }
                line(h.substr(pos, len), 0xb0bcd4);
                pos += len;
                while (pos < h.size() && h[pos] == ' ') ++pos;
            }
            if (tool == T_SPRING) {
                gap(8);
                header("NEW SPRINGS");
                stepper("STIFFNESS (BOUNCES PER SEC)", fmt(springFreq), [this] { springFreq = std::max(0.2f, springFreq - (springFreq > 10.f ? 2.f : 0.5f)); },
                        [this] { springFreq = std::min(60.f, springFreq + (springFreq >= 10.f ? 2.f : 0.5f)); }, "HIGHER = STIFFER (SELECT A SPRING LATER TO CHANGE IT)");
                stepper("DAMPING (0 BOUNCY - 1 DEAD)", fmt(springDamp), [this] { springDamp = std::max(0.f, springDamp - 0.05f); },
                        [this] { springDamp = std::min(2.f, springDamp + 0.05f); }, "HOW QUICKLY THE BOUNCING DIES AWAY");
            }
        } else {
            // body tools and selection: what the bodies are made of, and their machine settings
            if (editing) {
                header("SELECTED");
                std::string kind = pb->shape == SHAPE_BOX ? "BOX " + fmt(pb->half.x * 2) + " X " + fmt(pb->half.y * 2) : "CIRCLE R " + fmt(pb->radius);
                line(kind, 0xe8eefc);
                line(std::to_string(sel.size()) + (sel.size() == 1 ? " BODY" : " BODIES") + (pb->group >= 0 ? ", GROUPED" : "") + ", " + fmt(pb->temp) + "C", 0x8fa0c0);
                if (sel.size() >= 2) line("RED OUTLINE = LAST CLICKED", 0xff8c8c);
                gap(4);
            } else if (tool == T_SELECT) {
                header("SELECT");
                line("CLICK A BODY. CLICK AGAIN TO", 0xb0bcd4);
                line("REACH THE ONE UNDER IT.", 0xb0bcd4);
                line("DRAG A SELECTED BODY TO MOVE.", 0xb0bcd4);
                line("DRAG EMPTY SPACE TO SELECT", 0xb0bcd4);
                line("SEVERAL. CTRL+C / CTRL+V COPY.", 0xb0bcd4);
                line("CLICK A SPRING, ROD, MOTOR OR", 0xb0bcd4);
                line("PIN TO EDIT THAT JOINT.", 0xb0bcd4);
                gap(8);
            } else if (creating) {
                header(std::string("NEW ") + TOOL_NAMES[tool]);
            }
            if (fanCtx) {
                header("FAN");
                const Body* f = nullptr;
                std::vector<int> fans = selFans();
                if (!fans.empty()) f = &phys.bodies[fans[0]];
                float st = f ? f->fan.strength : lastFan;
                stepper("AIRFLOW STRENGTH", fmt(std::fabs(st)) + (st < 0 ? "  (REVERSED)" : ""), [this] { adjustFan(-10.f); }, [this] { adjustFan(10.f); },
                        "CELLS PER SECOND THE FAN MOVES GAS (+ AND - KEYS)");
                bool vac = f ? f->fan.vacuum != 0 : fanVacuumDefault;
                row2("BLOW", !vac, [this] { setFanVacuum(false); }, "DRAWS IN AMBIENT AIR AT THE BACK AND PUSHES IT OUT THE FRONT",
                     "VACUUM", vac, [this] { setFanVacuum(true); }, "ONLY PULLS THE GAS ON THE INTAKE SIDE, ACCELERATING IT THROUGH THE FAN");
                button("FLIP DIRECTION", false, [this] { flipFan(); }, "REVERSE THE AIRFLOW (BACKSLASH)", x0, w, !fans.empty()); y += 30;
                gap(4);
            }
            if (emitCtx) {
                header("EMITTER");
                std::vector<int> em = selEmitters();
                uint8_t curMat = em.empty() ? payload : phys.bodies[em[0]].src.mat;
                line("ENDLESSLY MAKES THIS MATERIAL:", 0xb0bcd4);
                swatches(std::vector<uint8_t>(EMIT_MATS, EMIT_MATS + sizeof EMIT_MATS), [curMat](uint8_t m) { return m == curMat; },
                         [this](uint8_t m) { setEmitMaterial(m); }, 30);
                line(std::string("EMITS ") + MATS[curMat].name, 0xe8eefc);
                float rate = em.empty() ? lastRate : phys.bodies[em[0]].src.rate;
                stepper("RATE (CELLS PER SECOND)", fmt(rate), [this] { adjustRate(-5.f); }, [this] { adjustRate(5.f); }, "HOW FAST IT PRODUCES THE MATERIAL");
                int face = em.empty() ? emitFace : phys.bodies[em[0]].src.face;
                line("OUTLET SIDE (THROWS ALONG IT)", 0x8fa0c0);
                row2("RIGHT >", face == 1, [this] { setSelectionFace(1); }, "THE OUTLET IS THE +X SIDE OF THE BLOCK (ROTATES WITH IT)",
                     "< LEFT", face == 2, [this] { setSelectionFace(2); }, "THE OUTLET IS THE -X SIDE");
                row2("DOWN v", face == 3, [this] { setSelectionFace(3); }, "THE OUTLET IS THE BOTTOM SIDE",
                     "UP ^", face == 4, [this] { setSelectionFace(4); }, "THE OUTLET IS THE TOP SIDE");
                toggle("ALL SIDES", face == 0, [this] { setSelectionFace(0); }, "SPILLS FROM EVERY SIDE (NO DIRECTION)");
                gap(4);
            }
            if (!emitCtx && (creating || editing)) {
                header("MATERIAL");
                uint8_t cur = editing ? pb->mat : bodyMat;
                swatches(std::vector<uint8_t>(BODY_MATS, BODY_MATS + sizeof BODY_MATS), [cur](uint8_t m) { return m == cur; },
                         [this](uint8_t m) { setBodyMaterial(m); }, 34);
                bodyInfo(cur);
                gap(4);
            }
            if (creating || editing) {
                bool fixed = editing ? pb->isStatic : anchored;
                toggle("FIXED IN PLACE", fixed, [this, fixed] { setFixed(!fixed); },
                       "A FIXED BODY STAYS WHERE IT IS: WALLS, CYLINDER BLOCKS, MOUNTS (T)");
            }
            if (tool == T_PIPE || tool == T_HOSE) {
                stepper("DIAMETER (CELLS)", fmt(pipeD), [this] { pipeD = std::max(3.f, pipeD - 1.f); pipeWall = std::min(pipeWall, pipeD * 0.5f); },
                        [this] { pipeD = std::min(60.f, pipeD + 1.f); }, "OUTER DIAMETER (MOUSE WHEEL ALSO CHANGES IT)");
                stepper("WALL (CELLS)", fmt(pipeWall), [this] { pipeWall = std::max(0.5f, pipeWall - 0.5f); },
                        [this] { pipeWall = std::min(pipeD * 0.5f, pipeWall + 0.5f); }, "WALL THICKNESS");
            }
        }

        // settings that apply to everything, pinned to the bottom of the panel
        int by = WIN_H - BOT_H - 118;
        y = std::max(y + 6, by);
        header("SETTINGS");
        stepper("GRID SNAP (CELLS)", SNAPS[snapIdx] ? std::to_string(SNAPS[snapIdx]) : "OFF", [this] { snapIdx = (snapIdx + 4) % 5; }, [this] { snapIdx = (snapIdx + 1) % 5; },
                "ROUND MOUSE-DRAWN SHAPES AND DRAGGED BODIES TO A GRID");
        stepper("SPARK PLUG PERIOD", SPARK_RATES[sparkIdx] ? std::to_string(SPARK_RATES[sparkIdx]) + " FRAMES" : "OFF",
                [this] { sparkIdx = (sparkIdx + 6) % 7; world.sparkPeriod = SPARK_RATES[sparkIdx]; },
                [this] { sparkIdx = (sparkIdx + 1) % 7; world.sparkPeriod = SPARK_RATES[sparkIdx]; }, "HOW OFTEN SPARK PLUGS FIRE (HOLD E TO FIRE ONCE)");
    }

    // The scene picker: a card over the simulation view.
    void buildModal() {
        modal.clear();
        if (!scenesOpen) return;
        struct SceneDef { const char* name; void (Game::*fn)(); const char* tip; };
        static const SceneDef scenes[] = {
            {"DEMO", &Game::buildDemo, "A LITTLE OF EVERYTHING"},
            {"STEAM ENGINE", &Game::buildSteamEngine, "BOILER, VALVE, PISTON AND FLYWHEEL"},
            {"GAS ENGINE", &Game::buildGasEngine, "SPARK-IGNITED GASOLINE VAPOUR ENGINE"},
            {"DIESEL ENGINE", &Game::buildDieselEngine, "GLOW-PLUG DIESEL ENGINE"},
            {"HYDRAULICS", &Game::buildHydraulics, "MASTER AND SLAVE CYLINDERS"},
            {"CONDUCTION", &Game::buildConduction, "HEAT FLOW THROUGH DIFFERENT MATERIALS"},
            {"FUELS", &Game::buildFuels, "THE LIQUID FUELS SIDE BY SIDE"},
            {"ELECTRIC", &Game::buildElectricTest, "BATTERIES, FILAMENTS, FUSES AND A SPARK GAP"},
            {"BONDS", &Game::buildBondTest, "WAX AND SHEAR-PIN BONDS"},
            {"PRIMER", &Game::buildPrimerTest, "A FIRING PIN STRIKES A PRIMER"},
            {"FANS", &Game::buildFanTest, "BLOWERS, A CLOSED DUCT AND A VACUUM FAN"},
            {"SHOTGUN", &Game::buildShotgun, "SPRING HAMMER, PRIMER, POWDER CHARGE, WAD AND SHOT"},
            {"JET ENGINE", &Game::buildJet, "A TURBOJET ON WHEELS: FAN, FUEL, SPARK PLUG, NOZZLE"},
            {"ROAD + FOCUS", &Game::buildRoadTest, "A FAN-DRIVEN CAR AND THE FOLLOWING CAMERA"},
        };
        const int n = 14, cols = 3, bw = 220, bh = 34, gapx = 10, gapy = 10;
        int cw = cols * bw + (cols + 1) * gapx, ch = 70 + ((n + cols - 1) / cols) * (bh + gapy) + 16;
        SDL_Rect card{(SIM_W - cw) / 2, (SIM_H - ch) / 2, cw, ch};
        PItem bg; bg.kind = 5; bg.r = card; modal.push_back(bg);
        PItem title; title.kind = 3; title.text = "LOAD A SCENE (REPLACES THE DRAWING - CTRL+Z BRINGS IT BACK)"; title.color = 0xffd27a; title.r = SDL_Rect{card.x + 14, card.y + 14, cw - 28, 14}; modal.push_back(title);
        for (int i = 0; i < n; ++i) {
            PItem b; b.kind = 2; b.text = scenes[i].name; b.tip = scenes[i].tip;
            auto fn = scenes[i].fn;
            b.act = [this, fn] { scenesOpen = false; (this->*fn)(); currentFile.clear(); };
            b.r = SDL_Rect{card.x + gapx + (i % cols) * (bw + gapx), card.y + 50 + (i / cols) * (bh + gapy), bw, bh};
            modal.push_back(b);
        }
    }

    // Flow-lay out the toolbar from the current labels, rebuild the dynamic panels and find what the pointer is over.
    void layoutButtons() {
        int x0 = 10, xr0 = WIN_W - 10;
        const int y0 = (TOP_H - 30) / 2;
        hoverBtn = -1; hoverItem = -1;
        for (size_t i = 0; i < buttons.size(); ++i) {
            Button& b = buttons[i];
            if (b.zone == 0) {
                int w = std::max(b.minW, font::textWidth(b.label(), 2) + 20);
                if (b.right) { xr0 -= w; b.r = SDL_Rect{xr0, y0, w, 30}; xr0 -= 4; }
                else { x0 += b.gap; b.r = SDL_Rect{x0, y0, w, 30}; x0 += w + 4; }
            }
            if (b.zone != 2 && mousePx >= b.r.x && mousePx < b.r.x + b.r.w && mousePy >= b.r.y && mousePy < b.r.y + b.r.h) hoverBtn = (int)i;
        }
        buildRightPanel();
        buildModal();
        for (size_t i = 0; i < rp.size(); ++i) {
            const PItem& it = rp[i];
            if ((it.kind == 1 || it.kind == 2) && mousePx >= it.r.x && mousePx < it.r.x + it.r.w && mousePy >= it.r.y && mousePy < it.r.y + it.r.h) hoverItem = (int)i;
        }
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
        pushUndo();
        clearBodies();
        world.clear();
        labels.clear();
        phys.gravity = Vec2(0, 260.f);
        phys.motorInput = 0;
        world.sourceAmt = 1.f;
        heatView = false;
        focusBody = -1;
        setCam(0);
        playing = false;
        paused = false;
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
        rect(190, 217, World::W - 1, 239, M_STONE);
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
        rect(23, 178, 69, 202, M_WATER);
        rect(30, 208, 52, 216, M_HEATER);               // a modest burner under part of the floor, so the water lasts
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
        warm(23, 178, 69, 202, 96.f);
        // feedwater: an outlet in the boiler wall at the working water level. When the water reaches it the outlet is blocked
        // and feeding stops, like a float valve, so the boiler never overfills into the steam line
        int feed = phys.addBox(Vec2(25.5f, 177.f), Vec2(1.5f, 1.5f), 0, M_STEEL, true);
        phys.bodies[feed].src = Emitter{true, M_WATER, 150.f, 0.f, 1};
        for (auto& b : phys.bodies) if (b.alive) b.temp = 105.f;
        rect(0, 230, World::W - 1, 239, M_CONCRETE);
        label(22, 140, "BOILER");
        label(78, 124, "VALVE CHEST + GATE");
        label(112, 170, "EXHAUST PORT + DRAIN");
        label(196, 118, "FLYWHEEL");
        label(24, 222, "BURNER");
        label(26, 186, "FEEDWATER (STOPS AT THE WORKING LEVEL)");
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
        // The supply wall: alternate cells of gasoline vapour (0.8) and air (1.0), a carburettor of sorts. The charge enters the chest
        // already mixed, about 2.5 air per unit of vapour, and the bore gets fuel beside air at every port opening, so the plug can
        // light it. A wall of vapour beside a wall of air only mixes by diffusion, and denser air simply crowds the vapour out.
        for (int y = 136; y <= 145; ++y) {
            const bool air = y & 1;
            world.sourceAmt = air ? 1.f : 0.8f;
            rect(76, y, 76, y, M_SOURCE, air ? M_AIR : M_VAPOR);
        }
        rect(90, 152, 90, 156, M_IGNITER);                     // spark plug set into the head wall
        sparkIdx = 5;
        world.sparkPeriod = SPARK_RATES[sparkIdx];
        rect(0, 230, World::W - 1, 239, M_CONCRETE);
        label(50, 124, "GASOLINE VAPOUR + AIR SUPPLY");
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
        for (int y = 136; y <= 145; ++y) {   // the supply wall: alternate cells of diesel vapour and air (diesel wants 3 air per unit), see the gas engine
            const bool air = y & 1;
            world.sourceAmt = air ? 1.2f : 0.8f;
            rect(76, y, 76, y, M_SOURCE, air ? M_AIR : M_VAPOR);
            if (!air) world.at(76, y).aux = M_DIESEL;
        }
        rect(90, 152, 90, 156, M_HEATER);                      // glow plug: diesel needs heat, not a spark, to light
        sparkIdx = 0;
        world.sparkPeriod = 0;
        rect(0, 230, World::W - 1, 239, M_CONCRETE);
        label(50, 124, "DIESEL VAPOUR + AIR SUPPLY");
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
        rect(0, 230, World::W - 1, 239, M_CONCRETE);
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
        rect(0, 230, World::W - 1, 239, M_CONCRETE);
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
        rect(0, 230, World::W - 1, 239, M_CONCRETE);
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
        rect(0, 200, World::W - 1, 203, M_WALL);
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
        rect(0, 200, World::W - 1, 203, M_WALL);
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
    bool shapeTool(Tool t) const { return t == T_BOX || t == T_CIRCLE || t == T_WHEEL || t == T_ROCKET || t == T_PIPE || t == T_HOSE || t == T_EMITTER || t == T_FAN; }
    Vec2 smouse() const { return (shapeTool(tool) || tool == T_CUT) ? snap(mouse) : mouse; }

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
    int selJoint = -1;                          // a selected joint (spring, rod, motor, pin, slider, bond), edited in the right panel
    float springFreq = 2.5f, springDamp = 0.35f; // what new springs are made with
    bool jointValid(int j) const { return j >= 0 && j < (int)phys.joints.size() && phys.joints[j].alive && phys.joints[j].group < 0 && phys.joints[j].type != J_MOUSE; }
    // the joint nearest to a point (within a few cells of its anchor, or of the line of a spring or rod), or -1
    int jointAt(Vec2 p) const {
        int best = -1; float bd = 3.2f;
        for (auto& j : phys.joints) {
            if (!j.alive || j.group >= 0 || j.type == J_MOUSE) continue;
            Vec2 a = phys.jointAnchorA(j);
            float d;
            if (j.type == J_DISTANCE) {
                Vec2 b = phys.jointAnchorB(j), ab = b - a;
                float t = lengthSq(ab) > 1e-6f ? std::clamp(dot(p - a, ab) / lengthSq(ab), 0.f, 1.f) : 0.f;
                d = length(p - (a + ab * t)) - 0.6f;
            } else d = length(p - a);
            if (d < bd) { bd = d; best = j.id; }
        }
        return best;
    }
    void selectJoint(int id) { clearSelection(); selJoint = id; }
    void removeSelectedJoint() {
        if (!jointValid(selJoint)) return;
        pushUndo();
        int bond = phys.joints[selJoint].bondId;
        if (bond >= 0) { for (auto& k : phys.joints) if (k.alive && k.bondId == bond) phys.removeJoint(k.id); }
        else phys.removeJoint(selJoint);
        selJoint = -1;
    }
    template <class F> void editJoint(F f) {   // change the selected joint (every pin of a bond together)
        if (!jointValid(selJoint)) return;
        pushUndo("jointedit");
        int bond = phys.joints[selJoint].bondId;
        if (bond >= 0) { for (auto& k : phys.joints) if (k.alive && k.bondId == bond) f(k); }
        else f(phys.joints[selJoint]);
    }
    void clearSelection() { sel.clear(); primary = -1; partMode = false; selJoint = -1; }
    void selectBody(int id, bool add, bool part) {
        if (id < 0) { if (!add) clearSelection(); return; }
        selJoint = -1;
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
        pushUndo();
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
        if (!gs.empty()) pushUndo();
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
            if (b.shape == SHAPE_BOX) { fields.push_back({"FAN", fmt(b.fan.strength)}); fields.push_back({"VACUUM", b.fan.vacuum ? "1" : "0"}); }
            fPayload = b.src.on ? b.src.mat : payload; fFace = b.src.face;
            return;
        }
        switch (tool) {
            case T_CIRCLE: case T_WHEEL:
                formKind = FK_CIRCLE; wheelForm = tool == T_WHEEL;
                fields = {{"X", fmt(m.x)}, {"Y", fmt(m.y)}, {"RADIUS", fmt(lastR)}};
                break;
            case T_BOND: openBondForm(); return;
            case T_FAN:
                formKind = FK_FAN;
                fields = {{"X", fmt(m.x)}, {"Y", fmt(m.y)}, {"THICKNESS", "6"}, {"DIAMETER", "24"}, {"ANGLE", "0"}, {"STRENGTH", fmt(lastFan)}, {"VACUUM", fanVacuumDefault ? "1" : "0"}};
                break;
            case T_EMITTER:
                formKind = FK_EMITTER; fPayload = payload; fFace = emitFace;
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
        if (formKind != FK_SAVE && formKind != FK_LOAD) pushUndo("form");
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
            case FK_SAVE: {
                std::string name = cleanName(fields[0].text);
                if (name.empty()) { formMsg = "TYPE A NAME"; return; }
                if (writeFile(name)) { currentFile = name; notify("SAVED saves/" + name + ".sbot" + (playing ? " (AS DRAWN)" : "")); closeForm(); }
                else formMsg = "SAVE FAILED";
                return;
            }
            case FK_LOAD: {
                std::string name = cleanName(fields[0].text);
                if (name.empty()) { formMsg = "TYPE OR PICK A NAME"; return; }
                if (readFile(name)) { currentFile = name; notify("LOADED " + name + ". PRESS PLAY TO RUN"); closeForm(); }
                else formMsg = "COULD NOT LOAD " + name;
                return;
            }
            case FK_BATTERY:
                world.battV = std::clamp(fv(0), 1.f, 70000.f);
                world.battA = std::clamp(fv(1), 0.001f, 400.f);
                formMsg = "BATTERY CELLS YOU PAINT NOW: " + fmt(world.battV) + "V " + fmt(world.battA) + "A (REPAINT TO UPDATE)";
                selectMaterial(M_BATT_POS);
                break;
            case FK_BOND:
                bondT = std::clamp(fv(0), -50.f, 5000.f); bondG = std::clamp(fv(1), 1.f, 1000.f);
                formMsg = "NEXT BOND: MELTS " + fmt(bondT) + "C / HOLDS " + fmt(bondG) + "X ITS WEIGHT";
                break;
            case FK_FAN: {
                lastFan = std::clamp(std::fabs(fv(5)), 1.f, 300.f);
                int id = phys.addBox(Vec2(fv(0), fv(1)), Vec2(std::max(1.f, fv(2)), std::max(2.f, fv(3))) * 0.5f, fv(4) * PI / 180.f, bodyMat, anchored);
                phys.bodies[id].fan.strength = fv(5) < 0 ? -lastFan : lastFan;
                phys.bodies[id].fan.vacuum = fv(6) > 0.5f ? 1 : 0;
                fanVacuumDefault = fv(6) > 0.5f;
                sel = {id}; primary = id; partMode = false;
                formMsg = "CREATED FAN " + fmt(phys.bodies[id].fan.strength) + " (ARROW = AIRFLOW)";
                break;
            }
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
                if (fields.size() > 6) setFan(fv(6));
                if (fields.size() > 7) phys.bodies[primary].fan.vacuum = fv(7) > 0.5f ? 1 : 0;
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
    bool fanModeVacuum() const {
        if (primary >= 0 && primary < (int)phys.bodies.size() && phys.bodies[primary].alive && phys.bodies[primary].fan.strength != 0.f)
            return phys.bodies[primary].fan.vacuum != 0;
        return fanVacuumDefault;
    }
    const char* fanModeLabel() const { return fanModeVacuum() ? "FAN: VACUUM" : "FAN: BLOW"; }
    void toggleFanMode() {
        bool next = !fanModeVacuum();
        fanVacuumDefault = next;
        if (primary >= 0 && primary < (int)phys.bodies.size() && phys.bodies[primary].alive && phys.bodies[primary].fan.strength != 0.f) {
            for (int id : sel) if (id >= 0 && id < (int)phys.bodies.size() && phys.bodies[id].alive && phys.bodies[id].fan.strength != 0.f) phys.bodies[id].fan.vacuum = next;
            if (formKind == FK_EDIT_BOX) openForm();
        }
        notify(next ? "VACUUM MODE: PULLS THE GAS ON THE INTAKE SIDE THROUGH THE FAN AND ACCELERATES IT" : "BLOW MODE: DRAWS AMBIENT AIR IN AND PUSHES IT OUT");
    }
    void setFan(float strength) {
        Body& b = phys.bodies[primary];
        b.fan.strength = std::clamp(strength, -300.f, 300.f);
        if (b.fan.strength != 0.f) lastFan = std::fabs(b.fan.strength);
    }
    static const char* faceName(int f) { static const char* n[] = {"ALL SIDES", "+X SIDE", "-X SIDE", "+Y SIDE", "-Y SIDE"}; return n[f % 5]; }

    void cycleBond() {
        bondType = (bondType + 1) % 4;
        bondT = BOND_TEMP[bondType]; bondG = BOND_G[bondType];
        notify(std::string("BOND: ") + BOND_NAMES[bondType] + " MELTS AT " + fmt(bondT) + "C, HOLDS " + fmt(bondG) + "X ITS WEIGHT");
    }
    void openBatteryForm() {
        formKind = FK_BATTERY; fActive = 0; fFresh = true;
        fields = {{"VOLTS", fmt(world.battV)}, {"AMPS", fmt(world.battA)}};
        formMsg = "";
    }
    void openBondForm() {
        formKind = FK_BOND; fActive = 0; fFresh = true;
        fields = {{"MELT C", fmt(bondT)}, {"HOLDS X WEIGHT", fmt(bondG)}};
        formMsg = "";
    }
    // ---------------------------------------------------------------- run mode, snapshots and files
    static constexpr uint32_t STATE_MAGIC = 0x31544253u;  // "SBT1"
    void captureState(std::vector<uint8_t>& out) {
        out.clear();
        Writer w{out};
        w.pod(STATE_MAGIC);
        w.pod((uint32_t)sizeof(Cell)); w.pod((uint32_t)sizeof(Body)); w.pod((uint32_t)sizeof(Joint));
        w.pod((uint32_t)World::W); w.pod((uint32_t)World::H);
        w.pod((uint32_t)M_COUNT);   // the material table: adding or reordering materials changes what every saved cell means
        world.save(w);
        phys.save(w);
        w.pod((uint32_t)labels.size());
        for (auto& l : labels) { w.pod(l.p); w.str(l.s); }
        w.pod(bodyMat);
    }
    bool inRestore = false;
    bool restoreState(const std::vector<uint8_t>& buf) {
        selJoint = -1;
        Reader r(buf);
        if (r.pod<uint32_t>() != STATE_MAGIC || r.pod<uint32_t>() != sizeof(Cell) || r.pod<uint32_t>() != sizeof(Body) ||
            r.pod<uint32_t>() != sizeof(Joint) || r.pod<uint32_t>() != (uint32_t)World::W || r.pod<uint32_t>() != (uint32_t)World::H ||
            r.pod<uint32_t>() != (uint32_t)M_COUNT || !r.ok)
            return false;
        // loading is all or nothing: keep a copy of the current state and put it back if the file turns out to be damaged
        std::vector<uint8_t> backup;
        if (!inRestore) captureState(backup);
        if (!world.load(r) || !phys.load(r)) {
            if (!backup.empty()) { inRestore = true; restoreState(backup); inRestore = false; }
            return false;
        }
        uint32_t n = r.pod<uint32_t>();
        std::vector<Label> ls;
        for (uint32_t i = 0; i < n && r.ok && i < 1000; ++i) { Label l; l.p = r.pod<Vec2>(); l.s = r.str(); ls.push_back(l); }
        uint8_t bm = r.pod<uint8_t>();
        if (!r.ok) return false;
        labels = ls;
        bodyMat = bm;
        dragBody = -1; grabJoint = -1;
        pruneSelection();
        return true;
    }
    void startPlay() {
        if (playing) return;
        captureState(snapshot);
        playing = true;
        paused = false;
    }
    void play() {
        if (!playing) { startPlay(); notify("RUNNING. STOP RESTORES THE DRAWING AS IT WAS"); }
        else paused = false;
    }
    void stopPlay() {
        if (!playing) { notify("ALREADY STOPPED (EDIT MODE)"); return; }
        if (!restoreState(snapshot)) { notify("COULD NOT RESTORE THE SNAPSHOT"); return; }
        playing = false;
        paused = false;
        notify("STOPPED: BACK TO THE DRAWN STATE");
    }
    void togglePause() {
        if (!playing) { notify("PRESS PLAY FIRST"); return; }
        paused = !paused;
    }
    void stepFrame() {
        if (!playing) { startPlay(); paused = true; }
        stepOnce = true;
    }
    void newFile() {
        if (newArmed > 0) {
            resetWorld();
            phys.stampBodies();
            clearSelection();
            focusBody = -1;
            setCam(0);
            currentFile.clear();
            newArmed = 0;
            notify("NEW EMPTY FILE");
        } else {
            newArmed = 240;
            notify("PRESS NEW AGAIN TO CLEAR EVERYTHING (SAVE FIRST!)");
        }
    }
    static std::string saveDir() { return "saves"; }
    static std::string cleanName(const std::string& in) {
        std::string o;
        for (char c : in) if (std::isalnum((unsigned char)c) || c == '-' || c == '_') o += (char)std::tolower((unsigned char)c);
        return o.substr(0, 24);
    }
    void refreshFiles() {
        fileList.clear();
        std::error_code ec;
        std::vector<std::pair<std::filesystem::file_time_type, std::string>> found;
        for (auto& e : std::filesystem::directory_iterator(saveDir(), ec))
            if (e.is_regular_file(ec) && e.path().extension() == ".sbot") found.push_back({e.last_write_time(ec), e.path().stem().string()});
        std::sort(found.begin(), found.end(), [](auto& a, auto& b) { return a.first > b.first; });
        for (auto& f : found) fileList.push_back(f.second);
    }
    bool writeFile(const std::string& name) {
        std::error_code ec;
        std::filesystem::create_directories(saveDir(), ec);
        std::vector<uint8_t> buf;
        if (playing) buf = snapshot;   // a running machine is saved as it was drawn
        else captureState(buf);
        std::ofstream f(saveDir() + "/" + name + ".sbot", std::ios::binary);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(buf.data()), (std::streamsize)buf.size());
        return (bool)f;
    }
    bool readFile(const std::string& name) {
        std::ifstream f(saveDir() + "/" + name + ".sbot", std::ios::binary);
        if (!f) return false;
        std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        playing = false; paused = false;
        pushUndo();
        if (!restoreState(buf)) return false;
        clearSelection();
        focusBody = -1;
        setCam(0);
        phys.stampBodies();
        return true;
    }
    void saveQuick() {
        if (currentFile.empty()) { openFileForm(true); return; }
        notify(writeFile(currentFile) ? "SAVED " + currentFile + (playing ? " (AS DRAWN, NOT MID-RUN)" : "") : "SAVE FAILED");
    }
    void openFileForm(bool save) {
        refreshFiles();
        formKind = save ? FK_SAVE : FK_LOAD;
        fActive = 0; fFresh = true; formMsg.clear();
        fileIdx = -1;
        fields = {{"FILE", save ? currentFile : (fileList.empty() ? std::string() : fileList[0])}};
        if (!save && !fileList.empty()) fileIdx = 0;
    }
    // text entry for the file name; Up/Down pick from the existing files
    bool fileFormKey(SDL_Keycode k, Uint16 mod) {
        std::string& t = fields[0].text;
        auto typeChar = [&](char c) {
            if (fFresh) { t.clear(); fFresh = false; }
            if (t.size() < 24) t += c;
            fileIdx = -1;
        };
        if (k >= SDLK_a && k <= SDLK_z) typeChar((char)('a' + (k - SDLK_a)));
        else if (k >= SDLK_0 && k <= SDLK_9) typeChar((char)('0' + (k - SDLK_0)));
        else if (k == SDLK_MINUS) typeChar((mod & KMOD_SHIFT) ? '_' : '-');
        else if (k == SDLK_BACKSPACE) { if (!t.empty()) t.pop_back(); fFresh = false; fileIdx = -1; }
        else if ((k == SDLK_DOWN || k == SDLK_UP || k == SDLK_TAB) && !fileList.empty()) {
            int n = (int)fileList.size();
            fileIdx = fileIdx < 0 ? 0 : (fileIdx + (k == SDLK_UP ? n - 1 : 1)) % n;
            t = fileList[fileIdx]; fFresh = true;
        }
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) applyForm();
        else if (k == SDLK_ESCAPE) closeForm();
        return true;
    }

    void openScaleForm() {
        pruneSelection();
        if (sel.empty()) { notify("SELECT BODIES TO SCALE FIRST"); return; }
        formKind = FK_SCALE; fActive = 0; fFresh = true;
        fields = {{"PERCENT", fmt(lastScale)}};
        formMsg = "";
    }

    // Boolean subtract with bodies you have selected: the primary (last-clicked, outlined in red) is cut out of the others.
    void cutSelection() {
        pruneSelection();
        if (sel.size() < 2 || primary < 0) {
            notify("SELECT 2+ BODIES: THE LAST ONE CLICKED (RED) IS CUT OUT OF THE OTHERS. OR USE THE CUT TOOL: DRAG A BOX/CIRCLE OVER THE BODY");
            return;
        }
        std::vector<int> cutters;
        const Body& pb = phys.bodies[primary];
        if (pb.group >= 0 && !partMode) cutters = phys.groupMembers(pb.group); else cutters = {primary};
        pushUndo();
        int cutN = 0, pieces = 0;
        for (int t : std::vector<int>(sel)) {
            if (std::find(cutters.begin(), cutters.end(), t) != cutters.end()) continue;
            int r = phys.cutBody(t, cutters);
            if (r < 0) continue;
            ++cutN; pieces += r;
        }
        if (cutN && !keepCutter) for (int c : cutters) phys.removeBody(c);
        clearSelection();
        phys.stampBodies();
        if (cutN) notify("CUT " + std::to_string(cutN) + " BODIES INTO " + std::to_string(pieces) + " PIECES" + (keepCutter ? " (CUTTER KEPT)" : "") + ". CTRL+Z UNDOES");
        else notify("NOTHING CUT: THE RED BODY MUST OVERLAP THE OTHERS (WHEELS, ROCKETS, FANS AND EMITTERS CAN'T BE CUT)");
        if (formKind >= FK_EDIT_BOX) closeForm();
    }
    int autoSegs(Vec2 a, Vec2 b) const { return std::clamp((int)std::ceil(length(b - a) / std::max(4.f, pipeD * 1.2f)), 2, 60); }

    // returns true if the key was consumed by the form
    bool formKey(SDL_Keycode k, Uint16 mod) {
        if (formKind == FK_NONE) return false;
        if (formKind == FK_SAVE || formKind == FK_LOAD) return fileFormKey(k, mod);
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
    SDL_Rect formRect() const { return SDL_Rect{8, 8, 300, 56 + (int)fields.size() * 20 + (formKind >= FK_EDIT_BOX || formKind == FK_EMITTER ? 52 : 20) + ((formKind == FK_SAVE || formKind == FK_LOAD) ? 14 + 12 * (int)std::min<size_t>(fileList.size(), 6) : 0)}; }
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
        rect(0, 200, World::W - 1, 203, M_WALL);
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
        rect(0, 200, World::W - 1, 203, M_WALL);
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
        rect(0, 200, World::W - 1, 203, M_WALL);
        // A: 12 V / 20 A battery heats a tungsten filament, which lights gasoline vapour
        world.battV = 12.f; world.battA = 20.f;
        rect(20, 60, 22, 62, M_BATT_POS); rect(20, 80, 22, 82, M_BATT_NEG);
        rect(23, 61, 59, 61, M_COPPER); rect(60, 61, 79, 61, M_TUNGSTEN); rect(80, 61, 100, 61, M_COPPER);
        rect(100, 61, 100, 81, M_COPPER); rect(23, 81, 100, 81, M_COPPER);
        for (int y = 56; y <= 60; ++y)   // alternate cells of air: a solid block of vapour has no oxygen inside and would only burn at its edges
            for (int x = 60 + (y & 1); x <= 80; x += 2) if (world.at(x, y).t == M_EMPTY) { world.setCell(x, y, M_AIR); world.at(x, y).amt = 1.5f; }
        gasRect(60, 56, 80, 60, M_VAPOR, 0.6f);
        label(24, 48, "12V 20A BATTERY, TUNGSTEN FILAMENT IN FUEL VAPOUR");
        // B: 20 kV / 50 mA through a 2-cell air gap (a spark plug) in vapour
        world.battV = 20000.f; world.battA = 0.05f;
        rect(150, 60, 152, 62, M_BATT_POS); rect(150, 80, 152, 82, M_BATT_NEG);
        rect(153, 61, 190, 61, M_COPPER); rect(190, 61, 190, 70, M_COPPER); rect(190, 70, 192, 70, M_COPPER);
        rect(195, 70, 197, 70, M_COPPER); rect(197, 70, 197, 81, M_COPPER); rect(153, 81, 197, 81, M_COPPER);
        for (int y = 66; y <= 70; ++y)
            for (int x = 191 + (y & 1); x <= 196; x += 2) if (world.at(x, y).t == M_EMPTY) { world.setCell(x, y, M_AIR); world.at(x, y).amt = 1.5f; }
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
        rect(0, 200, World::W - 1, 203, M_WALL);
        // 1: a steel block glued under a ledge with paraffin; a heater beside it warms it until the wax lets go
        phys.addBox(Vec2(50, 90), Vec2(30, 3), 0, M_STEEL, true);
        int w1 = phys.addBox(Vec2(50, 100), Vec2(8, 8), 0, M_STEEL, false);
        phys.addBond(Vec2(50, 93), w1, 0, BOND_TEMP[0], 0.f, BOND_G[0]);
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
        rect(0, 200, World::W - 1, 203, M_WALL);
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
    void buildFanTest() {
        resetWorld();
        rect(0, 200, World::W - 1, 203, M_WALL);
        // 1: a wind tunnel: the fan blows a smoke stream and keeps light blocks aloft
        int f1 = phys.addBox(Vec2(30, 100), Vec2(3, 14), 0, M_STEEL, true);
        phys.bodies[f1].fan.strength = 90.f;
        phys.addBox(Vec2(95, 104), Vec2(5, 5), 0, M_WOOD, false);
        phys.addBox(Vec2(130, 98), Vec2(4, 4), 0, M_WOOD, false);
        int smoke = phys.addBox(Vec2(14, 100), Vec2(2, 2), 0, M_STEEL, true);
        phys.bodies[smoke].src = Emitter{true, M_SMOKE, 60.f, 0.f, 0};
        label(10, 74, "FAN BLOWS SMOKE AND LIFTS LIGHT BLOCKS");
        // 2: a closed duct: pressure builds ahead of the fan up to its stall pressure (try the PRESSURE view)
        rect(190, 120, 290, 122, M_WALL); rect(190, 142, 290, 144, M_WALL);
        rect(190, 123, 192, 141, M_WALL); rect(288, 123, 290, 141, M_WALL);
        int f2 = phys.addBox(Vec2(240, 132), Vec2(2, 8.5f), 0, M_STEEL, true);
        phys.bodies[f2].fan.strength = 80.f;
        label(196, 108, "CLOSED DUCT: PRESSURE RISES AHEAD, FALLS BEHIND");
        // 3: a free fan recoils: a car with a fan welded on its tail blows backwards and drives itself
        int chassis = phys.addBox(Vec2(80, 190), Vec2(24, 5), 0, M_ALUMINUM, false);
        for (int sx = -1; sx <= 1; sx += 2) {   // free-rolling wheels (no motor, so no brake)
            int wh = phys.addCircle(Vec2(80.f + 16.f * sx, 196), 4.f, M_RUBBER, false, true);
            phys.addPin(Vec2(80.f + 16.f * sx, 196), chassis, wh, false, false);
        }
        int tail = phys.addBox(Vec2(54, 183), Vec2(2, 7), 0, M_ALUMINUM, false);
        phys.bodies[tail].fan.strength = -250.f;
        phys.groupBodies({chassis, tail});
        label(30, 160, "A FAN ON A CAR RECOILS: IT DRIVES ITSELF");
        // 4: a vacuum fan empties a chamber of smoke through its throat, accelerating the gas
        rect(320, 60, 366, 62, M_WALL); rect(320, 98, 366, 100, M_WALL); rect(320, 60, 322, 100, M_WALL);
        gasRect(323, 63, 365, 97, M_SMOKE, 1.f);
        int f4 = phys.addBox(Vec2(367, 80), Vec2(2, 17.5f), 0, M_STEEL, true);
        phys.bodies[f4].fan.strength = 90.f; phys.bodies[f4].fan.vacuum = 1;
        label(312, 48, "VACUUM FAN DRAWS THE CHAMBER EMPTY");
        phys.stampBodies();
    }
    // Flying turbojet: inlet at the front (right), nozzle at the back (left). The casing is a welded body group on wheels.
    struct JetCfg { float fan = 100.f, fuel = 150.f, fuelU = 14.f, plugU = 18.f, len = 30.f, nozIn = 3.f, nozLen = 10.f, half = 11.5f; int spark = 1; bool space = false; };
    static JetCfg& jet() { static JetCfg c; return c; }
    int buildJetCar() {
        resetWorld();
        rect(0, 200, World::W - 1, 203, M_WALL);
        const JetCfg& C = jet();
        const float cy = C.space ? 100.f : 170.f, xc = 260.f;      // x = xc - u, where u runs from the inlet to the nozzle
        const float wallT = 1.5f, H = C.half + wallT;
        std::vector<int> parts;
        auto box = [&](float u, float y, float hu, float hy, uint8_t m) { int b = phys.addBox(Vec2(xc - u, y), Vec2(hu, hy), 0, m, false); parts.push_back(b); return b; };
        box(C.len * 0.5f, cy - H, C.len * 0.5f, wallT, M_ALUMINUM);
        box(C.len * 0.5f, cy + H, C.len * 0.5f, wallT, M_ALUMINUM);
        for (int sgn = -1; sgn <= 1; sgn += 2) {   // two angled plates make the converging nozzle
            Vec2 p0(xc - C.len, cy + sgn * C.half), p1(xc - C.len - C.nozLen, cy + sgn * (C.half - C.nozIn));
            Vec2 d = p1 - p0;
            int b = phys.addBox((p0 + p1) * 0.5f, Vec2(length(d) * 0.5f + 1.f, wallT), std::atan2(d.y, d.x), M_ALUMINUM, false);
            parts.push_back(b);
        }
        int comp = box(8, cy, 1.5f, C.half, M_ALUMINUM);
        phys.bodies[comp].fan.strength = -C.fan;
        int fuel = box(C.fuelU, cy - C.half + 1.6f, 1.5f, 1.5f, M_ALUMINUM);   // an injector on the wall, out of the airstream
        phys.bodies[fuel].src = Emitter{true, M_PROPANE, C.fuel, 0.f, 0};
        box(C.plugU, cy - C.half + 1.f, 1.5f, 1.f, M_IGNITER);
        sparkIdx = 5; world.sparkPeriod = C.spark ? SPARK_RATES[sparkIdx] : 0;
        int hull = box(C.len * 0.5f, cy + H + 4.f, C.len * 0.5f + 1.f, 1.5f, M_ALUMINUM);
        if (!C.space)
            for (int sx = -1; sx <= 1; sx += 2) {
                int wh = phys.addCircle(Vec2(xc - C.len * 0.5f + 40.f * sx, cy + H + 9.f), 4.f, M_RUBBER, false, true);
                phys.addPin(Vec2(xc - C.len * 0.5f + 40.f * sx, cy + H + 9.f), hull, wh, false, false);
            }
        phys.groupBodies(parts);
        label(xc - 120, cy - 40, "TURBOJET: FAN COMPRESSES AIR, FUEL + SPARK PLUG BURN IT, NOZZLE EXHAUSTS LEFT");
        selectBody(hull, false, false);
        focusBody = hull;
        setCam(100);
        phys.stampBodies();
        return hull;
    }
    // A shotgun: a spring-driven hammer on a slider strikes a primer, the flash lights the powder in the sealed chamber, the
    // gas pressure drives a wad and a load of shot down the barrel.
    struct Shotgun { int hammer = -1, wad = -1; std::vector<int> shot; };
    Shotgun lastGun;
    void buildShotgun() {
        resetWorld();
        rect(0, 230, World::W - 1, 239, M_CONCRETE);
        const int bx0 = 120, by0 = 108;   // breech block top-left
        const int ym = by0 + 12;           // bore centre line
        rect(bx0 - 6, by0 - 6, bx0 + 150, by0 - 1, M_STEEL);        // barrel top wall
        rect(bx0 - 6, by0 + 25, bx0 + 150, by0 + 30, M_STEEL);      // barrel bottom wall
        rect(bx0 - 6, by0 - 6, bx0 + 7, by0 + 30, M_STEEL);         // breech block
        rect(bx0 - 6, ym - 1, bx0 + 7, ym + 1, M_EMPTY);            // the firing-pin hole through it
        // primer seated in the breech: struck on its left end, flashes out of its right end into the chamber
        phys.addBox(Vec2(bx0 + 4, ym + 0.5f), Vec2(4.f, 1.5f), 0, M_PRIMER, true);
        // powder charge
        rect(bx0 + 8, by0, bx0 + 23, by0 + 24, M_GUNPOWDER);
        // wad and shot
        const float wx = bx0 + 27.f;
        lastGun.wad = phys.addBox(Vec2(wx + 3.f, ym + 0.5f), Vec2(5.5f, 11.5f), 0, M_ALUMINUM, false);   // long enough that it can't tip over in the bore
        lastGun.shot.clear();
        for (int i = 0; i < 15; ++i) lastGun.shot.push_back(phys.addCircle(Vec2(wx + 11.f + 4.6f * (i / 5), ym - 8.8f + 4.4f * (i % 5) + 0.2f), 2.15f, M_LEAD, false, false));   // shot filling the bore, so the wad is pushed evenly
        // hammer on a slider, driven by a compressed spring from a post behind it
        int post = phys.addBox(Vec2(bx0 - 56, ym), Vec2(3.f, 8.f), 0, M_STEEL, true);
        lastGun.hammer = phys.addBox(Vec2(bx0 - 28, ym + 0.5f), Vec2(8.f, 1.2f), 0, M_STEEL, false);
        phys.addSlider(lastGun.hammer, Vec2(1, 0));
        int sp = phys.addDistance(lastGun.hammer, Vec2(bx0 - 28, ym), post, Vec2(bx0 - 53, ym), 3.f);
        if (sp >= 0) phys.joints[sp].length = 70.f;   // longer than it is: a compressed spring pushing the hammer at the primer
        // targets down range
        for (int i = 0; i < 4; ++i) phys.addBox(Vec2(bx0 + 215.f, 220.f - 11.f * i), Vec2(5.f, 5.5f), 0, M_WOOD, false);
        phys.addBox(Vec2(bx0 + 232.f, 215.f), Vec2(4.f, 12.f), 0, M_BRICK, false);
        label(bx0 - 64, by0 - 18, "SPRING-DRIVEN HAMMER (ON A SLIDER) STRIKES THE PRIMER");
        label(bx0 + 10, by0 - 30, "POWDER CHARGE");
        label(bx0 + 46, by0 - 18, "WAD + SHOT");
        label(bx0 + 150, by0 - 10, "BARREL");
        phys.stampBodies();
    }
    void buildJet() { buildJetCar(); }
    void buildRoadTest() {
        resetWorld();
        rect(0, 200, World::W - 1, 203, M_WALL);
        for (int x0 = 260; x0 < World::W - 100; x0 += 170)   // rolling bumps along the way
            for (int x = 0; x < 40; ++x) rect(x0 + x, 200 - (int)(6.f * std::sin(3.14159f * x / 40.f)), x0 + x, 200, M_STONE);
        // car A: a fan on its tail blows backwards and drives it
        int chassis = phys.addBox(Vec2(60, 190), Vec2(24, 5), 0, M_ALUMINUM, false);
        for (int sx = -1; sx <= 1; sx += 2) {
            int wh = phys.addCircle(Vec2(60.f + 16.f * sx, 196), 4.f, M_RUBBER, false, true);
            phys.addPin(Vec2(60.f + 16.f * sx, 196), chassis, wh, false, false);
        }
        int tail = phys.addBox(Vec2(34, 183), Vec2(2, 7), 0, M_ALUMINUM, false);
        phys.bodies[tail].fan.strength = -250.f;
        phys.groupBodies({chassis, tail});
        // car B: motor-driven wheels that spin on their own, a little behind
        int b = phys.addBox(Vec2(20, 190), Vec2(18, 4), 0, M_STEEL, false);
        for (int sx = -1; sx <= 1; sx += 2) {
            int wh = phys.addCircle(Vec2(20.f + 12.f * sx, 195), 4.f, M_RUBBER, false, true);
            phys.addPin(Vec2(20.f + 12.f * sx, 195), b, wh, true, false);
        }
        label(8, 160, "SELECT THE FAN CAR, PRESS FOCUS (Z), THEN PLAY: THE CAMERA FOLLOWS IT DOWN THE ROAD");
        selectBody(chassis, false, false);
        focusBody = chassis;
        setCam(0);
        phys.stampBodies();
        notify("FOCUS IS ON THE FAN CAR: PRESS PLAY. Z TURNS FOCUS OFF, MIDDLE-DRAG PANS");
    }
    void buildEmitterTest() {
        resetWorld();
        rect(0, 200, World::W - 1, 203, M_WALL);
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
    // window pixel -> world cell
    Vec2 toWorld(int mx, int my) const { return Vec2((float)(mx - SIM_X) / sc() + camXf, (float)(my - SIM_Y) / sc() + camYf); }
    static bool inSimPx(int x, int y) { return x >= SIM_X && x < SIM_X + SIM_W && y >= SIM_Y && y < SIM_Y + SIM_H; }

    // ---------------------------------------------------------------- undo / redo
    static std::vector<uint8_t> packState(const std::vector<uint8_t>& in) {
        // run-length coding over 16-byte records: a mostly empty world collapses to almost nothing
        std::vector<uint8_t> out;
        Writer w{out};
        w.pod((uint32_t)in.size());
        size_t n = in.size(), i = 0;
        while (i < n) {
            size_t rec = std::min<size_t>(16, n - i);
            uint32_t run = 1;
            if (rec == 16)
                while (i + 16 * (size_t)(run + 1) <= n && run < 0xffffff && std::memcmp(&in[i], &in[i + 16 * (size_t)run], 16) == 0) ++run;
            w.pod(run);
            w.pod((uint8_t)rec);
            out.insert(out.end(), in.begin() + (long)i, in.begin() + (long)(i + rec));
            i += rec * run;
        }
        return out;
    }
    static std::vector<uint8_t> unpackState(const std::vector<uint8_t>& in) {
        Reader r(in);
        uint32_t n = r.pod<uint32_t>();
        std::vector<uint8_t> out;
        out.reserve(n);
        while (r.ok && out.size() < n) {
            uint32_t run = r.pod<uint32_t>();
            uint8_t rec = r.pod<uint8_t>();
            if (!r.ok || rec > 16 || (size_t)(r.end - r.p) < rec) break;
            for (uint32_t k = 0; k < run; ++k) out.insert(out.end(), r.p, r.p + rec);
            r.p += rec;
        }
        return out;
    }
    // Remember the drawing as it is NOW, just before an edit changes it. A burst of the same kind of edit (dragging a
    // stepper, painting strokes) shares one undo step.
    void pushUndo(const char* key = nullptr) {
        if (playing) return;
        Uint32 now = SDL_GetTicks();
        if (key && lastUndoKey == key && now - lastUndoTick < 1500) { lastUndoTick = now; return; }
        std::vector<uint8_t> raw;
        captureState(raw);
        undoStack.push_back(packState(raw));
        undoBytes += undoStack.back().size();
        while (!undoStack.empty() && (undoStack.size() > 60 || undoBytes > (size_t)120 << 20)) {
            undoBytes -= undoStack.front().size();
            undoStack.erase(undoStack.begin());
        }
        redoStack.clear();
        lastUndoKey = key ? key : "";
        lastUndoTick = now;
    }
    void undo() {
        if (playing) { notify("UNDO WORKS IN EDIT MODE - PRESS STOP FIRST (STOP RESTORES THE DRAWING)"); return; }
        if (undoStack.empty()) { notify("NOTHING TO UNDO"); return; }
        std::vector<uint8_t> raw;
        captureState(raw);
        redoStack.push_back(packState(raw));
        std::vector<uint8_t> prev = unpackState(undoStack.back());
        undoBytes -= undoStack.back().size();
        undoStack.pop_back();
        if (!restoreState(prev)) { notify("UNDO FAILED"); return; }
        lastUndoKey.clear();
        notify("UNDO (CTRL+Y REDOES)");
    }
    void redo() {
        if (playing) { notify("REDO WORKS IN EDIT MODE"); return; }
        if (redoStack.empty()) { notify("NOTHING TO REDO"); return; }
        std::vector<uint8_t> raw;
        captureState(raw);
        undoStack.push_back(packState(raw));
        undoBytes += undoStack.back().size();
        std::vector<uint8_t> next = unpackState(redoStack.back());
        redoStack.pop_back();
        if (!restoreState(next)) { notify("REDO FAILED"); return; }
        lastUndoKey.clear();
        notify("REDO");
    }

    // ---------------------------------------------------------------- copy / paste
    void copySelection() {
        pruneSelection();
        if (sel.empty()) { notify("SELECT BODIES FIRST, THEN CTRL+C"); return; }
        std::vector<int> ids = sel;
        std::sort(ids.begin(), ids.end());
        std::vector<int> index(phys.bodies.size(), -1);
        clip = Clip{};
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (int id : ids) {
            index[id] = (int)clip.bodies.size();
            clip.bodies.push_back(phys.bodies[id]);
            const Body& b = phys.bodies[id];
            x0 = std::min(x0, b.pos.x - b.bound); x1 = std::max(x1, b.pos.x + b.bound);
            y0 = std::min(y0, b.pos.y - b.bound); y1 = std::max(y1, b.pos.y + b.bound);
        }
        clip.center = Vec2((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
        for (auto& j : phys.joints) {
            if (!j.alive || j.group >= 0 || j.type == J_MOUSE || j.a < 0 || j.b < 0 || index[j.a] < 0 || index[j.b] < 0) continue;
            Joint c = j;
            c.a = index[j.a]; c.b = index[j.b];
            clip.joints.push_back(c);
        }
        notify("COPIED " + std::to_string(clip.bodies.size()) + " BODIES (CTRL+V PASTES AT THE CURSOR)");
    }
    void pasteClipboard() {
        if (clip.bodies.empty()) { notify("NOTHING COPIED YET (SELECT, THEN CTRL+C)"); return; }
        pushUndo();
        Vec2 target = inSim ? snap(mouse) : Vec2(camXf + viewW() * 0.5f, camYf + viewH() * 0.4f);
        Vec2 delta = target - clip.center;
        std::vector<int> ids;
        std::vector<std::pair<int, int>> groupMap, bondMap;
        auto mapped = [](std::vector<std::pair<int, int>>& m, int old, int fresh) {
            for (auto& p : m) if (p.first == old) return p.second;
            m.push_back({old, fresh});
            return fresh;
        };
        for (const Body& src : clip.bodies) {
            Body b = src;
            b.pos += delta;
            int id = phys.addBodyCopy(b);
            if (src.group >= 0) {
                int g = -1;
                for (auto& p : groupMap) if (p.first == src.group) g = p.second;
                if (g < 0) { g = phys.newGroupId(); groupMap.push_back({src.group, g}); }
                phys.bodies[id].group = g;
            }
            ids.push_back(id);
        }
        for (const Joint& src : clip.joints) {
            Joint j = src;
            j.a = ids[src.a]; j.b = ids[src.b];
            if (j.bondId >= 0) {
                int fresh = -1;
                for (auto& p : bondMap) if (p.first == j.bondId) fresh = p.second;
                if (fresh < 0) { fresh = phys.newBondId(); bondMap.push_back({j.bondId, fresh}); }
                j.bondId = fresh;
            }
            phys.addJointCopy(j);
        }
        for (auto& p : groupMap) phys.rebuildGroup(p.second);
        (void)mapped;
        sel = ids; primary = ids[0]; partMode = false;
        phys.stampBodies();
        notify("PASTED " + std::to_string(ids.size()) + " BODIES - DRAG THEM INTO PLACE");
    }
    void selectAll() {
        sel.clear();
        for (auto& b : phys.bodies) if (b.alive) sel.push_back(b.id);
        primary = sel.empty() ? -1 : sel[0];
        partMode = false;
        notify("SELECTED " + std::to_string(sel.size()) + " BODIES");
    }

    // ---------------------------------------------------------------- picking, moving, cutting
    // Clicking the same spot again steps down through the bodies stacked under the cursor.
    int pickCycling(Vec2 p) {
        std::vector<int> ids = phys.bodiesAt(p);   // ascending: the last one is on top
        if (ids.empty()) return -1;
        Uint32 now = SDL_GetTicks();
        bool same = length(p - lastClickPos) < 2.f && ids.size() == lastClickCount && now - lastClickTick < 2500;
        cycleIdx = same ? (cycleIdx + (int)ids.size() - 1) % (int)ids.size() : (int)ids.size() - 1;
        lastClickPos = p; lastClickTick = now; lastClickCount = ids.size();
        if (ids.size() > 1) notify("BODY " + std::to_string((int)ids.size() - cycleIdx) + " OF " + std::to_string(ids.size()) + " UNDER THE CURSOR - CLICK AGAIN FOR THE NEXT");
        return ids[cycleIdx];
    }
    int topBodyAt(Vec2 p, bool preferSelected) {
        std::vector<int> ids = phys.bodiesAt(p);
        if (ids.empty()) return -1;
        if (preferSelected)
            for (int k = (int)ids.size() - 1; k >= 0; --k)
                if (std::find(sel.begin(), sel.end(), ids[k]) != sel.end()) return ids[k];
        return ids.back();
    }
    void moveSelectionBy(Vec2 delta) {
        if (sel.empty() || (delta.x == 0.f && delta.y == 0.f)) return;
        phys.translateBodies(sel, delta);
    }

    // The CUT tool: a box or circle dragged over the picture is cut out of every body it touches.
    void applyCutShape(Vec2 a, Vec2 b) {
        std::vector<int> targets;
        for (auto& bd : phys.bodies) if (bd.alive) targets.push_back(bd.id);
        int cutter;
        if (cutCircle) {
            float r = length(b - a);
            if (r < 1.5f) { notify("DRAG TO SIZE THE CIRCLE YOU WANT TO CUT OUT"); return; }
            cutter = phys.addCircle(a, r, M_STEEL, true, false);
        } else {
            Vec2 half = Vec2(std::fabs(b.x - a.x), std::fabs(b.y - a.y)) * 0.5f;
            if (half.x < 0.75f || half.y < 0.75f) { notify("DRAG TO SIZE THE BOX YOU WANT TO CUT OUT"); return; }
            cutter = phys.addBox((a + b) * 0.5f, half, 0, M_STEEL, true);
        }
        pushUndo();
        int bodiesCut = 0, pieces = 0;
        for (int t : targets) {
            if (t == cutter) continue;
            int r = phys.cutBody(t, {cutter});
            if (r >= 0) { ++bodiesCut; pieces += r; }
        }
        phys.removeBody(cutter);
        clearSelection();
        phys.stampBodies();
        if (bodiesCut) notify("CUT " + std::to_string(bodiesCut) + (bodiesCut == 1 ? " BODY" : " BODIES") + " (" + std::to_string(pieces) + " PIECES). CTRL+Z UNDOES");
        else notify("NOTHING TO CUT THERE: THE SHAPE MUST OVERLAP A BOX OR CIRCLE (WHEELS, ROCKETS, FANS AND EMITTERS CAN'T BE CUT)");
    }

    // ---------------------------------------------------------------- camera
    void setCam(float x, float y = -1e9f) {
        camXf = std::clamp(x, 0.f, std::max(0.f, (float)World::W - viewW()));
        if (y > -1e8f) camYf = y;
        camYf = std::clamp(camYf, 0.f, std::max(0.f, (float)World::H - viewH()));
        camX = (int)std::lround(camXf); camY = (int)std::lround(camYf);
    }
    // change the zoom one step, keeping the world point under (px, py) (window pixels) where it is
    void zoomStep(int dir, int px, int py) {
        int idx = 0;
        for (int i = 0; i < 7; ++i) if (std::fabs(ZOOMS[i] - zoom) < 0.01f) idx = i;
        int ni = std::clamp(idx + dir, 0, 6);
        if (ni == idx) { notify(dir > 0 ? "MAXIMUM ZOOM" : "ALREADY SHOWING THE WHOLE HEIGHT"); return; }
        Vec2 anchor = toWorld(px, py);
        float fx = (float)(px - SIM_X) / SIM_W, fy = (float)(py - SIM_Y) / SIM_H;
        zoom = ZOOMS[ni];
        setCam(anchor.x - fx * viewW(), anchor.y - fy * viewH());
        notify("ZOOM " + fmt(zoom) + "X  (CTRL+WHEEL, OR CTRL + PLUS / MINUS; CTRL+0 RESETS)");
    }
    void zoomCentre(int dir) { zoomStep(dir, SIM_X + SIM_W / 2, SIM_Y + SIM_H / 2); }
    void zoomReset() { zoom = 1.f; setCam(camXf, 0.f); }
    Vec2 focusPoint(bool& ok) const {
        ok = false;
        if (focusBody < 0 || focusBody >= (int)phys.bodies.size() || !phys.bodies[focusBody].alive) return Vec2();
        ok = true;
        const Body& b = phys.bodies[focusBody];
        if (b.group < 0) return b.pos;
        double m = 0, x = 0, y = 0;   // a group is followed by its centre of mass
        for (auto& o : phys.bodies) if (o.alive && o.group == b.group) { m += o.mass; x += o.pos.x * o.mass; y += o.pos.y * o.mass; }
        return m > 0 ? Vec2((float)(x / m), (float)(y / m)) : b.pos;
    }
    void updateCamera(bool snap = false) {
        bool ok;
        Vec2 f = focusPoint(ok);
        if (focusBody >= 0 && !ok) { focusBody = -1; notify("FOCUS LOST (THE BODY IS GONE)"); }
        if (!ok || panning || scrubbing) return;
        float target = f.x - viewW() * 0.5f, targetY = f.y - viewH() * 0.5f;
        setCam(snap ? target : camXf + (target - camXf) * 0.14f, snap ? targetY : camYf + (targetY - camYf) * 0.14f);
    }
    void toggleFocus() {
        if (focusBody >= 0) { focusBody = -1; notify("FOCUS OFF: THE CAMERA STAYS PUT (MIDDLE-DRAG OR THE STRIP BELOW PANS)"); return; }
        pruneSelection();
        if (primary < 0) { notify("SELECT A BODY FIRST (SELECT TOOL), THEN PRESS FOCUS (Z)"); return; }
        focusBody = primary;
        updateCamera(true);
        notify("FOCUS ON: THE CAMERA FOLLOWS THIS BODY. PRESS Z AGAIN TO RELEASE");
    }
    bool inScrollStrip(int localY) const { return localY >= SIM_H - 12 && localY < SIM_H; }
    void scrubTo(int mx) { setCam((float)mx / SIM_W * World::W - viewW() * 0.5f); }
    void scrubToY(int my) { setCam(camXf, (float)my / SIM_H * World::H - viewH() * 0.5f); }
    bool inVStrip(int localX) const { return zoom > 1.01f && localX >= SIM_W - 12; }

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
            case T_FAN: {
                Vec2 half = Vec2(std::fabs(d.x), std::fabs(d.y)) * 0.5f;
                Vec2 c = (a + b) * 0.5f;
                if (half.x < 1.5f || half.y < 1.5f) { half = Vec2(3, 10); c = a; }
                float ang = 0.f;
                if (half.x > half.y) { std::swap(half.x, half.y); ang = PI * 0.5f; }   // the long side is the blade span
                int id = phys.addBox(c, half, ang, bodyMat, anchored);
                phys.bodies[id].fan.strength = lastFan;
                phys.bodies[id].fan.vacuum = fanVacuumDefault ? 1 : 0;
                sel = {id}; primary = id; partMode = false;
                notify("FAN " + fmt(lastFan) + ". + / - CHANGE STRENGTH, \\ FLIPS DIRECTION, ENTER = EXACT VALUES");
                break;
            }
            case T_EMITTER: {
                Vec2 half = Vec2(std::fabs(d.x), std::fabs(d.y)) * 0.5f;
                Vec2 c = (a + b) * 0.5f;
                if (half.x < 1.5f || half.y < 1.5f) { half = Vec2(3, 3); c = a; }
                int id = phys.addBox(c, half, 0, bodyMat, anchored);
                phys.bodies[id].src = Emitter{true, payload, lastRate, 0.f, emitFace};
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

    // The two bodies a click is meant to join: those under the pointer, or failing that the nearest ones within a few
    // cells (bodies that merely touch have nothing under their seam). b = -1 means the world.
    bool pairAt(Vec2 p, int& a, int& b, float reach = 3.5f) {
        std::vector<int> under = phys.bodiesAt(p);
        a = -1; b = -1;
        auto nearest = [&](int skipA, int skipGroupOf) {
            int best = -1; float bd = reach;
            for (auto& o : phys.bodies) {
                if (!o.alive || o.id == skipA) continue;
                if (skipGroupOf >= 0 && o.group >= 0 && o.group == phys.bodies[skipGroupOf].group) continue;
                float d = o.distanceTo(p);
                if (d < bd) { bd = d; best = o.id; }
            }
            return best;
        };
        if (!under.empty()) a = under.back();
        else a = nearest(-1, -1);
        if (a < 0) return false;
        for (int i = (int)under.size() - 1; i >= 0; --i) {
            int o = under[i];
            if (o == a || (phys.bodies[a].group >= 0 && phys.bodies[o].group == phys.bodies[a].group)) continue;
            b = o; break;
        }
        if (b < 0) b = nearest(a, a);
        return true;
    }

    void clickJoint(Vec2 p) {
        int a, b;
        if (!pairAt(p, a, b)) { notify("CLICK ON A BODY, OR WHERE TWO BODIES MEET"); return; }
        bool motor = tool == T_MOTOR || tool == T_AUTOMOTOR;
        if (b >= 0 && phys.bodies[b].seq < phys.bodies[a].seq) std::swap(a, b);
        phys.addPin(p, a, b, motor, tool != T_AUTOMOTOR);
        notify(b >= 0 ? "JOINED THE TWO BODIES" : "PINNED TO THE WORLD (NO OTHER BODY NEAR THE CLICK)");
    }

    void clickBond(Vec2 p) {
        int a, b;
        if (!pairAt(p, a, b)) { notify("CLICK ON A BODY, OR WHERE TWO BODIES MEET"); return; }
        phys.addBond(p, a, b, bondT, 0.f, bondG);
        notify(std::string(b >= 0 ? "BONDED THE TWO BODIES" : "BONDED TO THE WORLD (NO OTHER BODY NEAR THE CLICK)") + ": MELTS AT " + fmt(bondT) + "C, HOLDS " + fmt(bondG) + "X ITS WEIGHT");
    }

    // Starts moving the selected bodies once the pointer has really dragged; also called on release so that a quick
    // press-drag-release inside one frame still moves them.
    void updateMoveDrag() {
        if (!lmb || tool != T_SELECT || !moveArmed) return;
        Vec2 total = mouse - dragStart;
        if (!moving) {
            if (length(total) < 2.5f) return;
            const Uint8* ks = SDL_GetKeyboardState(nullptr);
            bool add = ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT];
            if (std::find(sel.begin(), sel.end(), moveHit) == sel.end()) selectBody(moveHit, add, false);
            if (sel.empty()) return;
            pushUndo();
            moving = true;
            moveApplied = Vec2();
        }
        Vec2 target = snapIdx ? snap(total) : total;
        moveSelectionBy(target - moveApplied);
        moveApplied = target;
    }

    void handleSimDown(int button) {
        if (button == SDL_BUTTON_RIGHT) { pushUndo("erase"); rmb = true; return; }
        lmb = true;
        dragStart = smouse();
        moving = false; moveArmed = false;
        switch (tool) {
            case T_MAT: pushUndo("paint"); break;
            case T_BOND: pushUndo(); clickBond(mouse); break;
            case T_PIN: case T_MOTOR: case T_AUTOMOTOR: pushUndo(); clickJoint(mouse); break;
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
            case T_SELECT:
                dragStart = mouse;
                moveHit = topBodyAt(mouse, true);   // a press on a selected body drags the whole selection
                moveArmed = moveHit >= 0;
                break;
            case T_DELETE: {
                int id = phys.pickBody(mouse, true);
                pushUndo();
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
        switch (tool) {
            case T_BOX: case T_CIRCLE: case T_WHEEL: case T_ROCKET: case T_PIPE: case T_HOSE: case T_EMITTER: case T_FAN:
                pushUndo();
                createShape(dragStart, smouse());
                break;
            case T_CUT: applyCutShape(dragStart, smouse()); break;
            case T_SELECT: {
                const Uint8* ks = SDL_GetKeyboardState(nullptr);
                bool add = ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT];
                bool part = ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_RCTRL];
                if (moveArmed) updateMoveDrag();
                if (moving) { moving = false; moveArmed = false; phys.stampBodies(); break; }
                moveArmed = false;
                if (length(mouse - dragStart) < 3.f) {
                    int jj = jointAt(mouse);
                    if (jj >= 0) selectJoint(jj);
                    else selectBody(pickCycling(mouse), add, part);
                }
                else boxSelect(dragStart, mouse, add, part);
                break;
            }
            case T_ROD: case T_SPRING: {
                std::vector<int> ids = phys.bodiesAt(mouse);
                int endBody = ids.empty() ? -1 : ids.back();
                if (length(mouse - dragStart) > 3.f && (dragBody >= 0 || endBody >= 0) && dragBody != endBody) {
                    pushUndo();
                    { int jid = phys.addDistance(dragBody, dragStart, endBody, mouse, tool == T_SPRING ? springFreq : 0.f); if (jid >= 0 && tool == T_SPRING) phys.joints[jid].damping = springDamp; }
                }
                dragBody = -1;
                break;
            }
            case T_SLIDER: {
                if (dragBody < 0) { notify("PRESS ON THE BODY THAT SHOULD SLIDE, THEN DRAG ALONG ITS LINE"); break; }
                Vec2 axis = mouse - dragStart;
                if (length(axis) < 3.f) axis = Vec2(1, 0);
                // the body it slides along: the one under the release point or the press point (not part of its own group)
                int host = -1;
                for (Vec2 q : {mouse, dragStart}) {
                    std::vector<int> ids = phys.bodiesAt(q);
                    for (int i = (int)ids.size() - 1; i >= 0 && host < 0; --i) {
                        int o = ids[i];
                        if (o == dragBody || (phys.bodies[dragBody].group >= 0 && phys.bodies[o].group == phys.bodies[dragBody].group)) continue;
                        host = o;
                    }
                    if (host >= 0) break;
                }
                bool moving_ = !phys.bodies[dragBody].isStatic;
                if (host >= 0 && (moving_ || !phys.bodies[host].isStatic)) {
                    pushUndo();
                    phys.addSliderRel(dragBody, host, dragStart, axis);
                    notify("SLIDER: IT NOW SLIDES ALONG THE OTHER BODY, WHEREVER THAT GOES");
                } else if (moving_) {
                    pushUndo();
                    phys.addSlider(dragBody, axis);
                    notify("SLIDER: IT NOW SLIDES ALONG THIS LINE (FIXED IN THE WORLD)");
                } else {
                    notify("A FIXED BODY CAN'T SLIDE: UNCHECK 'FIXED IN PLACE' OR PICK A MOVING BODY");
                }
                dragBody = -1;
                break;
            }
            case T_GRAB:
                if (grabJoint >= 0) phys.removeJoint(grabJoint);
                grabJoint = -1;
                dragBody = -1;
                break;
            default: break;
        }
        lmb = false;
    }

    void clickPanels(int x, int y) {
        layoutButtons();
        for (auto& b : buttons)
            if (b.zone != 2 && b.enabled() && x >= b.r.x && x < b.r.x + b.r.w && y >= b.r.y && y < b.r.y + b.r.h) { b.action(); return; }
        for (auto& it : rp)
            if ((it.kind == 1 || it.kind == 2) && it.enabled && it.act && x >= it.r.x && x < it.r.x + it.r.w && y >= it.r.y && y < it.r.y + it.r.h) { it.act(); return; }
    }

    void handleEvents() {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_MOUSEMOTION:
                    mousePx = e.motion.x; mousePy = e.motion.y;
                    mouse = toWorld(e.motion.x, e.motion.y);
                    inSim = inSimPx(e.motion.x, e.motion.y);
                    if (panning) setCam(panStartCam - (float)(e.motion.x - panStartPx) / sc(), panStartCamY - (float)(e.motion.y - panStartPy) / sc());
                    if (scrubbing) scrubTo(e.motion.x - SIM_X);
                    if (scrubbingV) scrubToY(e.motion.y - SIM_Y);
                    break;
                case SDL_MOUSEBUTTONDOWN: {
                    mousePx = e.button.x; mousePy = e.button.y;
                    mouse = toWorld(e.button.x, e.button.y);
                    const int lx = e.button.x - SIM_X, ly = e.button.y - SIM_Y;
                    const bool inS = inSimPx(e.button.x, e.button.y);
                    const bool left = e.button.button == SDL_BUTTON_LEFT;
                    if (scenesOpen && left) {   // the scene picker takes the click
                        layoutButtons();
                        bool hit = false;
                        for (auto& it : modal)
                            if (it.kind == 2 && it.act && lx >= it.r.x && lx < it.r.x + it.r.w && ly >= it.r.y && ly < it.r.y + it.r.h) { it.act(); hit = true; break; }
                        if (!hit && inS) { scenesOpen = false; }
                        if (hit || inS) break;
                    }
                    if (left && inS && formClick(lx, ly)) break;
                    if (inS) {
                        if (e.button.button == SDL_BUTTON_MIDDLE) {
                            panning = true; panStartPx = e.button.x; panStartPy = e.button.y; panStartCam = camXf; panStartCamY = camYf;
                            if (focusBody >= 0) { focusBody = -1; notify("FOCUS OFF (YOU PANNED THE CAMERA)"); }
                            break;
                        }
                        if (left && inVStrip(lx) && !helpOn && formKind == FK_NONE) {
                            scrubbingV = true; scrubToY(ly);
                            if (focusBody >= 0) { focusBody = -1; notify("FOCUS OFF (YOU MOVED THE CAMERA)"); }
                            break;
                        }
                        if (left && inScrollStrip(ly) && !helpOn && formKind == FK_NONE) {
                            scrubbing = true; scrubTo(lx);
                            if (focusBody >= 0) { focusBody = -1; notify("FOCUS OFF (YOU MOVED THE CAMERA)"); }
                            break;
                        }
                        if (helpOn) { helpOn = false; break; }   // a click anywhere on the help card closes it
                        lastMouse = mouse;
                        handleSimDown(e.button.button);
                    } else if (left) {
                        clickPanels(e.button.x, e.button.y);
                    }
                    break;
                }
                case SDL_MOUSEBUTTONUP:
                    mouse = toWorld(e.button.x, e.button.y);
                    if (e.button.button == SDL_BUTTON_MIDDLE) { panning = false; break; }
                    if (e.button.button == SDL_BUTTON_LEFT && (scrubbing || scrubbingV)) { scrubbing = false; scrubbingV = false; break; }
                    if (lmb || e.button.button == SDL_BUTTON_RIGHT) handleSimUp(e.button.button);
                    break;
                case SDL_MOUSEWHEEL:
                    if (!inSimPx(mousePx, mousePy)) break;
                    if (SDL_GetModState() & KMOD_CTRL) { zoomStep(e.wheel.y > 0 ? 1 : -1, mousePx, mousePy); break; }
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

    void deleteSelection() {
        if (jointValid(selJoint)) { removeSelectedJoint(); return; }
        pruneSelection();
        if (sel.empty()) {
            int id = phys.pickBody(mouse, true);
            if (id < 0) return;
            pushUndo();
            phys.removeBody(id);
        } else {
            pushUndo();
            for (int id : std::vector<int>(sel)) phys.removeBody(id);
            clearSelection();
        }
        phys.stampBodies();
    }

    // in edit mode the arrow keys nudge whatever is selected (while playing they still drive motors and rockets):
    // 1 cell, Shift 10 cells, Ctrl a quarter of a cell
    void nudgeSelection(SDL_Keycode k) {
        pruneSelection();
        if (playing || sel.empty()) return;
        const Uint16 mod = SDL_GetModState();
        float step = (mod & KMOD_SHIFT) ? 10.f : (mod & KMOD_CTRL) ? 0.25f : 1.f;
        Vec2 d(k == SDLK_RIGHT ? step : k == SDLK_LEFT ? -step : 0.f, k == SDLK_DOWN ? step : k == SDLK_UP ? -step : 0.f);
        pushUndo("nudge");
        moveSelectionBy(d);
        phys.stampBodies();
        if (primary >= 0 && primary < (int)phys.bodies.size() && phys.bodies[primary].alive)
            notify("MOVED TO " + fmt(phys.bodies[primary].pos.x) + ", " + fmt(phys.bodies[primary].pos.y) + "  (ARROW = 1 CELL, SHIFT = 10, CTRL = 0.25)");
    }

    void handleKey(SDL_Keycode k) {
        if (formKey(k, SDL_GetModState())) return;
        const Uint16 mod = SDL_GetModState();
        const bool ctrl = (mod & KMOD_CTRL) != 0, shift = (mod & KMOD_SHIFT) != 0;
        if (ctrl) {
            switch (k) {
                case SDLK_c: copySelection(); break;
                case SDLK_v: pasteClipboard(); break;
                case SDLK_z: if (shift) redo(); else undo(); break;
                case SDLK_y: redo(); break;
                case SDLK_a: selectAll(); break;
                case SDLK_s: saveQuick(); break;
                case SDLK_o: openFileForm(false); break;
                case SDLK_n: newFile(); break;
                case SDLK_g: groupSelection(); break;
                case SDLK_u: ungroupSelection(); break;
                case SDLK_EQUALS: case SDLK_PLUS: case SDLK_KP_PLUS: zoomCentre(1); break;
                case SDLK_MINUS: case SDLK_KP_MINUS: zoomCentre(-1); break;
                case SDLK_0: case SDLK_KP_0: zoomReset(); break;
                case SDLK_LEFT: case SDLK_RIGHT: case SDLK_UP: case SDLK_DOWN: nudgeSelection(k); break;
                default: break;
            }
            return;
        }
        switch (k) {
            case SDLK_RETURN: case SDLK_KP_ENTER: openForm(); break;
            case SDLK_F1: helpOn = !helpOn; break;
            case SDLK_f: toggleFocus(); break;
            case SDLK_m: toggleFanMode(); break;
            case SDLK_HOME: if (focusBody >= 0) focusBody = -1; setCam(0); break;
            case SDLK_END: if (focusBody >= 0) focusBody = -1; setCam((float)World::W); break;
            case SDLK_PAGEUP: if (focusBody >= 0) focusBody = -1; setCam(camXf - viewW() * 0.5f); break;
            case SDLK_PAGEDOWN: if (focusBody >= 0) focusBody = -1; setCam(camXf + viewW() * 0.5f); break;
            case SDLK_EQUALS: case SDLK_PLUS: case SDLK_KP_PLUS: adjustFan(10.f); break;
            case SDLK_MINUS: case SDLK_KP_MINUS: adjustFan(-10.f); break;
            case SDLK_BACKSLASH: flipFan(); break;
            case SDLK_ESCAPE:
                if (scenesOpen) scenesOpen = false;
                else if (helpOn) helpOn = false;
                else if (!sel.empty()) clearSelection();
                break;
            case SDLK_SPACE: if (!playing) play(); else togglePause(); break;
            case SDLK_n: stepFrame(); break;
            case SDLK_t: setFixed(!anchored); break;
            case SDLK_g: phys.gravity.y = phys.gravity.y > 0 ? -260.f : 260.f; notify(phys.gravity.y > 0 ? "GRAVITY DOWN" : "GRAVITY UP"); break;
            case SDLK_h: heatView = !heatView; break;
            case SDLK_k: gridOn = !gridOn; break;
            case SDLK_p: pressureView = !pressureView; break;
            case SDLK_COMMA: sparkIdx = (sparkIdx + 6) % 7; world.sparkPeriod = SPARK_RATES[sparkIdx]; break;
            case SDLK_PERIOD: sparkIdx = (sparkIdx + 1) % 7; world.sparkPeriod = SPARK_RATES[sparkIdx]; break;
            case SDLK_LEFT: case SDLK_RIGHT: case SDLK_UP: case SDLK_DOWN: nudgeSelection(k); break;
            case SDLK_LEFTBRACKET: brush = std::max(1, brush - 1); break;
            case SDLK_RIGHTBRACKET: brush = std::min(24, brush + 1); break;
            case SDLK_DELETE: case SDLK_BACKSPACE: deleteSelection(); break;
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
        updateMoveDrag();
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
        if (playing && !paused) fanPhase += 1.f;
        if (!playing) phys.stampBodies();   // editing: keep the cover cells in step with what is drawn
        if (newArmed > 0) --newArmed;
        if ((playing && !paused) || stepOnce) {
            phys.step(1.f / 60.f);
            world.step();
            stepOnce = false;
        }
        updateCamera();
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
            case M_AIR: {
                float a = std::clamp(0.03f + c.amt * 0.07f, 0.03f, 0.35f);
                return mix(bg, m.color, a);
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
        // only the visible cells are coloured (one extra row and column, so a fractional camera still covers the edge)
        const int x0 = std::clamp((int)std::floor(camXf), 0, World::W - 1), x1 = std::min(World::W, x0 + (int)std::ceil(viewW()) + 2);
        const int y0 = std::clamp((int)std::floor(camYf), 0, World::H - 1), y1 = std::min(World::H, y0 + (int)std::ceil(viewH()) + 2);
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) { int i = y * World::W + x; pixels[i] = cellColor(world.cells[i], bg); }
        if (pressureView) {
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) {
                    int i = y * World::W + x;
                    const Cell& c = world.cells[i];
                    if (world.bodyMask[i] >= 0 || c.t == M_EMPTY) { pixels[i] = mix(pixels[i], bg, 0.35f); continue; }
                    Kind k = MATS[c.t].kind;
                    if (k == K_GAS && c.t != M_FIRE) {
                        float p = c.amt * (c.temp + 273.f) / 293.f;     // 1 = ambient
                        uint32_t col = p < 1.f ? mix(0x203a78, 0xe8eef8, std::sqrt(std::clamp(p, 0.f, 1.f)))
                                               : mix(0xe8eef8, 0xe03020, std::clamp((p - 1.f) / 2.f, 0.f, 1.f));
                        pixels[i] = col;
                    } else if (k == K_LIQUID && c.amt > 1.01f) pixels[i] = mix(pixels[i], 0xe03020, std::clamp((c.amt - 1.f) * 3.f, 0.f, 1.f));
                    else pixels[i] = mix(pixels[i], bg, 0.6f);
                }
        }
        if (elecView && (int)world.volt.size() == World::W * World::H) {
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) {
                    int i = y * World::W + x;
                    int b = world.bodyMask[i];
                    float sg = b >= 0 ? (b < (int)world.bodySigma.size() ? world.bodySigma[b] : 0.f) : MATS[world.cells[i].t].elec;
                    if (sg <= 0.f) { pixels[i] = mix(pixels[i], bg, 0.65f); continue; }
                    float v = world.volt[i], f = world.vMax > 0.5f ? std::clamp(std::log1p(std::fabs(v)) / std::log1p(world.vMax), 0.f, 1.f) : 0.f;
                    uint32_t col = v > 0.01f ? mix(0x1c4a78, 0xfff060, f) : 0x203040;
                    float cur = std::min(1.f, world.curr[i] * 0.05f);
                    pixels[i] = mix(col, 0xff9040, cur * 0.6f);
                }
        }
        SDL_Rect area{x0, y0, x1 - x0, y1 - y0};
        SDL_UpdateTexture(tex, &area, pixels.data() + (size_t)y0 * World::W + x0, World::W * 4);
        SDL_Rect dst{(int)std::lround((x0 - camXf) * sc()), (int)std::lround((y0 - camYf) * sc()), (int)std::lround((x1 - x0) * sc()), (int)std::lround((y1 - y0) * sc())};
        SDL_RenderCopy(ren, tex, &area, &dst);
    }

    SDL_FPoint sp(Vec2 p) const { return SDL_FPoint{(p.x - camXf) * sc(), (p.y - camYf) * sc()}; }

    void fillPoly(const std::vector<Vec2>& pts, uint32_t color) { fillPolyC(pts, rgb(color)); }
    void fillPolyC(const std::vector<Vec2>& pts, SDL_Color c) {
        std::vector<SDL_Vertex> v;
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

    // blades sliding across the housing, and an arrow along the direction of airflow
    void renderFan(const Body& b, uint32_t fill) {
        float s = b.fan.strength, mag = std::fabs(s);
        float dirSign = s > 0 ? 1.f : -1.f;
        SDL_Color blade = rgb(shade(fill, 1.35f) & 0xFFFFFF);
        SDL_Color dark = rgb(shade(fill, 0.45f) & 0xFFFFFF);
        float span = 2.f * b.half.y;
        int n = std::max(3, (int)std::lround(span / 4.f));
        float gap = span / n;
        float phase = std::fmod(fanPhase * mag * 0.012f, gap * 2.f);   // faster when stronger
        for (int i = -1; i <= n; ++i) {
            float y = -b.half.y + std::fmod(phase * dirSign + i * gap + span * 4.f, span + gap) - gap * 0.0f;
            if (y < -b.half.y || y > b.half.y) continue;
            float slant = std::min(b.half.x * 1.6f, 3.f) * dirSign;
            lineWorld(b.toWorld(Vec2(-b.half.x + 0.3f, y - slant)), b.toWorld(Vec2(b.half.x - 0.3f, y + slant)), blade, 2);
        }
        lineWorld(b.toWorld(Vec2(-b.half.x, -b.half.y)), b.toWorld(Vec2(-b.half.x, b.half.y)), dark, 1);
        lineWorld(b.toWorld(Vec2(b.half.x, -b.half.y)), b.toWorld(Vec2(b.half.x, b.half.y)), dark, 1);
        // arrow: from the centre along the flow
        Vec2 dir = rotate(Vec2(dirSign, 0.f), b.angle), nrm(-dir.y, dir.x);
        float len = std::clamp(span * 0.55f, 9.f, 26.f);
        Vec2 tail = b.pos - dir * (len * 0.5f), tip = b.pos + dir * (len * 0.5f);
        SDL_Color ac{255, 235, 120, 255};
        lineWorld(tail, tip, ac, 3);
        lineWorld(tip, tip - dir * 5.f + nrm * 3.5f, ac, 3);
        lineWorld(tip, tip - dir * 5.f - nrm * 3.5f, ac, 3);
        if (b.fan.vacuum) {   // converging chevrons on the intake side: gas is being drawn in
            SDL_Color cy{120, 225, 255, 255};
            for (int i = -1; i <= 1; ++i) {
                Vec2 base = b.pos - dir * (b.half.x + 9.f) + nrm * (b.half.y * 0.55f * (float)i);
                Vec2 tp = base + dir * 5.f;
                lineWorld(base, tp, cy, 2);
                lineWorld(tp, tp - dir * 3.f + nrm * 2.5f, cy, 2);
                lineWorld(tp, tp - dir * 3.f - nrm * 2.5f, cy, 2);
            }
        }
        std::string lab = std::string(b.fan.vacuum ? "VAC " : "") + fmt(mag);
        Vec2 lp = b.toWorld(Vec2(0.f, -b.half.y)) - Vec2(0.f, 7.f);
        font::draw(ren, lab, (int)sp(lp).x - font::textWidth(lab, 1) / 2, (int)sp(lp).y, 1, SDL_Color{255, 235, 120, 255});
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
                if (b.group < 0) outlinePoly(pts, edge);   // pieces of a group read as one solid shape
                if (b.isRocket) {
                    Vec2 tip = b.toWorld(Vec2(0, -b.half.y - 5));
                    fillPoly({b.toWorld(Vec2(-b.half.x, -b.half.y)), b.toWorld(Vec2(b.half.x, -b.half.y)), tip}, 0xf0e0d0);
                    fillPoly({b.toWorld(Vec2(-b.half.x, b.half.y)), b.toWorld(Vec2(b.half.x, b.half.y)),
                              b.toWorld(Vec2(0, b.half.y + 3))}, 0x404048);
                }
                if (b.fan.strength != 0.f) renderFan(b, fill);
                if (b.src.on) {
                    fillPoly(circlePts(b.pos, std::min(2.f, std::min(b.half.x, b.half.y) * 0.6f), 10), 0xFF000000u | MATS[b.src.mat].color);
                    static const Vec2 fd[5] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                    if (b.src.face) {   // the outlet: a bright slot in the chosen face and an arrow out of it
                        Vec2 nl = fd[b.src.face], tl(-nl.y, nl.x);
                        float hw = std::fabs(tl.x) * b.half.x + std::fabs(tl.y) * b.half.y;   // half length of that face
                        Vec2 fc(nl.x * b.half.x, nl.y * b.half.y);
                        SDL_Color white{255, 255, 255, 235};
                        lineWorld(b.toWorld(fc + tl * hw * 0.8f), b.toWorld(fc - tl * hw * 0.8f), white, 3);
                        Vec2 tip = b.toWorld(fc + nl * 5.f), mid = b.toWorld(fc + nl * 1.f);
                        lineWorld(mid, tip, white, 2);
                        lineWorld(tip, b.toWorld(fc + nl * 2.5f + tl * 2.2f), white, 2);
                        lineWorld(tip, b.toWorld(fc + nl * 2.5f - tl * 2.2f), white, 2);
                    }
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

    void renderFocusMark() {
        bool ok;
        Vec2 f = focusPoint(ok);
        if (!ok) return;
        const Body& b = phys.bodies[focusBody];
        float r = std::max(6.f, b.bound + 3.f);
        SDL_Color c{255, 214, 90, 230};
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2) {
                Vec2 corner = f + Vec2(r * sx, r * sy);
                lineWorld(corner, corner - Vec2(4.f * sx, 0), c, 2);
                lineWorld(corner, corner - Vec2(0, 4.f * sy), c, 2);
            }
    }

    void renderSelection() {
        const bool several = sel.size() >= 2;
        for (int id : sel) {
            if (id < 0 || id >= (int)phys.bodies.size() || !phys.bodies[id].alive) continue;
            const Body& b = phys.bodies[id];
            bool pri = id == primary;
            std::vector<Vec2> o = bodyOutline(b, 0.f);
            fillPolyC(o, SDL_Color{255, 220, 80, 70});   // a warm tint over everything selected
            if (pri) outlinePoly(bodyOutline(b, 1.2f), several ? SDL_Color{255, 90, 90, 255} : SDL_Color{255, 255, 255, 255});
            else if (b.group < 0) outlinePoly(bodyOutline(b, 1.2f), SDL_Color{255, 220, 80, 230});
        }
        if (primary >= 0 && primary < (int)phys.bodies.size() && phys.bodies[primary].alive) {
            Vec2 p = phys.bodies[primary].pos;
            lineWorld(p + Vec2(-3, 0), p + Vec2(3, 0), SDL_Color{255, 255, 255, 255});
            lineWorld(p + Vec2(0, -3), p + Vec2(0, 3), SDL_Color{255, 255, 255, 255});
            if (several && (tool == T_SELECT || tool == T_CUT)) {
                SDL_FPoint q = sp(p);
                font::draw(ren, "CUTTER", (int)q.x - 36, (int)q.y - 22, 1, SDL_Color{255, 120, 120, 255});
            }
        }
        if (jointValid(selJoint)) {   // a ring round the selected joint (and a halo along a spring or rod)
            const Joint& j = phys.joints[selJoint];
            Vec2 a = phys.jointAnchorA(j);
            SDL_Color hl{255, 230, 90, 255};
            if (j.type == J_DISTANCE) { Vec2 b = phys.jointAnchorB(j); lineWorld(a, b, SDL_Color{255, 230, 90, 90}, 9); outlinePoly(circlePts(b, 5.f, 16), hl); }
            outlinePoly(circlePts(a, 5.f, 16), hl);
            outlinePoly(circlePts(a, 6.f, 16), SDL_Color{255, 255, 255, 160});
        }
        if (tool == T_SELECT && lmb && !moveArmed && !moving && length(mouse - dragStart) >= 3.f) {
            Vec2 a = dragStart, b = mouse;
            outlinePoly({a, Vec2(b.x, a.y), b, Vec2(a.x, b.y)}, SDL_Color{255, 220, 80, 200});
        }
    }

    std::vector<int> bondsDrawn;
    void renderJoints() {
        bondsDrawn.clear();
        for (auto& j : phys.joints) {
            if (!j.alive || j.group >= 0) continue;
            Vec2 a = phys.jointAnchorA(j);
            if (j.type == J_MOUSE) {
                lineWorld(a, j.lb, SDL_Color{255, 255, 255, 160});
                continue;
            }
            if (j.type == J_SLIDER && j.b >= 0) {   // between two bodies: rails along the line fixed in the host
                Vec2 ax = rotate(j.u, phys.bodies[j.b].angle), p = phys.jointAnchorA(j);
                lineWorld(p - ax * 14.f, p + ax * 14.f, SDL_Color{255, 255, 255, 110}, 1);
                for (int i = -2; i <= 2; ++i) { Vec2 q = p + ax * (float)(i * 6), nn(-ax.y, ax.x); lineWorld(q - nn * 2.f, q + nn * 2.f, SDL_Color{255, 255, 255, 150}, 1); }
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
            if (j.bondId >= 0) {   // a bond: a visible seam between its two pins, warming towards red as it nears melting
                if (std::find(bondsDrawn.begin(), bondsDrawn.end(), j.bondId) != bondsDrawn.end()) continue;
                bondsDrawn.push_back(j.bondId);
                Vec2 other = a;
                for (auto& k : phys.joints) if (k.alive && k.bondId == j.bondId && k.id != j.id) other = phys.jointAnchorA(k);
                float T = phys.bodies[j.a].temp;
                if (j.b >= 0) T = std::max(T, phys.bodies[j.b].temp);
                float hot = std::clamp((T - 20.f) / std::max(10.f, j.breakT - 20.f), 0.f, 1.f);
                SDL_Color base = j.breakT < 100.f ? SDL_Color{240, 232, 190, 255} : j.breakT < 230.f ? SDL_Color{190, 194, 206, 255} :
                                 j.breakT < 1000.f ? SDL_Color{224, 160, 64, 255} : SDL_Color{120, 124, 134, 255};
                SDL_Color c{(Uint8)(base.r + (255 - base.r) * hot), (Uint8)(base.g * (1.f - 0.75f * hot)), (Uint8)(base.b * (1.f - 0.85f * hot)), 255};
                lineWorld(a, other, SDL_Color{30, 30, 40, 255}, 6);
                lineWorld(a, other, c, 4);
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

    // ---- a frame of reference: grid lines in the world, rulers along the top and left, a scale bar, the world's edges
    static int niceStep(float cells) {   // the smallest of 1, 2, 5, 10, 20, 50... that is at least `cells`
        static const int steps[] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000};
        for (int s : steps) if ((float)s >= cells) return s;
        return 1000;
    }
    void renderGrid() {
        if (!gridOn) return;
        const float s = sc();
        const int minor = niceStep(14.f / s), label = std::max(minor, niceStep(64.f / s));
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        const int xa = (int)std::floor(camXf / minor) * minor, xb = (int)std::ceil(camXf + viewW());
        for (int x = std::max(0, xa); x <= std::min(World::W, xb); x += minor) {
            bool major = x % label == 0;
            SDL_SetRenderDrawColor(ren, 150, 175, 220, major ? 46 : 18);
            int px = (int)std::lround((x - camXf) * s);
            SDL_RenderDrawLine(ren, px, 0, px, SIM_H);
        }
        const int ya = (int)std::floor(camYf / minor) * minor, yb = (int)std::ceil(camYf + viewH());
        for (int y = std::max(0, ya); y <= std::min(World::H, yb); y += minor) {
            bool major = y % label == 0;
            SDL_SetRenderDrawColor(ren, 150, 175, 220, major ? 46 : 18);
            int py = (int)std::lround((y - camYf) * s);
            SDL_RenderDrawLine(ren, 0, py, SIM_W, py);
        }
    }
    void renderRulers() {
        const float s = sc();
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        // the edges of the world
        SDL_SetRenderDrawColor(ren, 255, 150, 60, 200);
        for (int ex : {0, World::W}) { int px = (int)std::lround((ex - camXf) * s); if (px >= 0 && px < SIM_W) SDL_RenderDrawLine(ren, px, 0, px, SIM_H); }
        for (int ey : {0, World::H}) { int py = (int)std::lround((ey - camYf) * s); if (py >= 0 && py < SIM_H) SDL_RenderDrawLine(ren, 0, py, SIM_W, py); }
        if (!gridOn) return;
        const int minor = niceStep(14.f / s), label = std::max(minor, niceStep(64.f / s));
        const int RW = 30, RH = 13;
        SDL_SetRenderDrawColor(ren, 8, 11, 18, 205);
        SDL_Rect top{0, 0, SIM_W, RH}, left{0, RH, RW, SIM_H - RH};
        SDL_RenderFillRect(ren, &top); SDL_RenderFillRect(ren, &left);
        SDL_SetRenderDrawColor(ren, 80, 95, 130, 255);
        SDL_RenderDrawLine(ren, 0, RH, SIM_W, RH); SDL_RenderDrawLine(ren, RW, RH, RW, SIM_H);
        const SDL_Color txt{170, 190, 225, 255};
        for (int x = std::max(0, (int)std::floor(camXf / label) * label); x <= std::min(World::W, (int)std::ceil(camXf + viewW())); x += label) {
            int px = (int)std::lround((x - camXf) * s);
            if (px < RW) continue;
            SDL_SetRenderDrawColor(ren, 150, 170, 210, 255); SDL_RenderDrawLine(ren, px, RH - 5, px, RH);
            font::draw(ren, std::to_string(x), px + 3, 2, 1, txt);
        }
        for (int y = std::max(0, (int)std::floor(camYf / label) * label); y <= std::min(World::H, (int)std::ceil(camYf + viewH())); y += label) {
            int py = (int)std::lround((y - camYf) * s);
            if (py < RH + 2) continue;
            SDL_SetRenderDrawColor(ren, 150, 170, 210, 255); SDL_RenderDrawLine(ren, RW - 5, py, RW, py);
            font::draw(ren, std::to_string(y), 2, py + 2, 1, txt);
        }
        if (inSim) {   // where the pointer is, on both rulers
            int px = (int)std::lround((mouse.x - camXf) * s), py = (int)std::lround((mouse.y - camYf) * s);
            SDL_SetRenderDrawColor(ren, 255, 220, 100, 255);
            if (px > RW) SDL_RenderDrawLine(ren, px, 0, px, RH - 1);
            if (py > RH) SDL_RenderDrawLine(ren, 0, py, RW - 1, py);
        }
        // a scale bar: a round number of cells, 50 to 200 pixels long
        int bar = niceStep(60.f / s);
        if (bar * s > 220.f) bar = std::max(1, bar / 2 == 0 ? 1 : (bar == 10 ? 5 : bar == 100 ? 50 : bar / 2));
        int bx = RW + 14, by = SIM_H - 50, bw = (int)std::lround(bar * s);
        SDL_SetRenderDrawColor(ren, 8, 11, 18, 190);
        SDL_Rect bg{bx - 6, by - 16, bw + 12 + 70, 28};
        SDL_RenderFillRect(ren, &bg);
        SDL_SetRenderDrawColor(ren, 235, 240, 250, 255);
        SDL_RenderDrawLine(ren, bx, by, bx + bw, by);
        SDL_RenderDrawLine(ren, bx, by - 4, bx, by + 4); SDL_RenderDrawLine(ren, bx + bw, by - 4, bx + bw, by + 4);
        font::draw(ren, std::to_string(bar) + " CELLS", bx, by - 14, 1, SDL_Color{235, 240, 250, 255});
    }

    // a strip along the bottom of the view: the whole world, the visible part, and where the moving bodies are
    void renderScrollStrip() {
        const float k = (float)SIM_W / World::W;
        SDL_Rect track{0, SIM_H - 8, SIM_W, 8};
        SDL_SetRenderDrawColor(ren, 8, 12, 20, 210);
        SDL_RenderFillRect(ren, &track);
        for (auto& b : phys.bodies)
            if (b.alive && !b.isStatic && (int)(&b - &phys.bodies[0]) != focusBody) {
                SDL_Rect t{(int)(b.pos.x * k), SIM_H - 6, 2, 4};
                SDL_SetRenderDrawColor(ren, 170, 185, 215, 255);
                SDL_RenderFillRect(ren, &t);
            }
        SDL_Rect thumb{(int)(camXf * k), SIM_H - 8, std::max(6, (int)(viewW() * k)), 8};
        SDL_SetRenderDrawColor(ren, 70, 100, 160, 110);
        SDL_RenderFillRect(ren, &thumb);
        SDL_SetRenderDrawColor(ren, 140, 175, 240, 255);
        SDL_RenderDrawRect(ren, &thumb);
        bool ok;
        Vec2 f = focusPoint(ok);
        if (ok) {
            SDL_Rect t{(int)(f.x * k) - 1, SIM_H - 8, 4, 8};
            SDL_SetRenderDrawColor(ren, 255, 214, 90, 255);
            SDL_RenderFillRect(ren, &t);
        }
        if (zoom > 1.01f) {   // zoomed in: a vertical strip on the right edge shows and moves the view up and down
            const float ky = (float)(SIM_H - 12) / World::H;
            SDL_Rect vt{SIM_W - 8, 0, 8, SIM_H - 12};
            SDL_SetRenderDrawColor(ren, 8, 12, 20, 210);
            SDL_RenderFillRect(ren, &vt);
            SDL_Rect vth{SIM_W - 8, (int)(camYf * ky), 8, std::max(6, (int)(viewH() * ky))};
            SDL_SetRenderDrawColor(ren, 70, 100, 160, 110);
            SDL_RenderFillRect(ren, &vth);
            SDL_SetRenderDrawColor(ren, 140, 175, 240, 255);
            SDL_RenderDrawRect(ren, &vth);
        }
    }

    void renderLabels() {
        for (auto& l : labels) font::draw(ren, l.s, (int)sp(l.p).x, (int)sp(l.p).y, 1, SDL_Color{200, 210, 230, 200});
    }

    void ghostLabel(Vec2 at, const std::string& t) {
        font::draw(ren, t, (int)sp(at).x + 8, (int)sp(at).y - 14, 2, SDL_Color{255, 240, 150, 255});
    }

    void renderGhost() {
        if (!inSim) return;
        SDL_Color white{255, 255, 255, 170};
        Vec2 m = smouse();
        if (lmb) {
            Vec2 d = m - dragStart;
            switch (tool) {
                case T_BOX: case T_EMITTER: case T_FAN: {
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
            case FK_FAN: title = "NEW FAN (STRENGTH < 0 REVERSES)"; break;
            case FK_BATTERY: title = "BATTERY (VOLTS / AMPS)"; break;
            case FK_BOND: title = "BOND SETTINGS"; break;
            case FK_SAVE: title = "SAVE AS (saves/NAME.sbot)"; break;
            case FK_LOAD: title = "LOAD (TYPE A NAME OR UP/DOWN)"; break;
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
            font::draw(ren, std::string("EMITS: ") + MATS[fPayload].name + " (CHOOSE IN THE PANEL)  FACE: " + faceName(fFace) + " (F)", 16, y + (formKind >= FK_EDIT_BOX ? 12 : 0), 1, SDL_Color{255, 160, 255, 255});
            if (formKind == FK_EMITTER) y += 12;
        }
        if (formKind >= FK_EDIT_BOX) {
            std::string ms = formKind == FK_EDIT_GROUP ? "" : std::string("M: ") + MATS[fMat].name + "  ";
            ms += std::string("S: ") + (fStatic ? "STATIC" : "DYNAMIC");
            if (formKind == FK_EDIT_GROUP) ms = "PARTS: " + std::to_string(sel.size()) + "  CTRL+CLICK EDITS ONE PART";
            font::draw(ren, ms, 16, y, 1, SDL_Color{255, 220, 120, 255});
            y += 12;
        }
        if (formKind == FK_SAVE || formKind == FK_LOAD) {
            int ly = y;
            font::draw(ren, fileList.empty() ? "NO SAVED FILES YET" : "SAVED FILES (NEWEST FIRST):", 16, ly, 1, SDL_Color{150, 160, 180, 255});
            for (size_t i = 0; i < fileList.size() && i < 6; ++i) {
                bool cur = (int)i == fileIdx;
                font::draw(ren, (cur ? "> " : "  ") + fileList[i], 16, ly + 12 + 12 * (int)i, 1, cur ? SDL_Color{255, 255, 255, 255} : SDL_Color{190, 205, 225, 255});
            }
            y += 14 + 12 * (int)std::min<size_t>(fileList.size(), 6);
            font::draw(ren, "ENTER: OK  ESC: CANCEL", 16, y, 1, SDL_Color{150, 160, 180, 255});
            font::draw(ren, formMsg, 16, y + 12, 1, SDL_Color{255, 150, 120, 255});
            return;
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
            if (b.fan.strength != 0.f) std::snprintf(buf, sizeof buf, "FAN %s %g/S%s", b.fan.vacuum ? "VACUUM" : "BLOW", b.fan.strength, b.fan.strength < 0 ? " REVERSED" : "");
            else if (b.src.on) std::snprintf(buf, sizeof buf, "EMITTER %s %g/S", MATS[b.src.mat].name, b.src.rate);
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

    // ---- drawing helpers: chamfered rectangles, text in segments
    void rrect(SDL_Rect r, SDL_Color c) {
        SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
        SDL_Rect a{r.x + 2, r.y, r.w - 4, r.h}, b{r.x, r.y + 2, r.w, r.h - 4}, d{r.x + 1, r.y + 1, r.w - 2, r.h - 2};
        SDL_RenderFillRect(ren, &a); SDL_RenderFillRect(ren, &b); SDL_RenderFillRect(ren, &d);
    }
    void rrectLine(SDL_Rect r, SDL_Color c) {
        SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
        int x0 = r.x, y0 = r.y, x1 = r.x + r.w - 1, y1 = r.y + r.h - 1;
        SDL_RenderDrawLine(ren, x0 + 2, y0, x1 - 2, y0); SDL_RenderDrawLine(ren, x0 + 2, y1, x1 - 2, y1);
        SDL_RenderDrawLine(ren, x0, y0 + 2, x0, y1 - 2); SDL_RenderDrawLine(ren, x1, y0 + 2, x1, y1 - 2);
        SDL_RenderDrawPoint(ren, x0 + 1, y0 + 1); SDL_RenderDrawPoint(ren, x1 - 1, y0 + 1);
        SDL_RenderDrawPoint(ren, x0 + 1, y1 - 1); SDL_RenderDrawPoint(ren, x1 - 1, y1 - 1);
    }
    int drawSegments(const std::vector<std::pair<std::string, SDL_Color>>& segs, int x, int y, int scale) {
        for (auto& sg : segs) { font::draw(ren, sg.first, x, y, scale, sg.second); x += font::textWidth(sg.first, scale); }
        return x;
    }

    void renderButton(const Button& b, bool hovered) {
        if (b.style == 3) {   // section header in the tool panel
            font::draw(ren, b.label(), b.r.x + 2, b.r.y + 2, 1, SDL_Color{120, 140, 178, 255});
            SDL_SetRenderDrawColor(ren, 44, 52, 70, 255);
            SDL_RenderDrawLine(ren, b.r.x + font::textWidth(b.label(), 1) + 8, b.r.y + 6, b.r.x + b.r.w, b.r.y + 6);
            return;
        }
        bool act = b.active(), en = b.enabled();
        SDL_Color fill{46, 52, 68, 255}, line{78, 88, 110, 255}, text{232, 236, 245, 255};
        if (hovered && en) { fill = SDL_Color{62, 72, 96, 255}; line = SDL_Color{120, 138, 180, 255}; }
        if (act) { fill = SDL_Color{52, 100, 172, 255}; line = SDL_Color{150, 190, 255, 255}; text = SDL_Color{255, 255, 255, 255}; }
        if (b.style == 1 && act) { fill = SDL_Color{36, 132, 78, 255}; line = SDL_Color{120, 230, 160, 255}; }
        if (b.style == 1 && !act) { fill = hovered ? SDL_Color{44, 112, 74, 255} : SDL_Color{36, 88, 62, 255}; line = SDL_Color{90, 170, 120, 255}; }
        if (b.style == 2 && en) { fill = hovered ? SDL_Color{190, 70, 70, 255} : SDL_Color{150, 56, 56, 255}; line = SDL_Color{230, 130, 130, 255}; }
        if (!en) { fill = SDL_Color{34, 38, 48, 255}; line = SDL_Color{48, 54, 68, 255}; text = SDL_Color{98, 106, 124, 255}; }
        rrect(b.r, fill);
        rrectLine(b.r, line);
        std::string lab = b.label();
        int tw = font::textWidth(lab, 2);
        int tx = b.r.x + (b.r.w - tw) / 2;
        if (tw > b.r.w - 6) { lab = lab.substr(0, std::max<size_t>(1, (size_t)(b.r.w - 6) / 12)); tx = b.r.x + 3; }
        font::draw(ren, lab, tx, b.r.y + (b.r.h - 14) / 2, 2, text);
    }

    void renderPItem(const PItem& it, bool hovered) {
        switch (it.kind) {
            case 0:   // header
                font::draw(ren, it.text, it.r.x, it.r.y + 2, 2, SDL_Color{255, 214, 120, 255});
                SDL_SetRenderDrawColor(ren, 52, 60, 80, 255);
                SDL_RenderDrawLine(ren, it.r.x, it.r.y + 18, it.r.x + it.r.w, it.r.y + 18);
                break;
            case 1: {   // swatch
                SDL_Rect r = it.r;
                rrect(r, SDL_Color{22, 26, 36, 255});
                SDL_Rect in{r.x + 3, r.y + 3, r.w - 6, r.h - 6};
                SDL_Color c = rgb(it.color);
                SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
                SDL_RenderFillRect(ren, &in);
                if (it.text == "ERASER") { SDL_SetRenderDrawColor(ren, 255, 255, 255, 255); SDL_RenderDrawLine(ren, in.x + 3, in.y + 3, in.x + in.w - 4, in.y + in.h - 4); SDL_RenderDrawLine(ren, in.x + in.w - 4, in.y + 3, in.x + 3, in.y + in.h - 4); }
                rrectLine(r, it.active ? SDL_Color{255, 255, 255, 255} : (hovered ? SDL_Color{170, 190, 235, 255} : SDL_Color{60, 68, 90, 255}));
                if (it.active) { SDL_Rect r2{r.x + 1, r.y + 1, r.w - 2, r.h - 2}; rrectLine(r2, SDL_Color{255, 255, 255, 255}); }
                break;
            }
            case 2: {   // button
                SDL_Color fill{46, 52, 68, 255}, line{78, 88, 110, 255}, text{232, 236, 245, 255};
                if (hovered && it.enabled) { fill = SDL_Color{62, 72, 96, 255}; line = SDL_Color{120, 138, 180, 255}; }
                if (it.active) { fill = SDL_Color{52, 100, 172, 255}; line = SDL_Color{150, 190, 255, 255}; text = SDL_Color{255, 255, 255, 255}; }
                if (!it.enabled) { fill = SDL_Color{34, 38, 48, 255}; line = SDL_Color{48, 54, 68, 255}; text = SDL_Color{98, 106, 124, 255}; }
                rrect(it.r, fill); rrectLine(it.r, line);
                std::string lab = it.text;
                int tw = font::textWidth(lab, 2);
                int tx = it.r.x + (it.r.w - tw) / 2;
                if (tw > it.r.w - 6) { lab = lab.substr(0, std::max<size_t>(1, (size_t)(it.r.w - 6) / 12)); tx = it.r.x + 3; }
                font::draw(ren, lab, tx, it.r.y + (it.r.h - 14) / 2, 2, text);
                break;
            }
            case 3:   // text line
                font::draw(ren, it.text, it.r.x, it.r.y, 1, rgb(it.color));
                break;
            case 4: {  // value box
                rrect(it.r, SDL_Color{22, 26, 36, 255});
                rrectLine(it.r, SDL_Color{52, 60, 80, 255});
                int tw = font::textWidth(it.text, 2);
                font::draw(ren, it.text, it.r.x + std::max(4, (it.r.w - tw) / 2), it.r.y + 6, 2, SDL_Color{255, 255, 255, 255});
                break;
            }
            default: break;
        }
    }

    void renderHelp() {
        SDL_Rect card{(SIM_W - 1040) / 2, 40, 1040, SIM_H - 80};
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 150);
        SDL_Rect all{0, 0, SIM_W, SIM_H};
        SDL_RenderFillRect(ren, &all);
        rrect(card, SDL_Color{22, 26, 36, 250});
        rrectLine(card, SDL_Color{120, 150, 210, 255});
        int x = card.x + 24, y = card.y + 18;
        auto head = [&](const std::string& t) { font::draw(ren, t, x, y, 2, SDL_Color{255, 200, 110, 255}); y += 20; };
        const int maxChars = (card.w - 24 - 36 - 12) / 12;
        auto line = [&](const std::string& t) {   // word-wrapped
            size_t pos = 0;
            std::string cur;
            bool first = true;
            auto flush = [&] { font::draw(ren, (first ? "" : "   ") + cur, x + 12, y, 2, SDL_Color{214, 222, 238, 255}); y += 17; cur.clear(); first = false; };
            while (pos < t.size()) {
                size_t e = t.find(' ', pos);
                if (e == std::string::npos) e = t.size();
                std::string w = t.substr(pos, e - pos);
                if (!cur.empty() && (int)(cur.size() + 1 + w.size()) + (first ? 0 : 3) > maxChars) flush();
                cur += (cur.empty() ? "" : " ") + w;
                pos = e + 1;
            }
            if (!cur.empty()) flush();
        };
        font::draw(ren, "SANDBOTS - QUICK HELP", x, y, 3, SDL_Color{255, 255, 255, 255});
        font::draw(ren, "F1 / ESC / CLICK TO CLOSE", card.x + card.w - font::textWidth("F1 / ESC / CLICK TO CLOSE", 2) - 24, y + 4, 2, SDL_Color{150, 165, 195, 255});
        y += 38;
        head("WORKFLOW");
        line("1. DRAW IN EDIT MODE - NOTHING MOVES, SO YOU CAN BUILD CALMLY. CTRL+Z UNDOES ANY EDIT.");
        line("2. PRESS PLAY (SPACE). THE DRAWING IS SNAPSHOT FIRST. PAUSE / STEP WHILE IT RUNS.");
        line("3. PRESS STOP TO GO BACK TO EXACTLY WHAT YOU DREW. SAVE / OPEN KEEP YOUR MACHINES.");
        y += 6;
        head("THE WINDOW");
        line("LEFT: TOOLS, GROUPED. PICK ONE AND DRAW IN THE MIDDLE.   RIGHT: PROPERTIES OF THE TOOL OR SELECTION - MATERIALS ARE SWATCHES YOU CLICK, NOTHING TO SCROLL THROUGH.");
        line("TOP: PLAY / STOP, FILES, UNDO, VIEWS, FOCUS AND SCENES.   BOTTOM: A HINT FOR THE TOOL OR FOR WHATEVER YOU HOVER.");
        y += 6;
        head("EDITING BODIES");
        line("SELECT: CLICK A BODY (CLICK AGAIN TO REACH THE ONE UNDER IT, SHIFT ADDS), DRAG EMPTY SPACE TO BOX-SELECT, DRAG A SELECTED BODY TO MOVE IT.");
        line("CLICKING A MATERIAL SWATCH WHILE BODIES ARE SELECTED CHANGES THEIR MATERIAL. CTRL+C / CTRL+V COPY AND PASTE; DEL DELETES.");
        line("CUT TOOL: DRAG A BOX OR CIRCLE OVER A BODY AND THAT AREA IS CUT OUT OF IT. SUBTRACT: CUT ONE SELECTED BODY (THE RED, LAST-CLICKED ONE) OUT OF THE OTHERS.");
        line("EMITTER: A BODY THAT ENDLESSLY MAKES THE MATERIAL YOU PICK IN THE PANEL. PIN IT, OR LET IT TRAVEL WITH A MACHINE.");
        y += 6;
        head("KEYS");
        line("SPACE PLAY / PAUSE    N STEP    ENTER EXACT-VALUE FORM    F1 HELP    F FOCUS ON THE SELECTED BODY    DEL DELETE");
        line("CTRL+Z UNDO    CTRL+Y REDO    CTRL+C COPY    CTRL+V PASTE    CTRL+A SELECT ALL    CTRL+S SAVE    CTRL+O OPEN    CTRL+N NEW");
        line("CTRL+G GROUP    CTRL+U UNGROUP    T FIXED IN PLACE    H HEAT VIEW    P PRESSURE VIEW    G FLIP GRAVITY    [ ] BRUSH SIZE");
        line("FAN SELECTED: + / - STRENGTH    BACKSLASH FLIPS    M BLOW / VACUUM       E HOLD = SPARK PLUG    , . SPARK PLUG PERIOD");
        line("ARROWS OR A / D DRIVE MOTORS    UP OR W FIRE ROCKETS       MIDDLE-DRAG, THE STRIPS AT THE VIEW EDGES, HOME / END / PAGE UP / PAGE DOWN SCROLL");
        line("ARROW KEYS NUDGE THE SELECTION IN EDIT MODE: 1 CELL, SHIFT = 10 CELLS, CTRL = 1/4 CELL (WHILE PLAYING THEY DRIVE MOTORS AS BEFORE).");
        line("ZOOM: CTRL+WHEEL, CTRL + PLUS / MINUS, OR THE - AND + BUTTONS (CTRL+0 RESETS).    K OR THE GRID BUTTON: REFERENCE GRID, RULERS (CELL NUMBERS) AND A SCALE BAR.");
    }

    // things drawn over the simulation view (the viewport is already the view): messages, mode, readouts
    void renderSimOverlays() {
        if (noteFrames > 0) {
            int w = font::textWidth(note, 2) + 16;
            rrect(SDL_Rect{8, SIM_H - 34, std::min(w, SIM_W - 16), 24}, SDL_Color{16, 20, 28, 225});
            font::draw(ren, note.substr(0, (size_t)(SIM_W - 40) / 12), 16, SIM_H - 29, 2, SDL_Color{255, 214, 140, 255});
        } else if (phys.eventFrames > 0) {
            int w = font::textWidth(phys.lastEvent, 2) + 16;
            rrect(SDL_Rect{8, SIM_H - 34, w, 24}, SDL_Color{16, 20, 28, 225});
            font::draw(ren, phys.lastEvent, 16, SIM_H - 29, 2, SDL_Color{255, 130, 100, 255});
        }
        std::string mode = playing ? (paused ? "PAUSED" : "RUNNING") : "EDIT MODE";
        SDL_Color mc = playing ? (paused ? SDL_Color{255, 190, 90, 255} : SDL_Color{110, 240, 150, 255}) : SDL_Color{130, 190, 255, 255};
        int w = font::textWidth(mode, 2) + 36;
        SDL_Rect pill{SIM_W - w - 8, 8, w, 24};
        rrect(pill, SDL_Color{16, 20, 28, 215});
        rrectLine(pill, mc);
        rrect(SDL_Rect{pill.x + 9, pill.y + 8, 8, 8}, mc);
        font::draw(ren, mode, pill.x + 24, pill.y + 5, 2, mc);
        if (!playing) {
            std::string hint = "PRESS PLAY (SPACE) TO RUN";
            font::draw(ren, hint, SIM_W - font::textWidth(hint, 1) - 10, 38, 1, SDL_Color{140, 160, 200, 255});
        }
        if (focusBody >= 0) {
            std::string f = "FOCUS ON (F RELEASES)";
            font::draw(ren, f, SIM_W - font::textWidth(f, 1) - 10, 50, 1, SDL_Color{255, 214, 90, 255});
        }
        if (world.vMax > 0.f) {
            char eb[96];
            std::snprintf(eb, sizeof eb, "PEAK %.4g V   SOURCE %.3g A   ARCS %ld", world.vMax, world.iSource, world.arcCount);
            font::draw(ren, eb, SIM_W - font::textWidth(eb, 1) - 10, 62, 1, SDL_Color{255, 240, 140, 255});
        }
    }

    void renderModal() {
        if (!scenesOpen) return;
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 140);
        SDL_Rect all{0, 0, SIM_W, SIM_H};
        SDL_RenderFillRect(ren, &all);
        int lx = mousePx - SIM_X, ly = mousePy - SIM_Y;
        for (auto& it : modal) {
            if (it.kind == 5) { rrect(it.r, SDL_Color{22, 26, 36, 250}); rrectLine(it.r, SDL_Color{120, 150, 210, 255}); }
            else if (it.kind == 3) font::draw(ren, it.text, it.r.x, it.r.y, 2, rgb(it.color));
            else if (it.kind == 2) {
                bool hov = lx >= it.r.x && lx < it.r.x + it.r.w && ly >= it.r.y && ly < it.r.y + it.r.h;
                rrect(it.r, hov ? SDL_Color{62, 82, 120, 255} : SDL_Color{46, 52, 68, 255});
                rrectLine(it.r, hov ? SDL_Color{150, 190, 255, 255} : SDL_Color{78, 88, 110, 255});
                int tw = font::textWidth(it.text, 2);
                font::draw(ren, it.text, it.r.x + (it.r.w - tw) / 2, it.r.y + 10, 2, SDL_Color{236, 240, 248, 255});
                if (hov) font::draw(ren, it.tip, 20 + 0, SIM_H - 30, 1, SDL_Color{190, 205, 230, 255});
            }
        }
    }

    // the toolbar, the tool panel, the properties panel and the status bar (window coordinates)
    void renderPanels() {
        layoutButtons();
        SDL_SetRenderDrawColor(ren, 26, 30, 40, 255);
        SDL_Rect topBar{0, 0, WIN_W, TOP_H};
        SDL_RenderFillRect(ren, &topBar);
        SDL_SetRenderDrawColor(ren, 20, 23, 31, 255);
        SDL_Rect left{0, TOP_H, LEFT_W, WIN_H - TOP_H}, right{LEFT_W + SIM_W, TOP_H, RIGHT_W, WIN_H - TOP_H}, bottom{LEFT_W, TOP_H + SIM_H, SIM_W, BOT_H};
        SDL_RenderFillRect(ren, &left); SDL_RenderFillRect(ren, &right); SDL_RenderFillRect(ren, &bottom);
        SDL_SetRenderDrawColor(ren, 62, 72, 100, 255);
        SDL_RenderDrawLine(ren, 0, TOP_H - 1, WIN_W, TOP_H - 1);
        SDL_RenderDrawLine(ren, LEFT_W - 1, TOP_H, LEFT_W - 1, WIN_H);
        SDL_RenderDrawLine(ren, LEFT_W + SIM_W, TOP_H, LEFT_W + SIM_W, WIN_H);
        for (size_t i = 0; i < buttons.size(); ++i) renderButton(buttons[i], (int)i == hoverBtn);
        for (size_t i = 0; i < rp.size(); ++i) renderPItem(rp[i], (int)i == hoverItem);

        // status bar under the view
        const int bx = LEFT_W + 10, sy = TOP_H + SIM_H + 6;
        const SDL_Color gold{255, 214, 120, 255}, dim{118, 128, 150, 255}, cyan{150, 228, 255, 255}, white{236, 240, 248, 255};
        std::vector<std::pair<std::string, SDL_Color>> segs;
        auto sep = [&] { segs.push_back({"   |   ", dim}); };
        segs.push_back({"TOOL ", dim});
        segs.push_back({tool == T_MAT ? (mat == M_EMPTY ? "ERASER" : MATS[mat].name) : TOOL_NAMES[tool], gold});
        sep();
        if (tool == T_MAT) { segs.push_back({"BRUSH ", dim}); segs.push_back({std::to_string(brush), white}); }
        else if (tool == T_PIPE || tool == T_HOSE) { segs.push_back({"DIAMETER ", dim}); segs.push_back({fmt(pipeD), white}); }
        else { segs.push_back({"MATERIAL ", dim}); segs.push_back({MATS[bodyMat].name, white}); }
        if (!sel.empty()) { sep(); segs.push_back({"SELECTED ", dim}); segs.push_back({std::to_string(sel.size()), white}); }
        sep();
        segs.push_back({"X ", dim}); segs.push_back({std::to_string((int)smouse().x), white});
        segs.push_back({"  Y ", dim}); segs.push_back({std::to_string((int)smouse().y), white});
        if (!currentFile.empty()) { sep(); segs.push_back({"FILE ", dim}); segs.push_back({currentFile, white}); }
        drawSegments(segs, bx, sy, 2);
        std::string fpsS = "FPS " + std::to_string((int)fps);
        font::draw(ren, fpsS, LEFT_W + SIM_W - font::textWidth(fpsS, 2) - 10, sy, 2, dim);

        std::string tip;
        if (hoverBtn >= 0 && !buttons[hoverBtn].tip.empty()) tip = buttons[hoverBtn].tip;
        else if (hoverItem >= 0 && !rp[hoverItem].tip.empty()) tip = rp[hoverItem].tip;
        else tip = TOOL_HINTS[tool];
        // wrap the hint over two lines
        const size_t per = (size_t)(SIM_W - 20) / 6;
        std::string l1 = tip.substr(0, per), l2;
        if (tip.size() > per) {
            size_t cut = tip.rfind(' ', per);
            if (cut == std::string::npos) cut = per;
            l1 = tip.substr(0, cut); l2 = tip.substr(cut + 1);
        }
        font::draw(ren, l1, bx, sy + 26, 1, SDL_Color{190, 202, 226, 255});
        if (!l2.empty()) font::draw(ren, l2, bx, sy + 38, 1, SDL_Color{190, 202, 226, 255});
        std::string hover = hoverText();
        if (!hover.empty()) font::draw(ren, hover, LEFT_W + SIM_W - font::textWidth(hover, 2) - 10, sy + 26, 2, cyan);
        font::draw(ren, "CTRL+Z UNDO   CTRL+C / CTRL+V COPY, PASTE   F1 HELP", bx, sy + 54, 1, SDL_Color{90, 100, 124, 255});
    }

    void render() {
        layoutButtons();
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_Rect simRect{SIM_X, SIM_Y, SIM_W, SIM_H};
        SDL_RenderSetViewport(ren, &simRect);      // from here on (0,0) is the corner of the view, and drawing is clipped to it
        renderParticles();
        renderGrid();
        renderBodies();
        renderJoints();
        renderArcs();
        renderSelection();
        renderFocusMark();
        renderLabels();
        renderGhost();
        renderScrollStrip();
        renderForm();
        renderRulers();
        renderSimOverlays();
        if (helpOn) renderHelp();
        renderModal();
        SDL_RenderSetViewport(ren, nullptr);
        renderPanels();
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
    // Headless self-test: sandbots --shot out.bmp [frames] [--scene N] [--heat] [--trace] [--no-air] [--no-momentum]
    const char* shot = nullptr;
    int shotFrames = 300, scene = 0;
    bool heat = false, trace = false, g0 = false, elecFlag = false, helpFlag = false, pressureFlag = false, noAir = false, noMomentum = false;
    int camFlag = -1;
    bool scenesFlag = false; bool timeFlag = false; float zoomFlag = 1.f, camYFlag = 0.f;
    int tabFlag = -1, hoverX = -1, hoverY = -1;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) {
            shot = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') shotFrames = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--scene") && i + 1 < argc) {
            scene = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--heat")) heat = true;
        else if (!std::strcmp(argv[i], "--trace")) trace = true;
        else if (!std::strcmp(argv[i], "--elec")) elecFlag = true;
        else if (!std::strcmp(argv[i], "--help-card")) helpFlag = true;
        else if (!std::strcmp(argv[i], "--pressure")) pressureFlag = true;
        else if (!std::strcmp(argv[i], "--cam") && i + 1 < argc) camFlag = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--tool") && i + 1 < argc) tabFlag = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--scenes")) scenesFlag = true;
        else if (!std::strcmp(argv[i], "--time")) timeFlag = true;
        else if (!std::strcmp(argv[i], "--zoom") && i + 1 < argc) zoomFlag = (float)std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--camy") && i + 1 < argc) camYFlag = (float)std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--jet") && i + 1 < argc) {   // --jet key=value,key=value
            std::string kv = argv[++i];
            size_t p = 0;
            while (p < kv.size()) {
                size_t e = kv.find(',', p); if (e == std::string::npos) e = kv.size();
                std::string item = kv.substr(p, e - p); size_t q = item.find('=');
                if (q != std::string::npos) {
                    std::string k = item.substr(0, q); float v = (float)std::atof(item.c_str() + q + 1); auto& J = Game::jet();
                    if (k == "fan") J.fan = v; else if (k == "fuel") J.fuel = v; else if (k == "fuelU") J.fuelU = v; else if (k == "plugU") J.plugU = v;
                    else if (k == "len") J.len = v; else if (k == "noz") J.nozIn = v; else if (k == "nozLen") J.nozLen = v; else if (k == "half") J.half = v;
                    else if (k == "spark") J.spark = (int)v; else if (k == "space") J.space = v > 0.5f;
                }
                p = e + 1;
            }
        }
        else if (!std::strcmp(argv[i], "--hover") && i + 2 < argc) { hoverX = std::atoi(argv[i + 1]); hoverY = std::atoi(argv[i + 2]); i += 2; }
        else if (!std::strcmp(argv[i], "--g0")) g0 = true;
        else if (!std::strcmp(argv[i], "--no-air")) noAir = true;
        else if (!std::strcmp(argv[i], "--no-momentum")) noMomentum = true;
    }
    if (shot) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);

    Game g;
    if (!g.init(shot != nullptr)) return 1;
    g.world.needAir = !noAir;              // --no-air: the old model, fuel burns without oxygen
    g.world.gasMomentum = !noMomentum;   // --no-momentum: the diffusion-only gas model, for comparing scenes

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
            case 22: g.buildFanTest(); break;
            case 23: g.buildRoadTest(); break;
            case 26: g.buildJetCar(); break;
            case 27: g.buildJetCar(); break;
            case 24: {   // camera controls through real SDL events
                g.buildRoadTest();
                g.focusBody = -1;
                auto push = [&](SDL_Event e) { SDL_PushEvent(&e); g.handleEvents(); };
                SDL_Event e{};
                e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_MIDDLE; e.button.x = SIM_X + 600; e.button.y = SIM_Y + 300; push(e);
                e = SDL_Event{}; e.type = SDL_MOUSEMOTION; e.motion.x = SIM_X + 300; e.motion.y = SIM_Y + 300; push(e);
                e = SDL_Event{}; e.type = SDL_MOUSEBUTTONUP; e.button.button = SDL_BUTTON_MIDDLE; e.button.x = SIM_X + 300; e.button.y = SIM_Y + 300; push(e);
                std::printf("middle-drag 300px left: camera at %d (expected 100)\n", g.camX);
                e = SDL_Event{}; e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT; e.button.x = SIM_X + 900; e.button.y = SIM_Y + SIM_H - 4; push(e);
                e = SDL_Event{}; e.type = SDL_MOUSEBUTTONUP; e.button.button = SDL_BUTTON_LEFT; e.button.x = SIM_X + 900; e.button.y = SIM_Y + SIM_H - 4; push(e);
                std::printf("scroll strip click at 900px: camera at %d (expected %d)\n", g.camX, (int)std::lround(900.f / SIM_W * World::W - g.viewW() * 0.5f));
                e = SDL_Event{}; e.type = SDL_KEYDOWN; e.key.keysym.sym = SDLK_HOME; push(e);
                std::printf("Home: camera at %d\n", g.camX);
                g.selectBody(g.phys.bodies.size() > 0 ? 0 : -1, false, false);
                e = SDL_Event{}; e.type = SDL_KEYDOWN; e.key.keysym.sym = SDLK_f; push(e);
                std::printf("F with a selection: focus body %d\n", g.focusBody);
                g.play();
                for (int i = 0; i < 600; ++i) g.update();
                bool ok; Vec2 f = g.focusPoint(ok);
                std::printf("after 600 frames the car is at x=%.0f and the camera at %d (view centre %d)\n", f.x, g.camX, g.camX + (int)(g.viewW() / 2));
                e = SDL_Event{}; e.type = SDL_KEYDOWN; e.key.keysym.sym = SDLK_f; push(e);
                std::printf("F again: focus body %d\n", g.focusBody);
                break;
            }
            case 25: {   // editing through real mouse and keyboard events: select, cycle, move, copy/paste, undo, cut
                g.resetWorld();
                g.undoStack.clear();
                auto push = [&](SDL_Event e) { SDL_PushEvent(&e); g.handleEvents(); };
                auto px = [&](float x) { return g.scrX(x); };
                auto py = [&](float y) { return g.scrY(y); };
                auto mouseTo = [&](float x, float y) { SDL_Event e{}; e.type = SDL_MOUSEMOTION; e.motion.x = px(x); e.motion.y = py(y); push(e); };
                auto down = [&](float x, float y) { mouseTo(x, y); SDL_Event e{}; e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT; e.button.x = px(x); e.button.y = py(y); push(e); };
                auto up = [&](float x, float y) { SDL_Event e{}; e.type = SDL_MOUSEBUTTONUP; e.button.button = SDL_BUTTON_LEFT; e.button.x = px(x); e.button.y = py(y); push(e); };
                auto drag = [&](float x0, float y0, float x1, float y1) { down(x0, y0); mouseTo((x0 + x1) / 2, (y0 + y1) / 2); g.update(); mouseTo(x1, y1); g.update(); up(x1, y1); };
                auto key = [&](SDL_Keycode k, Uint16 mod = 0) { SDL_SetModState((SDL_Keymod)mod); SDL_Event e{}; e.type = SDL_KEYDOWN; e.key.keysym.sym = k; push(e); SDL_SetModState(KMOD_NONE); };
                auto alive = [&]() { int n = 0; for (auto& b : g.phys.bodies) n += b.alive; return n; };
                g.tool = T_BOX;
                drag(100, 100, 160, 130);                       // a box 60 x 30
                g.tool = T_CIRCLE;
                drag(130, 115, 130, 125);                       // a circle of radius 10 on top of it
                std::printf("drew a box and a circle: %d bodies\n", alive());
                g.tool = T_SELECT;
                down(130, 115); up(130, 115);
                int first = g.primary;
                down(130, 115); up(130, 115);
                int second = g.primary;
                std::printf("click on the overlap twice selects two different bodies: %s (%d then %d)\n", first != second && first >= 0 && second >= 0 ? "yes" : "NO", first, second);
                Vec2 before = g.phys.bodies[g.primary].pos;
                drag(130, 115, 190, 135);                       // drag the selected body
                Vec2 after = g.phys.bodies[g.primary].pos;
                std::printf("dragging the selection moved it by (%.0f, %.0f) (expected 60, 20)\n", after.x - before.x, after.y - before.y);
                key(SDLK_z, KMOD_CTRL);
                std::printf("ctrl+z undoes the move: back at (%.0f, %.0f)\n", g.phys.bodies[g.primary].pos.x, g.phys.bodies[g.primary].pos.y);
                key(SDLK_y, KMOD_CTRL);
                std::printf("ctrl+y redoes it: (%.0f, %.0f)\n", g.phys.bodies[g.primary].pos.x, g.phys.bodies[g.primary].pos.y);
                int n0 = alive();
                key(SDLK_c, KMOD_CTRL);
                mouseTo(300, 150);
                key(SDLK_v, KMOD_CTRL);
                std::printf("ctrl+c / ctrl+v: %d -> %d bodies, pasted body at (%.0f, %.0f)\n", n0, alive(), g.phys.bodies[g.primary].pos.x, g.phys.bodies[g.primary].pos.y);
                key(SDLK_z, KMOD_CTRL);
                std::printf("ctrl+z removes the paste: %d bodies\n", alive());
                g.clearSelection();
                g.tool = T_BOX;
                drag(200, 60, 260, 90);                         // a fresh box to cut: x 200..260, y 60..90
                g.cutCircle = true;
                g.tool = T_CUT;
                drag(230, 75, 230, 67);                         // cut a radius-8 circle out of its middle
                int cutPieces = 0;
                for (auto& b : g.phys.bodies) if (b.alive && b.group >= 0) ++cutPieces;
                auto covered = [&](float x, float y) { for (auto& b : g.phys.bodies) if (b.alive && b.contains(Vec2(x, y))) return true; return false; };
                std::printf("cut tool: the box became %d welded pieces; centre of the circle is empty: %s; just outside it is solid: %s\n",
                            cutPieces, !covered(230, 75) ? "yes" : "NO", covered(230, 63) && covered(242, 75) ? "yes" : "NO");
                key(SDLK_z, KMOD_CTRL);
                std::printf("ctrl+z restores the box: centre solid again: %s\n", covered(230, 75) ? "yes" : "NO");
                key(SDLK_y, KMOD_CTRL);
                std::printf("ctrl+y cuts it again: centre empty: %s\n", !covered(230, 75) ? "yes" : "NO");
                // subtract with two selected bodies (the circle laid on a box), cutter removed by default
                g.clearSelection();
                g.tool = T_BOX; drag(280, 60, 340, 90);
                g.tool = T_CIRCLE; drag(310, 75, 310, 67);
                g.tool = T_SELECT;
                down(310, 75); up(310, 75);                    // the circle (top) first...
                down(295, 65); up(295, 65);                    // ...then the box; the box is last-clicked, so make the circle the cutter:
                key(SDLK_z, 0);
                std::vector<int> two;
                for (auto& b : g.phys.bodies) if (b.alive && b.pos.x > 275 && b.pos.x < 345) two.push_back(b.id);
                g.sel = two;
                for (int id : two) if (g.phys.bodies[id].shape == SHAPE_CIRCLE) g.primary = id;
                int before2 = 0; for (auto& b : g.phys.bodies) before2 += b.alive;
                g.cutSelection();
                std::printf("subtract selection: circle %s, centre of the hole empty: %s\n", g.keepCutter ? "kept" : "consumed", !covered(310, 75) ? "yes" : "NO");
                (void)before2;
                break;
            }
            case 28: {   // paraffin bonds: they hold a heavy block, give way when warmed, and melt in a flame
                g.resetWorld();
                g.rect(0, 200, World::W - 1, 203, M_WALL);
                g.phys.addBox(Vec2(60, 60), Vec2(50, 3), 0, M_STEEL, true);            // ledge
                int cold = g.phys.addBox(Vec2(30, 76), Vec2(14, 10), 0, M_STEEL, false);      // heavy block, wax glued under the ledge
                g.phys.addBond(Vec2(30, 63), cold, 0, BOND_TEMP[0], 0.f, BOND_G[0]);
                int warm = g.phys.addBox(Vec2(90, 76), Vec2(14, 10), 0, M_STEEL, false);
                g.phys.addBond(Vec2(90, 63), warm, 0, BOND_TEMP[0], 0.f, BOND_G[0]);
                g.rect(105, 68, 111, 84, M_HEATER);                                        // a heater next to the second block
                // two stacked blocks glued with wax, the lower one resting on the ground
                int low = g.phys.addBox(Vec2(160, 188), Vec2(12, 8), 0, M_STEEL, false);
                int top = g.phys.addBox(Vec2(160, 172), Vec2(10, 8), 0, M_STEEL, false);
                g.phys.addBond(Vec2(160, 180), top, low, BOND_TEMP[0], 0.f, BOND_G[0]);
                g.phys.stampBodies();
                g.play();
                auto st = [&](int id) { return g.phys.bodies[id].pos; };
                for (int f = 0; f <= 900; ++f) {
                    g.update();
                    if (f % 100 == 0)
                        std::printf("f=%d cold block y=%.1f (hangs at 76), warm block y=%.1f temp %.0f, top block y=%.1f, bonds broken so far %ld\n", f, st(cold).y, st(warm).y, g.phys.bodies[warm].temp, st(top).y, g.phys.bondsBroken);
                }
                break;
            }
            case 30: {   // joints through real clicks: touching bodies bond, a piston slides in its host, a spring is selected and edited
                g.resetWorld();
                g.undoStack.clear();
                auto push = [&](SDL_Event e) { SDL_PushEvent(&e); g.handleEvents(); };
                auto px = [&](float x) { return g.scrX(x); };
                auto py = [&](float y) { return g.scrY(y); };
                auto mouseTo = [&](float x, float y) { SDL_Event e{}; e.type = SDL_MOUSEMOTION; e.motion.x = px(x); e.motion.y = py(y); push(e); };
                auto down = [&](float x, float y) { mouseTo(x, y); SDL_Event e{}; e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT; e.button.x = px(x); e.button.y = py(y); push(e); };
                auto up = [&](float x, float y) { SDL_Event e{}; e.type = SDL_MOUSEBUTTONUP; e.button.button = SDL_BUTTON_LEFT; e.button.x = px(x); e.button.y = py(y); push(e); };
                auto drag = [&](float x0, float y0, float x1, float y1) { down(x0, y0); mouseTo((x0 + x1) / 2, (y0 + y1) / 2); g.update(); mouseTo(x1, y1); g.update(); up(x1, y1); };
                auto clickWin = [&](int wx, int wy) { SDL_Event e{}; e.type = SDL_MOUSEMOTION; e.motion.x = wx; e.motion.y = wy; push(e); e = SDL_Event{}; e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT; e.button.x = wx; e.button.y = wy; push(e); e = SDL_Event{}; e.type = SDL_MOUSEBUTTONUP; e.button.button = SDL_BUTTON_LEFT; e.button.x = wx; e.button.y = wy; push(e); };
                auto key = [&](SDL_Keycode k, Uint16 mod = 0) { SDL_SetModState((SDL_Keymod)mod); SDL_Event e{}; e.type = SDL_KEYDOWN; e.key.keysym.sym = k; push(e); SDL_SetModState(KMOD_NONE); };
                g.anchored = false;
                // 1. two boxes that touch (do not overlap), bonded by clicking on their seam
                int A = g.phys.addBox(Vec2(60, 100), Vec2(15, 8), 0, M_STEEL, false);
                int Bx = g.phys.addBox(Vec2(91, 100), Vec2(15, 8), 0, M_STEEL, false);   // 1-cell gap, as touching bodies sit
                g.phys.stampBodies();
                g.tool = T_BOND;
                down(75.5f, 100); up(75.5f, 100);
                int bonds = 0; for (auto& j : g.phys.joints) if (j.alive && j.bondId >= 0) ++bonds;
                int worldPins = 0; for (auto& j : g.phys.joints) if (j.alive && j.bondId >= 0 && (j.b < 0 || j.a < 0)) ++worldPins;
                std::printf("bond tool on a seam between touching boxes: %d pins, %d of them to the world (expect 2 and 0)\n", bonds, worldPins);
                g.phys.bodies[A].vel = Vec2(0, 0);
                g.tool = T_SELECT;
                for (int i = 0; i < 40; ++i) g.update();   // let them settle, then shove one
                g.phys.bodies[A].vel = Vec2(-40, 0);
                float gap0 = g.phys.bodies[Bx].pos.x - g.phys.bodies[A].pos.x;
                g.play();
                for (int i = 0; i < 60; ++i) g.update();
                float gap1 = g.phys.bodies[Bx].pos.x - g.phys.bodies[A].pos.x;
                std::printf("shoved one box: the other followed: centre gap %.1f -> %.1f, bonds broken %ld\n", gap0, gap1, g.phys.bondsBroken);
                g.stopPlay();
                // 2. a piston inside a host body, slider drawn by dragging from the piston along the host
                g.resetWorld();
                g.undoStack.clear();
                int host = g.phys.addBox(Vec2(200, 100), Vec2(40, 10), 0, M_STEEL, false);
                int piston = g.phys.addBox(Vec2(190, 130), Vec2(5, 5), 0, M_STEEL, false);   // beside it
                g.phys.stampBodies();
                g.tool = T_SLIDER;
                drag(190, 130, 215, 100);   // press on the piston, release over the host
                int rel = 0; for (auto& j : g.phys.joints) if (j.alive && j.type == J_SLIDER && j.b == host && j.a == piston) ++rel;
                std::printf("slider tool: piston-to-host slider created: %d (expect 1)\n", rel);
                g.play();
                for (int i = 0; i < 90; ++i) g.update();
                std::printf("after 90 frames the host fell to y=%.0f and the piston to y=%.0f; their vertical gap %.1f (started 30)\n", g.phys.bodies[host].pos.y, g.phys.bodies[piston].pos.y, g.phys.bodies[piston].pos.y - g.phys.bodies[host].pos.y);
                g.stopPlay();
                // 3. a spring between two bodies: select it, change its stiffness and rest length from the panel, delete it
                g.resetWorld();
                g.undoStack.clear();
                int s1 = g.phys.addBox(Vec2(100, 60), Vec2(8, 8), 0, M_STEEL, true);
                int s2 = g.phys.addBox(Vec2(160, 60), Vec2(8, 8), 0, M_STEEL, false);
                (void)s1; (void)s2;
                g.phys.stampBodies();
                g.tool = T_SPRING;
                drag(100, 60, 160, 60);
                int sj = -1; for (auto& j : g.phys.joints) if (j.alive && j.type == J_DISTANCE) sj = j.id;
                std::printf("spring tool: spring %s, rest %.0f, %.2f Hz\n", sj >= 0 ? "made" : "NOT made", sj >= 0 ? g.phys.joints[sj].length : 0.f, sj >= 0 ? g.phys.joints[sj].freq : 0.f);
                g.tool = T_SELECT;
                down(130, 60); up(130, 60);
                std::printf("click on the spring selects it: %s\n", g.selJoint == sj ? "yes" : "NO");
                g.layoutButtons();
                float f0 = g.phys.joints[sj].freq, l0 = g.phys.joints[sj].length, d0 = g.phys.joints[sj].damping;
                auto pressStepper = [&](const char* label, int which /*0 minus, 1 plus*/) {
                    g.layoutButtons();
                    for (size_t i = 0; i + 2 < g.rp.size(); ++i)
                        if (g.rp[i].kind == 3 && g.rp[i].text.rfind(label, 0) == 0) {
                            // the line is followed by - value + ; find the minus button after it
                            for (size_t k = i + 1; k + 2 < g.rp.size(); ++k)
                                if (g.rp[k].kind == 2 && g.rp[k].text == "-") { const SDL_Rect& r = g.rp[k + 2 * which + (which ? 1 : 0)].r; if (which == 0) { clickWin(r.x + r.w / 2, r.y + r.h / 2); } else { const SDL_Rect& rp2 = g.rp[k + 2].r; clickWin(rp2.x + rp2.w / 2, rp2.y + rp2.h / 2); } return true; }
                        }
                    return false;
                };
                bool a1 = pressStepper("STIFFNESS", 1), a2 = pressStepper("DAMPING", 1), a3 = pressStepper("REST LENGTH", 0);
                std::printf("panel steppers found: %d %d %d; stiffness %.2f -> %.2f, damping %.2f -> %.2f, rest length %.0f -> %.0f\n", a1, a2, a3, f0, g.phys.joints[sj].freq, d0, g.phys.joints[sj].damping, l0, g.phys.joints[sj].length);
                key(SDLK_DELETE);
                std::printf("Delete removes the selected spring: %s\n", g.phys.joints[sj].alive ? "NO" : "yes");
                key(SDLK_z, KMOD_CTRL);
                std::printf("Ctrl+Z brings it back: %s\n", g.phys.joints[sj].alive ? "yes" : "NO");
                break;
            }
            case 33: {
                g.resetWorld();
                g.phys.addBox(Vec2(100, 60), Vec2(8, 8), 0, M_STEEL, true);
                int b2 = g.phys.addBox(Vec2(160, 60), Vec2(8, 8), 0, M_STEEL, false);
                int sj = g.phys.addDistance(0, Vec2(100, 60), b2, Vec2(160, 60), 3.f);
                g.phys.stampBodies();
                g.tool = T_SELECT; g.selectJoint(sj);
                break;
            }
            case 36: {   // arrow keys nudge the selection in edit mode: a body, a group, a box-selection
                g.resetWorld();
                g.undoStack.clear();
                auto push = [&](SDL_Event e) { SDL_PushEvent(&e); g.handleEvents(); };
                auto key = [&](SDL_Keycode k, Uint16 mod = 0) { SDL_SetModState((SDL_Keymod)mod); SDL_Event e{}; e.type = SDL_KEYDOWN; e.key.keysym.sym = k; push(e); SDL_SetModState(KMOD_NONE); };
                int a = g.phys.addBox(Vec2(100, 100), Vec2(10, 5), 0, M_STEEL, false);
                int b = g.phys.addCircle(Vec2(130, 100), 6.f, M_STEEL, false, false);
                int c = g.phys.addBox(Vec2(160, 100), Vec2(5, 5), 0, M_STEEL, false);
                g.phys.addPin(Vec2(115, 100), a, b, false, false);
                g.phys.stampBodies();
                g.tool = T_SELECT;
                g.selectBody(a, false, false);
                key(SDLK_RIGHT); key(SDLK_RIGHT); key(SDLK_DOWN);
                std::printf("one body, Right Right Down: at (%.2f, %.2f) (expected 102, 101)\n", g.phys.bodies[a].pos.x, g.phys.bodies[a].pos.y);
                key(SDLK_LEFT, KMOD_SHIFT);
                std::printf("Shift+Left: x %.2f (expected 92)\n", g.phys.bodies[a].pos.x);
                key(SDLK_UP, KMOD_CTRL);
                std::printf("Ctrl+Up: y %.2f (expected 100.75)\n", g.phys.bodies[a].pos.y);
                key(SDLK_z, KMOD_CTRL);
                std::printf("Ctrl+Z undoes the whole run of nudges: (%.2f, %.2f) (expected 100, 100)\n", g.phys.bodies[a].pos.x, g.phys.bodies[a].pos.y);
                // a box selection of the two pinned bodies moves together, the pin keeping them joined
                g.clearSelection();
                g.sel = {a, b}; g.primary = a;
                float gap0 = g.phys.bodies[b].pos.x - g.phys.bodies[a].pos.x;
                for (int i = 0; i < 5; ++i) key(SDLK_RIGHT);
                std::printf("two pinned bodies selected, Right x5: a at %.1f, b at %.1f, spacing %.1f -> %.1f, the third body stayed at %.1f\n", g.phys.bodies[a].pos.x, g.phys.bodies[b].pos.x, gap0, g.phys.bodies[b].pos.x - g.phys.bodies[a].pos.x, g.phys.bodies[c].pos.x);
                // a group
                g.clearSelection();
                g.sel = {b, c}; g.primary = b;
                g.groupSelection();
                g.selectBody(c, false, false);
                float cx0 = g.phys.bodies[c].pos.x, bx0 = g.phys.bodies[b].pos.x;
                key(SDLK_DOWN); key(SDLK_DOWN);
                std::printf("a group (clicked as a whole), Down x2: c moved %.1f, b moved %.1f in y\n", g.phys.bodies[c].pos.y - 100.f, g.phys.bodies[b].pos.y - 100.f);
                (void)cx0; (void)bx0;
                // while playing, the arrow keys leave the selection alone
                g.play();
                float yb = g.phys.bodies[c].pos.y;
                key(SDLK_DOWN);
                std::printf("while playing an arrow key does not nudge: %s\n", g.phys.bodies[c].pos.y == yb ? "yes" : "NO");
                g.stopPlay();
                break;
            }
            case 35: {   // zoom: the mouse still lands on the right cell, Ctrl+wheel zooms about the pointer, drawing works zoomed in
                g.resetWorld();
                g.undoStack.clear();
                auto push = [&](SDL_Event e) { SDL_PushEvent(&e); g.handleEvents(); };
                auto mouseTo = [&](float x, float y) { SDL_Event e{}; e.type = SDL_MOUSEMOTION; e.motion.x = g.scrX(x); e.motion.y = g.scrY(y); push(e); };
                auto down = [&](float x, float y) { mouseTo(x, y); SDL_Event e{}; e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT; e.button.x = g.scrX(x); e.button.y = g.scrY(y); push(e); };
                auto up = [&](float x, float y) { SDL_Event e{}; e.type = SDL_MOUSEBUTTONUP; e.button.button = SDL_BUTTON_LEFT; e.button.x = g.scrX(x); e.button.y = g.scrY(y); push(e); };
                g.setCam(300.f, 60.f);
                int px = SIM_X + 540, py = SIM_Y + 300;
                mouseTo(0, 0);
                { SDL_Event e{}; e.type = SDL_MOUSEMOTION; e.motion.x = px; e.motion.y = py; push(e); }
                Vec2 before = g.toWorld(px, py);
                SDL_SetModState(KMOD_CTRL);
                for (int i = 0; i < 4; ++i) { SDL_Event e{}; e.type = SDL_MOUSEWHEEL; e.wheel.y = 1; push(e); }
                SDL_SetModState(KMOD_NONE);
                Vec2 after = g.toWorld(px, py);
                std::printf("Ctrl+wheel x4: zoom %.1fx; the cell under the pointer was (%.1f, %.1f) and is (%.1f, %.1f)\n", g.zoom, before.x, before.y, after.x, after.y);
                g.setCam(300.f, 60.f);
                g.tool = T_BOX; g.anchored = true;
                down(320, 90); { mouseTo(335, 98); g.update(); mouseTo(340, 100); g.update(); } up(340, 100);
                int made = -1; for (auto& b : g.phys.bodies) if (b.alive) made = b.id;
                if (made >= 0) std::printf("a box dragged from (320,90) to (340,100) at %.1fx zoom: centre (%.1f, %.1f), size %.1f x %.1f (expected 330, 95, 20 x 10)\n", g.zoom, g.phys.bodies[made].pos.x, g.phys.bodies[made].pos.y, g.phys.bodies[made].half.x * 2, g.phys.bodies[made].half.y * 2);
                else std::printf("no box was made\n");
                // the vertical strip moves the view up and down
                float cy0 = g.camYf;
                { SDL_Event e{}; e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT; e.button.x = SIM_X + SIM_W - 5; e.button.y = SIM_Y + 40; push(e); e = SDL_Event{}; e.type = SDL_MOUSEBUTTONUP; e.button.button = SDL_BUTTON_LEFT; e.button.x = SIM_X + SIM_W - 5; e.button.y = SIM_Y + 40; push(e); }
                std::printf("click near the top of the vertical strip: view top moved from y=%.0f to y=%.0f\n", cy0, g.camYf);
                g.zoomReset();
                std::printf("reset: zoom %.1f, view top y=%.0f\n", g.zoom, g.camYf);
                g.zoom = 4.f; g.setCam(300.f, 60.f);
                break;
            }
            case 34: {
                g.resetWorld();
                int e = g.phys.addBox(Vec2(100, 100), Vec2(6, 6), 0, M_STEEL, true);
                g.phys.bodies[e].src = Emitter{true, M_WATER, 60.f, 0.f, 1};
                g.phys.addPipe(Vec2(110, 100), Vec2(180, 100), 12.f, 2.f, M_STEEL, true);
                g.phys.stampBodies();
                g.tool = T_SELECT; g.selectBody(e, false, false);
                break;
            }
            case 32: g.buildShotgun(); break;
            case 31: {   // the shotgun, run
                g.buildShotgun();
                g.play();
                for (int f = 0; f <= 600; ++f) {
                    g.update();
                    if (f % 20 == 0 || f == 5) {
                        const Body& w = g.phys.bodies[g.lastGun.wad]; const Body& h = g.phys.bodies[g.lastGun.hammer];
                        float pmax = 0; int gas = 0; double amt = 0, fire = 0;
                        for (int y = 108; y <= 138; ++y) for (int x = 126; x <= 250; ++x) { const Cell& c = g.world.cells[y * World::W + x]; if (MATS[c.t].kind == K_GAS) { pmax = std::max(pmax, c.amt * (c.temp + 273.f) / 293.f); ++gas; amt += c.amt; } if (c.t == M_FIRE) ++fire; }
                        int gp = 0; for (auto& c : g.world.cells) if (c.t == M_GUNPOWDER) ++gp;
                        float vs = 0; for (int id : g.lastGun.shot) vs = std::max(vs, g.phys.bodies[id].vel.x);
                        std::printf("f=%3d hammer x=%.0f vx=%.0f | powder cells %d gas cells %d total %.0f maxP %.1f | wad x=%.0f vx=%.0f | shot vx max %.0f | primer %s\n", f, h.pos.x, h.vel.x, gp, gas, amt, pmax, w.pos.x, w.vel.x, vs, g.phys.lastEvent.c_str());
                    }
                }
                break;
            }
            case 29: {   // the BONDS scene, run
                g.buildBondTest();
                g.play();
                for (int f = 0; f <= 900; ++f) {
                    g.update();
                    if (f % 150 == 0) {
                        std::printf("f=%d broken=%ld last=%s |", f, g.phys.bondsBroken, g.phys.lastEvent.c_str());
                        for (auto& b : g.phys.bodies) if (b.alive && !b.isStatic) std::printf(" [%d %.0f,%.0f T%.0f]", b.id, b.pos.x, b.pos.y, b.temp);
                        std::printf("\n");
                    }
                }
                break;
            }
            case 13: g.buildPrecisionTest(); g.tool = Tool::T_HOSE; g.clearSelection(); g.openForm(); break;
            default: g.buildTestScene(scene); break;
        }
        if (heat) g.heatView = true;
        if (elecFlag) g.elecView = true;
        if (helpFlag) g.helpOn = true;
        if (pressureFlag) g.pressureView = true;
        g.zoom = zoomFlag;
        if (camFlag >= 0) g.setCam((float)camFlag, camYFlag); else g.setCam(g.camXf, camYFlag);
        if (tabFlag >= 0) g.tool = (Tool)tabFlag;
        if (scenesFlag) g.scenesOpen = true;
        if (hoverX >= 0) { g.mousePx = hoverX; g.mousePy = hoverY; }
        if (g0) g.phys.gravity = Vec2(0, 0);
        double tPhys = 0, tWorld = 0;
        auto nowS = [] { return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency(); };
        for (int f = 0; f < shotFrames; ++f) {
            g.phys.motorInput = (scene == 0 && f > 40) ? 1.f : 0.f;
            if (scene == 1 || scene == 2) g.phys.thrustOn = scene == 2;
            double t0 = nowS();
            g.phys.step(1.f / 60.f);
            double t1 = nowS();
            g.world.step();
            tPhys += t1 - t0; tWorld += nowS() - t1;
            g.updateCamera(f == 0);
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
        if (timeFlag) { double r0 = nowS(); g.render(); double r1 = nowS(); int n = 0; for (auto& c : g.world.cells) n += c.t != M_EMPTY; std::printf("   grid split ms: electricity %.2f, heat %.2f, cell updates %.2f, gas flow %.2f, liquid pressure %.2f\n", g.world.prof[0] / 1000 / g.world.profN, g.world.prof[1] / 1000 / g.world.profN, g.world.prof[2] / 1000 / g.world.profN, g.world.prof[3] / 1000 / g.world.profN, g.world.prof[4] / 1000 / g.world.profN); std::printf("TIME per frame: bodies+emitters+fans %.2f ms, grid %.2f ms, render %.2f ms; %d non-empty cells\n", 1000 * tPhys / shotFrames, 1000 * tWorld / shotFrames, 1000 * (r1 - r0), n); }
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
        acc = std::min(acc + frame, 2.2 * dt);   // never more than two steps behind: a slow machine runs in slow motion instead of spiralling
        while (acc >= dt) { g.update(); acc -= dt; }
        g.render();
        SDL_RenderPresent(g.ren);
        SDL_Delay(1);
    }
    g.shutdown();
    return 0;
}
