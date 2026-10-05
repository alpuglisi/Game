# SandBots interface redesign

This document is the design the new interface is built from. It replaces the first interface, which grew out of a
falling-sand toy: a flat wall of uppercase text buttons, a properties column that changed shape with every tool, a modal
numeric form, fixed pixel layout, and no way to find a command except by reading every label.

## 1. What the interface has to do

Everything below exists today and must survive the redesign, reorganised rather than removed.

**Content.** Two kinds of things live in the world and the interface must make the difference obvious:

- *Cells*: painted particles and solids on a grid of 1200 x 240. Powders (sand, ash, gunpowder, coal), liquids (water,
  oil, gasoline, diesel, kerosene, jet fuel, ethanol, hydraulic fluid, acid, lava), gases (steam, fire, smoke, exhaust,
  fuel vapour, propane, hydrogen, air), metals, structural solids (wall, stone, concrete, brick, ceramic, glass, wood,
  rubber, plastic, ice, plant, TNT, paraffin), devices (heater, cooler, spark plug, fuel supply, drain, battery +/-,
  primer). Painted with a round brush of radius 1..24; the eraser removes cells. Battery cells take a voltage and
  current setting as they are painted; fuel supply cells take a payload material and a density.
- *Bodies*: rigid boxes and circles with a material (metals, building solids), fixed or free. Specialised bodies:
  wheels (high friction, auto-pinned with a keyed motor when dropped on a body), rockets (thrust along their axis,
  fired with Up/W), fans (airflow along +x, blow or vacuum mode, strength, flips), emitters (produce a material from
  one face at a rate), pipes (two welded walls) and hoses (chains of pipe segments).
- *Joints*: pin, motor (keyed to the arrow keys or always on; speed, power), rod (fixed length), spring (stiffness,
  damping, rest length), slider (a body kept on a line fixed in the world or in another body; rotation locked), bond
  (a frangible weld with a melting temperature and a strength rating; presets paraffin, solder, epoxy, shear pin).
  Welds hold groups together and are not edited directly.
- *Groups*: welded sets of bodies that act as one object but remain individually selectable.

**Building operations.** Draw a shape by dragging (snap to a 1/2/5/10 cell grid when on); create by exact numbers
(position, size, angle, and the machine settings); select (click, click-again to cycle through stacked bodies, Shift
adds, Ctrl picks a part of a group, drag on empty space box-selects); move by dragging or nudging (1 cell, Shift 10,
Ctrl 0.25); resize and rotate with handles (opposite edge fixed, Ctrl about the centre, Shift keeps proportions or
snaps to 15 degrees); edit a body's material, fixed flag, fan and emitter settings; edit a joint's parameters; cut a
box or circle out of bodies; subtract one selected body from the others; scale the selection by a percentage; group
and ungroup; copy, paste, duplicate; flip left-right and top-bottom; delete bodies and joints; measure; undo and redo
everything; grab (drag with a soft spring while the simulation runs); wipe all cells or all bodies.

**Simulation.** Edit mode freezes the world. Play snapshots the drawing and runs; pause and resume; single step; stop
restores the snapshot exactly. Gravity flips. Spark plugs pulse at a chosen period or while a key is held. While
playing the arrow keys drive keyed motors and Up fires rockets. Views: heat (temperature colours), pressure (gas
pressure colours), electric (voltage and current), reference grid with rulers and a scale bar, zoom 1x..8x about the
pointer, panning by middle-drag, keyboard and a scroll strip, and a focus mode where the camera follows a body.

**Files and scenes.** New, open, save, save as (`saves/NAME.sbot`; the save browser lists files); fourteen ready-made
scenes (demo, steam engine, gas engine, diesel engine, hydraulics, conduction, fuels, electric, bonds, primer, fans,
shotgun, jet engine, road + focus). The headless runner (`--shot`, `--scene`, `--trace`, `--selftest`) must keep
working because the selftests and the event-driven editor test depend on it.

**Information.** A hint for whatever is under the pointer; what is selected (shape, size, angle, material); cursor
position in cells; frame rate; notifications for what just happened; a help card.

## 2. What was wrong

- Organisation followed history, not the user's task: PAINT and ERASER sat at the bottom under a "PARTICLES" header,
  bodies were split across SHAPES / MACHINES / JOINTS / EDIT, and the properties column rebuilt itself differently
  for every tool, so nothing stayed where you last saw it.
- Precision was a separate mode: a modal form you had to open, with its own keys, instead of numbers you can type or
  drag in place.
- Discovery was by reading: no icons, no search, shortcuts hidden in a help card, no right-click menu.
- The layout was fixed at one window size, computed from constants used in fifty places; three overflow bugs
  (clipped form labels, help card spilling, truncated toolbar) were symptoms of that.
- Text was an uppercase-only 5x7 font drawn pixel by pixel.

## 3. Architecture

- `src/font.hpp`: a full printable-ASCII mixed-case bitmap font with a cached texture atlas, plus the old small face
  for world labels.
- `src/icons.hpp`: 16 px one-bit icons for every tool and action, drawn from a cached atlas.
- `src/ui.hpp` / `src/ui.cpp`: a small immediate-mode toolkit drawn with SDL2 primitives: panels and docks with
  scrolling, buttons (icon, label, shortcut badge), toggles and segmented controls, sliders, numeric fields that drag
  to adjust and click to type, text fields, dropdowns, collapsible sections, swatch grids, list views, tabs,
  tooltips, toasts, modal dialogs, context menus, a command palette. One theme table holds every colour and spacing.
- `src/main.cpp`: the application. The world rendering, scenes, editing logic (selection, handles, cut, undo,
  clipboard, flip, measure) and the physics coupling stay; the chrome is rebuilt on the toolkit with a layout that is
  computed from the window size every frame.
- `--shot --scene 25` remains the event-driven editor test and is rewritten against the new layout; `--ui-gallery`
  renders the main interface states to one image for review.

## 4. Verification

- The full app builds warning-free; `--selftest` passes (the chrome must not change physics).
- The editor test drives the new interface through real SDL events and prints one yes/NO line per behaviour.
- Every interface state is rendered headless and inspected: default, each tool's context bar, the inspector for a
  body, a joint, a fan and an emitter, the command palette, a context menu, the scene browser, the file dialog, the
  help card, a narrow window and a wide window.

## 5. Where things come from

The design follows what the reference tools agree on, found by surveying them (sources in the research notes):

- *CAD* (Fusion 360, Onshape, SolidWorks, SketchUp, FreeCAD, Blender, Shapr3D, Figma): one modal-tool contract
  (left button or Enter commits, right button or Esc cancels, Esc with nothing active clears the selection); numbers
  typed during the drag instead of a form; a fixed right-hand inspector whose fields scrub and accept expressions; a
  command search; a status bar that lists the active tool's mouse and key bindings; "select other" for stacked
  objects; snapping as a toggle plus a transient modifier, with drawn targets; canvas-first chrome in fixed islands.
- *Physics sandboxes* (Algodoo, Incredibots, The Powder Toy, Sandboxels, Besiege, Garry's Mod, Poly Bridge 2,
  Universe Sandbox, LittleBigPlanet, Spore): one docked palette with sections rather than a global mode switch;
  right-click settings for the selection; slider plus number everywhere; a hover readout for whatever is under the
  cursor; categories with search and favourites; a separate simulation bar that keeps tools live while paused and
  dims destructive ones while running; never bind panning to the select button.
- *Game editors and custom toolkits* (Godot, Unity, Tiled, LDtk, Aseprite, Dear ImGui, microui, Nuklear): the
  five-zone frame (top bar, tool strip, context bar over the canvas, tabbed right dock, status bar); transport
  controls dead centre; collapsible inspector sections with remembered fold state; drag-float fields (scrub, click
  to type, Enter commits, Esc reverts); click-outside closes popups, Esc closes the topmost, modals dim and block;
  tooltips after 0.4 s with the shortcut in secondary text, shown even for disabled items; monochrome 16 px icons
  always paired with a label; UI text at an integer 2x scale.

## 6. Layout: the five zones

The window is resizable (minimum 1024 x 640); every rectangle below is computed from the window size each frame.

```
+------------------------------------------------------------------------------------------+
| TOP BAR 40: New Open Save | Undo Redo | [tool strip toggle]   ▶ ❚❚ ⏭ ■  1.0x  EDITING   | Heat Pressure Electric Grid | - 1x + | Focus | Scenes | Search | ? |
+-----------+------------------------------------------------------------------------------+-------------------+
| TOOLS 96  | CONTEXT BAR 32: options of the active tool, snapping on the right              | DOCK 320          |
|  Select   +------------------------------------------------------------------------------+ Inspector |       |
|  Grab     |                                                                              | Materials |       |
|  Measure  |                                                                              | Scene     |       |
| CELLS     |                               CANVAS                                         | History   |       |
|  Paint    |                                                                              +-----------+       |
|  Erase    |                                                                              | collapsible       |
| PARTS     |                                                                              | sections, fields  |
|  Box ...  |                                                                              | that scrub        |
| JOINTS    |                                                                              |                   |
|  Pin ...  |                        [scroll strip / minimap]                              |                   |
| MODIFY    |                                                                              |                   |
|  Cut ...  |                                                                              |                   |
+-----------+------------------------------------------------------------------------------+-------------------+
| STATUS 28: LMB paint · RMB erase · [ ] size  |  X 412 Y 180  WATER 20C  |  BOX 30x8 10deg STEEL  |  2x  60 FPS |
+------------------------------------------------------------------------------------------+
```

**Top bar.** Files and undo on the left; the transport (Play, Pause, Step, Stop, a speed field 0.1x to 2x) in the
exact centre with a mode badge whose colour tints the canvas frame (green running, amber paused, none editing); view
toggles, zoom, focus, scenes, search and help on the right. Destructive file buttons dim while running.

**Tool strip.** One docked palette, two columns of 44 x 40 buttons (16 px icon over a one-line label), grouped under
small headers: TOOLS (Select, Grab, Measure), CELLS (Paint, Erase), PARTS (Box, Circle, Wheel, Rocket, Pipe, Hose, Fan,
Emitter), JOINTS (Pin, Motor, Spinner, Rod, Spring, Slider, Bond), MODIFY (Cut, Scale, Group, Ungroup, Flip H, Flip V,
Duplicate, Delete). The active tool is filled with the accent colour. Each button's tooltip is its name, its shortcut
and one line of what it does. The strip can be collapsed to icons only.

**Context bar.** Fixed height, changes with the tool, never moves the canvas: Paint shows the material chip and name,
brush size, and a replace-mode toggle; Erase the brush size; shape tools the material chip, FIXED toggle and the
typed-dimension hint; Fan strength, blow/vacuum, flip; Emitter material, rate, outlet side; Spring stiffness and
damping for new springs; Bond preset, melt temperature, rating; Cut box/circle and keep-cutter; Select the align
buttons (left, centre, right, top, middle, bottom) and the selection filter (bodies, joints, cells); Measure nothing
but the last reading. The right end always holds the snap controls: grid snap on/off, the step (1/2/5/10), and the
Ctrl-inverts hint.

**Dock.** Four tabs. *Inspector*: collapsible sections that remember whether they are open. For a body: Transform
(X, Y, width, height or radius, angle), Body (material chip with a search field over the body materials, FIXED,
density, melting point), Fan or Emitter when present, Info (mass, temperature, group). For a joint: its type, then
Spring (stiffness, damping, rest length, set rest to now, spring/rod switch), Motor (speed, power, keyed/always),
Bond (melts at, holds), Slider and Pin notes; Delete. For several bodies: the count and groups, material and FIXED
applied to all, align and distribute. With nothing selected: the tool's defaults (new springs, new bonds, pipe
diameter and wall) and the world settings (gravity, spark period, battery volts and amps, source density). Every
numeric field scrubs, types, takes +/- and expressions, clamps on commit and pushes one undo entry per edit.
*Materials*: the whole palette in categories with a search field and favourites, one grid for painting and one for
body materials, each chip with its properties on hover; clicking assigns to the current tool or to the selection.
*Scene*: a list of the world's bodies, groups and joints with kind, size and material; click selects, double-click
zooms to it; a filter field. *History*: the undo stack with one line per step ("Paint 312 cells", "Add motor",
"Resize box"); click to jump.

**Status bar.** Left: the active tool's bindings in the form `LMB draw · Shift square · Ctrl snap · Esc cancel`,
or the hovered widget's tip. Centre: cursor position in cells and what is under it (cell material and temperature, or
body material, kind, size, FIXED, GROUP OF n). Right: the selection summary, zoom and frame rate. Notifications
appear as toasts above the status bar and fade.

**Canvas.** Everything else. Rulers and grid are a toggle. Middle-drag or Space+drag pans; the wheel zooms about the
pointer (Ctrl+wheel keeps the old meaning as well); the scroll strip stays along the bottom edge. A dimension field
appears beside the pointer while drawing or dragging a handle: digits typed go into it, Tab moves to the next value
(width, height, angle), Enter commits at those values, Backspace returns to the mouse.

## 7. Interaction model

- **Tool contract.** Every tool runs begin, preview, commit or cancel. Left release or Enter commits; right click or
  Esc cancels and leaves the world and the undo stack untouched; Esc with nothing in progress clears the selection,
  then closes panels. Right click never erases any more (the Erase tool and Shift+paint do), so it is free for cancel
  and the context menu.
- **Selection.** Click selects the topmost body or joint; click again at the same spot cycles downwards; Shift adds
  or removes; Ctrl picks one part of a group; drag on empty space box-selects (left to right: enclosed, right to
  left: crossing); click and hold for 400 ms, or press the backtick key, opens a "select other" list of everything
  under the pointer, front to back, with hover pre-highlighting. The selection filter in the context bar limits what
  box-select and click pick.
- **Direct manipulation.** The handles stay as they are (edges, corners, rotation stalk; Ctrl about the centre, Shift
  proportions or 15 degree steps), with the dimension field for typed values and the snapping rules below.
- **Snapping.** Grid snap is a toggle with a step; holding Ctrl inverts it for the duration of a drag. Smart snap, on
  by default, snaps a dragged body's edges and centre to other bodies' edges and centres within 6 px and draws the
  guide line it used; the status bar names the snap ("CENTRE OF BOX 12"). Rotation snaps to 15 degrees with Shift.
- **Context menu.** Right click on a body or joint: Properties (focuses the inspector), Duplicate, Flip H, Flip V,
  Group / Ungroup, Fixed, Material (submenu of recent materials), Delete. On empty canvas: Paste here, Select all,
  Add box / circle here (opens with the shape placed at the pointer), Zoom to fit. The first item is always the one
  most likely wanted, and the slots never move between invocations.
- **Command palette.** Ctrl+K or Ctrl+Shift+P opens a search over every command (tools, actions, view toggles,
  scenes, materials); results show the category and the shortcut, recently used first; Enter runs. The same command
  table feeds the menus, the tooltips and the F1 cheat sheet, so a shortcut is never documented in two places.
- **Simulation.** Space plays and pauses, N steps, Shift+Space stops and restores the snapshot. While running the
  editing tools stay usable on the paused world and dim when they cannot apply; the arrow keys drive motors and
  Up/W fires rockets; grab works live. A speed field slows the run for inspection.
- **Precision.** All numbers are typed in cells and degrees. Fields accept `+ - * /` expressions. The dimension
  field during a drag and the inspector afterwards edit the same values, so there is nothing that can only be done
  one way.

## 8. Visual language

Palette (contrast ratios against the surface it sits on, all AA or better for text):

| Role | Colour | Use |
|---|---|---|
| Window background | `#121417` | behind everything |
| Surface | `#1B1E23` | panels, bars |
| Elevated | `#252930` | fields, menus, popups, buttons at rest |
| Hover | `#2E333B` | hovered buttons and rows |
| Border | `#3A4049` | dividers, field outlines |
| Selected row | `#2A3B52` | list rows, active tabs |
| Text | `#E8EAED` / `#A6ADB7` / `#6B737E` | primary / secondary / disabled |
| Accent | `#5AA9FF` | the active tool, links, focused field outline `#8CC4FF` |
| Running / paused / error | `#4CC38A` / `#E6B450` / `#FF6B6B` | the mode badge and canvas frame tint, warnings, destructive actions |

Type: the new mixed-case bitmap font at 2x (12 x 16 px cell) for all interface text, 1x only for rulers and dense
read-outs; never a non-integer scale; text always on a flat fill. Icons: 16 px, one colour, filled strokes snapped to
the pixel grid, recoloured for state, always next to a label in the strip, menus and context bar. Spacing on a 4 px
unit: rows 28, bars 40 / 32 / 28, section headers 24, panel padding 8, gaps 4, hit targets never under 24 px.
States: rest, hover, pressed, active (accent fill), focused (2 px ring), disabled (38 % text, never hidden).

## 9. Keyboard map

| Keys | Action |
|---|---|
| `Q` `G` `M` | Select, Grab, Measure |
| `P` `X` | Paint, Erase |
| `B` `C` `W` `R` | Box, Circle, Wheel, Rocket |
| `J` `O` `L` `S` | Pin, Motor, Rod, Spring |
| `K` | Cut |
| `1`..`9` `0` | Select, Grab, Box, Circle, Wheel, Pin, Motor, Rod, Spring, Paint (the digits shown on the strip) |
| `Space` / `N` / `Shift+Space` | Play or pause / step / stop and restore |
| `Esc` / `Enter` / right click | Cancel the tool in progress, clear the selection, close panels / commit / cancel |
| `Tab` | Next value in the dimension field or the inspector |
| `Shift` `Ctrl` while dragging | Keep proportions or 15 degree steps / resize about the centre, invert snapping |
| `Ctrl+Z` `Ctrl+Y` | Undo, redo |
| `Ctrl+C` `Ctrl+V` `Ctrl+D` | Copy, paste at the pointer, duplicate |
| `Ctrl+G` `Ctrl+U` | Group, ungroup |
| `Ctrl+H` `Ctrl+Shift+H` | Flip left-right, top-bottom |
| `Ctrl+A` `Del` | Select all, delete |
| `Ctrl+N` `Ctrl+O` `Ctrl+S` `Ctrl+Shift+S` | New, open, save, save as |
| `Ctrl+K` / `Ctrl+Shift+P` | Command palette |
| `F1` | Cheat sheet of every command and shortcut |
| `F` / `Shift+F` | Follow the selected body / zoom to the selection |
| `H` `V` `E` `I` | Heat view, pressure view, electric view, grid and rulers |
| `-` `=` `Ctrl+0` wheel | Zoom out, in, reset, zoom about the pointer |
| Middle drag, `Space`+drag, `Home` `End` `PgUp` `PgDn` | Pan |
| `T` | Toggle FIXED on the selection |
| Backtick | Select other (everything under the pointer) |
| Arrows / `A` `D` / `Up` `W` while running | Drive keyed motors / fire rockets |
| Arrows in edit mode | Nudge the selection 1 cell (Shift 10, Ctrl 0.25) |
| `[` `]` | Brush size |

Letters that drove the old single-key toggles (fan mode `M`, gravity `G`, fixed `T`, heat `H`, pressure `P`, grid
`K`) move to the context bar, the inspector and the view toggles, except `T` and `H`, which keep their meaning.

## 10. Robustness rules

- Layout is a function of the window size and the dock widths; nothing is positioned from a constant. Panels scroll
  rather than overflow; every text truncates with an ellipsis; the dock collapses to its tab row below 1200 px.
- The command table is the single source of truth for names, shortcuts, enabled state and help text.
- Every state the user can reach is rendered headless by the gallery flag and inspected; the event-driven editor
  test drives the new chrome through real SDL events.
