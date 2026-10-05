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

The window opens at 1600 x 900 and can be resized (down to 1024 x 640). Press **F1** in the program for the cheat sheet of every command and shortcut, or `Ctrl+K` to search them.

Troubleshooting: if CMake says it cannot find SDL2, the development package is missing (`libsdl2-dev` / `SDL2-devel`, not just the runtime library); run `./install_deps.sh`. If you change compilers or move the folder, delete `build/` and configure again. On a machine with no display, use the headless `--shot` mode below.

## The screen

The window has five zones, laid out from its size every frame (it can be resized; the minimum is 1024 x 640):

* **Top bar** – New, Open, Save, Save as and Undo / Redo on the left (with a button that collapses the tool strip to icons); the transport dead centre: Play, Pause, Step, Stop, a speed field (0.1x to 2x) and the mode badge (`EDITING`, `RUNNING` in green, `PAUSED` in amber, which also tints the canvas frame); on the right the view toggles (heat, pressure, electric, grid and rulers), zoom out / level / in, Focus, Scenes, the command search and the `?` cheat sheet.
* **Tool strip** (left, 96 px) – two columns of icon + label buttons under the headers TOOLS (Select, Grab, Measure), CELLS (Paint, Erase), PARTS (Box, Circle, Wheel, Rocket, Pipe, Hose, Fan, Emitter), JOINTS (Pin, Motor, Spinner, Rod, Spring, Slider, Bond) and MODIFY (Cut, Scale, Group, Ungroup, Flip H, Flip V, Duplicate, Delete). The active tool is filled with the accent colour; the small letter in a button's corner is its key. Hover a button for its name, shortcut and one line of what it does. The strip scrolls when the window is short.
* **Context bar** (above the canvas, 32 px) – the options of the active tool: Paint shows the material chip, the brush size and a Replace toggle; shape tools the material chip and the Fixed toggle; Fan its strength and blow / vacuum; Emitter the material, rate and outlet side; Spring stiffness and damping; Bond the preset, melting temperature and rating; Cut box / circle, keep-cutter and Subtract; Select the align and distribute buttons and the selection filter (all, bodies, joints); Pipe and Hose the diameter, wall and segments; Measure the last reading. The right end always holds the snap controls: the grid snap toggle, the step (1, 2, 5 or 10 cells) and the reminder that `Ctrl` inverts it while dragging.
* **Canvas** – the world. Rulers, grid and a scale bar are a toggle (`I`); the strip along the bottom shows the whole world and scrubs the view; a second strip on the right edge appears when zoomed in.
* **Dock** (right, 320 px) with four tabs. **Inspector**: the selection's properties as fields that scrub when dragged, type when clicked (Enter commits, Esc reverts, Tab moves on) and accept `+ - * /` expressions; every edit is one undo step. For a body: Transform (X, Y, width and height or radius, angle), Body (material search and swatches, Fixed, density, melting point), Fan and Emitter (make this body one, or its strength / mode / direction and material / rate / outlet), Info. For a joint: its kind, then the spring's stiffness, damping and rest length (and a Spring / Rod switch), the motor's speed, power and keyed / always-on, the bond's melting temperature and rating. For several bodies: the count and groups, material and Fixed for all of them, align, distribute, group, scale and subtract. With nothing selected: the tool defaults (new springs, bonds, pipes, fans, emitters) and the world settings (gravity, spark period, the battery volts and amps and the fuel-supply material and density stamped into painted cells). **Materials**: the whole palette by category with a search field; click a chip to paint with it (or, in the Bodies grid, to make new bodies of it and recolour the selection), hover it for its density, melting, ignition and conduction, Shift+click to favourite it. **Scene**: every body, group and joint in the world with its kind, size and material; click selects, double-click zooms to it; a filter field; the Wipe cells and Wipe bodies buttons. **History**: the undo stack, one line per step; click a line to jump there. Below 1200 px wide the dock collapses to a column of tab icons and opens as a flyout over the canvas.
* **Status bar** (28 px) – the active tool's mouse and key bindings (`LMB select · Shift add · Ctrl part · drag move or box`) or the hovered control's tip, or the name of the snap a drag is using; the cursor position in cells and what is under it (a cell's material and temperature, or a body's material, kind, size, `FIXED`, `GROUP OF n`); the selection summary (`Box 30x8 10 deg STEEL`, `3 bodies (1 group)`, `Spring 2.5/s`), the zoom and the frame rate. Notifications appear as toasts above it and fade.

Every command lives in one table: the **command palette** (`Ctrl+K` or `Ctrl+Shift+P`) searches it (tools, actions, views, scenes, materials; recently used first; Enter runs), the tooltips, the context menu and the `F1` **cheat sheet** are drawn from it, so a shortcut is documented in exactly one place.

## Controls

| Input | Action |
|---|---|
| `Q` `G` `M` | Select, Grab, Measure |
| `P` `X` | Paint, Erase |
| `B` `C` `W` `R` | Box, Circle, Wheel, Rocket |
| `J` `O` `L` `S` | Pin, Motor, Rod, Spring |
| `K` | Cut |
| `1`..`9` `0` | Select, Grab, Box, Circle, Wheel, Pin, Motor, Rod, Spring, Paint (the digits shown on the strip) |
| `Space` / `N` / `Shift+Space` | Play or pause / step one frame / stop and restore the drawing |
| `Esc` / `Enter` / right click | Cancel the drag in progress (world and undo stack untouched), then clear the selection, then close panels / commit the drag at the typed values / cancel the drag, or open the context menu |
| `Tab` | Next value in the dimension field, or the next field in the Inspector |
| `Shift` `Ctrl` while dragging | Keep proportions or 15 degree steps / resize about the centre and invert snapping |
| `Ctrl+Z` `Ctrl+Y` (or `Ctrl+Shift+Z`) | Undo, redo (edit mode) |
| `Ctrl+C` `Ctrl+V` `Ctrl+D` | Copy, paste at the pointer, duplicate at the pointer |
| `Ctrl+G` `Ctrl+U` | Group, ungroup |
| `Ctrl+H` `Ctrl+Shift+H` | Flip the selection left-right, top-bottom |
| `Ctrl+A` `Del` | Select all, delete the selection |
| `Ctrl+N` `Ctrl+O` `Ctrl+S` `Ctrl+Shift+S` | New (asks first), open, save, save as |
| `Ctrl+K` / `Ctrl+Shift+P` | Command palette |
| `F1` | Cheat sheet of every command and shortcut |
| `F` / `Shift+F` | Camera follows the selected body / zoom to the selection (nothing selected: reset) |
| `H` `V` `E` `I` | Heat view, pressure view, electric view, grid and rulers |
| `-` `=` `Ctrl+0`, wheel | Zoom out, in, reset; the wheel zooms about the pointer (`Shift`+wheel sizes the brush or pipe) |
| Middle drag, `Space`+drag, `Home` `End` `PgUp` `PgDn` | Pan |
| `T` | Toggle Fixed on the selection (and for new bodies) |
| Backtick, or click and hold 0.4 s | "Select other": a list of everything under the pointer, front to back |
| Arrows / `A` `D` / `Up` `W` while running | Drive keyed motors / fire rockets |
| Arrows in edit mode | Nudge the selection 1 cell (`Shift` 10, `Ctrl` 0.25) |
| `[` `]` | Brush size |
| `Z` (hold) | Fire the spark plugs now |

Mouse, with Select: click selects the topmost body or joint, clicking the same spot again cycles downwards, `Shift` adds or removes, `Ctrl` picks one part of a group; drag a selected body to move the selection; drag on empty space to box-select (left to right picks what lies wholly inside, right to left whatever the box crosses). Right click on a body or joint opens its context menu (Properties, Duplicate, Flip, Group / Ungroup, Fixed, Material, Delete); on empty canvas it offers Paste here, Select all, Add box / circle here and Zoom to fit. Right click never erases: the Erase tool and `Shift`+paint do.

### Edit mode, play and stop, files

The game starts in **edit mode**: nothing moves, so you can draw, place bodies and joints, and set up a machine in a frozen state. **Play** (or `Space`) takes a snapshot of everything (grid cells, bodies, joints, bonds, emitters, battery settings) and starts the simulation; **Pause** / `Space` freezes and resumes it; **Step** (`N`) advances one frame; **Stop** (`Shift+Space`) restores the snapshot, so the machine is exactly as you drew it. The speed field beside the transport slows the run down (to 0.1x) for inspection or runs it at 2x. The editing tools stay usable on the paused world; undo works in edit mode. Scenes and loaded files open in edit mode.

**New** asks before clearing everything (`Ctrl+Z` brings the drawing back anyway). **Save** writes to the current file (or asks for a name the first time), **Save as** asks for a name, **Open** lists the saved files, newest first (click one, or type a name; Enter or a double click loads). Files are `saves/NAME.sbot` next to where you run the game. Saving while playing stores the design as it was drawn, not the mid-run state. Files can only be read by a build with the same data layout.

### Body tools

* **Box / Circle** – drag to size (box: drag a rectangle; circle: drag out the radius). While you drag, a **dimension field** follows the pointer: digits typed go into it (width, then `Tab` for the height, then the angle; a circle has the radius, a pipe its length and diameter), `+ - * /` expressions are allowed, `Backspace` returns a value to the mouse, `Enter` commits at those values, `Esc` cancels. The same numbers are in the Inspector afterwards, so nothing can only be done one way.
* **Wheel** – a circle with high friction; if it is dropped on top of another body it is automatically pinned to it with a keyed motor, so a car is just a box plus two wheels.
* **Pin** – click where two bodies overlap or touch to hinge them (with only one nearby it is pinned to the world).
* **Motor** – a pin whose angular velocity is driven by the arrow keys. **Spinner** always spins (windmills, conveyors). Select a motor to set its speed, power and keyed / always-on in the Inspector.
* **Rod / Spring** – drag from one body (or empty space = world) to another. The context bar sets the stiffness and damping of new springs; click any joint with Select to edit it later (see below).
* **Rocket** – drag to choose the thrust direction; hold `Up` or `W` to fire while running.
* **Select** – click a body to select it; clicking the same spot again cycles through the bodies stacked there (including parts of a group), click and hold (or press the backtick) for a list of everything under the pointer. Shift adds/removes, dragging on empty space draws a selection box. **Drag a selected body to move it** (the whole selection moves together; joints to bodies outside the selection keep their anchors). With two or more selected, one is the red "cutter" (the last one clicked). The selection filter in the context bar limits what clicks and box-selects pick.
* **Handles** – a selected box shows eight small squares on its corners and edge midpoints and a round handle on a stalk above it; a circle shows four. Drag an edge or corner to resize: the opposite edge stays where it is, hold `Ctrl` to resize about the centre, and `Shift` on a corner to keep the proportions (nothing gets thinner than one cell). Drag the round handle to rotate about the centre; `Shift` snaps to 15° steps. The handles sit in the body's own frame, so they turn with it; the dimension field beside the pointer shows the size or angle and takes typed values. With grid snap on, the dragged edge or corner lands on a grid line, and a moved body is dropped on grid coordinates. A whole group shows only the rotation handle and turns as one; `Ctrl`+click a part to resize it alone. Joints and welds follow every change, `Ctrl+Z` undoes a drag and `Esc` abandons one. Handles are hidden while the simulation runs (pause or stop to edit).
* **Snapping** – the Snap toggle at the right end of the context bar rounds drawn shapes, dropped bodies and dragged edges to a 1 / 2 / 5 / 10 cell grid; holding `Ctrl` inverts it for one drag. Smart snap (on by default, a command in the palette) makes a dragged body's edges and centre stick to other bodies' edges and centres within 6 px, draws the guide line it used and names it in the status bar (`Snap: left of Box 60x30 STEEL 12`).
* **Status bar and hover line** – with one body selected the status bar shows its shape, size, angle and material (`Box 30x8 10 deg STEEL`, `Circle R10 WOOD`, plus `FIXED` for a static body); with several it shows `n bodies (k groups)`; with a joint selected, the joint and its key value (`Spring 2.5/s`, `Rod 40`, `Motor 6 rad/s`, `Bond Paraffin`). The centre names whatever is under the pointer: a cell's material and temperature, or a body's material, shape and size, with `FIXED`, `GROUP OF n` and its fan or emitter settings. Hovering a material chip shows the material's density, friction, melting / boiling / ignition temperature, `explosive` and `conducts`.
* **Group / Ungroup** – welds the selected bodies into one rigid object; parts stay individually selectable by click-cycling. Parts of a group never collide with each other.
* **Pipe** – hollow tube between two points (two welded walls). **Hose** – a chain of pipe segments hinged together so it bends. The diameter, wall and (for hoses) segment count are in the context bar; `Shift`+wheel also sets the diameter.
* **Inspector** – the exact numbers, in grid cells and degrees: X, Y, width and height (or radius) and angle of the selected body, its material and Fixed flag, and the fan or emitter settings; a group moves and turns as one. Fields scrub when dragged (`Shift` fine, `Ctrl` coarse), type when clicked, take `+ - * /` expressions and clamp on commit; every edit is one undo step.
* **Cut** – the Cut tool: choose box or circle in the context bar, drag over any part of the drawing and that area is removed from every body it touches. Each damaged body becomes a welded group of small rectangles (steps down to 0.5 cell; the hole is never smaller than the cut). Undo brings it back. **Subtract** is the selection version: select the target(s), make the cutter the red primary, press Subtract (context bar or Inspector); the cutter is consumed unless Keep cutter is on. Wheels, rockets and emitters can't be cut.
* **Scale** – with bodies selected, the Scale button opens a percentage (default 98) that resizes the whole selection about its centre, groups included; joints follow. Cut a hole with a shape, scale that shape to 98 % and you have a tight-fitting plug, valve ball or piston.
* **Emitter** – a body that endlessly produces the powder, liquid or gas chosen in the context bar (or in the Inspector once made), out of **one side** (`+X`, `-X`, `+Y`, `-Y` in the block's own frame, so it turns with the block; an arrow marks the outlet). The material is thrown a few cells along that side's normal (further at higher rates), stopping at the first obstruction, so an outlet placed against a pipe or channel fills it. `All` spills from every face. Set the rate (cells per second) and the material in the Inspector; any box can be made an emitter there, or stop being one. It can be fixed, welded into a group or travel with a machine. If the outlet is blocked (by the liquid it is feeding, say) it simply waits, which makes a self-regulating feed: put the outlet at the level you want the water to stand at. (The old grid "SOURCE" cell survives as FUEL SUPPLY; it emits the material and density set under World in the Inspector, stamped into the cell as it is painted, the way the battery settings are stamped into battery cells.)
* **Bond** – a temporary weld. Click where two bodies overlap or touch (with only one nearby it bonds to the world). Pick a preset in the context bar: `Paraffin` (melts at 55 °C, holds 10x the weight it carries), `Solder` (190 °C, 40x), `Epoxy` (260 °C, 120x) or `Shear pin` (never melts, snaps at 30x), or type the melting temperature and rating. Strength is rated against the weight of the lighter bonded body (a whole group counts as one), so a bond holds a block no matter how heavy it is, but gives way to a hard sustained pull. Brief jolts don't count - the load is averaged over about a tenth of a second. The bond softens as it nears its melting point, and is heated by the bodies it joins and by flame, hot gas or molten metal within a few cells of the seam. It is drawn as a seam between the bodies that goes from cream (paraffin), grey (solder), amber (epoxy) or dark (shear pin) towards red as it heats. Broken bonds drop out and the bodies collide normally again.
* **Flip** (Flip H / Flip V, `Ctrl+H` / `Ctrl+Shift+H`) – mirrors the selected bodies left-right or top-bottom about the centre of their bounding box. Joints, welds, slider rails, fans, emitter outlets and motor directions all follow, so a mirrored machine runs as the mirror image of the original.
* **Duplicate** (`Ctrl+D`) – copies the selected bodies with their joints and welds, placing the copy at the cursor (or just beside the originals when the cursor is off the canvas) and selecting it so you can drag it into place. The clipboard is left untouched.
* **Measure** – drag between two points to read the length in cells, dx, dy and the angle in degrees (snap applies). The last measurement stays on screen and in the context bar until the next drag or a change of tool; nothing in the world is changed.
* **Grab** – drag bodies around with a soft spring, even while the simulation runs. **Delete** – `Del` removes the selection (or the context menu, or the strip's Delete button); the Delete tool in the palette removes whatever body or joint is clicked.

### Materials

The Materials tab groups particle materials: **Powder** (sand, ash, gunpowder, coal), **Liquid** (water, oil, gasoline, diesel, kerosene, jet fuel, ethanol, hydraulic fluid, acid, lava), **Gas** (steam, fire, smoke, exhaust, fuel vapour, propane, hydrogen, air), **Metal** (steel, iron, copper, aluminum, lead, gold, titanium, tungsten, solder), **Struct** (wall, stone, concrete, brick, ceramic, glass, wood, rubber, plastic, ice, plant, TNT, paraffin), **Device** (heater, cooler, spark plug/igniter, fuel supply, drain, battery+ and battery-, primer); the Scenes button in the top bar holds the ready-made machines and the heat view (`H`) colours cells by temperature.

Every material has density, friction, restitution, **thermal conductivity** and heat capacity. Solid materials drawn as cells use those properties, and the Bodies grid of the Materials tab (or the Inspector's material swatches) picks the material for bodies (density, friction, bounce, heat conduction, melting and burning all follow from it).

* **Fuels differ** – flash point, autoignition temperature, volatility, burn rate and energy: gasoline flashes below freezing and evaporates fast, ethanol burns clean, kerosene/jet fuel/diesel need heat (diesel autoignites under compression/hot surfaces – glow plug), hydraulic fluid barely burns.
* **Heat** flows by conduction through everything (copper moves it quickly, ceramic and plastic insulate), phase changes (water↔steam↔ice, metals↔molten, fuel↔vapour) absorb latent heat, and gases heat when compressed.
* **Gas pressure** acts on rigid bodies, so steam and combustion gas can push pistons. **Liquid pressure** is equalised through connected liquid, so a closed hydraulic circuit transmits force from a master piston to a slave piston.
* **Slider** – press on the body that should slide (a piston) and drag along its line. End the drag on the body it slides in (the cylinder) and the line is fixed in that body, so it works on moving machines and tumbles with them; end it on empty space and the line is fixed in the world. Rotation is locked. A fixed body can't be the sliding one. The spark plug period is under World in the Inspector (hold `Z` to fire them now).
* **Editing joints** – with Select, click a joint (the dot of a pin or motor, the line of a spring or rod) and the Inspector edits it: springs have stiffness (bounces per second), damping and rest length (and can be switched to a rigid rod); rods have a length; motors have speed, power and keyed/always-on; bonds have melt temperature and strength. Delete removes it; Ctrl+Z undoes.

### Gunpowder and the shotgun

**Gunpowder** carries its own oxidiser: it burns packed solid, sealed inside a chamber with no air, and each burning grain lights its neighbours. Any spark, flame or primer flash lights it. It turns into a large amount of very hot gas, so a sealed charge builds real pressure, which pushes on bodies (a piston, a wad) and escapes through whatever opening it finds. The **Shotgun** scene (Scenes) uses all of it: a hammer on a slider is driven by a compressed spring (select the spring to make it stronger or weaker) and strikes a primer in the breech; the flash lights the powder charge in the chamber; the gas drives a wad and a load of lead shot down the barrel at a few hundred cells per second towards a stack of wooden and brick targets.

### Gas has momentum

Pressure equalisation alone spreads gas like treacle: nothing travels faster than the pressure difference across one cell carries it, and the moment the difference is gone the gas stops. On top of the pressure solver every open gas cell now carries a **velocity**: the pressure gradient accelerates it, drag and walls slow it, neighbouring parcels trade momentum, and the gas is carried along it, taking its heat with it. A stream therefore keeps going when the push stops (a puff of smoke coasts a dozen cells down a pipe after its fan is switched off), drags the gas beside it along, fills a cylinder through a port about one and a half to two times faster than diffusion did, and costs more to force through a restriction the faster it goes, so the pressure drop across an orifice grows with the flow. Fans bring the gas at their rotor up to speed (scaled by the fan curve, so a stalled fan pushes nothing) and the jet coasts from there, so it carries on past the end of the lane and round corners. A fan in the open therefore runs close to free delivery instead of stalling on its own exhaust: the FANS scene's wind tunnel and car are noticeably livelier, so lower a fan's strength if a scene was tuned to the old behaviour. Empty cells inside a machine are vacuum and pull gas in; in the open they stand for ambient air and do not. The flow field is transient (a loaded world starts still) and only cells that are moving or whose surroundings changed pressure are worked on, so a still cloud costs nothing and the heaviest open-air plumes cost one to three milliseconds more per frame. `World::gasMomentum` (default on, not saved) switches the layer off, and `sandbots --shot ... --no-momentum` does the same for headless scene runs.

### Combustion needs oxygen

Fuel no longer burns by itself. A burning cell draws its oxygen from the open air (empty space, or smoke, steam and exhaust, connected to the edge of the world, which never runs out) or from **AIR** cells, which it uses up: each fuel has an air demand (about 1 unit of air per unit of hydrogen, 2 of propane, 2.5 of gasoline vapour, 3 of the heavier oils and solids), and a vapour cell with too little air around it burns only the part that air supports, leaving the rest as unburnt vapour for when more air arrives. Empty cells inside a sealed machine are vacuum: vapour, propane or hydrogen sealed in with a spark plug does nothing, a wood block in a vacuum box only gets hot, and a flame in vacuum dies quickly. Burning solids and liquids need an oxygen-bearing face and consume a little air each frame; starved of it they go out but stay hot and relight when air returns. Gunpowder and TNT carry their own oxidiser and are unaffected. So a sealed engine needs an air supply as well as a fuel supply: an AIR emitter or a FUEL SUPPLY cell set to AIR beside the fuel one (the GAS and DIESEL ENGINE scenes have one), a fan intake, or a port open to the outside. A fuel/air charge laid as alternate cells of vapour and air burns completely, where a solid block of vapour only burns at its edges. `World::needAir` (default on, not saved) switches the rule off, and `sandbots --shot ... --no-air` does the same for headless scene runs.

### Steam and liquids

Liquids flow sideways through gas-filled channels (the steam in a pipe is no wall to the water in it), so puddles level out and condensate runs back down to the boiler instead of sitting in the steam line. Gas pressure also pushes liquid: a slug of water with higher-pressure gas behind it is blown along the pipe. The **Steam engine** scene has a feedwater outlet in the boiler wall at the working water level and a modest burner under part of the floor: the level holds steady, the steam line stays dry and the engine keeps running. Heat moves through cells at a rate set by each material's conductivity and heat capacity, and through bodies as a single lump, so a thin steel body heats faster than the same shape carved from cells; given time they end up in the same place.

### Buoyancy

A body's lift is the weight of the liquid its submerged part displaces. Each frame the engine samples the cells just outside the body's outline; where the wet samples all lie below the ones in open air (measured along gravity, so flipped gravity works too) it reads the liquid level off the grid beside the body and clips the body's shape at that level: a box as a polygon, a disc as a circular segment. The submerged area times the liquid's density gives the lift, applied at the centroid of the submerged part, so a wood plate floats with 40 % of its height out of the water, a tilted plate rights itself, and a welded group is clipped member by member and floats like the single block it replaces. Where the liquid has no readable surface at the body (liquid against one face of a piston, a wave washing over it, a piston in its bore) the wetted share of the outline stands in, as before; drag always scales with the wetted outline. The surface is read in whole cells, so small floaters sit about half a cell deeper than the ideal.

### Zoom, frame of reference and speed

**Zoom** in to work on small structures: the wheel over the canvas (it zooms about the pointer), the `-` / `+` buttons in the top bar or the `-` / `=` keys; the zoom button or `Ctrl+0` resets. Steps are 1x, 1.5x, 2x, 3x, 4x, 6x and 8x. When zoomed, middle-drag pans in both directions, and the strip along the bottom (and a second strip on the right edge) shows where the view is and scrubs it. Everything works at any zoom: drawing, selecting, dragging, cutting, the Inspector.

**Frame of reference**: the grid toggle in the top bar (or `I`) draws a grid in the world whose spacing follows the zoom, rulers along the top and left edges numbered in cells, a tick on each ruler at the pointer, and a scale bar (a round number of cells). The orange lines mark the edges of the world. Coordinates in the status bar, the dimension field and the Inspector are the same cells.

**The world is open, not a sealed box.** Gas that reaches the edge of the world leaves, and thin wisps of gas bordering empty space disperse, so a fan or a fire no longer slowly fills the whole screen with air. The gas solver also only keeps working where gas is still moving: still air round a machine costs almost nothing. In the scenes that were slowest (fans, jet engine) the grid step is now roughly 2-3x faster. If the simulation still cannot keep up it runs in slow motion rather than falling further behind. `sandbots --shot out.bmp 300 --scene N --time` prints where the time goes.

### Known limits

Found by an adversarial review and left as they are: welds between grouped bodies are soft, so a long heavy cantilever sags, and if a member is later removed the survivors are re-welded in their sagged pose; blast impulses do not depend on mass and are not blocked by walls; evaporating liquid makes less gas per cell than boiling it; the gas solver stops iterating where pressure differences have become tiny, so very small residual gradients can persist; and the saved format changes between versions (a build refuses files from a different layout or material list). In the bulk-flow layer the pressure drop across an orifice grows in proportion to the flow rather than with its square, a free jet entrains the gas beside a side opening only weakly (it is a vacuum fan's suction that empties a venturi's pocket), bodies do not hand their velocity to the gas directly (a piston compresses it and the pressure gradient does the rest), and a piston driven into the last cell of a sealed gas pocket loses that gas, since it has nowhere to go. Buoyancy reads the liquid level in whole cells, so small floaters sit about half a cell deeper than the ideal, and a float whose sides are within a cell or two of a wall falls back to the wetted-outline model.

### Jet engine

The **Jet engine** scene (Scenes) is a small turbojet on wheels: a fan at the front (blower mode) compresses air, a fuel injector (an emitter of propane on the duct wall) feeds the burner, a spark-plug body (an `IGNITER` body, so it travels with the engine) lights it, and a converging nozzle at the back lets the exhaust out. Press PLAY. The fan alone moves it; once the flame is lit it pushes noticeably harder, because thrust is the fan's throughput times how fast the exhaust leaves, and the exhaust speed grows with the square root of its temperature (the model reads the temperature of the hottest gas in the stream, up to about 2.6x). The fan's airflow is unchanged by the flame. The thrust is a model of the engine's push rather than a sum over every gas molecule (the bulk-flow layer gives the exhaust jet its reach and entrainment, not the force on the engine); a machine welded around a powered fan feels no separate grid-pressure force on its casing, so the push is clean and on-axis. Build your own: weld a duct, a fan (`FAN`, blow mode), an emitter and an `IGNITER` body into one group, keep the fuel injector and the spark plug on the wall downstream of the fan, and keep the duct shorter than the fan's reach so the airstream ends outside it.

### Fans and airflow

* **Fan** (PARTS): a rotor body that pulls gas in from the back and pushes it out of the front. Drag it out (the long side is the blade span), typing the size into the dimension field if you like; the strength and mode of new fans are in the context bar. The **arrow** on the body shows the direction of airflow, the number above it is the strength in cells/s, and the blades slide faster the stronger it is. With a fan selected, the Inspector's Fan section sets the strength, blow / vacuum and flips the direction (a negative strength blows the other way); any box can be made a fan there. The body's own angle sets where it points.
* **How it works.** A fan moves gas explicitly: along lanes of cells leading out of the exhaust face (reach grows with strength) and back from the intake face, gas hops one cell down the lane each frame, through the fan itself. The moved gas piles up ahead and drains from behind, and the pressure-driven gas flow does the rest, so the fan builds a real pressure difference in a duct. The gas at the rotor is also brought up to the fan's speed, so the jet carries its momentum beyond the lane, bends round corners and drags the gas beside it along (see *Gas has momentum*). A **fan curve** reduces the flow as the pressure it works against approaches its stall pressure (0.02 x strength above ambient), so a fan in a closed duct settles instead of pumping without limit. The intake is open to ambient **AIR** (a new inert gas, also in the Gas palette), which is drawn in as needed; thin air leaks away where it meets the void, so a fan in the open makes a plume rather than filling the world, and inside a closed volume nothing is lost. Smoke, steam, fuel vapour and flames in the lanes are carried along, so a fan can ventilate, scavenge or supercharge a machine.
* **Forces.** A free fan is pushed the opposite way to its exhaust (a fan on a cart drives it), light bodies in the stream are blown along it (and drawn in weakly from behind), scaled by the local gas density; and the pressure on the faces of bodies in a duct is applied as before. Fans are not cut, and strength 0 turns one off. The pressure view (`V`) colours gas by pressure (blue below ambient, white about 1, red high) so you can see the duct fill. The Fans scene shows a wind tunnel, a closed duct and a fan-driven car.

### Vacuum mode

A fan has two modes (Blow / Vacuum in the context bar for new fans, in the Inspector for a selected one): **blow** draws ambient air in at the back and pushes it out; **vacuum** only pulls the gas that is actually on its intake side - it makes no air of its own - through the fan and out of the front. The suction reaches much further back (up to 60 cells), the gas speeds up as it approaches (it hops about a third as fast far upstream, full speed at the throat, and about twice that leaving the fan), so a sealed chamber on the intake side is pumped down while the far side is pressurised, and bodies on the intake side are pulled in much harder. Vacuum fans are drawn with converging cyan arrows on the intake side and a `VAC` label. They share the fan curve and thrust of the blower.

### Camera and focus

The world is three screens wide (1200 cells) and the canvas shows a third or so of it at 1x. **Focus** (the Focus button or `F`): select a body (a vehicle's chassis, say; a welded group is followed by its centre of mass) and press focus - the camera then follows it, with a yellow corner mark on the focused body and a yellow tick on the strip at the bottom. Press it again to release. You can also pan by hand: middle-mouse drag, click or drag on the strip along the bottom of the view (it shows the whole world, the visible part, and a tick for every moving body), or `Home` / `End` / `Page Up` / `Page Down`. Panning by hand turns focus off. Scenes now have a floor the full width of the world; the Road + focus scene sets focus on a fan-driven car.

### Electricity

* **Battery cells** (Devices): paint a `BATTERY+` blob and a `BATTERY-` blob, join them with conductors, done. The Volts and Amps fields under World in the Inspector (the context bar shows them while a battery material is chosen) set the **voltage** (1 V – 70 kV) and **amperage** (up to 400 A) stamped into battery cells you paint from then on; repaint cells to change a battery.
* The conducting cells (all the metals, solder, water, and rigid bodies made of metal) form a resistor network solved every frame. The `+` terminal sits at its voltage, the `-` terminal at 0 V; if the circuit would draw more than the battery's rated amperage the voltage sags to hold the current at that limit. Conductances: copper 400, aluminium 250, gold 280, tungsten 70, iron 45, steel 40, lead 20, solder 120, water 0.4 (per cell edge).
* **Heating** – current heats conductors by I²R: a thin tungsten filament glows and lights fuel vapour, a lead wire is a fuse that melts and opens the circuit, solder is a thermal fuse, copper stays cool. A conducting rigid body that touches wire cells (a lever, a falling bar, a piston) closes the circuit like a switch.
* **Sparks** – where two conductors are separated by an air gap (up to 9 cells, straight or diagonal) and the potential difference exceeds about 3 kV per cell of gap (higher in compressed gas, lower in thin gas), the gap breaks down: the arc is drawn, heats the channel and ignites fuel mixtures in it. That is a spark plug: two electrodes, a gap, a high-voltage battery, and a closing contact. The electric view (`E`) colours conductors by potential and current.
* Limits: batteries are grid cells (they don't move), grounds are always 0 V, and the arc doesn't load the source. Good enough to build ignition circuits; to be refined later.

### Frangible materials and primers

* **Paraffin** (Building) is a solid wax: it melts at 55 °C into liquid wax, burns, and as a plug it gives way under pressure — it fails layer by layer when the pressure difference across it exceeds 5 per cell of thickness, so a thick plug holds more. **Solder** (Metals) is a conductive metal that melts at 190 °C.
* **Primer** (Devices, also a body material): impact-sensitive. When a body strikes it hard enough (closing speed over 70 cells/s and impact energy over 3·10⁴) it fires once. A primer **body** goes spent and throws a flash of flame out of the end opposite the one that was struck (a flash hole), which ignites fuel and powder there. **Primer cells** painted on the grid behave the same when struck, and also fire from heat (230 °C) or flame, passing the flash along the cells. Firing pin: any hard, fast body (a steel pin on a slider, a dropped weight).

### Scenes

* **Steam engine** – boiler, gate valve driven by an eccentric, piston, crank and flywheel. Runs continuously.
* **Hydraulics** – master/slave cylinders with a mechanical advantage.
* **Conduction** – bars of different metals/insulators heated at one end.
* **Fuels** – seven liquids on a warming plate with spark plugs, showing flash-point order.
* **Gasoline / diesel engines** – intake and exhaust gate valves, spark or glow-plug ignition, fuel supply. *Limitation:* these are demonstrators spun up by a starter; combustion works and cycles the piston, but the simplified gas model loses more energy on compression than it gains, so the flywheel slowly spins down rather than self-sustaining.

Other things to try: lava on water makes stone and steam; fire spreads through wood, plants and oil; acid eats most solids; gunpowder and TNT chain-explode and throw bodies; a light ball floats on oil on water.

## Developer flags

`sandbots --selftest` runs headless regression checks (no window): the same physical situation built from a single body and from compound shapes (welded strips, a pocket cut into a piston, a chamber or boiler cut out of a block, a plug scaled to 98 %) must agree on buoyancy, gas-pressure force, heat conduction, hydraulic transmission, sealing, combustion, boiling and flame burn-out. It also replays the bugs found by the reviews of the rigid-body and grid engines (a body against the border of the world, damaged save files, a group moved through the edit form, a piston sweeping gas up, angles wrapping, stacks, slopes, fans, bonds and emitters) so they stay fixed.

`sandbots --shot out.bmp [frames] [--scene N] [--win WxH] [--heat] [--pressure] [--elec] [--trace] [--g0] [--no-air] [--no-momentum] [--tool N] [--zoom Z] [--cam X] [--hover X Y] [--help-card] [--scenes]` runs headless (SDL dummy video driver), simulates, prints burn counts/wheel speed (and body/gas stats with `--trace`) and saves a screenshot of the whole window with its interface. `--win` sets the window size (default 1600 x 900, minimum 1024 x 640); `--tool N` selects a tool by number (0 Paint, 1 Box, 2 Circle, 3 Wheel, 4 Rocket, 5 Pin, 6 Motor, 7 Spinner, 8 Rod, 9 Spring, 10 Grab, 11 Delete tool, 12 Slider, 13 Select, 14 Pipe, 15 Hose, 16 Emitter, 17 Bond, 18 Fan, 19 Cut, 20 Measure); `--hover X Y` parks the pointer at a window pixel so the status bar's read-out and tooltips can be checked; `--help-card` opens the F1 cheat sheet and `--scenes` the scene browser. Scenes: `1` material reactions, `2` joints/springs/rockets, `3` scripted tool use, `11`–`13` precision/group/hose tests (12 selects a part of a group, 13 shows the Hose tool's context bar), `15` cut + scale, `16` emitters, `17` electricity, `18` bonds/wax, `19` primers, `22` fans, `23` road + focus, `4` steam engine, `5` gasoline engine, `6` hydraulics, `7` conduction, `8` fuels, `9` pressure test, `10` diesel engine.

Scenes `24`, `25`, `30`, `35` and `36` are event-driven tests: they push real SDL mouse and keyboard events through the normal event path, with every coordinate computed from the window layout, and print what happened. `25` is the editor test, one `yes` / `NO` line per behaviour: the layout tiles the window at twelve sizes and collapses the dock below 1200 px; drawing, click-cycling, moving, undo and redo; copy, paste, the Cut tool, Shift+click and subtract; box select in both directions, duplicate, flip; measure; the handles with Ctrl and Shift; Esc cancelling a drag with the undo stack untouched; grid snap, Ctrl inverting it, smart snap and its guide; zoom to the selection; the dimension field with an expression; an Inspector field edit that pushes one undo entry; the dock tabs; the command palette; the context menu; select other by click-hold and by backtick; right click cancelling a drag; the transport keys. `24` is the camera, `30` joints through clicks and the Inspector, `35` zooming, `36` nudging.

`sandbots --ui-gallery out.bmp [--win WxH]` renders the main interface states one under the other into one tall image (the default view with a body selected, each tool's context bar, the Inspector for a body, a joint, a fan, an emitter, a multi-selection and nothing selected, the Materials, Scene and History tabs, the palette, the context menu, the scene browser, the file dialog, the cheat sheet, a toast and the paused run) and each state as its own `out-NN.bmp` beside it, for review.
