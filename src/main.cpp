// SandBots: a physics sandbox (rigid bodies, joints, motors) fused with a
// falling-sand / fluid powder toy.
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "font.hpp"
#include "physics.hpp"
#include "sand.hpp"

namespace {

constexpr int S = 3;                       // screen pixels per sand cell
constexpr int SIM_W = World::W * S;
constexpr int SIM_H = World::H * S;
constexpr int ROWS = 4, BTN_H = 26, BTN_GAP = 3;
constexpr int UI_H = ROWS * (BTN_H + BTN_GAP) + 34;
constexpr int WIN_W = SIM_W, WIN_H = SIM_H + UI_H;
constexpr float PI = 3.14159265f;
constexpr uint32_t BG = 0x0b0e14;

enum Tool {
    T_MAT, T_BOX, T_CIRCLE, T_WHEEL, T_ROCKET, T_PIN, T_MOTOR, T_AUTOMOTOR, T_ROD, T_SPRING, T_GRAB, T_DELETE
};

const char* TOOL_NAMES[] = {"PARTICLES", "BOX", "CIRCLE", "WHEEL", "ROCKET", "PIN JOINT", "MOTOR (ARROWS)",
                            "AUTO MOTOR", "ROD", "SPRING", "GRAB", "DELETE"};
const char* TOOL_HINTS[] = {
    "LMB: PAINT   RMB: ERASE   WHEEL: BRUSH SIZE",
    "DRAG TO SIZE A BOX (CLICK = DEFAULT)",
    "DRAG FROM CENTRE TO SET RADIUS",
    "DRAG TO SIZE A WHEEL. IT AUTO-MOTORS ONTO A BODY UNDER ITS CENTRE (ARROWS DRIVE)",
    "DRAG TO SET THRUST DIRECTION. HOLD UP/W TO FIRE",
    "CLICK WHERE TWO BODIES OVERLAP (OR ONE BODY = PIN TO WORLD)",
    "CLICK ON A JOINT SPOT. LEFT/RIGHT ARROWS (A/D) SPIN IT",
    "LIKE MOTOR BUT SPINS ALL THE TIME (WINDMILLS, CONVEYORS)",
    "DRAG FROM ONE BODY/POINT TO ANOTHER",
    "DRAG FROM ONE BODY/POINT TO ANOTHER",
    "DRAG BODIES AROUND",
    "CLICK A BODY OR JOINT TO REMOVE IT",
};
const float DENSITIES[3] = {0.5f, 1.5f, 4.0f};
const char* DENSITY_NAMES[3] = {"LIGHT", "MED", "HEAVY"};
const uint32_t DENSITY_COLORS[3] = {0x7fd1ff, 0xe0b060, 0xb86a6a};

SDL_Color rgb(uint32_t c, uint8_t a = 255) {
    return SDL_Color{(uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c, a};
}
uint32_t shade(uint32_t c, float f) {
    auto ch = [&](int s) { return (uint32_t)std::clamp((int)(((c >> s) & 255) * f), 0, 255); };
    return 0xFF000000u | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

struct Button {
    SDL_Rect r;
    std::function<std::string()> label;
    std::function<void()> action;
    std::function<bool()> active;
    uint32_t swatch = 0;
    bool hasSwatch = false;
};

struct Game {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    SDL_Texture* tex = nullptr;
    World world;
    Physics phys{&world};
    std::vector<uint32_t> pixels = std::vector<uint32_t>(World::W * World::H);
    std::vector<Button> buttons;

    Tool tool = T_MAT;
    uint8_t mat = M_SAND;
    int brush = 3;
    bool paused = false, stepOnce = false, anchored = false, running = true;
    int densityIdx = 1;

    // mouse state
    bool lmb = false, rmb = false;
    Vec2 mouse, lastMouse, dragStart;
    int dragBody = -1;      // body under press (rod/spring), or grabbed body
    int grabJoint = -1;
    bool inSim = false;
    int frameCount = 0;
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

    void buildButtons() {
        int cols = 9;
        int bw = (WIN_W - (cols + 1) * BTN_GAP) / cols;
        auto slot = [&](int idx) {
            int row = idx / cols, col = idx % cols;
            return SDL_Rect{BTN_GAP + col * (bw + BTN_GAP), SIM_H + BTN_GAP + row * (BTN_H + BTN_GAP), bw, BTN_H};
        };
        int idx = 0;
        const uint8_t order[] = {M_WALL, M_SAND, M_WATER, M_OIL, M_LAVA, M_ACID, M_FIRE, M_GUNPOWDER, M_TNT,
                                 M_STONE, M_WOOD, M_PLANT, M_ICE, M_ASH, M_STEAM, M_SMOKE, M_VOID, M_EMPTY};
        for (uint8_t m : order) {
            Button b;
            b.r = slot(idx++);
            b.hasSwatch = true;
            b.swatch = m == M_EMPTY ? 0xff5050 : MATS[m].color;
            std::string name = m == M_EMPTY ? "ERASER" : MATS[m].name;
            b.label = [name] { return name; };
            b.action = [this, m] { tool = T_MAT; mat = m; };
            b.active = [this, m] { return tool == T_MAT && mat == m; };
            buttons.push_back(b);
        }
        const Tool tools[] = {T_BOX, T_CIRCLE, T_WHEEL, T_ROCKET, T_PIN, T_MOTOR, T_AUTOMOTOR, T_ROD, T_SPRING};
        const char* names[] = {"BOX", "CIRCLE", "WHEEL", "ROCKET", "PIN", "MOTOR", "AUTOMOTOR", "ROD", "SPRING"};
        for (int i = 0; i < 9; ++i) {
            Tool t = tools[i];
            Button b;
            b.r = slot(idx++);
            std::string nm = names[i];
            b.label = [nm] { return nm; };
            b.action = [this, t] { tool = t; };
            b.active = [this, t] { return tool == t; };
            buttons.push_back(b);
        }
        auto add = [&](std::function<std::string()> label, std::function<void()> act, std::function<bool()> active) {
            Button b;
            b.r = slot(idx++);
            b.label = label; b.action = act; b.active = active;
            buttons.push_back(b);
        };
        add([] { return std::string("GRAB"); }, [this] { tool = T_GRAB; }, [this] { return tool == T_GRAB; });
        add([] { return std::string("DELETE"); }, [this] { tool = T_DELETE; }, [this] { return tool == T_DELETE; });
        add([this] { return std::string(anchored ? "ANCHOR:ON" : "ANCHOR:OFF"); }, [this] { anchored = !anchored; },
            [this] { return anchored; });
        add([this] { return std::string("MASS:") + DENSITY_NAMES[densityIdx]; },
            [this] { densityIdx = (densityIdx + 1) % 3; }, [] { return false; });
        add([this] { return std::string(paused ? "RESUME" : "PAUSE"); }, [this] { paused = !paused; },
            [this] { return paused; });
        add([] { return std::string("STEP"); }, [this] { stepOnce = true; }, [] { return false; });
        add([] { return std::string("CLR BODIES"); }, [this] { clearBodies(); }, [] { return false; });
        add([] { return std::string("CLR SAND"); }, [this] { world.clear(); phys.stampBodies(); }, [] { return false; });
        add([] { return std::string("DEMO"); }, [this] { buildDemo(); }, [] { return false; });
    }

    void clearBodies() {
        phys.clear();
        grabJoint = -1;
        dragBody = -1;
    }

    int spawnCar(Vec2 c, float scale = 1.f) {
        float dens = 1.5f;
        int chassis = phys.addBox(c, Vec2(24 * scale, 5 * scale), 0, dens, false);
        phys.bodies[chassis].color = 0xd8a040;
        for (int i = -1; i <= 1; i += 2) {
            Vec2 wc = c + Vec2(16.f * scale * i, 6.f * scale);
            int w = phys.addCircle(wc, 9.f * scale, dens, false, true);
            phys.bodies[w].color = 0x5a5f6b;
            phys.addPin(wc, chassis, w, true, true);
        }
        return chassis;
    }

    void buildDemo() {
        clearBodies();
        world.clear();
        // walled water tank on the left
        world.fillRect(10, 160, 13, 239, M_WALL);
        world.fillRect(10, 236, 100, 239, M_WALL);
        world.fillRect(97, 160, 100, 239, M_WALL);
        world.fillRect(14, 190, 96, 235, M_WATER);
        // ramp and plateau
        for (int x = 0; x < 70; ++x) world.fillRect(120 + x, 235 - x / 4, 120 + x, 239, M_STONE);
        world.fillRect(190, 217, 399, 239, M_STONE);
        // wooden tower and TNT
        world.fillRect(330, 180, 345, 216, M_WOOD);
        world.fillRect(350, 205, 358, 216, M_TNT);
        // sand drop and oil
        world.fillRect(150, 40, 175, 70, M_SAND);
        world.fillRect(20, 120, 50, 140, M_OIL);
        // a car, a box stack, a beam
        spawnCar(Vec2(235, 205));
        for (int i = 0; i < 3; ++i) {
            int b = phys.addBox(Vec2(300, 210.f - i * 12.f), Vec2(6, 6), 0, 1.5f, false);
            phys.bodies[b].color = DENSITY_COLORS[1];
        }
        int ball = phys.addCircle(Vec2(60, 90), 8, 0.5f, false, false);
        phys.bodies[ball].color = DENSITY_COLORS[0];
        phys.stampBodies();
    }

    // Developer test scenes (used with --shot / --scene).
    void buildTestScene(int which) {
        clearBodies();
        world.clear();
        world.fillRect(0, 200, 399, 203, M_WALL);
        if (which == 1) {
            for (int c = 0; c < 6; ++c) {
                int x0 = 10 + c * 65;
                world.fillRect(x0 - 2, 120, x0 - 1, 199, M_WALL);
                world.fillRect(x0 + 55, 120, x0 + 56, 199, M_WALL);
            }
            world.fillRect(20, 185, 40, 199, M_WOOD); world.fillRect(20, 170, 40, 184, M_OIL);
            world.fillRect(28, 165, 30, 168, M_FIRE);
            world.fillRect(75, 170, 125, 199, M_WATER); world.fillRect(90, 100, 100, 108, M_LAVA);
            world.fillRect(140, 190, 180, 199, M_STONE); world.fillRect(145, 175, 175, 189, M_SAND);
            world.fillRect(150, 150, 170, 160, M_ACID);
            world.fillRect(205, 190, 235, 199, M_PLANT); world.fillRect(205, 160, 235, 189, M_WATER);
            world.fillRect(270, 198, 320, 199, M_GUNPOWDER); world.fillRect(271, 190, 273, 197, M_FIRE);
            world.fillRect(335, 185, 370, 199, M_ICE); world.fillRect(340, 120, 345, 126, M_LAVA);
        } else {
            // pendulum: rod pinned to the world
            int bob = phys.addCircle(Vec2(80, 100), 8, 3.f, false, false);
            phys.bodies[bob].color = 0xb86a6a;
            phys.addPin(Vec2(40, 40), phys.addBox(Vec2(300, 20), Vec2(2, 2), 0, 1, true), -1, false, true);
            phys.addDistance(bob, Vec2(80, 100), -1, Vec2(40, 40), 0.f);
            // spring bouncer
            int box = phys.addBox(Vec2(150, 120), Vec2(10, 8), 0, 1.5f, false);
            phys.addDistance(box, Vec2(150, 112), -1, Vec2(150, 30), 2.5f);
            // windmill: auto motor pinned to the world
            int arm = phys.addBox(Vec2(230, 150), Vec2(25, 2), 0, 1.5f, false);
            phys.addPin(Vec2(230, 150), arm, -1, true, false);
            // rocket glued to a box
            int rb = phys.addBox(Vec2(320, 190), Vec2(10, 6), 0, 1.5f, false);
            int rk = phys.addRocket(Vec2(320, 178), 0);
            phys.addPin(Vec2(320, 184), rb, rk, false, true);
            phys.thrustOn = true;
            spawnCar(Vec2(370, 150));
        }
        phys.stampBodies();
    }

    // Scripted tool use through the same handlers the mouse drives.
    void simDrag(Tool t, Vec2 a, Vec2 b) {
        tool = t;
        mouse = a; lastMouse = a; handleSimDown(SDL_BUTTON_LEFT);
        mouse = b; handleSimUp(SDL_BUTTON_LEFT);
    }
    void buildScriptedScene() {
        clearBodies();
        world.clear();
        world.fillRect(0, 200, 399, 203, M_WALL);
        simDrag(T_BOX, Vec2(40, 150), Vec2(100, 165));          // chassis
        simDrag(T_WHEEL, Vec2(48, 175), Vec2(48, 183));         // wheel (no body under centre)
        simDrag(T_WHEEL, Vec2(48, 160), Vec2(48, 168));         // wheel on chassis -> motor
        simDrag(T_WHEEL, Vec2(92, 158), Vec2(92, 167));
        simDrag(T_CIRCLE, Vec2(150, 100), Vec2(150, 110));
        simDrag(T_BOX, Vec2(200, 100), Vec2(215, 112));
        simDrag(T_BOX, Vec2(240, 100), Vec2(255, 112));
        simDrag(T_ROD, Vec2(207, 106), Vec2(247, 106));         // rod between the two boxes
        simDrag(T_BOX, Vec2(300, 100), Vec2(320, 110));
        simDrag(T_SPRING, Vec2(310, 105), Vec2(310, 20));       // spring to world
        simDrag(T_ROCKET, Vec2(350, 150), Vec2(350, 120));
        simDrag(T_PIN, Vec2(350, 150), Vec2(350, 150));
        anchored = true;
        simDrag(T_BOX, Vec2(120, 130), Vec2(180, 135));         // static shelf
        anchored = false;
        simDrag(T_AUTOMOTOR, Vec2(150, 130), Vec2(150, 130));
        // grab: pick body, drag target, release
        tool = T_GRAB; mouse = Vec2(150, 100); lastMouse = mouse; handleSimDown(SDL_BUTTON_LEFT);
        mouse = Vec2(170, 90); continuousInput(); handleSimUp(SDL_BUTTON_LEFT);
        simDrag(T_DELETE, Vec2(207, 106), Vec2(207, 106));
        phys.stampBodies();
    }

    // ---------------------------------------------------------------- input
    Vec2 toWorld(int mx, int my) const { return Vec2((float)mx / S, (float)my / S); }

    void createShape(Vec2 a, Vec2 b) {
        uint32_t col = anchored ? 0x808890 : DENSITY_COLORS[densityIdx];
        float dens = DENSITIES[densityIdx];
        Vec2 d = b - a;
        switch (tool) {
            case T_BOX: {
                Vec2 half = Vec2(std::fabs(d.x), std::fabs(d.y)) * 0.5f;
                Vec2 c = (a + b) * 0.5f;
                if (half.x < 2 || half.y < 2) { half = Vec2(7, 7); c = a; }
                int id = phys.addBox(c, half, 0, dens, anchored);
                phys.bodies[id].color = col;
                break;
            }
            case T_CIRCLE: case T_WHEEL: {
                float r = length(d);
                if (r < 3) r = 8;
                bool wheel = tool == T_WHEEL;
                int id = phys.addCircle(a, r, dens, anchored, wheel);
                phys.bodies[id].color = wheel ? 0x5a5f6b : col;
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
            case T_ROD: case T_SPRING: {
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
                                if (e.button.x >= b.r.x && e.button.x < b.r.x + b.r.w && e.button.y >= b.r.y &&
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
            case SDLK_f: densityIdx = (densityIdx + 1) % 3; break;
            case SDLK_v: spawnCar(mouse); break;
            case SDLK_LEFTBRACKET: brush = std::max(1, brush - 1); break;
            case SDLK_RIGHTBRACKET: brush = std::min(24, brush + 1); break;
            case SDLK_TAB: {
                tool = T_MAT;
                mat = (uint8_t)(mat % (M_COUNT - 1) + 1);
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

        if (!inSim) return;
        int cx = (int)mouse.x, cy = (int)mouse.y, lx = (int)lastMouse.x, ly = (int)lastMouse.y;
        if (lmb && tool == T_MAT)
            world.paintLine(lx, ly, cx, cy, brush, mat, mat == M_EMPTY ? 1.f : 0.45f);
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
    void renderParticles() {
        const uint32_t bg = 0xFF000000u | BG;
        for (int i = 0; i < World::W * World::H; ++i) {
            const Cell& c = world.cells[i];
            uint32_t col;
            switch (c.t) {
                case M_EMPTY: col = bg; break;
                case M_FIRE: {
                    int l = c.life;
                    int g = std::min(255, 60 + l * 5 + (c.var & 15));
                    int b = l > 35 ? 70 : 10;
                    col = 0xFF000000u | (255u << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                    break;
                }
                case M_SMOKE: {
                    int v = 28 + c.life / 3 + (c.var & 7);
                    col = 0xFF000000u | ((uint32_t)v << 16) | ((uint32_t)v << 8) | (uint32_t)(v + 6);
                    break;
                }
                case M_STEAM: {
                    int v = 60 + c.life / 3 + (c.var & 7);
                    col = 0xFF000000u | ((uint32_t)v << 16) | ((uint32_t)(v + 6) << 8) | (uint32_t)(v + 14);
                    break;
                }
                default: {
                    float f = 0.84f + 0.16f * (c.var / 255.f);
                    if (c.t == M_LAVA) f = 0.8f + 0.35f * (c.var / 255.f);
                    col = shade(MATS[c.t].color, f);
                    break;
                }
            }
            pixels[i] = col;
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
            SDL_Color edge = rgb(shade(b.color, 0.55f) & 0xFFFFFF);
            if (b.shape == SHAPE_BOX) {
                std::vector<Vec2> pts;
                Vec2 c[4] = {{-b.half.x, -b.half.y}, {b.half.x, -b.half.y}, {b.half.x, b.half.y}, {-b.half.x, b.half.y}};
                for (auto& p : c) pts.push_back(b.toWorld(p));
                fillPoly(pts, b.color);
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
                fillPoly(pts, b.color);
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
                case T_ROCKET: case T_ROD: case T_SPRING: lineWorld(dragStart, mouse, white, 2); break;
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

    void renderUI() {
        SDL_SetRenderDrawColor(ren, 22, 26, 34, 255);
        SDL_Rect panel{0, SIM_H, WIN_W, UI_H};
        SDL_RenderFillRect(ren, &panel);
        for (auto& b : buttons) {
            bool act = b.active();
            SDL_SetRenderDrawColor(ren, act ? 70 : 40, act ? 90 : 46, act ? 130 : 58, 255);
            SDL_RenderFillRect(ren, &b.r);
            SDL_SetRenderDrawColor(ren, act ? 255 : 90, act ? 255 : 98, act ? 255 : 112, 255);
            SDL_RenderDrawRect(ren, &b.r);
            int tx = b.r.x + 6;
            if (b.hasSwatch) {
                SDL_Rect sw{b.r.x + 4, b.r.y + 5, 10, BTN_H - 10};
                SDL_Color sc = rgb(b.swatch);
                SDL_SetRenderDrawColor(ren, sc.r, sc.g, sc.b, 255);
                SDL_RenderFillRect(ren, &sw);
                tx = b.r.x + 18;
            }
            font::draw(ren, b.label(), tx, b.r.y + (BTN_H - 14) / 2, 2, SDL_Color{235, 238, 245, 255});
        }
        int sy = SIM_H + ROWS * (BTN_H + BTN_GAP) + 5;
        std::string status = std::string("TOOL: ") + (tool == T_MAT ? (mat == M_EMPTY ? "ERASER" : MATS[mat].name) : TOOL_NAMES[tool]);
        status += "  BRUSH " + std::to_string(brush);
        status += "  BODIES " + std::to_string(phys.bodyCount());
        status += "  FPS " + std::to_string((int)fps);
        if (paused) status += "  [PAUSED]";
        font::draw(ren, status, 8, sy, 2, SDL_Color{255, 220, 120, 255});
        std::string hint = TOOL_HINTS[tool];
        font::draw(ren, hint, 8 + font::textWidth(status, 2) + 24, sy + 3, 1, SDL_Color{170, 180, 200, 255});
        font::draw(ren,
                   "SPACE PAUSE  N STEP  C CLEAR SAND  X CLEAR BODIES  R DEMO  T ANCHOR  F MASS  V CAR  G FLIP GRAVITY  TAB NEXT MATERIAL  ARROWS DRIVE  UP FIRE ROCKETS",
                   8, sy + 14, 1, SDL_Color{120, 130, 150, 255});
    }

    void render() {
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        renderParticles();
        renderBodies();
        renderJoints();
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
    // Headless self-test: sandbots --shot out.bmp [frames]
    const char* shot = nullptr;
    int shotFrames = 300;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) {
            shot = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') shotFrames = std::atoi(argv[++i]);
        }
    }
    int scene = 0;
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--scene") && i + 1 < argc) scene = std::atoi(argv[i + 1]);
    if (shot) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);

    Game g;
    if (!g.init(shot != nullptr)) return 1;

    if (scene == 3) g.buildScriptedScene();
    else if (scene) g.buildTestScene(scene);
    if (shot) {
        for (int f = 0; f < shotFrames; ++f) {
            g.phys.motorInput = f > 40 ? 1.f : 0.f;
            if (scene) g.phys.thrustOn = scene == 2;
            g.phys.step(1.f / 60.f);
            g.world.step();
            if (!scene && f == 120) g.world.explode(352, 210, 24.f, 260.f);
        }
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
