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
Individual demos retain their own controls and color themes. The collection
uses one CLI catalog entry; individual pages can still be opened with `view`.

| Tile | Entry point | What to explore |
| --- | --- | --- |
| [CellScope](cellscope/README.md) | `cellscope/cellscope.ls` | Cell models, organelle information, and synchronized comparison |
| [Domino circuit](physics/README.md) | `physics/domino.ls` | A 32-tile cascade and a counter-nudge |
| [Salvage bay](physics/README.md) | `physics/salvage.ls` | Compound assemblies, impulses, and weightlessness |
| [Marble sorter](physics/README.md) | `physics/sorter.ls` | A kinematic paddle and three sensor gates |
| [Bounce lab](physics/README.md) | `physics/bounce.ls` | Restitution and measured trajectory charts |
| Ringworld | `ringworld.ls` | A procedural planet, rings, moons, and an automatic camera tour |
| Crystal garden | `three-gallery.html` | Instanced crystals, textures, lighting, and transparency |
| Kinetic observatory | `observatory.html` | Materials, shadows, satellites, and camera interaction |
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

`three-gallery.html` runs the official, unmodified Three.js 0.186.1
`WebGLRenderer` in LambdaJS. It shows 49 instanced crystals, Lambert lighting,
a repeating data texture, a torus and a transparent halo. Its `ImprovedNoise`
addon exercises a real ESM import through the document's `three` import map.
Drag to orbit, use Pan or Shift-drag to move, and scroll to zoom. The camera
toolbar also provides orbit, tilt, zoom, and Reset view. Focus the canvas for
arrow-key panning, Shift-arrow tilting, `+` / `-` zoom, and `R` reset. Camera
changes redraw the garden on demand through the unmodified OrbitControls addon.
The pinned library, addons, license and source hashes live under `vendor/three/`.

`ringworld.ls` uses the native `lambda.scene3d` package from Phase I.
It starts with a spinning storm-banded planet and three orbiting moons, sampled
by the shared SVG/native animation engine (S12.1.1v2, D7.4.4). Pause / Play and
Restart control that timeline independently of the camera. Drag to orbit,
Shift-drag or select Pan to move, and scroll to zoom. The toolbar also offers
orbit, tilt, zoom, Top / Side presets and Reset view. Focus the scene for arrow
key panning, Shift-arrow orbiting, `+` / `-` zoom, `R` reset and Space playback.
`ringworld.js` reuses unmodified OrbitControls for camera input; rendering stays
on the native scene path. Camera distance, tilt and pan have bounded limits.
Auto Play runs a repeating 24-second camera tour with orbit, pan, tilt and zoom,
starting from the current view. Stop Auto Play holds the view; dragging, scrolling,
camera keys or a camera toolbar button also stop the tour for manual control.
Pause / Play and Restart continue to control the planet/moon timeline separately.
All demo resources are local to this directory; viewing needs no network.

For PNG output with the full native host:

```sh
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe render test/demo/scene3d/three-gallery.html \
  -o temp/three-gallery.png -vw 848 -vh 650
```

Ordinary headless mode returns `null` from `getContext('webgl2')`.
The explicit GLFW option enables graphics for automated full-host captures;
the separate headless-C executable excludes graphics (D7.1.4v2).

`reference/three-gallery-chromium.png` is the original independent
Chromium/SwiftShader reference, also used for its tile preview. Its manifest
records the original viewport/canvas bounds, fixture results, calls/queries,
and shader hashes. Capture a fresh browser reference before comparing the
current page, whose breadcrumbs change the canvas position.
See [the WebGL test guide](../../webgl/README.md) for reproduction.

`observatory.html` is the Phase III interactive demo: metallic rings and satellites,
PNG normal/JPEG albedo maps, RoomEnvironment/PMREM illumination, moving PCF
shadows and EffectComposer RenderPass/FXAA/OutputPass. Drag to orbit, scroll to
zoom, click a satellite or use Play / pause. Radiant uses the first-party
`NativeAnimationMixer` binding adapter and shared SVG/native evaluator; a browser
uses the pinned upstream Three.js mixer. Addons remain unmodified.

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
