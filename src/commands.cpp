// The command table: one list of everything the interface can do, with its name, category, shortcut and description. The
// palette searches it, the strip and top bar take their tooltips from it, the context menu and the F1 cheat sheet list it,
// and the keyboard map runs it, so a shortcut is documented in exactly one place.
#include "app.hpp"

namespace {
// does a shortcut such as "Ctrl+Shift+H", "Space", "Del", "F1", "`" or "-" name this key press?
SDL_Keycode keyNamed(const std::string& n) {
    static const std::pair<const char*, SDL_Keycode> special[] = {
        {"Space", SDLK_SPACE}, {"Enter", SDLK_RETURN}, {"Esc", SDLK_ESCAPE}, {"Tab", SDLK_TAB}, {"Del", SDLK_DELETE}, {"Backspace", SDLK_BACKSPACE},
        {"F1", SDLK_F1}, {"F2", SDLK_F2}, {"Home", SDLK_HOME}, {"End", SDLK_END}, {"PgUp", SDLK_PAGEUP}, {"PgDn", SDLK_PAGEDOWN},
        {"Left", SDLK_LEFT}, {"Right", SDLK_RIGHT}, {"Up", SDLK_UP}, {"Down", SDLK_DOWN}, {"`", SDLK_BACKQUOTE}, {"-", SDLK_MINUS}, {"=", SDLK_EQUALS},
        {"[", SDLK_LEFTBRACKET}, {"]", SDLK_RIGHTBRACKET}, {",", SDLK_COMMA}, {".", SDLK_PERIOD}, {"\\", SDLK_BACKSLASH},
    };
    for (auto& s : special) if (n == s.first) return s.second;
    if (n.size() == 1) {
        char c = n[0];
        if (c >= 'A' && c <= 'Z') return (SDL_Keycode)(c - 'A' + 'a');
        if (c >= '0' && c <= '9') return (SDL_Keycode)c;
    }
    return SDLK_UNKNOWN;
}
bool shortcutHit(const std::string& sc, SDL_Keycode k, bool ctrl, bool shift) {
    if (sc.empty()) return false;
    // several shortcuts may be separated by " / "
    size_t p = 0;
    while (p <= sc.size()) {
        size_t e = sc.find(" / ", p);
        std::string one = sc.substr(p, e == std::string::npos ? std::string::npos : e - p);
        bool wantCtrl = false, wantShift = false;
        size_t q = 0;
        while (true) {
            size_t plus = one.find('+', q);
            if (plus == std::string::npos || plus + 1 >= one.size()) break;
            std::string mod = one.substr(q, plus - q);
            if (mod == "Ctrl") wantCtrl = true; else if (mod == "Shift") wantShift = true; else break;
            q = plus + 1;
        }
        std::string key = one.substr(q);
        if (keyNamed(key) == k && wantCtrl == ctrl && wantShift == shift) return true;
        if (e == std::string::npos) break;
        p = e + 3;
    }
    return false;
}
}  // namespace

void Game::buildCommands() {
    commands.clear(); commandIndex.clear();
    auto add = [&](const char* id, const char* name, const char* cat, const char* sc, const char* desc, icons::Id icon, std::function<void()> run,
                   std::function<bool()> enabled = nullptr) {
        ui::Command c;
        c.name = name; c.category = cat; c.shortcut = sc; c.description = desc; c.icon = icon; c.run = run; c.enabled = enabled;
        commandIndex[id] = (int)commands.size();
        commands.push_back(c);
    };
    auto editing = [this] { return !playing || paused; };
    auto hasSel = [this] { return !sel.empty(); };
    auto hasSelOrJoint = [this] { return !sel.empty() || jointValid(selJoint); };
    // tools
    for (int t = 0; t < T_COUNT; ++t) {
        const ToolInfo& ti = toolInfo((Tool)t);
        std::string id = std::string("tool.") + ti.name;
        std::string sc = ti.key;
        for (int d = 0; d < 10; ++d) if (DIGIT_TOOLS[d] == t) sc += (sc.empty() ? "" : " / ") + std::string(1, (char)('0' + d));
        if (t == T_MAT) add("tool.Paint", "Paint", "Tools", sc.c_str(), ti.what, ti.icon, [this] { pickTool(T_MAT); });
        else add(id.c_str(), ti.name, "Tools", sc.c_str(), ti.what, ti.icon, [this, t] { setTool((Tool)t); });
    }
    add("tool.Erase", "Erase", "Tools", "X", "Remove cells under the brush", icons::Eraser, [this] { setTool(T_MAT); mat = M_EMPTY; });
    // transport
    add("sim.play", "Play / pause", "Simulation", "Space", "Run the simulation (the drawing is snapshot first), or pause and resume it", icons::Play,
        [this] { if (!playing) play(); else togglePause(); });
    add("sim.step", "Step one frame", "Simulation", "N", "Advance the simulation by one frame", icons::Step, [this] { stepFrame(); });
    add("sim.stop", "Stop and restore", "Simulation", "Shift+Space", "Stop and put the drawing back exactly as it was before Play", icons::Stop, [this] { stopPlay(); },
        [this] { return playing; });
    add("sim.gravity", "Flip gravity", "Simulation", "", "Gravity pulls the other way", icons::Gravity,
        [this] { pushUndo(nullptr, "Flip gravity"); phys.gravity.y = phys.gravity.y > 0 ? -260.f : 260.f; notify(phys.gravity.y > 0 ? "Gravity down" : "Gravity up"); });
    // files
    add("file.new", "New", "File", "Ctrl+N", "Clear everything (asks first)", icons::New, [this] { newConfirm = true; }, editing);
    add("file.open", "Open", "File", "Ctrl+O", "Open a saved file from saves/", icons::Open, [this] { openFileDialog(false); }, editing);
    add("file.save", "Save", "File", "Ctrl+S", "Save to the current file (asks for a name the first time)", icons::Save, [this] { saveQuick(); });
    add("file.saveas", "Save as", "File", "Ctrl+Shift+S", "Save under a new name", icons::SaveAs, [this] { openFileDialog(true); });
    add("file.scenes", "Scenes", "File", "", "Ready-made machines and tests", icons::Scenes, [this] { scenesOpen = !scenesOpen; sceneSel = -1; });
    // edit
    add("edit.undo", "Undo", "Edit", "Ctrl+Z", "Undo the last edit", icons::Undo, [this] { undo(); }, [this] { return !undoStack.empty() && !playing; });
    add("edit.redo", "Redo", "Edit", "Ctrl+Y / Ctrl+Shift+Z", "Redo what was undone", icons::Redo, [this] { redo(); }, [this] { return !redoStack.empty() && !playing; });
    add("edit.copy", "Copy", "Edit", "Ctrl+C", "Copy the selected bodies with the joints between them", icons::Copy, [this] { copySelection(); }, hasSel);
    add("edit.paste", "Paste", "Edit", "Ctrl+V", "Paste the clipboard at the pointer", icons::Paste, [this] { pasteClipboard(); }, [this] { return !clip.bodies.empty(); });
    add("edit.duplicate", "Duplicate", "Edit", "Ctrl+D", "Copy the selection, joints included, at the pointer or beside itself", icons::Duplicate, [this] { duplicateSelection(); }, hasSel);
    add("edit.delete", "Delete", "Edit", "Del", "Remove the selected bodies or joint", icons::Delete, [this] { deleteSelection(); }, hasSelOrJoint);
    add("edit.selectall", "Select all", "Edit", "Ctrl+A", "Select every body", icons::Select, [this] { selectAll(); });
    add("edit.group", "Group", "Edit", "Ctrl+G", "Weld the selected bodies into one rigid object", icons::Group, [this] { groupSelection(); }, [this] { return sel.size() >= 2; });
    add("edit.ungroup", "Ungroup", "Edit", "Ctrl+U", "Split a group back into bodies", icons::Ungroup, [this] { ungroupSelection(); },
        [this] { for (int id : sel) if (id >= 0 && id < (int)phys.bodies.size() && phys.bodies[id].group >= 0) return true; return false; });
    add("edit.fliph", "Flip left-right", "Edit", "Ctrl+H", "Mirror the selection about its centre, left to right", icons::FlipH, [this] { flipSelection(true); }, hasSel);
    add("edit.flipv", "Flip top-bottom", "Edit", "Ctrl+Shift+H", "Mirror the selection about its centre, top to bottom", icons::FlipV, [this] { flipSelection(false); }, hasSel);
    add("edit.fixed", "Toggle fixed", "Edit", "T", "A fixed body stays where it is: walls, cylinder blocks, mounts", icons::Fixed, [this] { setFixed(!anchored); });
    add("edit.scale", "Scale", "Edit", "", "Resize the selection by a percentage about its centre", icons::Scale, [this] { scaleOpen = !scaleOpen; }, hasSel);
    add("edit.subtract", "Subtract", "Edit", "", "Cut the last-clicked (red) body out of the other selected bodies", icons::Subtract, [this] { cutSelection(); },
        [this] { return sel.size() >= 2; });
    add("edit.wipecells", "Wipe cells", "Edit", "", "Remove every sand, liquid, gas and solid cell", icons::Eraser,
        [this] { pushUndo(nullptr, "Wipe cells"); world.clear(); phys.stampBodies(); notify("All cells removed"); }, [this] { return !playing; });
    add("edit.wipebodies", "Wipe bodies", "Edit", "", "Remove every rigid body and joint", icons::Delete,
        [this] { pushUndo(nullptr, "Wipe bodies"); clearBodies(); clearSelection(); phys.stampBodies(); notify("All bodies removed"); }, [this] { return !playing; });
    add("edit.selectother", "Select other", "Edit", "`", "List everything under the pointer, front to back, to pick from", icons::Layers,
        [this] { if (inSim) openSelectOther(in.mx, in.my); });
    add("edit.properties", "Properties", "Edit", "", "Show the selection in the Inspector", icons::Settings, [this] { dockTab = 0; if (L.dockCollapsed) dockFlyout = true; });
    // view
    add("view.heat", "Heat view", "View", "H", "Colour everything by temperature", icons::Heat, [this] { heatView = !heatView; });
    add("view.pressure", "Pressure view", "View", "V", "Colour gas by pressure: blue below ambient, white about 1, red high", icons::Pressure, [this] { pressureView = !pressureView; });
    add("view.electric", "Electric view", "View", "E", "Show voltage and current on conductors", icons::Electric, [this] { elecView = !elecView; });
    add("view.grid", "Grid and rulers", "View", "I", "A reference grid with rulers numbered in cells and a scale bar", icons::Grid, [this] { gridOn = !gridOn; });
    add("view.zoomin", "Zoom in", "View", "=", "Zoom in one step (the wheel zooms about the pointer)", icons::ZoomIn, [this] { zoomCentre(1); }, [this] { return zoom < 7.9f; });
    add("view.zoomout", "Zoom out", "View", "-", "Zoom out one step", icons::ZoomOut, [this] { zoomCentre(-1); }, [this] { return zoom > 1.01f; });
    add("view.zoomreset", "Reset zoom", "View", "Ctrl+0", "Back to 1x", icons::ZoomFit, [this] { zoomReset(); });
    add("view.zoomfit", "Zoom to fit", "View", "", "Show the whole height of the world from its left edge", icons::ZoomFit, [this] { zoomFit(); });
    add("view.zoomsel", "Zoom to selection", "View", "Shift+F", "Zoom in as far as the selection fits the canvas (nothing selected: reset)", icons::ZoomIn, [this] { zoomToSelection(); });
    add("view.focus", "Follow body", "View", "F", "The camera follows the selected body (a group by its centre of mass)", icons::Focus, [this] { toggleFocus(); });
    add("view.snap", "Grid snap", "View", "", "Round drawn shapes, dropped bodies and dragged edges to the grid (Ctrl inverts while dragging)", icons::Snap,
        [this] { snapOn = !snapOn; });
    add("view.smartsnap", "Smart snap", "View", "", "A dragged body's edges and centre stick to other bodies' edges and centres", icons::Snap, [this] { smartSnap = !smartSnap; });
    add("view.strip", "Collapse tool strip", "View", "", "Show the tool strip as icons only", icons::ChevronRight, [this] { stripCollapsed = !stripCollapsed; });
    add("view.palette", "Command palette", "View", "Ctrl+K / Ctrl+Shift+P", "Search every command, scene and material", icons::Search, [this] { paletteOpen = true; paletteQuery.clear(); });
    add("view.help", "Cheat sheet", "View", "F1", "Every command and shortcut on one card", icons::Help, [this] { cheatOpen = !cheatOpen; });
    add("view.inspector", "Inspector tab", "View", "", "The selection's properties", icons::Settings, [this] { dockTab = 0; });
    add("view.materials", "Materials tab", "View", "", "The whole palette of materials", icons::Layers, [this] { dockTab = 1; });
    add("view.scenetab", "Scene tab", "View", "", "A list of the world's bodies, groups and joints", icons::Layers, [this] { dockTab = 2; });
    add("view.history", "History tab", "View", "", "The undo stack, one line per step", icons::Undo, [this] { dockTab = 3; });
    // scenes
    const auto& scenes = sceneList();
    for (size_t i = 0; i < scenes.size(); ++i) {
        std::string id = std::string("scene.") + std::to_string(i);
        add(id.c_str(), scenes[i].name, "Scenes", "", scenes[i].tip, icons::Scenes, [this, i] { loadScene((int)i); }, editing);
    }
    // materials: paint with one, or make bodies of one
    for (auto& g : PAINT_GROUPS)
        for (uint8_t m : g.mats) {
            std::string id = std::string("paint.") + MATS[m].name;
            add(id.c_str(), (std::string("Paint ") + MATS[m].name).c_str(), "Materials", "", matTip(m).c_str(), icons::Paint, [this, m] { selectMaterial(m); });
        }
    for (uint8_t m : BODY_MATS) {
        std::string id = std::string("body.") + MATS[m].name;
        add(id.c_str(), (std::string("Body material ") + MATS[m].name).c_str(), "Materials", "", (matTip(m) + " (for new bodies and the selection)").c_str(), icons::Box,
            [this, m] { setBodyMaterial(m); });
    }
}
const ui::Command* Game::cmd(const char* id) const {
    auto it = commandIndex.find(id);
    return it == commandIndex.end() ? nullptr : &commands[it->second];
}
void Game::runCommand(const char* id) {
    const ui::Command* c = cmd(id);
    if (!c) return;
    if (c->enabled && !c->enabled()) { notify(c->name + " is not available now"); return; }
    c->run();
}
const char* Game::shortcutOf(const char* id) const {
    const ui::Command* c = cmd(id);
    return c && !c->shortcut.empty() ? c->shortcut.c_str() : nullptr;
}
std::string Game::tipOf(const char* id) const {
    const ui::Command* c = cmd(id);
    if (!c) return "";
    return c->description;
}

// The keyboard map (design section 9). Keys with a fixed meaning (Esc, Enter, arrows, Tab, brackets) are handled here; every
// other binding comes from the command table. Returns true when the key did something.
bool Game::handleKey(SDL_Keycode k, bool ctrl, bool shift) {
    // the tool contract: Enter commits, Esc cancels; then Esc clears the selection, then closes panels
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) { if (lmb) { commitDrag(); return true; } return false; }
    if (k == SDLK_ESCAPE) {   // the drag in progress, then the topmost popup or dialog, then the selection
        if (cancelDrag()) return true;
        if (selOtherOpen || ctxOpen || matMenuOpen || paletteOpen || scaleOpen) { selOtherOpen = ctxOpen = matMenuOpen = paletteOpen = scaleOpen = false; return true; }
        if (cheatOpen || scenesOpen || fileOpen || newConfirm) { cheatOpen = scenesOpen = fileOpen = newConfirm = false; return true; }
        if (!sel.empty() || jointValid(selJoint)) { clearSelection(); return true; }
        if (dockFlyout) { dockFlyout = false; return true; }
        return true;
    }
    if (!ctrl && (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_UP || k == SDLK_DOWN)) { nudgeSelection(k); return true; }
    if (ctrl && (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_UP || k == SDLK_DOWN)) { nudgeSelection(k); return true; }
    if (k == SDLK_LEFTBRACKET) { brush = std::max(1, brush - 1); return true; }
    if (k == SDLK_RIGHTBRACKET) { brush = std::min(24, brush + 1); return true; }
    if (k == SDLK_BACKSPACE && !ctrl) { deleteSelection(); return true; }
    if (k == SDLK_HOME) { if (focusBody >= 0) focusBody = -1; setCam(0); return true; }
    if (k == SDLK_END) { if (focusBody >= 0) focusBody = -1; setCam((float)World::W); return true; }
    if (k == SDLK_PAGEUP) { if (focusBody >= 0) focusBody = -1; setCam(camXf - viewW() * 0.5f); return true; }
    if (k == SDLK_PAGEDOWN) { if (focusBody >= 0) focusBody = -1; setCam(camXf + viewW() * 0.5f); return true; }
    if (k == SDLK_KP_PLUS || (ctrl && k == SDLK_EQUALS)) { zoomCentre(1); return true; }
    if (k == SDLK_KP_MINUS || (ctrl && k == SDLK_MINUS)) { zoomCentre(-1); return true; }
    if (ctrl && k == SDLK_KP_0) { zoomReset(); return true; }
    if (k == SDLK_SPACE) return true;   // Space is handled on release (it also pans while held), see pollEvents
    if (k == SDLK_w && !ctrl && !shift && playing && !paused) return true;   // W fires rockets while the simulation runs; it picks the Wheel tool otherwise
    for (auto& c : commands)
        if (shortcutHit(c.shortcut, k, ctrl, shift)) {
            if (c.enabled && !c.enabled()) { notify(c.name + " is not available now"); return true; }
            c.run();
            return true;
        }
    return false;
}

// the active tool's mouse and key bindings, for the status bar
std::string Game::toolBindings() const {
    std::string s = toolInfo(tool).bindings;
    if (tool == T_MAT && mat == M_EMPTY) s = "LMB erase | [ ] size | RMB cancel";
    return s;
}
