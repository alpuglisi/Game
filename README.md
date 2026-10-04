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

### Materials

Wall, Sand, Water, Oil, Lava, Acid, Fire, Gunpowder, TNT, Stone, Wood, Plant, Ice, Ash, Steam, Smoke, Void, Eraser.

Some things to try: lava on water makes stone and steam; fire spreads through wood, plants and oil; plants grow by drinking water; acid eats most solids; gunpowder and TNT chain-explode and throw bodies and debris; ice melts near heat and freezes water; steam condenses back into water; a light ball floats on oil on water.

## Developer flags

`sandbots --shot out.bmp [frames] [--scene N]` runs headless (SDL dummy video driver), simulates, and saves a screenshot. Scenes: `1` material reactions, `2` joints/springs/rockets, `3` scripted tool use.
