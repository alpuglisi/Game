// Editing: selection, groups, undo, clipboard, handles, snapping, the tool contract on the canvas, files and the camera.
#include <filesystem>
#include <fstream>
#include "app.hpp"

// ---------------------------------------------------------------- tables
const ToolInfo& toolInfo(Tool t) {
    static const ToolInfo T[T_COUNT] = {
        {"Paint", icons::Paint, "P", "Paint cells of the chosen material with a round brush", "LMB paint | Shift erase | [ ] size | RMB cancel"},
        {"Box", icons::Box, "B", "Drag a rectangle; type width Tab height Tab angle for exact sizes", "LMB drag | type size | Enter commit | Esc cancel"},
        {"Circle", icons::Circle, "C", "Drag from the centre out to set the radius", "LMB drag | type radius | Enter commit | Esc cancel"},
        {"Wheel", icons::Wheel, "W", "A high-friction circle; dropped on a body it is pinned there with a keyed motor", "LMB drag | type radius | Enter commit | Esc cancel"},
        {"Rocket", icons::Rocket, "R", "Drag to set the thrust direction; Up or W fires it while running", "LMB drag | Enter commit | Esc cancel"},
        {"Pin", icons::Pin, "J", "Click where two bodies overlap or touch to hinge them (one body: pinned to the world)", "LMB click on a seam"},
        {"Motor", icons::Motor, "O", "A pin driven by the arrow keys (A / D) while running", "LMB click on a seam"},
        {"Spinner", icons::Spinner, "", "A motor that spins all the time", "LMB click on a seam"},
        {"Rod", icons::Rod, "L", "Drag from one body or point to another: a rigid link of fixed length", "LMB drag | Esc cancel"},
        {"Spring", icons::Spring, "S", "Drag from one body or point to another: a soft link (stiffness and damping in the bar)", "LMB drag | Esc cancel"},
        {"Grab", icons::Grab, "G", "Drag bodies about with a soft spring, even while the simulation runs", "LMB drag"},
        {"Delete tool", icons::Delete, "", "Click a body or joint to remove it", "LMB click"},
        {"Slider", icons::Slider, "", "Press on the sliding body and drag along its line; end on the body it slides in, or on empty space", "LMB drag | Esc cancel"},
        {"Select", icons::Select, "Q", "Click selects, click again cycles, Shift adds, Ctrl picks a part; drag moves or box-selects", "LMB select | Shift add | Ctrl part | drag move or box | hold or ` select other"},
        {"Pipe", icons::Pipe, "", "Drag along the pipe: two welded walls (diameter and wall in the bar)", "LMB drag | type length Tab diameter | Enter commit | Esc cancel"},
        {"Hose", icons::Hose, "", "Drag along the hose: a chain of hinged pipe segments", "LMB drag | type length Tab diameter | Enter commit | Esc cancel"},
        {"Emitter", icons::Emitter, "", "Drag a block that endlessly produces a material out of one side", "LMB drag | type size | Enter commit | Esc cancel"},
        {"Bond", icons::Bond, "", "Click where two bodies meet: a weld that lets go when too hot or overloaded", "LMB click on a seam"},
        {"Fan", icons::Fan, "", "Drag a fan; the long side is the blade span and the arrow shows the airflow", "LMB drag | type size | Enter commit | Esc cancel"},
        {"Cut", icons::Cut, "K", "Drag a box or circle over bodies: the area under it is cut out of every body it touches", "LMB drag | Enter commit | Esc cancel"},
        {"Measure", icons::Measure, "M", "Drag between two points to read length, dx, dy and angle", "LMB drag | snap applies"},
    };
    return T[t];
}
const Tool DIGIT_TOOLS[10] = {T_MAT, T_SELECT, T_GRAB, T_BOX, T_CIRCLE, T_WHEEL, T_PIN, T_MOTOR, T_ROD, T_SPRING};

const std::vector<uint8_t> BODY_MATS = {M_STEEL, M_IRON, M_COPPER, M_ALUMINUM, M_LEAD, M_GOLD, M_TITANIUM, M_TUNGSTEN,
                                        M_WOOD, M_RUBBER, M_PLASTIC, M_GLASS, M_CONCRETE, M_BRICK, M_CERAMIC, M_STONE,
                                        M_ICE, M_SOLDER, M_PARAFFIN, M_PRIMER};
const std::vector<uint8_t> EMIT_MATS = {M_SAND, M_ASH, M_GUNPOWDER, M_COAL,
                                        M_WATER, M_OIL, M_GASOLINE, M_DIESEL, M_KEROSENE, M_JETFUEL, M_ETHANOL, M_HYDRAULIC, M_ACID, M_LAVA,
                                        M_STEAM, M_SMOKE, M_EXHAUST, M_VAPOR, M_PROPANE, M_HYDROGEN, M_AIR};
const std::vector<PaintGroup> PAINT_GROUPS = {
    {"Powders", {M_SAND, M_ASH, M_GUNPOWDER, M_COAL}},
    {"Liquids", {M_WATER, M_OIL, M_GASOLINE, M_DIESEL, M_KEROSENE, M_JETFUEL, M_ETHANOL, M_HYDRAULIC, M_ACID, M_LAVA}},
    {"Gases", {M_STEAM, M_FIRE, M_SMOKE, M_EXHAUST, M_VAPOR, M_PROPANE, M_HYDROGEN, M_AIR}},
    {"Metals", {M_STEEL, M_IRON, M_COPPER, M_ALUMINUM, M_LEAD, M_GOLD, M_TITANIUM, M_TUNGSTEN, M_SOLDER}},
    {"Building", {M_WALL, M_STONE, M_CONCRETE, M_BRICK, M_CERAMIC, M_GLASS, M_WOOD, M_RUBBER, M_PLASTIC, M_ICE, M_PLANT, M_TNT, M_PARAFFIN}},
    {"Devices", {M_HEATER, M_COOLER, M_IGNITER, M_SOURCE, M_VOID, M_BATT_POS, M_BATT_NEG, M_PRIMER}},
};
const int SPARK_RATES[7] = {0, 120, 60, 40, 30, 20, 12};
const int SNAP_STEPS[4] = {1, 2, 5, 10};
const char* BOND_NAMES[4] = {"Paraffin", "Solder", "Epoxy", "Shear pin"};
const float BOND_TEMP[4] = {55.f, 190.f, 260.f, 5000.f};
const float BOND_G[4] = {10.f, 40.f, 120.f, 30.f};

uint32_t heatColor(float T) {
    struct Stop { float t; uint32_t c; };
    static const Stop stops[] = {{-60, 0x1e3cff}, {0, 0x1ea0ff}, {20, 0x1a1c26}, {150, 0xaa281e}, {400, 0xff7814}, {800, 0xffdc3c}, {1500, 0xffffff}};
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
std::string fmt(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%g", std::round(v * 100.f) / 100.f);
    return b;
}
std::string matTip(uint8_t m) {
    if (m == M_EMPTY) return "Eraser: removes cells";
    if (m >= M_COUNT) return "";
    static std::string cache[M_COUNT];   // the table is constant and the Materials tab asks for every chip every frame
    if (!cache[m].empty()) return cache[m];
    const MatInfo& mi = MATS[m];
    char d[32];
    std::snprintf(d, sizeof d, "%.3g", mi.density);
    std::string s = std::string(mi.name) + ": density " + d;
    if (mi.kind == K_SOLID) s += ", friction " + fmt(mi.friction);
    if (mi.hiT < 1e8f) s += (MATS[mi.hiTo].kind == K_GAS ? ", boils " : ", melts ") + fmt(mi.hiT) + " C";
    if (mi.loT > -1e8f) s += (mi.kind == K_GAS ? ", condenses " : ", freezes ") + fmt(mi.loT) + " C";
    if (mi.ignT > 0.f) s += ", ignites " + fmt(mi.ignT) + " C";
    if (mi.blastR > 0.f) s += ", explosive";
    if (mi.elec > 0.f) s += ", conducts";
    cache[m] = s;
    return s;
}
// a small recursive-descent evaluator: numbers, + - * /, parentheses, unary minus
namespace {
struct Expr {
    const char* p;
    bool ok = true;
    void ws() { while (*p == ' ') ++p; }
    float atom() {
        ws();
        if (*p == '(') { ++p; float v = sum(); ws(); if (*p == ')') ++p; else ok = false; return v; }
        if (*p == '-') { ++p; return -atom(); }
        if (*p == '+') { ++p; return atom(); }
        char* e;
        float v = std::strtof(p, &e);
        if (e == p) { ok = false; return 0.f; }
        p = e;
        return v;
    }
    float prod() {
        float v = atom();
        for (;;) {
            ws();
            if (*p == '*') { ++p; v *= atom(); }
            else if (*p == '/') { ++p; float d = atom(); v = d != 0.f ? v / d : 0.f; }
            else return v;
        }
    }
    float sum() {
        float v = prod();
        for (;;) {
            ws();
            if (*p == '+') { ++p; v += prod(); }
            else if (*p == '-') { ++p; v -= prod(); }
            else return v;
        }
    }
};
}  // namespace
bool evalExpr(const std::string& s, float& out) {
    Expr e{s.c_str()};
    float v = e.sum();
    e.ws();
    if (!e.ok || *e.p != 0 || !std::isfinite(v)) return false;
    out = v;
    return true;
}

// ---------------------------------------------------------------- setup
bool Game::init(bool headless, int w, int h) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return false; }
    winW = std::max(1024, w); winH = std::max(640, h);
    win = SDL_CreateWindow("SandBots", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH, SDL_WINDOW_RESIZABLE);
    if (!win) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return false; }
    SDL_SetWindowMinimumSize(win, 1024, 640);
    ren = SDL_CreateRenderer(win, -1, headless ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { std::fprintf(stderr, "renderer: %s\n", SDL_GetError()); return false; }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_StartTextInput();
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, World::W, World::H);
    L = computeLayout(winW, winH, stripCollapsed);
    in.winW = winW; in.winH = winH;
    buildCommands();
    buildDemo();
    notify("Edit mode: draw freely, then press Play (Space). Stop restores your drawing");
    return true;
}
void Game::shutdown() {
    if (tex) SDL_DestroyTexture(tex);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
}
void Game::screenshot(const char* path) {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, L.winW, L.winH, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
}
void Game::notify(const std::string& s) { note = s; ui.toast(s); }
void Game::selectMaterial(uint8_t m) {   // picking a paint material picks the Paint tool, which ends any drag in progress like a tool change
    if (tool != T_MAT) setTool(T_MAT);
    mat = m;
    if (m != M_EMPTY) lastPaintMat = m;
    rememberMaterial(m);
}
void Game::pickTool(Tool t) {   // a digit key or the Paint button: painting needs a material, not the eraser
    if (t == T_MAT && mat == M_EMPTY) mat = lastPaintMat == M_EMPTY ? (uint8_t)M_SAND : lastPaintMat;
    setTool(t);
}
void Game::setTool(Tool t) {
    if (dragInProgress()) cancelDrag();
    tool = t;
    if (t != T_MEASURE) measOn = false;
}

// ---------------------------------------------------------------- what the panels act on
std::vector<int> Game::selectedWith(const std::function<bool(const Body&)>& pred) {
    pruneSelection();
    std::vector<int> r;
    for (int id : sel) if (pred(phys.bodies[id])) r.push_back(id);
    return r;
}
void Game::setBodyMaterial(uint8_t m) {
    bodyMat = m;
    rememberMaterial(m);
    pruneSelection();
    if (sel.empty()) return;
    pushUndo("material", "Material");
    for (int id : sel) { const Body b = phys.bodies[id]; phys.reshape(id, b.pos, b.half, b.radius, b.angle, m, b.isStatic); }
    phys.stampBodies();
}
void Game::setFixed(bool fixed) {
    anchored = fixed;
    pruneSelection();
    if (sel.empty()) return;
    pushUndo("fixed", fixed ? "Fixed" : "Free");
    for (int id : sel) { const Body b = phys.bodies[id]; phys.reshape(id, b.pos, b.half, b.radius, b.angle, b.mat, fixed); }
    phys.stampBodies();
}
void Game::adjustFan(float d) {
    std::vector<int> fans = selFans();
    if (fans.empty()) { lastFan = std::clamp(lastFan + d, 5.f, 300.f); return; }
    setFanStrength(std::fabs(phys.bodies[fans[0]].fan.strength) + d);
}
void Game::setFanStrength(float s) {
    std::vector<int> fans = selFans();
    float mag = std::clamp(std::fabs(s), 5.f, 300.f);
    if (fans.empty()) { lastFan = mag; return; }
    pushUndo("fan", "Fan strength");
    for (int id : fans) { float& st = phys.bodies[id].fan.strength; st = st < 0 ? -mag : mag; }
    lastFan = mag;
}
void Game::flipFan() {
    std::vector<int> fans = selFans();
    if (fans.empty()) return;
    pushUndo("fanflip", "Flip fan");
    for (int id : fans) phys.bodies[id].fan.strength = -phys.bodies[id].fan.strength;
}
void Game::setFanVacuum(bool v) {
    fanVacuumDefault = v;
    std::vector<int> fans = selFans();
    if (!fans.empty()) pushUndo("fanmode", "Fan mode");
    for (int id : fans) phys.bodies[id].fan.vacuum = v ? 1 : 0;
}
void Game::adjustRate(float d) {
    std::vector<int> em = selEmitters();
    if (em.empty()) { lastRate = std::clamp(lastRate + d, 5.f, 1000.f); return; }
    setRate(phys.bodies[em[0]].src.rate + d);
}
void Game::setRate(float r) {
    std::vector<int> em = selEmitters();
    r = std::clamp(r, 5.f, 1000.f);
    if (em.empty()) { lastRate = r; return; }
    pushUndo("rate", "Emitter rate");
    for (int id : em) phys.bodies[id].src.rate = r;
    lastRate = r;
}
void Game::setEmitMaterial(uint8_t m) {
    payload = m;
    std::vector<int> em = selEmitters();
    if (!em.empty()) pushUndo("emits", "Emitter material");
    for (int id : em) phys.bodies[id].src.mat = m;
}
void Game::setSelectionFace(int f) {
    emitFace = (uint8_t)f;
    std::vector<int> em = selEmitters();
    if (!em.empty()) pushUndo("face", "Emitter outlet");
    for (int id : em) phys.bodies[id].src.face = (uint8_t)f;
}
void Game::scaleSelection(float pct) {
    pruneSelection();
    if (sel.empty()) { notify("Select bodies to scale first"); return; }
    pct = std::clamp(pct, 5.f, 1000.f);
    pushUndo(nullptr, "Scale");
    double wsum = 0, cx = 0, cy = 0;
    for (int id : sel) { const Body& b = phys.bodies[id]; wsum += b.area; cx += b.pos.x * b.area; cy += b.pos.y * b.area; }
    phys.scaleBodies(sel, pct / 100.f, wsum > 0 ? Vec2((float)(cx / wsum), (float)(cy / wsum)) : phys.bodies[primary].pos);
    lastScale = pct;
    phys.stampBodies();
    notify("Scaled to " + fmt(pct) + "% (" + std::to_string(sel.size()) + " bodies)");
}
// Align the selected bodies (whole groups move together) on an edge or centre line of the selection's bounding box.
void Game::alignSelection(int how) {
    pruneSelection();
    if (sel.size() < 2) { notify("Select two or more bodies to align"); return; }
    std::vector<int> ids = wholeGroups(sel);
    std::pair<Vec2, Vec2> all = bodiesBox(ids);
    pushUndo(nullptr, "Align");
    std::vector<int> done;
    for (int id : ids) {
        if (std::find(done.begin(), done.end(), id) != done.end()) continue;
        std::vector<int> unit = phys.bodies[id].group >= 0 ? phys.groupMembers(phys.bodies[id].group) : std::vector<int>{id};
        std::pair<Vec2, Vec2> bb = bodiesBox(unit);
        Vec2 d;
        switch (how) {
            case 0: d.x = all.first.x - bb.first.x; break;
            case 1: d.x = (all.first.x + all.second.x) * 0.5f - (bb.first.x + bb.second.x) * 0.5f; break;
            case 2: d.x = all.second.x - bb.second.x; break;
            case 3: d.y = all.first.y - bb.first.y; break;
            case 4: d.y = (all.first.y + all.second.y) * 0.5f - (bb.first.y + bb.second.y) * 0.5f; break;
            default: d.y = all.second.y - bb.second.y; break;
        }
        phys.translateBodies(unit, d);
        done.insert(done.end(), unit.begin(), unit.end());
    }
    phys.stampBodies();
}
// Spread the selected units out so the gaps between them are equal along one axis.
void Game::distributeSelection(bool horizontal) {
    pruneSelection();
    std::vector<int> ids = wholeGroups(sel);
    std::vector<std::vector<int>> units;
    std::vector<int> done;
    for (int id : ids) {
        if (std::find(done.begin(), done.end(), id) != done.end()) continue;
        std::vector<int> unit = phys.bodies[id].group >= 0 ? phys.groupMembers(phys.bodies[id].group) : std::vector<int>{id};
        done.insert(done.end(), unit.begin(), unit.end());
        units.push_back(unit);
    }
    if (units.size() < 3) { notify("Select three or more bodies to distribute"); return; }
    struct U { std::vector<int> ids; float lo, hi; };
    std::vector<U> us;
    for (auto& u : units) { std::pair<Vec2, Vec2> bb = bodiesBox(u); us.push_back({u, horizontal ? bb.first.x : bb.first.y, horizontal ? bb.second.x : bb.second.y}); }
    std::sort(us.begin(), us.end(), [](const U& a, const U& b) { return a.lo < b.lo; });
    float span = us.back().hi - us.front().lo, total = 0.f;
    for (auto& u : us) total += u.hi - u.lo;
    float gap = (span - total) / (float)(us.size() - 1);
    pushUndo(nullptr, "Distribute");
    float at = us.front().lo;
    for (auto& u : us) {
        float d = at - u.lo;
        phys.translateBodies(u.ids, horizontal ? Vec2(d, 0) : Vec2(0, d));
        at += (u.hi - u.lo) + gap;
    }
    phys.stampBodies();
}
void Game::setBodyTransform(int id, Vec2 pos, Vec2 half, float radius, float angleDeg) {
    if (id < 0 || id >= (int)phys.bodies.size() || !phys.bodies[id].alive) return;
    const Body& b = phys.bodies[id];
    if (b.group >= 0 && !partMode && sel.size() > 1) phys.transformGroup(id, pos, angleDeg * PI / 180.f);
    else phys.reshape(id, pos, Vec2(std::max(0.5f, half.x), std::max(0.5f, half.y)), std::max(0.5f, radius), angleDeg * PI / 180.f, b.mat, b.isStatic);
    phys.stampBodies();
}
void Game::clearBodies() {
    phys.clear();
    grabJoint = -1;
    dragBody = -1;
}
// A paused run is a live state on top of the drawing: anything that replaces the whole world (a scene, a file, New) first goes
// back to the drawing, so the undo entry it pushes holds what the user drew and nothing is lost.
void Game::leavePlay() {
    if (!playing) return;
    if (lmb) cancelDrag();
    restoreState(snapshot);
    playing = false; paused = false; stepOnce = false;
}
void Game::resetWorld() {
    leavePlay();
    pushUndo(nullptr, "New scene");
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
    clearSelection();
}

// ---------------------------------------------------------------- selection / groups
bool Game::snapActive() const {
    bool ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
    return snapOn != (ctrl && (lmb || handle >= 0));
}
Vec2 Game::snap(Vec2 p) const {
    if (!snapActive()) return p;
    float g = (float)snapStep;
    return Vec2(std::round(p.x / g) * g, std::round(p.y / g) * g);
}
bool Game::shapeTool(Tool t) const { return t == T_BOX || t == T_CIRCLE || t == T_WHEEL || t == T_ROCKET || t == T_PIPE || t == T_HOSE || t == T_EMITTER || t == T_FAN; }
Vec2 Game::smouse() const { return (shapeTool(tool) || tool == T_CUT) ? snap(mouse) : mouse; }

void Game::pruneSelection() {
    sel.erase(std::remove_if(sel.begin(), sel.end(), [&](int id) { return id < 0 || id >= (int)phys.bodies.size() || !phys.bodies[id].alive; }), sel.end());
    if (std::find(sel.begin(), sel.end(), primary) == sel.end()) primary = sel.empty() ? -1 : sel[0];
    if (selJoint >= 0 && !jointValid(selJoint)) selJoint = -1;
}
bool Game::jointValid(int j) const { return j >= 0 && j < (int)phys.joints.size() && phys.joints[j].alive && phys.joints[j].group < 0 && phys.joints[j].type != J_MOUSE; }
// the joint nearest to a point (within a few cells of its anchor, or of the line of a spring or rod), or -1
int Game::jointAt(Vec2 p) const {
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
void Game::selectJoint(int id) { clearSelection(); selJoint = id; }
void Game::removeSelectedJoint() {
    if (!jointValid(selJoint)) return;
    pushUndo(nullptr, "Delete joint");
    int bond = phys.joints[selJoint].bondId;
    if (bond >= 0) { for (auto& k : phys.joints) if (k.alive && k.bondId == bond) phys.removeJoint(k.id); }
    else phys.removeJoint(selJoint);
    selJoint = -1;
}
void Game::clearSelection() { sel.clear(); primary = -1; partMode = false; selJoint = -1; }
void Game::selectBody(int id, bool add, bool part) {
    if (id < 0 || id >= (int)phys.bodies.size() || !phys.bodies[id].alive) { if (!add) clearSelection(); return; }
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
}
// Box select: dragged left to right picks what lies wholly inside the box, right to left (crossing) whatever it touches.
void Game::boxSelect(Vec2 a, Vec2 b, bool add, bool part, bool crossing) {
    float x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x), y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
    if (!add) clearSelection();
    if (selFilter != 2)
        for (auto& bd : phys.bodies) {
            if (!bd.alive) continue;
            Vec2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
            for (Vec2 p : bodyOutline(bd)) { lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); }
            bool in_ = crossing ? (hi.x >= x0 && lo.x <= x1 && hi.y >= y0 && lo.y <= y1) : (lo.x >= x0 && hi.x <= x1 && lo.y >= y0 && hi.y <= y1);
            if (!in_) continue;
            std::vector<int> m = bd.group >= 0 && !part ? phys.groupMembers(bd.group) : std::vector<int>{bd.id};
            for (int id : m) if (std::find(sel.begin(), sel.end(), id) == sel.end()) sel.push_back(id);
        }
    if (sel.empty() && selFilter != 1) {   // nothing but joints in the box: pick the first joint
        for (auto& j : phys.joints) {
            if (!jointValid(j.id)) continue;
            Vec2 p = phys.jointAnchorA(j);
            if (p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1) { selJoint = j.id; break; }
        }
    }
    partMode = false;
    primary = sel.empty() ? -1 : sel[0];
    pruneSelection();
}
void Game::groupSelection() {
    pruneSelection();
    if (sel.size() < 2) { notify("Select two or more bodies to group"); return; }
    pushUndo(nullptr, "Group");
    int g = phys.groupBodies(sel);
    sel = phys.groupMembers(g);
    partMode = false;
    notify("Grouped " + std::to_string(sel.size()) + " bodies");
}
void Game::ungroupSelection() {
    pruneSelection();
    std::vector<int> gs;
    for (int id : sel) { int g = phys.bodies[id].group; if (g >= 0 && std::find(gs.begin(), gs.end(), g) == gs.end()) gs.push_back(g); }
    if (gs.empty()) { notify("Nothing grouped in the selection"); return; }
    pushUndo(nullptr, "Ungroup");
    for (int g : gs) phys.ungroup(g);
    partMode = false;
    notify("Ungrouped");
}
// Boolean subtract with bodies you have selected: the primary (last-clicked, outlined in red) is cut out of the others.
void Game::cutSelection() {
    pruneSelection();
    if (sel.size() < 2 || primary < 0) {
        notify("Select two or more bodies: the last one clicked (red) is cut out of the others. Or use the Cut tool");
        return;
    }
    std::vector<int> cutters;
    const Body& pb = phys.bodies[primary];
    if (pb.group >= 0 && !partMode) cutters = phys.groupMembers(pb.group); else cutters = {primary};
    pushUndo(nullptr, "Subtract");
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
    if (cutN) notify("Cut " + std::to_string(cutN) + " bodies into " + std::to_string(pieces) + " pieces" + (keepCutter ? " (cutter kept)" : ""));
    else notify("Nothing cut: the red body must overlap the others (wheels, rockets, fans and emitters can't be cut)");
}
int Game::autoSegs(Vec2 a, Vec2 b) const { return std::clamp((int)std::ceil(length(b - a) / std::max(4.f, pipeD * 1.2f)), 2, 60); }
bool Game::fanModeVacuum() const {
    if (primary >= 0 && primary < (int)phys.bodies.size() && phys.bodies[primary].alive && phys.bodies[primary].fan.strength != 0.f)
        return phys.bodies[primary].fan.vacuum != 0;
    return fanVacuumDefault;
}
void Game::toggleFanMode() { setFanVacuum(!fanModeVacuum()); }
const char* Game::faceName(int f) { static const char* n[] = {"All sides", "+X side", "-X side", "+Y side", "-Y side"}; return n[f % 5]; }

// ---------------------------------------------------------------- run mode, snapshots and files
void Game::captureState(std::vector<uint8_t>& out) {
    out.clear();
    Writer w{out};
    w.pod(STATE_MAGIC);
    w.pod((uint32_t)sizeof(Cell)); w.pod((uint32_t)sizeof(Body)); w.pod((uint32_t)sizeof(Joint));
    w.pod((uint32_t)World::W); w.pod((uint32_t)World::H);
    w.pod((uint32_t)M_COUNT);   // the material table: adding or reordering materials changes what every saved cell means
    world.save(w);
    // the grab tool's mouse joint belongs to the pointer, not to the drawing: a snapshot taken mid-grab must not keep it
    const bool hideGrab = grabJoint >= 0 && grabJoint < (int)phys.joints.size() && phys.joints[grabJoint].alive;
    if (hideGrab) phys.joints[grabJoint].alive = false;
    phys.save(w);
    if (hideGrab) phys.joints[grabJoint].alive = true;
    w.pod((uint32_t)labels.size());
    for (auto& l : labels) { w.pod(l.p); w.str(l.s); }
    w.pod(bodyMat);
}
bool Game::restoreState(const std::vector<uint8_t>& buf) {
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
    if (std::count(BODY_MATS.begin(), BODY_MATS.end(), bm)) bodyMat = bm;   // a damaged file must not name a material the table lacks
    dragBody = -1; grabJoint = -1;
    pruneSelection();
    return true;
}
// A change of mode ends the drag in progress: a stroke begun in one world must not carry on in the other (a paint stroke that
// started while running and went on after Stop would paint the restored drawing with no undo entry).
void Game::startPlay() {
    if (playing) return;
    if (lmb) cancelDrag();
    captureState(snapshot);
    playing = true;
    paused = false;
}
void Game::play() {
    if (!playing) { startPlay(); notify("Running. Stop restores the drawing as it was"); }
    else paused = false;
}
void Game::stopPlay() {
    if (!playing) { notify("Already stopped (edit mode)"); return; }
    if (lmb) cancelDrag();
    if (!restoreState(snapshot)) { notify("Could not restore the snapshot"); return; }
    playing = false;
    paused = false;
    stepOnce = false;   // a Step asked for in the same frame must not run on the restored drawing
    notify("Stopped: back to the drawn state");
}
void Game::togglePause() {
    if (!playing) { notify("Press Play first"); return; }
    paused = !paused;
}
void Game::stepFrame() {
    if (!playing) { startPlay(); paused = true; }
    stepOnce = true;
}
void Game::newFile() {
    resetWorld();
    phys.stampBodies();
    clearSelection();
    focusBody = -1;
    setCam(0);
    currentFile.clear();
    notify("New empty file");
}
std::string Game::saveDir() { return "saves"; }
std::string Game::cleanName(const std::string& in) {
    std::string o;
    for (char c : in) if (std::isalnum((unsigned char)c) || c == '-' || c == '_') o += (char)std::tolower((unsigned char)c);
    return o.substr(0, 24);
}
void Game::refreshFiles() {
    fileList.clear();
    std::error_code ec;
    std::vector<std::pair<std::filesystem::file_time_type, std::string>> found;
    for (auto& e : std::filesystem::directory_iterator(saveDir(), ec))
        if (e.is_regular_file(ec) && e.path().extension() == ".sbot") found.push_back({e.last_write_time(ec), e.path().stem().string()});
    std::sort(found.begin(), found.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (auto& f : found) fileList.push_back(f.second);
}
bool Game::writeFile(const std::string& name) {
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
bool Game::readFile(const std::string& name) {
    std::ifstream f(saveDir() + "/" + name + ".sbot", std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    leavePlay();
    pushUndo(nullptr, ("Open " + name).c_str());
    if (!restoreState(buf)) return false;
    clearSelection();
    focusBody = -1;
    setCam(0);
    phys.stampBodies();
    return true;
}
void Game::saveQuick() {
    if (currentFile.empty()) { openFileDialog(true); return; }
    notify(writeFile(currentFile) ? "Saved " + currentFile + (playing ? " (as drawn, not mid-run)" : "") : "Save failed");
}
void Game::openFileDialog(bool save) {
    refreshFiles();
    fileOpen = true; fileSave = save;
    fileSel = -1;
    fileName = save ? currentFile : (fileList.empty() ? std::string() : fileList[0]);
    if (!save && !fileList.empty()) fileSel = 0;
}
bool Game::fileDialogAccept() {
    std::string name = cleanName(fileName);
    if (name.empty()) { notify(fileSave ? "Type a name" : "Type or pick a name"); return false; }
    if (fileSave) {
        if (!writeFile(name)) { notify("Save failed"); return false; }
        currentFile = name;
        notify("Saved saves/" + name + ".sbot" + (playing ? " (as drawn)" : ""));
    } else {
        if (!readFile(name)) { notify("Could not load " + name); return false; }
        currentFile = name;
        notify("Loaded " + name + ". Press Play to run");
    }
    fileOpen = false;
    return true;
}

// ---------------------------------------------------------------- undo / redo
std::vector<uint8_t> Game::packState(const std::vector<uint8_t>& in) {
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
std::vector<uint8_t> Game::unpackState(const std::vector<uint8_t>& in) {
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
// Remember the drawing as it is NOW, just before an edit changes it. A burst of the same kind of edit (scrubbing a field,
// painting strokes) shares one undo step. The label names the edit for the History tab.
bool Game::pushUndo(const char* key, const char* label) {
    if (playing) return false;
    Uint32 now = SDL_GetTicks();
    if (key && lastUndoKey == key && now - lastUndoTick < 1500) { lastUndoTick = now; return false; }
    std::vector<uint8_t> raw;
    captureState(raw);
    undoStack.push_back(packState(raw));
    undoLabels.push_back(label ? label : key ? key : "Edit");
    undoBytes += undoStack.back().size();
    while (!undoStack.empty() && (undoStack.size() > 60 || undoBytes > (size_t)120 << 20)) {
        undoBytes -= undoStack.front().size();
        undoStack.erase(undoStack.begin());
        undoLabels.erase(undoLabels.begin());
    }
    redoStack.clear();
    redoLabels.clear();
    lastUndoKey = key ? key : "";
    lastUndoTick = now;
    return true;
}
// The press of a drag remembers the state the way pushUndo does; when nothing was pushed (the stroke merged into the previous
// one, or the simulation is paused) a private copy is kept instead, so Esc can still put the world back without touching an
// entry that belongs to an earlier edit.
void Game::beginDragUndo(const char* key, const char* label) {
    dragBackup.clear();
    dragPushed = pushUndo(key, label);
    dragUndoDepth = undoStack.size();
    if (!dragPushed) captureState(dragBackup);
}
void Game::cancelDragUndo() {
    if (dragPushed && undoStack.size() == dragUndoDepth) popUndo();   // nothing else was pushed since the press
    else if (!dragBackup.empty()) restoreState(dragBackup);
    dragPushed = false; dragBackup.clear();
}
void Game::popUndo() {
    if (undoStack.empty()) return;
    std::vector<uint8_t> prev = unpackState(undoStack.back());
    undoBytes -= undoStack.back().size();
    undoStack.pop_back();
    undoLabels.pop_back();
    restoreState(prev);
    lastUndoKey.clear();
}
void Game::undo() {
    if (lmb) { cancelDrag(); return; }   // mid-drag, undo means: abandon the drag (the stack must not change under it)
    if (playing) { notify("Undo works in edit mode: press Stop first (Stop restores the drawing)"); return; }
    if (undoStack.empty()) { notify("Nothing to undo"); return; }
    std::vector<uint8_t> raw;
    captureState(raw);
    redoStack.push_back(packState(raw));
    redoLabels.push_back(undoLabels.back());
    std::vector<uint8_t> prev = unpackState(undoStack.back());
    undoBytes -= undoStack.back().size();
    undoStack.pop_back();
    undoLabels.pop_back();
    if (!restoreState(prev)) { notify("Undo failed"); return; }
    lastUndoKey.clear();
    notify("Undo: " + redoLabels.back() + " (Ctrl+Y redoes)");
}
void Game::redo() {
    if (lmb) { cancelDrag(); return; }
    if (playing) { notify("Redo works in edit mode"); return; }
    if (redoStack.empty()) { notify("Nothing to redo"); return; }
    std::vector<uint8_t> raw;
    captureState(raw);
    undoStack.push_back(packState(raw));
    undoLabels.push_back(redoLabels.back());
    undoBytes += undoStack.back().size();
    std::vector<uint8_t> next = unpackState(redoStack.back());
    redoStack.pop_back();
    redoLabels.pop_back();
    if (!restoreState(next)) { notify("Redo failed"); return; }
    lastUndoKey.clear();
    notify("Redo: " + undoLabels.back());
}
// The History tab lists every undo step, then "Now", then the redo steps; clicking a row undoes or redoes up to it.
// The entries between here and the target move from one stack to the other as undo / redo would move them, but only the
// target state is unpacked and restored: a click far down the list costs one restore, not one per step.
void Game::jumpHistory(int row) {
    if (lmb) cancelDrag();
    if (playing) return;
    const int now = (int)undoStack.size();
    if (row == now || row < 0 || row > now + (int)redoStack.size()) return;
    std::vector<uint8_t> raw;
    captureState(raw);
    std::vector<uint8_t> cur = packState(raw);   // the state the stacks see as "now", moving along as entries change sides
    if (row < now) {
        for (int i = 0; i < now - row; ++i) {
            redoStack.push_back(std::move(cur)); redoLabels.push_back(undoLabels.back());
            cur = std::move(undoStack.back()); undoBytes -= cur.size();
            undoStack.pop_back(); undoLabels.pop_back();
        }
    } else {
        for (int i = 0; i < row - now; ++i) {
            undoBytes += cur.size();
            undoStack.push_back(std::move(cur)); undoLabels.push_back(redoLabels.back());
            cur = std::move(redoStack.back());
            redoStack.pop_back(); redoLabels.pop_back();
        }
    }
    if (!restoreState(unpackState(cur))) { notify("History jump failed"); return; }
    lastUndoKey.clear();
    notify(row < now ? "Undo: " + redoLabels.back() : "Redo: " + undoLabels.back());
}

// ---------------------------------------------------------------- copy / paste
void Game::copySelection() {
    pruneSelection();
    if (sel.empty()) { notify("Select bodies first, then Ctrl+C"); return; }
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
    notify("Copied " + std::to_string(clip.bodies.size()) + " bodies (Ctrl+V pastes at the pointer)");
}
void Game::pasteClipboard() { pasteAt(inSim ? snap(mouse) : Vec2(camXf + viewW() * 0.5f, camYf + viewH() * 0.4f)); }
void Game::pasteAt(Vec2 target) {
    if (clip.bodies.empty()) { notify("Nothing copied yet (select, then Ctrl+C)"); return; }
    pushUndo(nullptr, "Paste");
    Vec2 delta = target - clip.center;
    std::vector<int> ids;
    std::vector<std::pair<int, int>> groupMap, bondMap;
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
    sel = ids; primary = ids[0]; partMode = false; selJoint = -1;
    phys.stampBodies();
    notify("Pasted " + std::to_string(ids.size()) + " bodies: drag them into place");
}
void Game::selectAll() {
    sel.clear();
    for (auto& b : phys.bodies) if (b.alive) sel.push_back(b.id);
    primary = sel.empty() ? -1 : sel[0];
    partMode = false; selJoint = -1;
    notify("Selected " + std::to_string(sel.size()) + " bodies");
}

// ---------------------------------------------------------------- picking, moving, cutting
// Clicking the same spot again steps down through the bodies stacked under the cursor.
int Game::pickCycling(Vec2 p) {
    std::vector<int> ids = phys.bodiesAt(p);   // ascending: the last one is on top
    if (ids.empty()) return -1;
    Uint32 now = SDL_GetTicks();
    bool same = length(p - lastClickPos) < 2.f && ids.size() == lastClickCount && now - lastClickTick < 2500;
    cycleIdx = same ? (cycleIdx + (int)ids.size() - 1) % (int)ids.size() : (int)ids.size() - 1;
    lastClickPos = p; lastClickTick = now; lastClickCount = ids.size();
    if (ids.size() > 1) notify("Body " + std::to_string((int)ids.size() - cycleIdx) + " of " + std::to_string(ids.size()) + " under the pointer: click again for the next, hold for the list");
    return ids[cycleIdx];
}
int Game::topBodyAt(Vec2 p, bool preferSelected) {
    std::vector<int> ids = phys.bodiesAt(p);
    if (ids.empty()) return -1;
    if (preferSelected)
        for (int k = (int)ids.size() - 1; k >= 0; --k)
            if (std::find(sel.begin(), sel.end(), ids[k]) != sel.end()) return ids[k];
    return ids.back();
}
void Game::moveSelectionBy(Vec2 delta) {
    if (sel.empty() || (delta.x == 0.f && delta.y == 0.f)) return;
    phys.translateBodies(sel, delta);
}
std::vector<int> Game::wholeGroups(const std::vector<int>& ids) const {   // the ids with every group they touch completed
    std::vector<int> out = ids;
    for (int id : ids) {
        int g = phys.bodies[id].group;
        if (g < 0) continue;
        for (int m : phys.groupMembers(g)) if (std::find(out.begin(), out.end(), m) == out.end()) out.push_back(m);
    }
    return out;
}
std::pair<Vec2, Vec2> Game::bodiesBox(const std::vector<int>& ids) {   // the tight bounding box of some bodies: {min, max}
    Vec2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
    for (int id : ids)
        for (Vec2 p : bodyOutline(phys.bodies[id])) {
            lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
            hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
        }
    return {lo, hi};
}
// Mirror the selection (whole groups) about the centre of its bounding box, left-right or top-bottom. Joints, welds, fans,
// emitters and motors follow (Physics::flipBodies). Works while stopped or paused, like moving.
void Game::flipSelection(bool horizontal) {
    pruneSelection();
    if (sel.empty()) { notify(horizontal ? "Select bodies to flip first (Ctrl+H)" : "Select bodies to flip first (Ctrl+Shift+H)"); return; }
    std::vector<int> ids = wholeGroups(sel);
    std::pair<Vec2, Vec2> box = bodiesBox(ids);
    pushUndo(nullptr, horizontal ? "Flip left-right" : "Flip top-bottom");
    phys.flipBodies(ids, horizontal, (box.first + box.second) * 0.5f);
    phys.stampBodies();
    notify(horizontal ? "Flipped left-right" : "Flipped top-bottom");
}
// Copy the selection with its joints and welds. The copy lands at the pointer, or 1 cell right of and below the originals
// when the pointer is off the view, and becomes the selection. The clipboard is left as it was.
void Game::duplicateSelection() {
    pruneSelection();
    if (sel.empty()) { notify("Select bodies to duplicate first (Ctrl+D)"); return; }
    Clip saved = clip;
    Vec2 from = bodiesBox(sel).first;
    copySelection();
    pasteClipboard();   // one undo entry; at the pointer, or at the view centre
    undoLabels.back() = "Duplicate";
    if (!inSim) moveSelectionBy(from + Vec2(1.f, 1.f) - bodiesBox(sel).first);
    clip = saved;
    phys.stampBodies();
    notify("Duplicated " + std::to_string(sel.size()) + " bodies: drag them into place");
}
// The Cut tool: a box or circle dragged over the picture is cut out of every body it touches.
void Game::applyCutShape(Vec2 a, Vec2 b) {
    std::vector<int> targets;
    for (auto& bd : phys.bodies) if (bd.alive) targets.push_back(bd.id);
    const float r = length(b - a);
    const Vec2 half = Vec2(std::fabs(b.x - a.x), std::fabs(b.y - a.y)) * 0.5f;
    if (cutCircle && r < 1.5f) { notify("Drag to size the circle you want to cut out"); return; }
    if (!cutCircle && (half.x < 0.75f || half.y < 0.75f)) { notify("Drag to size the box you want to cut out"); return; }
    // the undo entry is taken before the temporary cutter body exists, or undoing the cut would bring the cutter back as a body
    pushUndo(nullptr, "Cut");
    const int cutter = cutCircle ? phys.addCircle(a, r, M_STEEL, true, false) : phys.addBox((a + b) * 0.5f, half, 0, M_STEEL, true);
    int bodiesCut = 0, pieces = 0;
    for (int t : targets) {
        if (t == cutter) continue;
        int r = phys.cutBody(t, {cutter});
        if (r >= 0) { ++bodiesCut; pieces += r; }
    }
    phys.removeBody(cutter);
    clearSelection();
    phys.stampBodies();
    if (bodiesCut) notify("Cut " + std::to_string(bodiesCut) + (bodiesCut == 1 ? " body" : " bodies") + " (" + std::to_string(pieces) + " pieces)");
    else notify("Nothing to cut there: the shape must overlap a box or circle (wheels, rockets, fans and emitters can't be cut)");
}
void Game::deleteSelection() {
    if (jointValid(selJoint)) { removeSelectedJoint(); return; }
    pruneSelection();
    if (sel.empty()) {
        int id = inSim ? phys.pickBody(mouse, true) : -1;
        if (id < 0) { notify("Select something to delete"); return; }
        pushUndo(nullptr, "Delete");
        phys.removeBody(id);
    } else {
        pushUndo(nullptr, "Delete");
        for (int id : std::vector<int>(sel)) phys.removeBody(id);
        clearSelection();
    }
    phys.stampBodies();
}
// in edit mode the arrow keys nudge whatever is selected (while playing they still drive motors and rockets):
// 1 cell, Shift 10 cells, Ctrl a quarter of a cell
void Game::nudgeSelection(SDL_Keycode k) {
    pruneSelection();
    if (playing || sel.empty()) return;
    const Uint16 mod = SDL_GetModState();
    float step = (mod & KMOD_SHIFT) ? 10.f : (mod & KMOD_CTRL) ? 0.25f : 1.f;
    Vec2 d(k == SDLK_RIGHT ? step : k == SDLK_LEFT ? -step : 0.f, k == SDLK_DOWN ? step : k == SDLK_UP ? -step : 0.f);
    pushUndo("nudge", "Nudge");
    moveSelectionBy(d);
    phys.stampBodies();
}

// ---------------------------------------------------------------- camera
void Game::setCam(float x, float y) {
    camXf = std::clamp(x, 0.f, std::max(0.f, (float)World::W - viewW()));
    if (y > -1e8f) camYf = y;
    camYf = std::clamp(camYf, 0.f, std::max(0.f, (float)World::H - viewH()));
    camX = (int)std::lround(camXf); camY = (int)std::lround(camYf);
}
// change the zoom one step, keeping the world point under (px, py) (window pixels) where it is
void Game::zoomStep(int dir, int px, int py) {
    int idx = 0;
    for (int i = 0; i < 7; ++i) if (std::fabs(ZOOMS[i] - zoom) < 0.01f) idx = i;
    int ni = std::clamp(idx + dir, 0, 6);
    if (ni == idx) { notify(dir > 0 ? "Maximum zoom" : "Already showing the whole height"); return; }
    Vec2 anchor = toWorld(px, py);
    float fx = (float)(px - L.canvas.x) / L.canvas.w, fy = (float)(py - L.canvas.y) / L.canvas.h;
    zoom = ZOOMS[ni];
    setCam(anchor.x - fx * viewW(), anchor.y - fy * viewH());
}
void Game::zoomCentre(int dir) { zoomStep(dir, L.canvas.x + L.canvas.w / 2, L.canvas.y + L.canvas.h / 2); }
void Game::zoomReset() { zoom = 1.f; setCam(camXf, 0.f); }
void Game::zoomFit() { zoom = 1.f; setCam(0.f, 0.f); }
Vec2 Game::focusPoint(bool& ok) const {
    ok = false;
    if (focusBody < 0 || focusBody >= (int)phys.bodies.size() || !phys.bodies[focusBody].alive) return Vec2();
    ok = true;
    const Body& b = phys.bodies[focusBody];
    if (b.group < 0) return b.pos;
    double m = 0, x = 0, y = 0;   // a group is followed by its centre of mass
    for (auto& o : phys.bodies) if (o.alive && o.group == b.group) { m += o.mass; x += o.pos.x * o.mass; y += o.pos.y * o.mass; }
    return m > 0 ? Vec2((float)(x / m), (float)(y / m)) : b.pos;
}
void Game::updateCamera(bool snapNow) {
    bool ok;
    Vec2 f = focusPoint(ok);
    if (focusBody >= 0 && !ok) { focusBody = -1; notify("Focus lost (the body is gone)"); }
    if (!ok || panning || scrubbing) return;
    float target = f.x - viewW() * 0.5f, targetY = f.y - viewH() * 0.5f;
    setCam(snapNow ? target : camXf + (target - camXf) * 0.14f, snapNow ? targetY : camYf + (targetY - camYf) * 0.14f);
}
void Game::toggleFocus() {
    if (focusBody >= 0) { focusBody = -1; notify("Focus off: the camera stays put"); return; }
    pruneSelection();
    if (primary < 0) { notify("Select a body first, then press Focus (F)"); return; }
    focusBody = primary;
    updateCamera(true);
    notify("Focus on: the camera follows this body. F again releases it");
}
// Shift+F: the largest zoom step at which the selection (grown by a fifth) fits the view, centred on it (within the
// camera clamps); with nothing selected the zoom resets
void Game::zoomToSelection() {
    pruneSelection();
    if (sel.empty()) { zoomReset(); notify("Nothing selected: zoom reset (Shift+F zooms in on a selection)"); return; }
    Vec2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
    for (int id : sel)
        for (Vec2 p : bodyOutline(phys.bodies[id])) {
            lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
            hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
        }
    Vec2 size = (hi - lo) * 1.2f, c = (lo + hi) * 0.5f;
    const float base = (float)L.canvas.h / World::H;
    int best = 0;
    for (int i = 0; i < 7; ++i) if (size.x * base * ZOOMS[i] <= L.canvas.w && size.y * base * ZOOMS[i] <= L.canvas.h) best = i;
    zoom = ZOOMS[best];
    bool hadFocus = focusBody >= 0;
    focusBody = -1;
    setCam(c.x - viewW() * 0.5f, c.y - viewH() * 0.5f);
    notify("Zoom " + fmt(zoom) + "x on the selection" + (hadFocus ? ", focus off" : ""));
}
void Game::zoomToBody(int id) {
    if (id < 0 || id >= (int)phys.bodies.size() || !phys.bodies[id].alive) return;
    std::vector<int> keep = sel; int kp = primary;
    sel = phys.bodies[id].group >= 0 ? phys.groupMembers(phys.bodies[id].group) : std::vector<int>{id};
    primary = id;
    zoomToSelection();
    sel = keep; primary = kp;
}
bool Game::inScrollStrip(int localY) const { return localY >= L.canvas.h - 12 && localY < L.canvas.h; }
void Game::scrubTo(int mx) { setCam((float)mx / L.canvas.w * World::W - viewW() * 0.5f); }
void Game::scrubToY(int my) { setCam(camXf, (float)my / L.canvas.h * World::H - viewH() * 0.5f); }
bool Game::inVStrip(int localX) const { return zoom > 1.01f && localX >= L.canvas.w - 12; }

// ---------------------------------------------------------------- creating things
void Game::createCircle(Vec2 c, float r, bool wheel) {
    int id = phys.addCircle(c, r, wheel && bodyMat == M_STEEL ? (uint8_t)M_RUBBER : bodyMat, anchored, wheel);
    if (wheel) {
        int under = phys.pickBody(c, true, id);
        if (under >= 0) phys.addPin(c, under, id, true, true);
    }
    sel = {id}; primary = id; partMode = false; selJoint = -1;
}
void Game::createShape(Vec2 a, Vec2 b, float angleDeg) {
    Vec2 d = b - a;
    const float ang0 = angleDeg * PI / 180.f;
    switch (tool) {
        case T_BOX: {
            Vec2 half = Vec2(std::fabs(d.x), std::fabs(d.y)) * 0.5f;
            Vec2 c = (a + b) * 0.5f;
            if (half.x < 2 || half.y < 2) { half = Vec2(7, 7); c = a; }
            int id = phys.addBox(c, half, ang0, bodyMat, anchored);
            sel = {id}; primary = id; partMode = false; selJoint = -1;
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
            float ang = ang0;
            if (half.x > half.y) { std::swap(half.x, half.y); ang += PI * 0.5f; }   // the long side is the blade span
            int id = phys.addBox(c, half, ang, bodyMat, anchored);
            phys.bodies[id].fan.strength = lastFan;
            phys.bodies[id].fan.vacuum = fanVacuumDefault ? 1 : 0;
            sel = {id}; primary = id; partMode = false; selJoint = -1;
            notify("Fan " + fmt(lastFan) + ": strength, mode and direction are in the Inspector");
            break;
        }
        case T_EMITTER: {
            Vec2 half = Vec2(std::fabs(d.x), std::fabs(d.y)) * 0.5f;
            Vec2 c = (a + b) * 0.5f;
            if (half.x < 1.5f || half.y < 1.5f) { half = Vec2(3, 3); c = a; }
            int id = phys.addBox(c, half, ang0, bodyMat, anchored);
            phys.bodies[id].src = Emitter{true, payload, lastRate, 0.f, emitFace};
            sel = {id}; primary = id; partMode = false; selJoint = -1;
            notify(std::string("Emitter of ") + MATS[payload].name + " at " + fmt(lastRate) + "/s");
            break;
        }
        case T_ROCKET: {
            float ang = length(d) > 4 ? std::atan2(d.x, -d.y) : 0.f;
            int id = phys.addRocket(a, ang);
            sel = {id}; primary = id; partMode = false; selJoint = -1;
            break;
        }
        case T_PIPE: case T_HOSE: {
            if (length(d) < 4) break;
            int g = tool == T_PIPE ? phys.addPipe(a, b, pipeD, pipeWall, bodyMat, anchored)
                                   : phys.addHose(a, b, pipeD, pipeWall, hoseSegs > 0 ? hoseSegs : autoSegs(a, b), bodyMat, anchored);
            if (g >= 0) { sel = phys.groupMembers(g); primary = sel.empty() ? -1 : sel[0]; partMode = false; selJoint = -1; }
            lastLen = length(d);
            break;
        }
        default: break;
    }
}
// The two bodies a click is meant to join: those under the pointer, or failing that the nearest ones within a few
// cells (bodies that merely touch have nothing under their seam). b = -1 means the world.
bool Game::pairAt(Vec2 p, int& a, int& b, float reach) {
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
void Game::clickJoint(Vec2 p) {
    int a, b;
    if (!pairAt(p, a, b)) { notify("Click on a body, or where two bodies meet"); return; }
    bool motor = tool == T_MOTOR || tool == T_AUTOMOTOR;
    if (b >= 0 && phys.bodies[b].seq < phys.bodies[a].seq) std::swap(a, b);
    pushUndo(nullptr, motor ? "Add motor" : "Add pin");
    int j = phys.addPin(p, a, b, motor, tool != T_AUTOMOTOR);
    selectJoint(j);
    notify(b >= 0 ? "Joined the two bodies" : "Pinned to the world (no other body near the click)");
}
void Game::clickBond(Vec2 p) {
    int a, b;
    if (!pairAt(p, a, b)) { notify("Click on a body, or where two bodies meet"); return; }
    pushUndo(nullptr, "Add bond");
    phys.addBond(p, a, b, bondT, 0.f, bondG);
    notify(std::string(b >= 0 ? "Bonded the two bodies" : "Bonded to the world") + ": melts at " + fmt(bondT) + " C, holds " + fmt(bondG) + "x its weight");
}

// ---- handles. HSX / HSY index the eight box handles, corners and edge midpoints in the body's own frame (a circle uses the
// four edge ones, which set its radius); H_ROT is the round handle on a stalk above the top edge. They keep a constant size
// on screen, so their geometry is in pixels divided by sc().
Vec2 Game::bodyExtent(const Body& b) { return b.shape == SHAPE_BOX ? b.half : Vec2(b.radius, b.radius); }
Vec2 Game::handlePos(const Body& b, int h) const {
    Vec2 e = bodyExtent(b);
    if (h == H_ROT) return b.toWorld(Vec2(0, -e.y - STALK_PX / sc()));
    return b.toWorld(Vec2(HSX[h] * e.x, HSY[h] * e.y));
}
// the body whose handles are shown: the one selected body (or the primary part of a group), or the primary of a whole
// selected group, which only gets the rotation handle; none while the simulation runs unpaused
int Game::handleBody() const {
    if (tool != T_SELECT || (playing && !paused) || primary < 0 || primary >= (int)phys.bodies.size() || !phys.bodies[primary].alive) return -1;
    return sel.size() == 1 || partMode || phys.bodies[primary].group >= 0 ? primary : -1;
}
bool Game::handleResizes() const { return sel.size() == 1 || partMode; }
bool Game::handleShown(const Body& b, int h) const { return h == H_ROT || (handleResizes() && (b.shape == SHAPE_BOX || (HSX[h] == 0) != (HSY[h] == 0))); }
// the handle under a point, or -1; the hit area is a little bigger than the drawn square
int Game::handleAt(Vec2 p) const {
    int id = handleBody();
    if (id < 0) return -1;
    const Body& b = phys.bodies[id];
    int best = -1;
    float bd = 6.f / sc();
    for (int h = 0; h <= H_ROT; ++h) {
        if (!handleShown(b, h)) continue;
        float d = length(p - handlePos(b, h));
        if (d < bd) { bd = d; best = h; }
    }
    return best;
}
// Every frame of a handle drag (and once more on release). The body is recomputed from how it was at the press, so the
// modifiers can change mid-drag and nothing drifts. Resize: the dragged edge or corner follows the pointer by the distance
// dragged (snapped to the grid when snap is on, so an edge lands on a grid line) and the opposite edge stays; Ctrl keeps the
// centre instead; Shift on a corner keeps the proportions. Rotate: by the angle the pointer has swept round the centre,
// in 15 degree steps with Shift. Values typed into the dimension field replace the pointer's. Joints follow through
// reshape / transformGroup.
void Game::updateHandleDrag() {
    if (!lmb || handle < 0) return;
    if (handleId < 0 || handleId >= (int)phys.bodies.size() || !phys.bodies[handleId].alive) { handle = -1; return; }
    const Uint16 mod = SDL_GetModState();
    const bool ctrl = (mod & KMOD_CTRL) != 0, shift = (mod & KMOD_SHIFT) != 0;
    const Body& s = handleStart;
    if (handle == H_ROT) {
        float a0 = std::atan2(handlePress.y - s.pos.y, handlePress.x - s.pos.x), a1 = std::atan2(mouse.y - s.pos.y, mouse.x - s.pos.x);
        float ang = s.angle + (a1 - a0);
        if (shift) ang = std::round(ang / (PI / 12.f)) * (PI / 12.f);
        float deg = std::fmod(ang * 180.f / PI, 360.f); if (deg < 0) deg += 360.f;
        ang = dimValue(0, deg) * PI / 180.f;
        if (handleResizes()) phys.reshape(handleId, s.pos, s.half, s.radius, ang, s.mat, s.isStatic);
        else phys.transformGroup(handleId, s.pos, ang);
        return;
    }
    Vec2 hp = handlePos(s, handle) + (mouse - handlePress);
    if (snapActive()) hp = snap(hp);
    Vec2 l = s.toLocal(hp), e = bodyExtent(s);
    const int sx = HSX[handle], sy = HSY[handle];
    if (s.shape == SHAPE_CIRCLE) {
        float r = std::max(0.5f, sx ? sx * l.x : sy * l.y);
        r = std::clamp(dimValue(0, r), 0.5f, 300.f);
        phys.reshape(handleId, s.pos, s.half, r, s.angle, s.mat, s.isStatic);
        return;
    }
    // a typed width or height (clamped to the inspector's range) places the dragged edge where that size would put it
    if (sx && dimTyped(0)) { float w = std::clamp(dimValue(0, 0), 1.f, 600.f); l.x = ctrl ? sx * w * 0.5f : -sx * e.x + sx * w; }
    if (sy && dimTyped(sx ? 1 : 0)) { float h = std::clamp(dimValue(sx ? 1 : 0, 0), 1.f, 600.f); l.y = ctrl ? sy * h * 0.5f : -sy * e.y + sy * h; }
    Vec2 half = e, c;   // the new half extents, and the new centre in the frame of the body as it was
    auto axis = [&](int sgn, float lp, float ext, float& h, float& cc) {
        if (!sgn) return;
        if (ctrl) { h = std::max(0.5f, sgn * lp); return; }
        float fixed = -sgn * ext, edge = fixed + sgn * std::max(1.f, sgn * (lp - fixed));   // never thinner than one cell
        h = sgn * (edge - fixed) * 0.5f;
        cc = (edge + fixed) * 0.5f;
    };
    axis(sx, l.x, e.x, half.x, c.x);
    axis(sy, l.y, e.y, half.y, c.y);
    if (shift && sx && sy && !dimTyped(0) && !dimTyped(1)) {
        half = e * std::max(half.x / e.x, half.y / e.y);
        if (!ctrl) c = Vec2(sx * (half.x - e.x), sy * (half.y - e.y));
    }
    phys.reshape(handleId, s.toWorld(c), half, s.radius, s.angle, s.mat, s.isStatic);
}
// the live dimensions shown next to the pointer during a handle drag
std::string Game::handleReadout() const {
    if (handle < 0 || handleId < 0 || handleId >= (int)phys.bodies.size() || !phys.bodies[handleId].alive) return "";
    const Body& b = phys.bodies[handleId];
    if (handle == H_ROT) { float deg = std::fmod(b.angle * 180.f / PI, 360.f); if (deg < 0) deg += 360.f; return fmt(deg) + " deg"; }
    return b.shape == SHAPE_BOX ? fmt(b.half.x * 2) + " x " + fmt(b.half.y * 2) : "R " + fmt(b.radius);
}
// Starts moving the selected bodies once the pointer has really dragged; also called on release so that a quick
// press-drag-release inside one frame still moves them. With snap on, the primary body's resulting position is snapped
// (not the displacement), so a dropped body lands on grid coordinates; smart snap then pulls its edges and centre onto
// other bodies' edges and centres. Typed X / Y values place the primary body exactly.
void Game::updateMoveDrag() {
    if (!lmb || tool != T_SELECT || !moveArmed) return;
    Vec2 total = mouse - dragStart;
    if (!moving) {
        if (length(total) < 2.5f) return;
        if (moveHit < 0 || moveHit >= (int)phys.bodies.size() || !phys.bodies[moveHit].alive) { moveArmed = false; return; }   // gone since the press
        const bool add = (SDL_GetModState() & KMOD_SHIFT) != 0;
        if (std::find(sel.begin(), sel.end(), moveHit) == sel.end()) selectBody(moveHit, add, false);
        pruneSelection();
        if (sel.empty()) { moveArmed = false; return; }
        beginDragUndo(nullptr, "Move");
        moving = true;
        moveApplied = Vec2();
        moveStart = phys.bodies[primary >= 0 ? primary : moveHit].pos;
        dimBegin(2, "X", "Y");
    }
    Vec2 target = snapActive() ? snap(moveStart + total) - moveStart : total;
    guide.on = false;
    if (smartSnap && !dimTyped(0) && !dimTyped(1)) smartSnapMove(target);
    if (dimTyped(0)) target.x = dimValue(0, 0) - moveStart.x;
    if (dimTyped(1)) target.y = dimValue(1, 0) - moveStart.y;
    dim.shown[0] = moveStart.x + target.x; dim.shown[1] = moveStart.y + target.y;
    moveSelectionBy(target - moveApplied);
    moveApplied = target;
}
// Smart snap: the primary body's left / centre / right (top / middle / bottom) stick to the same lines of any body outside
// the selection within 6 px; the guide line it used is drawn and named in the status bar.
void Game::smartSnapMove(Vec2& target) {
    if (primary < 0 || primary >= (int)phys.bodies.size() || !phys.bodies[primary].alive) return;
    const Body& pb = phys.bodies[primary];
    Vec2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
    for (Vec2 p : bodyOutline(pb)) { lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); }
    // where the primary body would be with the move applied so far replaced by `target`
    Vec2 shift = target - moveApplied;
    lo += shift; hi += shift;
    const float reach = 6.f / sc();
    const float myX[3] = {lo.x, (lo.x + hi.x) * 0.5f, hi.x}, myY[3] = {lo.y, (lo.y + hi.y) * 0.5f, hi.y};
    static const char* nx[3] = {"left", "centre", "right"}, *ny[3] = {"top", "middle", "bottom"};
    struct Cand { float dist = 1e9f, delta = 0.f; Guide g; int who = -1; const char* line = ""; } bx, by;   // the nearest line on each axis
    for (auto& o : phys.bodies) {
        if (!o.alive || std::find(sel.begin(), sel.end(), o.id) != sel.end()) continue;
        Vec2 olo(1e9f, 1e9f), ohi(-1e9f, -1e9f);
        for (Vec2 p : bodyOutline(o)) { olo.x = std::min(olo.x, p.x); olo.y = std::min(olo.y, p.y); ohi.x = std::max(ohi.x, p.x); ohi.y = std::max(ohi.y, p.y); }
        const float ox[3] = {olo.x, (olo.x + ohi.x) * 0.5f, ohi.x}, oy[3] = {olo.y, (olo.y + ohi.y) * 0.5f, ohi.y};
        for (int i = 0; i < 3; ++i)
            for (int k = 0; k < 3; ++k) {
                float dx = ox[k] - myX[i], dy = oy[k] - myY[i];
                if (std::fabs(dx) < bx.dist) bx = Cand{std::fabs(dx), dx, Guide{true, true, ox[k], std::min(olo.y, lo.y), std::max(ohi.y, hi.y), ""}, o.id, nx[k]};
                if (std::fabs(dy) < by.dist) by = Cand{std::fabs(dy), dy, Guide{true, false, oy[k], std::min(olo.x, lo.x), std::max(ohi.x, hi.x), ""}, o.id, ny[k]};
            }
    }
    const bool snapX = bx.dist < reach, snapY = by.dist < reach;
    Cand* use = snapX && (!snapY || bx.dist <= by.dist) ? &bx : snapY ? &by : nullptr;
    if (snapX) target.x += bx.delta;
    if (snapY) target.y += by.delta;
    if (use) {   // the guide is named only for the line that won: no string per body per frame
        guide = use->g;
        guide.name = std::string(use->line) + " of " + bodyName(phys.bodies[use->who]) + " " + std::to_string(use->who);
    }
}

// ---------------------------------------------------------------- the tool contract: press, drag, release / Enter, Esc
void Game::handleSimDown() {
    lmb = true;
    dragStart = smouse();
    moving = false; moveArmed = false;
    guide.on = false;
    dragPushed = false; dragBackup.clear();
    switch (tool) {
        case T_MAT: beginDragUndo("paint", mat == M_EMPTY ? "Erase" : ("Paint " + std::string(MATS[mat].name)).c_str()); break;
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
        case T_SELECT: {
            dragStart = mouse;
            int h = handleAt(mouse);
            if (h >= 0) {   // a press on a handle resizes or rotates; it never starts a move or a box-select
                handle = h; handleId = handleBody(); handleStart = phys.bodies[handleId]; handlePress = mouse;
                beginDragUndo(nullptr, h == H_ROT ? "Rotate" : "Resize");
                if (h == H_ROT) dimBegin(1, "Angle");
                else if (handleStart.shape == SHAPE_CIRCLE) dimBegin(1, "R");
                else if (HSX[h] && HSY[h]) dimBegin(2, "W", "H");
                else dimBegin(1, HSX[h] ? "W" : "H");
                break;
            }
            moveHit = selFilter == 2 ? -1 : topBodyAt(mouse, true);   // a press on a selected body drags the whole selection
            moveArmed = moveHit >= 0;
            break;
        }
        case T_DELETE: {
            int id = phys.pickBody(mouse, true);
            int j = id < 0 ? phys.nearestJoint(mouse, 6.f) : -1;
            if (id < 0 && j < 0) break;
            pushUndo(nullptr, "Delete");
            if (id >= 0) phys.removeBody(id); else phys.removeJoint(j);
            phys.stampBodies();
            break;
        }
        case T_MEASURE: measA = measB = snap(mouse); measOn = true; break;
        case T_BOX: case T_FAN: case T_EMITTER: dimBegin(3, "W", "H", "Angle"); break;
        case T_CIRCLE: case T_WHEEL: dimBegin(1, "R"); break;
        case T_PIPE: case T_HOSE: dimBegin(2, "L", "D"); break;
        default: break;
    }
}
void Game::handleSimUp() {
    if (!lmb) return;
    Vec2 m = smouse();
    auto live = [&](int id) { return id >= 0 && id < (int)phys.bodies.size() && phys.bodies[id].alive; };
    if (!live(dragBody)) dragBody = -1;   // the body pressed on may have gone (deleted, or the world replaced) during the drag
    switch (tool) {
        case T_BOX: case T_FAN: case T_EMITTER: case T_CIRCLE: case T_WHEEL: case T_ROCKET: case T_PIPE: case T_HOSE: {
            Vec2 d = m - dragStart;
            float ang = 0.f;
            // typed sizes are clamped to what the inspector accepts, so a stray digit cannot make a world-sized body
            if (tool == T_BOX || tool == T_FAN || tool == T_EMITTER) {
                float w = std::clamp(std::fabs(dimValue(0, std::fabs(d.x))), 0.f, 600.f), h = std::clamp(std::fabs(dimValue(1, std::fabs(d.y))), 0.f, 600.f);
                ang = dimValue(2, 0.f);
                m = dragStart + Vec2(d.x < 0 ? -w : w, d.y < 0 ? -h : h);
            } else if (tool == T_CIRCLE || tool == T_WHEEL) {
                float r = std::clamp(std::fabs(dimValue(0, length(d))), 0.f, 300.f);
                Vec2 dir = length(d) > 1e-3f ? normalize(d) : Vec2(1, 0);
                m = dragStart + dir * r;
            } else if (tool == T_PIPE || tool == T_HOSE) {
                float len = std::clamp(std::fabs(dimValue(0, length(d))), 0.f, (float)World::W);
                if (dimTyped(1)) { pipeD = std::clamp(dimValue(1, pipeD), 3.f, 60.f); pipeWall = std::min(pipeWall, pipeD * 0.5f); }
                Vec2 dir = length(d) > 1e-3f ? normalize(d) : Vec2(1, 0);
                m = dragStart + dir * len;
            }
            pushUndo(nullptr, ("Add " + std::string(toolInfo(tool).name)).c_str());
            createShape(dragStart, m, ang);
            break;
        }
        case T_CUT: applyCutShape(dragStart, m); break;
        case T_SELECT: {
            const Uint16 mod = SDL_GetModState();
            const bool add = (mod & KMOD_SHIFT) != 0, part = (mod & KMOD_CTRL) != 0;
            if (handle >= 0) {   // end of a resize or rotation: the cover cells catch up
                updateHandleDrag();
                handle = -1; handleId = -1;
                phys.stampBodies();
                break;
            }
            if (moveArmed) updateMoveDrag();
            if (moving) { moving = false; moveArmed = false; guide.on = false; phys.stampBodies(); break; }
            moveArmed = false;
            if (length(mouse - dragStart) < 3.f) {
                int jj = selFilter == 1 ? -1 : jointAt(mouse);
                if (jj >= 0) selectJoint(jj);
                else if (selFilter == 2) { if (!add) clearSelection(); }
                else selectBody(pickCycling(mouse), add, part);
            }
            else boxSelect(dragStart, mouse, add, part, mouse.x < dragStart.x);
            break;
        }
        case T_ROD: case T_SPRING: {
            std::vector<int> ids = phys.bodiesAt(mouse);
            int endBody = ids.empty() ? -1 : ids.back();
            if (length(mouse - dragStart) > 3.f && (dragBody >= 0 || endBody >= 0) && dragBody != endBody) {
                pushUndo(nullptr, tool == T_SPRING ? "Add spring" : "Add rod");
                int jid = phys.addDistance(dragBody, dragStart, endBody, mouse, tool == T_SPRING ? springFreq : 0.f);
                if (jid >= 0 && tool == T_SPRING) phys.joints[jid].damping = springDamp;
                if (jid >= 0) selectJoint(jid);
            }
            dragBody = -1;
            break;
        }
        case T_SLIDER: {
            if (dragBody < 0) { notify("Press on the body that should slide, then drag along its line"); break; }
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
                pushUndo(nullptr, "Add slider");
                phys.addSliderRel(dragBody, host, dragStart, axis);
                notify("Slider: it now slides along the other body, wherever that goes");
            } else if (moving_) {
                pushUndo(nullptr, "Add slider");
                phys.addSlider(dragBody, axis);
                notify("Slider: it now slides along this line (fixed in the world)");
            } else {
                notify("A fixed body can't slide: untick Fixed or pick a moving body");
            }
            dragBody = -1;
            break;
        }
        case T_GRAB:
            if (grabJoint >= 0) phys.removeJoint(grabJoint);
            grabJoint = -1;
            dragBody = -1;
            break;
        case T_MEASURE: measB = snap(mouse); break;
        default: break;
    }
    lmb = false;
    dimEnd();
}
bool Game::dragInProgress() const { return lmb && (shapeTool(tool) || tool == T_CUT || tool == T_ROD || tool == T_SPRING || tool == T_SLIDER || tool == T_SELECT || tool == T_MAT || tool == T_GRAB || tool == T_MEASURE); }
void Game::commitDrag() { if (lmb) handleSimUp(); }
// Esc or a right click while something is being dragged: the world goes back to how it was and the undo entry the press
// made is dropped, so neither the world nor the undo stack remembers the attempt.
bool Game::cancelDrag() {
    if (!lmb) return false;
    if (tool == T_SELECT && handle >= 0) {
        if (handleId >= 0 && handleId < (int)phys.bodies.size() && phys.bodies[handleId].alive) {
            if (handleResizes()) phys.reshape(handleId, handleStart.pos, handleStart.half, handleStart.radius, handleStart.angle, handleStart.mat, handleStart.isStatic);
            else phys.transformGroup(handleId, handleStart.pos, handleStart.angle);
        }
        handle = -1; handleId = -1;
        cancelDragUndo();
        phys.stampBodies();
    } else if (tool == T_SELECT && moving) {
        moveSelectionBy(-moveApplied);
        moving = false; moveArmed = false; guide.on = false;
        cancelDragUndo();
        phys.stampBodies();
    } else if (tool == T_MAT) {
        cancelDragUndo();
        phys.stampBodies();
    } else if (tool == T_GRAB) {
        if (grabJoint >= 0) phys.removeJoint(grabJoint);
        grabJoint = -1;
    } else if (tool == T_MEASURE) measOn = false;
    moveArmed = false; dragBody = -1;
    lmb = false;
    dimEnd();
    return true;
}

// Held keys drive motors, rockets and spark plugs; held buttons paint, grab and drag. Called once per simulation step.
void Game::continuousInput() {
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    const bool typing = ui.wantsKeyboard() || dim.active;
    float m = 0;
    if (ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_D]) m += 1;
    if (ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_A]) m -= 1;
    phys.motorInput = typing ? 0.f : m;
    phys.thrustOn = !typing && (ks[SDL_SCANCODE_UP] || ks[SDL_SCANCODE_W]);
    world.sparkHeld = !typing && ks[SDL_SCANCODE_Z] && !(SDL_GetModState() & KMOD_CTRL);
    if (!inSim && !lmb) return;
    int cx = (int)mouse.x, cy = (int)mouse.y, lx = (int)lastMouse.x, ly = (int)lastMouse.y;
    if (lmb && tool == T_MAT) {
        const bool erase = mat == M_EMPTY || ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT];
        if (erase || paintReplace) world.paintLine(lx, ly, cx, cy, brush, M_EMPTY, 1.f);
        if (!erase) world.paintLine(lx, ly, cx, cy, brush, mat, 0.45f, payload);
    }
    if (lmb && tool == T_GRAB && grabJoint >= 0) phys.setMouseTarget(grabJoint, mouse);
    updateMoveDrag();
    updateHandleDrag();
    lastMouse = mouse;
}

// One simulation tick (60 Hz): the held input, the speed-scaled physics and grid steps, the camera.
void Game::update() {
    pruneSelection();
    continuousInput();
    if (!playing) phys.stampBodies();   // editing: keep the cover cells in step with what is drawn
    if (playing && !paused) {
        stepAcc += speed;
        while (stepAcc >= 1.f) {
            phys.step(1.f / 60.f);
            world.step();
            fanPhase += 1.f;
            stepAcc -= 1.f;
        }
    } else if (stepOnce && playing) {   // never a step in edit mode: the drawing only changes through edits
        phys.step(1.f / 60.f);
        world.step();
        fanPhase += 1.f;
    }
    stepOnce = false;
    updateCamera();
}

// "Select other": everything under the pointer, front to back, as a list to pick from.
void Game::openSelectOther(int px, int py) {
    selOther.clear();
    Vec2 p = toWorld(px, py);
    std::vector<int> ids = phys.bodiesAt(p);
    for (int i = (int)ids.size() - 1; i >= 0; --i) selOther.push_back({false, ids[i], bodyName(phys.bodies[ids[i]]) + " " + std::to_string(ids[i])});
    for (auto& j : phys.joints) {
        if (!jointValid(j.id)) continue;
        Vec2 a = phys.jointAnchorA(j);
        float d = length(p - a);
        if (j.type == J_DISTANCE) { Vec2 b = phys.jointAnchorB(j), ab = b - a; float t = lengthSq(ab) > 1e-6f ? std::clamp(dot(p - a, ab) / lengthSq(ab), 0.f, 1.f) : 0.f; d = length(p - (a + ab * t)); }
        if (d < 4.f) selOther.push_back({true, j.id, jointName(j)});
    }
    if (selOther.empty()) { notify("Nothing under the pointer"); return; }
    selOtherOpen = true; selOtherX = px; selOtherY = py; selOtherHover = -1;
}

// ---------------------------------------------------------------- the dimension field
void Game::dimBegin(int n, const char* a, const char* b, const char* c) {
    dim = DimField{};
    dim.active = true; dim.n = n; dim.cur = 0;
    dim.names[0] = a; dim.names[1] = b; dim.names[2] = c;
}
void Game::dimEnd() { dim = DimField{}; }
bool Game::dimKey(SDL_Keycode k) {
    if (!dim.active) return false;
    if (k == SDLK_TAB) { dim.cur = (dim.cur + 1) % dim.n; return true; }
    if (k == SDLK_BACKSPACE) {
        std::string& t = dim.text[dim.cur];
        if (!t.empty()) t.pop_back();
        if (t.empty()) dim.typed[dim.cur] = false;
        return true;
    }
    return false;
}
void Game::dimType(const std::string& s) {
    if (!dim.active) return;
    for (char c : s)
        if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == '*' || c == '/') {
            if (dim.text[dim.cur].size() < 12) { dim.text[dim.cur] += c; dim.typed[dim.cur] = true; }
        }
}
float Game::dimValue(int i, float fromMouse) {
    if (!dimTyped(i)) { if (dim.active && i < dim.n) dim.shown[i] = fromMouse; return fromMouse; }
    float v;
    if (!evalExpr(dim.text[i], v)) return fromMouse;
    dim.shown[i] = v;
    return v;
}
