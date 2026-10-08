# Radiant WebGL Phase II — implementation evidence

**Status:** Initial WebGL2/Three.js profile implemented and validated on macOS,
2026-10-08. This is the selected Phase II workload in
[Radiant_Design_WebGL.md](../radiant/Radiant_Design_WebGL.md#phase-ii--js-webgl-around-the-native-graphics-core),
not complete browser WebGL conformance. Phase I's native Lambda scene path
remains available independently.

## Behavior and architecture

`HTMLCanvasElement.getContext('webgl2')` selects the native graphics mode.
Repeated requests return the same context; Canvas 2D and WebGL modes are
exclusive. Canvas bitmap attributes control drawing-buffer dimensions while
CSS controls the painted rectangle. Resizing preserves context identity and
application viewport/scissor state; Three.js updates its viewport normally.
A failed graphics request leaves the mode unselected, allowing Canvas 2D.

The adapter uses a declared static Jube interface, native-backed branded
objects, explicit prototypes, and opaque resource IDs (D7.4.1v2/D7.4.4).
`webgl_methods.def` declares 144 selected methods and their conversion/arity
contracts. Constants and query result shapes have separate catalogs. The
shared `radiant/gl_core.cpp` provider serves both JS and native scene clients;
Three.js retains its own scene graph in LambdaJS.

WebGL wrappers retain their canvas/context through precise realm-owned roots
and the host object's ownership edge (D4.5.2/D5.3.3). Native records retain IDs,
never raw GC pointers. The document owns graphics contexts and snapshot
leases; teardown removes the canvas registry before releasing its pool.
Each resource is checked against its context/kind; uniform locations also
carry program/link generation. Old wrappers remain invalid after restoration.

The selected upload path accepts numeric views, buffer byte ranges and
WebGL2 element offsets/lengths. It reacquires view data after author coercion,
checks detached/out-of-range backing storage, and prepares writable COW
storage for readback. Borrowed memory is consumed synchronously by the driver.
It cannot be interpreted as a PBO offset. Buffer shadows support draw bounds
validation; texture allocation initializes observable storage to zero.
RGBA8 and float RGBA uploads implement flip/premultiply where applicable.
Invalid query names, data types/ranges, resource uses and desktop-only state
produce WebGL errors rather than false success.

Default framebuffer zero is a private multisample target plus resolve image.
Internal resolve/readback preserves framebuffer bindings, read/draw buffer
selection, scissor, pack state/PBO bindings and application errors. Readback
clips the default buffer and leaves out-of-bounds destination bytes untouched,
as required by [Khronos's read-operation rule](https://registry.khronos.org/webgl/specs/latest/1.0/#READING_PIXELS_OUTSIDE_THE_FRAMEBUFFER).
A normal clear/draw requests repaint without requiring `flush`. Presentation
publishes an immutable ImageSurface lease (D4.5.1v4); unchanged page repaints
reuse it. With `preserveDrawingBuffer:false`, later drawing/readback lazily
resets the underlying buffer while the published image remains retained.
Writes to user FBOs do not dirty the default image.

The token-aware shader adapter handles the pinned ES100/ES300 corpus:
version normalization, precision tokens, `GL_ES`/`__VERSION__` guards, and
ES100 attribute/varying/texture/output spellings. It preserves comments and
source line numbering; `getShaderSource` returns original source. Explicit
desktop versions bypass adaptation. The system driver compiles/links and
provides actual logs/reflection. There is no ANGLE/GLSL translator dependency
or claim of complete browser shader validation.

`WEBGL_lose_context` is the sole advertised extension. Loss/restoration events
use the existing host task loop (D7.4.2v2), with actual WebGLContextEvent/Event
prototypes, trusted/cancelable event flags and one consumed loss error.
Resources created while lost receive invalid wrappers without native GPU
allocation, matching the current [Khronos context-loss contract](https://registry.khronos.org/webgl/specs/latest/1.0/#CONTEXT_LOST).
Restoration requires cancellation of the loss event and recreates resources
under a new native generation. The test covers controlled loss/restoration;
a full Three.js reconstruction/device-reset matrix remains further coverage.

The provider limits contexts to 32, resources to 4,096 per context,
dimensions to 4,096 and accounted GPU/upload storage to 128 MiB per context.
Shader sources are limited to 1 MiB. The realm identity cache retains wrappers
until teardown and is capped at 16,384 wrappers; exhaustion reports
`OUT_OF_MEMORY`. These conservative allocation estimates are not driver-private
memory measurements. Shader/program reuse follows the pinned renderer's own
cache and explicit resource lifetimes.

## Pinned library and compatibility manifest

The local demo uses official **Three.js 0.186.1**, revision
`9b4a2ac29c63ccb43fd51c5661f2f873ac2c39b8`, without library patches.
`test/demo/scene3d/vendor/three/manifest.json` records npm integrity and per-file
SHA256 hashes for `three.module.js`, `three.core.js`, `ImprovedNoise.js` and
LICENSE. Every hash was verified against the original package.

The gallery renders 49 indexed/instanced crystals with per-instance colors,
Lambert ambient/directional lighting, a repeated data texture, a basic torus,
and a transparent halo. The unmodified ImprovedNoise addon imports bare
`three` through a document import map. Generic document module resolution adds
exact/prefix mappings, relative URL normalization and nested-scope fallback;
this selected ESM path does not establish full HTML import-map conformance.

`test/demo/scene3d/reference/manifest.json` records the browser-audited API
manifest: **60 distinct gallery methods**, **88 methods across the selected
fixtures**, actual query/extension probes, and **7 unique shader hashes**
submitted through 8 shader-source calls. All audited methods are declared by
the adapter. Startup includes actual 3D/array texture placeholders. Optional
Three.js extension probes honestly return null when unimplemented.

The second Three.js fixture verifies pixel ratio 2, bitmap resize, two
independent renderers, indexed box pixels, geometry/program disposal, renderer
teardown and clean native error state. The fixtures run in both Radiant and
Chromium. All gallery assets/references live under `test/demo/scene3d/`.

## Validation

Commands and fixture scope are in [test/webgl/README.md](../../test/webgl/README.md).
`make test-scene3d` and the existing Radiant baseline include the native gate
(D7.3.5). Named Khronos assertion ports are separately pinned/licensed in
`test/webgl/conformance-manifest.json`; they are not presented as complete
upstream test files or full Khronos/WPT conformance.

| Gate | Result |
|---|---|
| Release `test_scene3d_gtest` | **29/29 pass**, including existing native scenes, JS API pixels, Three.js, context loss, shader adaptation and selected Khronos assertions |
| Browser API fixture | **49 assertions pass** |
| Browser/native selected Khronos fixture | **100 assertions pass**; eight numeric buffer-view types, source/destination subranges, actual readback, object lifetime and RGBA8 upload |
| Browser/native Three.js lifecycle | **16 assertions pass** and actual two-canvas pixels |
| Browser controlled loss/restoration | Pass |
| `LAMBDA_GC_FORCE_EVERY=1` | **3/3 pass**: native API, loss/restoration and selected Khronos fixtures |
| `make test-lambda-baseline` | **6,405/6,405 pass**: 4,301 runtime + 2,104 input |
| `make test262-baseline` | **40,261/40,261 pass**, 2,652 skipped; no baseline regressions |
| `make test-js-exception-catalog test-js-callable-catalog` | Pass |
| `make test-radiant-baseline` | **4,089 pass / 350 partial / 1 pre-existing failure** |
| `make build-headless` | Pass; no GLFW/OpenGL imports/linkage |
| Windowless full host and headless-C | WebGL returns null, Canvas 2D remains available, green pixel capture; native scene construction golden passes in headless-C (D7.1.4v2) |
| Radiant no-int-cast lint and first-party whitespace check | Pass |

The complete staged whitespace check additionally reports one original
indentation line in pinned `three.core.js`; its verified upstream bytes are
preserved. The first-party changes pass `git diff --cached --check` with that
vendor directory excluded.

The sole aggregate failure is the unchanged CSS cascade memory budget:
`jqueryui` 380,794 vs 373,940 bytes; `linuxmint` 677,194 vs 667,901;
`bschool` 734,028 vs 731,361, for initial and recascade. These are identical
to the Phase I before/after evidence. No budgets, baselines, test harnesses or
vendor files were modified to conceal it. All layout categories, page snapshot,
UI/DOM UI, view commands, vector rendering, page load, fuzzy rendering, visual
baseline and selected WPT CSS/DOM2 gates pass.

The 800×500 native canvas at density 1 was compared to the independent
Chromium 154/ANGLE SwiftShader capture, rectangle `(24,106)-(824,606)`.
Mean RGB absolute errors were **0.02173 / 0.0368875 / 0.0318375** on a 0–255
scale; **99.9019%** of channel samples differ by at most 8. These are rendered
pixels, not mocked GL output. Both images and machine-readable evidence are
under `test/demo/scene3d/reference/`.

On Apple M4, OpenGL `4.1 Metal - 90.5`, GLSL `4.10`, the release gallery made
4 draw submissions. Eight source adaptations took **442 µs total** in one
cold fixture run, excluding driver compilation. Accounted GPU bytes were
14,460,004; core CPU bytes 2,884,092; the 800×500 RGBA snapshot adds 1,600,000
bytes. These are one-run measurements, not a steady-state performance claim.

A matched release comparison rebuilt Phase I HEAD `e272d2979` in an isolated
temporary worktree, using the same arm64/compiler/link/strip configuration.
The current stripped executable is **19,715,368 bytes**,
gzip-9/mtime-0 **8,943,875 bytes**. The paired Phase I
executable is 19,583,016 bytes: growth is
**132,352 bytes**.
The deterministic complete `lambda` + tracked `lmd` runtime package grows
from **11,481,771** to **11,530,050 gzip bytes**,
a delta of **48,279 bytes**.
All runtime files are unchanged except the executable; the source package
manifest/recipe is preserved in `temp/webgl2/release-measurement.json` and
summarized in the checked-in native evidence. Three.js is a local demo
dependency, not bundled into the executable or the `lmd` runtime package.

## Explicit remaining coverage

The initial profile implements the selected typed-data texture overloads.
DOM image/video/canvas and PBO-offset upload/readback overloads, packed and
compressed formats, advanced UBO/MRT/transform-feedback/query/sampler/sync
surfaces, PBR, shadows, postprocessing, controls/picking, additional platforms,
comprehensive device recovery and complete shader/API conformance remain
separately scoped work. Missing methods/extensions are unavailable rather
than success stubs. The checked-in catalogs and audited calls define the
implemented Phase II surface.
