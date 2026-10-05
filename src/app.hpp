#pragma once
// SandBots: a physics sandbox (rigid bodies, joints, motors) fused with a thermal falling-sand / fluid simulation.
// This header holds the application state (struct Game); the methods live in editor.cpp (editing, input, files, camera),
// scenes.cpp (ready-made scenes and test builders), render.cpp (drawing the world), commands.cpp (the command table and
// the keyboard map) and chrome.cpp (the interface built on the toolkit in ui.hpp). main.cpp parses the flags and runs.
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "font.hpp"
#include "icons.hpp"
#include "physics.hpp"
#include "sand.hpp"
#include "ui.hpp"

constexpr float PI = 3.14159265f;

// The tools. The numbering is kept stable because --tool N selects one by number.
enum Tool {
    T_MAT, T_BOX, T_CIRCLE, T_WHEEL, T_ROCKET, T_PIN, T_MOTOR, T_AUTOMOTOR, T_ROD, T_SPRING, T_GRAB, T_DELETE, T_SLIDER, T_SELECT, T_PIPE, T_HOSE,
    T_EMITTER, T_BOND, T_FAN, T_CUT, T_MEASURE, T_COUNT
};
// what every tool is called, how it is drawn, its key, one line of what it does and its mouse bindings for the status bar
struct ToolInfo { const char* name; icons::Id icon; const char* key; const char* what; const char* bindings; };
const ToolInfo& toolInfo(Tool t);
// the digit keys pick tools: 1 Select, 2 Grab, 3 Box, 4 Circle, 5 Wheel, 6 Pin, 7 Motor, 8 Rod, 9 Spring, 0 Paint
extern const Tool DIGIT_TOOLS[10];

// materials a rigid body can be made of (shown as swatches, never cycled through)
extern const std::vector<uint8_t> BODY_MATS;
// what an emitter body can produce
extern const std::vector<uint8_t> EMIT_MATS;
struct PaintGroup { const char* name; std::vector<uint8_t> mats; };
extern const std::vector<PaintGroup> PAINT_GROUPS;
extern const int SPARK_RATES[7];
extern const int SNAP_STEPS[4];
// bond presets: melting temperature (deg C) and the load each bond holds, as a multiple of the weight it carries
extern const char* BOND_NAMES[4];
extern const float BOND_TEMP[4];
extern const float BOND_G[4];

inline SDL_Color rgb(uint32_t c, uint8_t a = 255) { return SDL_Color{(uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c, a}; }
inline uint32_t shade(uint32_t c, float f) {
    auto ch = [&](int s) { return (uint32_t)std::clamp((int)(((c >> s) & 255) * f), 0, 255); };
    return 0xFF000000u | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}
inline uint32_t mix(uint32_t a, uint32_t b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    auto ch = [&](int s) {
        float va = (float)((a >> s) & 255), vb = (float)((b >> s) & 255);
        return (uint32_t)std::clamp((int)(va + (vb - va) * t), 0, 255);
    };
    return 0xFF000000u | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}
// the small face for text inside the canvas: world labels, rulers, dense read-outs
inline void smallText(SDL_Renderer* r, const std::string& s, int x, int y, int scale, SDL_Color c) { font::text(r, s, x, y, font::Face::Small, scale, c); }
inline int smallWidth(const std::string& s, int scale) { return font::width(s, font::Face::Small, scale); }
uint32_t heatColor(float T);   // temperature (deg C) -> false-colour heat map
uint32_t glowColor(uint32_t base, float T);
std::string fmt(float v);      // a number with up to two decimals and no trailing zeros
std::string matTip(uint8_t m); // what a material is like, for tooltips
bool evalExpr(const std::string& s, float& out);   // "+ - * /" arithmetic on numbers, for the dimension field

// The five zones, computed from the window size every frame; nothing is positioned from a constant.
struct Layout {
    int winW = 1600, winH = 900;
    SDL_Rect top{}, strip{}, ctx{}, canvas{}, dock{}, dockTabs{}, dockBody{}, status{};
    bool dockCollapsed = false;   // below 1200 px the dock is a column of tab icons and its body is a flyout over the canvas
    SDL_Rect flyout{};            // where the dock body goes when it is a flyout
};
Layout computeLayout(int winW, int winH, bool stripCollapsed);

struct Label { Vec2 p; std::string s; };

struct Game {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    SDL_Texture* tex = nullptr;
    World world;
    Physics phys{&world};
    std::vector<uint32_t> pixels = std::vector<uint32_t>(World::W * World::H);
    std::vector<Label> labels;

    Tool tool = T_MAT;
    uint8_t mat = M_SAND;        // the paint material (M_EMPTY = the eraser)
    uint8_t lastPaintMat = M_SAND;
    uint8_t bodyMat = M_STEEL;
    uint8_t payload = M_WATER;   // what new emitters (and SOURCE cells) emit
    int brush = 3;
    bool paintReplace = false;   // paint over cells that are already there
    bool paused = false, stepOnce = false, anchored = false, running = true, heatView = false;
    int sparkIdx = 2;

    // ---- selection and groups
    std::vector<int> sel;      // selected body ids (a group is always selected whole unless in part mode)
    int primary = -1;          // the last-clicked body: the one the inspector edits and the cutter of a subtract
    bool partMode = false;     // true = the selection is one component of a group
    int selJoint = -1;         // a selected joint (spring, rod, motor, pin, slider, bond)
    int selFilter = 0;         // what clicks and box-selects pick: 0 bodies and joints, 1 bodies, 2 joints
    // ---- snapping
    bool snapOn = false;       // round drawn shapes, dropped bodies and dragged edges to the grid
    int snapStep = 5;          // the grid: 1, 2, 5 or 10 cells
    bool smartSnap = true;     // a dragged body's edges and centre stick to other bodies' edges and centres
    struct Guide { bool on = false; bool vertical = false; float coord = 0.f, from = 0.f, to = 0.f; std::string name; } guide;
    // ---- editing: undo, clipboard, moving, cutting
    std::vector<std::vector<uint8_t>> undoStack, redoStack;
    std::vector<std::string> undoLabels, redoLabels;   // one line per step, for the History tab
    size_t undoBytes = 0;
    std::string lastUndoKey;
    Uint32 lastUndoTick = 0;
    // what the press of the drag in progress did to the undo stack, so a cancel can take exactly that back: whether it pushed an
    // entry (and the depth after it), or, when the entry was merged into the previous one or the simulation runs, a private copy
    bool dragPushed = false;
    size_t dragUndoDepth = 0;
    std::vector<uint8_t> dragBackup;
    struct Clip { std::vector<Body> bodies; std::vector<Joint> joints; Vec2 center; } clip;
    bool moving = false, moveArmed = false;
    Vec2 moveApplied;
    int moveHit = -1;
    Vec2 moveStart;            // where the primary body was when a move drag began: with snap on, the body lands on the grid
    // ---- handles: small squares on the selected body's edges and corners resize it, a round one on a stalk above it rotates it
    int handle = -1;           // the handle being dragged (an index into HSX / HSY, or H_ROT); -1 = none
    int handleId = -1;         // the body it belongs to
    Body handleStart;          // that body as it was at the press: every frame of the drag is computed from it, so nothing drifts
    Vec2 handlePress;          // the pointer at the press, in cells
    int cycleIdx = 0;
    Vec2 lastClickPos;
    Uint32 lastClickTick = 0;
    size_t lastClickCount = 0;
    bool cutCircle = false, keepCutter = false;
    float pipeD = 12.f, pipeWall = 2.f;
    int hoseSegs = 0;          // 0 = automatic
    float springFreq = 2.5f, springDamp = 0.35f;   // what new springs are made with
    float lastW = 40.f, lastH = 10.f, lastR = 8.f, lastLen = 60.f, lastScale = 98.f;
    // ---- run mode: the drawing is edited while stopped; PLAY snapshots it and STOP restores the snapshot
    bool playing = false;
    std::vector<uint8_t> snapshot;
    float speed = 1.f, stepAcc = 0.f;   // simulation speed 0.1x .. 2x
    std::string currentFile;
    std::vector<std::string> fileList;
    int bondType = 0;
    float bondT = 55.f, bondG = 10.f;
    // ---- camera: the canvas shows part of the wider world
    float camXf = 0.f, camYf = 0.f;   // top-left of the view, in cells
    int camX = 0, camY = 0;
    float zoom = 1.f;                 // 1 = the whole height of the world fits the canvas; higher shows fewer, bigger cells
    bool gridOn = true;               // reference grid and rulers
    static constexpr float ZOOMS[7] = {1.f, 1.5f, 2.f, 3.f, 4.f, 6.f, 8.f};
    float sc() const { return (float)L.canvas.h / World::H * zoom; }   // screen pixels per cell
    float viewW() const { return L.canvas.w / sc(); }                   // cells visible across / down
    float viewH() const { return L.canvas.h / sc(); }
    int scrX(float x) const { return L.canvas.x + (int)std::lround((x - camXf) * sc()); }   // world -> window pixels
    int scrY(float y) const { return L.canvas.y + (int)std::lround((y - camYf) * sc()); }
    Vec2 toWorld(int mx, int my) const { return Vec2((float)(mx - L.canvas.x) / sc() + camXf, (float)(my - L.canvas.y) / sc() + camYf); }
    bool inCanvasPx(int x, int y) const { return x >= L.canvas.x && x < L.canvas.x + L.canvas.w && y >= L.canvas.y && y < L.canvas.y + L.canvas.h; }
    int focusBody = -1;        // the camera follows this body when >= 0
    bool panning = false, scrubbing = false, scrubbingV = false;
    int panStartPx = 0, panStartPy = 0;
    float panStartCam = 0.f, panStartCamY = 0.f;
    bool fanVacuumDefault = false;
    bool elecView = false, pressureView = false;
    float fanPhase = 0.f;
    float lastFan = 60.f;
    uint8_t emitFace = 1;   // outlet side of new emitters: 0 all sides, 1 +x, 2 -x, 3 +y (down), 4 -y (up), in the emitter's own frame
    float lastRate = 30.f;
    std::string note;        // the last notification (toasts show it; the headless tests can read it)

    bool lmb = false;
    Vec2 mouse, lastMouse, dragStart;
    int dragBody = -1;
    int grabJoint = -1;
    bool inSim = false;      // the pointer is over the canvas and no popup has it
    float fps = 60.f;
    Vec2 measA, measB;       // the measure tool's last line
    bool measOn = false;

    // ---- the interface
    ui::Context ui;
    ui::Input in;
    Layout L;
    int winW = 1600, winH = 900;
    bool stripCollapsed = false;
    int dockTab = 0;                 // Inspector, Materials, Scene, History
    bool dockFlyout = false;         // narrow window: the dock body is shown over the canvas
    bool spaceHeld = false, spaceUsed = false;   // Space plays / pauses on release unless it was used to pan
    std::vector<ui::Command> commands;
    std::map<std::string, int> commandIndex;
    bool paletteOpen = false, cheatOpen = false, scenesOpen = false, fileOpen = false, fileSave = false, newConfirm = false, scaleOpen = false;
    std::string paletteQuery, fileName, matSearch, bodyMatSearch, sceneFilter, inspectorMatSearch;
    int fileSel = -1, sceneSel = -1;
    bool ctxOpen = false, ctxCanvas = false, matMenuOpen = false;   // the context menu, and its material submenu
    int ctxX = 0, ctxY = 0;
    Vec2 ctxWorld;
    bool selOtherOpen = false;       // the "select other" list of everything under the pointer
    int selOtherX = 0, selOtherY = 0, selOtherHover = -1;
    struct Pick { bool joint; int id; std::string name; };
    std::vector<Pick> selOther;
    Uint32 pressTick = 0;            // when the left button went down on the canvas (click-hold opens select other)
    int pressPx = 0, pressPy = 0;
    bool pressHeldOpen = false;
    std::vector<uint8_t> favourites, recentMats;
    std::vector<std::string> cheatSections;
    // the dimension field beside the pointer: typed values replace the mouse while a shape is drawn or a handle dragged
    struct DimField { bool active = false; int cur = 0, n = 0; std::string text[3]; bool typed[3] = {false, false, false}; const char* names[3] = {"", "", ""}; float shown[3] = {0, 0, 0}; } dim;
    // where the inspector's fields were drawn this frame, by name, so the headless editor test can click them
    std::map<std::string, SDL_Rect> fieldRects;

    // ---------------------------------------------------------------- setup (editor.cpp)
    bool init(bool headless, int w, int h);
    void shutdown();
    void screenshot(const char* path);
    void notify(const std::string& s);
    void selectMaterial(uint8_t m);
    void pickTool(Tool t);
    void setTool(Tool t);

    // ---------------------------------------------------------------- what the panels act on
    std::vector<int> selectedWith(const std::function<bool(const Body&)>& pred);
    std::vector<int> selFans() { return selectedWith([](const Body& b) { return b.fan.strength != 0.f; }); }
    std::vector<int> selEmitters() { return selectedWith([](const Body& b) { return b.src.on; }); }
    void setBodyMaterial(uint8_t m);
    void setFixed(bool fixed);
    void adjustFan(float d);
    void setFanStrength(float s);
    void flipFan();
    void setFanVacuum(bool v);
    void adjustRate(float d);
    void setRate(float r);
    void setEmitMaterial(uint8_t m);
    void setSelectionFace(int f);
    void scaleSelection(float pct);
    void alignSelection(int how);      // 0 left 1 centre 2 right 3 top 4 middle 5 bottom
    void distributeSelection(bool horizontal);
    void setBodyTransform(int id, Vec2 pos, Vec2 half, float radius, float angleDeg);   // one inspector edit
    void clearBodies();
    void resetWorld();

    // ---------------------------------------------------------------- scenes (scenes.cpp)
    int spawnCar(Vec2 c, float scale = 1.f);
    void rect(int x0, int y0, int x1, int y1, uint8_t m, uint8_t pl = 0);
    void gasRect(int x0, int y0, int x1, int y1, uint8_t m, float amt, float temp = -1e9f);
    void warm(int x0, int y0, int x1, int y1, float T);
    void label(float x, float y, const std::string& s);
    void buildDemo();
    void cylinder(int headX, int len, uint8_t wall);
    int lastWheel = -1;
    Vec2 lastCrank;
    int crankSlider(int headX, float pistonLen, float rod, float crankY, float crankR, float wheelR, uint8_t wheelMat);
    void gateValve(uint8_t wall, float phase = 0.f);
    void exhaustValve(uint8_t wall);
    void buildSteamEngine();
    void buildGasEngine();
    void buildDieselEngine();
    void buildHydraulics();
    void buildConduction();
    void buildFuels();
    void buildPressureTest();
    void buildTestScene(int which);
    void simDrag(Tool t, Vec2 a, Vec2 b);
    void buildScriptedScene();
    int exactBox(Vec2 c, float w, float h, float angleDeg);   // the scene builders' exact shapes (what the numeric form used to make)
    int exactCircle(Vec2 c, float r, bool wheel);
    int exactPipe(Vec2 a, Vec2 b, float d, float wall, bool hose, int segs);
    void buildPrecisionTest();
    void buildCutTest();
    void buildElectricTest();
    void buildBondTest();
    void buildPrimerTest();
    void buildFanTest();
    struct JetCfg { float fan = 100.f, fuel = 150.f, fuelU = 14.f, plugU = 18.f, len = 30.f, nozIn = 3.f, nozLen = 10.f, half = 11.5f; int spark = 1; bool space = false; };
    static JetCfg& jet();
    int buildJetCar();
    struct Shotgun { int hammer = -1, wad = -1; std::vector<int> shot; };
    Shotgun lastGun;
    void buildShotgun();
    void buildJet();
    void buildRoadTest();
    void buildEmitterTest();
    struct SceneDef { const char* name; void (Game::*fn)(); const char* tip; };
    static const std::vector<SceneDef>& sceneList();
    void loadScene(int i);

    // ---------------------------------------------------------------- selection / groups (editor.cpp)
    Vec2 snap(Vec2 p) const;
    bool snapActive() const;   // grid snap, with Ctrl inverting it
    bool shapeTool(Tool t) const;
    Vec2 smouse() const;
    void pruneSelection();
    bool jointValid(int j) const;
    int jointAt(Vec2 p) const;
    void selectJoint(int id);
    void removeSelectedJoint();
    template <class F> void editJoint(F f, const char* what = "Edit joint") {   // change the selected joint (every pin of a bond together)
        if (!jointValid(selJoint)) return;
        pushUndo("jointedit", what);
        int bond = phys.joints[selJoint].bondId;
        if (bond >= 0) { for (auto& k : phys.joints) if (k.alive && k.bondId == bond) f(k); }
        else f(phys.joints[selJoint]);
    }
    void clearSelection();
    void selectBody(int id, bool add, bool part);
    void boxSelect(Vec2 a, Vec2 b, bool add, bool part, bool crossing);
    void groupSelection();
    void ungroupSelection();
    void cutSelection();
    int autoSegs(Vec2 a, Vec2 b) const;
    bool fanModeVacuum() const;
    void toggleFanMode();
    static const char* faceName(int f);

    // ---------------------------------------------------------------- run mode, snapshots and files
    static constexpr uint32_t STATE_MAGIC = 0x31544253u;  // "SBT1"
    void captureState(std::vector<uint8_t>& out);
    bool inRestore = false;
    bool restoreState(const std::vector<uint8_t>& buf);
    void startPlay();
    void play();
    void stopPlay();
    void leavePlay();          // back to the drawing without a notification, before the world is replaced wholesale
    void togglePause();
    void stepFrame();
    void newFile();
    static std::string saveDir();
    static std::string cleanName(const std::string& in);
    void refreshFiles();
    bool writeFile(const std::string& name);
    bool readFile(const std::string& name);
    void saveQuick();
    void openFileDialog(bool save);
    bool fileDialogAccept();

    // ---------------------------------------------------------------- undo / redo
    static std::vector<uint8_t> packState(const std::vector<uint8_t>& in);
    static std::vector<uint8_t> unpackState(const std::vector<uint8_t>& in);
    bool pushUndo(const char* key = nullptr, const char* label = nullptr);   // true when an entry was pushed (not merged, not while playing)
    void popUndo();            // a cancelled edit: put the last snapshot back and drop it from the stack
    void beginDragUndo(const char* key, const char* label);   // the press of a drag: push, or keep a private copy for a cancel
    void cancelDragUndo();     // a cancelled drag: take back what beginDragUndo did
    void undo();
    void redo();
    void jumpHistory(int row);

    // ---------------------------------------------------------------- copy / paste / modify
    void copySelection();
    void pasteClipboard();
    void pasteAt(Vec2 target);
    void selectAll();
    int pickCycling(Vec2 p);
    int topBodyAt(Vec2 p, bool preferSelected);
    void moveSelectionBy(Vec2 delta);
    std::vector<int> wholeGroups(const std::vector<int>& ids) const;
    std::pair<Vec2, Vec2> bodiesBox(const std::vector<int>& ids);
    void flipSelection(bool horizontal);
    void duplicateSelection();
    void applyCutShape(Vec2 a, Vec2 b);
    void deleteSelection();
    void nudgeSelection(SDL_Keycode k);

    // ---------------------------------------------------------------- camera
    void setCam(float x, float y = -1e9f);
    void zoomStep(int dir, int px, int py);
    void zoomCentre(int dir);
    void zoomReset();
    void zoomFit();
    Vec2 focusPoint(bool& ok) const;
    void updateCamera(bool snap = false);
    void toggleFocus();
    void zoomToSelection();
    void zoomToBody(int id);
    bool inScrollStrip(int localY) const;
    void scrubTo(int mx);
    void scrubToY(int my);
    bool inVStrip(int localX) const;

    // ---------------------------------------------------------------- tools on the canvas
    void createCircle(Vec2 c, float r, bool wheel);
    void createShape(Vec2 a, Vec2 b, float angleDeg = 0.f);
    bool pairAt(Vec2 p, int& a, int& b, float reach = 3.5f);
    void clickJoint(Vec2 p);
    void clickBond(Vec2 p);
    static constexpr int H_ROT = 8;
    static constexpr int HSX[8] = {-1, 0, 1, 1, 1, 0, -1, -1}, HSY[8] = {-1, -1, -1, 0, 1, 1, 1, 0};
    static constexpr float HANDLE_PX = 3.5f, STALK_PX = 18.f;   // half the side of a handle square, and the length of the stalk
    static Vec2 bodyExtent(const Body& b);
    Vec2 handlePos(const Body& b, int h) const;
    int handleBody() const;
    bool handleResizes() const;
    bool handleShown(const Body& b, int h) const;
    int handleAt(Vec2 p) const;
    void updateHandleDrag();
    std::string handleReadout() const;
    void updateMoveDrag();
    void smartSnapMove(Vec2& target);
    void handleSimDown();
    void handleSimUp();
    void commitDrag();         // Enter: finish the drag in progress at the current (or typed) values
    bool cancelDrag();         // Esc or right click: abandon the drag in progress, world and undo stack untouched; false if none
    bool dragInProgress() const;
    void continuousInput();
    void update();
    void openSelectOther(int px, int py);

    // ---------------------------------------------------------------- the dimension field
    void dimBegin(int n, const char* a, const char* b = "", const char* c = "");
    void dimEnd();
    bool dimKey(SDL_Keycode k);               // true when the key went into the field
    void dimType(const std::string& s);
    float dimValue(int i, float fromMouse);    // the typed value when there is one, else the mouse's
    bool dimTyped(int i) const { return dim.active && i < dim.n && dim.typed[i] && !dim.text[i].empty(); }

    // ---------------------------------------------------------------- commands and keys (commands.cpp)
    void buildCommands();
    const ui::Command* cmd(const char* id) const;
    void runCommand(const char* id);
    const char* shortcutOf(const char* id) const;
    std::string tipOf(const char* id) const;   // "What it does  (Shortcut)" for tooltips
    bool handleKey(SDL_Keycode k, bool ctrl, bool shift);
    std::string toolBindings() const;

    // ---------------------------------------------------------------- the frame (chrome.cpp)
    void handleEvents();       // poll SDL events into the input of one frame, then run that frame
    void pollEvents();
    void frame();              // ui.begin, canvas input, world, chrome, popups, ui.end: one immediate-mode pass
    void render() { frame(); }
    void canvasInput();
    void drawChrome();
    void drawTopBar();
    void drawToolStrip();
    void drawContextBar();
    void drawDock();
    void drawInspector();
    void drawMaterials();
    void drawSceneTab();
    void drawHistory();
    void drawStatusBar();
    void drawPopups();
    void drawContextMenu();
    void drawCheatSheet();
    void drawScenesDialog();
    void drawFileDialog();
    void drawSelectOther();
    void drawCanvasOverlays();
    void openContextMenu(int px, int py);
    void materialChip(const char* id, uint8_t m, bool paint, const char* tip);
    void bodyMaterialPicker(const char* id, std::string& search, uint8_t current, const std::function<void(uint8_t)>& pick);
    void rememberMaterial(uint8_t m);
    void fieldRectBegin(const char* name);
    void fieldRectEnd(const char* name);
    std::vector<std::string> cheatLines() const;

    // ---------------------------------------------------------------- render (render.cpp)
    uint32_t cellColor(const Cell& c, uint32_t bg) const;
    void renderParticles();
    SDL_FPoint sp(Vec2 p) const;
    void fillPoly(const std::vector<Vec2>& pts, uint32_t color);
    void fillPolyC(const std::vector<Vec2>& pts, SDL_Color c);
    void lineWorld(Vec2 a, Vec2 b, SDL_Color c, int thick = 1);
    void outlinePoly(const std::vector<Vec2>& pts, SDL_Color c);
    std::vector<Vec2> circlePts(Vec2 c, float r, int n = 28);
    void renderFan(const Body& b, uint32_t fill);
    void renderBodies();
    std::vector<Vec2> bodyOutline(const Body& b, float grow = 0.f);
    void renderFocusMark();
    void renderSelection();
    void renderHandles();
    std::vector<int> bondsDrawn;
    void renderJoints();
    void renderArcs();
    static int niceStep(float cells);
    void renderGrid();
    void renderRulers();
    void renderScrollStrip();
    void renderLabels();
    void ghostLabel(Vec2 at, const std::string& t);
    void renderGhost();
    void renderGuide();
    void renderWorld();        // everything inside the canvas
    std::string shapeText(const Body& b) const;
    std::string selectionText() const;
    std::string hoverText() const;
    std::string bodyName(const Body& b) const;   // "Box 60x30 STEEL", for lists
    std::string jointName(const Joint& j) const;
};
