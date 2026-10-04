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
* **Select** – click a body (Shift adds, Ctrl picks a single part of a group), or drag a box around several. Enter edits the selection numerically.
* **Group / Ungroup** (`Ctrl+G` / `Ctrl+U`) – welds the selected bodies into one rigid object. Every part stays individually editable (Ctrl+click it, then Enter), while grabbing, moving or rotating the group treats it as one thing. Parts of a group never collide with each other. Grouped bodies are outlined in cyan.
* **Pipe** – hollow tube between two points (two welded walls). **Hose** – a chain of pipe segments hinged together so it bends; the mouse wheel sets the diameter.
* **Exact** (`Enter`) – a numeric form for precise work, in grid cells and degrees. With nothing selected it creates a box (X, Y, width, height, angle), circle/wheel (X, Y, radius), pipe or hose (both end points, diameter, wall, segments) at exactly those values and shows a green preview outline. With a body selected it edits that body's position, size, angle, material (`M`) and static flag (`S`); with a group selected it moves/rotates the whole group. `Tab`/click move between fields.
* **Snap** – the SNAP button rounds mouse-drawn shapes to a 1/2/5/10 cell grid. While dragging, the exact dimensions are shown next to the cursor and the cursor coordinates are in the status line, so mouse drawing stays available and can be made precise too.
* **Cut** – boolean subtract. Select the target body(ies), then click the cutter *last* (it has the white outline) and press `CUT`. The cutter's area is removed from every other selected body (a group target is cut part by part). The remainder becomes a welded group of rectangular pieces (steps of 0.5 cell, so curves are fine staircases and the hole is never smaller than the cutter). The cutter is kept and left selected so you can delete it or `SCALE` it. Wheels, rockets and emitters can't be cut.
* **Scale** – with bodies selected, `SCALE` opens a percentage form (default 98) that resizes the whole selection about its centre, groups included; joints follow. Cut a hole with a shape, scale that shape to 98 % and you have a tight-fitting plug, valve ball or piston.
* **Emitter** – a body that endlessly produces the powder, liquid or gas you picked in the palette, in free cells beside its surface. Drag one out with `EMITTER` (or use `Enter`), then select it and press `Enter` to change the **rate** (cells per second; 0 switches it off), which side it emits from (`F`) and the material (click a palette material while the form is open). Because it is an ordinary body it can be anchored, pinned, welded into a group, or left to travel with a machine; if its outlet is blocked it simply waits instead of building up a burst. Use it as an endless fuel or water supply.
* **Bond** – a temporary connection. Click where two bodies overlap (or one body, to bond it to the world): a weld that lets go when the bodies/cells at the joint exceed a **melt temperature** or the joint load exceeds a **breaking force**. The `PARAFFIN / SOLDER / EPOXY / SHEAR PIN` button cycles presets (55 °C, 190 °C, 260 °C, force-only); `Enter` with the Bond tool sets both numbers exactly (°C and kN). Broken bonds drop out and the bodies collide normally again. Bonded bodies don't collide while the bond holds.
* **Grab** – drag bodies around with a soft spring. **Delete** – click a body or joint.

### Materials and tabs

The palette is split into tabs: **Powder** (sand, ash, gunpowder, coal), **Liquid** (water, oil, gasoline, diesel, kerosene, jet fuel, ethanol, hydraulic fluid, acid, lava), **Gas** (steam, fire, smoke, exhaust, fuel vapour, propane, hydrogen), **Metal** (steel, iron, copper, aluminum, lead, gold, titanium, tungsten, solder), **Struct** (wall, stone, concrete, brick, ceramic, glass, wood, rubber, plastic, ice, plant, TNT, paraffin), **Device** (heater, cooler, spark plug/igniter, fluid source, void drain, battery+ and battery-, primer), **Scenes** (ready-made machines) and a **Heat view** toggle that colours cells by temperature.

Every material has density, friction, restitution, **thermal conductivity** and heat capacity. Solid materials drawn as cells use those properties, and the `BODY:` button picks the material for box/circle bodies (density, friction, bounce, heat conduction, melting and burning all follow from it).

* **Fuels differ** – flash point, autoignition temperature, volatility, burn rate and energy: gasoline flashes below freezing and evaporates fast, ethanol burns clean, kerosene/jet fuel/diesel need heat (diesel autoignites under compression/hot surfaces – glow plug), hydraulic fluid barely burns.
* **Heat** flows by conduction through everything (copper moves it quickly, ceramic and plastic insulate), phase changes (water↔steam↔ice, metals↔molten, fuel↔vapour) absorb latent heat, and gases heat when compressed.
* **Gas pressure** acts on rigid bodies, so steam and combustion gas can push pistons. **Liquid pressure** is equalised through connected liquid, so a closed hydraulic circuit transmits force from a master piston to a slave piston.
* **Slider** tool – prismatic joint for pistons, valves and rams. **Spark** button – set the spark rate (or hold `E`).

### Electricity

* **Battery cells** (Device tab): paint a `BATTERY+` blob and a `BATTERY-` blob, join them with conductors, done. The `BATT nV` button (or `Enter` after clicking it) sets the **voltage** (1 V – 70 kV) and **amperage** (up to 400 A) stamped into battery cells you paint from then on; repaint cells to change a battery.
* The conducting cells (all the metals, solder, water, and rigid bodies made of metal) form a resistor network solved every frame. The `+` terminal sits at its voltage, the `-` terminal at 0 V; if the circuit would draw more than the battery's rated amperage the voltage sags to hold the current at that limit. Conductances: copper 400, aluminium 250, gold 280, tungsten 70, iron 45, steel 40, lead 20, solder 120, water 0.4 (per cell edge).
* **Heating** – current heats conductors by I²R: a thin tungsten filament glows and lights fuel vapour, a lead wire is a fuse that melts and opens the circuit, solder is a thermal fuse, copper stays cool. A conducting rigid body that touches wire cells (a lever, a falling bar, a piston) closes the circuit like a switch.
* **Sparks** – where two conductors are separated by an air gap (up to 9 cells, straight or diagonal) and the potential difference exceeds about 3 kV per cell of gap (higher in compressed gas, lower in thin gas), the gap breaks down: the arc is drawn, heats the channel and ignites fuel mixtures in it. That is a spark plug: two electrodes, a gap, a high-voltage battery, and a closing contact. `ELEC VIEW` colours conductors by potential and current.
* Limits: batteries are grid cells (they don't move), grounds are always 0 V, and the arc doesn't load the source. Good enough to build ignition circuits; to be refined later.

### Frangible materials and primers

* **Paraffin** (Struct tab) is a solid wax: it melts at 55 °C into liquid wax, burns, and as a plug it gives way under pressure — it fails layer by layer when the pressure difference across it exceeds 5 per cell of thickness, so a thick plug holds more. **Solder** (Metal tab) is a conductive metal that melts at 190 °C.
* **Primer** (Device tab, also a body material): impact-sensitive. When a body strikes it hard enough (closing speed over 70 cells/s and impact energy over 3·10⁴) it fires once. A primer **body** goes spent and throws a flash of flame out of the end opposite the one that was struck (a flash hole), which ignites fuel and powder there. **Primer cells** painted on the grid behave the same when struck, and also fire from heat (230 °C) or flame, passing the flash along the cells. Firing pin: any hard, fast body (a steel pin on a slider, a dropped weight).

### Scenes

* **Steam engine** – boiler, gate valve driven by an eccentric, piston, crank and flywheel. Runs continuously.
* **Hydraulics** – master/slave cylinders with a mechanical advantage.
* **Conduction** – bars of different metals/insulators heated at one end.
* **Fuels** – seven liquids on a warming plate with spark plugs, showing flash-point order.
* **Gasoline / diesel engines** – intake and exhaust gate valves, spark or glow-plug ignition, fuel supply. *Limitation:* these are demonstrators spun up by a starter; combustion works and cycles the piston, but the simplified gas model loses more energy on compression than it gains, so the flywheel slowly spins down rather than self-sustaining.

Other things to try: lava on water makes stone and steam; fire spreads through wood, plants and oil; acid eats most solids; gunpowder and TNT chain-explode and throw bodies; a light ball floats on oil on water.

## Developer flags

`sandbots --shot out.bmp [frames] [--scene N] [--heat] [--elec] [--trace] [--g0]` runs headless (SDL dummy video driver), simulates, prints burn counts/wheel speed (and body/gas stats with `--trace`) and saves a screenshot. Scenes: `1` material reactions, `2` joints/springs/rockets, `3` scripted tool use, `11`–`13` precision/group/hose tests (13 shows the numeric form), `15` cut + scale, `16` emitters, `17` electricity, `18` bonds/wax, `19` primers, `4` steam engine, `5` gasoline engine, `6` hydraulics, `7` conduction, `8` fuels, `9` pressure test, `10` diesel engine.
