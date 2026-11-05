# Scene3D demos

Open the collection from the repository or release root:

```sh
./lambda.exe demo scene3d
# Equivalent direct entry:
./lambda.exe view test/demo/scene3d/index.html
```

The index has one tile for each demo. Every page shares breadcrumb navigation
back to **Scene3D**. The four physics experiments are direct tiles on the same
index, with no intermediate gallery.
The [outdoor rendering study](outdoors.ls) compares native height fog, thin-leaf lighting, instanced grass wind, cascaded sun shadows and refractive pond water. See the [outdoor API guide](../../../doc/Lambda_Scene3d_Outdoor.md).

Individual demos retain their own controls and color themes. The collection
uses one CLI catalog entry; individual pages can still be opened with `view`.

Native scenes can also use the ordered bloom/FXAA/custom color-pass API in
[`lambda.scene3d.postprocessing`](../../../doc/Lambda_Packages.md#native-postprocessing).
The focused [postprocessing fixture](../../scene3d/postprocessing.ls) demonstrates
uniform playback and template edits; it is separate from the Make waves image
material presets.

| Tile | Entry point | What to explore |
| --- | --- | --- |
| [Butterflies](butterflies/README.md) | `butterflies/butterflies.ls` | A GPU-simulated swarm of 4,096 and three flexible wings retuned by sliders |
| [Make waves](effects/README.md) | `effects/effects.ls` | Five animated GLSL effects applied to text, SVG artwork or raster images |
| [CellScope](cellscope/README.md) | `cellscope/cellscope.ls` | Cell models, organelle information, and synchronized comparison |
| [Domino circuit](physics/README.md) | `physics/domino.ls` | A 32-tile cascade and a counter-nudge |
| [Salvage bay](physics/README.md) | `physics/salvage.ls` | Compound assemblies, impulses, and weightlessness |
| [Marble sorter](physics/README.md) | `physics/sorter.ls` | A kinematic paddle and three sensor gates |
| [Bounce lab](physics/README.md) | `physics/bounce.ls` | Restitution and measured trajectory charts |
| Ringworld | `ringworld.ls` | A procedural planet, rings, moons, and an automatic camera tour |
| Crystal garden | `three-gallery.ls` | Instanced crystals, checkerboard, lighting, and transparency |
| Kinetic observatory | `observatory.ls` | Materials, shadows, satellites, and camera interaction |
| Shared animation | `shared-animation.html` | Native skinning/morphing and SVG timelines |
| Cube pets | `motion/pets.ls` | Kenney's CC0 Cube Pets switching clips together through motion controllers |
| Animated robot | `motion/robot.ls` | Quaternius' CC0 robot walking, running, dancing and gesturing through a motion controller |

The tile previews use local scene captures and the existing Chromium references.
CellScope bundles its Plant model; its other specimens need a network connection
on first selection. The other demos use local resources.

`motion/mixamo.ls` has no tile: it dances Mixamo's Beta character, whose files
are never committed. Fetch them with
`sh test/demo/scene3d/assets/motion/_fetch_mixamo.sh`, then open it with
`./lambda.exe view test/demo/scene3d/motion/mixamo.ls`; without the files it
shows how to fetch them.

`three-gallery.ls` constructs 49 native instanced crystals, a vertex-colored
checkerboard, a torus and a transparent halo. Drag to orbit, use Pan or
Shift-drag to move, and scroll to zoom. The toolbar and keyboard also offer
orbit, pan, tilt, zoom and Reset view. `lambda.scene3d.viewer` keeps the live
camera and pointer capture in the Lambda view (**D9.5.6**).

The original `three-gallery.html` and `observatory.html` remain independent
Three.js/WebGL renderer fixtures. Their pinned library, addons, license and
source hashes are under `vendor/three/`.

`ringworld.ls` uses the native `lambda.scene3d` package from Phase I.
It starts with a spinning storm-banded planet and three orbiting moons, sampled
by the shared SVG/native animation engine (S12.1.1v2, D9.5.4). Pause / Play and
Restart control that timeline independently of the camera. Drag to orbit,
Shift-drag or select Pan to move, and scroll to zoom. The toolbar also offers
orbit, tilt, zoom, Top / Side presets and Reset view. Focus the scene for arrow
key panning, Shift-arrow orbiting, `+` / `-` zoom, `R` reset and Space playback.
The shared Lambda viewer handles camera input. Camera distance and tilt have bounded limits.
Auto Play runs a repeating 24-second camera tour with orbit, pan, tilt and zoom,
starting from the current view. Stop Auto Play holds the view; dragging, scrolling,
camera keys or a camera toolbar button also stop the tour for manual control.
Pause / Play and Restart continue to control the planet/moon timeline separately.
All demo resources are local to this directory; viewing needs no network.

For PNG output with the full native host:

```sh
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe render test/demo/scene3d/three-gallery.ls \
  -o temp/three-gallery.png -vw 848 -vh 650
```

The explicit GLFW option enables graphics for automated full-host captures;
the separate headless-C executable excludes graphics (D9.2.1). The independent
WebGL fixtures also require a graphics host: ordinary headless mode returns
`null` from `getContext('webgl2')`.

`reference/three-gallery-chromium.png` is the original independent
Chromium/SwiftShader reference, also used for its tile preview. Its manifest
records the original viewport/canvas bounds, fixture results, calls/queries,
and shader hashes. Capture a fresh browser reference before comparing the
current page, whose breadcrumbs change the canvas position.
See [the WebGL test guide](../../webgl/README.md) for reproduction.

`observatory.ls` uses native Scene3D for metallic rings, satellites, a PNG
normal map, JPEG stone, room-environment illumination, moving shadows and
FXAA. Drag to orbit, scroll to zoom, click a satellite or use Play / Pause.
Lambda projects satellite centers through the active camera for selection;
the native scene clock controls playback (**S12.1.1v3 / D9.5.6**).

`shared-animation.html` combines native GPU skinning and relative morph targets
with SVG/SMIL. Separate buttons pause/play/seek each timeline. Native deformation
currently supports 16 bones, four normalized influences per vertex and two
relative morph targets, including optional relative normals. Bone order is DFS
within `<skeleton>`; mesh `skeleton` references its ID. `skin-indices`,
`skin-weights`, `morph-positions` and `morph-normals` are geometry attributes;
`morph-weights` belongs to the mesh. Skinned instancing is rejected.

The generated original assets and provenance are in `assets/`; the generator
needs Python Pillow. Phase III fixed-time references and API/shader audit live
under `reference/phase3/`. See the [implementation record](../../../vibe/impl/Radiant_WebGL_Phase3.md)
for the declared playback/binding profile, validation and measurements.

Imported-asset coverage remains in the integration fixture
[`test/scene3d/imported-assets.ls`](../../scene3d/imported-assets.ls). Its generated
CC0 resources and dependencies remain in `assets/loading/`; this fixture is
not part of the demo index. See [the asset-loading design](../../../vibe/radiant/Radiant_Design_WebGL.md#15-asset-loading-into-native-scenes)
for supported profiles, API options, and limitations.

The [model-fidelity fixture](../../scene3d/model-fidelity.ls) exercises imported
cameras, independent UV0/UV1 map transforms and RGBA geometry. The package
[coordinate profile](../../../doc/Lambda_Packages.md#texture-coordinates-and-vertex-alpha)
describes native authoring and the static export limits.
