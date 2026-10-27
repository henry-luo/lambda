# Procedural generation workshops

```sh
./lambda.exe demo procgen
```

The header links Terrain, Plants & foliage, Rocks & crystals, Textures & materials,
and Decorative patterns. Every destination has its own page:

| Workshop | Direct entry | Presets |
| --- | --- | --- |
| Terrain | `test/demo/procgen/world.ls` | Islands, rivers, districts, towns and ruins |
| Plants & foliage | `test/demo/procgen/plants.ls` | Eleven garden forms and seven foliage studies, in illustration and 3D |
| Rocks & crystals | `test/demo/procgen/rocks.ls` | Granite boulders, basalt columns, quartz clusters and amethyst geodes |
| Textures & materials | `test/demo/procgen/textures.ls` | Wood, marble, granite, brick, linen and ceramic |
| Decorative patterns | `test/demo/procgen/patterns.ls` | Mandalas, stars, Truchet paths, ribbon weave, Voronoi mosaics and golden spirals |

The three workshops share seed, detail, colorway and inspection controls through
`mod_workshop.ls`. Generation is pure (**S12.1.1v3**). Rocks use the native 3D
viewer; textures encode generated pixels as PNG data URIs; patterns build SVG
graphics. **More / Less** changes the form complexity or texture scale; **Change
colorway** cycles palettes. **Inspect** shows mineral facet edges, a 2 × 2 texture
repeat or the pattern's construction guides. Rocks support the same drag, zoom
and reset controls as the plants.

The navigation replay opens all three from the header, exercises all sixteen
presets, checks seed replay and controls, and captures the rendered pages:

```sh
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe view test/demo/procgen/world.ls --headless --event-file test/demo/procgen/tests/workshop_navigation_events.json --event-result temp/procgen-navigation/navigation-result.json
```

Open the botanical page directly with:

```sh
./lambda.exe view test/demo/procgen/plants.ls
```

The **glasshouse** has eighteen samples in two collections. **Garden plants &
flowers** contains woodland fern, meadow herb, wild cosmos,
golden sunflower, lavender, desert cactus, woodland mushrooms and four flowers
inspired by the supplied image references:

| Sample | Reference features |
| --- | --- |
| **Pink lupin** | A tall, tapering pink spike with folded pea-like flowers, pale wings and palmate foliage. |
| **Blue crown** | Overlapping rounded blue petals, pale accents and slender golden stamens. |
| **Pink rosette** | Staggered concentric rings of pointed pink petals, opening from a compact center. |
| **Pond lotus** | Broad rounded pink petals, central stamens and notched pond leaves with radial veins. |

These are stylized procedural interpretations. The first four catalog samples
use bracketed L-systems (`lambda.procgen.lsystem`); the other samples use seeded
solid-organ recipes. Cacti have branching ribbed columns, spines and pink flowers.
Mushroom colonies have domed, spotted caps, stems and radial gills; fungi are
identified as their own kingdom.

**Leaves & foliage** adds seven studies inspired by the botanical reference:
an olive sprig with dark fruit, round silver eucalyptus leaves, feathered fern
and palm fronds, a lobed oak leaf, a bilobed ginkgo fan and a branching berry spray.
Leaf veins follow the blade surfaces. Growth adds leaves or changes a single
leaf's size; branch angle changes their spread. **Olives / Berries** controls fruit
visibility independently of the leaves. Every study supports both illustration
and native 3D, using the same curved leaf geometry and seeded dimensions.

**Illustration / 3D** switches the selected specimen between SVG and native
`lambda.scene3d`. All eighteen have geometry, lighting and depth. L-system forms
grow three radial whorls from their 2D turtle recipe, with volumetric stems,
leaves, petals and flower centers. This is a demo adapter, not a new 3D L-system
turtle API. Solid-organ recipes share geometry between both views. Layered blooms
reuse one ring generator; their recipes set petal shape, colors, packing and opening.
Rounded petal surfaces and notched leaf surfaces have analytic normals. Thin leaf
bounds preserve the specimen's position above the ground. Mesh instances share
sphere, petal, leaf and cap buffers. Drag to orbit, scroll or use **Zoom + / −**,
use the arrow keys to rotate, and press **R** or **Reset view** to restore the camera.
Camera motion retains the meshes; a new specimen resets the camera. Blue crown,
rosette and lotus start at higher viewpoints so their layers are visible, and
**Reset view** restores the selected flower's viewpoint.

Flowers vary in overall size, and their individual petals vary in length, width
and upward tilt. In 3D, tapered petal surfaces curve into a cup and each head has
a slight lean. These choices come from independent seed keys, so they reproduce
without changing the branch skeleton. The illustration uses the same bloom sizes
and petal proportions. Lavender grows separate slender stalks above basal foliage;
each terminal spike has small, spaced violet florets that taper toward a pale bud.

**More growth / Less growth** select one of five growth stages, adding more petal
rings or more flowers along a spike in the new families. **Wider / Narrower**
change the branch angle from 15° to 40°, or the spread of lavender stalks, cactus
arms and mushroom caps; **Petal opening** controls the layered blooms. **New seed / Previous** explore reproducible specimens. **Leaves**, **Spines**
or **Cap spots** toggle the selected form's details; **Flowers** controls blossoms
on flowering forms. Visibility keeps the framing stable. Fern and herb remain
foliage plants. Generation is pure (**S12.1.1v3**); free-angle turtle coordinates
use the package's tolerance tier (**PG2**). Retained camera changes cross the DOM
boundary under **D7.5.3v2 / S12.1.3**.

`mod_plants.ls` owns the catalog, seeded model and controls; `mod_botanical.ls`
draws the illustrations; `mod_plant_solids.ls` builds shared solid organs;
`mod_flower_samples.ls` builds the reference-inspired families;
`mod_bloom.ls` shares petal/leaf surfaces, normals and bloom variation;
`mod_foliage.ls` builds the seven leaf families; `mod_botanical3d.ls` supplies the
botanical meshes. `mod_scene_view.ls` shares instancing, framing and retained
camera interaction between plants and minerals. `mod_minerals.ls`,
`mod_textures.ls` and `mod_patterns.ls` own the new workshop generators. The layout
stacks controls beneath the specimen on smaller windows. The model goldens check
seed replay, growth, terminal-tip placement, control bounds, finite 3D transforms,
depth and scene validation for every sample. `tests/procgen_flower_form.ls` checks
raised and cupped petals, size variation, finite unit normals and separated,
slender lavender spikes. `tests/procgen_flower_samples.ls` checks the four new
architectures, growth, replay, stamens, petal tiers, leaf bounds and normals.
`tests/procgen_foliage.ls` checks the foliage models, collection switching and
curved-surface normals. `tests/procgen_workshops.ls` checks mineral geometry,
all six pattern families, encoded texture pixels, periodic seams and controls.
The illustration replay checks
organs, growth, angle, resize and terrain navigation:

```sh
./lambda.exe view test/demo/procgen/plants.ls --headless --event-file test/demo/procgen/tests/plants_events.json --event-result temp/procgen/plants-events.json
```

The native replay requires macOS graphics access and a hidden GLFW window. It
checks pixels for the original seven specimens, physical orbit with retained meshes,
camera reset/zoom, growth, seed replay, spines, spots, flowers and mobile layout.
Both replays save PNGs under `temp/procgen/`:

```sh
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe view test/demo/procgen/plants.ls --headless --event-file test/demo/procgen/tests/plants_3d_events.json --event-result temp/procgen/plants-3d-events.json
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe view test/demo/procgen/plants.ls --headless --event-file test/demo/procgen/tests/flower_samples_events.json --event-result temp/procgen/flower-samples-events.json
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe view test/demo/procgen/plants.ls --headless --event-file test/demo/procgen/tests/foliage_events.json --event-result temp/procgen/foliage-events.json
./test/test_lambda_gtest.exe --gtest_filter='*tests_procgen*'
```

The reference-family replay checks both views for all four flowers, real pixels,
custom camera reset, growth, petal opening, seed changes, leaf/flower toggles and
the 390px layout. It saves the `ref-*-2d.png` and `ref-*-3d.png` previews under
`temp/procgen/`.

The foliage replay checks all seven studies in both views, actual 3D pixels,
growth, angle, fruit/leaf toggles, seed replay and the 390px layout. Workshop
navigation also checks mineral camera retention and all three mobile layouts.

Current qualification: all 353 UI assertions and all eight focused model/noise
goldens pass on the default backend; the eight goldens also pass with
`LAMBDA_EXEC_BACKEND=interp`. Forced JIT passes seven of eight goldens: exact seed
replay in `procgen_foliage.ls` still fails for palm fronds and berry sprays. The
regression remains enabled; its root cause is unresolved. The broader Lambda
baseline reports a separate greetings-model failure and four sandbox-blocked
HTTP cases; those four HTTP cases pass with local networking access.

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

`tests/resize_events.json` checks twenty-one assertions across six window sizes, including stable map and sidebar dimensions, seed and working controls after resizing:

```sh
./lambda.exe view test/demo/procgen/world.ls --headless --event-file test/demo/procgen/tests/resize_events.json --event-result temp/procgen-resize.json
```
