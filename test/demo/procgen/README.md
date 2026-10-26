# World — a lambda.procgen showcase

```sh
./lambda.exe view test/demo/procgen/world.ls
```

One seed builds a whole world with [lambda.procgen](../../../doc/Lambda_Packages.md#26-procgen--procedural-generation):

- **Land.** An island heightmap (fBm simplex noise with domain warp, blended toward a dome), worn by hydraulic and thermal erosion. Pits are filled so every river reaches the sea. Rivers are drawn where enough cells drain, and biomes come from elevation against moisture (`terrain`).
- **Towns.** Placed by Poisson-disk sampling on habitable land (`sample`), then named by a Markov model trained on invented place names (`text`).
- **Lore.** One line from a Tracery-style grammar (`text`).
- **Emblem.** A tree grown by a stochastic L-system (`lsystem`).
- **Ruin.** A rooms-and-corridors dungeon on a BSP partition (`grid`).

**New world** and **Previous** step the seed. **Rivers** hides or shows the rivers, and **Erosion** rebuilds the land without erosion. Every part draws from its own key derived from the seed, so the names, lore, emblem and ruin stay the same when erosion is switched off. A world takes about 2.5 s on a debug build.

The world model is in `mod_world.ls` and the SVG drawings are in `mod_map.ls`. `tests/procgen_world.ls` pins the model for seed 2026. `tests/world_events.json` replays the buttons headlessly:

```sh
./lambda.exe view test/demo/procgen/world.ls --headless --event-file test/demo/procgen/tests/world_events.json --event-result temp/procgen-world.json
```

The demo is not yet in the `lambda demo` catalog. Registering it means adding entries to `test/demo/index.ls`, `test/demo/mod_demo_art.ls`, this folder's parent `README.md` and `LAMBDA_DEMOS` in `lambda/main.cpp`.
