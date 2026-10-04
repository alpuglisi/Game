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
            cy = r.phys.bodies[id].pos.y;
        } else {
            std::vector<int> ids;
            for (int i = 0; i < 4; ++i) ids.push_back(r.phys.addBox(Vec2(120, 100 + (i - 1.5f) * 2.f), Vec2(14, 1), 0, M_WOOD, false));
            r.phys.groupBodies(ids);
            r.phys.stampBodies();
            r.step(500);
            for (int id : ids) cy += r.phys.bodies[id].pos.y / 4.f;
        }
        y[variant] = cy;
    }
    char d[96];
    std::snprintf(d, sizeof d, "single y=%.2f, welded strips y=%.2f", y[0], y[1]);
    check(std::fabs(y[0] - y[1]) < 1.0f, "grouped plate floats at the same level as the single block", d);
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
    for (int f = 0; f < 900; ++f) {
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
    std::printf("%s (%d failing)\n", failures ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", failures);
    return failures ? 1 : 0;
}
