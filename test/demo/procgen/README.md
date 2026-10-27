# World — a lambda.procgen showcase

```sh
./lambda.exe demo procgen
```

One seed builds a whole world with [lambda.procgen](../../../doc/Lambda_Packages.md#26-procgen--procedural-generation):

- **Land.** An island heightmap (fBm simplex noise with domain warp, blended toward a dome), worn by hydraulic and thermal erosion. Pits are filled so every river reaches the sea. Joined river paths are drawn where enough cells drain. Biomes use moisture against elevation, or latitude/altitude temperature when climate is enabled (`terrain`).
- **Towns.** Placed by Poisson-disk sampling on habitable land (`sample`), then named by a Markov model trained on invented place names (`text`). Voronoi cells around them divide the map into districts (`geom`).
- **Lore.** One line from a Tracery-style grammar (`text`).
- **Emblem.** A tree grown by a stochastic L-system (`lsystem`).
- **Ruin.** A rooms-and-corridors dungeon on a BSP partition (`grid`).

**New world** and **Previous** step the seed. **Rivers** hides or shows the rivers, **Erosion** rebuilds the land without erosion, and **Climate** switches the biomes to latitude and altitude temperature. Every part draws from its own key derived from the seed, so the names, lore, emblem and ruin stay the same when erosion is switched off.

The world model is in `mod_world.ls` and the SVG drawings are in `mod_map.ls`. `tests/procgen_world.ls` pins the model for seed 2026, district coverage and climate behavior. `tests/world_events.json` replays the buttons headlessly:

```sh
./lambda.exe view test/demo/procgen/world.ls --headless --event-file test/demo/procgen/tests/world_events.json --event-result temp/procgen-world.json
```

The demo also appears in the `lambda demo` gallery. The event replay checks eight assertions, including district drawing and climate toggles.

`tests/resize_events.json` checks fifteen assertions across six window sizes, including stable map dimensions, seed and working controls after resizing:

```sh
./lambda.exe view test/demo/procgen/world.ls --headless --event-file test/demo/procgen/tests/resize_events.json --event-result temp/procgen-resize.json
```
