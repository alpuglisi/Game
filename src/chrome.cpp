// The interface: the five zones (top bar, tool strip, context bar, dock, status bar) and the popups (context menu, palette,
// select other, dialogs, cheat sheet), all described to the toolkit every frame from a layout computed from the window size.
#include "app.hpp"

namespace {
bool inRect(const SDL_Rect& r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
const char* TAB_NAMES[4] = {"Inspector", "Materials", "Scene", "History"};
const icons::Id TAB_ICONS[4] = {icons::Settings, icons::Layers, icons::Scenes, icons::Undo};
std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
bool matches(const std::string& query, const std::string& text) { return query.empty() || ui::fuzzyScore(query, text) > 0; }
int fontH() { return font::height(ui::theme().face, ui::theme().fontScale); }   // the interface text height, for centring
}  // namespace

// ---------------------------------------------------------------- layout
Layout computeLayout(int winW, int winH, bool stripCollapsed) {
    Layout L;
    L.winW = std::max(1024, winW); L.winH = std::max(640, winH);
    const int topH = 40, ctxH = 32, statusH = 28, tabsH = 28;
    // the strip is 96 px: two 44 px buttons; when the window is too short for every tool it scrolls, and the scrollbar lane is added
    const int stripNeeds = 5 * ui::theme().sectionH + 14 * (40 + ui::theme().gap) + 5 * ui::theme().gap + 8;
    const bool stripScrolls = L.winH - topH - statusH < stripNeeds;
    const int stripW = stripCollapsed ? 44 : (stripScrolls ? 96 + ui::theme().scrollbarW : 96);
    L.dockCollapsed = L.winW < 1200;
    const int dockW = L.dockCollapsed ? 36 : 320;
    L.top = SDL_Rect{0, 0, L.winW, topH};
    L.strip = SDL_Rect{0, topH, stripW, L.winH - topH - statusH};
    L.dock = SDL_Rect{L.winW - dockW, topH, dockW, L.winH - topH - statusH};
    L.ctx = SDL_Rect{stripW, topH, L.winW - stripW - dockW, ctxH};
    L.canvas = SDL_Rect{stripW, topH + ctxH, L.winW - stripW - dockW, L.winH - topH - ctxH - statusH};
    L.status = SDL_Rect{0, L.winH - statusH, L.winW, statusH};
    if (!L.dockCollapsed) {
        L.dockTabs = SDL_Rect{L.dock.x, L.dock.y, dockW, tabsH};
        L.dockBody = SDL_Rect{L.dock.x, L.dock.y + tabsH, dockW, L.dock.h - tabsH};
        L.flyout = L.dockBody;
    } else {
        L.dockTabs = L.dock;
        L.flyout = SDL_Rect{L.dock.x - 320, L.dock.y, 320, L.dock.h};
        L.dockBody = L.flyout;
    }
    return L;
}

// ---------------------------------------------------------------- events and the frame
void Game::handleEvents() { pollEvents(); frame(); }

void Game::pollEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT: running = false; break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED) { winW = std::max(1024, e.window.data1); winH = std::max(640, e.window.data2); }
                break;
            case SDL_MOUSEMOTION: in.mx = e.motion.x; in.my = e.motion.y; break;
            case SDL_MOUSEBUTTONDOWN:
                in.mx = e.button.x; in.my = e.button.y;
                if (e.button.button == SDL_BUTTON_LEFT) { in.lDown = true; in.lPressed = true; in.lDouble = e.button.clicks >= 2; }
                else if (e.button.button == SDL_BUTTON_RIGHT) { in.rDown = true; in.rPressed = true; }
                else if (e.button.button == SDL_BUTTON_MIDDLE) { in.mDown = true; in.mPressed = true; }
                break;
            case SDL_MOUSEBUTTONUP:
                in.mx = e.button.x; in.my = e.button.y;
                if (e.button.button == SDL_BUTTON_LEFT) { in.lDown = false; in.lReleased = true; }
                else if (e.button.button == SDL_BUTTON_RIGHT) { in.rDown = false; in.rReleased = true; }
                else if (e.button.button == SDL_BUTTON_MIDDLE) { in.mDown = false; in.mReleased = true; }
                break;
            case SDL_MOUSEWHEEL: in.wheel += e.wheel.y; break;
            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_SPACE) { if (!e.key.repeat) { spaceHeld = true; spaceUsed = false; } }
                in.keys.push_back(e.key.keysym.sym);
                break;
            case SDL_KEYUP:
                if (e.key.keysym.sym == SDLK_SPACE) {   // Space plays / pauses on release, unless it was held to pan; Shift+Space stops
                    spaceHeld = false;
                    if (!spaceUsed && !ui.wantsKeyboard() && !paletteOpen && !fileOpen) runCommand((SDL_GetModState() & KMOD_SHIFT) ? "sim.stop" : "sim.play");
                }
                break;
            case SDL_TEXTINPUT: in.typed += e.text.text; break;
            default: break;
        }
    }
    const Uint16 mod = SDL_GetModState();
    in.ctrl = (mod & KMOD_CTRL) != 0; in.shift = (mod & KMOD_SHIFT) != 0; in.alt = (mod & KMOD_ALT) != 0;
}

// One immediate-mode pass: the canvas takes its input, the world is drawn, then the chrome and the popups are described
// to the toolkit (which handles their input as it draws them).
void Game::frame() {
    int w = 0, h = 0;
    SDL_GetWindowSize(win, &w, &h);
    if (w > 0 && h > 0) { winW = std::max(1024, w); winH = std::max(640, h); }
    L = computeLayout(winW, winH, stripCollapsed);
    in.winW = L.winW; in.winH = L.winH; in.ticks = SDL_GetTicks();
    fieldRects.clear();
    ui.begin(ren, in);
    SDL_SetRenderDrawColor(ren, (uint8_t)(ui::theme().bg >> 16), (uint8_t)(ui::theme().bg >> 8), (uint8_t)ui::theme().bg, 255);
    SDL_RenderClear(ren);
    canvasInput();
    renderWorld();
    drawCanvasOverlays();
    drawChrome();
    drawPopups();
    ui.end();
    in.lPressed = in.lReleased = in.rPressed = in.rReleased = in.mPressed = in.mReleased = in.lDouble = false;
    in.wheel = 0; in.typed.clear(); in.keys.clear();
}

// Pointer and keys on the canvas: the tool contract, panning, zooming, click-hold for "select other", the context menu.
void Game::canvasInput() {
    const bool popups = selOtherOpen || ctxOpen || matMenuOpen || paletteOpen || cheatOpen || scenesOpen || fileOpen || newConfirm || scaleOpen;
    const bool overFlyout = L.dockCollapsed && dockFlyout && inRect(L.flyout, in.mx, in.my);
    const bool overCanvas = inCanvasPx(in.mx, in.my) && !overFlyout;
    mouse = toWorld(in.mx, in.my);
    inSim = overCanvas && !popups;
    // keys go to the dimension field while a shape or handle is dragged, else to the keyboard map
    if (!ui.wantsKeyboard()) {
        for (SDL_Keycode k : in.keys) {
            if (dim.active && lmb) {
                if (dimKey(k)) continue;
                const bool digit = (k >= SDLK_0 && k <= SDLK_9) || (k >= SDLK_KP_1 && k <= SDLK_KP_9) || k == SDLK_KP_0 || k == SDLK_PERIOD || k == SDLK_KP_PERIOD ||
                                   k == SDLK_MINUS || k == SDLK_KP_MINUS || k == SDLK_PLUS || k == SDLK_KP_PLUS || k == SDLK_EQUALS || k == SDLK_SLASH ||
                                   k == SDLK_KP_DIVIDE || k == SDLK_KP_MULTIPLY || k == SDLK_ASTERISK;
                if (digit) continue;   // these arrive as typed text below
            }
            handleKey(k, in.ctrl, in.shift);
        }
        if (dim.active && lmb) dimType(in.typed);
    }
    if (popups) { if (lmb && !in.lDown) { lmb = false; dimEnd(); } return; }
    // the wheel zooms about the pointer (Ctrl+wheel too); Shift+wheel sizes the brush or the pipe
    if (overCanvas && in.wheel) {
        if (in.shift) {
            if (tool == T_PIPE || tool == T_HOSE) { pipeD = std::clamp(pipeD + (in.wheel > 0 ? 1.f : -1.f), 3.f, 60.f); pipeWall = std::min(pipeWall, pipeD * 0.5f); }
            else brush = std::clamp(brush + (in.wheel > 0 ? 1 : -1), 1, 24);
        } else zoomStep(in.wheel > 0 ? 1 : -1, in.mx, in.my);
    }
    // middle drag, or Space + left drag, pans
    if (in.mPressed && overCanvas) {
        panning = true; panStartPx = in.mx; panStartPy = in.my; panStartCam = camXf; panStartCamY = camYf;
        if (focusBody >= 0) { focusBody = -1; notify("Focus off (you panned the camera)"); }
    }
    if (panning) {
        setCam(panStartCam - (float)(in.mx - panStartPx) / sc(), panStartCamY - (float)(in.my - panStartPy) / sc());
        if ((in.mReleased || !in.mDown) && !(spaceUsed && in.lDown)) panning = false;
    }
    const int lx = in.mx - L.canvas.x, ly = in.my - L.canvas.y;
    if (in.lPressed && overCanvas && !lmb) {
        if (spaceHeld) {
            panning = true; spaceUsed = true; panStartPx = in.mx; panStartPy = in.my; panStartCam = camXf; panStartCamY = camYf;
            if (focusBody >= 0) focusBody = -1;
        } else if (inVStrip(lx)) {
            scrubbingV = true; scrubToY(ly);
            if (focusBody >= 0) { focusBody = -1; notify("Focus off (you moved the camera)"); }
        } else if (inScrollStrip(ly)) {
            scrubbing = true; scrubTo(lx);
            if (focusBody >= 0) { focusBody = -1; notify("Focus off (you moved the camera)"); }
        } else {
            lastMouse = mouse;
            pressTick = in.ticks; pressPx = in.mx; pressPy = in.my; pressHeldOpen = false;
            handleSimDown();
        }
    }
    if (scrubbing) scrubTo(lx);
    if (scrubbingV) scrubToY(ly);
    // click and hold on a body without moving opens the list of everything under the pointer
    if (lmb && in.lDown && tool == T_SELECT && moveArmed && !moving && handle < 0 && !pressHeldOpen && in.ticks - pressTick >= 400 &&
        std::abs(in.mx - pressPx) < 3 && std::abs(in.my - pressPy) < 3) {
        pressHeldOpen = true;
        lmb = false; moveArmed = false; dimEnd();
        openSelectOther(pressPx, pressPy);
    }
    if (in.lReleased) {
        if (scrubbing || scrubbingV) { scrubbing = false; scrubbingV = false; }
        else if (lmb) handleSimUp();
        if (spaceUsed) panning = false;
    }
    // right click: cancel what is being dragged, otherwise the context menu
    if (in.rPressed && overCanvas) {
        if (!cancelDrag()) openContextMenu(in.mx, in.my);
    }
}

// ---------------------------------------------------------------- the chrome
void Game::drawChrome() {
    drawTopBar();
    drawToolStrip();
    drawContextBar();
    drawDock();
    drawStatusBar();
}

// a button bound to a command: its tooltip and shortcut come from the table
#define CMD_BUTTON(id, icon) \
    do { const ui::Command* c_ = cmd(id); if (c_ && ui.iconButton(id, icon, c_->description.c_str(), !c_->enabled || c_->enabled(), false, c_->shortcut.empty() ? nullptr : c_->shortcut.c_str())) runCommand(id); } while (0)
#define CMD_TOGGLE(id, icon, on) \
    do { const ui::Command* c_ = cmd(id); if (c_ && ui.iconButton(id, icon, c_->description.c_str(), true, on, c_->shortcut.empty() ? nullptr : c_->shortcut.c_str())) runCommand(id); } while (0)

// A bar laid out as fixed-width slots from the left (and a group aligned to the right end): each slot is a small panel
// holding one widget, so a bar may hold any number of controls and a slot's width is exactly what it asks for.
struct Slots {
    ui::Context& ui;
    SDL_Rect r;
    int x, h, n = 0;
    Slots(ui::Context& u, SDL_Rect rect, int height) : ui(u), r(rect), x(rect.x), h(height) {}
    void gap(int w) { x += w; }
    template <class F> void slot(int w, F f) {
        SDL_Rect s{x - 1, r.y + (r.h - h) / 2 - 1, w + 2, h + 2};
        ui.pushId(n++);
        ui.beginPanel("slot", s, false, false, 1);
        ui.row(1, h);
        f();
        ui.endPanel();
        ui.popId();
        x += w + ui::theme().gap;
    }
    void from(int startX) { x = startX; }
};

void Game::drawTopBar() {
    const ui::Theme& th = ui::theme();
    const int b = th.iconButton, g = th.gap, sep = 8;
    ui.beginPanel("top", L.top, false, true, 6);
    SDL_Rect inner = ui.panelRect();
    Slots s(ui, inner, b);
    // files and undo, from the left
    s.slot(b, [&] { CMD_BUTTON("file.new", icons::New); });
    s.slot(b, [&] { CMD_BUTTON("file.open", icons::Open); });
    s.slot(b, [&] { CMD_BUTTON("file.save", icons::Save); });
    s.slot(b, [&] { CMD_BUTTON("file.saveas", icons::SaveAs); });
    s.gap(sep);
    s.slot(b, [&] { CMD_BUTTON("edit.undo", icons::Undo); });
    s.slot(b, [&] { CMD_BUTTON("edit.redo", icons::Redo); });
    s.gap(sep);
    s.slot(b, [&] {
        if (ui.iconButton("view.strip", stripCollapsed ? icons::ArrowRight : icons::ArrowLeft, stripCollapsed ? "Show the tool labels" : "Collapse the tool strip to icons", true, false))
            stripCollapsed = !stripCollapsed;
    });
    const int leftEnd = s.x;
    // views, zoom, focus, scenes, search and help, from the right
    const int zoomW = 52, rightW = 10 * b + 9 * g + zoomW + 3 * sep;
    const int rightStart = inner.x + inner.w - rightW;
    // the transport dead centre, with the speed field and the mode badge; shifted only when the window is too narrow
    const int speedW = 80, badgeW = 104, centreW = 4 * b + 3 * g + 2 * sep + speedW + badgeW + 2 * g;
    int cx = L.winW / 2 - centreW / 2;
    cx = std::min(cx, rightStart - centreW - sep);
    cx = std::max(cx, leftEnd + sep);
    s.from(cx);
    s.slot(b, [&] { if (ui.iconButton("sim.play", icons::Play, "Run the simulation (the drawing is snapshot first)", true, playing && !paused, "Space")) play(); });
    s.slot(b, [&] { if (ui.iconButton("sim.pause", icons::Pause, "Freeze / resume while playing", playing, playing && paused, "Space")) togglePause(); });
    s.slot(b, [&] { CMD_BUTTON("sim.step", icons::Step); });
    s.slot(b, [&] { CMD_BUTTON("sim.stop", icons::Stop); });
    s.gap(sep);
    s.slot(speedW, [&] { if (ui.dragFloat("speed", "", speed, 0.1f, 0.1f, 2.f, "x", 1, "Simulation speed, 0.1x to 2x")) speed = std::clamp(speed, 0.1f, 2.f); });
    s.gap(sep);
    s.slot(badgeW, [&] {
        SDL_Rect r = ui.next(b);
        uint32_t col = playing ? (paused ? th.warning : th.success) : th.border;
        const char* txt = playing ? (paused ? "PAUSED" : "RUNNING") : "EDITING";
        ui.fillRect(r, th.surface2, 255, th.radius);
        ui.strokeRect(r, col, 255, th.radius);
        ui.fillRect(SDL_Rect{r.x + 8, r.y + r.h / 2 - 4, 8, 8}, col, 255, 4);
        ui.text(txt, r.x + 22, r.y + (r.h - fontH()) / 2, playing ? (paused ? ui::TextStyle::Warning : ui::TextStyle::Accent) : ui::TextStyle::Dim);
    });
    s.from(rightStart);
    s.slot(b, [&] { CMD_TOGGLE("view.heat", icons::Heat, heatView); });
    s.slot(b, [&] { CMD_TOGGLE("view.pressure", icons::Pressure, pressureView); });
    s.slot(b, [&] { CMD_TOGGLE("view.electric", icons::Electric, elecView); });
    s.slot(b, [&] { CMD_TOGGLE("view.grid", icons::Grid, gridOn); });
    s.gap(sep);
    s.slot(b, [&] { CMD_BUTTON("view.zoomout", icons::ZoomOut); });
    s.slot(zoomW, [&] { if (ui.button("zoomlabel", fmt(zoom) + "x", icons::None, "The zoom. Click to reset to 1x", true, zoom > 1.01f, "Ctrl+0")) zoomReset(); });
    s.slot(b, [&] { CMD_BUTTON("view.zoomin", icons::ZoomIn); });
    s.gap(sep);
    s.slot(b, [&] { CMD_TOGGLE("view.focus", icons::Focus, focusBody >= 0); });
    s.gap(sep);
    s.slot(b, [&] { CMD_TOGGLE("file.scenes", icons::Scenes, scenesOpen); });
    s.slot(b, [&] { CMD_BUTTON("view.palette", icons::Search); });
    s.slot(b, [&] { CMD_TOGGLE("view.help", icons::Help, cheatOpen); });
    ui.endPanel();
}

// The tool strip: two columns of icon + label buttons under small group headers; collapsed, one column of icons.
void Game::drawToolStrip() {
    struct Item { const char* id; Tool tool; const char* label; bool erase; };   // a tool (tool < T_COUNT) or an action command
    struct Group { const char* name; std::vector<Item> items; };
    static const std::vector<Group> groups = {
        {"TOOLS", {{"tool.Select", T_SELECT, "Select", false}, {"tool.Grab", T_GRAB, "Grab", false}, {"tool.Measure", T_MEASURE, "Measure", false}}},
        {"CELLS", {{"tool.Paint", T_MAT, "Paint", false}, {"tool.Erase", T_MAT, "Erase", true}}},
        {"PARTS", {{"tool.Box", T_BOX, "Box", false}, {"tool.Circle", T_CIRCLE, "Circle", false}, {"tool.Wheel", T_WHEEL, "Wheel", false}, {"tool.Rocket", T_ROCKET, "Rocket", false},
                   {"tool.Pipe", T_PIPE, "Pipe", false}, {"tool.Hose", T_HOSE, "Hose", false}, {"tool.Fan", T_FAN, "Fan", false}, {"tool.Emitter", T_EMITTER, "Emitter", false}}},
        {"JOINTS", {{"tool.Pin", T_PIN, "Pin", false}, {"tool.Motor", T_MOTOR, "Motor", false}, {"tool.Spinner", T_AUTOMOTOR, "Spinner", false}, {"tool.Rod", T_ROD, "Rod", false},
                    {"tool.Spring", T_SPRING, "Spring", false}, {"tool.Slider", T_SLIDER, "Slider", false}, {"tool.Bond", T_BOND, "Bond", false}}},
        {"MODIFY", {{"tool.Cut", T_CUT, "Cut", false}, {"edit.scale", T_COUNT, "Scale", false}, {"edit.group", T_COUNT, "Group", false}, {"edit.ungroup", T_COUNT, "Ungroup", false},
                    {"edit.fliph", T_COUNT, "Flip H", false}, {"edit.flipv", T_COUNT, "Flip V", false}, {"edit.duplicate", T_COUNT, "Dupl.", false}, {"edit.delete", T_COUNT, "Delete", false}}},
    };
    const ui::Theme& th = ui::theme();
    ui.beginPanel("strip", L.strip, true, true, 2);
    for (auto& grp : groups) {
        if (!stripCollapsed) {
            SDL_Rect r = ui.next(th.sectionH);
            ui.text(grp.name, r.x + 4, r.y + (r.h - fontH()) / 2, ui::TextStyle::Section);
        } else ui.separator();
        const int per = stripCollapsed ? 1 : 2;
        for (size_t i = 0; i < grp.items.size(); i += per) {
            ui.row(per, stripCollapsed ? th.iconButton : 40);
            for (int k = 0; k < per; ++k) {
                if (i + k >= grp.items.size()) { ui.label(""); continue; }
                const Item& it = grp.items[i + k];
                const ui::Command* c = cmd(it.id);
                if (!c) { ui.label(""); continue; }
                const bool active = it.tool < T_COUNT && tool == it.tool && (it.tool != T_MAT || (it.erase == (mat == M_EMPTY)));
                const bool enabled = !c->enabled || c->enabled();
                const char* sc = c->shortcut.empty() ? nullptr : c->shortcut.c_str();
                bool hit = stripCollapsed ? ui.iconButton(it.id, c->icon, c->description.c_str(), enabled, active, sc)
                                          : ui.toolButton(it.id, c->icon, it.label, active, c->description.c_str(), sc);
                if (hit) runCommand(it.id);
            }
        }
    }
    ui.endPanel();
}

// a material chip: the swatch; clicking it opens the Materials tab
void Game::materialChip(const char* id, uint8_t m, bool paint, const char* tip) {
    std::vector<ui::SwatchItem> one = {{m == M_EMPTY ? 0xff5050u : MATS[m].color, m == M_EMPTY ? "Eraser" : MATS[m].name, tip ? tip : matTip(m)}};
    ui.pushId(id);
    if (ui.swatchGrid("chip", one, 0, 24) >= 0) { dockTab = 1; if (L.dockCollapsed) dockFlyout = true; matSearch.clear(); }
    ui.popId();
    (void)paint;
}
// a body-material picker: a search field over the body materials and the matching swatches
void Game::bodyMaterialPicker(const char* id, std::string& search, uint8_t current, const std::function<void(uint8_t)>& pick) {
    ui.pushId(id);
    ui.textField("search", search, "Search body materials");
    std::vector<ui::SwatchItem> items;
    std::vector<uint8_t> ids;
    int cur = -1;
    for (uint8_t m : BODY_MATS) {
        if (!matches(search, MATS[m].name)) continue;
        if (m == current) cur = (int)items.size();
        items.push_back({MATS[m].color, MATS[m].name, matTip(m)});
        ids.push_back(m);
    }
    int hit = ui.swatchGrid("grid", items, cur, 26, false);
    if (hit >= 0) pick(ids[hit]);
    ui.keyValue("Material", MATS[current].name);
    ui.popId();
}
void Game::rememberMaterial(uint8_t m) {
    recentMats.erase(std::remove(recentMats.begin(), recentMats.end(), m), recentMats.end());
    recentMats.insert(recentMats.begin(), m);
    if (recentMats.size() > 8) recentMats.resize(8);
}

// The context bar: the options of the active tool from the left, the snap controls at the right end.
void Game::drawContextBar() {
    const ui::Theme& th = ui::theme();
    const int B = th.iconButton, h = th.rowH;
    ui.beginPanel("ctx", L.ctx, false, true, 2);
    SDL_Rect inner = ui.panelRect();
    Slots s(ui, inner, h);
    auto chip = [&](const char* tip) { s.slot(24, [&] { materialChip("chip", bodyMat, false, tip); }); s.slot(std::max(60, ui.textWidth(MATS[bodyMat].name) + 4), [&] { ui.label(MATS[bodyMat].name); }); };
    auto fixedToggle = [&] { s.slot(108, [&] { ui.toggle("fixed", "Fixed", anchored, "New bodies stay where they are (T)"); }); };
    switch (tool) {
        case T_MAT:
            if (mat == M_EMPTY) {
                s.slot(150, [&] { ui.dragInt("brush", "Brush", brush, 1, 1, 24, "", "Brush radius in cells ([ and ])"); });
            } else {
                s.slot(24, [&] { materialChip("paintchip", mat, true, "The paint material: click for the Materials tab"); });
                s.slot(std::max(60, ui.textWidth(MATS[mat].name) + 4), [&] { ui.label(MATS[mat].name); });
                s.slot(150, [&] { ui.dragInt("brush", "Brush", brush, 1, 1, 24, "", "Brush radius in cells ([ and ])"); });
                s.slot(136, [&] { ui.toggle("replace", "Replace", paintReplace, "Paint over cells that are already there"); });
                if (mat == M_BATT_POS || mat == M_BATT_NEG)
                    s.slot(150, [&] { if (ui.button("batt", fmt(world.battV) + "V " + fmt(world.battA) + "A", icons::Electric, "The volts and amps stamped into battery cells you paint: set them in the Inspector (World)")) { dockTab = 0; clearSelection(); } });
                else if (mat == M_SOURCE)
                    s.slot(190, [&] { if (ui.button("supply", std::string("Emits ") + MATS[payload].name, icons::Emitter, "What fuel supply cells emit, and how dense: set them in the Inspector (World)")) { dockTab = 0; clearSelection(); } });
            }
            break;
        case T_BOX: case T_CIRCLE: case T_WHEEL: case T_ROCKET:
            chip("The material of new bodies");
            fixedToggle();
            s.slot(360, [&] { ui.label(tool == T_CIRCLE || tool == T_WHEEL ? "Drag, or type the radius and press Enter" : tool == T_ROCKET ? "Drag to point it" : "Drag, or type W Tab H Tab angle, then Enter", ui::TextStyle::Dim); });
            break;
        case T_PIPE: case T_HOSE:
            chip("The material of new pipes");
            fixedToggle();
            s.slot(200, [&] { if (ui.dragFloat("pipeD", "Diameter", pipeD, 1.f, 3.f, 60.f, "", 1, "Outer diameter in cells (Shift+wheel too)")) pipeWall = std::min(pipeWall, pipeD * 0.5f); });
            s.slot(130, [&] { if (ui.dragFloat("pipeWall", "Wall", pipeWall, 0.5f, 0.5f, 30.f, "", 1, "Wall thickness in cells")) pipeWall = std::min(pipeWall, pipeD * 0.5f); });
            if (tool == T_HOSE) s.slot(170, [&] { ui.dragInt("segs", "Segments", hoseSegs, 1, 0, 60, "", "Hinged segments (0 = automatic)"); });
            break;
        case T_FAN:
            chip("The material of new fans");
            fixedToggle();
            s.slot(180, [&] { ui.dragFloat("fanS", "Strength", lastFan, 5.f, 5.f, 300.f, "/s", 0, "Airflow of new fans in cells per second"); });
            s.slot(150, [&] { int m = ui.segmented("fanmode", {"Blow", "Vacuum"}, fanVacuumDefault ? 1 : 0, "Blow draws ambient air in; vacuum only pulls the gas that is there"); fanVacuumDefault = m == 1; });
            break;
        case T_EMITTER: {
            chip("The material of the emitter's block");
            fixedToggle();
            s.slot(190, [&] {
                std::vector<std::string> names; int cur = 0;
                for (size_t i = 0; i < EMIT_MATS.size(); ++i) { names.push_back(MATS[EMIT_MATS[i]].name); if (EMIT_MATS[i] == payload) cur = (int)i; }
                if (ui.dropdown("emits", "Emits", names, cur, "What new emitters produce")) payload = EMIT_MATS[cur];
            });
            s.slot(130, [&] { ui.dragFloat("rate", "Rate", lastRate, 5.f, 5.f, 1000.f, "/s", 0, "Cells per second"); });
            s.slot(190, [&] { emitFace = (uint8_t)ui.segmented("face", {"All", "+X", "-X", "+Y", "-Y"}, emitFace, "The outlet side, in the emitter's own frame"); });
            break;
        }
        case T_SPRING:
            s.slot(190, [&] { ui.dragFloat("springF", "Stiffness", springFreq, 0.5f, 0.2f, 60.f, "/s", 1, "Bounces per second of new springs: higher is stiffer"); });
            s.slot(170, [&] { ui.dragFloat("springD", "Damping", springDamp, 0.05f, 0.f, 2.f, "", 2, "0 bouncy, 1 dead"); });
            break;
        case T_BOND:
            s.slot(190, [&] {
                std::vector<std::string> names(BOND_NAMES, BOND_NAMES + 4);
                int bt = bondType;
                if (ui.dropdown("bondpreset", "Preset", names, bt, "Paraffin melts at 55 C, solder 190, epoxy 260; a shear pin never melts but snaps")) { bondType = bt; bondT = BOND_TEMP[bt]; bondG = BOND_G[bt]; }
            });
            s.slot(180, [&] { ui.dragFloat("bondT", "Melts at", bondT, 5.f, -50.f, 5000.f, "C", 0, "Temperature at which the bond gives way"); });
            s.slot(150, [&] { ui.dragFloat("bondG", "Holds", bondG, 1.f, 1.f, 1000.f, "x", 0, "How many times the weight it carries the bond can hold"); });
            break;
        case T_CUT:
            s.slot(150, [&] { cutCircle = ui.segmented("cutshape", {"Box", "Circle"}, cutCircle ? 1 : 0, "The shape cut out of the bodies it covers") == 1; });
            s.slot(180, [&] { ui.toggle("keep", "Keep cutter", keepCutter, "Subtract keeps the red body, for example to scale it into a plug"); });
            s.slot(140, [&] { if (ui.button("subtract", "Subtract", icons::Subtract, tipOf("edit.subtract").c_str(), sel.size() >= 2)) cutSelection(); });
            break;
        case T_SELECT: {
            struct Al { const char* id; icons::Id icon; const char* tip; int how; };
            static const Al als[6] = {{"al0", icons::ArrowLeft, "Align left edges", 0}, {"al1", icons::Dot, "Align centres", 1}, {"al2", icons::ArrowRight, "Align right edges", 2},
                                      {"al3", icons::ArrowUp, "Align top edges", 3}, {"al4", icons::Minus, "Align middles", 4}, {"al5", icons::ArrowDown, "Align bottom edges", 5}};
            for (auto& a : als) s.slot(B, [&] { if (ui.iconButton(a.id, a.icon, a.tip, sel.size() >= 2)) alignSelection(a.how); });
            s.gap(8);
            s.slot(B, [&] { if (ui.iconButton("dh", icons::FlipH, "Distribute evenly left to right", sel.size() >= 3)) distributeSelection(true); });
            s.slot(B, [&] { if (ui.iconButton("dv", icons::FlipV, "Distribute evenly top to bottom", sel.size() >= 3)) distributeSelection(false); });
            s.gap(8);
            s.slot(270, [&] { selFilter = ui.segmented("filter", {"All", "Bodies", "Joints"}, selFilter, "What clicks and box-selects pick"); });
            break;
        }
        case T_MEASURE: {
            Vec2 d = measB - measA;
            float deg = length(d) > 1e-4f ? std::atan2(d.y, d.x) * 180.f / PI : 0.f;
            s.slot(460, [&] { ui.label(measOn ? "Length " + fmt(length(d)) + "   dx " + fmt(d.x) + "   dy " + fmt(d.y) + "   angle " + fmt(deg) + " deg" : "Drag between two points to measure", measOn ? ui::TextStyle::Normal : ui::TextStyle::Dim); });
            break;
        }
        default:
            s.slot(std::max(200, inner.w - 420), [&] { ui.label(toolInfo(tool).what, ui::TextStyle::Dim); });
            break;
    }
    // the snap controls at the right end
    const int snapW = 92, stepW = 160, hintW = ui.textWidth("Ctrl inverts") + 4, right = inner.x + inner.w;
    const bool hint = s.x + snapW + stepW + hintW + 2 * th.gap <= right, step = s.x + snapW + stepW + th.gap <= right;
    if (s.x + snapW <= right) {
        s.from(right - snapW - (step ? stepW + th.gap : 0) - (hint ? hintW + th.gap : 0));
        s.slot(snapW, [&] { ui.toggle("snap", "Snap", snapOn, "Grid snap: drawn shapes, dropped bodies and dragged edges land on the grid (Ctrl inverts it while dragging)"); });
        if (step) s.slot(stepW, [&] {
            int stepIdx = 0; for (int i = 0; i < 4; ++i) if (SNAP_STEPS[i] == snapStep) stepIdx = i;
            int ns = ui.segmented("snapstep", {"1", "2", "5", "10"}, stepIdx, "The grid step in cells");
            if (ns != stepIdx) { snapStep = SNAP_STEPS[ns]; snapOn = true; }
        });
        if (hint) s.slot(hintW, [&] { ui.label("Ctrl inverts", ui::TextStyle::Dim); });
    }
    ui.endPanel();
}


// ---------------------------------------------------------------- the dock
void Game::drawDock() {
    if (!L.dockCollapsed) {
        ui.beginPanel("docktabs", L.dockTabs, false, true, 0);
        dockTab = ui.tabs("tabs", {TAB_NAMES[0], TAB_NAMES[1], TAB_NAMES[2], TAB_NAMES[3]}, dockTab);
        ui.endPanel();
        ui.beginPanel("dock", L.dockBody, true, true);
    } else {   // narrow window: a column of tab icons; the body is a flyout over the canvas
        ui.beginPanel("docktabs", L.dockTabs, false, true, 4);
        for (int i = 0; i < 4; ++i) {
            ui.row(1, ui::theme().iconButton);
            if (ui.iconButton(TAB_NAMES[i], TAB_ICONS[i], TAB_NAMES[i], true, dockFlyout && dockTab == i)) {
                if (dockFlyout && dockTab == i) dockFlyout = false; else { dockTab = i; dockFlyout = true; }
            }
        }
        ui.endPanel();
        if (!dockFlyout) return;
        ui.beginPanel("dock", L.flyout, true, true);
        ui.row({9.f, 1.f}, ui::theme().rowH);
        ui.label(TAB_NAMES[dockTab], ui::TextStyle::Heading);
        if (ui.iconButton("closefly", icons::Close, "Close")) dockFlyout = false;
    }
    switch (dockTab) {
        case 0: drawInspector(); break;
        case 1: drawMaterials(); break;
        case 2: drawSceneTab(); break;
        default: drawHistory(); break;
    }
    ui.endPanel();
}

void Game::fieldRectBegin(const char* name) { fieldRects[name] = SDL_Rect{0, ui.contentHeight(), 0, 0}; }
void Game::fieldRectEnd(const char* name) {
    SDL_Rect p = ui.panelRect();
    int y0 = fieldRects[name].y, y1 = ui.contentHeight();
    const int gap = y0 > 0 ? ui::theme().gap : 0;   // the layout gap between the previous widget and this one
    fieldRects[name] = SDL_Rect{p.x, p.y + y0 + gap, p.w, std::max(1, y1 - y0 - gap)};
}

// The Inspector: the selection's properties as fields that scrub, type and take expressions; one undo entry per edit.
void Game::drawInspector() {
    pruneSelection();
    const ui::Theme& th = ui::theme();
    // a numeric field bound to a value: the change is applied at once (so scrubbing is live), one undo entry per edit
    auto fieldF = [&](const char* id, const char* label, float v, float step, float lo, float hi, const char* unit, int dec, const char* tip,
                      const std::function<void(float)>& apply, const char* undoKey, const char* undoLabel) {
        fieldRectBegin(id);
        float nv = v;
        if (ui.dragFloat(id, label, nv, step, lo, hi, unit, dec, tip)) { pushUndo(undoKey, undoLabel); apply(nv); }
        if (ui.edited()) lastUndoKey.clear();
        fieldRectEnd(id);
    };
    auto deleteButton = [&] {
        ui.space(th.gap);
        if (ui.button("delete", "Delete", icons::Delete, tipOf("edit.delete").c_str(), true, false, "Del")) deleteSelection();
    };
    if (jointValid(selJoint)) {
        const Joint& J = phys.joints[selJoint];
        ui.label(jointName(J), ui::TextStyle::Heading);
        const bool spring = J.type == J_DISTANCE && J.freq > 0.f;
        if (J.type == J_DISTANCE) {
            if (ui.section("jdist", spring ? "Spring" : "Rod")) {
                float cur = length(phys.jointAnchorB(J) - phys.jointAnchorA(J));
                ui.keyValue("Length now", fmt(cur));
                if (spring) {
                    fieldF("jfreq", "Stiffness", J.freq, 0.5f, 0.2f, 60.f, "/s", 1, "Bounces per second: higher is stiffer", [&](float v) { editJoint([v](Joint& k) { k.freq = v; }, "Spring stiffness"); }, "jfreq", "Spring stiffness");
                    fieldF("jdamp", "Damping", J.damping, 0.05f, 0.f, 2.f, "", 2, "0 bouncy, 1 dead", [&](float v) { editJoint([v](Joint& k) { k.damping = v; }, "Spring damping"); }, "jdamp", "Spring damping");
                }
                fieldF("jlen", "Rest length", J.length, 1.f, 1.f, 400.f, "", 1, "The length at which it pulls neither way", [&](float v) { editJoint([v](Joint& k) { k.length = v; }, "Rest length"); }, "jlen", "Rest length");
                if (ui.button("restnow", "Set rest = now", icons::Check, "Make the current length the natural one")) editJoint([cur](Joint& k) { k.length = std::max(1.f, cur); }, "Rest length");
                int kind = ui.segmented("springrod", {"Spring", "Rod"}, spring ? 0 : 1, "A soft link that stretches, or a rigid one of fixed length");
                if (kind == 0 && !spring) editJoint([this](Joint& k) { k.freq = springFreq; }, "Rod to spring");
                if (kind == 1 && spring) editJoint([](Joint& k) { k.freq = 0.f; }, "Spring to rod");
            }
        } else if (J.type == J_MOTOR) {
            if (ui.section("jmotor", "Motor")) {
                fieldF("jspeed", "Speed", J.speed, 0.5f, -60.f, 60.f, "rad/s", 1, "Target turning speed; negative turns the other way", [&](float v) { editJoint([v](Joint& k) { k.speed = v; }, "Motor speed"); }, "jspeed", "Motor speed");
                fieldF("jpower", "Power", J.power, 10.f, 5.f, 5000.f, "", 0, "How hard the motor can turn", [&](float v) { editJoint([v](Joint& k) { k.power = v; }, "Motor power"); }, "jpower", "Motor power");
                int keyed = ui.segmented("keyed", {"Arrow keys", "Always on"}, J.keyed ? 0 : 1, "Driven by the arrow keys (A / D), or spinning all the time");
                if ((keyed == 0) != J.keyed) editJoint([keyed](Joint& k) { k.keyed = keyed == 0; }, "Motor drive");
            }
        } else if (J.bondId >= 0) {
            if (ui.section("jbond", "Bond")) {
                fieldF("jbreakT", "Melts at", J.breakT, 5.f, -50.f, 5000.f, "C", 0, "Temperature at which the bond gives way", [&](float v) { editJoint([v](Joint& k) { k.breakT = v; }, "Bond melts at"); }, "jbreakT", "Bond melts at");
                fieldF("jloadG", "Holds", J.loadG, 1.f, 1.f, 1000.f, "x", 0, "How many times the weight it carries it can hold", [&](float v) { editJoint([v](Joint& k) { k.loadG = v; }, "Bond holds"); }, "jloadG", "Bond holds");
            }
        } else if (J.type == J_SLIDER) {
            if (ui.section("jslider", "Slider")) ui.labelWrapped("Keeps one body on a line, rotation locked. The line is fixed in the world or in another body.", ui::TextStyle::Dim);
        } else {
            if (ui.section("jpin", "Pin")) ui.labelWrapped("A hinge between two bodies, or between a body and the world.", ui::TextStyle::Dim);
        }
        deleteButton();
        return;
    }
    if (sel.empty()) {
        ui.label("Nothing selected", ui::TextStyle::Heading);
        ui.label("Click a body or joint (Q)", ui::TextStyle::Dim);
        if (ui.section("defaults", "Tool defaults", false)) {
            if (ui.dragFloat("dSpringF", "Spring stiffness", springFreq, 0.5f, 0.2f, 60.f, "/s", 1, "Bounces per second of new springs")) {}
            if (ui.dragFloat("dSpringD", "Spring damping", springDamp, 0.05f, 0.f, 2.f, "", 2, "0 bouncy, 1 dead")) {}
            std::vector<std::string> names(BOND_NAMES, BOND_NAMES + 4);
            int bt = bondType;
            if (ui.dropdown("dBond", "Bond preset", names, bt)) { bondType = bt; bondT = BOND_TEMP[bt]; bondG = BOND_G[bt]; }
            if (ui.dragFloat("dBondT", "Bond melts at", bondT, 5.f, -50.f, 5000.f, "C", 0)) {}
            if (ui.dragFloat("dBondG", "Bond holds", bondG, 1.f, 1.f, 1000.f, "x", 0)) {}
            if (ui.dragFloat("dPipeD", "Pipe diameter", pipeD, 1.f, 3.f, 60.f, "", 1)) pipeWall = std::min(pipeWall, pipeD * 0.5f);
            if (ui.dragFloat("dPipeW", "Pipe wall", pipeWall, 0.5f, 0.5f, 30.f, "", 1)) pipeWall = std::min(pipeWall, pipeD * 0.5f);
            if (ui.dragFloat("dFan", "Fan strength", lastFan, 5.f, 5.f, 300.f, "/s", 0)) {}
            if (ui.dragFloat("dRate", "Emitter rate", lastRate, 5.f, 5.f, 1000.f, "/s", 0)) {}
        }
        if (ui.section("world", "World")) {
            int grav = ui.segmented("gravity", {"Gravity down", "Gravity up"}, phys.gravity.y > 0 ? 0 : 1, "Which way gravity pulls");
            if ((grav == 0) != (phys.gravity.y > 0)) runCommand("sim.gravity");
            std::vector<std::string> sparks = {"Off", "Every 120 frames", "Every 60", "Every 40", "Every 30", "Every 20", "Every 12"};
            int si = sparkIdx;
            if (ui.dropdown("spark", "Spark plugs", sparks, si, "How often spark plugs fire (hold Z to fire them now)")) { sparkIdx = si; world.sparkPeriod = SPARK_RATES[si]; }
            fieldF("battV", "Volts", world.battV, 1.f, 1.f, 70000.f, "V", 0, "Stamped into battery cells you paint next", [&](float v) { world.battV = v; }, "battV", "Battery volts");
            fieldF("battA", "Amps", world.battA, 1.f, 0.001f, 400.f, "A", 2, "The current limit stamped into battery cells you paint next", [&](float v) { world.battA = v; }, "battA", "Battery amps");
            std::vector<std::string> names; int cur = 0;
            for (size_t i = 0; i < EMIT_MATS.size(); ++i) { names.push_back(MATS[EMIT_MATS[i]].name); if (EMIT_MATS[i] == payload) cur = (int)i; }
            if (ui.dropdown("supplyMat", "Supply emits", names, cur, "What painted fuel supply cells produce")) payload = EMIT_MATS[cur];
            if (ui.dragFloat("supplyAmt", "Amount/cell", world.sourceAmt, 0.1f, 0.1f, 4.f, "", 1, "The gas amount a fuel supply cell emits, stamped as it is painted")) {}
            ui.keyValue("Bodies", std::to_string(phys.bodyCount()));
        }
        return;
    }
    if (sel.size() > 1 && !partMode) {
        const bool wholeGroup = primary >= 0 && phys.bodies[primary].group >= 0 && (int)phys.groupMembers(phys.bodies[primary].group).size() == (int)sel.size() &&
                                std::all_of(sel.begin(), sel.end(), [&](int id) { return phys.bodies[id].group == phys.bodies[primary].group; });
        if (!wholeGroup) {   // several bodies: what applies to all of them
            std::vector<int> groups;
            for (int id : sel) { int g = phys.bodies[id].group; if (g >= 0 && std::find(groups.begin(), groups.end(), g) == groups.end()) groups.push_back(g); }
            ui.label(std::to_string(sel.size()) + " bodies selected", ui::TextStyle::Heading);
            ui.keyValue("Groups", std::to_string(groups.size()));
            ui.keyValue("Cutter", primary >= 0 ? bodyName(phys.bodies[primary]) : "-");
            if (ui.section("multimat", "Material and fixed")) {
                bodyMaterialPicker("mmat", inspectorMatSearch, phys.bodies[primary].mat, [this](uint8_t m) { setBodyMaterial(m); });
                bool fixed = std::all_of(sel.begin(), sel.end(), [&](int id) { return phys.bodies[id].isStatic; });
                if (ui.toggle("mfixed", "Fixed", fixed, "Fixed bodies stay where they are (T)")) setFixed(fixed);
            }
            if (ui.section("arrange", "Arrange")) {
                ui.row(6, th.iconButton);
                if (ui.iconButton("mal0", icons::ArrowLeft, "Align left edges")) alignSelection(0);
                if (ui.iconButton("mal1", icons::Dot, "Align centres")) alignSelection(1);
                if (ui.iconButton("mal2", icons::ArrowRight, "Align right edges")) alignSelection(2);
                if (ui.iconButton("mal3", icons::ArrowUp, "Align top edges")) alignSelection(3);
                if (ui.iconButton("mal4", icons::Minus, "Align middles")) alignSelection(4);
                if (ui.iconButton("mal5", icons::ArrowDown, "Align bottom edges")) alignSelection(5);
                ui.row(2);
                if (ui.button("mdh", "Spread H", icons::FlipH, "Distribute evenly left to right: equal gaps", sel.size() >= 3)) distributeSelection(true);
                if (ui.button("mdv", "Spread V", icons::FlipV, "Distribute evenly top to bottom: equal gaps", sel.size() >= 3)) distributeSelection(false);
                ui.row(2);
                if (ui.button("mgroup", "Group", icons::Group, tipOf("edit.group").c_str(), true, false, "Ctrl+G")) groupSelection();
                if (ui.button("mungroup", "Ungroup", icons::Ungroup, tipOf("edit.ungroup").c_str(), !groups.empty(), false, "Ctrl+U")) ungroupSelection();
                ui.row({2.f, 1.f});
                if (ui.dragFloat("mscale", "Scale", lastScale, 1.f, 5.f, 1000.f, "%", 0, "Resize the selection about its centre")) {}
                if (ui.button("mscaleGo", "Apply", icons::Scale, "Scale the selection by this percentage")) scaleSelection(lastScale);
                if (ui.button("msub", "Subtract", icons::Subtract, tipOf("edit.subtract").c_str())) cutSelection();
            }
            deleteButton();
            return;
        }
    }
    // one body, or a part of a group, or a whole group (which moves and turns as one)
    const Body& b = phys.bodies[primary];
    const bool whole = sel.size() > 1 && !partMode;
    ui.label(whole ? "Group of " + std::to_string(sel.size()) : bodyName(b), ui::TextStyle::Heading);
    if (ui.section("transform", "Transform")) {
        float deg = std::fmod(b.angle * 180.f / PI, 360.f); if (deg < 0) deg += 360.f;
        Vec2 half = b.half; float radius = b.radius; Vec2 pos = b.pos;
        auto apply = [&](Vec2 p, Vec2 h, float r, float a) { setBodyTransform(primary, p, h, r, a); };
        fieldF("X", "X", pos.x, 1.f, -200.f, (float)World::W + 200.f, "", 2, "Centre, in cells", [&](float v) { apply(Vec2(v, pos.y), half, radius, deg); }, "tx", "Move");
        fieldF("Y", "Y", pos.y, 1.f, -200.f, (float)World::H + 200.f, "", 2, "Centre, in cells", [&](float v) { apply(Vec2(pos.x, v), half, radius, deg); }, "ty", "Move");
        if (!whole) {
            if (b.shape == SHAPE_BOX) {
                fieldF("W", "Width", half.x * 2, 1.f, 1.f, 600.f, "", 2, "In cells", [&](float v) { apply(pos, Vec2(v * 0.5f, half.y), radius, deg); }, "tw", "Resize");
                fieldF("H", "Height", half.y * 2, 1.f, 1.f, 600.f, "", 2, "In cells", [&](float v) { apply(pos, Vec2(half.x, v * 0.5f), radius, deg); }, "th", "Resize");
            } else fieldF("R", "Radius", radius, 0.5f, 0.5f, 300.f, "", 2, "In cells", [&](float v) { apply(pos, half, v, deg); }, "tr", "Resize");
        }
        fieldF("A", "Angle", deg, 1.f, -720.f, 720.f, "deg", 1, "Clockwise on screen", [&](float v) { apply(pos, half, radius, v); }, "ta", "Rotate");
    }
    if (!whole && ui.section("body", "Body")) {
        bodyMaterialPicker("bmat", inspectorMatSearch, b.mat, [this](uint8_t m) { setBodyMaterial(m); });
        bool fixed = b.isStatic;
        if (ui.toggle("bfixed", "Fixed", fixed, "A fixed body stays where it is: walls, cylinder blocks, mounts (T)")) setFixed(fixed);
        const MatInfo& mi = MATS[b.mat];
        ui.keyValue("Density", fmt(mi.density));
        ui.keyValue("Melts at", mi.hiT < 1e8f ? fmt(mi.hiT) + " C" : "never");
        if (mi.elec > 0.f) ui.keyValue("Conducts", fmt(mi.elec));
    }
    if (!whole && b.shape == SHAPE_BOX && !b.isRocket && !b.isWheel && ui.section("fan", "Fan", true, b.fan.strength != 0.f ? "on" : nullptr)) {
        if (b.fan.strength == 0.f) {
            if (ui.button("makefan", "Make this a fan", icons::Fan, "Airflow along the body's +x axis")) { pushUndo(nullptr, "Make fan"); phys.bodies[primary].fan.strength = lastFan; phys.bodies[primary].fan.vacuum = fanVacuumDefault; }
        } else {
            fieldF("fanS", "Strength", std::fabs(b.fan.strength), 5.f, 5.f, 300.f, "/s", 0, "Cells per second of gas the fan moves", [&](float v) { setFanStrength(v); }, "fan", "Fan strength");
            int m = ui.segmented("fanmode", {"Blow", "Vacuum"}, b.fan.vacuum ? 1 : 0, "Blow draws ambient air in; vacuum only pulls the gas that is there");
            if ((m == 1) != (b.fan.vacuum != 0)) setFanVacuum(m == 1);
            ui.row(2);
            if (ui.button("fanflip", "Flip", icons::FlipH, "Reverse the airflow")) flipFan();
            if (ui.button("fanoff", "Not a fan", icons::Close, "Strength 0: an ordinary body again")) { pushUndo(nullptr, "Fan off"); phys.bodies[primary].fan.strength = 0.f; }
        }
    }
    if (!whole && b.shape == SHAPE_BOX && !b.isRocket && ui.section("emitter", "Emitter", true, b.src.on ? "on" : nullptr)) {
        if (!b.src.on) {
            if (ui.button("makeemit", "Make this an emitter", icons::Emitter, "It will endlessly produce a material out of one side")) { pushUndo(nullptr, "Make emitter"); phys.bodies[primary].src = Emitter{true, payload, lastRate, 0.f, emitFace}; }
        } else {
            std::vector<std::string> names; int cur = 0;
            for (size_t i = 0; i < EMIT_MATS.size(); ++i) { names.push_back(MATS[EMIT_MATS[i]].name); if (EMIT_MATS[i] == b.src.mat) cur = (int)i; }
            if (ui.dropdown("emits", "Emits", names, cur, "What it produces")) setEmitMaterial(EMIT_MATS[cur]);
            fieldF("rate", "Rate", b.src.rate, 5.f, 5.f, 1000.f, "/s", 0, "Cells per second", [&](float v) { setRate(v); }, "rate", "Emitter rate");
            int face = ui.segmented("face", {"All", "+X", "-X", "+Y", "-Y"}, b.src.face, "The outlet side, in the block's own frame");
            if (face != b.src.face) setSelectionFace(face);
            if (ui.button("emitoff", "Stop emitting", icons::Close, "An ordinary body again")) { pushUndo(nullptr, "Emitter off"); phys.bodies[primary].src.on = false; }
        }
    }
    if (ui.section("info", "Info", false)) {
        ui.keyValue("Id", std::to_string(b.id));
        ui.keyValue("Mass", fmt(b.mass));
        ui.keyValue("Temperature", fmt(b.temp) + " C");
        ui.keyValue("Group", b.group >= 0 ? std::to_string(b.group) + " (" + std::to_string(phys.groupMembers(b.group).size()) + " parts)" : "none");
        if (b.isRocket) ui.keyValue("Kind", "Rocket");
        if (b.isWheel) ui.keyValue("Kind", "Wheel");
    }
    if (whole) {
        ui.row(2);
        if (ui.button("gungroup", "Ungroup", icons::Ungroup, tipOf("edit.ungroup").c_str(), true, false, "Ctrl+U")) ungroupSelection();
        if (ui.button("gscale", "Scale " + fmt(lastScale) + "%", icons::Scale, "Resize the group about its centre")) scaleSelection(lastScale);
    }
    deleteButton();
}

// The Materials tab: the whole palette in categories with a search field and favourites; one grid for painting, one for bodies.
void Game::drawMaterials() {
    ui.textField("matsearch", matSearch, "Search materials");
    const std::string q = lower(matSearch);
    auto grid = [&](const char* id, const std::vector<uint8_t>& mats, uint8_t current, bool paint) {
        std::vector<ui::SwatchItem> items;
        std::vector<uint8_t> ids;
        int cur = -1;
        for (uint8_t m : mats) {
            if (!matches(q, MATS[m].name)) continue;
            if (m == current) cur = (int)items.size();
            items.push_back({m == M_EMPTY ? 0xff5050u : MATS[m].color, m == M_EMPTY ? "Eraser" : MATS[m].name, matTip(m)});
            ids.push_back(m);
        }
        if (items.empty()) return false;
        int hit = ui.swatchGrid(id, items, cur, 30, false);   // the names and properties are the chips' tooltips
        if (hit >= 0) {
            uint8_t m = ids[hit];
            if (in.shift) {   // Shift+click marks a favourite
                auto it = std::find(favourites.begin(), favourites.end(), m);
                if (it != favourites.end()) favourites.erase(it); else favourites.push_back(m);
            } else if (paint) selectMaterial(m);
            else setBodyMaterial(m);
        }
        return true;
    };
    ui.labelWrapped("Hover a chip for its properties. Click assigns it to the tool or the selection; Shift+click marks a favourite.", ui::TextStyle::Dim);
    if (!favourites.empty() && ui.section("fav", "Favourites")) grid("favgrid", favourites, tool == T_MAT ? mat : bodyMat, tool == T_MAT || !std::count(BODY_MATS.begin(), BODY_MATS.end(), tool == T_MAT ? mat : bodyMat));
    if (!recentMats.empty() && ui.section("recent", "Recent", false)) grid("recentgrid", recentMats, tool == T_MAT ? mat : bodyMat, tool == T_MAT);
    if (ui.section("paintmats", "Paint", true, tool == T_MAT && mat != M_EMPTY ? MATS[mat].name : nullptr)) {
        for (auto& g : PAINT_GROUPS) {
            ui.pushId(g.name);
            SDL_Rect r = ui.next(ui::theme().sectionH);
            ui.text(g.name, r.x, r.y + (r.h - fontH()) / 2, ui::TextStyle::Section);
            if (!grid("grid", g.mats, mat, true)) ui.label("no match", ui::TextStyle::Disabled);
            ui.popId();
        }
    }
    if (ui.section("bodymats", "Bodies", true, MATS[bodyMat].name)) {
        grid("bodygrid", BODY_MATS, bodyMat, false);
        ui.keyValue("New bodies", MATS[bodyMat].name);
        if (!sel.empty()) ui.label("Clicking recolours the selection", ui::TextStyle::Dim);
    }
}

// The Scene tab: the world's bodies, groups and joints; click selects, double-click zooms to it.
void Game::drawSceneTab() {
    ui.textField("scenefilter", sceneFilter, "Filter by kind or material");
    struct Row { bool joint; int id; std::string name, detail; };
    std::vector<Row> rows;
    std::vector<int> groupsSeen;
    for (auto& b : phys.bodies) {
        if (!b.alive) continue;
        std::string n = bodyName(b), d = (b.isStatic ? "fixed" : "free");
        if (b.group >= 0) { d += ", group " + std::to_string(b.group); }
        if (!matches(sceneFilter, n + " " + d)) continue;
        rows.push_back({false, b.id, n, d});
    }
    for (auto& j : phys.joints) {
        if (!jointValid(j.id)) continue;
        std::string n = jointName(j), d = j.b < 0 ? "to the world" : "bodies " + std::to_string(j.a) + " and " + std::to_string(j.b);
        if (!matches(sceneFilter, n)) continue;
        rows.push_back({true, j.id, n, d});
    }
    std::vector<std::string> names, details;
    int selected = -1;
    for (size_t i = 0; i < rows.size(); ++i) {
        names.push_back(rows[i].name); details.push_back(rows[i].detail);
        if (!rows[i].joint && rows[i].id == primary) selected = (int)i;
        if (rows[i].joint && rows[i].id == selJoint) selected = (int)i;
    }
    const int nJoints = (int)std::count_if(phys.joints.begin(), phys.joints.end(), [&](const Joint& j) { return jointValid(j.id); });
    ui.label(std::to_string(phys.bodyCount()) + " bodies, " + std::to_string(nJoints) + " joints", ui::TextStyle::Dim);
    int hit = ui.listView("scenelist", names, selected, std::max(6, (ui.panelRect().h - 5 * ui::theme().rowH) / ui::theme().rowH), &details);
    if (hit >= 0) {
        if (rows[hit].joint) selectJoint(rows[hit].id); else selectBody(rows[hit].id, in.shift, in.ctrl);
        if (ui.listActivated() && !rows[hit].joint) zoomToBody(rows[hit].id);
    }
    ui.row(2);
    if (ui.button("wipecells", "Wipe cells", icons::None, tipOf("edit.wipecells").c_str(), !playing)) runCommand("edit.wipecells");
    if (ui.button("wipebodies", "Wipe bodies", icons::None, tipOf("edit.wipebodies").c_str(), !playing)) runCommand("edit.wipebodies");
}

// The History tab: the undo stack, one line per step, then Now, then what can be redone; click to jump.
void Game::drawHistory() {
    std::vector<std::string> rows = undoLabels;
    rows.push_back("Now");
    for (int i = (int)redoLabels.size() - 1; i >= 0; --i) rows.push_back(redoLabels[i]);
    int hit = ui.listView("history", rows, (int)undoLabels.size(), std::max(6, (ui.panelRect().h - 3 * ui::theme().rowH) / ui::theme().rowH));
    if (hit >= 0 && !playing) jumpHistory(hit);
    ui.label(playing ? "Stop the simulation to undo" : std::to_string(undoLabels.size()) + " to undo, " + std::to_string(redoLabels.size()) + " to redo", ui::TextStyle::Dim);
}

// The status bar: the tool's bindings or the hovered tip, the cursor and what is under it, the selection, zoom and FPS.
void Game::drawStatusBar() {
    const ui::Theme& th = ui::theme();
    ui.beginPanel("status", L.status, false, true, 0);
    SDL_Rect r = ui.panelRect();
    const int ty = r.y + (r.h - fontH()) / 2;
    auto fit = [&](std::string s, int maxW) {
        if (ui.textWidth(s) <= maxW) return s;
        while (!s.empty() && ui.textWidth(s + "...") > maxW) s.pop_back();
        return s + "...";
    };
    std::string right = selectionText();
    if (!right.empty()) right += "  ·  ";
    right += fmt(zoom) + "x  ·  " + std::to_string((int)std::lround(fps)) + " FPS";
    int rightW = ui.textWidth(right);
    std::string centre;
    if (inSim) {
        Vec2 m = smouse();
        centre = "X " + std::to_string((int)std::floor(m.x)) + "  Y " + std::to_string((int)std::floor(m.y));
        std::string under = hoverText();
        if (!under.empty()) centre += "   " + under;
    }
    std::string left = guide.on ? "Snap: " + guide.name : ui.hoveredTip();
    if (left.empty()) left = toolBindings();
    const int pad = th.pad;
    // the centre read-out gets its natural width (at most two fifths), the left text what is left
    const int room = r.w - rightW - 5 * pad;
    const int centreW = std::min(ui.textWidth(centre), room * 2 / 5);
    const int leftMax = std::max(60, room - centreW);
    int leftW = std::min(ui.textWidth(left), leftMax);
    ui.text(fit(left, leftMax), r.x + pad, ty, ui::TextStyle::Dim);
    int cx = r.x + pad + leftW + 2 * pad;
    int centreMax = r.x + r.w - rightW - 2 * pad - cx;
    if (centreMax > 40) ui.text(fit(centre, centreMax), cx, ty, ui::TextStyle::Normal);
    ui.text(right, r.x + r.w - pad - rightW, ty, ui::TextStyle::Dim);
    ui.endPanel();
}

// ---------------------------------------------------------------- overlays on the canvas
void Game::drawCanvasOverlays() {
    const ui::Theme& th = ui::theme();
    // the mode tints the canvas frame: green running, amber paused
    if (playing) ui.strokeRect(L.canvas, paused ? th.warning : th.success, 200);
    // the dimension field beside the pointer while a shape is drawn or a handle dragged
    if (dim.active && lmb) {
        int x = in.mx + 18, y = in.my + 18;
        const int fw = 112, fh = fontH() + 8;
        const int w = dim.n * (fw + 4) + 4, h = fh + 8;
        if (x + w > L.canvas.x + L.canvas.w) x = in.mx - w - 8;
        if (y + h > L.canvas.y + L.canvas.h) y = in.my - h - 8;
        ui.fillRect(SDL_Rect{x, y, w, h}, th.surface, 240, th.radius);
        ui.strokeRect(SDL_Rect{x, y, w, h}, th.border, 255, th.radius);
        for (int i = 0; i < dim.n; ++i) {
            SDL_Rect f{x + 4 + i * (fw + 4), y + 4, fw, fh};
            ui.fillRect(f, th.surface2, 255, th.radius);
            ui.strokeRect(f, i == dim.cur ? th.focus : th.border, 255, th.radius);
            std::string val = dim.typed[i] ? dim.text[i] + (i == dim.cur ? "_" : "") : fmt(dim.shown[i]);
            ui.text(dim.names[i], f.x + 6, f.y + 4, ui::TextStyle::Dim);
            int vw = ui.textWidth(val);
            ui.text(val, f.x + f.w - 6 - vw, f.y + 4, dim.typed[i] ? ui::TextStyle::Accent : ui::TextStyle::Normal);
        }
    }
    // "select other": the item under the pointer in the list is pre-highlighted on the canvas
    if (selOtherOpen && selOtherHover >= 0 && selOtherHover < (int)selOther.size()) {
        const Pick& p = selOther[selOtherHover];
        SDL_Rect r = L.canvas;
        SDL_RenderSetViewport(ren, &r);
        if (!p.joint && p.id >= 0 && p.id < (int)phys.bodies.size() && phys.bodies[p.id].alive) outlinePoly(bodyOutline(phys.bodies[p.id], 1.5f), rgb(th.accent));
        else if (p.joint && jointValid(p.id)) outlinePoly(circlePts(phys.jointAnchorA(phys.joints[p.id]), 6.f, 16), rgb(th.accent));
        SDL_RenderSetViewport(ren, nullptr);
    }
}

// ---------------------------------------------------------------- popups
void Game::drawPopups() {
    drawSelectOther();
    drawContextMenu();
    if (scaleOpen) {
        SDL_Rect anchor{L.strip.x + L.strip.w, L.ctx.y + L.ctx.h + 8, 1, 1};
        if (ui.beginPopup("scalepop", anchor, 280, scaleOpen)) {
            ui.label("Scale the selection", ui::TextStyle::Heading);
            if (ui.dragFloat("scalepct", "Percent", lastScale, 1.f, 5.f, 1000.f, "%", 0, "About the selection's centre; joints and welds follow")) {}
            ui.row(2);
            if (ui.button("scaleok", "Apply", icons::Scale, "Scale now", !sel.empty())) { scaleSelection(lastScale); scaleOpen = false; }
            if (ui.button("scalecancel", "Cancel", icons::Close, "Close")) scaleOpen = false;
            ui.endPopup();
        }
    }
    if (paletteOpen) ui.palette("palette", paletteOpen, commands, paletteQuery);
    drawScenesDialog();
    drawFileDialog();
    if (newConfirm) {
        if (ui.beginModal("newmodal", "New file", 420, 170, newConfirm)) {
            ui.labelWrapped("Clear everything? Ctrl+Z brings the drawing back.");
            ui.space(ui::theme().gap);
            ui.row(2);
            if (ui.button("newok", "Clear", icons::New, "Start from an empty world")) { newFile(); newConfirm = false; }
            if (ui.button("newcancel", "Cancel", icons::Close, "Keep the drawing")) newConfirm = false;
            ui.endModal();
        }
    }
    drawCheatSheet();
}

void Game::openContextMenu(int px, int py) {
    ctxOpen = true; matMenuOpen = false;
    ctxX = px; ctxY = py;
    ctxWorld = toWorld(px, py);
    int jj = jointAt(ctxWorld);
    int body = topBodyAt(ctxWorld, true);
    ctxCanvas = jj < 0 && body < 0;
    if (!ctxCanvas) {
        if (tool != T_SELECT) setTool(T_SELECT);
        if (jj >= 0 && body < 0) { if (selJoint != jj) selectJoint(jj); }
        else if (body >= 0 && std::find(sel.begin(), sel.end(), body) == sel.end()) selectBody(body, false, false);
    }
}
// Right click on a body or joint, or on empty canvas. The slots never move between invocations; items that do not apply are dimmed.
void Game::drawContextMenu() {
    if (!ctxOpen && !matMenuOpen) return;
    if (ctxOpen) {
        std::vector<ui::MenuItem> items;
        const bool joint = jointValid(selJoint) && sel.empty();
        bool grouped = false;
        for (int id : sel) if (phys.bodies[id].group >= 0) grouped = true;
        bool fixed = !sel.empty() && std::all_of(sel.begin(), sel.end(), [&](int id) { return phys.bodies[id].isStatic; });
        if (!ctxCanvas) {
            items.push_back({"Properties", "", icons::Settings, true, true});
            items.push_back({"Duplicate", shortcutOf("edit.duplicate"), icons::Duplicate, !joint});
            items.push_back({"Flip left-right", shortcutOf("edit.fliph"), icons::FlipH, !joint});
            items.push_back({"Flip top-bottom", shortcutOf("edit.flipv"), icons::FlipV, !joint, true});
            items.push_back({"Group", shortcutOf("edit.group"), icons::Group, sel.size() >= 2});
            items.push_back({"Ungroup", shortcutOf("edit.ungroup"), icons::Ungroup, grouped, true});
            items.push_back({"Fixed", shortcutOf("edit.fixed"), icons::Fixed, !joint, false, fixed});
            items.push_back({"Material...", "", icons::Layers, !joint, true});
            items.push_back({"Delete", shortcutOf("edit.delete"), icons::Delete, true});
        } else {
            items.push_back({"Paste here", shortcutOf("edit.paste"), icons::Paste, !clip.bodies.empty()});
            items.push_back({"Select all", shortcutOf("edit.selectall"), icons::Select, phys.bodyCount() > 0, true});
            items.push_back({"Add box here", "", icons::Box, true});
            items.push_back({"Add circle here", "", icons::Circle, true, true});
            items.push_back({"Zoom to fit", "", icons::ZoomFit, true});
        }
        int pick = ui.contextMenu("ctxmenu", ctxOpen, ctxX, ctxY, items);
        if (pick >= 0) {
            if (!ctxCanvas) {
                switch (pick) {
                    case 0: runCommand("edit.properties"); break;
                    case 1: mouse = ctxWorld; inSim = true; duplicateSelection(); break;
                    case 2: flipSelection(true); break;
                    case 3: flipSelection(false); break;
                    case 4: groupSelection(); break;
                    case 5: ungroupSelection(); break;
                    case 6: setFixed(!fixed); break;
                    case 7: matMenuOpen = true; break;
                    default: deleteSelection(); break;
                }
            } else {
                switch (pick) {
                    case 0: pasteAt(snap(ctxWorld)); break;
                    case 1: selectAll(); break;
                    case 2: { pushUndo(nullptr, "Add box"); setTool(T_BOX); createShape(ctxWorld - Vec2(lastW * 0.5f, lastH * 0.5f), ctxWorld + Vec2(lastW * 0.5f, lastH * 0.5f)); setTool(T_SELECT); phys.stampBodies(); break; }
                    case 3: { pushUndo(nullptr, "Add circle"); setTool(T_CIRCLE); createShape(ctxWorld, ctxWorld + Vec2(lastR, 0)); setTool(T_SELECT); phys.stampBodies(); break; }
                    default: zoomFit(); break;
                }
            }
        }
    }
    if (matMenuOpen) {   // the material submenu: recent materials first, then every body material
        SDL_Rect anchor{ctxX, ctxY, 1, 1};
        if (ui.beginPopup("matmenu", anchor, 300, matMenuOpen)) {
            ui.label("Material", ui::TextStyle::Heading);
            std::vector<uint8_t> list;
            for (uint8_t m : recentMats) if (std::count(BODY_MATS.begin(), BODY_MATS.end(), m)) list.push_back(m);
            for (uint8_t m : BODY_MATS) if (!std::count(list.begin(), list.end(), m)) list.push_back(m);
            std::vector<ui::SwatchItem> items;
            int cur = -1;
            for (size_t i = 0; i < list.size(); ++i) { items.push_back({MATS[list[i]].color, MATS[list[i]].name, matTip(list[i])}); if (primary >= 0 && list[i] == phys.bodies[primary].mat) cur = (int)i; }
            int hit = ui.swatchGrid("matmenugrid", items, cur, 28, true);
            if (hit >= 0) { setBodyMaterial(list[hit]); matMenuOpen = false; }
            ui.endPopup();
        }
    }
}

// "Select other": a list of everything under the pointer, front to back; hovering a row highlights it on the canvas.
void Game::drawSelectOther() {
    if (!selOtherOpen) return;
    SDL_Rect anchor{selOtherX, selOtherY, 1, 1};
    if (!ui.beginPopup("selother", anchor, 280, selOtherOpen)) { selOtherHover = -1; return; }
    ui.label("Under the pointer", ui::TextStyle::Heading);
    selOtherHover = -1;
    for (size_t i = 0; i < selOther.size(); ++i) {
        ui.pushId((int)i);
        std::string tip = "Select " + selOther[i].name + " #" + std::to_string(i);
        if (ui.button("pick", selOther[i].name, selOther[i].joint ? icons::Pin : icons::Box, tip.c_str())) {
            if (selOther[i].joint) selectJoint(selOther[i].id); else selectBody(selOther[i].id, in.shift, in.ctrl);
            selOtherOpen = false;
        }
        ui.popId();
        if (ui.hoveredTip() == tip) selOtherHover = (int)i;
    }
    ui.endPopup();
}

// The scene browser: a list with a line about each scene; Load or a double click replaces the drawing.
void Game::drawScenesDialog() {
    if (!scenesOpen) return;
    const auto& scenes = sceneList();
    if (!ui.beginModal("scenes", "Scenes", std::min(L.winW - 80, 640), std::min(L.winH - 80, 560), scenesOpen)) return;
    ui.labelWrapped("Ready-made machines and tests. Loading one replaces the drawing (Ctrl+Z brings it back).", ui::TextStyle::Dim);
    std::vector<std::string> names, tips;
    for (auto& s : scenes) { names.push_back(s.name); tips.push_back(s.tip); }
    int hit = ui.listView("scenelist", names, sceneSel, 10, &tips);
    if (hit >= 0) { sceneSel = hit; if (ui.listActivated()) { loadScene(hit); ui.endModal(); return; } }
    ui.row(2);
    if (ui.button("sceneload", "Load", icons::Open, "Load the highlighted scene", sceneSel >= 0)) { loadScene(sceneSel); ui.endModal(); return; }
    if (ui.button("scenecancel", "Cancel", icons::Close, "Keep the drawing")) scenesOpen = false;
    ui.endModal();
}

// The file dialog: a name and the saved files (newest first); Enter, the button or a double click accepts.
void Game::drawFileDialog() {
    if (!fileOpen) return;
    if (!ui.beginModal("filedlg", fileSave ? "Save as" : "Open", std::min(L.winW - 80, 520), std::min(L.winH - 80, 480), fileOpen)) return;
    bool submitted = false;
    ui.textField("filename", fileName, "File name (saves/NAME.sbot)", &submitted, nullptr, 24);
    int hit = ui.listView("files", fileList, fileSel, 8);
    if (hit >= 0) { fileSel = hit; fileName = fileList[hit]; if (ui.listActivated()) submitted = true; }
    if (fileList.empty()) ui.label("No saved files yet", ui::TextStyle::Dim);
    ui.row(2);
    if (ui.button("fileok", fileSave ? "Save" : "Open", fileSave ? icons::Save : icons::Open, fileSave ? "Write saves/NAME.sbot" : "Load the file", !fileName.empty())) submitted = true;
    if (ui.button("filecancel", "Cancel", icons::Close, "Close")) fileOpen = false;
    if (submitted) fileDialogAccept();
    ui.endModal();
}

// The F1 cheat sheet: every command with its shortcut, from the one command table, plus the fixed mouse and key bindings.
std::vector<std::string> Game::cheatLines() const {
    std::vector<std::string> out;
    static const char* fixedRows[] = {
        "## Mouse", "LMB|draw, select, drag", "RMB|cancel, or the menu", "Middle drag|pan", "Wheel|zoom at the pointer", "Shift+wheel|brush or pipe size",
        "Click again|cycle stacked bodies", "Hold 0.4 s|select other", "Shift+click|add to the selection", "Ctrl+click|pick a group part", "Drag empty|box select",
        "## Keys with a fixed meaning", "Esc|cancel, deselect, close", "Enter|commit the drag", "Tab|next value", "Arrows|nudge (Shift 10, Ctrl 1/4)",
        "A D / Up W|motors / rockets", "Z (hold)|spark plugs", "[ ]|brush size", "Home End PgUp PgDn|pan", "Shift (drag)|proportions, 15 deg", "Ctrl (drag)|centre, invert snap",
    };
    for (const char* r : fixedRows) out.push_back(r);
    std::vector<std::string> cats;
    for (auto& c : commands) if (c.category != "Materials" && std::find(cats.begin(), cats.end(), c.category) == cats.end()) cats.push_back(c.category);
    for (auto& cat : cats) {
        out.push_back("## " + cat);
        for (auto& c : commands) if (c.category == cat) out.push_back(c.shortcut + "|" + c.name);
    }
    return out;
}
void Game::drawCheatSheet() {
    if (!cheatOpen) return;
    if (!ui.beginModal("cheat", "Cheat sheet", std::min(L.winW - 60, 1280), std::min(L.winH - 60, 760), cheatOpen)) return;
    std::vector<std::string> lines = cheatLines();
    // three columns of roughly equal length (two in a narrow window), each a scrolling panel, sections kept whole
    SDL_Rect area = ui.next(ui.panelRect().h - ui::theme().rowH - 2 * ui::theme().gap);
    const int cols = area.w >= 1100 ? 3 : 2, colW = (area.w - (cols - 1) * ui::theme().gap) / cols;
    std::vector<std::vector<std::string>> colLines(cols);
    size_t per = (lines.size() + cols - 1) / cols, i = 0;
    for (int c = 0; c < cols && i < lines.size(); ++c) {
        while (i < lines.size() && (colLines[c].size() < per || c == cols - 1 || lines[i].rfind("## ", 0) != 0)) colLines[c].push_back(lines[i++]);
    }
    for (int c = 0; c < cols; ++c) {
        SDL_Rect r{area.x + c * (colW + ui::theme().gap), area.y, colW, area.h};
        ui.pushId(c);
        ui.beginPanel("col", r, true, false, 4);
        for (auto& l : colLines[c]) {
            if (l.rfind("## ", 0) == 0) { ui.space(ui::theme().gap); ui.label(l.substr(3), ui::TextStyle::Section); continue; }
            size_t bar = l.find('|');
            std::string key = l.substr(0, bar), what = bar == std::string::npos ? "" : l.substr(bar + 1);
            if (key.empty()) key = "-";
            SDL_Rect row = ui.next(fontH() + 4);   // the name takes what the shortcut leaves, cut with an ellipsis
            int kw = ui.textWidth(key);
            ui.text(key, row.x + row.w - kw, row.y + 2, ui::TextStyle::Dim);
            int maxW = row.w - kw - 2 * ui::theme().gap;
            if (ui.textWidth(what) > maxW) { while (!what.empty() && ui.textWidth(what + "...") > maxW) what.pop_back(); what += "..."; }
            ui.text(what, row.x, row.y + 2, ui::TextStyle::Normal);
        }
        ui.endPanel();
        ui.popId();
    }
    ui.label("F1 or Esc closes. Ctrl+K searches the same commands.", ui::TextStyle::Dim);
    ui.endModal();
}
