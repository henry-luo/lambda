# Native scene asset loading

Implemented against [WebGL design §15](../radiant/Radiant_Design_WebGL.md#15-asset-loading-into-native-scenes).
Ownership and module boundaries follow **D4.1.3**, **D7.2.4**, and
**S12.1.1v2**; no new Lambda value domain or formal ruling is introduced.

`lambda.scene3d.load()` adapts the raw OBJ/MTL, glTF JSON/GLB, and A3D mappings into
ordinary scene elements. Dependencies use the existing input/URL path; resource
references and animation bindings are namespaced by the caller's group ID.
Native static quaternion/matrix transforms and authored inverse binds preserve
imported poses. glTF cubic tracks use a Hermite mode in the shared SVG/native
sampler, mixer and scheduler. Raster sampler options and mip storage are
handled by the native graphics layer. The raw input mappings remain available.
glTF OPAQUE materials ignore texture/factor alpha; explicit BLEND materials
use the existing sorted transparent pass.

The supported profiles and unmapped material/geometry features are enumerated
in §15. Full PBR rendering, glTF alpha masks, compressed geometry, A3D texture
name resolution, parametric surfaces, voxel meshing and binary A3D containers
are outside this increment. WASM source registration is included; WASM
execution is unverified.

Verification commands:

```sh
make build
make -C build/premake config=debug_native test_input_model_gtest test_radiant_view_gtest test_scene3d_unit_gtest
./test/test_input_model_gtest.exe
./test/test_radiant_view_gtest.exe --gtest_filter='Scene3dTest.*:EventSimRenderTest.*'
./test/test_scene3d_unit_gtest.exe
./lambda.exe test/lambda/scene3d/assets.ls
mkdir -p temp/scene3d-assets
LAMBDA_SCENE3D_CAPTURE_DIR=temp/scene3d-assets ./test/test_radiant_view_gtest.exe \
  --gtest_filter='Scene3dTest.ImportedAssetGallery*'
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe render test/scene3d/imported-assets.ls \
  -o temp/scene3d-assets/gallery.png -vw 1280 -vh 520
./lambda.exe view test/scene3d/imported-assets.ls
```

Run graphics commands with desktop graphics access on macOS. Fixture resources
are local and the Python generator uses no third-party packages.

Validation on macOS (2026-10-08):

- Debug host and required baseline executable builds passed.
- Model input/asset tests: **21/21** passed. They cover external and data-URI
  glTF buffers, interleaved and sparse accessors, reversed skin joint order,
  authored inverse binds, cubic tracks, persistent A3D overrides, late OBJ
  material declarations, sampler options and missing/invalid dependencies.
- Native scene/WebGL/shared-animation suite: **45/45** passed. Final fixed-time
  asset/Hermite checks include OPAQUE/BLEND rendering of an alpha-bearing image.
  Captures at 0 and 1 second and the complete gallery were visually inspected.
- Scene package goldens: **21/21** runs passed (seven scripts across interpreter,
  automatic and JIT tiers), including namespaced asset composition.
- Input baseline: **1,725/1,725** passed, run with worktree executables from the
  main cwd for the tracked external-corpus symlinks (Developer Guide §7).
- Lambda runtime baseline: **4,339/4,339** passed from this worktree. An earlier
  main-cwd runtime run mixed this binary with ongoing main-checkout package
  edits; the consistent worktree run is the authoritative result.
- Radiant dimension-cast lint and `git diff --check` passed.

The full Radiant baseline remains red on two existing checks. An independent
source archive and debug build of pre-change commit `7ae626eed` reproduced both
with identical results:

| Check | Pre-change / current result |
|---|---|
| CSS live-byte budget, initial and recascade | jqueryui **382,042**, linuxmint **679,658**, bschool **736,012**, all above their recorded budgets |
| Render baseline `pp_btn_shapes_01` | **1.85%** differing pixels against the recorded **1.34%** baseline |

The render gate otherwise reported 206/212 passing fixtures, four new passes,
five expected failures and one skip on both builds. Other Radiant categories,
including native scenes, layout baselines, page snapshots, UI/DOM, view, vector,
page loading, fuzzy crash and WPT checks, passed their required gates. No
baseline thresholds or unrelated source files were changed.

The exact pre-change comparison lives under `temp/asset-baseline-src`; build
and comparison logs use `temp/asset-*`. Backdating its copied RE2 files per
Developer Guide §7 avoided regenerating the shared vendor build cache. No
vendor source was patched.

## CellScope model loading (2026-10-13)

GLB v2 JSON/BIN chunks now enter the existing glTF adapter. Header/version,
chunk alignment and bounds, duplicate chunks, and declared BIN size are checked;
geometry is copied into Input-owned values (**D4.1.3**). Static primitives over
native draw limits are partitioned with original UV/normal/color seams retained.
Static glTF primitives without normals receive per-face normals, as required by
[glTF §3.7.2.1](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes-overview);
zero-area triangles are discarded. Animated oversized primitives still fail.

HTTP acquisition of explicit `scene3d-asset` inputs uses the same 64 MiB limit
as the glTF buffer decoder; other HTTP inputs retain their 50 MiB default and
all paths honor the configured archive limit. Static native meshes omit unused deformation and default-white color lanes.
Vertex/instance buffers do not retain redundant CPU copies; index buffers
retain their exact range-validation data.
Textures accept `max-size` (integer 1..4096, default 4096), box-downsampling by
powers of two while preserving aspect ratio and source pixels. Alpha-weighted
RGB averaging avoids transparent-edge halos; mip storage uses the actual upload
dimensions. The existing 128 MiB graphics-context limit is unchanged.

`test/demo/scene3d/cellscope` keeps one reduced, textured plant model locally and loads
four pinned upstream GLBs over HTTP on selection. Its manual online fixture
verifies each published model produces nonblank native pixels; routine UI
fixtures exercise the bundled plant and procedural Muscle without remote
model downloads. Texture caps are a presentation choice in the demo, not a
source-data rewrite.

The gallery omits Epithelial. Muscle retains upstream's seven bundles, 84
sarcomeres with three bands each, capsule membrane, nuclei, mitochondria, and
four 80-by-12 Catmull–Rom tubes. Tube positions and normals match Three.js r181
to float precision. The NIH models use upstream's generated vertex coloring;
the split Neuron draws share the original primitive's color bounds. Bacteria
is explicitly labeled as a wall cross-section. Scene3D's Lambert lighting
still differs from upstream's PBR environment and contact shadows.

The follow-up demo checks passed: **9/9** golden runs across the three execution
tiers, **3/3** native UI cases, **13/13** online assertions for all four models,
and **9/9** additional online assertions after sharing the NIH color bounds.

The bundled plant buffer is now **237,024 bytes (237 KB / 231 KiB)**, below the
strict 250,000-byte budget, 53% smaller than the previous 503,752-byte asset,
and 90% smaller than the first 2,383,652-byte port. Preparation uses mesh
simplification ratio 0.004 / error 0.01, 14-bit positions in signed shorts,
8-bit signed normals, and 16-bit UVs. It preserves texture seams, authored
normal directions, and the exact source JPEG; the mesh has 11,382 vertices
and 9,152 triangles. Fine structures show more facets when enlarged.

The adapter now recognizes required `KHR_mesh_quantization` and applies its
component-encoding rules to positions, normals, UVs, and supported morph
attributes. Existing accessor decoding, node/skin transforms, and destination
Input ownership (**D4.1.3**) handle dequantization. Integer normals without
normalization, unsigned normals/morph displacements, and undeclared quantized
attributes remain errors. Model input tests passed **30/30**; the updated
asset/package goldens passed **9/9** runs across all execution tiers. All
**3/3** native UI cases passed with an isolated executable after a concurrent
build briefly removed the shared host during the first mobile run. Three
native viewing angles retain 98.6–98.9% silhouette overlap against the previous
mesh; the plant thumbnail and desktop preview were refreshed.

CellScope zoom continues optically after the camera reaches its 4-unit clearance
distance. Its framing distance now reaches 0.05 instead of stopping at 4,
allowing about 30 toolbar or keyboard zoom-in steps from the default view.
The field of view follows the perspective tangent relation, so magnification
and pan sensitivity stay continuous across the dolly/optical transition.
Both comparison cameras receive the same field of view. Regression checks
cover 30 decreasing projected spans, 24 native zoom-in clicks, matching peer
zoom, zooming back out, and reset. All **9/9** package golden runs and **3/3**
native UI cases passed; the desktop fixture now has 109 events.

CellScope validation on macOS (2026-10-13):

- Model input tests: **30/30**; native Scene3D unit tests: **24/24**.
- Scene3D/cellscope package goldens: **30/30** runs across interpreter, automatic,
  and JIT tiers. Desktop, compact, and mobile cellscope event fixtures passed.
- All four pinned remote GLBs loaded and produced native pixels: **13/13**
  assertions in the explicit online fixture. Plant and remote captures were
  visually inspected; the checked-in preview and local thumbnails were refreshed.
- Full native view suite: **175/175**. Radiant baseline: **5,427 passed,
  351 partial, one skipped, one failure**. The remaining `md_nuxt-readme` layout
  failure is the same anchor Y offset (148.5px versus browser 142.5px) in both
  this executable and the saved 09:22 pre-change executable
  `temp/timeline-implementation/lambda-release-measure.exe`. Comparison logs:
  `temp/cellview-markdown-{before,current}.log`. No baseline was changed.
- Radiant dimension-cast lint and whitespace checks passed.

The full Lambda baseline after quantization support completed **6,824/6,825**
checks: input **2,112/2,112**, runtime **4,712/4,713**. The sole failure is
`ui_blade_blade_catalog`, whose shared-contract equality check differs while
the Blade artwork is being changed independently. The same false result
reproduces with the saved 13:21 executable, before this importer change;
no Blade source or golden was changed here. Evidence is in
`temp/cellview-quarter-lambda-baseline.log`,
`temp/cellview-quarter-blade-rerun.log`, and
`temp/cellview-quarter-blade-before.txt`.
Build-time upstream sources and preparation tools stay under ignored `temp/`;
the shipped assets and preparation instructions are self-contained.

## STL, PLY, meshopt, glTF extensions and export (2026-10-26)

Implemented against [WebGL design §4, §14 and §15](../radiant/Radiant_Design_WebGL.md#15-asset-loading-into-native-scenes), including its new "glTF and GLB export" subsection. **D4.1.3**, **D7.1.1** and **D7.2.4** apply; no formal ruling changed.

- **`EXT_meshopt_compression`** (`lambda/input/input-meshopt.cpp`): the attribute (version 0), triangle and index-sequence (version 1) codecs and the octahedral, quaternion and exponential filters, every read bounded by the declared compressed length. Compressed views decode once into loader scratch before any accessor; fallback buffers are placeholders and are never read.
- **`KHR_texture_transform`** on the base-color texture is baked into the primitive's UVs. **`emissiveFactor`** with **`KHR_materials_emissive_strength`** becomes material `emissive` / `emissive-intensity`, omitted with a warning when an emissive texture would modulate it. **`KHR_lights_punctual`** lights become `<light>` children of their nodes; `lambda.scene3d.load(..., {lights: false})` drops them.
- **Renderer** (`radiant/scene3d.cpp`): `point` and `spot` lights (inverse-square falloff with a smooth `distance` cutoff; spot `angle`/`penumbra` cones, Three.js semantics) share the eight-light quota with directional lights; materials add `emissive` and `emissive-intensity`. The box/plane generator and the smooth-normal accumulation moved to `lib/mesh_primitive.h` so export reproduces rendered geometry.
- **STL and PLY** (`lambda/input/input-mesh.cpp`): one native parse feeds both the raw typed-array mapping and the scene conversion; binary STL is recognized by its size, PLY reads all three encodings.
- **Export** (`lambda/format/format-gltf.cpp`): `format(x, 'gltf')` (embedded buffer) and `format(x, 'glb')` (binary value), and `lambda convert … -t gltf|glb`, which loads the source as a scene asset. Skeletons, morphs, animation clips and depth-only materials are refused with a named error.

Verification on macOS, debug build:

- `test_input_model_gtest`: **47/47**, including 13 new cases. Meshopt streams were built by hand from the specification, with expected outputs worked out independently of the decoder (every group mode and escape, block-to-block baselines, FIFO/codeaux/free-index triangle paths, two-baseline index sequences, and all three filters). No reference-encoder fixture was available locally, so the decoder is not yet cross-checked against `meshoptimizer`'s own encoder.
- Round trips: PLY → GLB, binary STL → glTF and OBJ/MTL → GLB re-imported through the glTF loader with matching geometry and textures; an exported extension fixture re-imports its emission, lights and transformed UVs.
- `Scene3dTest.*`: **68/68**, with the new `PointSpotAndEmissiveLightsShadeLambertSurfaces`, whose expected pixels were computed from the shading model (point centre 128 and 78 one unit out, cutoff 68, spot 128 inside / 0 outside, emission 255,0,0 and 188); a deliberately wrong expectation fails. `test_scene3d_unit_gtest`: **28/28**.
- Package goldens `AutoDiscovered/*scene3d_*`: **14/14**, with the new `test/lambda/scene3d/formats.ls`.
- `make test-lambda-baseline`: **8,271/8,272** (input 2,112/2,112, runtime 6,159/6,160). The one failure, `Tiers/HotwireHttpTest.ClientAndServer/1`, stopped mid-script under full-suite load and passed on two isolated reruns; this work does not touch that path.
- `make test-radiant-baseline` passed with no failures: 6,217 passed, 141 partial (the suites' recorded partial-pass counts), 2 skipped; Radiant View 217/218 with one skip, render visual baseline 207/212 with its 4 expected failures and 1 skip.
- `make lint` (`no-int-cast-radiant`) and `git diff --check` are clean. `make check-doc-code` reports 19 failures, all in code fences these doc edits did not touch.

Remaining limits: Draco, Basis/KTX2 and USD are not supported; exported texture URIs are written as resolved (absolute `file:` URLs for loaded models) and GLB does not embed images; binary STL colour conventions are not interpreted.
