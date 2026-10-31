# Procart — procedural art workshops

```sh
./lambda.exe demo procart
```

The header links Form studies, Tree studio, Terrain, Meadow, Plants & foliage, Rocks & crystals,
Textures & materials, Decorative patterns, and Painterly. Every destination has its own page:

| Workshop | Direct entry | Presets |
| --- | --- | --- |
| Form studies | `test/demo/procart/grammar.ls` | Nine primitives, turning ribbons, recursive branchwork, a mixed garden and patterned stone/bark/leaves |
| Tree studio | `test/demo/procart/trees.ls` | Broadleaf, conifer, wind-shaped tree and parametric apex development |
| Terrain | `test/demo/procart/world.ls` | Islands, rivers, districts, towns and ruins |
| Meadow | `test/demo/procart/meadow.ls` | A 28 × 20 living meadow with a pond, six flower varieties, reeds, weather and solar controls |
| Plants & foliage | `test/demo/procart/plants.ls` | Eleven garden forms and seven foliage studies, in illustration and 3D |
| Rocks & crystals | `test/demo/procart/rocks.ls` | Granite boulders, basalt columns, quartz clusters and amethyst geodes |
| Textures & materials | `test/demo/procart/textures.ls` | Wood, marble, granite, brick, linen and ceramic |
| Decorative patterns | `test/demo/procart/patterns.ls` | Mandalas, stars, Truchet paths, ribbon weave, Voronoi mosaics and golden spirals |
| Painterly | `test/demo/procart/painterly.ls` | Three sample photos repainted in four brush-stroke modes with three procedural brushes |

## Form studies — a drawing grammar

```sh
./lambda.exe view test/demo/procart/grammar.ls
./lambda.exe validate test/demo/procart/samples/primitives.mark -s lmd/package/procart/schema.ls -f mark
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe view test/demo/procart/grammar.ls --headless --event-file test/demo/procart/tests/grammar_events.json --event-result temp/procart-p7/grammar-events.json
```

The page loads five literal Mark files in [samples/](samples/). Shapes receive
inherited `scale`, translation, rotation, hue and opacity. Rules and variants
declare `name`; a direct tag such as `<ribbon id:"first">` calls that rule.
`id` remains the instance identity and the reference identity for recipes and
geometry prototypes. Named weighted rules recurse; `repeat` accumulates a frame
and color step. Global `settings` govern
recursion, minimum/maximum frame size, work budgets and primitive defaults.
The page exposes sample selection, seed replay, maximum depth and retained
orbit/zoom/reset, with desktop and mobile layouts.

`lambda.procart.scene` validates and expands the data, then emits existing native
Scene3D markup. The app supplies a trusted registry reusing the existing plants,
trees, rocks and grass. No generator code is loaded from a saved artwork
(**S12.1.1v3 / PG12**). Paired model goldens and the native replay cover the
pipeline; [the P7 record](../../../vibe/impl/Lambda_Impl_Procart_Grammar.md)
tracks checks and limits. The structural schema requires semantic admission by
`scene.normalize`/`scene.expand` for full validation.

## Tree studio — plant architecture and development

```sh
./lambda.exe view test/demo/procart/trees.ls
./lambda.exe validate test/demo/procart/samples/tree-broadleaf.mark -s lmd/package/procart/plant_schema.ls
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe view test/demo/procart/trees.ls --headless --event-file test/demo/procart/tests/trees_events.json --event-result temp/procart-p8/trees-events.json
```

Four separate `samples/tree-*.mark` files store a semantic `tree`, root seed,
generator/version and parameters. Per-level controls shape branch length,
thickness, taper, curvature, attachment, spiral angle and terminal foliage.
Directional growth bends branches toward a force vector. The apex sample uses
parametric L-system productions and a true 3D turtle rather than rotated planar
branches. Both paths use the same tapered bark and instanced leaf mesher.

The page exposes seed replay, branch levels/generations, directional force,
leaf visibility, full/distant detail and retained orbit. Detail changes reuse
the skeleton; hiding leaves preserves the camera. A strip below the main view
compares branch levels 1, 2 and 3 using the same seed, force and viewing scale.
Select a tile to inspect that stage above; detail, foliage and seed controls also
update the previews. The apex sample compares generations 1, 3 and 5. The package exports
`plant.architecture`, `skeleton`, `mesh`, `lods`, `from_document` and `registry`.
`plant.tree@1` and `plant.apex@1` also work as semantic recipes in P7 scenes.
Legacy Meadow recipes keep their existing generator contract.

`plant_*.ls` package goldens, `tests/procart_trees.ls` and `trees_events.json`
cover structure, geometry, admission and the native UI. Design: [PG13](../../../vibe/Lambda_Design_Procart.md#10-p8--plant-architecture-and-developmental-models-pg13);
delivery: [P8 record](../../../vibe/impl/Lambda_Impl_Procart_Plants.md).

## Meadow

The meadow loads [meadow.mark](meadow.mark): a literal data document containing
versioned recipes and nested `group`, `flower`, `tree`, `stone` and `grass`
elements. Groups compose transforms and can scatter repeated objects. Recipe
settings override defaults; named random streams keep siblings independent.
The model reuses the existing plants, stone facets and terrain, with new curved
grass blades instanced across seeded clumps. Generation remains pure
(**S12.1.1v3**, **PG11 / PG15**).

```sh
./lambda.exe view test/demo/procart/meadow.ls
./lambda.exe validate test/demo/procart/meadow.mark -s test/demo/procart/mod_composition_schema.ls -f mark
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe view test/demo/procart/meadow.ls --headless --event-file test/demo/procart/tests/meadow_events.json --event-result temp/procart-meadow/meadow-events.json
```

**New meadow / Previous** replay seeds; the five landscape layers toggle without
regenerating model data. Drag to orbit, zoom or reset the camera. The native UI
replay checks real pixels, retained camera interaction, layers, seed replay and
mobile layout. `tests/procart_meadow.ls` and `tests/procart_grass.ls` check saved
data/model replay, transforms, settings, named keys, bounds and scene validation.
The standalone schema checks structural shape; the resolver also checks recipe
references, supported versions, settings, transforms and expansion limits.
The [semantic scene design](../../../vibe/Lambda_Design_Procart.md) describes the
format and currently supported generator profiles. Qualification, including the
existing foliage replay issue exposed by generator extraction, is recorded in
the [implementation record](../../../vibe/impl/Lambda_Impl_Procart_Meadow.md).

The three mineral/texture/pattern workshops share seed, detail, colorway and inspection controls through
`mod_workshop.ls`. Generation is pure (**S12.1.1v3**). Rocks use the native 3D
viewer; textures encode generated pixels as PNG data URIs; patterns build SVG
graphics. **More / Less** changes the form complexity or texture scale; **Change
colorway** cycles palettes. **Inspect** shows mineral facet edges, a 2 × 2 texture
repeat or the pattern's construction guides. Rocks support the same drag, zoom
and reset controls as the plants.

The navigation replay opens all three from the header, exercises all sixteen
presets, checks seed replay and controls, and captures the rendered pages:

```sh
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe view test/demo/procart/world.ls --headless --event-file test/demo/procart/tests/workshop_navigation_events.json --event-result temp/procart-navigation/navigation-result.json
```

Open the botanical page directly with:

```sh
./lambda.exe view test/demo/procart/plants.ls
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
use bracketed L-systems (`lambda.procart.lsystem`); the other samples use seeded
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
depth and scene validation for every sample. `tests/procart_flower_form.ls` checks
raised and cupped petals, size variation, finite unit normals and separated,
slender lavender spikes. `tests/procart_flower_samples.ls` checks the four new
architectures, growth, replay, stamens, petal tiers, leaf bounds and normals.
`tests/procart_foliage.ls` checks the foliage models, collection switching and
curved-surface normals. `tests/procart_workshops.ls` checks mineral geometry,
all six pattern families, encoded texture pixels, periodic seams and controls.
The illustration replay checks
organs, growth, angle, resize and terrain navigation:

```sh
./lambda.exe view test/demo/procart/plants.ls --headless --event-file test/demo/procart/tests/plants_events.json --event-result temp/procart/plants-events.json
```

The native replay requires macOS graphics access and a hidden GLFW window. It
checks pixels for the original seven specimens, physical orbit with retained meshes,
camera reset/zoom, growth, seed replay, spines, spots, flowers and mobile layout.
Both replays save PNGs under `temp/procart/`:

```sh
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe view test/demo/procart/plants.ls --headless --event-file test/demo/procart/tests/plants_3d_events.json --event-result temp/procart/plants-3d-events.json
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe view test/demo/procart/plants.ls --headless --event-file test/demo/procart/tests/flower_samples_events.json --event-result temp/procart/flower-samples-events.json
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe view test/demo/procart/plants.ls --headless --event-file test/demo/procart/tests/foliage_events.json --event-result temp/procart/foliage-events.json
./test/test_lambda_gtest.exe --gtest_filter='*tests_procart*'
```

The reference-family replay checks both views for all four flowers, real pixels,
custom camera reset, growth, petal opening, seed changes, leaf/flower toggles and
the 390px layout. It saves the `ref-*-2d.png` and `ref-*-3d.png` previews under
`temp/procart/`.

The foliage replay checks all seven studies in both views, actual 3D pixels,
growth, angle, fruit/leaf toggles, seed replay and the 390px layout. Workshop
navigation also checks mineral camera retention and all three mobile layouts.

Pre-Meadow qualification: all 353 UI assertions and all eight focused model/noise
goldens pass on the default backend; the eight goldens also pass with
`LAMBDA_EXEC_BACKEND=interp`. Forced JIT passes seven of eight goldens: exact seed
replay in `procart_foliage.ls` still fails for palm fronds and berry sprays. The
regression remains enabled; its root cause is unresolved. The broader Lambda
baseline reports a separate greetings-model failure and four sandbox-blocked
HTTP cases; those four HTTP cases pass with local networking access.

One seed builds a whole world with [lambda.procart](../../../doc/Lambda_Packages.md#26-procart--procedural-art):

- **Land.** An island heightmap (fBm simplex noise with domain warp, blended toward a dome), worn by hydraulic and thermal erosion. Pits are filled so every river reaches the sea. Joined river paths are drawn where enough cells drain. Biomes use moisture against elevation, or latitude/altitude temperature when climate is enabled (`terrain`).
- **Towns.** Placed by Poisson-disk sampling on habitable land (`sample`), then named by a Markov model trained on invented place names (`text`). Voronoi cells around them divide the map into districts (`geom`).
- **Lore.** One line from a Tracery-style grammar (`text`).
- **Emblem.** A tree grown by a stochastic L-system (`lsystem`).
- **Ruin.** A rooms-and-corridors dungeon on a BSP partition (`grid`).

**New world** and **Previous** step the seed. **Rivers** hides or shows the rivers, **Erosion** rebuilds the land without erosion, and **Climate** switches the biomes to latitude and altitude temperature. Every part draws from its own key derived from the seed, so the names, lore, emblem and ruin stay the same when erosion is switched off.

The world model is in `mod_world.ls` and the SVG drawings are in `mod_map.ls`. `tests/procart_world.ls` pins the model for seed 2026, district coverage and climate behavior. `tests/world_events.json` replays the buttons headlessly:

```sh
./lambda.exe view test/demo/procart/world.ls --headless --event-file test/demo/procart/tests/world_events.json --event-result temp/procart-world.json
```

The demo also appears in the `lambda demo` gallery. The event replay checks eight assertions, including district drawing and climate toggles.

`tests/resize_events.json` checks twenty-one assertions across six window sizes, including stable map and sidebar dimensions, seed and working controls after resizing:

```sh
./lambda.exe view test/demo/procart/world.ls --headless --event-file test/demo/procart/tests/resize_events.json --event-result temp/procart-resize.json
```

The fifth **Form studies** tab, **Grain, bark & veins**, loads
[`samples/surfaces.mark`](samples/surfaces.mark). Its three reusable pattern
resources bind to stone object coordinates, branch-length UVs and normalized
leaf UVs. The tree, two stones and enlarged leaf use ordinary native textures
and standard materials. Generation and bindings live in `lambda.procart.surface`
and `lambda.procart.scene`; the saved composition contains no functions.

```sh
./lambda.exe view test/demo/procart/grammar.ls
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe view test/demo/procart/grammar.ls --headless --event-file test/demo/procart/tests/surfaces_events.json --event-result temp/procart-p9/surfaces-events.json
./test/test_lambda_gtest.exe --gtest_filter='ProcartSurfaceTests.*'
```

The model gate checks the surface fixtures and saved study on interpreter,
AUTO and JIT. Native replay checks material bindings, actual pixels, exact seed
replay, retained orbit and the 390px layout. Schema and coordinate contracts:
[PG14](../../../vibe/Lambda_Design_Procart.md#11-p9--reusable-surface-patterns-pg14).

The plot is now 28 × 20, with 104 recipe instances: cosmos, sunflower, lavender,
daisy, poppy and day-flowering water lily; a four-tree grove; granite;
fine/tall grass and pond reeds. A saved `<pond>` carves the terrain and anchors
lily pads to its water level. Terrestrial plants stay outside its interior.

A global `<environment>` stores latitude, altitude, day of year, solar time,
humidity, weather, wind and lighting. The sidebar edits these independently of
the seed. Sunflowers artistically track the sampled sun, lilies close at night,
and grasses/trees/flowers sway in wind. Rain drops and mist use existing native
meshes and animation. Pause motion or reset the day; changing conditions keeps
the orbit camera. This is an illustrative scene, not an ecological simulator.

```sh
./test/test_lambda_gtest.exe --gtest_filter='ProcartEnvironmentTests.*'
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe view test/demo/procart/meadow.ls --headless --event-file test/demo/procart/tests/environment_events.json --event-result temp/procart-p10/environment-events.json
```

The package exports `lambda.procart.environment` and `lambda.procart.herb`;
`plant.herb@1` also works in generic `procart` scenes. [PG15](../../../vibe/Lambda_Design_Procart.md#12-p10--environment-and-a-living-meadow-pg15)
records units/defaults and the plant response contract. [P10 qualification](../../../vibe/impl/Lambda_Impl_Procart_Environment.md)
records the cross-tier and native interaction checks.

## Painterly — photographs repainted

```sh
./lambda.exe view test/demo/procart/painterly.ls
```

A port of [weimegan/painterly](https://github.com/weimegan/painterly), an MIT 6.815 non-photorealistic rendering assignment. Thousands of brush stamps rebuild a photo; each stamp takes the color under its center with ±15% per-channel jitter and blends in linear light (gamma 2.2), as the original's `brush` does. The four tabs are the original's four modes:

| Tab | Original | Behavior |
| --- | --- | --- |
| Single scale | `singleScalePaint` | One layer of equal strokes at uniform random positions |
| Two-scale painterly | `painterly` | A coarse layer, then strokes a quarter the size, rejection-sampled against a sharpness map so they gather on fine detail |
| Oriented strokes | `singleScaleOrientedPaint` | One layer; each stroke is turned along the local edge direction from the smoothed structure tensor |
| Oriented painterly | `orientedPaint` | Both: coarse and detail layers, all oriented |

**Less / More** sets the stroke count (1,200–13,000 per layer). **Change photo** cycles three photos: the two Unsplash photos bundled with the photo demo (`../photo/assets/`) and `assets/chihuly.png`, the photo from the original repository's README (provenance in `assets/ATTRIBUTION.md`). The default Oriented painterly mode and Long bristle brush on Chihuly glass reproduce that README's showcase painting; **More** once matches its 10,000-stroke density most closely. **Change brush** cycles three procedural brushes: long bristle (the counterpart of the original's `longBrush.png`), round dab and dry flat. **Show photo** swaps the painting for its source. **New seed / Previous** re-sample the strokes; the same seed and settings recreate the same PNG.

`mod_painterly.ls` holds the algorithm. Brush tiles, image analysis (luminance, Gaussian sharpness map, Sobel structure tensor) and stroke plans are pure functions; only `paint`, which stamps the planned strokes into one float buffer in order, is a procedure. The page therefore paints from its `load` and `click` handlers rather than the `fn` state initializer (**S12.1.1v3 / S12.1.3**), so it has its own view instead of using `mod_workshop.ls`; it reuses the workshop styling and button helper.

Deliberate differences from the original:

- Stroke direction uses the closed form ½·atan2(2·Ixy, Ixx−Iyy) + π/2 instead of a per-pixel `eigh`, with 18 bins over 180° (the original used 36 over 360°, and eigenvector sign made half of them equivalent).
- Rotated brushes are built once per layer; the original rebuilt all 36 for every stroke.
- Strokes may land anywhere and clip at the canvas edge, so there is no unpainted margin; the canvas starts as a paper tone instead of black.
- Brush size follows the canvas short edge: 60 px at 640 px, and 47 px on the 500 × 667 Chihuly photo, which is never enlarged; the original used a fixed 50 px.
- Brush scaling and rotation are bilinear (`resize`, `rotate`) rather than cubic, and the RNG is `lambda.procart.rng`, so the output is visually equivalent, not pixel-identical.

`tests/procart_painterly.ls` checks brushes, brush-size scaling, edge-direction bins, analysis ranges, the portrait sample, plan replay and seed sensitivity, and that detail strokes favor sharp pixels; `tests/proc/procart_painterly_paint.ls` checks that painting replays exactly, depends on the brush, and encodes a PNG that decodes back. The replay paints on load, walks all four modes, seed replay, brush, all three photos (including the 500 × 667 portrait canvas), density, the source toggle and the 390px layout, saving PNGs under `temp/procart-painterly/`:

```sh
./lambda.exe view test/demo/procart/painterly.ls --headless --event-file test/demo/procart/tests/painterly_events.json --event-result temp/procart-painterly/result.json
```
