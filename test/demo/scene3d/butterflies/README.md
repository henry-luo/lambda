# Butterflies

```sh
./lambda.exe demo scene3d    # then choose the Butterflies tile
./lambda.exe view test/demo/scene3d/butterflies/butterflies.ls
```

A swarm of 4,096 butterflies in clusters and streams, beside three single butterflies whose wing motion you retune while they fly. It is the native port of the three.js sample `test/html/butterflies.html`.

The solo cards show, from top to bottom: wireframe; texture on the left wing and wireframe on the right; and full texture. The wireframe follows the flapping grid and uses the atlas silhouette to keep the butterfly's shape.

The page divides the work the way a three.js page does: Lambda describes the scenes, composes the GLSL and sets uniforms; the GPU does the per-frame work.

- **The atlas** (`mod_atlas.ls`): sixteen species — mourning cloak, peacock, red admiral, morpho, swallowtail and more — painted in Lambda as one SVG of clip paths, gradients, veins, spots and eyespots. Radiant rasterizes it once into a mipmapped texture that all four viewports sample.
- **The swarm** (`mod_glsl.ls`): a `<simulation>` of 64 × 64 records, position and velocity. Each step follows a curl-noise flow, steers toward one of five wandering attractors and keeps loners near the view; a butterfly's size, speed, species, cluster and personal offset are hashed from its record index, so Lambda sends no per-butterfly data. A three-second head start opens the page mid-flight.
- **The wings**: one shader material places every instance from the simulation records and bends each wing as it flaps: the span curls against the stroke, the hindwing lags the forewing, and larger butterflies beat slower and flex more.
- **The sliders** set the uniforms of each single butterfly: species, flex, curl, hindwing lag, flap speed and flap height. A slider re-renders only its own card; the swarm keeps flying, and no shader is recompiled.

Coverage: `Scene3dTest.ButterfliesSwarmFliesAndSlidersRetuneOneWing` checks the swarm's head start, a slider retuning one card without relinking, and the swarm staying untouched. The engine features it uses are specified in `vibe/impl/Radiant_Impl_Scene3d_Shaders.md`.
