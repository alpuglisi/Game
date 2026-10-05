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

(Sections 5 onward, the research-driven layout, interaction model, visual language and keyboard map, follow.)
