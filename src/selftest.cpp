// Headless self-checks of the simulation: the same physical situation built from single bodies and from the
// compound (cut / grouped / welded) shapes must give the same answer. Run with: sandbots --selftest
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>
#include "physics.hpp"
#include "sand.hpp"

namespace {

struct Rig {
    World world;
    Physics phys{&world};
    void step(int n) { for (int i = 0; i < n; ++i) { phys.step(1.f / 60.f); world.step(); } }
    void gas(int x0, int y0, int x1, int y1, uint8_t t, float amt, float temp = 20.f) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                if (world.at(x, y).t == M_EMPTY && world.bodyMask[y * World::W + x] < 0) {
                    world.setCell(x, y, t);
                    world.at(x, y).amt = amt;
                    world.at(x, y).temp = temp;
                }
    }
};

int failures = 0;
void check(bool ok, const char* what, const char* detail) {
    std::printf("  [%s] %s  (%s)\n", ok ? "PASS" : "FAIL", what, detail);
    if (!ok) ++failures;
}

// 1. buoyancy: a welded plate floats where the single block of the same size floats
void buoyancy() {
    std::printf("buoyancy\n");
    float y[2];
    for (int variant = 0; variant < 2; ++variant) {
        Rig r;
        r.world.fillRect(40, 150, 200, 153, M_WALL);
        r.world.fillRect(40, 100, 41, 153, M_WALL);
        r.world.fillRect(199, 100, 200, 153, M_WALL);
        r.world.fillRect(42, 110, 198, 149, M_WATER);
        float cy = 0;
        if (variant == 0) {
            int id = r.phys.addBox(Vec2(120, 100), Vec2(14, 4), 0, M_WOOD, false);
            r.phys.stampBodies();
            r.step(500);
            for (int k = 0; k < 400; ++k) { r.step(1); cy += r.phys.bodies[id].pos.y / 400.f; }   // (the mean over a bob, not a snapshot of it)
        } else {
            std::vector<int> ids;
            for (int i = 0; i < 4; ++i) ids.push_back(r.phys.addBox(Vec2(120, 100 + (i - 1.5f) * 2.f), Vec2(14, 1), 0, M_WOOD, false));
            r.phys.groupBodies(ids);
            r.phys.stampBodies();
            r.step(500);
            for (int k = 0; k < 400; ++k) { r.step(1); for (int id : ids) cy += r.phys.bodies[id].pos.y / 1600.f; }
        }
        y[variant] = cy;
    }
    char d[96];
    std::snprintf(d, sizeof d, "single y=%.2f, welded strips y=%.2f", y[0], y[1]);
    check(std::fabs(y[0] - y[1]) < 1.5f, "grouped plate floats at the same level as the single block (to within the grid's one-cell rounding)", d);
}

// 2. gas pressure on a piston face: same force whether the piston is one box, strips, or has a pocket cut in it
float pistonForce(int variant) {
    Rig r;
    r.phys.gravity = Vec2(0, 0);
    r.world.fillRect(50, 95, 120, 98, M_WALL);
    r.world.fillRect(50, 111, 120, 114, M_WALL);
    r.world.fillRect(50, 99, 53, 110, M_WALL);
    std::vector<int> ids;
    if (variant == 0) ids.push_back(r.phys.addBox(Vec2(90, 105), Vec2(6, 5.5f), 0, M_STEEL, false));
    else if (variant == 1) {
        for (int i = 0; i < 4; ++i) ids.push_back(r.phys.addBox(Vec2(90, 105 + (i - 1.5f) * 2.75f), Vec2(6, 1.375f), 0, M_STEEL, false));
        r.phys.groupBodies(ids);
    } else {  // box with a round pocket cut into the pressurised face
        int box = r.phys.addBox(Vec2(90, 105), Vec2(6, 5.5f), 0, M_STEEL, false);
        int cutter = r.phys.addCircle(Vec2(84, 105), 3.5f, M_STEEL, false, false);
        r.phys.cutBody(box, {cutter});
        r.phys.removeBody(cutter);
        for (auto& b : r.phys.bodies) if (b.alive) ids.push_back(b.id);
    }
    r.phys.stampBodies();
    r.gas(54, 99, 90, 110, M_STEAM, 3.f);
    r.phys.step(1.f / 60.f);
    float F = 0;
    for (int id : ids) if (r.phys.bodies[id].alive) F += r.phys.bodies[id].fluidF.x;
    if (getenv("PDBG") && variant == 2) for (int id : ids) { auto& b = r.phys.bodies[id]; if (b.alive) std::printf("piece %d pos (%.2f,%.2f) half (%.2f,%.2f) Fx %.0f\n", id, b.pos.x, b.pos.y, b.half.x, b.half.y, b.fluidF.x); }
    return F;
}
void pressure() {
    std::printf("gas pressure on a piston\n");
    float f0 = pistonForce(0), f1 = pistonForce(1), f2 = pistonForce(2);
    char d[128];
    std::snprintf(d, sizeof d, "single %.0f, strips %.0f, pocketed %.0f", f0, f1, f2);
    check(std::fabs(f1 - f0) < 0.04f * std::fabs(f0), "welded strips feel the same pressure force", d);
    check(std::fabs(f2 - f0) < 0.08f * std::fabs(f0), "a pocket cut into the face does not change the net force", d);
}

// 3. heat flows along a welded bar
void conduction() {
    std::printf("conduction through a welded bar\n");
    Rig r;
    r.phys.gravity = Vec2(0, 0);
    r.world.fillRect(30, 100, 32, 104, M_HEATER);
    std::vector<int> ids;
    for (int i = 0; i < 8; ++i) ids.push_back(r.phys.addBox(Vec2(37.f + 10 * i, 102), Vec2(5, 2.5f), 0, M_COPPER, true));
    r.phys.groupBodies(ids);
    r.phys.stampBodies();
    r.step(1200);
    float near_ = r.phys.bodies[ids[0]].temp, far = r.phys.bodies[ids[7]].temp;
    char d[96];
    std::snprintf(d, sizeof d, "first piece %.0f C, last piece %.0f C", near_, far);
    check(far > 25.f && far < near_, "heat spreads down the pieces of a group", d);
}

// 4. hydraulic ratio with single and grouped pistons
float hydraulicRatio(bool grouped) {
    Rig r;
    World& w = r.world;
    w.fillRect(96, 90, 111, 225, M_STEEL);
    w.fillRect(156, 90, 187, 225, M_STEEL);
    w.fillRect(96, 210, 187, 225, M_STEEL);
    w.fillRect(100, 90, 107, 218, M_EMPTY);
    w.fillRect(160, 90, 183, 218, M_EMPTY);
    w.fillRect(100, 211, 183, 218, M_EMPTY);
    w.fillRect(100, 190, 107, 218, M_HYDRAULIC);
    w.fillRect(160, 190, 183, 218, M_HYDRAULIC);
    w.fillRect(108, 211, 159, 218, M_HYDRAULIC);
    int master = r.phys.addBox(Vec2(104, 184), Vec2(3.6f, 6), 0, M_STEEL, false);
    int slave;
    std::vector<int> slaveParts;
    if (!grouped) { slave = r.phys.addBox(Vec2(172, 184), Vec2(11.6f, 6), 0, M_STEEL, false); slaveParts = {slave}; }
    else {
        for (int i = 0; i < 3; ++i) slaveParts.push_back(r.phys.addBox(Vec2(172, 184 + (i - 1) * 4.f), Vec2(11.6f, 2), 0, M_STEEL, false));
        r.phys.groupBodies(slaveParts);
        slave = slaveParts[0];
    }
    r.phys.addSlider(master, Vec2(0, 1));
    r.phys.addSlider(slave, Vec2(0, 1));
    r.phys.stampBodies();
    r.step(30);
    float m0 = r.phys.bodies[master].pos.y, s0 = r.phys.bodies[slave].pos.y;
    for (int i = 0; i < 90; ++i) {
        r.phys.bodies[master].vel.y = 12.f;
        r.phys.step(1.f / 60.f);
        r.world.step();
    }
    float dm = r.phys.bodies[master].pos.y - m0, ds = s0 - r.phys.bodies[slave].pos.y;
    return dm > 1e-3f ? ds / dm : 0.f;
}
void hydraulics() {
    std::printf("hydraulic transmission (ideal slave/master travel = 7.2/23.2 = 0.31)\n");
    float a = hydraulicRatio(false), b = hydraulicRatio(true);
    char d[96];
    std::snprintf(d, sizeof d, "single slave %.3f, grouped slave %.3f", a, b);
    check(std::fabs(b - a) < 0.08f, "a grouped slave piston moves like the single one", d);
}

// 5. a sealed combustion chamber cut out of a block behaves like one carved from cells
struct ChamberResult { float leaked, inside, tmax; int burnt; };
ChamberResult chamber(bool cut) {
    Rig r;
    r.phys.gravity = Vec2(0, 0);
    const int cx = 100, cy = 100, R = 8;
    if (cut) {
        int block = r.phys.addBox(Vec2(cx, cy), Vec2(22, 16), 0, M_STEEL, true);
        int cutter = r.phys.addCircle(Vec2(cx, cy), R, M_STEEL, true, false);
        r.phys.cutBody(block, {cutter});
        r.phys.removeBody(cutter);
        r.phys.stampBodies();
    } else {
        r.world.fillRect(cx - 22, cy - 16, cx + 21, cy + 15, M_STEEL);
        for (int y = cy - R - 1; y <= cy + R + 1; ++y)
            for (int x = cx - R - 1; x <= cx + R + 1; ++x)
                if ((x - cx + 0.5f) * (x - cx + 0.5f) + (y - cy + 0.5f) * (y - cy + 0.5f) < (R + 0.5f) * (R + 0.5f)) r.world.at(x, y) = Cell{};
    }
    // fill the cavity with fuel vapour and light it
    int filled = 0;
    for (int y = cy - R; y <= cy + R; ++y)
        for (int x = cx - R; x <= cx + R; ++x) {
            if (r.world.at(x, y).t != M_EMPTY || r.world.bodyMask[y * World::W + x] >= 0) continue;
            float d2 = (x - cx + 0.5f) * (x - cx + 0.5f) + (y - cy + 0.5f) * (y - cy + 0.5f);
            if (d2 < (R - 1.f) * (R - 1.f)) { r.world.setCell(x, y, M_VAPOR); r.world.at(x, y).amt = 0.8f; ++filled; }
        }
    r.world.ignitePoint(cx, cy);
    ChamberResult out{0, 0, 0, 0};
    for (int f = 0; f < 240; ++f) {
        r.phys.step(1.f / 60.f);
        r.world.step();
        for (int y = cy - 14; y <= cy + 14; ++y)
            for (int x = cx - 20; x <= cx + 20; ++x) {
                const Cell& c = r.world.at(x, y);
                if (MATS[c.t].kind == K_GAS) out.tmax = std::max(out.tmax, c.temp);
            }
    }
    for (int y = 0; y < World::H; ++y)
        for (int x = 0; x < World::W; ++x) {
            const Cell& c = r.world.at(x, y);
            if (MATS[c.t].kind != K_GAS) continue;
            float d = std::sqrt((x - cx + 0.5f) * (x - cx + 0.5f) + (y - cy + 0.5f) * (y - cy + 0.5f));
            if (d <= R + 1.f) out.inside += c.amt; else out.leaked += c.amt;
        }
    out.burnt = (int)r.world.burnEvents;
    (void)filled;
    return out;
}
void combustion() {
    std::printf("combustion in a sealed chamber (cut from a body vs carved from cells)\n");
    ChamberResult a = chamber(false), b = chamber(true);
    char d[160];
    std::snprintf(d, sizeof d, "cells: leaked %.2f inside %.1f Tmax %.0f | cut body: leaked %.2f inside %.1f Tmax %.0f", a.leaked, a.inside, a.tmax, b.leaked, b.inside, b.tmax);
    check(b.leaked < 0.5f, "no gas escapes through the joints of the cut pieces", d);
    check(std::fabs(b.inside - a.inside) < 0.2f * std::max(1.f, a.inside), "the same amount of gas ends up in the chamber", d);
    check(b.tmax > 0.7f * a.tmax && b.tmax < 1.3f * a.tmax, "peak temperature matches", d);
}

// 6. a boiler: water in a sealed cavity heated through the walls (vaporisation and pressurisation)
struct BoilerResult { float steam, water, pmax, pmean, tmean; };
BoilerResult boiler(bool cut) {
    Rig r;
    r.phys.gravity = Vec2(0, 260.f);
    const int cx = 100, cy = 100;
    if (cut) {
        int block = r.phys.addBox(Vec2(cx, cy), Vec2(22, 16), 0, M_STEEL, true);
        int cutter = r.phys.addBox(Vec2(cx, cy), Vec2(16, 10), 0, M_STEEL, true);
        r.phys.cutBody(block, {cutter});
        r.phys.removeBody(cutter);
        r.phys.stampBodies();
    } else {
        r.world.fillRect(cx - 22, cy - 16, cx + 21, cy + 15, M_STEEL);
        r.world.fillRect(cx - 16, cy - 10, cx + 15, cy + 9, M_EMPTY);
    }
    r.world.fillRect(cx - 24, cy + 16, cx + 24, cy + 19, M_HEATER);   // the heater touches the underside
    for (int y = cy + 1; y <= cy + 9; ++y)
        for (int x = cx - 15; x <= cx + 14; ++x)
            if (r.world.at(x, y).t == M_EMPTY && r.world.bodyMask[y * World::W + x] < 0) r.world.setCell(x, y, M_WATER);
    BoilerResult out{0, 0, 0, 0, 0};
    for (int f = 0; f < 2400; ++f) {
        r.phys.step(1.f / 60.f);
        r.world.step();
        if (f % 30 == 0)
            for (int y = cy - 10; y < cy + 10; ++y)
                for (int x = cx - 16; x < cx + 16; ++x) {
                    const Cell& c = r.world.at(x, y);
                    if (MATS[c.t].kind == K_GAS) out.pmax = std::max(out.pmax, c.amt * (c.temp + 273.f) / 293.f);
                }
    }
    int ns = 0;
    for (int y = cy - 11; y < cy + 11; ++y)
        for (int x = cx - 17; x < cx + 17; ++x) {
            const Cell& c = r.world.at(x, y);
            if (c.t == M_STEAM) { out.steam += c.amt; out.pmean += c.amt * (c.temp + 273.f) / 293.f; out.tmean += c.temp; ++ns; }
            if (c.t == M_WATER) out.water += 1.f;
        }
    if (ns) { out.pmean /= ns; out.tmean /= ns; }
    return out;
}
void boiling() {
    std::printf("boiling in a sealed boiler (cut from a body vs carved from cells)\n");
    BoilerResult a = boiler(false), b = boiler(true);
    char d[160];
    std::snprintf(d, sizeof d, "cells: steam %.0f water %.0f mean p %.2f T %.0f | cut body: steam %.0f water %.0f mean p %.2f T %.0f", a.steam, a.water, a.pmean, a.tmean, b.steam, b.water, b.pmean, b.tmean);
    check(b.steam > 0.5f * a.steam && b.steam < 2.0f * a.steam, "steam production is the same order", d);
    check(b.pmean > 0.6f * a.pmean && b.pmean < 1.6f * a.pmean, "mean steam pressure matches", d);
}

// 7. a tight-fitting plug (cut hole + plug scaled to 98 %) seals the hole
void plug() {
    std::printf("a 98%% plug in its cut hole seals\n");
    Rig r;
    r.phys.gravity = Vec2(0, 0);
    r.world.fillRect(50, 90, 150, 93, M_WALL);
    r.world.fillRect(50, 117, 150, 120, M_WALL);
    r.world.fillRect(50, 94, 53, 116, M_WALL);
    r.world.fillRect(147, 94, 150, 116, M_WALL);
    int plate = r.phys.addBox(Vec2(100, 105), Vec2(3, 12), 0, M_STEEL, true);      // a partition across the chamber
    int ball = r.phys.addCircle(Vec2(100, 105), 6.f, M_STEEL, true, false);
    r.phys.cutBody(plate, {ball});
    r.phys.scaleBodies({ball}, 0.98f, r.phys.bodies[ball].pos);
    r.phys.stampBodies();
    r.gas(54, 94, 96, 116, M_STEAM, 3.f);
    r.step(300);
    float right = 0, left = 0;
    for (int y = 94; y <= 116; ++y)
        for (int x = 54; x <= 146; ++x) {
            const Cell& c = r.world.at(x, y);
            if (MATS[c.t].kind != K_GAS) continue;
            (x > 104 ? right : left) += c.amt;
        }
    char d[96];
    std::snprintf(d, sizeof d, "gas on the far side %.2f of %.0f total", right, left + right);
    check(right < 0.02f * (left + right), "no gas leaks past the plug", d);
}

// 8. flames burn out into smoke, never into stray materials
void flames() {
    std::printf("burnt-out flames\n");
    Rig r;
    r.phys.gravity = Vec2(0, 0);
    for (int y = 100; y < 140; ++y)
        for (int x = 100; x < 160; ++x) r.world.spawn(x, y, M_FIRE, (uint8_t)(4 + (x + y) % 10), 900.f);
    r.step(200);
    int stray = 0;
    for (int y = 0; y < World::H; ++y)
        for (int x = 0; x < World::W; ++x) {
            Kind k = MATS[r.world.at(x, y).t].kind;
            if (k == K_SOLID || k == K_POWDER || k == K_LIQUID) ++stray;
        }
    char d[64];
    std::snprintf(d, sizeof d, "%d stray solid/powder/liquid cells", stray);
    check(stray == 0, "dying flames leave no random material behind", d);
}

// 9. fans: directed airflow, pressure rise against a closed duct, wind on bodies, thrust, direction
float meanPsi(Rig& r, int x0, int x1, int y0, int y1) {
    float sum = 0; int n = 0;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const Cell& c = r.world.at(x, y);
            sum += MATS[c.t].kind == K_GAS ? c.amt * (c.temp + 273.f) / 293.f : 0.f;
            ++n;
        }
    return sum / n;
}
void fans() {
    std::printf("fans and airflow\n");
    {   // a fan in a closed duct raises the pressure ahead of it and lowers it behind, up to its stall pressure
        Rig r;
        r.phys.gravity = Vec2(0, 0);
        r.world.fillRect(40, 95, 140, 98, M_WALL);
        r.world.fillRect(40, 111, 140, 114, M_WALL);
        r.world.fillRect(40, 99, 43, 110, M_WALL);
        r.world.fillRect(137, 99, 140, 110, M_WALL);
        int fan = r.phys.addBox(Vec2(90, 105), Vec2(2, 5.5f), 0, M_STEEL, true);
        r.phys.bodies[fan].fan.strength = 80.f;
        r.phys.stampBodies();
        r.step(500);
        float ahead = meanPsi(r, 100, 134, 99, 110), behind = meanPsi(r, 46, 80, 99, 110);
        char d[128];
        std::snprintf(d, sizeof d, "pressure ahead %.2f, behind %.2f, rise %.2f (stall %.2f)", ahead, behind, ahead - behind, 0.02f * 80.f);
        check(ahead > behind + 0.3f, "the fan pumps up one side of a closed duct and draws down the other", d);
        check(ahead - behind < 0.02f * 80.f * 1.4f, "the pressure rise stops near the stall pressure", d);
    }
    for (int dir = 0; dir < 2; ++dir) {   // a puff of smoke is carried downstream, in either direction
        Rig r;
        r.phys.gravity = Vec2(0, 0);
        int fan = r.phys.addBox(Vec2(100, 105), Vec2(2, 6), 0, M_STEEL, true);
        r.phys.bodies[fan].fan.strength = dir ? -60.f : 60.f;
        r.phys.stampBodies();
        int x0 = dir ? 112 : 84;
        for (int y = 101; y <= 109; ++y) for (int x = x0; x < x0 + 4; ++x) { r.world.setCell(x, y, M_SMOKE); r.world.at(x, y).amt = 1.f; }
        auto centroid = [&]() { double sx = 0, n = 0; for (int y = 90; y < 120; ++y) for (int x = 40; x < 200; ++x) if (r.world.at(x, y).t == M_SMOKE) { sx += x * r.world.at(x, y).amt; n += r.world.at(x, y).amt; } return n ? sx / n : 0; };
        double c0 = centroid();
        r.step(90);
        double c1 = centroid();
        char d[96];
        std::snprintf(d, sizeof d, "smoke centroid x %.1f -> %.1f", c0, c1);
        check(dir ? (c1 < c0 - 8) : (c1 > c0 + 8), dir ? "a reversed fan carries smoke the other way" : "smoke behind the fan is carried through it and downstream", d);
    }
    {   // wind lifts a light body that sits in the stream, and does nothing when the fan is off
        float moved[2];
        for (int on = 0; on < 2; ++on) {
            Rig r;
            r.phys.gravity = Vec2(0, 0);
            int fan = r.phys.addBox(Vec2(60, 105), Vec2(2, 8), 0, M_STEEL, true);
            r.phys.bodies[fan].fan.strength = on ? 100.f : 0.f;
            int ball = r.phys.addBox(Vec2(80, 105), Vec2(4, 4), 0, M_WOOD, false);
            r.phys.stampBodies();
            r.step(120);
            moved[on] = r.phys.bodies[ball].pos.x - 80.f;
        }
        char d[96];
        std::snprintf(d, sizeof d, "fan off: %.1f cells, fan on: %.1f cells", moved[0], moved[1]);
        check(std::fabs(moved[0]) < 0.5f && moved[1] > 8.f, "wind pushes a light body along the stream", d);
    }
    {   // a free fan is pushed the opposite way; a rotated fan blows along its own axis
        Rig r;
        r.phys.gravity = Vec2(0, 0);
        int fan = r.phys.addBox(Vec2(100, 105), Vec2(2, 8), 0, M_ALUMINUM, false);
        r.phys.bodies[fan].fan.strength = 100.f;
        r.phys.stampBodies();
        r.step(60);
        float vx = r.phys.bodies[fan].vel.x;
        char d[64];
        std::snprintf(d, sizeof d, "vx = %.1f", vx);
        check(vx < -5.f, "a free fan recoils against its own exhaust", d);
        Rig q;
        q.phys.gravity = Vec2(0, 0);
        int f2 = q.phys.addBox(Vec2(100, 60), Vec2(2, 8), 1.5707963f, M_STEEL, true);   // axis points down
        q.phys.bodies[f2].fan.strength = 100.f;
        int ball = q.phys.addBox(Vec2(100, 85), Vec2(4, 4), 0, M_WOOD, false);
        q.phys.stampBodies();
        q.step(120);
        float dy = q.phys.bodies[ball].pos.y - 85.f;
        std::snprintf(d, sizeof d, "dy = %.1f, dx = %.1f", dy, q.phys.bodies[ball].pos.x - 100.f);
        check(dy > 8.f, "a rotated fan blows along its rotated axis", d);
    }
}

// 10. vacuum mode: draws only what is there, evacuates the intake side, accelerates the gas, pulls on bodies
void vacuumFans() {
    std::printf("vacuum fans\n");
    {   // no ambient air is created
        Rig r;
        r.phys.gravity = Vec2(0, 0);
        int fan = r.phys.addBox(Vec2(100, 105), Vec2(2, 8), 0, M_STEEL, true);
        r.phys.bodies[fan].fan.strength = 80.f; r.phys.bodies[fan].fan.vacuum = 1;
        r.phys.stampBodies();
        r.step(120);
        int air = 0;
        for (auto& c : r.world.cells) if (c.t == M_AIR) ++air;
        char d[64];
        std::snprintf(d, sizeof d, "%d air cells appeared", air);
        check(air == 0, "a vacuum fan makes no air of its own", d);
    }
    {   // a sealed chamber on the intake side is pumped down, the exhaust side is pressurised
        Rig r;
        r.phys.gravity = Vec2(0, 0);
        r.world.fillRect(40, 95, 140, 98, M_WALL);
        r.world.fillRect(40, 111, 140, 114, M_WALL);
        r.world.fillRect(40, 99, 43, 110, M_WALL);
        r.world.fillRect(137, 99, 140, 110, M_WALL);
        int fan = r.phys.addBox(Vec2(90, 105), Vec2(2, 5.5f), 0, M_STEEL, true);
        r.phys.bodies[fan].fan.strength = 80.f; r.phys.bodies[fan].fan.vacuum = 1;
        r.phys.stampBodies();
        r.gas(44, 99, 86, 110, M_AIR, 1.f);
        float before = meanPsi(r, 44, 86, 99, 110);
        r.step(500);
        float left = meanPsi(r, 44, 86, 99, 110), right = meanPsi(r, 94, 136, 99, 110);
        char d[128];
        std::snprintf(d, sizeof d, "intake side %.2f -> %.2f, exhaust side %.2f", before, left, right);
        check(left < 0.5f * before && right > left + 0.8f, "gas is pulled out of the low-pressure side and pushed into the other", d);
    }
    {   // gas hops faster the closer it is to the fan, and fastest leaving it (a blower hops at one rate)
        double rate[2][3];
        for (int vac = 0; vac < 2; ++vac) {
            Rig r;
            r.phys.gravity = Vec2(0, 0);
            r.world.fillRect(40, 95, 340, 98, M_WALL);
            r.world.fillRect(40, 111, 340, 114, M_WALL);
            r.world.fillRect(40, 99, 43, 110, M_WALL);
            r.world.fillRect(337, 99, 340, 110, M_WALL);
            int fan = r.phys.addBox(Vec2(200, 105), Vec2(2, 5.5f), 0, M_STEEL, true);
            r.phys.bodies[fan].fan.strength = 100.f; r.phys.bodies[fan].fan.vacuum = (uint8_t)vac;
            r.phys.stampBodies();
            for (int y = 99; y <= 110; ++y) for (int x = 44; x < 337; ++x) if (r.world.bodyMask[y * World::W + x] < 0) { r.world.setCell(x, y, M_SMOKE); r.world.at(x, y).amt = 1.f; }
            r.step(40);
            for (int z = 0; z < 3; ++z) rate[vac][z] = r.phys.fanHopTries[z] ? (double)r.phys.fanHopMoves[z] / r.phys.fanHopTries[z] : 0;
            std::printf("    %s: hop rate far %.2f, near %.2f, ahead %.2f\n", vac ? "vacuum" : "blower", rate[vac][0], rate[vac][1], rate[vac][2]);
        }
        char d[128];
        std::snprintf(d, sizeof d, "vacuum near/far %.2f, ahead/far %.2f | blower near/far %.2f", rate[1][1] / std::max(1e-6, rate[1][0]), rate[1][2] / std::max(1e-6, rate[1][0]), rate[0][1] / std::max(1e-6, rate[0][0]));
        check(rate[1][1] > 1.4 * rate[1][0] && rate[1][2] > rate[1][1] && std::fabs(rate[0][1] - rate[0][0]) < 0.15 * rate[0][0] + 0.02,
              "a vacuum fan draws gas faster and faster towards it and flings it out; a blower does not", d);
    }
    {   // suction draws a body towards the intake
        float moved[2];
        for (int vac = 0; vac < 2; ++vac) {
            Rig r;
            r.phys.gravity = Vec2(0, 0);
            int fan = r.phys.addBox(Vec2(120, 105), Vec2(2, 8), 0, M_STEEL, true);
            r.phys.bodies[fan].fan.strength = 100.f; r.phys.bodies[fan].fan.vacuum = (uint8_t)vac;
            int ball = r.phys.addBox(Vec2(95, 105), Vec2(3, 3), 0, M_WOOD, false);
            r.phys.stampBodies();
            r.step(90);
            moved[vac] = r.phys.bodies[ball].pos.x - 95.f;
        }
        char d[96];
        std::snprintf(d, sizeof d, "towards the fan: blower %.1f cells, vacuum %.1f cells", moved[0], moved[1]);
        check(moved[1] > 15.f && moved[1] > 2.f * moved[0], "a vacuum fan pulls a body in much harder than a blower", d);
    }
}

// bonds: a paraffin bond must hold a heavy block, give way when warmed and when overloaded
void bonds() {
    std::printf("bonds\n");
    // cold: a heavy steel block hangs from a ledge by wax for 10 seconds
    for (int variant = 0; variant < 3; ++variant) {
        Rig r;
        r.phys.addBox(Vec2(60, 60), Vec2(40, 3), 0, M_STEEL, true);
        int blk = r.phys.addBox(Vec2(60, 76), Vec2(14, 10), 0, M_STEEL, false);
        r.phys.addBond(Vec2(60, 63), blk, 0, 55.f, 0.f, 10.f);
        if (variant == 1) for (int y = 68; y <= 84; ++y) for (int x = 75; x <= 80; ++x) r.world.setCell(x, y, M_HEATER);   // touching the block
        r.phys.stampBodies();
        float y0 = r.phys.bodies[blk].pos.y;
        if (variant == 2) {   // hammer it: a lead ball dropped on the block is a brief jolt, not a failure
            int ball = r.phys.addCircle(Vec2(38, 76), 3.f, M_LEAD, false, false);
            r.phys.bodies[ball].vel = Vec2(70.f, 0.f);
        }
        r.step(600);
        float dy = r.phys.bodies[blk].pos.y - y0;
        char d[96];
        std::snprintf(d, sizeof d, "block dropped %.1f cells, %ld bonds broken", dy, r.phys.bondsBroken);
        if (variant == 0) check(dy < 1.f && r.phys.bondsBroken == 0, "a paraffin bond holds a heavy steel block indefinitely", d);
        if (variant == 1) check(dy > 20.f && r.phys.bondsBroken == 1, "the same bond lets go when the block is warmed to its melting point", d);
        if (variant == 2) check(dy < 1.f && r.phys.bondsBroken == 0, "a small jolt does not break it", d);
    }
}

// jet engine: fan + fuel injector + spark plug in a duct with a nozzle, free in space. Burning must add thrust.
void jet() {
    std::printf("jet engine\n");
    float vx[2];
    for (int lit = 0; lit < 2; ++lit) {
        Rig r;
        r.phys.gravity = Vec2(0, 0);
        const float cy = 100.f, xc = 260.f, half = 11.5f;
        std::vector<int> parts;
        auto box = [&](float u, float y, float hu, float hy, uint8_t m, float ang = 0.f) { int b = r.phys.addBox(Vec2(xc - u, y), Vec2(hu, hy), ang, m, false); parts.push_back(b); return b; };
        box(15, cy - 13, 15, 1.5f, M_ALUMINUM);
        box(15, cy + 13, 15, 1.5f, M_ALUMINUM);
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            Vec2 p0(xc - 30.f, cy + sgn * half), p1(xc - 40.f, cy + sgn * (half - 3.f)), d = p1 - p0;
            parts.push_back(r.phys.addBox((p0 + p1) * 0.5f, Vec2(length(d) * 0.5f + 1.f, 1.5f), std::atan2(d.y, d.x), M_ALUMINUM, false));
        }
        int fan = box(8, cy, 1.5f, half, M_ALUMINUM);
        r.phys.bodies[fan].fan.strength = -100.f;
        int inj = box(14, cy - half + 1.6f, 1.5f, 1.5f, M_ALUMINUM);
        r.phys.bodies[inj].src = Emitter{true, M_PROPANE, 150.f, 0.f, 0};
        box(18, cy - half + 1.f, 1.5f, 1.f, M_IGNITER);
        box(15, cy + 13.f + 5.5f, 16.f, 1.5f, M_ALUMINUM);   // the hull
        r.phys.groupBodies(parts);
        r.world.sparkPeriod = lit ? 20 : 0;
        r.phys.stampBodies();
        r.step(120);
        vx[lit] = r.phys.bodies[fan].vel.x;
    }
    char d[96];
    std::snprintf(d, sizeof d, "forward speed after 2 s: cold fan %.1f, burning %.1f cells/s", vx[0], vx[1]);
    check(vx[0] > 5.f && vx[1] > 1.3f * vx[0], "a burning jet engine drives itself forward harder than the cold fan alone", d);
}

// sliders: a body on a slider keeps to its line and its angle, and still moves freely along it
void sliders() {
    std::printf("sliders\n");
    {   // horizontal slider under gravity, pushed sideways
        Rig r;
        r.phys.addBox(Vec2(100, 150), Vec2(60, 3), 0, M_STEEL, true);    // a floor to keep the world honest
        int b = r.phys.addBox(Vec2(100, 100), Vec2(6, 6), 0, M_STEEL, false);
        r.phys.addSlider(b, Vec2(1, 0));
        r.phys.stampBodies();
        r.phys.bodies[b].vel.x = 30.f;
        r.step(120);
        char d[96];
        std::snprintf(d, sizeof d, "moved dx %.1f, dy %.2f, angle %.3f", r.phys.bodies[b].pos.x - 100.f, r.phys.bodies[b].pos.y - 100.f, r.phys.bodies[b].angle);
        check(std::fabs(r.phys.bodies[b].pos.y - 100.f) < 1.f && r.phys.bodies[b].pos.x - 100.f > 20.f && std::fabs(r.phys.bodies[b].angle) < 0.05f,
              "a horizontal slider holds the body against gravity but lets it slide", d);
    }
    {   // a slider between two bodies: the piston keeps to a line fixed in the carrier, wherever the carrier goes
        Rig r;
        int carrier = r.phys.addBox(Vec2(100, 60), Vec2(30, 3), 0.5f, M_STEEL, false);   // tilted 0.5 rad, and free to fall
        Vec2 axis(std::cos(0.5f), std::sin(0.5f));
        Vec2 side(-axis.y, axis.x);
        int piston = r.phys.addBox(Vec2(100, 60) + axis * -10.f + side * -9.f, Vec2(4, 4), 0.5f, M_STEEL, false);   // beside the carrier, not overlapping it
        r.phys.addSliderRel(piston, carrier, r.phys.bodies[piston].pos, axis);
        r.phys.bodies[carrier].w = 1.5f;   // tumbling as it falls
        r.phys.stampBodies();
        r.phys.bodies[piston].vel = axis * 40.f;
        float maxPerp = 0.f, maxAng = 0.f, slid = 0.f;
        const float perp0 = cross(axis, r.phys.bodies[piston].pos - r.phys.bodies[carrier].pos), along0 = dot(axis, r.phys.bodies[piston].pos - r.phys.bodies[carrier].pos);
        for (int i = 0; i < 90; ++i) {
            r.step(1);
            const Body& C = r.phys.bodies[carrier]; const Body& P = r.phys.bodies[piston];
            Vec2 ax = rotate(Vec2(std::cos(0.5f), std::sin(0.5f)), C.angle - 0.5f);
            Vec2 d = P.pos - C.pos;
            maxPerp = std::max(maxPerp, std::fabs(cross(ax, d) - perp0));
            maxAng = std::max(maxAng, std::fabs((P.angle - C.angle) - 0.f));
            slid = dot(ax, d) - along0;
        }
        char d[110];
        std::snprintf(d, sizeof d, "perpendicular offset %.2f, relative angle %.3f, slid to %.1f along the carrier", maxPerp, maxAng, slid);
        check(maxPerp < 1.5f && maxAng < 0.1f && slid > 5.f, "a slider between two bodies keeps the piston on the carrier's line while it tumbles", d);
    }
    {   // a slider actuator: the keys drive it along its line, it holds against gravity, and stops at its limits
        Rig r;
        int g = r.phys.addBox(Vec2(100, 100), Vec2(2, 6), 0, M_STEEL, false);
        int j = r.phys.addSlider(g, Vec2(0, -1));   // the line points up
        r.phys.setActuator(j, 10.f, 0.f, 12.f);
        r.phys.stampBodies();
        float y0 = r.phys.bodies[g].pos.y;
        r.step(60);
        float held = r.phys.bodies[g].pos.y - y0;
        r.phys.motorInput = 1.f;
        r.step(60);
        float up = y0 - r.phys.bodies[g].pos.y;
        r.step(120);
        float top = y0 - r.phys.bodies[g].pos.y;
        r.phys.motorInput = 0.f;
        r.step(60);
        float stay = y0 - r.phys.bodies[g].pos.y;
        r.phys.motorInput = -1.f;
        r.step(240);
        float back = y0 - r.phys.bodies[g].pos.y;
        char d[140];
        std::snprintf(d, sizeof d, "held %.2f (no keys), up %.1f after 1 s, %.1f at the limit (12), stayed %.1f, back to %.2f", held, up, top, stay, back);
        check(std::fabs(held) < 0.5f && up > 7.f && up < 12.5f && top > 11.f && top < 12.6f && std::fabs(stay - top) < 0.5f && std::fabs(back) < 0.6f,
              "a slider actuator moves with the keys, holds against gravity and respects its limits", d);
    }
    {   // a slider on one piece of a welded group carries the whole group along its line
        Rig r;
        int a = r.phys.addBox(Vec2(100, 100), Vec2(8, 4), 0, M_STEEL, false);
        int c = r.phys.addBox(Vec2(112, 100), Vec2(4, 8), 0, M_STEEL, false);
        r.phys.groupBodies({a, c});
        r.phys.addSlider(a, Vec2(0, 1));
        r.phys.stampBodies();
        r.step(60);
        char d[96];
        std::snprintf(d, sizeof d, "x %.2f y %.1f, other piece x %.2f", r.phys.bodies[a].pos.x, r.phys.bodies[a].pos.y, r.phys.bodies[c].pos.x);
        check(std::fabs(r.phys.bodies[a].pos.x - 100.f) < 1.f && r.phys.bodies[a].pos.y > 120.f && std::fabs(r.phys.bodies[c].pos.x - 112.f) < 1.5f,
              "a slider on a group keeps it on the line and falls along it", d);
    }
}

// gunpowder carries its own oxidiser: it burns packed solid inside a sealed chamber, and the gas it makes builds pressure
void gunpowder() {
    std::printf("gunpowder\n");
    Rig r;
    // a sealed steel box, completely full of powder except for the spark plug in one wall
    for (int y = 100; y <= 119; ++y) for (int x = 100; x <= 129; ++x) r.world.setCell(x, y, M_STEEL);
    for (int y = 103; y <= 116; ++y) for (int x = 103; x <= 126; ++x) r.world.setCell(x, y, M_GUNPOWDER);
    r.world.setCell(102, 110, M_IGNITER);
    r.world.sparkPeriod = 10;
    int before = 0; for (auto& c : r.world.cells) before += c.t == M_GUNPOWDER;
    float pmax = 0;
    for (int i = 0; i < 120; ++i) {
        r.step(1);
        for (int y = 103; y <= 116; ++y) for (int x = 103; x <= 126; ++x) { const Cell& c = r.world.cells[y * World::W + x]; if (MATS[c.t].kind == K_GAS) pmax = std::max(pmax, c.amt * (c.temp + 273.f) / 293.f); }
    }
    int after = 0; for (auto& c : r.world.cells) after += c.t == M_GUNPOWDER;
    char d[110];
    std::snprintf(d, sizeof d, "powder cells %d -> %d, peak pressure %.1f", before, after, pmax);
    check(after < before / 10 && pmax > 5.f, "gunpowder sealed in a chamber burns completely and builds pressure", d);
    // and the pressure pushes a piston down a bore
    Rig q;
    for (int x = 100; x <= 160; ++x) { for (int y = 98; y <= 101; ++y) q.world.setCell(x, y, M_STEEL); for (int y = 114; y <= 117; ++y) q.world.setCell(x, y, M_STEEL); }
    for (int y = 98; y <= 117; ++y) for (int x = 96; x <= 99; ++x) q.world.setCell(x, y, M_STEEL);
    for (int y = 102; y <= 113; ++y) for (int x = 100; x <= 109; ++x) q.world.setCell(x, y, M_GUNPOWDER);
    q.world.setCell(100, 107, M_IGNITER);
    q.world.sparkPeriod = 10;
    int piston = q.phys.addBox(Vec2(115, 107.5f), Vec2(4.f, 5.5f), 0, M_ALUMINUM, false);
    q.phys.stampBodies();
    q.step(60);
    float x1 = q.phys.bodies[piston].pos.x;
    char d2[96];
    std::snprintf(d2, sizeof d2, "the piston moved %.0f cells down the bore", x1 - 115.f);
    check(x1 > 130.f, "burning powder drives a piston down a sealed bore", d2);
}

// liquid in pipes: it flows through gas-filled channels, and steam pressure pushes a slug of water out
void liquids() {
    std::printf("liquids\n");
    {   // 1. a puddle in a steam-filled horizontal channel spreads out along it
        Rig r;
        for (int x = 100; x <= 160; ++x) { r.world.setCell(x, 99, M_STEEL); r.world.setCell(x, 106, M_STEEL); }
        r.world.setCell(99, 100, M_STEEL); r.world.setCell(161, 100, M_STEEL);
        for (int y = 100; y <= 105; ++y) for (int x = 100; x <= 160; ++x) { r.world.setCell(x, y, M_STEEL); }
        for (int x = 100; x <= 160; ++x) for (int y = 103; y <= 105; ++y) r.world.setCell(x, y, M_EMPTY);   // channel 3 high
        for (int y = 103; y <= 105; ++y) { r.world.setCell(99, y, M_STEEL); r.world.setCell(161, y, M_STEEL); }
        r.gas(100, 103, 160, 105, M_STEAM, 0.6f, 105.f);
        for (int y = 104; y <= 105; ++y) for (int x = 100; x <= 109; ++x) { r.world.setCell(x, y, M_WATER); }
        r.step(240);
        int minx = 999, maxx = -1; for (int y = 103; y <= 105; ++y) for (int x = 100; x <= 160; ++x) if (r.world.at(x, y).t == M_WATER) { minx = std::min(minx, x); maxx = std::max(maxx, x); }
        char d[96]; std::snprintf(d, sizeof d, "20 cells of water spread over x %d..%d", minx, maxx);
        check(maxx - minx > 15, "a puddle spreads along a channel full of steam instead of sitting in a heap", d);
    }
    {   // 2. a slug of water in a pipe is blown out by the steam pressure behind it
        Rig r;
        for (int x = 100; x <= 200; ++x) { for (int y = 100; y <= 102; ++y) r.world.setCell(x, y, M_STEEL); for (int y = 107; y <= 109; ++y) r.world.setCell(x, y, M_STEEL); }
        for (int y = 100; y <= 109; ++y) for (int x = 96; x <= 99; ++x) r.world.setCell(x, y, M_STEEL);   // closed behind
        r.gas(100, 103, 120, 106, M_AIR, 3.0f, 20.f);    // compressed gas behind...
        for (int y = 103; y <= 106; ++y) for (int x = 121; x <= 126; ++x) r.world.setCell(x, y, M_WATER);   // ...a slug of water...
        r.step(200);                                                                                       // ...and open pipe in front
        int stay = 0, n = 0; for (int y = 103; y <= 106; ++y) for (int x = 100; x <= 200; ++x) if (r.world.at(x, y).t == M_WATER) { ++n; if (x <= 130) ++stay; }
        char d[96]; std::snprintf(d, sizeof d, "of 24 cells, %d still within 5 cells of where the slug started (%d left in the pipe)", stay, n);
        check(stay <= 14, "gas pressure behind a slug of water blows most of it down the pipe", d);
    }
}

// emitters: a single-sided outlet puts its material out of that side only, thrown a few cells along the normal
void emitters() {
    std::printf("emitters\n");
    for (int variant = 0; variant < 3; ++variant) {
        Rig r;
        r.phys.gravity = Vec2(0, 0);   // so the cells stay where they appear
        float ang = variant == 1 ? 1.5707963f : 0.f;   // variant 1: the block turned a quarter turn, so its right side faces down
        int e = r.phys.addBox(Vec2(150.f, 150.f), Vec2(4.f, 3.f), ang, M_STEEL, true);
        r.phys.bodies[e].src = Emitter{true, M_WATER, 120.f, 0.f, (uint8_t)(variant == 2 ? 0 : 1)};
        r.phys.stampBodies();
        for (int i = 0; i < 30; ++i) r.phys.step(1.f / 60.f);   // the physics step runs the emitters; leaving the grid frozen shows where the cells appear
        int good = 0, total = 0, maxd = 0;
        for (int y = 130; y <= 170; ++y) for (int x = 130; x <= 170; ++x) if (r.world.at(x, y).t == M_WATER && r.world.bodyMask[y * World::W + x] < 0) {
            ++total;
            bool right = x >= 154 && std::abs(y - 150) <= 3, down = y >= 153 && std::abs(x - 150) <= 4;
            if (variant == 0 ? right : variant == 1 ? down : false) ++good;
            maxd = std::max(maxd, std::max(std::abs(x - 150), std::abs(y - 150)));
        }
        char d[110]; std::snprintf(d, sizeof d, "%d cells out, %d of them in front of the outlet, the furthest %d cells from the block's centre", total, good, maxd);
        if (variant == 0) check(total > 10 && good == total && maxd >= 8, "a right-hand outlet puts everything out of the right-hand side, thrown several cells", d);
        if (variant == 1) check(total > 10 && good == total, "the outlet turns with the block", d);
        if (variant == 2) std::printf("  [info] all-sides emitter for comparison: %s\n", d);
    }
}

// robustness fixes from the adversarial review of the rigid-body engine
void rigidReview() {
    std::printf("rigid-body review fixes\n");
    {   // a perfectly aligned stack of boxes stands
        Rig r;
        r.world.fillRect(0, 200, 400, 203, M_WALL);
        for (int i = 0; i < 8; ++i) r.phys.addBox(Vec2(300, 195.f - 10.f * i), Vec2(5, 5), 0, M_STEEL, false);
        r.phys.stampBodies();
        r.step(600);
        float dx = 0; for (auto& b : r.phys.bodies) if (b.alive) dx = std::max(dx, std::fabs(b.pos.x - 300.f));
        char d[96]; std::snprintf(d, sizeof d, "largest sideways drift %.1f cells after 10 s", dx);
        check(dx < 6.f, "a stack of eight steel boxes stays standing", d);
    }
    {   // static friction is the friction coefficient, not that plus a sleep threshold
        Rig r;
        const float ang = std::atan(0.25f);   // a slope steeper than steel's friction (0.12)
        r.phys.addBox(Vec2(300, 150), Vec2(80, 3), ang, M_STEEL, true);
        Vec2 up(-std::sin(ang), -std::cos(ang));
        int b = r.phys.addBox(Vec2(300, 150) + up * 8.f, Vec2(5, 5), ang, M_STEEL, false);
        r.phys.stampBodies();
        Vec2 p0 = r.phys.bodies[b].pos;
        r.step(120);
        float moved = length(r.phys.bodies[b].pos - p0);
        char d[96]; std::snprintf(d, sizeof d, "moved %.1f cells in 2 s", moved);
        check(moved > 30.f, "a steel box slides down a slope steeper than its friction", d);
    }
    {   // a box embedded in the floor is eased out, not flung
        Rig r;
        r.world.fillRect(0, 200, 400, 210, M_WALL);
        int b = r.phys.addBox(Vec2(300, 195.f + 4.f), Vec2(5, 5), 0, M_WOOD, false);
        r.phys.stampBodies();
        float top = 1e9f;
        for (int i = 0; i < 120; ++i) { r.step(1); top = std::min(top, r.phys.bodies[b].pos.y); }
        char d[96]; std::snprintf(d, sizeof d, "rose to y=%.1f from 199 (resting height 195)", top);
        check(top > 190.f, "overlap with the floor is corrected without launching the body", d);
    }
    {   // a fan jammed against a wall, or with nothing to move, does not push
        Rig r;
        r.phys.gravity = Vec2(0, 0);
        r.world.fillRect(308, 80, 330, 120, M_WALL);
        int f = r.phys.addBox(Vec2(300, 100), Vec2(4, 6), 0, M_STEEL, false);
        r.phys.bodies[f].fan.strength = 300.f;
        r.phys.stampBodies();
        r.step(60);
        char d[96]; std::snprintf(d, sizeof d, "velocity %.1f cells/s", r.phys.bodies[f].vel.x);
        check(std::fabs(r.phys.bodies[f].vel.x) < 12.f, "a fan whose exhaust is walled off does not accelerate", d);
    }
    {   // moving a body does not change a spring's natural length
        Rig r;
        int c = r.phys.addBox(Vec2(100, 60), Vec2(5, 5), 0, M_STEEL, true);
        int a = r.phys.addBox(Vec2(100, 100), Vec2(5, 5), 0, M_STEEL, false);
        int j = r.phys.addDistance(c, Vec2(100, 60), a, Vec2(100, 100), 2.f);
        float len0 = r.phys.joints[j].length;
        r.phys.translateBodies({a}, Vec2(0, 3));
        r.phys.translateBodies({a}, Vec2(0, 3));
        char d[96]; std::snprintf(d, sizeof d, "rest length %.2f -> %.2f", len0, r.phys.joints[j].length);
        check(std::fabs(r.phys.joints[j].length - len0) < 1e-4f, "dragging a spring's end leaves its rest length alone", d);
    }
    {   // emitters can exceed 180 cells a second
        Rig r;
        r.world.fillRect(0, 200, 400, 203, M_WALL);
        int e = r.phys.addBox(Vec2(300, 150), Vec2(2, 2), 0, M_STEEL, true);
        r.phys.bodies[e].src = Emitter{true, M_SAND, 600.f, 0.f, 0};
        r.phys.stampBodies();
        r.step(60);
        int sand = 0; for (auto& c : r.world.cells) sand += c.t == M_SAND;
        char d[96]; std::snprintf(d, sizeof d, "%d grains in a second at a rate of 600", sand);
        check(sand > 330, "an emitter set to 600 cells a second delivers about that", d);
    }
    {   // a bond is both pins or neither; a zero-size body is not NaN; a damaged save does not crash
        Rig r;
        int a = r.phys.addBox(Vec2(100, 100), Vec2(10, 5), 0, M_STEEL, true);
        int b = r.phys.addBox(Vec2(100, 110), Vec2(10, 5), 0, M_STEEL, false);
        r.phys.addBond(Vec2(100, 105), a, b, 55.f, 0.f, 10.f);
        int alive0 = 0; for (auto& j : r.phys.joints) alive0 += j.alive;
        r.phys.removeJoint(0);
        int alive1 = 0; for (auto& j : r.phys.joints) alive1 += j.alive;
        int z = r.phys.addBox(Vec2(100, 130), Vec2(0, 0), 0, M_STEEL, false);
        r.phys.stampBodies();
        r.step(10);
        bool finite = r.phys.bodies[z].alive && std::isfinite(r.phys.bodies[z].pos.y) && std::isfinite(r.phys.bodies[b].pos.y);
        r.phys.joints[0].alive = true; r.phys.joints[0].a = 99;   // a joint pointing at a body that is not there
        std::vector<uint8_t> buf; Writer w{buf}; r.phys.save(w);
        Rig q; Reader rd(buf);
        bool ok = q.phys.load(rd);
        q.step(5);   // would have crashed
        char d[110]; std::snprintf(d, sizeof d, "bond pins %d -> %d after erasing one; zero-size body finite: %d; damaged save loaded: %d and ran", alive0, alive1, (int)finite, (int)ok);
        check(alive0 == 2 && alive1 == 0 && finite && ok, "bond erase, zero-size body and damaged save are all handled", d);
    }
}

// fixes from the adversarial review of the grid engine
void gridReview() {
    std::printf("grid review fixes\n");
    {   // fire spreads at the fuel's burn speed, not through a whole structure in a frame
        Rig r;
        for (int x = 100; x < 300; ++x) r.world.setCell(x, 100, M_WOOD);
        r.world.flashAt(200, 100);
        r.step(5);
        int burning = 0; for (int x = 100; x < 300; ++x) burning += r.world.at(x, 100).burn > 0 || r.world.at(x, 100).t != M_WOOD;
        char d[96]; std::snprintf(d, sizeof d, "%d of 200 cells alight after 5 frames", burning);
        check(burning < 25, "a burning plank does not light end to end in a few frames", d);
    }
    {   // fuel doused by water stops burning and is not endlessly relit
        Rig r;
        r.world.setCell(200, 100, M_WOOD);
        for (int d = 0; d < 4; ++d) { static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1}; if (d != 3) r.world.setCell(200 + dx[d], 100 + dy[d], M_WATER); }
        r.world.flashAt(200, 100);
        long ev0 = r.world.burnEvents;
        r.step(600);
        long ev = r.world.burnEvents - ev0;
        char d[96]; std::snprintf(d, sizeof d, "%ld ignitions in 600 frames; the wood cell is now material %d with burn %d", ev, (int)r.world.at(200, 100).t, (int)r.world.at(200, 100).burn);
        check(ev < 20, "wood beside water is put out, not relit every other frame", d);
    }
    {   // thin gas inside a sealed chamber is kept
        Rig r;
        for (int x = 100; x <= 160; ++x) for (int y = 100; y <= 130; ++y) if (x == 100 || x == 160 || y == 100 || y == 130) r.world.setCell(x, y, M_WALL);
        r.world.setCell(130, 115, M_PROPANE); r.world.at(130, 115).amt = 20.f;
        r.step(300);
        double tot = 0; for (int y = 101; y < 130; ++y) for (int x = 101; x < 160; ++x) if (MATS[r.world.at(x, y).t].kind == K_GAS) tot += r.world.at(x, y).amt;
        char d[96]; std::snprintf(d, sizeof d, "%.1f of 20 left after 5 s", tot);
        check(tot > 17.f, "a thin gas leak into a sealed room does not evaporate", d);
    }
    {   // pressure crosses the boundary between two different gases
        Rig r;
        for (int x = 100; x <= 160; ++x) for (int y = 100; y <= 110; ++y) if (x == 100 || x == 160 || y == 100 || y == 110) r.world.setCell(x, y, M_WALL);
        r.gas(101, 101, 130, 109, M_EXHAUST, 3.0f, 20.f);
        r.gas(131, 101, 159, 109, M_SMOKE, 0.3f, 20.f);
        auto mass = [&](int x0, int x1) { double m = 0; for (int y = 101; y < 110; ++y) for (int x = x0; x <= x1; ++x) m += r.world.at(x, y).amt; return m; };
        double l0 = mass(101, 130), r0 = mass(131, 159);
        r.step(400);
        double l1 = mass(101, 130), r1 = mass(131, 159);
        char d[110]; std::snprintf(d, sizeof d, "left/right mass %.0f/%.0f -> %.0f/%.0f (even would be about %.0f/%.0f)", l0, r0, l1, r1, (l0 + r0) * 0.5, (l0 + r0) * 0.5);
        check(l1 - r1 < 0.55 * (l0 - r0), "two different gases in a sealed box come to the same pressure", d);
    }
    {   // a heavy gas does not sink into water
        Rig r;
        for (int x = 100; x <= 140; ++x) for (int y = 100; y <= 140; ++y) if (x == 100 || x == 140 || y == 100 || y == 140) r.world.setCell(x, y, M_WALL);
        r.world.fillRect(101, 121, 139, 139, M_WATER);
        r.gas(101, 101, 139, 118, M_PROPANE, 1.0f, 20.f);
        r.step(300);
        int in = 0; for (int y = 123; y < 140; ++y) for (int x = 101; x < 140; ++x) in += r.world.at(x, y).t == M_PROPANE;
        char d[96]; std::snprintf(d, sizeof d, "%d propane cells below the surface", in);
        check(in <= 2, "propane stays above a pool of water", d);
    }
    {   // a puff of gas in open space does not drift to one side
        Rig r;
        r.world.setCell(500, 100, M_AIR); r.world.at(500, 100).amt = 200.f;
        r.step(20);
        double sx = 0, n = 0; for (int y = 60; y < 140; ++y) for (int x = 460; x < 540; ++x) if (r.world.at(x, y).t == M_AIR) { sx += x * r.world.at(x, y).amt; n += r.world.at(x, y).amt; }
        char d[96]; std::snprintf(d, sizeof d, "centre of the puff at x=%.2f (started at 500)", sx / n);
        check(std::fabs(sx / n - 500.0) < 0.15, "a puff of gas spreads evenly in every direction", d);
    }
    {   // heat spreads the same way in both directions
        Rig r;
        for (int y = 90; y <= 110; ++y) for (int x = 90; x <= 110; ++x) r.world.setCell(x, y, M_STEEL);
        r.world.at(100, 100).temp = 400.f;
        double plus = 0, minus = 0;
        for (int f = 0; f < 12; ++f) { r.step(1); plus += r.world.at(103, 100).temp; minus += r.world.at(97, 100).temp; }
        char d[96]; std::snprintf(d, sizeof d, "mean rise %.2f to the right, %.2f to the left", plus / 12 - 20, minus / 12 - 20);
        check(std::fabs(plus - minus) < 0.04 * (plus + minus - 40 * 12) + 0.3, "heat conducts equally to the left and right", d);
    }
    {   // a current through a conducting body heats it
        Rig r;
        r.world.battV = 20.f; r.world.battA = 200.f;
        int b = r.phys.addBox(Vec2(110, 102), Vec2(10, 2), 0, M_STEEL, true);
        for (int y = 100; y <= 104; ++y) { r.world.setCell(99, y, M_BATT_POS); r.world.setCell(121, y, M_BATT_NEG); }
        r.phys.stampBodies();
        r.step(240);
        char d[96]; std::snprintf(d, sizeof d, "the steel bar is at %.0f C", r.phys.bodies[b].temp);
        check(r.phys.bodies[b].temp > 30.f, "a steel bar carrying current warms up", d);
    }
    {   // damaged cell data and identical worlds
        Rig a, b;
        for (int x = 100; x < 140; ++x) { a.world.setCell(x, 100, M_WATER); b.world.setCell(x, 100, M_WATER); }
        std::vector<uint8_t> ba, bb; Writer wa{ba}, wb{bb};
        a.world.save(wa); b.world.save(wb);
        bool same = ba == bb;
        std::vector<uint8_t> bad = ba;
        // the first cell's material byte sits right after the vector's element count
        bad[sizeof(uint32_t)] = 200;
        Rig c; Reader rd(bad);
        bool refused = !c.world.load(rd);
        char d[96]; std::snprintf(d, sizeof d, "identical worlds save identical bytes: %d; a cell of material 200 is refused: %d", (int)same, (int)refused);
        check(same && refused, "saving is deterministic and a damaged cell cannot be loaded", d);
    }
}

}  // namespace

int runSelfTests() {
    buoyancy();
    pressure();
    conduction();
    hydraulics();
    combustion();
    boiling();
    plug();
    flames();
    fans();
    vacuumFans();
    bonds();
    jet();
    sliders();
    gunpowder();
    liquids();
    emitters();
    rigidReview();
    gridReview();
    std::printf("%s (%d failing)\n", failures ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", failures);
    return failures ? 1 : 0;
}
