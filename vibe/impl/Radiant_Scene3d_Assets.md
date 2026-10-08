# Native scene asset loading

Implemented against [WebGL design §15](../radiant/Radiant_Design_WebGL.md#15-asset-loading-into-native-scenes).
Ownership and module boundaries follow **D4.1.3**, **D7.2.4**, and
**S12.1.1v2**; no new Lambda value domain or formal ruling is introduced.

`lambda.scene3d.load()` adapts the raw OBJ/MTL, glTF JSON, and A3D mappings into
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
name resolution, parametric surfaces, voxel meshing and binary model containers
are outside this increment. WASM source registration is included; WASM
execution is unverified.

Verification commands:

```sh
make build
make -C build/premake config=debug_native test_input_model_gtest test_scene3d_gtest
./test/test_input_model_gtest.exe
./test/test_scene3d_gtest.exe
./lambda.exe test/lambda/scene3d/assets.ls
mkdir -p temp/scene3d-assets
LAMBDA_SCENE3D_CAPTURE_DIR=temp/scene3d-assets ./test/test_scene3d_gtest.exe \
  --gtest_filter='Scene3dTest.ImportedAssetGallery*'
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe render test/demo/scene3d/asset-gallery.ls \
  -o temp/scene3d-assets/gallery.png -vw 1280 -vh 520
./lambda.exe view test/demo/scene3d/asset-gallery.ls
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
