# SandBots

A C++17 / SDL2 toy that mixes two genres in one world:

* **Powder Game style falling-sand and fluid simulation** – 17 materials that flow, burn, melt, grow, dissolve and explode.
* **Incredibots style rigid-body sandbox** – boxes, circles, wheels, pin joints, keyboard-driven motors, rods, springs and rockets.

The two halves are fully coupled: bodies shove particles aside, float (or sink) in water, oil and lava according to their mass, get slowed by sand, rest on sand piles, and are blasted around by explosions.

## Build and run

SandBots is C++17 with SDL2 as its only dependency (the physics engine, sand engine and bitmap font are written from scratch).

### 1. Install the dependencies

```bash
./install_deps.sh            # checks everything, offers to install what is missing
./install_deps.sh --yes      # install without asking
./install_deps.sh --check    # only check (exit status 1 if something is missing)
```

The script looks for a C++17 compiler, CMake 3.10 or newer, make (or ninja), pkg-config and the SDL2 development files, and installs the missing ones with apt, dnf, pacman, zypper, apk, Homebrew or MSYS2. It does not need a network if everything is already present.

By hand instead:

| System | Command |
|---|---|
| Debian / Ubuntu | `sudo apt install build-essential cmake pkg-config libsdl2-dev` |
| Fedora | `sudo dnf install gcc-c++ make cmake pkgconf-pkg-config SDL2-devel` |
| Arch | `sudo pacman -S base-devel cmake pkgconf sdl2` |
| macOS | `xcode-select --install` then `brew install cmake pkg-config sdl2` |
| Windows | install [MSYS2](https://www.msys2.org), open the *MSYS2 MinGW x64* shell, then `pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-SDL2 mingw-w64-x86_64-pkgconf make` |

### 2. Compile

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

(On Windows/MSYS2 add `-G "MinGW Makefiles"` or `-G Ninja` to the first command.) The program is `build/sandbots`.

### 3. Run

```bash
./build/sandbots            # from the repository folder; saves go to ./saves
./build/sandbots --selftest # optional: headless regression checks, prints PASS/FAIL
```

The window is 1532 x 840. Press **F1** in the program for a quick tour.

Troubleshooting: if CMake says it cannot find SDL2, the development package is missing (`libsdl2-dev` / `SDL2-devel`, not just the runtime library); run `./install_deps.sh`. If you change compilers or move the folder, delete `build/` and configure again. On a machine with no display, use the headless `--shot` mode below.

## The screen

* **Top bar** – PLAY / PAUSE / STEP / STOP, file buttons (NEW, OPEN, SAVE, SAVE AS), UNDO / REDO, view toggles (HEAT, PRESSURE, ELECTRIC), FOCUS, SCENES and HELP.
* **Left panel** – the tools, grouped: Tools (SELECT, GRAB), Shapes, Machines (FAN, EMITTER), Joints, Edit (CUT, SCALE, EXACT, DELETE, GROUP, COPY, PASTE, SUBTRACT...), Particles (PAINT, ERASER) and Clear.
* **Right panel** – the properties of whatever you are doing. Painting shows swatches of every particle material, grouped (powders, liquids, gases, metals, building, devices) with no scrolling. Drawing a shape shows the swatches for the shape's material. A selection shows its position/size, material, fixed/free, and fan or emitter settings.
* **Bottom bar** – current tool, material, selection, cursor coordinates and a one-line hint for whatever is under the mouse.

## Controls

| Input | Action |
|---|---|
| `Ctrl+Z` / `Ctrl+Y` (or `Ctrl+Shift+Z`) | Undo / redo (edit mode) |
| `Ctrl+C` / `Ctrl+V` | Copy / paste the selected bodies, with the joints between them; paste lands at the cursor |
| `Ctrl+A` | Select all bodies |
| `Ctrl+G` / `Ctrl+U` | Group / ungroup |
| `Ctrl+S` / `Ctrl+O` / `Ctrl+N` | Save / open / new |
| `Space` | Play (from edit mode) / pause / resume |
| `N` | Single step |
| Arrow keys | Edit mode: nudge the selection 1 cell (`Shift` 10 cells, `Ctrl` a quarter cell); works on one body, a group, a box-selection or anything selected. While playing they drive motors as before |
| `Del` / `Backspace` | Delete the selection |
| `Enter` | Numeric form for the tool or selection |
| `F` | Focus the camera on the selected body |
| `Esc` | Close dialogs / clear the selection |
| `M`, `+`, `-`, `\` | Fan mode, strength, flip direction |
| `H`, `P`, `G`, `T` | Heat view, pressure view, flip gravity, fixed toggle |
| `[` `]` / mouse wheel | Brush size |
| Middle-drag, `Home`/`End`/`PgUp`/`PgDn` | Pan the camera |
| Arrow keys / `W` | Drive motors / fire rockets |

### Edit mode, play and stop, files

The game starts in **edit mode**: nothing moves, so you can draw, place bodies and joints, and set up a machine in a frozen state. **PLAY** (or `Space`) takes a snapshot of everything (grid cells, bodies, joints, bonds, emitters, battery settings) and starts the simulation; **PAUSE** / `Space` freezes and resumes it; **STEP** advances one frame; **STOP** restores the snapshot, so the machine is exactly as you drew it. The top-right corner shows the mode. Scenes and loaded files open in edit mode.

**NEW** (press twice to confirm) clears everything. **SAVE** writes to the current file (or asks for a name the first time), **SAVE AS** asks for a name, **LOAD** lists the saved files (Up/Down to pick, or type a name). Files are `saves/NAME.sbot` next to where you run the game. Saving while playing stores the design as it was drawn, not the mid-run state. Files can only be read by a build with the same data layout.

### Body tools

* **Box / Circle** – drag to size (box: drag a rectangle; circle: drag out the radius).
* **Wheel** – a circle with high friction; if it is dropped on top of another body it is automatically pinned to it with a keyed motor, so a car is just a box plus two wheels.
* **Pin** – click where two bodies overlap or touch to hinge them (with only one nearby it is pinned to the world).
* **Motor** – a pin whose angular velocity is driven by the arrow keys. **Auto motor** always spins (windmills, conveyors).
* **Rod / Spring** – drag from one body (or empty space = world) to another. Select the tool to set the stiffness and damping of new springs; or click any joint with SELECT to edit it later (see below).
* **Rocket** – drag to choose the thrust direction; hold `Up` to fire.
* **Select** – click a body to select it; clicking the same spot again cycles through the bodies stacked there (including parts of a group). Shift adds/removes, dragging on empty space draws a selection box. **Drag a selected body to move it** (the whole selection moves together; joints to bodies outside the selection keep their anchors). With two or more selected, one is the red "cutter" (the last one clicked).
* **Group / Ungroup** – welds the selected bodies into one rigid object; parts stay individually selectable by click-cycling. Parts of a group never collide with each other.
* **Pipe** – hollow tube between two points (two welded walls). **Hose** – a chain of pipe segments hinged together so it bends; the mouse wheel sets the diameter.
* **Exact** (`Enter`) – a numeric form for precise work, in grid cells and degrees. With nothing selected it creates a box (X, Y, width, height, angle), circle/wheel (X, Y, radius), pipe or hose (both end points, diameter, wall, segments) at exactly those values and shows a green preview outline. With a body selected it edits that body's position, size, angle, material (`M`) and static flag (`S`); with a group selected it moves/rotates the whole group. `Tab`/click move between fields.
* **Snap** – the SNAP button rounds mouse-drawn shapes to a 1/2/5/10 cell grid. While dragging, the exact dimensions are shown next to the cursor and the cursor coordinates are in the status line, so mouse drawing stays available and can be made precise too.
* **Cut** – the CUT tool: choose box or circle, drag over any part of the drawing and that area is removed from every body it touches. Each damaged body becomes a welded group of small rectangles (steps down to 0.5 cell; the hole is never smaller than the cut). Undo brings it back. **Subtract** is the selection version: select the target(s), make the cutter the red primary, press SUBTRACT; the cutter is consumed unless KEEP THE CUTTER is on. Wheels, rockets and emitters can't be cut.
* **Scale** – with bodies selected, `SCALE` opens a percentage form (default 98) that resizes the whole selection about its centre, groups included; joints follow. Cut a hole with a shape, scale that shape to 98 % and you have a tight-fitting plug, valve ball or piston.
* **Emitter** – a body that endlessly produces the powder, liquid or gas chosen in its panel swatches, out of **one side** (RIGHT, LEFT, DOWN, UP in the block's own frame, so it turns with the block; an arrow marks the outlet). The material is thrown a few cells along that side's normal (further at higher rates), stopping at the first obstruction, so an outlet placed against a pipe or channel fills it. ALL SIDES spills from every face. Set the rate (cells per second; 0 = off) and the material in the panel. It can be anchored, welded into a group or travel with a machine. If the outlet is blocked (by the liquid it is feeding, say) it simply waits, which makes a self-regulating feed: put the outlet at the level you want the water to stand at. (The old grid "SOURCE" cell survives only inside scenes as FUEL SUPPLY.)
* **Bond** – a temporary weld. Click where two bodies overlap or touch (with only one nearby it bonds to the world). Pick a bond material in the panel: `PARAFFIN` (melts at 55 °C, holds 10x the weight it carries), `SOLDER` (190 °C, 40x), `EPOXY` (260 °C, 120x) or `SHEAR PIN` (never melts, snaps at 30x). Strength is rated against the weight of the lighter bonded body (a whole group counts as one), so a bond holds a block no matter how heavy it is, but gives way to a hard sustained pull. Brief jolts don't count - the load is averaged over about a tenth of a second. The bond softens as it nears its melting point, and is heated by the bodies it joins and by flame, hot gas or molten metal within a few cells of the seam. It is drawn as a seam between the bodies that goes from cream (paraffin), grey (solder), amber (epoxy) or dark (shear pin) towards red as it heats. `Enter` sets the melt temperature and the rating exactly. Broken bonds drop out and the bodies collide normally again.
* **Grab** – drag bodies around with a soft spring. **Delete** – click a body or joint.

### Materials

The PAINT panel groups particle materials: **Powder** (sand, ash, gunpowder, coal), **Liquid** (water, oil, gasoline, diesel, kerosene, jet fuel, ethanol, hydraulic fluid, acid, lava), **Gas** (steam, fire, smoke, exhaust, fuel vapour, propane, hydrogen, air), **Metal** (steel, iron, copper, aluminum, lead, gold, titanium, tungsten, solder), **Struct** (wall, stone, concrete, brick, ceramic, glass, wood, rubber, plastic, ice, plant, TNT, paraffin), **Device** (heater, cooler, spark plug/igniter, fluid source, void drain, battery+ and battery-, primer), **Scenes** (ready-made machines) and a **Heat view** toggle that colours cells by temperature.

Every material has density, friction, restitution, **thermal conductivity** and heat capacity. Solid materials drawn as cells use those properties, and the material swatches shown in the right panel while a shape tool is active (or a body is selected) pick the material for bodies (density, friction, bounce, heat conduction, melting and burning all follow from it).

* **Fuels differ** – flash point, autoignition temperature, volatility, burn rate and energy: gasoline flashes below freezing and evaporates fast, ethanol burns clean, kerosene/jet fuel/diesel need heat (diesel autoignites under compression/hot surfaces – glow plug), hydraulic fluid barely burns.
* **Heat** flows by conduction through everything (copper moves it quickly, ceramic and plastic insulate), phase changes (water↔steam↔ice, metals↔molten, fuel↔vapour) absorb latent heat, and gases heat when compressed.
* **Gas pressure** acts on rigid bodies, so steam and combustion gas can push pistons. **Liquid pressure** is equalised through connected liquid, so a closed hydraulic circuit transmits force from a master piston to a slave piston.
* **Slider** – press on the body that should slide (a piston) and drag along its line. End the drag on the body it slides in (the cylinder) and the line is fixed in that body, so it works on moving machines and tumbles with them; end it on empty space and the line is fixed in the world. Rotation is locked. A fixed (anchored) body can't be the sliding one. **Spark** button – set the spark rate (or hold `E`).
* **Editing joints** – with SELECT, click a joint (the dot of a pin or motor, the line of a spring or rod) and the right panel edits it: springs have stiffness (bounces per second), damping and rest length (and can be switched to a rigid rod); rods have a length; motors have speed, power and keyed/always-on; bonds have melt temperature and strength. Delete removes it; Ctrl+Z undoes.

### Gunpowder and the shotgun

**Gunpowder** carries its own oxidiser: it burns packed solid, sealed inside a chamber with no air, and each burning grain lights its neighbours. Any spark, flame or primer flash lights it. It turns into a large amount of very hot gas, so a sealed charge builds real pressure, which pushes on bodies (a piston, a wad) and escapes through whatever opening it finds. The **SHOTGUN** scene (SCENES) uses all of it: a hammer on a slider is driven by a compressed spring (select the spring to make it stronger or weaker) and strikes a primer in the breech; the flash lights the powder charge in the chamber; the gas drives a wad and a load of lead shot down the barrel at a few hundred cells per second towards a stack of wooden and brick targets.

### Steam and liquids

Liquids flow sideways through gas-filled channels (the steam in a pipe is no wall to the water in it), so puddles level out and condensate runs back down to the boiler instead of sitting in the steam line. Gas pressure also pushes liquid: a slug of water with higher-pressure gas behind it is blown along the pipe. The **STEAM ENGINE** scene has a feedwater outlet in the boiler wall at the working water level and a modest burner under part of the floor: the level holds steady, the steam line stays dry and the engine keeps running. Heat moves through cells at a rate set by each material's conductivity and heat capacity, and through bodies as a single lump, so a thin steel body heats faster than the same shape carved from cells; given time they end up in the same place.

### Zoom, frame of reference and speed

**Zoom** in to work on small structures: the `-` / `+` buttons in the top bar, `Ctrl+wheel` (zooms about the pointer), or `Ctrl+Plus` / `Ctrl+Minus`; the `1X` button or `Ctrl+0` resets. Steps are 1x, 1.5x, 2x, 3x, 4x, 6x and 8x. When zoomed, middle-drag pans in both directions, and the strip along the bottom (and a second strip on the right edge) shows where the view is and scrubs it. Everything works at any zoom: drawing, selecting, dragging, cutting, the forms.

**Frame of reference**: the `GRID` button (or `K`) draws a grid in the world whose spacing follows the zoom, rulers along the top and left edges numbered in cells, a tick on each ruler at the pointer, and a scale bar (a round number of cells). The orange lines mark the edges of the world. Coordinates in the status bar and in the exact-value forms are the same cells.

**The world is open, not a sealed box.** Gas that reaches the edge of the world leaves, and thin wisps of gas bordering empty space disperse, so a fan or a fire no longer slowly fills the whole screen with air. The gas solver also only keeps working where gas is still moving: still air round a machine costs almost nothing. In the scenes that were slowest (fans, jet engine) the grid step is now roughly 2-3x faster. If the simulation still cannot keep up it runs in slow motion rather than falling further behind. `sandbots --shot out.bmp 300 --scene N --time` prints where the time goes.

### Known limits

Found by an adversarial review and left as they are: welds between grouped bodies are soft, so a long heavy cantilever sags, and if a member is later removed the survivors are re-welded in their sagged pose; blast impulses do not depend on mass and are not blocked by walls; evaporating liquid makes less gas per cell than boiling it; the gas solver stops iterating where pressure differences have become tiny, so very small residual gradients can persist; and the saved format changes between versions (a build refuses files from a different layout or material list).

### Jet engine

The **JET ENGINE** scene (SCENES) is a small turbojet on wheels: a fan at the front (blower mode) compresses air, a fuel injector (an emitter of propane on the duct wall) feeds the burner, a spark-plug body (an `IGNITER` body, so it travels with the engine) lights it, and a converging nozzle at the back lets the exhaust out. Press PLAY. The fan alone moves it; once the flame is lit it pushes noticeably harder, because thrust is the fan's throughput times how fast the exhaust leaves, and the exhaust speed grows with the square root of its temperature (the model reads the temperature of the hottest gas in the stream, up to about 2.6x). The fan's airflow is unchanged by the flame. Gas carries no momentum in the grid, so this is a model of the engine's push rather than a simulation of every gas molecule; a machine welded around a powered fan feels no separate grid-pressure force on its casing, so the push is clean and on-axis. Build your own: weld a duct, a fan (`FAN`, blow mode), an emitter and an `IGNITER` body into one group, keep the fuel injector and the spark plug on the wall downstream of the fan, and keep the duct shorter than the fan's reach so the airstream ends outside it.

### Fans and airflow

* **Fan** (Shapes tab): a rotor body that pulls gas in from the back and pushes it out of the front. Drag it out (the long side is the blade span) or use `Enter` for exact thickness, diameter, angle and strength. The **arrow** on the body shows the direction of airflow, the number above it is the strength in cells/s, and the blades slide faster the stronger it is. With a fan selected, `+` / `-` change the strength by 10 and `\` flips the direction (a negative strength blows the other way); the `FAN` field of the exact-value form sets it numerically. The body's own angle sets where it points.
* **How it works.** Gas cells have no velocity of their own, so a fan moves gas explicitly: along lanes of cells leading out of the exhaust face (reach grows with strength) and back from the intake face, gas hops one cell down the lane each frame, through the fan itself. The moved gas piles up ahead and drains from behind, and the existing pressure-driven gas flow does the rest, so the fan builds a real pressure difference in a duct. A **fan curve** reduces the flow as the pressure it works against approaches its stall pressure (0.02 x strength above ambient), so a fan in a closed duct settles instead of pumping without limit. The intake is open to ambient **AIR** (a new inert gas, also in the Gas palette), which is drawn in as needed; thin air leaks away where it meets the void, so a fan in the open makes a plume rather than filling the world, and inside a closed volume nothing is lost. Smoke, steam, fuel vapour and flames in the lanes are carried along, so a fan can ventilate, scavenge or supercharge a machine.
* **Forces.** A free fan is pushed the opposite way to its exhaust (a fan on a cart drives it), light bodies in the stream are blown along it (and drawn in weakly from behind), scaled by the local gas density; and the pressure on the faces of bodies in a duct is applied as before. Fans are not cut, and strength 0 turns one off. The `PRESSURE` button colours gas by pressure (blue below ambient, white about 1, red high) so you can see the duct fill. The `FANS` scene shows a wind tunnel, a closed duct and a fan-driven car.

### Vacuum mode

A fan has two modes (`M`, the `FAN: BLOW / VACUUM` button in the Shapes tab, or the `VACUUM` field of the form): **blow** draws ambient air in at the back and pushes it out; **vacuum** only pulls the gas that is actually on its intake side - it makes no air of its own - through the fan and out of the front. The suction reaches much further back (up to 60 cells), the gas speeds up as it approaches (it hops about a third as fast far upstream, full speed at the throat, and about twice that leaving the fan), so a sealed chamber on the intake side is pumped down while the far side is pressurised, and bodies on the intake side are pulled in much harder. Vacuum fans are drawn with converging cyan arrows on the intake side and a `VAC` label. They share the fan curve and thrust of the blower.

### Camera and focus

The world is three screens wide (1200 cells) and the window shows 400 of them. **Focus** (`FOCUS` button or `Z`): select a body (a vehicle's chassis, say; a welded group is followed by its centre of mass) and press focus - the camera then follows it, with a yellow corner mark on the focused body and a yellow tick on the strip at the bottom. Press it again to release. You can also pan by hand: middle-mouse drag, click or drag on the strip along the bottom of the view (it shows the whole world, the visible part, and a tick for every moving body), or `Home` / `End` / `Page Up` / `Page Down`. Panning by hand turns focus off. Scenes now have a floor the full width of the world; the `ROAD + FOCUS` scene sets focus on a fan-driven car.

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

`sandbots --selftest` runs headless regression checks (no window): the same physical situation built from a single body and from compound shapes (welded strips, a pocket cut into a piston, a chamber or boiler cut out of a block, a plug scaled to 98 %) must agree on buoyancy, gas-pressure force, heat conduction, hydraulic transmission, sealing, combustion, boiling and flame burn-out. It also replays the bugs found by the reviews of the rigid-body and grid engines (a body against the border of the world, damaged save files, a group moved through the edit form, a piston sweeping gas up, angles wrapping, stacks, slopes, fans, bonds and emitters) so they stay fixed.

`sandbots --shot out.bmp [frames] [--scene N] [--heat] [--elec] [--trace] [--g0]` runs headless (SDL dummy video driver), simulates, prints burn counts/wheel speed (and body/gas stats with `--trace`) and saves a screenshot. Scenes: `1` material reactions, `2` joints/springs/rockets, `3` scripted tool use, `11`–`13` precision/group/hose tests (13 shows the numeric form), `15` cut + scale, `16` emitters, `17` electricity, `18` bonds/wax, `19` primers, `22` fans, `23` road + focus, `24` camera controls via real events, `4` steam engine, `5` gasoline engine, `6` hydraulics, `7` conduction, `8` fuels, `9` pressure test, `10` diesel engine.
