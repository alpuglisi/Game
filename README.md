# SandBots

A C++17 / SDL2 toy that mixes two genres in one world:

* **Powder Game style falling-sand and fluid simulation** – 17 materials that flow, burn, melt, grow, dissolve and explode.
* **Incredibots style rigid-body sandbox** – boxes, circles, wheels, pin joints, keyboard-driven motors, rods, springs and rockets.

The two halves are fully coupled: bodies shove particles aside, float (or sink) in water, oil and lava according to their mass, get slowed by sand, rest on sand piles, and are blasted around by explosions.

## Build & run

```bash
sudo apt install libsdl2-dev cmake g++     # Debian/Ubuntu
cmake -S . -B build && cmake --build build -j
./build/sandbots
```

The physics engine, the sand engine and the bitmap font are all written from scratch – SDL2 is the only dependency.

## Controls

| Input | Action |
|---|---|
| Palette buttons (bottom) | Pick a material or tool |
| LMB / RMB | Use tool / erase particles |
| Mouse wheel, `[` `]` | Brush size |
| `Space`, `N` | Pause, single step |
| `C` / `X` | Clear particles / clear bodies |
| `R` | Reload the demo scene |
| `T` | Toggle "anchored" (new bodies are static) |
| `F` | Cycle mass of new bodies (light floats, heavy sinks) |
| `V` | Drop a car at the cursor |
| `G` | Flip gravity |
| `Tab` | Next material |
| `Left`/`Right` (or `A`/`D`) | Drive keyed motors |
| `Up` (or `W`) | Fire rockets |
| `Del` | Delete body under the cursor |

### Body tools

* **Box / Circle** – drag to size (box: drag a rectangle; circle: drag out the radius).
* **Wheel** – a circle with high friction; if it is dropped on top of another body it is automatically pinned to it with a keyed motor, so a car is just a box plus two wheels.
* **Pin** – click where two bodies overlap to hinge them (a single body is pinned to the world).
* **Motor** – a pin whose angular velocity is driven by the arrow keys. **Auto motor** always spins (windmills, conveyors).
* **Rod / Spring** – drag from one body (or empty space = world) to another.
* **Rocket** – drag to choose the thrust direction; hold `Up` to fire.
* **Grab** – drag bodies around with a soft spring. **Delete** – click a body or joint.

### Materials and tabs

The palette is split into tabs: **Powder** (sand, ash, gunpowder, coal), **Liquid** (water, oil, gasoline, diesel, kerosene, jet fuel, ethanol, hydraulic fluid, acid, lava), **Gas** (steam, fire, smoke, exhaust, fuel vapour, propane, hydrogen), **Metal** (steel, iron, copper, aluminum, lead, gold, titanium, tungsten), **Struct** (wall, stone, concrete, brick, ceramic, glass, wood, rubber, plastic, ice, plant, TNT), **Device** (heater, cooler, spark plug/igniter, fluid source, void drain), **Scenes** (ready-made machines) and a **Heat view** toggle that colours cells by temperature.

Every material has density, friction, restitution, **thermal conductivity** and heat capacity. Solid materials drawn as cells use those properties, and the `BODY:` button picks the material for box/circle bodies (density, friction, bounce, heat conduction, melting and burning all follow from it).

* **Fuels differ** – flash point, autoignition temperature, volatility, burn rate and energy: gasoline flashes below freezing and evaporates fast, ethanol burns clean, kerosene/jet fuel/diesel need heat (diesel autoignites under compression/hot surfaces – glow plug), hydraulic fluid barely burns.
* **Heat** flows by conduction through everything (copper moves it quickly, ceramic and plastic insulate), phase changes (water↔steam↔ice, metals↔molten, fuel↔vapour) absorb latent heat, and gases heat when compressed.
* **Gas pressure** acts on rigid bodies, so steam and combustion gas can push pistons. **Liquid pressure** is equalised through connected liquid, so a closed hydraulic circuit transmits force from a master piston to a slave piston.
* **Slider** tool – prismatic joint for pistons, valves and rams. **Spark** button – set the spark rate (or hold `E`).

### Scenes

* **Steam engine** – boiler, gate valve driven by an eccentric, piston, crank and flywheel. Runs continuously.
* **Hydraulics** – master/slave cylinders with a mechanical advantage.
* **Conduction** – bars of different metals/insulators heated at one end.
* **Fuels** – seven liquids on a warming plate with spark plugs, showing flash-point order.
* **Gasoline / diesel engines** – intake and exhaust gate valves, spark or glow-plug ignition, fuel supply. *Limitation:* these are demonstrators spun up by a starter; combustion works and cycles the piston, but the simplified gas model loses more energy on compression than it gains, so the flywheel slowly spins down rather than self-sustaining.

Other things to try: lava on water makes stone and steam; fire spreads through wood, plants and oil; acid eats most solids; gunpowder and TNT chain-explode and throw bodies; a light ball floats on oil on water.

## Developer flags

`sandbots --shot out.bmp [frames] [--scene N] [--heat] [--trace] [--g0]` runs headless (SDL dummy video driver), simulates, prints burn counts/wheel speed (and body/gas stats with `--trace`) and saves a screenshot. Scenes: `1` material reactions, `2` joints/springs/rockets, `3` scripted tool use, `4` steam engine, `5` gasoline engine, `6` hydraulics, `7` conduction, `8` fuels, `9` pressure test, `10` diesel engine.
