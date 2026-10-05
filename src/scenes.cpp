// The ready-made scenes (Demo, engines, hydraulics, ...) and the builders of the headless test scenes.
#include "app.hpp"

int Game::spawnCar(Vec2 c, float scale) {
    int chassis = phys.addBox(c, Vec2(24 * scale, 5 * scale), 0, M_ALUMINUM, false);
    for (int i = -1; i <= 1; i += 2) {
        Vec2 wc = c + Vec2(16.f * scale * i, 6.f * scale);
        int w = phys.addCircle(wc, 9.f * scale, M_RUBBER, false, true);
        phys.addPin(wc, chassis, w, true, true);
    }
    return chassis;
}

void Game::rect(int x0, int y0, int x1, int y1, uint8_t m, uint8_t pl) { world.fillRect(x0, y0, x1, y1, m, pl); }

void Game::gasRect(int x0, int y0, int x1, int y1, uint8_t m, float amt, float temp) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            if (world.at(x, y).t != M_EMPTY) continue;
            world.setCell(x, y, m);
            world.at(x, y).amt = amt;
            if (temp > -1e8f) world.at(x, y).temp = temp;
        }
}

void Game::warm(int x0, int y0, int x1, int y1, float T) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (world.inb(x, y) && world.at(x, y).t != M_EMPTY) world.at(x, y).temp = T;
}

void Game::label(float x, float y, const std::string& s) { labels.push_back({Vec2(x, y), s}); }

void Game::buildDemo() {
    resetWorld();
    rect(10, 160, 13, 239, M_WALL);
    rect(10, 236, 100, 239, M_WALL);
    rect(97, 160, 100, 239, M_WALL);
    rect(14, 190, 96, 235, M_WATER);
    for (int x = 0; x < 70; ++x) rect(120 + x, 235 - x / 4, 120 + x, 239, M_STONE);
    rect(190, 217, World::W - 1, 239, M_STONE);
    rect(330, 180, 345, 216, M_WOOD);
    rect(350, 205, 358, 216, M_TNT);
    rect(150, 40, 175, 70, M_SAND);
    rect(20, 120, 50, 140, M_GASOLINE);
    spawnCar(Vec2(235, 205));
    for (int i = 0; i < 3; ++i) phys.addBox(Vec2(300, 210.f - i * 12.f), Vec2(6, 6), 0, M_WOOD, false);
    phys.addCircle(Vec2(60, 90), 8, M_PLASTIC, false, false);
    label(14, 150, "SCENES TAB: STEAM / GAS ENGINES, HYDRAULICS, CONDUCTION, FUELS");
    label(330, 170, "TNT");
    phys.stampBodies();
}

void Game::cylinder(int headX, int len, uint8_t wall) {
    rect(headX - 3, 146, headX + len - 1, 165, wall);
    rect(headX, 150, headX + len - 1, 161, M_EMPTY);
    rect(headX + len - 12, 146, headX + len - 1, 149, wall);   // keep the ceiling over the piston's travel
    rect(headX + len, 150, headX + len + 1, 161, M_VOID);       // drain: keeps the space behind the piston at vacuum
}

int Game::crankSlider(int headX, float pistonLen, float rod, float crankY, float crankR, float wheelR, uint8_t wheelMat) {
    const float boreY0 = 150, boreH = 12;
    float axisY = boreY0 + boreH * 0.5f;
    float crankX = headX + 1 + pistonLen * 0.5f + rod + crankR;
    float th = PI + 0.7f;
    Vec2 pin = Vec2(crankX, crankY) + Vec2(std::cos(th), std::sin(th)) * crankR;
    float px = pin.x - std::sqrt(rod * rod - (pin.y - axisY) * (pin.y - axisY));
    int piston = phys.addBox(Vec2(px, axisY), Vec2(pistonLen * 0.5f, boreH * 0.5f - 0.5f), 0, M_ALUMINUM, false);
    int wheel = phys.addCircle(Vec2(crankX, crankY), wheelR, wheelMat, false, false);
    phys.addPin(Vec2(crankX, crankY), wheel, -1, false, true);
    phys.addDistance(piston, Vec2(px, axisY), wheel, pin, 0.f);
    phys.addSlider(piston, Vec2(1, 0));
    phys.bodies[wheel].w = 7.f;  // starter motor: a kick to carry the crank past dead centre
    lastWheel = wheel;
    lastCrank = Vec2(crankX, crankY);
    return piston;
}

void Game::gateValve(uint8_t wall, float phase) {
    const float gateHalf = 10.f, ecc = 8.f, restX = 96.f;
    rect(74, 134, 122, 149, wall);
    rect(76, 136, 120, 145, M_EMPTY);       // tall chest: steam flows over the gate, so the gate is pressure-balanced
    rect(92, 146, 97, 149, M_EMPTY);        // port: x 92..97
    float crankX = lastCrank.x, crankY = lastCrank.y;
    float rod = crankX - restX;
    float ang = phase;   // eccentric phase: 0 = open from top dead centre to ~80 degrees after it; PI = open around bottom dead centre
    Vec2 pin = Vec2(crankX, crankY) + Vec2(std::cos(ang), std::sin(ang)) * ecc;
    float gy = 145.0f;
    float gx = pin.x - std::sqrt(rod * rod - (pin.y - gy) * (pin.y - gy));
    int gate = phys.addBox(Vec2(gx, gy), Vec2(gateHalf, 1.0f), 0, M_STEEL, false);
    phys.addDistance(gate, Vec2(gx, gy), lastWheel, pin, 0.f);
    phys.addSlider(gate, Vec2(1, 0));
}

void Game::exhaustValve(uint8_t wall) {
    const float gateHalf = 9.f, ecc = 8.f, restX = 102.f;
    rect(74, 166, 122, 175, wall);
    rect(76, 168, 120, 170, M_EMPTY);       // chest interior (3 high)
    rect(96, 162, 101, 167, M_EMPTY);       // port through the cylinder floor: x 96..101
    rect(74, 168, 75, 170, M_VOID);         // drains at both ends of the chest
    rect(121, 168, 122, 170, M_VOID);
    float crankX = lastCrank.x, crankY = lastCrank.y;
    float rod = crankX - restX;
    float ang = 2.58f;                      // window centred about 250 degrees after top dead centre
    Vec2 pin = Vec2(crankX, crankY) + Vec2(std::cos(ang), std::sin(ang)) * ecc;
    float gy = 170.0f;
    float gx = pin.x - std::sqrt(rod * rod - (pin.y - gy) * (pin.y - gy));
    int gate = phys.addBox(Vec2(gx, gy), Vec2(gateHalf, 1.0f), 0, M_STEEL, false);
    phys.addDistance(gate, Vec2(gx, gy), lastWheel, pin, 0.f);
    phys.addSlider(gate, Vec2(1, 0));
}

void Game::buildSteamEngine() {
    resetWorld();
    // boiler (steel) with water, heated from below through a copper floor
    rect(20, 150, 72, 205, M_STEEL);
    rect(23, 153, 69, 202, M_EMPTY);
    rect(23, 178, 69, 202, M_WATER);
    rect(30, 208, 52, 216, M_HEATER);               // a modest burner under part of the floor, so the water lasts
    rect(23, 203, 69, 207, M_COPPER);
    // insulated (ceramic) steam line: boiler -> riser -> valve chest
    rect(70, 150, 85, 164, M_CERAMIC);
    cylinder(91, 60, M_CERAMIC);
    rect(118, 162, 129, 165, M_EMPTY);       // exhaust port in the floor, draining into a VOID sink
    rect(118, 166, 129, 167, M_VOID);
    crankSlider(91, 16, 100, 156, 18, 24, M_IRON);
    rect(74, 134, 85, 164, M_CERAMIC);
    gateValve(M_CERAMIC);
    rect(76, 136, 83, 162, M_EMPTY);          // riser into the chest
    rect(70, 153, 83, 162, M_EMPTY);          // steam line (carved last so nothing walls it off)
    warm(18, 136, 160, 206, 105.f);              // a warmed-up engine: cold walls would just condense the steam
    warm(23, 178, 69, 202, 96.f);
    // feedwater: an outlet in the boiler wall at the working water level. When the water reaches it the outlet is blocked
    // and feeding stops, like a float valve, so the boiler never overfills into the steam line
    int feed = phys.addBox(Vec2(25.5f, 177.f), Vec2(1.5f, 1.5f), 0, M_STEEL, true);
    phys.bodies[feed].src = Emitter{true, M_WATER, 150.f, 0.f, 1};
    for (auto& b : phys.bodies) if (b.alive) b.temp = 105.f;
    rect(0, 230, World::W - 1, 239, M_CONCRETE);
    label(22, 140, "BOILER");
    label(78, 124, "VALVE CHEST + GATE");
    label(112, 170, "EXHAUST PORT + DRAIN");
    label(196, 118, "FLYWHEEL");
    label(24, 222, "BURNER");
    label(26, 186, "FEEDWATER (STOPS AT THE WORKING LEVEL)");
    phys.stampBodies();
}

void Game::buildGasEngine() {
    resetWorld();
    cylinder(91, 60, M_STEEL);
    crankSlider(91, 16, 100, 156, 18, 24, M_IRON);
    gateValve(M_STEEL);
    exhaustValve(M_STEEL);
    // The supply wall: alternate cells of gasoline vapour (0.8) and air (1.0), a carburettor of sorts. The charge enters the chest
    // already mixed, about 2.5 air per unit of vapour, and the bore gets fuel beside air at every port opening, so the plug can
    // light it. A wall of vapour beside a wall of air only mixes by diffusion, and denser air simply crowds the vapour out.
    for (int y = 136; y <= 145; ++y) {
        const bool air = y & 1;
        world.sourceAmt = air ? 1.f : 0.8f;
        rect(76, y, 76, y, M_SOURCE, air ? M_AIR : M_VAPOR);
    }
    rect(90, 152, 90, 156, M_IGNITER);                     // spark plug set into the head wall
    sparkIdx = 5;
    world.sparkPeriod = SPARK_RATES[sparkIdx];
    rect(0, 230, World::W - 1, 239, M_CONCRETE);
    label(50, 124, "GASOLINE VAPOUR + AIR SUPPLY");
    label(78, 118, "GATE VALVE (DRIVEN BY ECCENTRIC)");
    label(60, 168, "SPARK PLUG");
    label(196, 118, "FLYWHEEL (STARTED SPINNING)");
    label(76, 178, "EXHAUST GATE VALVE + DRAIN");
    phys.stampBodies();
}

void Game::buildDieselEngine() {
    resetWorld();
    cylinder(91, 60, M_STEEL);
    crankSlider(91, 16, 100, 156, 18, 24, M_IRON);
    gateValve(M_STEEL);
    exhaustValve(M_STEEL);
    for (int y = 136; y <= 145; ++y) {   // the supply wall: alternate cells of diesel vapour and air (diesel wants 3 air per unit), see the gas engine
        const bool air = y & 1;
        world.sourceAmt = air ? 1.2f : 0.8f;
        rect(76, y, 76, y, M_SOURCE, air ? M_AIR : M_VAPOR);
        if (!air) world.at(76, y).aux = M_DIESEL;
    }
    rect(90, 152, 90, 156, M_HEATER);                      // glow plug: diesel needs heat, not a spark, to light
    sparkIdx = 0;
    world.sparkPeriod = 0;
    rect(0, 230, World::W - 1, 239, M_CONCRETE);
    label(50, 124, "DIESEL VAPOUR + AIR SUPPLY");
    label(78, 118, "GATE VALVE (DRIVEN BY ECCENTRIC)");
    label(60, 168, "GLOW PLUG");
    label(196, 118, "FLYWHEEL (STARTED SPINNING)");
    label(76, 178, "EXHAUST GATE VALVE + DRAIN");
    phys.stampBodies();
}

void Game::buildHydraulics() {
    resetWorld();
    // master (narrow) and slave (wide) cylinders joined by a pipe, filled with hydraulic fluid
    rect(96, 90, 111, 225, M_STEEL);
    rect(156, 90, 187, 225, M_STEEL);
    rect(96, 210, 187, 225, M_STEEL);
    rect(100, 90, 107, 218, M_EMPTY);
    rect(160, 90, 183, 218, M_EMPTY);
    rect(100, 211, 183, 218, M_EMPTY);
    rect(100, 190, 107, 218, M_HYDRAULIC);
    rect(160, 190, 183, 218, M_HYDRAULIC);
    rect(108, 211, 159, 218, M_HYDRAULIC);
    phys.addBox(Vec2(104, 184), Vec2(3.6f, 6), 0, M_STEEL, false);          // master piston
    int slave = phys.addBox(Vec2(172, 184), Vec2(11.6f, 6), 0, M_STEEL, false);
    int load = phys.addBox(Vec2(172, 166), Vec2(10, 12), 0, M_LEAD, false);
    (void)slave; (void)load;
    // motor-driven crank on the master piston
    int wheel = phys.addCircle(Vec2(104, 130), 14, M_IRON, false, false);
    phys.addPin(Vec2(104, 130), wheel, -1, true, false);
    phys.joints.back().speed = 1.5f;
    int mp = 0;
    for (auto& b : phys.bodies) if (b.alive && b.half.x > 3.5f && b.half.x < 3.7f) mp = b.id;
    phys.addDistance(wheel, Vec2(118, 130), mp, Vec2(104, 178), 0.f);
    phys.addSlider(mp, Vec2(0, 1));
    phys.addSlider(slave, Vec2(0, 1));
    rect(0, 230, World::W - 1, 239, M_CONCRETE);
    label(70, 76, "MASTER 8 WIDE");
    label(150, 76, "SLAVE 24 WIDE: 3X FORCE");
    label(112, 206, "HYDRAULIC FLUID");
    phys.stampBodies();
}

void Game::buildConduction() {
    resetWorld();
    heatView = true;
    static const uint8_t mats[] = {M_COPPER, M_ALUMINUM, M_GOLD, M_IRON, M_STEEL, M_LEAD, M_TITANIUM, M_GLASS, M_BRICK, M_WOOD, M_CERAMIC};
    int n = sizeof(mats);
    rect(20, 20, 29, 20 + n * 14 - 5, M_HEATER);
    for (int i = 0; i < n; ++i) {
        int y = 20 + i * 14;
        rect(30, y, 190, y + 8, mats[i]);
        label(196, (float)y + 1, MATS[mats[i]].name);
    }
    rect(0, 230, World::W - 1, 239, M_CONCRETE);
    label(10, 8, "THERMAL CONDUCTIVITY: HEATER AT LEFT, TOGGLE HEAT VIEW");
    phys.stampBodies();
}

void Game::buildFuels() {
    resetWorld();
    static const uint8_t fuels[] = {M_GASOLINE, M_ETHANOL, M_KEROSENE, M_JETFUEL, M_DIESEL, M_OIL, M_HYDRAULIC};
    rect(10, 170, 395, 175, M_COPPER);       // shared plate
    rect(10, 176, 24, 190, M_HEATER);          // heats the plate from the left
    for (int i = 0; i < 7; ++i) {
        int x0 = 40 + i * 50;
        rect(x0, 130, x0 + 2, 169, M_STEEL);
        rect(x0 + 33, 130, x0 + 35, 169, M_STEEL);
        rect(x0 + 3, 150, x0 + 32, 169, fuels[i]);
        rect(x0 + 2, 146, x0 + 2, 152, M_IGNITER);  // spark plug in the wall at the fuel surface
        label((float)x0, 180, MATS[fuels[i]].name);
    }
    rect(0, 230, World::W - 1, 239, M_CONCRETE);
    label(10, 100, "FLASH POINT ORDER: GASOLINE < ETHANOL < KEROSENE/JET < DIESEL < OIL < HYDRAULIC");
    label(10, 112, "PLATE WARMS FROM THE LEFT; SPARK PLUGS FIRE PERIODICALLY (SPARK BUTTON)");
    world.sparkPeriod = 60;
    sparkIdx = 2;
    phys.stampBodies();
}

void Game::buildPressureTest() {
    resetWorld();
    rect(109, 146, 172, 165, M_STEEL);
    rect(112, 150, 170, 161, M_EMPTY);
    gasRect(112, 150, 120, 161, M_STEAM, 4.f, 300.f);
    int p = phys.addBox(Vec2(128, 155.5f), Vec2(6, 5.5f), 0, M_STEEL, false);
    (void)p;
    phys.stampBodies();
}

void Game::buildTestScene(int which) {
    resetWorld();
    rect(0, 200, World::W - 1, 203, M_WALL);
    if (which == 1) {
        for (int c = 0; c < 6; ++c) {
            int x0 = 10 + c * 65;
            rect(x0 - 2, 120, x0 - 1, 199, M_WALL);
            rect(x0 + 55, 120, x0 + 56, 199, M_WALL);
        }
        rect(20, 185, 40, 199, M_WOOD); rect(20, 170, 40, 184, M_OIL);
        rect(28, 165, 30, 168, M_FIRE);
        rect(75, 170, 125, 199, M_WATER); rect(90, 100, 100, 108, M_LAVA);
        rect(140, 190, 180, 199, M_STONE); rect(145, 175, 175, 189, M_SAND);
        rect(150, 150, 170, 160, M_ACID);
        rect(205, 190, 235, 199, M_PLANT); rect(205, 160, 235, 189, M_WATER);
        rect(270, 198, 320, 199, M_GUNPOWDER); rect(271, 190, 273, 197, M_FIRE);
        rect(335, 185, 370, 199, M_ICE); rect(340, 120, 345, 126, M_LAVA);
    } else {
        int bob = phys.addCircle(Vec2(80, 100), 8, M_LEAD, false, false);
        int anchor = phys.addBox(Vec2(300, 20), Vec2(2, 2), 0, M_STEEL, true);
        (void)anchor;
        phys.addDistance(bob, Vec2(80, 100), -1, Vec2(40, 40), 0.f);
        int box = phys.addBox(Vec2(150, 120), Vec2(10, 8), 0, M_WOOD, false);
        phys.addDistance(box, Vec2(150, 112), -1, Vec2(150, 30), 2.5f);
        int arm = phys.addBox(Vec2(230, 150), Vec2(25, 2), 0, M_ALUMINUM, false);
        phys.addPin(Vec2(230, 150), arm, -1, true, false);
        int rb = phys.addBox(Vec2(320, 190), Vec2(10, 6), 0, M_PLASTIC, false);
        int rk = phys.addRocket(Vec2(320, 178), 0);
        phys.addPin(Vec2(320, 184), rb, rk, false, true);
        phys.thrustOn = true;
        spawnCar(Vec2(370, 150));
    }
    phys.stampBodies();
}

void Game::simDrag(Tool t, Vec2 a, Vec2 b) {
    tool = t;
    mouse = a; lastMouse = a; inSim = true; handleSimDown();
    mouse = b; handleSimUp();
}

void Game::buildScriptedScene() {
    resetWorld();
    rect(0, 200, World::W - 1, 203, M_WALL);
    simDrag(T_BOX, Vec2(40, 150), Vec2(100, 165));
    simDrag(T_WHEEL, Vec2(48, 175), Vec2(48, 183));
    simDrag(T_WHEEL, Vec2(48, 160), Vec2(48, 168));
    simDrag(T_WHEEL, Vec2(92, 158), Vec2(92, 167));
    simDrag(T_CIRCLE, Vec2(150, 100), Vec2(150, 110));
    simDrag(T_BOX, Vec2(200, 100), Vec2(215, 112));
    simDrag(T_BOX, Vec2(240, 100), Vec2(255, 112));
    simDrag(T_ROD, Vec2(207, 106), Vec2(247, 106));
    simDrag(T_BOX, Vec2(300, 100), Vec2(320, 110));
    simDrag(T_SPRING, Vec2(310, 105), Vec2(310, 20));
    simDrag(T_ROCKET, Vec2(350, 150), Vec2(350, 120));
    simDrag(T_PIN, Vec2(350, 150), Vec2(350, 150));
    anchored = true;
    simDrag(T_BOX, Vec2(120, 130), Vec2(180, 135));
    anchored = false;
    simDrag(T_AUTOMOTOR, Vec2(150, 130), Vec2(150, 130));
    tool = T_GRAB; mouse = Vec2(150, 100); lastMouse = mouse; handleSimDown();
    mouse = Vec2(170, 90); continuousInput(); handleSimUp();
    simDrag(T_DELETE, Vec2(207, 106), Vec2(207, 106));
    phys.stampBodies();
}

// Exact shapes for the scene builders: what the old numeric form used to create, in cells and degrees.
int Game::exactBox(Vec2 c, float w, float h, float angleDeg) {
    int id = phys.addBox(c, Vec2(std::max(1.f, w), std::max(1.f, h)) * 0.5f, angleDeg * PI / 180.f, bodyMat, anchored);
    sel = {id}; primary = id; partMode = false; selJoint = -1;
    phys.stampBodies();
    return id;
}
int Game::exactCircle(Vec2 c, float r, bool wheel) {
    createCircle(c, std::max(0.5f, r), wheel);
    phys.stampBodies();
    return primary;
}
int Game::exactPipe(Vec2 a, Vec2 b, float d, float wall, bool hose, int segs) {
    pipeD = std::max(2.f, d); pipeWall = std::clamp(wall, 0.5f, pipeD * 0.5f);
    int g = hose ? phys.addHose(a, b, pipeD, pipeWall, segs > 0 ? segs : autoSegs(a, b), bodyMat, anchored) : phys.addPipe(a, b, pipeD, pipeWall, bodyMat, anchored);
    if (g >= 0) { sel = phys.groupMembers(g); primary = sel.empty() ? -1 : sel[0]; partMode = false; selJoint = -1; }
    phys.stampBodies();
    return g;
}

void Game::buildPrecisionTest() {
    resetWorld();
    rect(0, 200, World::W - 1, 203, M_WALL);
    anchored = true;
    exactBox(Vec2(40, 120), 60, 6, 0);   // exact shelf 60 x 6
    exactPipe(Vec2(40, 100), Vec2(100, 100), 12, 2, false, 0);
    std::vector<int> pipeSel = sel;
    anchored = false;
    int a = exactBox(Vec2(200, 60), 30, 8, 30);
    int b = exactCircle(Vec2(200, 60), 10, false);
    sel = {a, b}; groupSelection();
    exactPipe(Vec2(100, 100), Vec2(100, 160), 10, 1.5f, true, 0);
    std::vector<int> hoseSel = sel;
    // hang the hose from the end of the pipe
    if (!pipeSel.empty() && !hoseSel.empty()) phys.addPin(Vec2(100, 100), pipeSel[0], hoseSel[0], false, false);
    // move and turn one part of the group exactly, as the inspector does in part mode
    sel = {a}; primary = a; partMode = true;
    setBodyTransform(a, Vec2(205, 62), Vec2(15, 4), 0, 10);
    clearSelection();
    sel = phys.groupMembers(phys.bodies[a].group); primary = a;
    phys.stampBodies();
}

void Game::buildCutTest() {
    resetWorld();
    rect(0, 200, World::W - 1, 203, M_WALL);
    anchored = true;
    int plate = exactBox(Vec2(100, 150), 70, 24, 0);
    anchored = false;
    int ball = exactCircle(Vec2(100, 150), 9, false);
    // a second, rotated target for the cut: a block with a rectangular notch
    int block = exactBox(Vec2(250, 150), 50, 30, 20);
    int slot = exactBox(Vec2(250, 150), 12, 50, 20);
    sel = {plate, ball}; primary = ball; cutSelection();
    sel = {block, slot}; primary = slot; cutSelection();
    // scale the cutters to 98% to make tight-fitting plugs
    sel = {ball}; primary = ball; scaleSelection(98.f);
    sel = {slot}; primary = slot; scaleSelection(98.f);
    clearSelection();
    phys.stampBodies();
}

void Game::buildElectricTest() {
    resetWorld();
    rect(0, 200, World::W - 1, 203, M_WALL);
    // A: 12 V / 20 A battery heats a tungsten filament, which lights gasoline vapour
    world.battV = 12.f; world.battA = 20.f;
    rect(20, 60, 22, 62, M_BATT_POS); rect(20, 80, 22, 82, M_BATT_NEG);
    rect(23, 61, 59, 61, M_COPPER); rect(60, 61, 79, 61, M_TUNGSTEN); rect(80, 61, 100, 61, M_COPPER);
    rect(100, 61, 100, 81, M_COPPER); rect(23, 81, 100, 81, M_COPPER);
    for (int y = 56; y <= 60; ++y)   // alternate cells of air: a solid block of vapour has no oxygen inside and would only burn at its edges
        for (int x = 60 + (y & 1); x <= 80; x += 2) if (world.at(x, y).t == M_EMPTY) { world.setCell(x, y, M_AIR); world.at(x, y).amt = 1.5f; }
    gasRect(60, 56, 80, 60, M_VAPOR, 0.6f);
    label(24, 48, "12V 20A BATTERY, TUNGSTEN FILAMENT IN FUEL VAPOUR");
    // B: 20 kV / 50 mA through a 2-cell air gap (a spark plug) in vapour
    world.battV = 20000.f; world.battA = 0.05f;
    rect(150, 60, 152, 62, M_BATT_POS); rect(150, 80, 152, 82, M_BATT_NEG);
    rect(153, 61, 190, 61, M_COPPER); rect(190, 61, 190, 70, M_COPPER); rect(190, 70, 192, 70, M_COPPER);
    rect(195, 70, 197, 70, M_COPPER); rect(197, 70, 197, 81, M_COPPER); rect(153, 81, 197, 81, M_COPPER);
    for (int y = 66; y <= 70; ++y)
        for (int x = 191 + (y & 1); x <= 196; x += 2) if (world.at(x, y).t == M_EMPTY) { world.setCell(x, y, M_AIR); world.at(x, y).amt = 1.5f; }
    gasRect(191, 66, 196, 70, M_VAPOR, 0.6f);
    label(150, 48, "20KV, 2 CELL GAP = SPARK PLUG");
    // C: a lead wire in the loop acts as a fuse when the circuit is shorted by a copper bar
    world.battV = 12.f; world.battA = 60.f;
    rect(250, 60, 252, 62, M_BATT_POS); rect(250, 80, 252, 82, M_BATT_NEG);
    rect(253, 61, 280, 61, M_LEAD); rect(281, 61, 300, 61, M_COPPER); rect(300, 61, 300, 81, M_COPPER); rect(253, 81, 300, 81, M_COPPER);
    label(250, 48, "SHORT CIRCUIT BLOWS THE LEAD FUSE WIRE");
    // D: a loose copper bar falls across a gap in the wire, completing the circuit through a filament
    world.battV = 12.f; world.battA = 30.f;
    rect(320, 108, 322, 110, M_BATT_POS); rect(320, 130, 322, 132, M_BATT_NEG);
    rect(323, 110, 355, 110, M_COPPER); rect(365, 110, 380, 110, M_COPPER);
    rect(380, 110, 380, 116, M_COPPER); rect(380, 117, 380, 124, M_TUNGSTEN); rect(380, 125, 380, 131, M_COPPER);
    rect(323, 131, 380, 131, M_COPPER);
    rect(349, 96, 349, 109, M_WALL); rect(371, 96, 371, 109, M_WALL);
    label(318, 86, "FALLING BAR CLOSES THE SWITCH");
    phys.addBox(Vec2(360, 90), Vec2(8, 2), 0, M_COPPER, false);
    phys.stampBodies();
}

void Game::buildBondTest() {
    resetWorld();
    rect(0, 200, World::W - 1, 203, M_WALL);
    // 1: a steel block glued under a ledge with paraffin; a heater beside it warms it until the wax lets go
    phys.addBox(Vec2(50, 90), Vec2(30, 3), 0, M_STEEL, true);
    int w1 = phys.addBox(Vec2(50, 100), Vec2(8, 8), 0, M_STEEL, false);
    phys.addBond(Vec2(50, 93), w1, 0, BOND_TEMP[0], 0.f, BOND_G[0]);
    rect(59, 94, 66, 106, M_HEATER);
    label(20, 78, "PARAFFIN BOND MELTS WHEN HEATED (55C)");
    // 2: platforms pinned in mid-air by shear pins; a heavy ball dropped on one snaps its pin (the twin stays)
    int p1 = phys.addBox(Vec2(135, 160), Vec2(14, 2), 0, M_STEEL, false);
    phys.addBond(Vec2(135, 160), p1, -1, BOND_TEMP[3], 3.2e5f);
    int p2 = phys.addBox(Vec2(185, 160), Vec2(14, 2), 0, M_STEEL, false);
    phys.addBond(Vec2(185, 160), p2, -1, BOND_TEMP[3], 3.2e5f);
    phys.addCircle(Vec2(135, 110), 5.f, M_LEAD, false, false);
    label(110, 100, "SHEAR PIN SNAPS UNDER THE BALL (TWIN STAYS)");
    // 3: a pressure-release plug: gas builds up behind a bonded plug in a tube until the bond breaks
    phys.addPipe(Vec2(220, 150), Vec2(290, 150), 14.f, 2.f, M_STEEL, true);
    rect(219, 143, 220, 157, M_WALL);
    int plug = phys.addBox(Vec2(285, 150), Vec2(3, 4.8f), 0, M_PLASTIC, false);
    phys.addBond(Vec2(285, 150), plug, -1, 55.f + 300.f, 8.0e4f);
    phys.addBox(Vec2(224, 150), Vec2(2, 3), 0, M_STEEL, true);
    phys.bodies.back().src = Emitter{true, M_STEAM, 300.f, 0.f, 1};
    label(212, 128, "PLUG BOND LETS GO UNDER GAS PRESSURE");
    // 4: a paraffin cell plug holds water back until warmed
    rect(330, 120, 360, 122, M_WALL); rect(330, 120, 331, 180, M_WALL); rect(359, 120, 360, 180, M_WALL);
    rect(332, 123, 358, 150, M_WATER); rect(344, 151, 346, 153, M_PARAFFIN); rect(332, 151, 343, 153, M_WALL); rect(347, 151, 358, 153, M_WALL);
    rect(343, 154, 347, 158, M_HEATER);
    label(318, 108, "WAX CELL PLUG MELTS, WATER DRAINS");
    phys.stampBodies();
}

void Game::buildPrimerTest() {
    resetWorld();
    rect(0, 200, World::W - 1, 203, M_WALL);
    // a steel firing pin on a slider strikes a primer cartridge; the flash from the far end lights gunpowder
    rect(92, 141, 130, 143, M_WALL); rect(92, 159, 130, 161, M_WALL);   // chamber roof and floor
    rect(126, 144, 130, 158, M_WALL);
    rect(109, 150, 125, 158, M_GUNPOWDER);
    phys.addBox(Vec2(100, 155), Vec2(8, 3.f), 0, M_PRIMER, true);       // seated primer, flash hole to the right
    rect(92, 144, 108, 151, M_WALL);
    int pin = phys.addBox(Vec2(40, 155), Vec2(10, 1.5f), 0, M_STEEL, false);
    phys.bodies[pin].vel = Vec2(160, 0);
    phys.addSlider(pin, Vec2(1, 0));
    label(30, 132, "FIRING PIN STRIKES THE PRIMER (LEFT END)");
    label(90, 124, "FLASH EXITS THE FAR END: IGNITES THE POWDER");
    // a primer cell rod, struck by a dropped weight, lights gasoline vapour at the other end
    rect(250, 190, 270, 192, M_WALL);
    rect(255, 185, 255, 189, M_PRIMER); rect(255, 189, 262, 189, M_PRIMER);
    rect(256, 170, 264, 188, M_EMPTY);
    gasRect(262, 184, 268, 188, M_VAPOR, 0.7f);
    phys.addBox(Vec2(255, 150), Vec2(5, 5), 0, M_STEEL, false);
    label(230, 138, "PRIMER CELLS: IMPACT FLASHES THROUGH THE ROD");
    phys.stampBodies();
}

void Game::buildFanTest() {
    resetWorld();
    rect(0, 200, World::W - 1, 203, M_WALL);
    // 1: a wind tunnel: the fan blows a smoke stream and keeps light blocks aloft
    int f1 = phys.addBox(Vec2(30, 100), Vec2(3, 14), 0, M_STEEL, true);
    phys.bodies[f1].fan.strength = 90.f;
    phys.addBox(Vec2(95, 104), Vec2(5, 5), 0, M_WOOD, false);
    phys.addBox(Vec2(130, 98), Vec2(4, 4), 0, M_WOOD, false);
    int smoke = phys.addBox(Vec2(14, 100), Vec2(2, 2), 0, M_STEEL, true);
    phys.bodies[smoke].src = Emitter{true, M_SMOKE, 60.f, 0.f, 0};
    label(10, 74, "FAN BLOWS SMOKE AND LIFTS LIGHT BLOCKS");
    // 2: a closed duct: pressure builds ahead of the fan up to its stall pressure (try the PRESSURE view)
    rect(190, 120, 290, 122, M_WALL); rect(190, 142, 290, 144, M_WALL);
    rect(190, 123, 192, 141, M_WALL); rect(288, 123, 290, 141, M_WALL);
    int f2 = phys.addBox(Vec2(240, 132), Vec2(2, 8.5f), 0, M_STEEL, true);
    phys.bodies[f2].fan.strength = 80.f;
    label(196, 108, "CLOSED DUCT: PRESSURE RISES AHEAD, FALLS BEHIND");
    // 3: a free fan recoils: a car with a fan welded on its tail blows backwards and drives itself
    int chassis = phys.addBox(Vec2(80, 190), Vec2(24, 5), 0, M_ALUMINUM, false);
    for (int sx = -1; sx <= 1; sx += 2) {   // free-rolling wheels (no motor, so no brake)
        int wh = phys.addCircle(Vec2(80.f + 16.f * sx, 196), 4.f, M_RUBBER, false, true);
        phys.addPin(Vec2(80.f + 16.f * sx, 196), chassis, wh, false, false);
    }
    int tail = phys.addBox(Vec2(54, 183), Vec2(2, 7), 0, M_ALUMINUM, false);
    phys.bodies[tail].fan.strength = -250.f;
    phys.groupBodies({chassis, tail});
    label(30, 160, "A FAN ON A CAR RECOILS: IT DRIVES ITSELF");
    // 4: a vacuum fan empties a chamber of smoke through its throat, accelerating the gas
    rect(320, 60, 366, 62, M_WALL); rect(320, 98, 366, 100, M_WALL); rect(320, 60, 322, 100, M_WALL);
    gasRect(323, 63, 365, 97, M_SMOKE, 1.f);
    int f4 = phys.addBox(Vec2(367, 80), Vec2(2, 17.5f), 0, M_STEEL, true);
    phys.bodies[f4].fan.strength = 90.f; phys.bodies[f4].fan.vacuum = 1;
    label(312, 48, "VACUUM FAN DRAWS THE CHAMBER EMPTY");
    phys.stampBodies();
}

int Game::buildJetCar() {
    resetWorld();
    rect(0, 200, World::W - 1, 203, M_WALL);
    const JetCfg& C = jet();
    const float cy = C.space ? 100.f : 170.f, xc = 260.f;      // x = xc - u, where u runs from the inlet to the nozzle
    const float wallT = 1.5f, H = C.half + wallT;
    std::vector<int> parts;
    auto box = [&](float u, float y, float hu, float hy, uint8_t m) { int b = phys.addBox(Vec2(xc - u, y), Vec2(hu, hy), 0, m, false); parts.push_back(b); return b; };
    box(C.len * 0.5f, cy - H, C.len * 0.5f, wallT, M_ALUMINUM);
    box(C.len * 0.5f, cy + H, C.len * 0.5f, wallT, M_ALUMINUM);
    for (int sgn = -1; sgn <= 1; sgn += 2) {   // two angled plates make the converging nozzle
        Vec2 p0(xc - C.len, cy + sgn * C.half), p1(xc - C.len - C.nozLen, cy + sgn * (C.half - C.nozIn));
        Vec2 d = p1 - p0;
        int b = phys.addBox((p0 + p1) * 0.5f, Vec2(length(d) * 0.5f + 1.f, wallT), std::atan2(d.y, d.x), M_ALUMINUM, false);
        parts.push_back(b);
    }
    int comp = box(8, cy, 1.5f, C.half, M_ALUMINUM);
    phys.bodies[comp].fan.strength = -C.fan;
    int fuel = box(C.fuelU, cy - C.half + 1.6f, 1.5f, 1.5f, M_ALUMINUM);   // an injector on the wall, out of the airstream
    phys.bodies[fuel].src = Emitter{true, M_PROPANE, C.fuel, 0.f, 0};
    box(C.plugU, cy - C.half + 1.f, 1.5f, 1.f, M_IGNITER);
    sparkIdx = 5; world.sparkPeriod = C.spark ? SPARK_RATES[sparkIdx] : 0;
    int hull = box(C.len * 0.5f, cy + H + 4.f, C.len * 0.5f + 1.f, 1.5f, M_ALUMINUM);
    if (!C.space)
        for (int sx = -1; sx <= 1; sx += 2) {
            int wh = phys.addCircle(Vec2(xc - C.len * 0.5f + 40.f * sx, cy + H + 9.f), 4.f, M_RUBBER, false, true);
            phys.addPin(Vec2(xc - C.len * 0.5f + 40.f * sx, cy + H + 9.f), hull, wh, false, false);
        }
    phys.groupBodies(parts);
    label(xc - 120, cy - 40, "TURBOJET: FAN COMPRESSES AIR, FUEL + SPARK PLUG BURN IT, NOZZLE EXHAUSTS LEFT");
    selectBody(hull, false, false);
    focusBody = hull;
    setCam(100);
    phys.stampBodies();
    return hull;
}

void Game::buildShotgun() {
    resetWorld();
    rect(0, 230, World::W - 1, 239, M_CONCRETE);
    const int bx0 = 120, by0 = 108;   // breech block top-left
    const int ym = by0 + 12;           // bore centre line
    rect(bx0 - 6, by0 - 6, bx0 + 150, by0 - 1, M_STEEL);        // barrel top wall
    rect(bx0 - 6, by0 + 25, bx0 + 150, by0 + 30, M_STEEL);      // barrel bottom wall
    rect(bx0 - 6, by0 - 6, bx0 + 7, by0 + 30, M_STEEL);         // breech block
    rect(bx0 - 6, ym - 1, bx0 + 7, ym + 1, M_EMPTY);            // the firing-pin hole through it
    // primer seated in the breech: struck on its left end, flashes out of its right end into the chamber
    phys.addBox(Vec2(bx0 + 4, ym + 0.5f), Vec2(4.f, 1.5f), 0, M_PRIMER, true);
    // powder charge
    rect(bx0 + 8, by0, bx0 + 23, by0 + 24, M_GUNPOWDER);
    // wad and shot
    const float wx = bx0 + 27.f;
    lastGun.wad = phys.addBox(Vec2(wx + 3.f, ym + 0.5f), Vec2(5.5f, 11.5f), 0, M_ALUMINUM, false);   // long enough that it can't tip over in the bore
    lastGun.shot.clear();
    for (int i = 0; i < 15; ++i) lastGun.shot.push_back(phys.addCircle(Vec2(wx + 11.f + 4.6f * (i / 5), ym - 8.8f + 4.4f * (i % 5) + 0.2f), 2.15f, M_LEAD, false, false));   // shot filling the bore, so the wad is pushed evenly
    // hammer on a slider, driven by a compressed spring from a post behind it
    int post = phys.addBox(Vec2(bx0 - 56, ym), Vec2(3.f, 8.f), 0, M_STEEL, true);
    lastGun.hammer = phys.addBox(Vec2(bx0 - 28, ym + 0.5f), Vec2(8.f, 1.2f), 0, M_STEEL, false);
    phys.addSlider(lastGun.hammer, Vec2(1, 0));
    int sp = phys.addDistance(lastGun.hammer, Vec2(bx0 - 28, ym), post, Vec2(bx0 - 53, ym), 3.f);
    if (sp >= 0) phys.joints[sp].length = 70.f;   // longer than it is: a compressed spring pushing the hammer at the primer
    // targets down range
    for (int i = 0; i < 4; ++i) phys.addBox(Vec2(bx0 + 215.f, 220.f - 11.f * i), Vec2(5.f, 5.5f), 0, M_WOOD, false);
    phys.addBox(Vec2(bx0 + 232.f, 215.f), Vec2(4.f, 12.f), 0, M_BRICK, false);
    label(bx0 - 64, by0 - 18, "SPRING-DRIVEN HAMMER (ON A SLIDER) STRIKES THE PRIMER");
    label(bx0 + 10, by0 - 30, "POWDER CHARGE");
    label(bx0 + 46, by0 - 18, "WAD + SHOT");
    label(bx0 + 150, by0 - 10, "BARREL");
    phys.stampBodies();
}

void Game::buildJet() { buildJetCar(); }

void Game::buildRoadTest() {
    resetWorld();
    rect(0, 200, World::W - 1, 203, M_WALL);
    for (int x0 = 260; x0 < World::W - 100; x0 += 170)   // rolling bumps along the way
        for (int x = 0; x < 40; ++x) rect(x0 + x, 200 - (int)(6.f * std::sin(3.14159f * x / 40.f)), x0 + x, 200, M_STONE);
    // car A: a fan on its tail blows backwards and drives it
    int chassis = phys.addBox(Vec2(60, 190), Vec2(24, 5), 0, M_ALUMINUM, false);
    for (int sx = -1; sx <= 1; sx += 2) {
        int wh = phys.addCircle(Vec2(60.f + 16.f * sx, 196), 4.f, M_RUBBER, false, true);
        phys.addPin(Vec2(60.f + 16.f * sx, 196), chassis, wh, false, false);
    }
    int tail = phys.addBox(Vec2(34, 183), Vec2(2, 7), 0, M_ALUMINUM, false);
    phys.bodies[tail].fan.strength = -250.f;
    phys.groupBodies({chassis, tail});
    // car B: motor-driven wheels that spin on their own, a little behind
    int b = phys.addBox(Vec2(20, 190), Vec2(18, 4), 0, M_STEEL, false);
    for (int sx = -1; sx <= 1; sx += 2) {
        int wh = phys.addCircle(Vec2(20.f + 12.f * sx, 195), 4.f, M_RUBBER, false, true);
        phys.addPin(Vec2(20.f + 12.f * sx, 195), b, wh, true, false);
    }
    label(8, 160, "SELECT THE FAN CAR, PRESS FOCUS (F), THEN PLAY: THE CAMERA FOLLOWS IT DOWN THE ROAD");
    selectBody(chassis, false, false);
    focusBody = chassis;
    setCam(0);
    phys.stampBodies();
    notify("Focus is on the fan car: press Play. F turns focus off, middle-drag pans");
}

void Game::buildEmitterTest() {
    resetWorld();
    rect(0, 200, World::W - 1, 203, M_WALL);
    rect(40, 100, 41, 199, M_WALL); rect(160, 100, 161, 199, M_WALL);
    auto emitter = [&](Vec2 c, float rate, uint8_t m) { int id = exactBox(c, 6, 6, 0); phys.bodies[id].src = Emitter{true, m, rate, 0.f, emitFace}; return id; };
    anchored = true;
    emitter(Vec2(100, 40), 60.f, M_WATER);
    anchored = false;
    // a free-falling emitter box carrying gasoline vapour, and one pinned to a swinging arm
    emitter(Vec2(250, 40), 40.f, M_GASOLINE);
    int sandE = emitter(Vec2(320, 60), 30.f, M_SAND);
    anchored = true;
    int pivot = exactBox(Vec2(320, 30), 6, 6, 0);
    anchored = false;
    phys.addPin(Vec2(320, 30), pivot, sandE, false, false);
    clearSelection();
    phys.stampBodies();
}

// The scene browser's list. Loading a scene replaces the drawing (Ctrl+Z brings it back).
const std::vector<Game::SceneDef>& Game::sceneList() {
    static const std::vector<SceneDef> scenes = {
        {"Demo", &Game::buildDemo, "A little of everything"},
        {"Steam engine", &Game::buildSteamEngine, "Boiler, valve, piston and flywheel"},
        {"Gas engine", &Game::buildGasEngine, "Spark-ignited gasoline vapour engine"},
        {"Diesel engine", &Game::buildDieselEngine, "Glow-plug diesel engine"},
        {"Hydraulics", &Game::buildHydraulics, "Master and slave cylinders"},
        {"Conduction", &Game::buildConduction, "Heat flow through different materials"},
        {"Fuels", &Game::buildFuels, "The liquid fuels side by side"},
        {"Electric", &Game::buildElectricTest, "Batteries, filaments, fuses and a spark gap"},
        {"Bonds", &Game::buildBondTest, "Wax and shear-pin bonds"},
        {"Primer", &Game::buildPrimerTest, "A firing pin strikes a primer"},
        {"Fans", &Game::buildFanTest, "Blowers, a closed duct and a vacuum fan"},
        {"Shotgun", &Game::buildShotgun, "Spring hammer, primer, powder charge, wad and shot"},
        {"Jet engine", &Game::buildJet, "A turbojet on wheels: fan, fuel, spark plug, nozzle"},
        {"Road + focus", &Game::buildRoadTest, "A fan-driven car and the following camera"},
    };
    return scenes;
}
void Game::loadScene(int i) {
    const auto& s = sceneList();
    if (i < 0 || i >= (int)s.size()) return;
    scenesOpen = false;
    (this->*s[i].fn)();
    if (!undoLabels.empty()) undoLabels.back() = std::string("Scene: ") + s[i].name;
    currentFile.clear();
    notify(std::string("Loaded the ") + s[i].name + " scene. Press Play (Space) to run it");
}


Game::JetCfg& Game::jet() { static JetCfg c; return c; }
