#include "app.hpp"


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
                // duplicate and flip through the keyboard: two boxes, box-selected, ctrl+d at the pointer, ctrl+h, undo twice
                g.clearSelection();
                g.tool = T_BOX; drag(40, 160, 80, 180); drag(90, 160, 130, 190);
                g.tool = T_SELECT;
                drag(30, 150, 140, 200);                        // box-select both (the press is on empty space)
                std::vector<int> pair = g.sel;
                int n1 = alive();
                auto bboxC = [&](const std::vector<int>& ids) {   // the centre of the pos +- bound box, as the clipboard measures it
                    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
                    for (int id : ids) {
                        const Body& b = g.phys.bodies[id];
                        x0 = std::min(x0, b.pos.x - b.bound); x1 = std::max(x1, b.pos.x + b.bound);
                        y0 = std::min(y0, b.pos.y - b.bound); y1 = std::max(y1, b.pos.y + b.bound);
                    }
                    return Vec2((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
                };
                mouseTo(250, 200);
                key(SDLK_d, KMOD_CTRL);
                bool fresh = g.sel.size() == 2;
                for (int id : g.sel) if (std::find(pair.begin(), pair.end(), id) != pair.end()) fresh = false;
                Vec2 dupC = bboxC(g.sel);
                std::printf("ctrl+d: %d -> %d bodies (expected %d), the duplicates are the new selection: %s, placed at the pointer (%.0f, %.0f): %s\n",
                            n1, alive(), n1 + 2, pair.size() == 2 && fresh ? "yes" : "NO", dupC.x, dupC.y,
                            length(dupC - Vec2(250, 200)) < 0.5f ? "yes" : "NO");
                std::vector<int> dup = g.sel;
                std::vector<Vec2> p0;
                float fx0 = 1e9f, fx1 = -1e9f;
                for (int id : dup) {
                    const Body& b = g.phys.bodies[id];
                    p0.push_back(b.pos); fx0 = std::min(fx0, b.pos.x - b.half.x); fx1 = std::max(fx1, b.pos.x + b.half.x);
                }
                float cx = (fx0 + fx1) * 0.5f;                  // the flip pivot: the centre of the selection's tight bounding box
                key(SDLK_h, KMOD_CTRL);
                bool mirrored = dup.size() == 2;
                for (size_t i = 0; i < dup.size(); ++i) {
                    const Vec2& q = g.phys.bodies[dup[i]].pos;
                    mirrored = mirrored && std::fabs(q.x - (2.f * cx - p0[i].x)) < 1e-3f && std::fabs(q.y - p0[i].y) < 1e-3f;
                }
                std::printf("ctrl+h mirrors the x positions about the selection centre %.1f: %s (%.1f, %.1f -> %.1f, %.1f)\n", cx, mirrored ? "yes" : "NO",
                            p0.empty() ? 0.f : p0[0].x, p0.size() > 1 ? p0[1].x : 0.f,
                            dup.empty() ? 0.f : g.phys.bodies[dup[0]].pos.x, dup.size() > 1 ? g.phys.bodies[dup[1]].pos.x : 0.f);
                key(SDLK_z, KMOD_CTRL);
                bool back = true;
                for (size_t i = 0; i < dup.size(); ++i) back = back && length(g.phys.bodies[dup[i]].pos - p0[i]) < 1e-3f;
                key(SDLK_z, KMOD_CTRL);
                std::printf("ctrl+z puts the duplicates back where they were: %s; another ctrl+z removes them: %d bodies (expected %d)\n",
                            back ? "yes" : "NO", alive(), n1);
                // the measure tool: a drag leaves its readout on screen and touches neither the world nor the undo stack
                size_t undoN = g.undoStack.size();
                g.tool = T_MEASURE;
                drag(160, 210, 220, 180);
                std::printf("measure tool: (%.0f, %.0f) -> (%.0f, %.0f) length %.1f, still shown: %s, bodies %d and undo entries unchanged: %s\n",
                            g.measA.x, g.measA.y, g.measB.x, g.measB.y, length(g.measB - g.measA), g.measOn ? "yes" : "NO", alive(),
                            alive() == n1 && g.undoStack.size() == undoN ? "yes" : "NO");
                // handles: a fresh 60 x 30 box, its right edge dragged 10 cells, a corner with Ctrl, the rotation handle plain and
                // with Shift, undo; then the grid snapping of a move drag and of a dragged edge
                g.clearSelection(); g.snapIdx = 0;
                g.tool = T_BOX;
                drag(100, 160, 160, 190);                       // x 100..160, y 160..190, centre (130, 175)
                g.tool = T_SELECT;
                down(130, 175); up(130, 175);
                const int hb = g.primary;
                auto body = [&]() -> const Body& { return g.phys.bodies[hb]; };
                auto hpos = [&](int h) { return g.handlePos(g.phys.bodies[hb], h); };
                auto near = [](float a, float b, float tol = 0.05f) { return std::fabs(a - b) <= tol; };
                auto nearV = [&](Vec2 a, Vec2 b) { return near(a.x, b.x) && near(a.y, b.y); };
                std::printf("a fresh 60 x 30 box is selected and shows handles: %s\n", hb >= 0 && g.handleBody() == hb && g.handleResizes() ? "yes" : "NO");
                Vec2 rh = hpos(3);                              // the right edge midpoint
                drag(rh.x, rh.y, rh.x + 10, rh.y);
                std::printf("right edge handle dragged 10 cells right: width %.2f (expected 70), left edge still at %.2f (expected 100): %s\n", body().half.x * 2,
                            body().pos.x - body().half.x, near(body().half.x * 2, 70) && near(body().pos.x - body().half.x, 100) ? "yes" : "NO");
                Vec2 c0 = body().pos, br = hpos(4);             // the bottom-right corner
                SDL_SetModState(KMOD_CTRL);
                drag(br.x, br.y, br.x + 4, br.y + 6);
                SDL_SetModState(KMOD_NONE);
                std::printf("ctrl + corner handle dragged (4, 6): %.0f x %.0f (expected 78 x 42), centre still at (%.2f, %.2f): %s\n", body().half.x * 2, body().half.y * 2,
                            body().pos.x, body().pos.y, nearV(body().pos, c0) && near(body().half.x * 2, 78) && near(body().half.y * 2, 42) ? "yes" : "NO");
                Vec2 rot = hpos(Game::H_ROT);
                float arm = length(rot - body().pos);
                drag(rot.x, rot.y, body().pos.x + arm, body().pos.y);   // from straight above the centre to straight right of it
                float deg = body().angle * 180.f / PI;
                std::printf("rotation handle dragged a quarter turn: angle %.1f (expected 90), centre still at (%.2f, %.2f): %s\n", deg, body().pos.x, body().pos.y,
                            near(deg, 90, 0.5f) && nearV(body().pos, c0) ? "yes" : "NO");
                key(SDLK_z, KMOD_CTRL);
                rot = hpos(Game::H_ROT);
                SDL_SetModState(KMOD_SHIFT);
                drag(rot.x, rot.y, body().pos.x + arm * std::cos(-53.f * PI / 180.f), body().pos.y + arm * std::sin(-53.f * PI / 180.f));   // 37 degrees round
                SDL_SetModState(KMOD_NONE);
                deg = body().angle * 180.f / PI;
                std::printf("shift + rotation handle swept 37 degrees: angle %.2f, snapped to a multiple of 15: %s\n", deg, near(deg, 30, 0.01f) ? "yes" : "NO");
                key(SDLK_z, KMOD_CTRL); key(SDLK_z, KMOD_CTRL); key(SDLK_z, KMOD_CTRL);
                bool restored = near(body().half.x * 2, 60) && near(body().half.y * 2, 30) && nearV(body().pos, Vec2(130, 175)) && near(body().angle, 0);
                std::printf("three undos restore the box: %.0f x %.0f at (%.0f, %.0f), angle %.0f: %s\n", body().half.x * 2, body().half.y * 2, body().pos.x, body().pos.y,
                            body().angle * 180.f / PI, restored ? "yes" : "NO");
                g.snapIdx = 3;                                  // a 5-cell grid
                drag(130, 175, 137, 178);
                std::printf("move drag by (7, 3) with a 5-cell grid: the body lands at (%.0f, %.0f) (expected 135, 180): %s\n", body().pos.x, body().pos.y,
                            nearV(body().pos, Vec2(135, 180)) ? "yes" : "NO");
                rh = hpos(3);
                drag(rh.x, rh.y, rh.x + 7.3f, rh.y);
                std::printf("right edge dragged 7.3 cells with the grid on: edge at %.1f (expected 170), width %.1f: %s\n", body().pos.x + body().half.x, body().half.x * 2,
                            near(body().pos.x + body().half.x, 170) ? "yes" : "NO");
                g.snapIdx = 0;
                key(SDLK_f, KMOD_SHIFT);                        // zoom to the selection: a 65 x 30 box grown by a fifth fits at 4x, not 6x
                Vec2 centre(g.camXf + g.viewW() * 0.5f, g.camYf + g.viewH() * 0.5f);
                std::printf("shift+f zooms to the selection: %.0fx (expected 4), view centre (%.1f, %.1f) on the body at (%.1f, %.1f): %s\n", g.zoom, centre.x, centre.y,
                            body().pos.x, body().pos.y, near(g.zoom, 4) && nearV(centre, body().pos) ? "yes" : "NO");
                g.clearSelection();
                key(SDLK_f, KMOD_SHIFT);
                std::printf("shift+f with nothing selected resets the zoom: %.0fx: %s\n", g.zoom, near(g.zoom, 1) ? "yes" : "NO");
                g.setCam(0, 0);
                down(135, 180); up(135, 180);
                // screenshot states: HANDLE_SHOT=hover parks the pointer on the rotation handle, =drag leaves the right edge mid-drag,
                // =circle selects the circle and hovers its right handle
                if (const char* hs = std::getenv("HANDLE_SHOT")) {
                    if (!std::strcmp(hs, "circle")) { down(130, 115); up(130, 115); Vec2 h = g.handlePos(g.phys.bodies[g.primary], 3); mouseTo(h.x, h.y); }
                    else if (!std::strcmp(hs, "hover")) { rot = hpos(Game::H_ROT); mouseTo(rot.x, rot.y); }
                    else { rh = hpos(3); down(rh.x, rh.y); mouseTo(rh.x + 12, rh.y + 4); g.update(); }
                }
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
        if (hoverX >= 0) { g.mousePx = hoverX; g.mousePy = hoverY; g.inSim = Game::inSimPx(hoverX, hoverY); if (g.inSim) g.mouse = g.toWorld(hoverX, hoverY); }
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

