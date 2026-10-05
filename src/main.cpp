// The entry point: the flags, the headless runner (--shot, --scene, --ui-gallery, --selftest), the event-driven editor tests and
// the interactive loop.
#include "app.hpp"

int runSelfTests();

namespace {

// Pushes real SDL events through the normal event path, with every coordinate computed from the layout.
struct Driver {
    Game& g;
    explicit Driver(Game& game) : g(game) {}
    void push(SDL_Event e) { SDL_PushEvent(&e); g.handleEvents(); }
    int px(float x) { return g.scrX(x); }
    int py(float y) { return g.scrY(y); }
    void moveWin(int wx, int wy) { SDL_Event e{}; e.type = SDL_MOUSEMOTION; e.motion.x = wx; e.motion.y = wy; push(e); }
    void downWin(int wx, int wy, Uint8 button = SDL_BUTTON_LEFT) {
        moveWin(wx, wy);
        SDL_Event e{}; e.type = SDL_MOUSEBUTTONDOWN; e.button.button = button; e.button.clicks = 1; e.button.x = wx; e.button.y = wy; push(e);
    }
    void upWin(int wx, int wy, Uint8 button = SDL_BUTTON_LEFT) { SDL_Event e{}; e.type = SDL_MOUSEBUTTONUP; e.button.button = button; e.button.x = wx; e.button.y = wy; push(e); }
    void clickWin(int wx, int wy, Uint8 button = SDL_BUTTON_LEFT) { downWin(wx, wy, button); upWin(wx, wy, button); }
    void mouseTo(float x, float y) { moveWin(px(x), py(y)); }
    void down(float x, float y) { downWin(px(x), py(y)); }
    void up(float x, float y) { upWin(px(x), py(y)); }
    void click(float x, float y) { down(x, y); up(x, y); }
    void drag(float x0, float y0, float x1, float y1) { down(x0, y0); mouseTo((x0 + x1) / 2, (y0 + y1) / 2); g.update(); mouseTo(x1, y1); g.update(); up(x1, y1); }
    void key(SDL_Keycode k, Uint16 mod = 0) {
        SDL_SetModState((SDL_Keymod)mod);
        SDL_Event e{}; e.type = SDL_KEYDOWN; e.key.keysym.sym = k; e.key.keysym.mod = mod; push(e);
        e = SDL_Event{}; e.type = SDL_KEYUP; e.key.keysym.sym = k; e.key.keysym.mod = mod; push(e);
        SDL_SetModState(KMOD_NONE);
    }
    void type(const std::string& s) {   // typed text arrives as a key press, the text input and the key release, as from a keyboard
        for (char c : s) {
            SDL_Event e{}; e.type = SDL_KEYDOWN; e.key.keysym.sym = (SDL_Keycode)(unsigned char)c; SDL_PushEvent(&e);
            e = SDL_Event{}; e.type = SDL_TEXTINPUT; e.text.text[0] = c; e.text.text[1] = 0; SDL_PushEvent(&e);
            e = SDL_Event{}; e.type = SDL_KEYUP; e.key.keysym.sym = (SDL_Keycode)(unsigned char)c; push(e);
        }
    }
    void frames(int n) { for (int i = 0; i < n; ++i) g.handleEvents(); }
    int alive() { int n = 0; for (auto& b : g.phys.bodies) n += b.alive; return n; }
};

const char* yn(bool b) { return b ? "yes" : "NO"; }
// positions that come from the mouse are quantised to pixels (about 0.3 cells at the default window), so they get that tolerance
bool near(float a, float b, float tol = 0.3f) { return std::fabs(a - b) <= tol; }
bool nearV(Vec2 a, Vec2 b) { return near(a.x, b.x) && near(a.y, b.y); }
bool overlaps(const SDL_Rect& a, const SDL_Rect& b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

// The event-driven editor test: builds, selects, moves, edits and undoes through the real event path and prints one yes / NO per behaviour.
void editorTest(Game& g) {
    Driver d(g);
    g.resetWorld();
    g.undoStack.clear(); g.undoLabels.clear();
    g.frame();
    auto covered = [&](float x, float y) { for (auto& b : g.phys.bodies) if (b.alive && b.contains(Vec2(x, y))) return true; return false; };
    // ---- the layout: a function of the window size, nothing overlapping, the dock collapsing below 1200 px
    {
        bool ok = true;
        for (int w : {1024, 1200, 1600, 1920})
            for (int h : {640, 900, 1080}) {
                Layout L = computeLayout(w, h, false);
                SDL_Rect zones[] = {L.top, L.strip, L.ctx, L.canvas, L.dock, L.status};
                for (int i = 0; i < 6 && ok; ++i) for (int j = i + 1; j < 6; ++j) if (overlaps(zones[i], zones[j])) ok = false;
                for (auto& z : zones) if (z.x < 0 || z.y < 0 || z.x + z.w > w || z.y + z.h > h) ok = false;
                if (L.canvas.x + L.canvas.w + L.dock.w != w || L.canvas.y + L.canvas.h != L.status.y) ok = false;
                if (L.dockCollapsed != (w < 1200)) ok = false;
                if (L.top.h != 40 || L.ctx.h != 32 || L.status.h != 28 || (L.strip.w != 96 && L.strip.w != 108)) ok = false;
                Layout C = computeLayout(w, h, true);
                if (C.strip.w >= L.strip.w || C.canvas.w <= L.canvas.w) ok = false;
            }
        std::printf("layout from the window size: zones tile the window without overlap at 12 sizes, dock collapses below 1200: %s\n", yn(ok));
    }
    // ---- drawing, selecting, moving, undo
    g.setTool(T_BOX);
    d.drag(100, 100, 160, 130);                       // a box 60 x 30
    g.setTool(T_CIRCLE);
    d.drag(130, 115, 130, 125);                       // a circle of radius 10 on top of it
    std::printf("drew a box and a circle through mouse events: %d bodies: %s\n", d.alive(), yn(d.alive() == 2));
    g.setTool(T_SELECT);
    d.click(130, 115);
    int first = g.primary;
    d.click(130, 115);
    int second = g.primary;
    std::printf("click on the overlap twice selects two different bodies: %s (%d then %d)\n", yn(first != second && first >= 0 && second >= 0), first, second);
    Vec2 before = g.phys.bodies[g.primary].pos;
    d.drag(130, 115, 190, 135);                       // drag the selected body
    Vec2 after = g.phys.bodies[g.primary].pos;
    std::printf("dragging the selection moved it by (%.0f, %.0f) (expected 60, 20): %s\n", after.x - before.x, after.y - before.y, yn(nearV(after - before, Vec2(60, 20))));
    d.key(SDLK_z, KMOD_CTRL);
    std::printf("ctrl+z undoes the move: back at (%.0f, %.0f): %s\n", g.phys.bodies[g.primary].pos.x, g.phys.bodies[g.primary].pos.y, yn(nearV(g.phys.bodies[g.primary].pos, before)));
    d.key(SDLK_y, KMOD_CTRL);
    std::printf("ctrl+y redoes it: (%.0f, %.0f): %s\n", g.phys.bodies[g.primary].pos.x, g.phys.bodies[g.primary].pos.y, yn(nearV(g.phys.bodies[g.primary].pos, after)));
    int n0 = d.alive();
    d.key(SDLK_c, KMOD_CTRL);
    d.mouseTo(300, 150);
    d.key(SDLK_v, KMOD_CTRL);
    std::printf("ctrl+c / ctrl+v: %d -> %d bodies, pasted body at (%.0f, %.0f): %s\n", n0, d.alive(), g.phys.bodies[g.primary].pos.x, g.phys.bodies[g.primary].pos.y,
                yn(d.alive() == n0 + 1 && nearV(g.phys.bodies[g.primary].pos, Vec2(300, 150))));
    d.key(SDLK_z, KMOD_CTRL);
    std::printf("ctrl+z removes the paste: %d bodies: %s\n", d.alive(), yn(d.alive() == n0));
    // ---- the cut tool, undo and redo
    g.clearSelection();
    g.setTool(T_BOX);
    d.drag(200, 60, 260, 90);                         // a fresh box to cut: x 200..260, y 60..90
    g.cutCircle = true;
    g.setTool(T_CUT);
    d.drag(230, 75, 230, 67);                         // cut a radius-8 circle out of its middle
    int cutPieces = 0;
    for (auto& b : g.phys.bodies) if (b.alive && b.group >= 0) ++cutPieces;
    std::printf("cut tool: the box became %d welded pieces, the circle's centre is empty and just outside it is solid: %s\n", cutPieces,
                yn(cutPieces > 1 && !covered(230, 75) && covered(230, 63) && covered(242, 75)));
    d.key(SDLK_z, KMOD_CTRL);
    std::printf("ctrl+z restores the box: %s\n", yn(covered(230, 75)));
    d.key(SDLK_y, KMOD_CTRL);
    std::printf("ctrl+y cuts it again: %s\n", yn(!covered(230, 75)));
    // ---- subtract with two selected bodies (the circle laid on a box), cutter removed by default
    g.clearSelection();
    g.setTool(T_BOX); d.drag(280, 60, 340, 90);
    g.setTool(T_CIRCLE); d.drag(310, 75, 310, 67);
    g.setTool(T_SELECT);
    d.click(295, 65);                                 // the box...
    d.downWin(d.px(310), d.py(75)); SDL_SetModState(KMOD_SHIFT); d.upWin(d.px(310), d.py(75)); SDL_SetModState(KMOD_NONE);   // ...Shift+click adds the circle, which becomes the cutter
    bool two = g.sel.size() == 2 && g.primary >= 0 && g.phys.bodies[g.primary].shape == SHAPE_CIRCLE;
    g.runCommand("edit.subtract");
    std::printf("shift+click adds to the selection (%s), subtract cuts the red circle out of the box: %s\n", yn(two), yn(!covered(310, 75) && covered(285, 65)));
    // ---- box select both ways, duplicate, flip, undo
    g.clearSelection();
    g.setTool(T_BOX); d.drag(40, 160, 80, 180); d.drag(90, 160, 130, 190);
    g.setTool(T_SELECT);
    d.drag(30, 150, 100, 200);                        // left to right: only the box wholly inside
    size_t enclosed = g.sel.size();
    d.drag(100, 200, 30, 150);                        // right to left: everything touched
    size_t crossing = g.sel.size();
    std::printf("box select: left to right encloses %zu body, right to left crosses %zu: %s\n", enclosed, crossing, yn(enclosed == 1 && crossing == 2));
    d.drag(30, 150, 140, 200);                        // both, enclosed
    std::vector<int> pair = g.sel;
    int n1 = d.alive();
    auto bboxC = [&](const std::vector<int>& ids) {   // the centre of the pos +- bound box, as the clipboard measures it
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (int id : ids) {
            const Body& b = g.phys.bodies[id];
            x0 = std::min(x0, b.pos.x - b.bound); x1 = std::max(x1, b.pos.x + b.bound);
            y0 = std::min(y0, b.pos.y - b.bound); y1 = std::max(y1, b.pos.y + b.bound);
        }
        return Vec2((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
    };
    d.mouseTo(250, 200);
    d.key(SDLK_d, KMOD_CTRL);
    bool fresh = g.sel.size() == 2;
    for (int id : g.sel) if (std::find(pair.begin(), pair.end(), id) != pair.end()) fresh = false;
    Vec2 dupC = bboxC(g.sel);
    std::printf("ctrl+d: %d -> %d bodies, the duplicates are the new selection, placed at the pointer (%.0f, %.0f): %s\n", n1, d.alive(), dupC.x, dupC.y,
                yn(d.alive() == n1 + 2 && pair.size() == 2 && fresh && length(dupC - Vec2(250, 200)) < 0.5f));
    std::vector<int> dup = g.sel;
    std::vector<Vec2> p0;
    float fx0 = 1e9f, fx1 = -1e9f;
    for (int id : dup) { const Body& b = g.phys.bodies[id]; p0.push_back(b.pos); fx0 = std::min(fx0, b.pos.x - b.half.x); fx1 = std::max(fx1, b.pos.x + b.half.x); }
    float cx = (fx0 + fx1) * 0.5f;
    d.key(SDLK_h, KMOD_CTRL);
    bool mirrored = dup.size() == 2;
    for (size_t i = 0; i < dup.size(); ++i) {
        const Vec2& q = g.phys.bodies[dup[i]].pos;
        mirrored = mirrored && std::fabs(q.x - (2.f * cx - p0[i].x)) < 1e-3f && std::fabs(q.y - p0[i].y) < 1e-3f;
    }
    std::printf("ctrl+h mirrors the x positions about the selection centre %.1f: %s\n", cx, yn(mirrored));
    d.key(SDLK_z, KMOD_CTRL);
    bool back = true;
    for (size_t i = 0; i < dup.size(); ++i) back = back && length(g.phys.bodies[dup[i]].pos - p0[i]) < 1e-3f;
    d.key(SDLK_z, KMOD_CTRL);
    std::printf("ctrl+z puts the duplicates back, another removes them (%d bodies): %s\n", d.alive(), yn(back && d.alive() == n1));
    // ---- the measure tool touches neither the world nor the undo stack
    size_t undoN = g.undoStack.size();
    g.setTool(T_MEASURE);
    d.drag(160, 210, 220, 180);
    std::printf("measure tool: (%.0f, %.0f) -> (%.0f, %.0f) length %.1f, shown, bodies and undo entries unchanged: %s\n",
                g.measA.x, g.measA.y, g.measB.x, g.measB.y, length(g.measB - g.measA), yn(g.measOn && near(length(g.measB - g.measA), 67.08f, 0.1f) && d.alive() == n1 && g.undoStack.size() == undoN));
    // ---- handles: a fresh 60 x 30 box, its right edge dragged, a corner with Ctrl, the rotation handle plain and with Shift
    g.clearSelection(); g.snapOn = false; g.smartSnap = false;
    g.setTool(T_BOX);
    d.drag(100, 160, 160, 190);                       // x 100..160, y 160..190, centre (130, 175)
    g.setTool(T_SELECT);
    d.click(130, 175);
    const int hb = g.primary;
    auto body = [&]() -> const Body& { return g.phys.bodies[hb]; };
    auto hpos = [&](int h) { return g.handlePos(g.phys.bodies[hb], h); };
    std::printf("a fresh 60 x 30 box is selected and shows handles: %s\n", yn(hb >= 0 && g.handleBody() == hb && g.handleResizes()));
    Vec2 rh = hpos(3);                                // the right edge midpoint
    d.drag(rh.x, rh.y, rh.x + 10, rh.y);
    std::printf("right edge handle dragged 10 cells right: width %.2f (expected 70), left edge still at %.2f: %s\n", body().half.x * 2, body().pos.x - body().half.x,
                yn(near(body().half.x * 2, 70) && near(body().pos.x - body().half.x, 100)));
    Vec2 c0 = body().pos, br = hpos(4);               // the bottom-right corner
    SDL_SetModState(KMOD_CTRL);                       // Ctrl resizes about the centre and, with the grid off, turns snapping on
    d.drag(br.x, br.y, br.x + 4, br.y + 6);
    SDL_SetModState(KMOD_NONE);
    std::printf("ctrl + corner handle dragged (4, 6): %.0f x %.0f (expected 80 x 40: about the centre, on the inverted 5-cell grid), centre kept: %s\n",
                body().half.x * 2, body().half.y * 2, yn(nearV(body().pos, c0) && near(body().half.x * 2, 80) && near(body().half.y * 2, 40)));
    Vec2 rot = hpos(Game::H_ROT);
    float arm = length(rot - body().pos);
    d.drag(rot.x, rot.y, body().pos.x + arm, body().pos.y);   // from straight above the centre to straight right of it
    float deg = body().angle * 180.f / PI;
    std::printf("rotation handle dragged a quarter turn: angle %.1f (expected 90), centre kept: %s\n", deg, yn(near(deg, 90, 0.5f) && nearV(body().pos, c0)));
    d.key(SDLK_z, KMOD_CTRL);
    rot = hpos(Game::H_ROT);
    SDL_SetModState(KMOD_SHIFT);
    d.drag(rot.x, rot.y, body().pos.x + arm * std::cos(-53.f * PI / 180.f), body().pos.y + arm * std::sin(-53.f * PI / 180.f));   // 37 degrees round
    SDL_SetModState(KMOD_NONE);
    deg = body().angle * 180.f / PI;
    std::printf("shift + rotation handle swept 37 degrees: angle %.2f, snapped to a multiple of 15: %s\n", deg, yn(near(deg, 30, 0.01f)));
    d.key(SDLK_z, KMOD_CTRL); d.key(SDLK_z, KMOD_CTRL); d.key(SDLK_z, KMOD_CTRL);
    bool restored = near(body().half.x * 2, 60) && near(body().half.y * 2, 30) && nearV(body().pos, Vec2(130, 175)) && near(body().angle, 0);
    std::printf("three undos restore the box: %.0f x %.0f at (%.0f, %.0f), angle %.0f: %s\n", body().half.x * 2, body().half.y * 2, body().pos.x, body().pos.y, body().angle * 180.f / PI, yn(restored));
    // ---- Esc cancels a handle drag: the body and the undo stack are as before
    undoN = g.undoStack.size();
    const float w0 = body().half.x * 2;
    rh = hpos(3);
    d.down(rh.x, rh.y); d.mouseTo(rh.x + 12, rh.y); g.update();
    bool midDrag = near(body().half.x * 2, w0 + 12);
    d.key(SDLK_ESCAPE);
    d.up(rh.x + 12, rh.y);
    std::printf("esc during a handle drag (width was %s 12 wider) puts the width back to %.0f and leaves the undo stack at %zu entries: %s\n", midDrag ? "mid-way" : "NOT", body().half.x * 2,
                g.undoStack.size(), yn(midDrag && near(body().half.x * 2, w0) && g.undoStack.size() == undoN));
    // ---- grid snap: the toggle in the context bar (the same command the button runs), a snapped move and a snapped edge
    g.runCommand("view.snap"); g.snapStep = 5;
    std::printf("snap toggle: grid snap on with a 5-cell step: %s\n", yn(g.snapOn && g.snapStep == 5));
    d.drag(130, 175, 137, 178);
    std::printf("move drag by (7, 3) with a 5-cell grid: the body lands at (%.0f, %.0f) (expected 135, 180): %s\n", body().pos.x, body().pos.y, yn(nearV(body().pos, Vec2(135, 180))));
    rh = hpos(3);
    d.drag(rh.x, rh.y, rh.x + 7.3f, rh.y);
    std::printf("right edge dragged 7.3 cells with the grid on: edge at %.1f (expected 170): %s\n", body().pos.x + body().half.x, yn(near(body().pos.x + body().half.x, 170)));
    Vec2 q0 = body().pos;
    SDL_SetModState(KMOD_CTRL);                       // Ctrl inverts the snap for the drag
    d.drag(q0.x, q0.y, q0.x + 3, q0.y + 1);
    SDL_SetModState(KMOD_NONE);
    std::printf("ctrl inverts the snap: a (3, 1) drag with the grid on moves the body by (%.1f, %.1f), off the grid: %s\n", body().pos.x - q0.x, body().pos.y - q0.y,
                yn(near(body().pos.x - q0.x, 3, 0.35f) && near(body().pos.y - q0.y, 1, 0.35f)));
    g.runCommand("view.snap");
    // ---- smart snap: a body dragged near another's left edge sticks to it and names the guide
    g.smartSnap = true;
    g.clearSelection();
    g.setTool(T_BOX); d.drag(300, 200, 340, 230);    // a target whose left edge is at x = 300, clear of everything else
    g.setTool(T_SELECT);
    d.click(140, 180);                                // the handled box
    Vec2 lp = body().pos;
    const float toLeft = 299.5f - (lp.x - body().half.x);
    d.down(lp.x, lp.y); d.mouseTo(lp.x + toLeft * 0.5f, lp.y); g.update(); d.mouseTo(lp.x + toLeft, lp.y); g.update();   // its left edge comes to 299.5, within 6 px of 300
    bool guided = g.guide.on && g.guide.vertical && near(g.guide.coord, 300, 0.01f) && g.guide.name.find("left") == 0;
    std::string guideName = g.guide.name;
    d.up(lp.x + toLeft, lp.y);
    std::printf("smart snap: the dragged box's left edge snapped to %.1f (expected 300) with the guide \"%s\": %s\n", body().pos.x - body().half.x, guideName.c_str(),
                yn(guided && near(body().pos.x - body().half.x, 300, 0.01f)));
    // ---- zoom to selection and back
    d.key(SDLK_f, KMOD_SHIFT);
    Vec2 centre(g.camXf + g.viewW() * 0.5f, g.camYf + g.viewH() * 0.5f);
    std::printf("shift+f zooms to the selection: %.0fx, view centred on the body: %s\n", g.zoom, yn(g.zoom > 1.5f && nearV(centre, body().pos)));
    g.clearSelection();
    d.key(SDLK_f, KMOD_SHIFT);
    std::printf("shift+f with nothing selected resets the zoom: %.0fx: %s\n", g.zoom, yn(near(g.zoom, 1)));
    g.setCam(0, 0);
    // ---- the dimension field: a box drawn with typed width, height and angle
    g.setTool(T_BOX);
    int nb = d.alive();
    d.down(100, 40); d.mouseTo(130, 60); g.update();
    d.type("60"); d.key(SDLK_TAB); d.type("30"); d.key(SDLK_TAB); d.type("10+5");
    bool fieldShown = g.dim.active && g.dim.n == 3 && g.dim.typed[0] && g.dim.typed[1] && g.dim.cur == 2;
    d.key(SDLK_RETURN);
    const Body* made = nullptr;
    for (auto& b : g.phys.bodies) if (b.alive) made = &b;
    std::printf("dimension field: typed 60 Tab 30 Tab 10+5 then Enter makes a %.0f x %.0f box at %.0f degrees (expected 60 x 30 at 15): %s\n",
                made ? made->half.x * 2 : 0.f, made ? made->half.y * 2 : 0.f, made ? made->angle * 180.f / PI : 0.f,
                yn(fieldShown && d.alive() == nb + 1 && made && near(made->half.x * 2, 60) && near(made->half.y * 2, 30) && near(made->angle * 180.f / PI, 15, 0.01f)));
    d.up(130, 60);
    // ---- the inspector: a field edit through the dock changes the body and pushes one undo entry
    g.setTool(T_SELECT);
    g.selectBody(hb, false, false);
    g.dockTab = 0;
    d.frames(2);
    SDL_Rect fx = g.fieldRects.count("X") ? g.fieldRects["X"] : SDL_Rect{0, 0, 0, 0};
    undoN = g.undoStack.size();
    float xBefore = body().pos.x;
    d.clickWin(fx.x + fx.w * 3 / 4, fx.y + fx.h / 2);   // the right part of the row is the number itself
    d.type("250"); d.key(SDLK_RETURN);
    d.frames(1);
    std::printf("inspector: the X field (found: %s) typed 250 moves the body from %.0f to %.0f and adds one undo entry (%zu -> %zu): %s\n", yn(fx.w > 0), xBefore, body().pos.x, undoN,
                g.undoStack.size(), yn(fx.w > 0 && near(body().pos.x, 250) && g.undoStack.size() == undoN + 1));
    d.key(SDLK_z, KMOD_CTRL);
    std::printf("ctrl+z undoes the inspector edit: x back to %.0f: %s\n", body().pos.x, yn(near(body().pos.x, xBefore)));
    // ---- the dock tabs: a click on the second tab shows Materials
    {
        SDL_Rect t = g.L.dockTabs;
        d.clickWin(t.x + t.w * 3 / 8, t.y + t.h / 2);
        bool mats = g.dockTab == 1;
        d.clickWin(t.x + t.w * 7 / 8, t.y + t.h / 2);
        std::printf("dock tabs: clicking the second tab shows Materials, the fourth History: %s\n", yn(mats && g.dockTab == 3));
        g.dockTab = 0;
    }
    // ---- the command palette: Ctrl+K, type, Enter runs the command
    bool heat0 = g.heatView;
    d.key(SDLK_k, KMOD_CTRL);
    bool opened = g.paletteOpen;
    d.type("heat view"); d.frames(1);
    d.key(SDLK_RETURN); d.frames(1);
    std::printf("palette: ctrl+k opens it (%s), typing \"heat view\" and Enter toggles the heat view (%s -> %s) and closes it: %s\n", yn(opened), yn(heat0), yn(g.heatView),
                yn(opened && g.heatView != heat0 && !g.paletteOpen));
    g.heatView = heat0;
    // ---- the context menu: right click on a body selects it and opens the menu; Esc closes it; on empty canvas it offers paste
    g.clearSelection();
    d.clickWin(d.px(body().pos.x), d.py(body().pos.y), SDL_BUTTON_RIGHT);
    bool onBody = g.ctxOpen && !g.ctxCanvas && g.primary == hb;
    d.key(SDLK_ESCAPE);
    bool closed = !g.ctxOpen;
    d.clickWin(d.px(20), d.py(20), SDL_BUTTON_RIGHT);
    bool onCanvas = g.ctxOpen && g.ctxCanvas;
    d.key(SDLK_ESCAPE);
    std::printf("context menu: right click on a body selects it and opens the body menu (%s), Esc closes it (%s), right click on empty canvas opens the canvas menu (%s): %s\n",
                yn(onBody), yn(closed), yn(onCanvas), yn(onBody && closed && onCanvas && !g.ctxOpen));
    // ---- select other: click and hold for 400 ms on stacked bodies lists them; the backtick key does the same
    g.clearSelection();
    g.setTool(T_BOX); d.drag(200, 160, 260, 190);
    g.setTool(T_CIRCLE); d.drag(230, 175, 230, 185);
    g.setTool(T_SELECT); g.clearSelection();
    d.down(230, 175);
    SDL_Delay(430);
    d.frames(1);
    bool held = g.selOtherOpen && g.selOther.size() == 2;
    size_t listed = g.selOther.size();
    d.up(230, 175);
    d.key(SDLK_ESCAPE);
    d.mouseTo(230, 175);
    d.key(SDLK_BACKQUOTE);
    bool tick = g.selOtherOpen && g.selOther.size() == 2;
    d.key(SDLK_ESCAPE);
    std::printf("select other: click-hold lists %zu things under the pointer (%s), backtick does too (%s): %s\n", listed, yn(held), yn(tick), yn(held && tick && !g.selOtherOpen));
    // ---- Esc with nothing in progress clears the selection; right click cancels a shape drag without an undo entry
    g.selectBody(hb, false, false);
    d.key(SDLK_ESCAPE);
    bool cleared = g.sel.empty();
    undoN = g.undoStack.size(); nb = d.alive();
    g.setTool(T_BOX);
    d.down(300, 120); d.mouseTo(340, 140); g.update();
    d.clickWin(d.px(340), d.py(140), SDL_BUTTON_RIGHT);
    d.up(340, 140);
    std::printf("esc clears the selection (%s); right click cancels a box drag: no body made, undo stack unchanged, no menu: %s\n", yn(cleared),
                yn(d.alive() == nb && g.undoStack.size() == undoN && !g.ctxOpen && !g.lmb));
    // ---- the transport: Space plays, Space pauses, Shift+Space stops and restores
    g.setTool(T_SELECT);
    d.key(SDLK_SPACE);
    bool running = g.playing && !g.paused;
    d.key(SDLK_SPACE);
    bool pausedNow = g.playing && g.paused;
    d.key(SDLK_SPACE, KMOD_SHIFT);
    std::printf("transport keys: Space plays (%s), Space pauses (%s), Shift+Space stops (%s): %s\n", yn(running), yn(pausedNow), yn(!g.playing), yn(running && pausedNow && !g.playing));
    g.setCam(0, 0);
    // ---- cancelling a second paint stroke (merged into the first stroke's undo entry) keeps the first stroke
    g.pickTool(T_MAT); g.mat = M_SAND; g.brush = 2;
    auto cellsOf = [&](uint8_t m) { int n = 0; for (auto& c : g.world.cells) n += c.t == m; return n; };
    undoN = g.undoStack.size();
    d.drag(20, 20, 60, 20);
    const int sand1 = cellsOf(M_SAND);
    d.down(20, 60); d.mouseTo(60, 60); g.update();
    const bool painting2 = cellsOf(M_SAND) > sand1 && g.undoStack.size() == undoN + 1;
    d.key(SDLK_ESCAPE); d.up(60, 60);
    std::printf("esc on a second paint stroke within the merge window (%s) keeps the first stroke's %d cells (%d now) and its undo entry (%zu): %s\n", yn(painting2), sand1, cellsOf(M_SAND),
                g.undoStack.size(), yn(painting2 && cellsOf(M_SAND) == sand1 && g.undoStack.size() == undoN + 1));
    // ---- ctrl+z mid-drag cancels the drag instead of changing the world under it
    g.setTool(T_SELECT); g.snapOn = false; g.smartSnap = false;
    g.selectBody(hb, false, false);
    Vec2 m0 = body().pos; undoN = g.undoStack.size();
    d.down(m0.x, m0.y); d.mouseTo(m0.x + 20, m0.y); g.update();
    const bool midMove = near(body().pos.x, m0.x + 20);
    d.key(SDLK_z, KMOD_CTRL);
    d.up(m0.x + 20, m0.y);
    std::printf("ctrl+z during a move drag (%s mid-way) puts the body back at x=%.0f and leaves the undo stack at %zu: %s\n", midMove ? "was" : "was NOT", body().pos.x, g.undoStack.size(),
                yn(midMove && nearV(body().pos, m0) && g.undoStack.size() == undoN));
    // ---- loading a scene while paused keeps the drawing: one undo brings it back
    const int drawn = d.alive();
    g.play(); g.togglePause();
    g.runCommand("scene.0");
    const bool replaced = !g.playing && d.alive() != drawn;
    d.key(SDLK_z, KMOD_CTRL);
    std::printf("a scene loaded while paused replaces the drawing (%s) and ctrl+z brings the %d bodies back (%d): %s\n", yn(replaced), drawn, d.alive(), yn(replaced && d.alive() == drawn));
    // ---- a typed width is clamped to what the inspector accepts
    g.setTool(T_BOX); g.clearSelection();
    d.down(100, 40); d.mouseTo(130, 60); g.update();
    d.type("99999"); d.key(SDLK_TAB); d.type("30"); d.key(SDLK_RETURN); d.up(130, 60);
    float wTyped = g.primary >= 0 ? g.phys.bodies[g.primary].half.x * 2 : 0.f;
    std::printf("dimension field: a typed width of 99999 makes a box %.0f wide (clamped to 600): %s\n", wTyped, yn(near(wTyped, 600)));
    d.key(SDLK_z, KMOD_CTRL);
    // ---- space released while a shape is being dragged does not start the simulation
    d.down(200, 40); d.mouseTo(240, 60); g.update();
    d.key(SDLK_SPACE);
    const bool stillEditing = !g.playing && g.lmb;
    d.key(SDLK_ESCAPE); d.up(240, 60);
    std::printf("space released mid-drag leaves the simulation stopped and the drag alive: %s\n", yn(stillEditing && !g.playing));
    // ---- a gravity flip is an undo step like any other edit
    undoN = g.undoStack.size();
    g.runCommand("sim.gravity");
    const bool up = g.phys.gravity.y < 0 && g.undoStack.size() == undoN + 1;
    d.key(SDLK_z, KMOD_CTRL);
    std::printf("flipping gravity pushes one undo entry (%s) and ctrl+z flips it back: %s\n", yn(up), yn(up && g.phys.gravity.y > 0));
    g.setTool(T_SELECT);
    d.click(body().pos.x, body().pos.y);
}

// --ui-gallery: the main interface states, one under the other in a tall image (and each as its own file beside it).
void gallery(Game& g, const char* out) {
    struct State { std::string name; std::function<void()> setup; };
    auto base = [&] {
        g.cheatOpen = g.scenesOpen = g.fileOpen = g.paletteOpen = g.ctxOpen = g.matMenuOpen = g.selOtherOpen = g.newConfirm = g.scaleOpen = false;
        g.dockTab = 0; g.dockFlyout = g.L.dockCollapsed;   // a narrow window shows the dock as a flyout
        g.clearSelection();
        g.setTool(T_SELECT);
        g.in.mx = g.L.canvas.x + g.L.canvas.w / 2; g.in.my = g.L.canvas.y + g.L.canvas.h / 2;
    };
    auto body = [&](std::function<bool(const Body&)> pred) { for (auto& b : g.phys.bodies) if (b.alive && pred(b)) return b.id; return -1; };
    std::vector<State> states = {
        {"default, a body selected", [&] { base(); g.selectBody(body([](const Body& b) { return b.shape == SHAPE_BOX && !b.isStatic && b.fan.strength == 0.f && !b.src.on; }), false, false); }},
        {"Paint tool", [&] { base(); g.pickTool(T_MAT); g.mat = M_WATER; }},
        {"Erase tool", [&] { base(); g.setTool(T_MAT); g.mat = M_EMPTY; }},
        {"Box tool", [&] { base(); g.setTool(T_BOX); }},
        {"Pipe tool", [&] { base(); g.setTool(T_PIPE); }},
        {"Fan tool", [&] { base(); g.setTool(T_FAN); }},
        {"Emitter tool", [&] { base(); g.setTool(T_EMITTER); }},
        {"Spring tool", [&] { base(); g.setTool(T_SPRING); }},
        {"Bond tool", [&] { base(); g.setTool(T_BOND); }},
        {"Cut tool", [&] { base(); g.setTool(T_CUT); }},
        {"Measure tool", [&] { base(); g.setTool(T_MEASURE); g.measOn = true; g.measA = Vec2(100, 100); g.measB = Vec2(160, 130); }},
        {"Inspector: a joint", [&] { base(); for (auto& j : g.phys.joints) if (g.jointValid(j.id) && j.type == J_DISTANCE && j.freq > 0.f) { g.selectJoint(j.id); break; } }},
        {"Inspector: a fan", [&] { base(); g.selectBody(body([](const Body& b) { return b.fan.strength != 0.f; }), false, false); }},
        {"Inspector: an emitter", [&] { base(); g.selectBody(body([](const Body& b) { return b.src.on; }), false, false); }},
        {"Inspector: several bodies", [&] { base(); g.selectAll(); }},
        {"Inspector: nothing selected", [&] { base(); }},
        {"Materials tab", [&] { base(); g.dockTab = 1; g.pickTool(T_MAT); }},
        {"Scene tab", [&] { base(); g.dockTab = 2; }},
        {"History tab", [&] { base(); g.dockTab = 3; }},
        {"Command palette", [&] { base(); g.paletteOpen = true; g.paletteQuery = "zoom"; }},
        {"Context menu on a body", [&] { base(); int id = body([](const Body& b) { return !b.isStatic; }); if (id >= 0) { g.selectBody(id, false, false); g.openContextMenu(g.scrX(g.phys.bodies[id].pos.x), g.scrY(g.phys.bodies[id].pos.y)); } }},
        {"Scene browser", [&] { base(); g.scenesOpen = true; g.sceneSel = 1; }},
        {"File dialog", [&] { base(); g.openFileDialog(true); g.fileName = "my-engine"; }},
        {"Cheat sheet", [&] { base(); g.cheatOpen = true; }},
        {"A toast", [&] { base(); g.notify("Saved saves/my-engine.sbot"); }},
        {"Running, paused", [&] { base(); g.play(); for (int i = 0; i < 30; ++i) g.update(); g.togglePause(); }},
    };
    g.buildFanTest();
    { int e = g.phys.addBox(Vec2(300, 60), Vec2(4, 4), 0, M_STEEL, true); g.phys.bodies[e].src = Emitter{true, M_WATER, 60.f, 0.f, 3}; }
    { int a = g.phys.addBox(Vec2(360, 60), Vec2(6, 6), 0, M_STEEL, true), b = g.phys.addBox(Vec2(360, 110), Vec2(6, 6), 0, M_STEEL, false); g.phys.addDistance(a, Vec2(360, 60), b, Vec2(360, 110), 2.5f); }
    g.phys.stampBodies();
    g.frame();
    const int w = g.L.winW, h = g.L.winH;
    SDL_Surface* tall = SDL_CreateRGBSurfaceWithFormat(0, w, h * (int)states.size(), 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Surface* one = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    std::string stem = out;
    if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".bmp") == 0) stem.resize(stem.size() - 4);
    for (size_t i = 0; i < states.size(); ++i) {
        states[i].setup();
        g.frame(); g.frame();
        const std::string& name = states[i].name;
        SDL_Rect tag{g.L.canvas.x + 8, g.L.canvas.y + 8, font::width(name, font::Face::Ui, 2) + 16, 28};
        SDL_SetRenderDrawColor(g.ren, 0, 0, 0, 180);
        SDL_RenderFillRect(g.ren, &tag);
        font::text(g.ren, name, tag.x + 8, tag.y + 4, font::Face::Ui, 2, SDL_Color{255, 230, 120, 255});
        SDL_RenderReadPixels(g.ren, nullptr, SDL_PIXELFORMAT_ARGB8888, one->pixels, one->pitch);
        SDL_Rect dst{0, (int)i * h, w, h};
        SDL_BlitSurface(one, nullptr, tall, &dst);
        char file[512];
        std::snprintf(file, sizeof file, "%s-%02zu.bmp", stem.c_str(), i + 1);
        SDL_SaveBMP(one, file);
        std::printf("gallery %2zu: %s\n", i + 1, name.c_str());
    }
    SDL_SaveBMP(tall, out);
    SDL_FreeSurface(one);
    SDL_FreeSurface(tall);
    std::printf("gallery: %zu states, %d x %d, written to %s\n", states.size(), w, h * (int)states.size(), out);
}

// --ui-fuzz SEED FRAMES: drives the whole application with pseudo-random SDL events (the pointer over every zone and off the
// window, every button with every modifier, double clicks, the wheel, every key in the map, typed text, real window resizes,
// the transport, scene loads, undo storms, mass drawing and selection) and checks the invariants after every frame: finite
// and positive body sizes, materials in range, joints to live bodies, selection ids in range, the layout tiling the window,
// the undo stack consistent, and every 300 frames that undoing back to a checkpoint restores the state byte for byte and
// redoing restores it again. The events are a deterministic function of the seed; prints one line per violation.
int fuzz(Game& g, uint32_t seed, int frames) {
    struct Rng {
        uint64_t s;
        uint32_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (uint32_t)(s >> 11); }
        int below(int n) { return n <= 0 ? 0 : (int)(next() % (uint32_t)n); }
        bool chance(int pct) { return below(100) < pct; }
        int range(int a, int b) { return a + below(b - a + 1); }
    } rng{0x9E3779B97F4A7C15ull ^ ((uint64_t)seed + 1) * 0xD1B54A32D192ED03ull};
    int frame = 0, violations = 0, events = 0, worstMs = 0;
    auto bad = [&](const std::string& what) { if (violations < 60) std::printf("fuzz VIOLATION at frame %d: %s\n", frame, what.c_str()); ++violations; };
    auto push = [&](SDL_Event e) { SDL_PushEvent(&e); ++events; };
    bool held[3] = {false, false, false};   // left, right, middle
    const Uint8 buttons[3] = {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE};
    int mx = g.L.canvas.x + 100, my = g.L.canvas.y + 100, spaceUp = -1;
    std::vector<SDL_Keycode> keys = {SDLK_ESCAPE, SDLK_RETURN, SDLK_KP_ENTER, SDLK_TAB, SDLK_BACKSPACE, SDLK_DELETE, SDLK_SPACE, SDLK_LEFT, SDLK_RIGHT, SDLK_UP, SDLK_DOWN,
                                     SDLK_HOME, SDLK_END, SDLK_PAGEUP, SDLK_PAGEDOWN, SDLK_LEFTBRACKET, SDLK_RIGHTBRACKET, SDLK_BACKQUOTE, SDLK_MINUS, SDLK_EQUALS, SDLK_F1,
                                     SDLK_F2, SDLK_KP_PLUS, SDLK_KP_MINUS, SDLK_KP_0, SDLK_PERIOD, SDLK_COMMA, SDLK_SLASH, SDLK_BACKSLASH};
    for (int c = 'a'; c <= 'z'; ++c) keys.push_back((SDL_Keycode)c);
    for (int c = '0'; c <= '9'; ++c) keys.push_back((SDL_Keycode)c);
    static const char* queries[] = {"heat", "zoom", "scene", "paint water", "body material", "wipe", "new", "open", "save as", "undo", "select", "fan"};
    static const char* names[] = {"../x", "a/b.c", "", "my-engine", "0123456789012345678901234567890123", "  ", "x.sbot", "UPPER"};
    auto motion = [&](int x, int y) { mx = x; my = y; SDL_Event e{}; e.type = SDL_MOUSEMOTION; e.motion.x = x; e.motion.y = y; push(e); };
    auto button = [&](int i, bool down, int clicks = 1) {
        SDL_Event e{}; e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        e.button.button = buttons[i]; e.button.clicks = (Uint8)clicks; e.button.x = mx; e.button.y = my;
        push(e); held[i] = down;
    };
    auto key = [&](SDL_Keycode k, bool up = true) {
        SDL_Event e{}; e.type = SDL_KEYDOWN; e.key.keysym.sym = k; e.key.keysym.mod = (Uint16)SDL_GetModState(); push(e);
        if (up) { e.type = SDL_KEYUP; push(e); }
    };
    auto type = [&](const std::string& s) { for (char c : s) { SDL_Event e{}; e.type = SDL_TEXTINPUT; e.text.text[0] = c; e.text.text[1] = 0; push(e); } };
    auto inRect = [&](const SDL_Rect& r) { motion(r.x + rng.below(std::max(1, r.w)), r.y + rng.below(std::max(1, r.h))); };
    auto step = [&] {
        const Uint64 f = SDL_GetPerformanceFrequency(), t0 = SDL_GetPerformanceCounter();
        g.pollEvents(); g.update();
        const Uint64 t1 = SDL_GetPerformanceCounter();
        g.frame();
        const Uint64 t2 = SDL_GetPerformanceCounter();
        int ms = (int)((t2 - t0) * 1000 / f);
        worstMs = std::max(worstMs, ms);
        if (ms > 1000) {   // what the slow frame was doing, and whether the simulation step or the drawing took the time
            int cells = 0, gas = 0; for (auto& c : g.world.cells) { cells += c.t != M_EMPTY; gas += MATS[c.t].kind == K_GAS; }
            int joints = 0; for (auto& j : g.phys.joints) joints += j.alive;
            std::printf("fuzz frame %d took %d ms (update %d, frame %d): %s speed %.1f, %d bodies, %d joints, %d cells (%d gas), tool %s, %zu selected, zoom %.1f, %dx%d\n",
                        frame, ms, (int)((t1 - t0) * 1000 / f), (int)((t2 - t1) * 1000 / f), g.playing ? (g.paused ? "paused," : "running,") : "editing,", g.speed,
                        g.phys.bodyCount(), joints, cells, gas, toolInfo(g.tool).name, g.sel.size(), g.zoom, g.L.winW, g.L.winH);
        }
        if (ms > 8000) bad("a frame took " + std::to_string(ms) + " ms");
    };
    auto finite = [](std::initializer_list<float> xs) { for (float x : xs) if (!std::isfinite(x)) return false; return true; };
    auto check = [&] {
        const Layout& L = g.L;
        SDL_Rect zones[] = {L.top, L.strip, L.ctx, L.canvas, L.dock, L.status};
        for (int i = 0; i < 6; ++i) {
            const SDL_Rect& z = zones[i];
            if (z.w <= 0 || z.h <= 0 || z.x < 0 || z.y < 0 || z.x + z.w > L.winW || z.y + z.h > L.winH)
                bad("layout zone " + std::to_string(i) + " outside the window or empty");
            for (int j = i + 1; j < 6; ++j)
                if (overlaps(zones[i], zones[j])) bad("layout zones " + std::to_string(i) + " and " + std::to_string(j) + " overlap");
        }
        const int nb = (int)g.phys.bodies.size();
        for (auto& b : g.phys.bodies) {
            if (!b.alive) continue;
            if (!finite({b.pos.x, b.pos.y, b.angle, b.half.x, b.half.y, b.radius, b.mass})) bad("body " + std::to_string(b.id) + " has a NaN or infinite value");
            if (b.shape == SHAPE_BOX ? (b.half.x < 0.25f || b.half.y < 0.25f) : b.radius < 0.25f) bad("body " + std::to_string(b.id) + " has no size");
            if (b.mat >= M_COUNT || b.src.mat >= M_COUNT) bad("body " + std::to_string(b.id) + " has a material out of range");
        }
        for (auto& j : g.phys.joints) {
            if (!j.alive) continue;
            auto ok = [&](int id) { return id == -1 || (id >= 0 && id < nb && g.phys.bodies[id].alive); };
            if (!ok(j.a) || !ok(j.b)) bad("joint " + std::to_string(j.id) + " refers to a dead or missing body");
        }
        for (int id : g.sel) if (id < 0 || id >= nb) bad("selection holds the out-of-range id " + std::to_string(id));
        if (g.primary >= nb) bad("primary out of range");
        if (g.selJoint >= (int)g.phys.joints.size()) bad("selected joint out of range");
        if (g.handle >= 0 && (g.handleId < 0 || g.handleId >= nb || !g.phys.bodies[g.handleId].alive || !g.lmb))
            bad("a handle is being dragged on a dead body or with no button down");
        if (g.dim.active && !g.lmb) bad("the dimension field is active with no drag in progress");
        if (g.speed < 0.1f || g.speed > 2.f || g.brush < 1 || g.brush > 24 || g.selFilter < 0 || g.selFilter > 2 || g.emitFace > 4 || g.dockTab < 0 || g.dockTab > 3 ||
            g.bondType < 0 || g.bondType > 3 || g.sparkIdx < 0 || g.sparkIdx > 6 || g.mat >= M_COUNT || g.bodyMat >= M_COUNT || g.payload >= M_COUNT)
            bad("a setting is out of range");
        if (!finite({g.camXf, g.camYf, g.zoom}) || g.zoom < 1.f || g.zoom > 8.f) bad("camera out of range");
        if (g.undoStack.size() != g.undoLabels.size() || g.redoStack.size() != g.redoLabels.size() || g.undoStack.size() > 60) bad("undo stack and labels disagree");
        size_t bytes = 0; for (auto& u : g.undoStack) bytes += u.size();
        if (bytes != g.undoBytes) bad("undo byte count is stale");
    };
    // a canonical digest of the drawing in four parts: the cells, the live bodies with gravity, the live joints, the labels. Dead
    // slots in the body and joint vectors are allocator noise and the body-material default is a preference, so neither counts.
    using Parts = std::vector<std::vector<uint8_t>>;
    auto digest = [&](Parts& parts) {
        parts.assign(4, {});
        { Writer w{parts[0]}; g.world.save(w); }
        { Writer w{parts[1]}; w.pod(g.phys.gravity);
          for (auto& b : g.phys.bodies) {
              if (!b.alive) continue;
              w.pod(b.id); w.pod(b.pos); w.pod(b.angle); w.pod(b.half); w.pod(b.radius); w.pod(b.mat); w.pod(b.isStatic); w.pod(b.isWheel); w.pod(b.isRocket);
              w.pod(b.group); w.pod(b.fan.strength); w.pod(b.fan.vacuum); w.pod(b.src.on); w.pod(b.src.mat); w.pod(b.src.rate); w.pod(b.src.face); w.pod(b.temp);
          } }
        { Writer w{parts[2]};
          for (auto& j : g.phys.joints) {
              if (!j.alive || j.type == J_MOUSE) continue;
              w.pod(j.id); w.pod(j.type); w.pod(j.a); w.pod(j.b); w.pod(j.la); w.pod(j.lb); w.pod(j.length); w.pod(j.freq); w.pod(j.damping);
              w.pod(j.speed); w.pod(j.power); w.pod(j.keyed); w.pod(j.group); w.pod(j.bondId); w.pod(j.breakT); w.pod(j.loadG);
          } }
        { Writer w{parts[3]}; w.pod((uint32_t)g.labels.size()); for (auto& l : g.labels) { w.pod(l.p); w.str(l.s); } }
    };
    auto differs = [](const Parts& a, const Parts& b) {
        static const char* names[] = {"cells", "bodies", "joints", "labels"};
        std::string s;
        for (int i = 0; i < 4; ++i) if (a[i] != b[i]) s += std::string(s.empty() ? "" : ", ") + names[i];
        return s;
    };
    // the checkpoint also fingerprints the undo entries beneath it: if the stack was trimmed by its cap (a mass draw pushes more
    // than 60 entries) or the history branched (undo, then a new edit), undoing to that depth lands elsewhere and the check is skipped
    auto hashEntry = [](const std::vector<uint8_t>& v) { uint64_t h = 1469598103934665603ull; for (uint8_t b : v) { h ^= b; h *= 1099511628211ull; } return h ^ (v.size() * 31); };
    auto stackHashes = [&](size_t n) { std::vector<uint64_t> hs; for (size_t i = 0; i < n && i < g.undoStack.size(); ++i) hs.push_back(hashEntry(g.undoStack[i])); return hs; };
    struct { Parts state; size_t depth = 0; std::vector<uint64_t> hashes; bool valid = false; } cp;
    auto settle = [&] {   // release everything and stop, so the state can be compared and undone
        for (int i = 0; i < 3; ++i) if (held[i]) button(i, false);
        SDL_SetModState(KMOD_NONE);
        g.cheatOpen = g.scenesOpen = g.fileOpen = g.paletteOpen = g.ctxOpen = g.matMenuOpen = g.selOtherOpen = g.newConfirm = g.scaleOpen = false;
        step();
        if (g.playing) { g.stopPlay(); step(); }
        if (g.playing) bad("stop did not restore the snapshot");
        if (g.lmb) bad("a drag survived the button release");
    };
    g.frame();
    const Uint64 start = SDL_GetPerformanceCounter();
    for (frame = 0; frame < frames; ++frame) {
        if (frame % 300 == 0) {
            settle();
            if (cp.valid && g.undoStack.size() >= cp.depth && stackHashes(cp.depth) == cp.hashes) {
                const size_t n = g.undoStack.size() - cp.depth;
                Parts now, back, again;
                digest(now);
                std::string labels;
                for (size_t i = 0; i < n; ++i) {
                    labels += (labels.empty() ? "" : ", ") + g.undoLabels.back();
                    g.undo();
                    if (g.note.find("failed") != std::string::npos) bad("undo reported: " + g.note);
                }
                if (g.undoStack.size() != cp.depth) bad("undo did not step back to the checkpoint depth");
                digest(back);
                std::string d = differs(back, cp.state);
                if (!d.empty()) bad("undoing " + std::to_string(n) + " steps (" + labels + ") to the checkpoint did not restore the drawing: " + d + " differ");
                for (size_t i = 0; i < n; ++i) { g.redo(); if (g.note.find("failed") != std::string::npos) bad("redo reported: " + g.note); }
                digest(again);
                d = differs(again, now);
                if (!d.empty()) bad("redoing " + std::to_string(n) + " steps did not restore the drawing: " + d + " differ");
            }
            digest(cp.state); cp.depth = g.undoStack.size(); cp.hashes = stackHashes(cp.depth); cp.valid = true;
        }
        const int mods[] = {KMOD_NONE, KMOD_NONE, KMOD_NONE, KMOD_CTRL, KMOD_SHIFT, KMOD_CTRL | KMOD_SHIFT};
        if (rng.chance(30)) SDL_SetModState((SDL_Keymod)mods[rng.below(6)]);
        const int acts = rng.range(1, 3);
        for (int a = 0; a < acts; ++a) {
            int r = rng.below(1000);
            if (r < 330) {   // the pointer: mostly over the canvas, sometimes every other zone, the window edge, or off the window
                int z = rng.below(100);
                if (z < 50) inRect(g.L.canvas);
                else if (z < 60) inRect(g.L.strip);
                else if (z < 70) inRect(g.L.dock);
                else if (z < 78) inRect(g.L.top);
                else if (z < 85) inRect(g.L.ctx);
                else if (z < 88) inRect(g.L.status);
                else if (z < 93) motion(rng.range(-60, g.L.winW + 60), rng.range(-60, g.L.winH + 60));
                else if (!g.fieldRects.empty()) {   // the number half of some inspector field
                    auto it = g.fieldRects.begin(); std::advance(it, rng.below((int)g.fieldRects.size()));
                    inRect(SDL_Rect{it->second.x + it->second.w / 2, it->second.y, it->second.w / 2, it->second.h});
                } else inRect(g.L.canvas);
            } else if (r < 560) {   // a button goes down or up (left most often), sometimes as a double click
                int i = rng.below(100) < 60 ? 0 : rng.below(100) < 60 ? 1 : 2;
                button(i, !held[i], !held[i] && rng.chance(10) ? 2 : 1);
            } else if (r < 610) { SDL_Event e{}; e.type = SDL_MOUSEWHEEL; e.wheel.y = rng.range(-3, 3); push(e); }
            else if (r < 780) {   // a key from the map, with the frame's modifiers; Space is sometimes held for a few frames
                SDL_Keycode k = keys[(size_t)rng.below((int)keys.size())];
                if (k == SDLK_SPACE && spaceUp < 0 && rng.chance(50)) { key(k, false); spaceUp = frame + rng.range(1, 6); }
                else key(k);
            } else if (r < 860) {   // typed text, as a keyboard sends it
                static const char chars[] = "0123456789.+-*/()ab Z";
                std::string s; int n = rng.range(1, 4);
                for (int i = 0; i < n; ++i) s += chars[rng.below((int)sizeof(chars) - 1)];
                type(s);
            } else if (r < 872) {   // a real window resize
                int w = rng.chance(10) ? rng.range(0, 100) : rng.range(1024, 2560), h = rng.chance(10) ? rng.range(0, 100) : rng.range(640, 1440);
                SDL_SetWindowSize(g.win, w, h);
            } else if (r < 892) g.runCommand(rng.chance(40) ? "sim.play" : rng.chance(50) ? "sim.step" : "sim.stop");
            else if (r < 897) g.runCommand(("scene." + std::to_string(rng.below((int)Game::sceneList().size()))).c_str());
            else if (r < 912) {   // an undo / redo storm
                int n = rng.range(5, 40);
                for (int i = 0; i < n; ++i) { SDL_SetModState(KMOD_CTRL); key(rng.chance(60) ? SDLK_z : SDLK_y); }
                SDL_SetModState(KMOD_NONE);
            }
            else if (r < 916 && g.phys.bodyCount() < 2500) {   // mass drawing, then select everything
                if (held[0]) button(0, false);
                step();
                g.setTool(T_BOX);
                int n = rng.range(40, 120), cols = 20;
                for (int i = 0; i < n; ++i) {
                    float x = g.camXf + 20.f + (float)(i % cols) * 14.f, y = g.camYf + 20.f + (float)(i / cols) * 10.f;
                    motion(g.scrX(x), g.scrY(y)); button(0, true); step(); motion(g.scrX(x + 8.f), g.scrY(y + 5.f)); step(); button(0, false); step();
                }
                SDL_SetModState(KMOD_CTRL); key(SDLK_a); SDL_SetModState(KMOD_NONE);
            } else if (r < 930) {   // the palette: open, type, run
                SDL_SetModState(KMOD_CTRL); key(SDLK_k); SDL_SetModState(KMOD_NONE); step();
                type(queries[rng.below(12)]); step(); key(SDLK_RETURN);
            } else if (r < 940) {   // the file dialog with awkward names
                SDL_SetModState((SDL_Keymod)(KMOD_CTRL | KMOD_SHIFT)); key(SDLK_s); SDL_SetModState(KMOD_NONE); step();
                type(names[rng.below(8)]); step(); key(rng.chance(70) ? SDLK_RETURN : SDLK_ESCAPE);
            } else if (r < 960 && !g.fieldRects.empty()) {   // a field in the dock: click the number, type an expression, commit
                auto it = g.fieldRects.begin(); std::advance(it, rng.below((int)g.fieldRects.size()));
                if (held[0]) button(0, false);
                motion(it->second.x + it->second.w * 3 / 4, it->second.y + it->second.h / 2); button(0, true); button(0, false); step();
                static const char* exprs[] = {"", "-", "1/0", "99999999999", "-50", "0", "10+5*2", "(3", "2.5", "abc", "1e9", "600*600"};
                type(exprs[rng.below(12)]); step(); key(rng.chance(80) ? SDLK_RETURN : SDLK_TAB);
            } else if (r < 975) { SDL_SetModState(KMOD_CTRL); key(SDLK_a); SDL_SetModState(KMOD_NONE); }   // select everything
            else if (r < 985 && g.inSim) g.openSelectOther(mx, my);
            else inRect(g.L.canvas);
        }
        if (spaceUp >= 0 && frame >= spaceUp) { SDL_Event e{}; e.type = SDL_KEYUP; e.key.keysym.sym = SDLK_SPACE; push(e); spaceUp = -1; }
        step();
        check();
        if (frame % 2000 == 0 && frame)
            std::printf("fuzz frame %d: %d bodies, %zu undo, %d events, %d violations, %.0f s\n", frame, g.phys.bodyCount(), g.undoStack.size(), events, violations,
                        (double)(SDL_GetPerformanceCounter() - start) / (double)SDL_GetPerformanceFrequency());
    }
    settle();
    check();
    // degenerate window sizes go through the layout alone (SDL clamps the real window to the minimum)
    for (int w : {0, 1, 100, 1023, 1024, 1200, 5000}) for (int h : {0, 1, 100, 639, 640, 5000}) {
        Layout L = computeLayout(w, h, w % 2 == 0);
        SDL_Rect zones[] = {L.top, L.strip, L.ctx, L.canvas, L.dock, L.status};
        for (int i = 0; i < 6; ++i) {
            const SDL_Rect& z = zones[i];
            if (z.w <= 0 || z.h <= 0 || z.x < 0 || z.y < 0 || z.x + z.w > L.winW || z.y + z.h > L.winH)
                bad("degenerate layout " + std::to_string(w) + "x" + std::to_string(h) + ": zone " + std::to_string(i) + " bad");
            for (int j = i + 1; j < 6; ++j) if (overlaps(zones[i], zones[j])) bad("degenerate layout: zones overlap");
        }
    }
    std::printf("fuzz: seed %u, %d frames, %d events, %d bodies at the end, worst frame %d ms, %d violations: %s\n", seed, frames, events, g.phys.bodyCount(),
                worstMs, violations, violations ? "FAIL" : "clean");
    return violations ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) if (!std::strcmp(argv[i], "--selftest")) return runSelfTests();
    // Headless runs: sandbots --shot out.bmp [frames] [--scene N] [--win WxH] [--heat] [--trace] [--no-air] [--no-momentum] ...
    const char* shot = nullptr;
    const char* galleryOut = nullptr;
    int shotFrames = 300, scene = 0, winW = 1600, winH = 900, fuzzFrames = -1;
    uint32_t fuzzSeed = 0;
    bool heat = false, trace = false, g0 = false, elecFlag = false, helpFlag = false, pressureFlag = false, noAir = false, noMomentum = false;
    int camFlag = -1;
    bool scenesFlag = false; bool timeFlag = false; float zoomFlag = 1.f, camYFlag = 0.f;
    int tabFlag = -1, hoverX = -1, hoverY = -1;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) {
            shot = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') shotFrames = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--ui-gallery") && i + 1 < argc) galleryOut = argv[++i];
        else if (!std::strcmp(argv[i], "--ui-fuzz") && i + 2 < argc) { fuzzSeed = (uint32_t)std::strtoul(argv[i + 1], nullptr, 10); fuzzFrames = std::atoi(argv[i + 2]); i += 2; }
        else if (!std::strcmp(argv[i], "--win") && i + 1 < argc) { if (std::sscanf(argv[++i], "%dx%d", &winW, &winH) != 2) { std::fprintf(stderr, "--win WxH\n"); return 2; } }
        else if (!std::strcmp(argv[i], "--scene") && i + 1 < argc) scene = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--heat")) heat = true;
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
    const bool headless = shot || galleryOut || fuzzFrames >= 0;
    if (headless) SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);

    Game g;
    if (!g.init(headless, winW, winH)) return 1;
    g.world.needAir = !noAir;              // --no-air: the old model, fuel burns without oxygen
    g.world.gasMomentum = !noMomentum;   // --no-momentum: the diffusion-only gas model, for comparing scenes

    if (fuzzFrames >= 0) {
        int rc = fuzz(g, fuzzSeed, fuzzFrames);
        g.shutdown();
        return rc;
    }
    if (galleryOut) {
        gallery(g, galleryOut);
        g.shutdown();
        return 0;
    }
    if (shot) {
        g.frame();   // the layout and the toolkit's first pass, so the tests have a canvas to aim at
        Driver d(g);
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
            case 12: g.buildPrecisionTest(); g.selectBody(g.sel.empty() ? -1 : g.sel[0], false, true); g.setTool(T_SELECT); break;
            case 13: g.buildPrecisionTest(); g.setTool(T_HOSE); g.clearSelection(); break;
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
                const SDL_Rect c = g.L.canvas;
                d.downWin(c.x + 600, c.y + 300, SDL_BUTTON_MIDDLE);
                d.moveWin(c.x + 300, c.y + 300);
                d.upWin(c.x + 300, c.y + 300, SDL_BUTTON_MIDDLE);
                std::printf("middle-drag 300px left: camera at %d (expected %d)\n", g.camX, (int)std::lround(300.f / g.sc()));
                d.clickWin(c.x + 900, c.y + c.h - 4);
                std::printf("scroll strip click at 900px: camera at %d (expected %d)\n", g.camX, (int)std::lround(900.f / c.w * World::W - g.viewW() * 0.5f));
                d.key(SDLK_HOME);
                std::printf("Home: camera at %d\n", g.camX);
                g.selectBody(g.phys.bodies.size() > 0 ? 0 : -1, false, false);
                d.key(SDLK_f);
                std::printf("F with a selection: focus body %d\n", g.focusBody);
                g.play();
                for (int i = 0; i < 600; ++i) g.update();
                bool ok; Vec2 f = g.focusPoint(ok);
                std::printf("after 600 frames the car is at x=%.0f and the camera at %d (view centre %d)\n", f.x, g.camX, g.camX + (int)(g.viewW() / 2));
                d.key(SDLK_f);
                std::printf("F again: focus body %d\n", g.focusBody);
                break;
            }
            case 25: editorTest(g); break;
            case 28: {   // paraffin bonds: they hold a heavy block, give way when warmed, and melt in a flame
                g.resetWorld();
                g.rect(0, 200, World::W - 1, 203, M_WALL);
                g.phys.addBox(Vec2(60, 60), Vec2(50, 3), 0, M_STEEL, true);            // ledge
                int cold = g.phys.addBox(Vec2(30, 76), Vec2(14, 10), 0, M_STEEL, false);      // heavy block, wax glued under the ledge
                g.phys.addBond(Vec2(30, 63), cold, 0, BOND_TEMP[0], 0.f, BOND_G[0]);
                int warm = g.phys.addBox(Vec2(90, 76), Vec2(14, 10), 0, M_STEEL, false);
                g.phys.addBond(Vec2(90, 63), warm, 0, BOND_TEMP[0], 0.f, BOND_G[0]);
                g.rect(105, 68, 111, 84, M_HEATER);                                        // a heater next to the second block
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
                g.undoStack.clear(); g.undoLabels.clear();
                g.anchored = false;
                int A = g.phys.addBox(Vec2(60, 100), Vec2(15, 8), 0, M_STEEL, false);
                int Bx = g.phys.addBox(Vec2(91, 100), Vec2(15, 8), 0, M_STEEL, false);   // 1-cell gap, as touching bodies sit
                g.phys.stampBodies();
                g.setTool(T_BOND);
                d.click(75.5f, 100);
                int bonds = 0; for (auto& j : g.phys.joints) if (j.alive && j.bondId >= 0) ++bonds;
                int worldPins = 0; for (auto& j : g.phys.joints) if (j.alive && j.bondId >= 0 && (j.b < 0 || j.a < 0)) ++worldPins;
                std::printf("bond tool on a seam between touching boxes: %d pins, %d of them to the world (expect 2 and 0)\n", bonds, worldPins);
                g.phys.bodies[A].vel = Vec2(0, 0);
                g.setTool(T_SELECT);
                for (int i = 0; i < 40; ++i) g.update();
                g.phys.bodies[A].vel = Vec2(-40, 0);
                float gap0 = g.phys.bodies[Bx].pos.x - g.phys.bodies[A].pos.x;
                g.play();
                for (int i = 0; i < 60; ++i) g.update();
                float gap1 = g.phys.bodies[Bx].pos.x - g.phys.bodies[A].pos.x;
                std::printf("shoved one box: the other followed: centre gap %.1f -> %.1f, bonds broken %ld\n", gap0, gap1, g.phys.bondsBroken);
                g.stopPlay();
                g.resetWorld();
                g.undoStack.clear(); g.undoLabels.clear();
                int host = g.phys.addBox(Vec2(200, 100), Vec2(40, 10), 0, M_STEEL, false);
                int piston = g.phys.addBox(Vec2(190, 130), Vec2(5, 5), 0, M_STEEL, false);
                g.phys.stampBodies();
                g.setTool(T_SLIDER);
                d.drag(190, 130, 215, 100);
                int rel = 0; for (auto& j : g.phys.joints) if (j.alive && j.type == J_SLIDER && j.b == host && j.a == piston) ++rel;
                std::printf("slider tool: piston-to-host slider created: %d (expect 1)\n", rel);
                g.play();
                for (int i = 0; i < 90; ++i) g.update();
                std::printf("after 90 frames the host fell to y=%.0f and the piston to y=%.0f; their vertical gap %.1f (started 30)\n", g.phys.bodies[host].pos.y, g.phys.bodies[piston].pos.y, g.phys.bodies[piston].pos.y - g.phys.bodies[host].pos.y);
                g.stopPlay();
                g.resetWorld();
                g.undoStack.clear(); g.undoLabels.clear();
                g.phys.addBox(Vec2(100, 60), Vec2(8, 8), 0, M_STEEL, true);
                g.phys.addBox(Vec2(160, 60), Vec2(8, 8), 0, M_STEEL, false);
                g.phys.stampBodies();
                g.setTool(T_SPRING);
                d.drag(100, 60, 160, 60);
                int sj = -1; for (auto& j : g.phys.joints) if (j.alive && j.type == J_DISTANCE) sj = j.id;
                std::printf("spring tool: spring %s, rest %.0f, %.2f Hz\n", sj >= 0 ? "made" : "NOT made", sj >= 0 ? g.phys.joints[sj].length : 0.f, sj >= 0 ? g.phys.joints[sj].freq : 0.f);
                g.setTool(T_SELECT); g.clearSelection();
                d.click(130, 60);
                std::printf("click on the spring selects it: %s\n", yn(g.selJoint == sj));
                g.dockTab = 0; d.frames(2);
                float f0 = g.phys.joints[sj].freq, l0 = g.phys.joints[sj].length;
                auto typeField = [&](const char* name, const char* value) {
                    if (!g.fieldRects.count(name)) return false;
                    SDL_Rect r = g.fieldRects[name];
                    d.clickWin(r.x + r.w * 3 / 4, r.y + r.h / 2); d.type(value); d.key(SDLK_RETURN); d.frames(1);
                    return true;
                };
                bool a1 = typeField("jfreq", "4"), a2 = typeField("jlen", "50");
                std::printf("inspector fields found: %d %d; stiffness %.2f -> %.2f, rest length %.0f -> %.0f\n", a1, a2, f0, g.phys.joints[sj].freq, l0, g.phys.joints[sj].length);
                d.key(SDLK_DELETE);
                std::printf("Delete removes the selected spring: %s\n", yn(!g.phys.joints[sj].alive));
                d.key(SDLK_z, KMOD_CTRL);
                std::printf("Ctrl+Z brings it back: %s\n", yn(g.phys.joints[sj].alive));
                break;
            }
            case 33: {
                g.resetWorld();
                g.phys.addBox(Vec2(100, 60), Vec2(8, 8), 0, M_STEEL, true);
                int b2 = g.phys.addBox(Vec2(160, 60), Vec2(8, 8), 0, M_STEEL, false);
                int sj = g.phys.addDistance(0, Vec2(100, 60), b2, Vec2(160, 60), 3.f);
                g.phys.stampBodies();
                g.setTool(T_SELECT); g.selectJoint(sj);
                break;
            }
            case 36: {   // arrow keys nudge the selection in edit mode: a body, a group, a box-selection
                g.resetWorld();
                g.undoStack.clear(); g.undoLabels.clear();
                int a = g.phys.addBox(Vec2(100, 100), Vec2(10, 5), 0, M_STEEL, false);
                int b = g.phys.addCircle(Vec2(130, 100), 6.f, M_STEEL, false, false);
                int c = g.phys.addBox(Vec2(160, 100), Vec2(5, 5), 0, M_STEEL, false);
                g.phys.addPin(Vec2(115, 100), a, b, false, false);
                g.phys.stampBodies();
                g.setTool(T_SELECT);
                g.selectBody(a, false, false);
                d.key(SDLK_RIGHT); d.key(SDLK_RIGHT); d.key(SDLK_DOWN);
                std::printf("one body, Right Right Down: at (%.2f, %.2f) (expected 102, 101)\n", g.phys.bodies[a].pos.x, g.phys.bodies[a].pos.y);
                d.key(SDLK_LEFT, KMOD_SHIFT);
                std::printf("Shift+Left: x %.2f (expected 92)\n", g.phys.bodies[a].pos.x);
                d.key(SDLK_UP, KMOD_CTRL);
                std::printf("Ctrl+Up: y %.2f (expected 100.75)\n", g.phys.bodies[a].pos.y);
                d.key(SDLK_z, KMOD_CTRL);
                std::printf("Ctrl+Z undoes the whole run of nudges: (%.2f, %.2f) (expected 100, 100)\n", g.phys.bodies[a].pos.x, g.phys.bodies[a].pos.y);
                g.clearSelection();
                g.sel = {a, b}; g.primary = a;
                float gap0 = g.phys.bodies[b].pos.x - g.phys.bodies[a].pos.x;
                for (int i = 0; i < 5; ++i) d.key(SDLK_RIGHT);
                std::printf("two pinned bodies selected, Right x5: a at %.1f, b at %.1f, spacing %.1f -> %.1f, the third body stayed at %.1f\n", g.phys.bodies[a].pos.x, g.phys.bodies[b].pos.x, gap0, g.phys.bodies[b].pos.x - g.phys.bodies[a].pos.x, g.phys.bodies[c].pos.x);
                g.clearSelection();
                g.sel = {b, c}; g.primary = b;
                g.groupSelection();
                g.selectBody(c, false, false);
                d.key(SDLK_DOWN); d.key(SDLK_DOWN);
                std::printf("a group (clicked as a whole), Down x2: c moved %.1f, b moved %.1f in y\n", g.phys.bodies[c].pos.y - 100.f, g.phys.bodies[b].pos.y - 100.f);
                g.play();
                float yb = g.phys.bodies[c].pos.y;
                d.key(SDLK_DOWN);
                std::printf("while playing an arrow key does not nudge: %s\n", yn(g.phys.bodies[c].pos.y == yb));
                g.stopPlay();
                break;
            }
            case 35: {   // zoom: the mouse still lands on the right cell, the wheel zooms about the pointer, drawing works zoomed in
                g.resetWorld();
                g.undoStack.clear(); g.undoLabels.clear();
                g.setCam(300.f, 60.f);
                const SDL_Rect c = g.L.canvas;
                int px = c.x + 540, py = c.y + 300;
                d.moveWin(px, py);
                Vec2 before = g.toWorld(px, py);
                for (int i = 0; i < 4; ++i) { SDL_Event e{}; e.type = SDL_MOUSEWHEEL; e.wheel.y = 1; d.push(e); }
                Vec2 after = g.toWorld(px, py);
                std::printf("wheel x4: zoom %.1fx; the cell under the pointer was (%.1f, %.1f) and is (%.1f, %.1f)\n", g.zoom, before.x, before.y, after.x, after.y);
                g.setCam(300.f, 60.f);
                g.setTool(T_BOX); g.anchored = true;
                d.down(320, 90); d.mouseTo(335, 98); g.update(); d.mouseTo(340, 100); g.update(); d.up(340, 100);
                int made = -1; for (auto& b : g.phys.bodies) if (b.alive) made = b.id;
                if (made >= 0) std::printf("a box dragged from (320,90) to (340,100) at %.1fx zoom: centre (%.1f, %.1f), size %.1f x %.1f (expected 330, 95, 20 x 10)\n", g.zoom, g.phys.bodies[made].pos.x, g.phys.bodies[made].pos.y, g.phys.bodies[made].half.x * 2, g.phys.bodies[made].half.y * 2);
                else std::printf("no box was made\n");
                float cy0 = g.camYf;
                d.clickWin(c.x + c.w - 5, c.y + 40);
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
                g.setTool(T_SELECT); g.selectBody(e, false, false);
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
            default: g.buildTestScene(scene); break;
        }
        if (heat) g.heatView = true;
        if (elecFlag) g.elecView = true;
        if (helpFlag) g.cheatOpen = true;
        if (pressureFlag) g.pressureView = true;
        g.zoom = zoomFlag;
        if (camFlag >= 0) g.setCam((float)camFlag, camYFlag); else g.setCam(g.camXf, camYFlag);
        if (tabFlag >= 0 && tabFlag < T_COUNT) g.setTool((Tool)tabFlag);
        if (scenesFlag) g.scenesOpen = true;
        if (hoverX >= 0) { g.in.mx = hoverX; g.in.my = hoverY; }
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
        g.render();   // a second pass so tooltips and hover states have settled
        g.screenshot(shot);
        g.shutdown();
        return 0;
    }

    Uint64 last = SDL_GetPerformanceCounter();
    double acc = 0;
    const double dt = 1.0 / 60.0;
    while (g.running) {
        g.pollEvents();
        Uint64 now = SDL_GetPerformanceCounter();
        double frameS = (double)(now - last) / (double)SDL_GetPerformanceFrequency();
        last = now;
        if (frameS > 0) g.fps = g.fps * 0.95f + (float)(1.0 / frameS) * 0.05f;
        acc = std::min(acc + frameS, 2.2 * dt);   // never more than two steps behind: a slow machine runs in slow motion instead of spiralling
        while (acc >= dt) { g.update(); acc -= dt; }
        g.frame();
        SDL_RenderPresent(g.ren);
        SDL_Delay(1);
    }
    g.shutdown();
    return 0;
}
