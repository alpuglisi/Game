// Drawing the world inside the canvas: cells, bodies, joints, selection and handles, grid and rulers, the tool ghosts.
#include "app.hpp"

uint32_t Game::cellColor(const Cell& c, uint32_t bg) const {
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

void Game::renderParticles() {
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

SDL_FPoint Game::sp(Vec2 p) const { return SDL_FPoint{(p.x - camXf) * sc(), (p.y - camYf) * sc()}; }

void Game::fillPoly(const std::vector<Vec2>& pts, uint32_t color) { fillPolyC(pts, rgb(color)); }

void Game::fillPolyC(const std::vector<Vec2>& pts, SDL_Color c) {
    if (pts.empty()) return;
    std::vector<SDL_Vertex>& v = scratchV;
    std::vector<int>& idx = scratchIdx;
    v.clear(); idx.clear();
    Vec2 cen;
    for (auto& p : pts) cen += p;
    cen = cen / (float)pts.size();
    v.push_back({sp(cen), c, {0, 0}});
    for (auto& p : pts) v.push_back({sp(p), c, {0, 0}});
    int n = (int)pts.size();
    for (int i = 0; i < n; ++i) { idx.push_back(0); idx.push_back(1 + i); idx.push_back(1 + (i + 1) % n); }
    SDL_RenderGeometry(ren, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

void Game::lineWorld(Vec2 a, Vec2 b, SDL_Color c, int thick) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_FPoint pa = sp(a), pb = sp(b);
    for (int i = 0; i < thick; ++i) {
        float o = (float)i - (thick - 1) * 0.5f;
        SDL_RenderDrawLineF(ren, pa.x + o, pa.y, pb.x + o, pb.y);
        if (thick > 1) SDL_RenderDrawLineF(ren, pa.x, pa.y + o, pb.x, pb.y + o);
    }
}

void Game::outlinePoly(const std::vector<Vec2>& pts, SDL_Color c) {
    for (size_t i = 0; i < pts.size(); ++i) lineWorld(pts[i], pts[(i + 1) % pts.size()], c, 2);
}

std::vector<Vec2> Game::circlePts(Vec2 c, float r, int n) {
    std::vector<Vec2> pts;
    for (int i = 0; i < n; ++i) {
        float a = 2 * PI * i / n;
        pts.push_back(c + Vec2(std::cos(a), std::sin(a)) * r);
    }
    return pts;
}

void Game::renderFan(const Body& b, uint32_t fill) {
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
    smallText(ren, lab, (int)sp(lp).x - smallWidth(lab, 1) / 2, (int)sp(lp).y, 1, SDL_Color{255, 235, 120, 255});
}

void Game::renderBodies() {
    for (auto& b : phys.bodies) {
        if (!b.alive) continue;
        uint32_t fill = heatView ? heatColor(b.temp) : glowColor(0xFF000000u | b.color, b.temp);
        if (b.src.on) fill = mix(fill, 0xFF000000u | MATS[b.src.mat].color, 0.5f);
        SDL_Color edge = rgb(shade(fill, 0.55f) & 0xFFFFFF);
        if (b.shape == SHAPE_BOX) {
            std::vector<Vec2>& pts = scratchPts;
            pts.clear();
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

std::vector<Vec2> Game::bodyOutline(const Body& b, float grow) {
    if (b.shape == SHAPE_CIRCLE) return circlePts(b.pos, b.radius + grow);
    Vec2 h = b.half + Vec2(grow, grow);
    return {b.toWorld(Vec2(-h.x, -h.y)), b.toWorld(Vec2(h.x, -h.y)), b.toWorld(Vec2(h.x, h.y)), b.toWorld(Vec2(-h.x, h.y))};
}

void Game::renderFocusMark() {
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

void Game::renderSelection() {
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
            smallText(ren, "CUTTER", (int)q.x - 36, (int)q.y - 22, 1, SDL_Color{255, 120, 120, 255});
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
    if (tool == T_SELECT && lmb && !moveArmed && !moving && handle < 0 && length(mouse - dragStart) >= 3.f) {
        Vec2 a = dragStart, b = mouse;
        outlinePoly({a, Vec2(b.x, a.y), b, Vec2(a.x, b.y)}, SDL_Color{255, 220, 80, 200});
    }
    renderHandles();
}

void Game::renderHandles() {
    int id = handleBody();
    if (id < 0) return;
    const Body& b = phys.bodies[id];
    const int hov = handle >= 0 ? handle : (inSim && !lmb ? handleAt(mouse) : -1);
    const float r = HANDLE_PX / sc();
    Vec2 e = bodyExtent(b), rot = handlePos(b, H_ROT);
    lineWorld(b.toWorld(Vec2(0, -e.y)), rot, SDL_Color{255, 255, 255, 170}, 1);
    for (int h = 0; h <= H_ROT; ++h) {
        if (!handleShown(b, h)) continue;
        const bool lit = h == hov;
        SDL_Color fill = lit ? SDL_Color{255, 255, 255, 255} : SDL_Color{225, 232, 245, 225};
        SDL_Color edge = lit ? SDL_Color{255, 220, 80, 255} : SDL_Color{30, 36, 50, 255};
        std::vector<Vec2> pts;
        if (h == H_ROT) pts = circlePts(rot, r * 1.2f, 14);
        else {
            Vec2 l(HSX[h] * e.x, HSY[h] * e.y);
            pts = {b.toWorld(l + Vec2(-r, -r)), b.toWorld(l + Vec2(r, -r)), b.toWorld(l + Vec2(r, r)), b.toWorld(l + Vec2(-r, r))};
        }
        fillPolyC(pts, fill);
        for (size_t i = 0; i < pts.size(); ++i) lineWorld(pts[i], pts[(i + 1) % pts.size()], edge, 1);
    }
    if (handle >= 0 || hov < 0) return;   // during a drag the dimension field beside the pointer shows the size
    const char* hint = hov == H_ROT ? "Drag to rotate (Shift: 15 deg steps)" : b.shape == SHAPE_CIRCLE ? "Drag to set the radius"
                     : (HSX[hov] && HSY[hov]) ? "Drag to resize (Ctrl: about the centre, Shift: keep proportions)"
                                              : "Drag to resize (Ctrl: about the centre)";
    SDL_FPoint q = sp(mouse);
    SDL_Rect bg{(int)q.x + 9, (int)q.y + 7, smallWidth(hint, 1) + 6, 13};
    SDL_SetRenderDrawColor(ren, 8, 11, 18, 190);
    SDL_RenderFillRect(ren, &bg);
    smallText(ren, hint, (int)q.x + 12, (int)q.y + 10, 1, SDL_Color{255, 240, 150, 255});
}

void Game::renderJoints() {
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

void Game::renderArcs() {
    for (auto& a : world.arcs) {
        Vec2 p0(a.x0 + 0.5f, a.y0 + 0.5f), p1(a.x1 + 0.5f, a.y1 + 0.5f);
        lineWorld(p0, p1, SDL_Color{110, 150, 255, 140}, 7);
        lineWorld(p0, p1, SDL_Color{230, 240, 255, 255}, 3);
    }
}

int Game::niceStep(float cells) {   // the smallest of 1, 2, 5, 10, 20, 50... that is at least `cells`
    static const int steps[] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000};
    for (int s : steps) if ((float)s >= cells) return s;
    return 1000;
}

void Game::renderGrid() {
    if (!gridOn) return;
    const float s = sc();
    const int minor = niceStep(14.f / s), label = std::max(minor, niceStep(64.f / s));
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    const int xa = (int)std::floor(camXf / minor) * minor, xb = (int)std::ceil(camXf + viewW());
    for (int x = std::max(0, xa); x <= std::min(World::W, xb); x += minor) {
        bool major = x % label == 0;
        SDL_SetRenderDrawColor(ren, 150, 175, 220, major ? 46 : 18);
        int px = (int)std::lround((x - camXf) * s);
        SDL_RenderDrawLine(ren, px, 0, px, L.canvas.h);
    }
    const int ya = (int)std::floor(camYf / minor) * minor, yb = (int)std::ceil(camYf + viewH());
    for (int y = std::max(0, ya); y <= std::min(World::H, yb); y += minor) {
        bool major = y % label == 0;
        SDL_SetRenderDrawColor(ren, 150, 175, 220, major ? 46 : 18);
        int py = (int)std::lround((y - camYf) * s);
        SDL_RenderDrawLine(ren, 0, py, L.canvas.w, py);
    }
}

void Game::renderRulers() {
    const float s = sc();
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    // the edges of the world
    SDL_SetRenderDrawColor(ren, 255, 150, 60, 200);
    for (int ex : {0, World::W}) { int px = (int)std::lround((ex - camXf) * s); if (px >= 0 && px < L.canvas.w) SDL_RenderDrawLine(ren, px, 0, px, L.canvas.h); }
    for (int ey : {0, World::H}) { int py = (int)std::lround((ey - camYf) * s); if (py >= 0 && py < L.canvas.h) SDL_RenderDrawLine(ren, 0, py, L.canvas.w, py); }
    if (!gridOn) return;
    const int minor = niceStep(14.f / s), label = std::max(minor, niceStep(64.f / s));
    const int RW = 30, RH = 13;
    SDL_SetRenderDrawColor(ren, 8, 11, 18, 205);
    SDL_Rect top{0, 0, L.canvas.w, RH}, left{0, RH, RW, L.canvas.h - RH};
    SDL_RenderFillRect(ren, &top); SDL_RenderFillRect(ren, &left);
    SDL_SetRenderDrawColor(ren, 80, 95, 130, 255);
    SDL_RenderDrawLine(ren, 0, RH, L.canvas.w, RH); SDL_RenderDrawLine(ren, RW, RH, RW, L.canvas.h);
    const SDL_Color txt{170, 190, 225, 255};
    for (int x = std::max(0, (int)std::floor(camXf / label) * label); x <= std::min(World::W, (int)std::ceil(camXf + viewW())); x += label) {
        int px = (int)std::lround((x - camXf) * s);
        if (px < RW) continue;
        SDL_SetRenderDrawColor(ren, 150, 170, 210, 255); SDL_RenderDrawLine(ren, px, RH - 5, px, RH);
        smallText(ren, std::to_string(x), px + 3, 2, 1, txt);
    }
    for (int y = std::max(0, (int)std::floor(camYf / label) * label); y <= std::min(World::H, (int)std::ceil(camYf + viewH())); y += label) {
        int py = (int)std::lround((y - camYf) * s);
        if (py < RH + 2) continue;
        SDL_SetRenderDrawColor(ren, 150, 170, 210, 255); SDL_RenderDrawLine(ren, RW - 5, py, RW, py);
        smallText(ren, std::to_string(y), 2, py + 2, 1, txt);
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
    int bx = RW + 14, by = L.canvas.h - 50, bw = (int)std::lround(bar * s);
    SDL_SetRenderDrawColor(ren, 8, 11, 18, 190);
    SDL_Rect bg{bx - 6, by - 16, bw + 12 + 70, 28};
    SDL_RenderFillRect(ren, &bg);
    SDL_SetRenderDrawColor(ren, 235, 240, 250, 255);
    SDL_RenderDrawLine(ren, bx, by, bx + bw, by);
    SDL_RenderDrawLine(ren, bx, by - 4, bx, by + 4); SDL_RenderDrawLine(ren, bx + bw, by - 4, bx + bw, by + 4);
    smallText(ren, std::to_string(bar) + " CELLS", bx, by - 14, 1, SDL_Color{235, 240, 250, 255});
}

void Game::renderScrollStrip() {
    const float k = (float)L.canvas.w / World::W;
    SDL_Rect track{0, L.canvas.h - 8, L.canvas.w, 8};
    SDL_SetRenderDrawColor(ren, 8, 12, 20, 210);
    SDL_RenderFillRect(ren, &track);
    for (auto& b : phys.bodies)
        if (b.alive && !b.isStatic && (int)(&b - &phys.bodies[0]) != focusBody) {
            SDL_Rect t{(int)(b.pos.x * k), L.canvas.h - 6, 2, 4};
            SDL_SetRenderDrawColor(ren, 170, 185, 215, 255);
            SDL_RenderFillRect(ren, &t);
        }
    SDL_Rect thumb{(int)(camXf * k), L.canvas.h - 8, std::max(6, (int)(viewW() * k)), 8};
    SDL_SetRenderDrawColor(ren, 70, 100, 160, 110);
    SDL_RenderFillRect(ren, &thumb);
    SDL_SetRenderDrawColor(ren, 140, 175, 240, 255);
    SDL_RenderDrawRect(ren, &thumb);
    bool ok;
    Vec2 f = focusPoint(ok);
    if (ok) {
        SDL_Rect t{(int)(f.x * k) - 1, L.canvas.h - 8, 4, 8};
        SDL_SetRenderDrawColor(ren, 255, 214, 90, 255);
        SDL_RenderFillRect(ren, &t);
    }
    if (zoom > 1.01f) {   // zoomed in: a vertical strip on the right edge shows and moves the view up and down
        const float ky = (float)(L.canvas.h - 12) / World::H;
        SDL_Rect vt{L.canvas.w - 8, 0, 8, L.canvas.h - 12};
        SDL_SetRenderDrawColor(ren, 8, 12, 20, 210);
        SDL_RenderFillRect(ren, &vt);
        SDL_Rect vth{L.canvas.w - 8, (int)(camYf * ky), 8, std::max(6, (int)(viewH() * ky))};
        SDL_SetRenderDrawColor(ren, 70, 100, 160, 110);
        SDL_RenderFillRect(ren, &vth);
        SDL_SetRenderDrawColor(ren, 140, 175, 240, 255);
        SDL_RenderDrawRect(ren, &vth);
    }
}

void Game::renderLabels() {
    for (auto& l : labels) smallText(ren, l.s, (int)sp(l.p).x, (int)sp(l.p).y, 1, SDL_Color{200, 210, 230, 200});
}

void Game::ghostLabel(Vec2 at, const std::string& t) {
    smallText(ren, t, (int)sp(at).x + 8, (int)sp(at).y - 14, 2, SDL_Color{255, 240, 150, 255});
}

void Game::renderGhost() {
    // the measure tool: a line from the press to the pointer with its length, dx, dy and angle; the last one stays until
    // the next press or a change of tool, wherever the pointer is
    if (tool != T_MEASURE) measOn = false;
    if (measOn) {
        if (lmb) measB = snap(mouse);
        Vec2 d = measB - measA;
        SDL_Color gold{255, 214, 90, 230};
        lineWorld(measA, measB, gold, 2);
        for (Vec2 e : {measA, measB}) {   // a small cross at each end
            lineWorld(e - Vec2(1.5f, 0), e + Vec2(1.5f, 0), gold, 1);
            lineWorld(e - Vec2(0, 1.5f), e + Vec2(0, 1.5f), gold, 1);
        }
        float deg = length(d) > 1e-4f ? std::atan2(d.y, d.x) * 180.f / PI : 0.f;   // the engine's convention: positive turns clockwise on screen
        ghostLabel(measB, "L " + fmt(length(d)) + "  A " + fmt(deg) + " deg");
        SDL_FPoint q = sp(measB);
        smallText(ren, "dx " + fmt(d.x) + "  dy " + fmt(d.y), (int)q.x + 8, (int)q.y + 2, 2, SDL_Color{255, 240, 150, 255});
    }
    if (!inSim && !lmb) return;
    SDL_Color white{255, 255, 255, 170};
    Vec2 m = smouse();
    if (lmb) {   // the shape being drawn, at the pointer's size or the typed one
        Vec2 d = m - dragStart;
        switch (tool) {
            case T_BOX: case T_EMITTER: case T_FAN: {
                float w = dimValue(0, std::fabs(d.x)), h = dimValue(1, std::fabs(d.y)), ang = dimValue(2, 0.f) * PI / 180.f;
                Vec2 b = dragStart + Vec2(d.x < 0 ? -w : w, d.y < 0 ? -h : h), c = (dragStart + b) * 0.5f;
                std::vector<Vec2> pts;
                for (Vec2 q : {Vec2(-w, -h), Vec2(w, -h), Vec2(w, h), Vec2(-w, h)}) pts.push_back(c + rotate(q * 0.5f, ang));
                outlinePoly(pts, white);
                break;
            }
            case T_CIRCLE: case T_WHEEL: outlinePoly(circlePts(dragStart, dimValue(0, length(d))), white); break;
            case T_PIPE: case T_HOSE: {
                if (length(d) < 1.f) break;
                float len = dimValue(0, length(d)), dia = dimValue(1, pipeD);
                Vec2 dir = normalize(d), n = Vec2(-dir.y, dir.x) * (dia * 0.5f), b = dragStart + dir * len;
                lineWorld(dragStart + n, b + n, white, 1);
                lineWorld(dragStart - n, b - n, white, 1);
                break;
            }
            case T_CUT:
                if (cutCircle) outlinePoly(circlePts(dragStart, length(d)), SDL_Color{255, 120, 120, 200});
                else outlinePoly({dragStart, Vec2(m.x, dragStart.y), m, Vec2(dragStart.x, m.y)}, SDL_Color{255, 120, 120, 200});
                break;
            case T_ROCKET: case T_ROD: case T_SPRING: case T_SLIDER: lineWorld(dragStart, m, white, 2); break;
            default: break;
        }
    }
    if (!inSim) return;
    if (tool == T_MAT) {
        outlinePoly(circlePts(mouse, (float)brush + 0.5f, 24), SDL_Color{255, 255, 255, 110});
    } else {
        lineWorld(m + Vec2(-3, 0), m + Vec2(3, 0), white);
        lineWorld(m + Vec2(0, -3), m + Vec2(0, 3), white);
    }
}

// the smart-snap guide: the line a dragged body snapped to, in the accent colour
void Game::renderGuide() {
    if (!guide.on) return;
    SDL_Color c = rgb(ui::theme().accent, 230);
    const float pad = 8.f / sc();
    if (guide.vertical) lineWorld(Vec2(guide.coord, guide.from - pad), Vec2(guide.coord, guide.to + pad), c, 1);
    else lineWorld(Vec2(guide.from - pad, guide.coord), Vec2(guide.to + pad, guide.coord), c, 1);
}

// Everything inside the canvas, drawn with the renderer's viewport set to it so (0, 0) is the canvas corner and drawing is clipped.
void Game::renderWorld() {
    SDL_Rect r = L.canvas;
    SDL_RenderSetViewport(ren, &r);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderFillRect(ren, nullptr);
    renderParticles();
    renderGrid();
    renderBodies();
    renderJoints();
    renderArcs();
    renderSelection();
    renderFocusMark();
    renderLabels();
    renderGhost();
    renderGuide();
    renderScrollStrip();
    renderRulers();
    if (world.vMax > 0.f) {   // the electric readout, dense, at the top right of the canvas
        char eb[96];
        std::snprintf(eb, sizeof eb, "PEAK %.4g V   SOURCE %.3g A   ARCS %ld", world.vMax, world.iSource, world.arcCount);
        smallText(ren, eb, L.canvas.w - smallWidth(eb, 1) - 18, 18, 1, SDL_Color{255, 240, 140, 255});
    }
    SDL_RenderSetViewport(ren, nullptr);
}

// "Box 60x30", "Circle R10", "Wheel R6", "Rocket": a body's kind and size
std::string Game::shapeText(const Body& b) const {
    if (b.isRocket) return "Rocket";
    if (b.shape == SHAPE_BOX) return "Box " + fmt(b.half.x * 2) + "x" + fmt(b.half.y * 2);
    return std::string(b.isWheel ? "Wheel R" : "Circle R") + fmt(b.radius);
}
std::string Game::bodyName(const Body& b) const {
    std::string s = b.fan.strength != 0.f ? "Fan " + fmt(b.half.x * 2) + "x" + fmt(b.half.y * 2) : b.src.on ? "Emitter " + fmt(b.half.x * 2) + "x" + fmt(b.half.y * 2) : shapeText(b);
    return s + " " + MATS[b.mat].name;
}
std::string Game::jointName(const Joint& J) const {
    if (J.bondId >= 0) {
        for (int i = 0; i < 4; ++i) if (BOND_TEMP[i] == J.breakT) return std::string("Bond ") + BOND_NAMES[i];
        return "Bond " + fmt(J.breakT) + " C";
    }
    if (J.type == J_DISTANCE) return J.freq > 0.f ? "Spring " + fmt(J.freq) + "/s" : "Rod " + fmt(J.length);
    if (J.type == J_MOTOR) return std::string(J.keyed ? "Motor " : "Spinner ") + fmt(J.speed) + " rad/s";
    return J.type == J_SLIDER ? "Slider" : "Pin";
}
// one line about the selection: a body's shape, size, angle and material; a count (and groups) for several; a joint and its key value
std::string Game::selectionText() const {
    if (jointValid(selJoint)) return jointName(phys.joints[selJoint]);
    if (sel.empty()) return "";
    if (sel.size() == 1 && primary >= 0 && primary < (int)phys.bodies.size()) {
        const Body& b = phys.bodies[primary];
        float deg = std::fmod(b.angle * 180.f / PI, 360.f);
        if (deg > 180.f) deg -= 360.f;
        if (deg <= -180.f) deg += 360.f;
        std::string s = shapeText(b);
        if (std::fabs(deg) >= 0.05f) s += " " + fmt(deg) + " deg";
        s += std::string(" ") + MATS[b.mat].name;
        if (b.isStatic) s += " FIXED";
        return s;
    }
    std::vector<int> groups;
    for (int id : sel) {
        if (id < 0 || id >= (int)phys.bodies.size()) continue;
        int g = phys.bodies[id].group;
        if (g >= 0 && std::find(groups.begin(), groups.end(), g) == groups.end()) groups.push_back(g);
    }
    std::string s = std::to_string(sel.size()) + " bodies";
    if (!groups.empty()) s += " (" + std::to_string(groups.size()) + (groups.size() == 1 ? " group)" : " groups)");
    return s;
}
// what is under the pointer: a body's material, kind and size, or a cell's material and temperature
std::string Game::hoverText() const {
    int x = (int)mouse.x, y = (int)mouse.y;
    if (!inSim || !world.inb(x, y)) return "";
    char buf[96];
    int bid = world.bodyMask[y * World::W + x];
    if (bid >= 0 && bid < (int)phys.bodies.size() && phys.bodies[bid].alive) {
        const Body& b = phys.bodies[bid];
        std::string s = std::string(MATS[b.mat].name) + " " + shapeText(b);
        if (b.isStatic) s += " FIXED";
        if (b.group >= 0) s += " GROUP OF " + std::to_string(phys.groupMembers(b.group).size());
        if (b.fan.strength != 0.f) s += std::string(" fan ") + (b.fan.vacuum ? "vacuum " : "blow ") + fmt(std::fabs(b.fan.strength)) + "/s";
        else if (b.src.on) s += std::string(" emits ") + MATS[b.src.mat].name + " " + fmt(b.src.rate) + "/s";
        s += " " + std::to_string((int)b.temp) + " C";
        return s;
    }
    const Cell& c = world.at(x, y);
    if (c.t == M_BATT_POS || c.t == M_BATT_NEG) {
        std::snprintf(buf, sizeof buf, "%s %gV %gA", MATS[c.t].name, std::round(World::decV(c.life) * 10) / 10, std::round(World::decA(c.aux) * 1000) / 1000);
        return buf;
    }
    if (c.t == M_EMPTY) return "";
    const MatInfo& m = MATS[c.t];
    if (m.kind == K_GAS || (m.kind == K_LIQUID && c.amt > 1.01f))
        std::snprintf(buf, sizeof buf, "%s %d C amount %.2f", m.name, (int)c.temp, c.amt);
    else
        std::snprintf(buf, sizeof buf, "%s %d C%s", m.name, (int)c.temp, c.burn ? " burning" : "");
    return buf;
}
