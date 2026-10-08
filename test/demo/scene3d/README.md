# Native 3D demos

From the worktree root:

```sh
./lambda.exe view test/demo/scene3d/observatory.html
./lambda.exe view test/demo/scene3d/shared-animation.html
./lambda.exe view test/demo/scene3d/three-gallery.html
./lambda.exe view test/demo/scene3d/ringworld.ls
```

`three-gallery.html` runs the official, unmodified Three.js 0.186.1
`WebGLRenderer` in LambdaJS. It shows 49 instanced crystals, Lambert lighting,
a repeating data texture, a torus and a transparent halo. Its `ImprovedNoise`
addon exercises a real ESM import through the document's `three` import map.
The pinned library, addon, license and source hashes live under `vendor/three/`.

`ringworld.ls` uses the native `lambda.scene3d` package from Phase I.
All demo resources are local to this directory; viewing needs no network.

For PNG output with the full native host:

```sh
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe render test/demo/scene3d/three-gallery.html \
  -o temp/three-gallery.png -vw 848 -vh 650
```

Ordinary headless mode returns `null` from `getContext('webgl2')`.
The explicit GLFW option enables graphics for automated full-host captures;
the separate headless-C executable excludes graphics (D7.1.4v2).

`reference/three-gallery-chromium.png` is the independent Chromium/SwiftShader
reference. `reference/manifest.json` records viewport, canvas bounds, fixture
results, actual calls/queries and hashes of the generated shader corpus.
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
