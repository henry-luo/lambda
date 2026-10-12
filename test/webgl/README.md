# Selected native WebGL2 gate

The existing Radiant baseline includes these fixtures through
`test_radiant_view_gtest`, where Scene3D cases share the View launcher, case workers and report while launching `lambda.exe` with event-sim fixtures. The 19 low-level native unit tests run separately in `test_scene3d_unit_gtest`; both targets remain in the Radiant baseline and `make test-scene3d`. Native rendering is required: missing graphics fails the
test instead of counting blank output as a pass.

```sh
make test-scene3d
./test/test_radiant_view_gtest.exe --gtest_filter='Scene3dTest.*:EventSimRenderTest.*'
./test/test_scene3d_unit_gtest.exe --gtest_filter='*WebGl*:*Es100*'
```

| Fixture | Evidence |
|---|---|
| `api.html` | 49 context, brand, typed-view, upload/readback, shader/reflection, indexed-draw, error and resize assertions; two real canvas images |
| `khronos-selected.html` | 100 assertions ported from named, pinned upstream Khronos slices; eight numeric buffer-view types, subranges, actual readback, object lifetimes and RGBA8 flip/premultiply |
| `unavailable.html` | Ordinary windowless/full-host and headless-C captures must be green: WebGL returns null and leaves Canvas 2D available |
| `loss.html` | Trusted cancelable WebGLContextEvents, consumed loss error, invalid old/lost-created wrappers and fresh native resources after restoration |
| `three-lifecycle.html` | Unmodified Three.js startup, indexed geometry, bitmap resize, density, two renderers, resource disposal and native error checks |
| `../demo/scene3d/three-gallery.html` | Unmodified pinned WebGLRenderer/addon, Lambert/basic materials, indexed geometry, texture/transparency, instancing and image output |

`Scene3dTest.WebGlVectorPaintRefreshesSnapshotsAndPreservesRetainedPixels` also
uses `api.html`: vector paint runs before raster setup, unchanged reads retain
the surface, and eight redraws preserve leased old pixels while retiring their
generation under **D4.5.1v4 / D4.1.4v5** in the [formal design](../../doc/Lambda_Formal_Design.md).

`CanvasWebGlExportTest.PixelsAlphaAndEffectsReachEveryOutput` generates independent
red/blue PNG references and checks 96 Canvas pairs across PNG/SVG/PDF, alpha
modes, preserved/discarded buffers, CSS effects and 1x/2x density. The control
explicitly enables the full native host's existing `LAMBDA_HEADLESS_GLFW_WINDOW=1`
option. Default null-host context absence remains covered by `unavailable.html`.

`conformance-manifest.json` pins upstream paths/revision/hashes and identifies
the exact assertion ports. `LICENSE.Khronos.txt` preserves their license.
These selected slices do **not** establish complete Khronos or WPT conformance.

Run the same four API/lifecycle fixtures in Chromium and capture the gallery:

```sh
CHROME_HEADLESS_SHELL=/path/to/chrome-headless-shell \
  node test/webgl/capture-browser.cjs temp/webgl2/browser
```

For a native capture at the same 848×650 viewport, compare its actual canvas
pixels (requires Python Pillow):

```sh
mkdir -p temp/webgl2
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe render test/demo/scene3d/three-gallery.html \
  -o temp/webgl2/three-gallery-native.png -vw 848 -vh 650
python3 test/webgl/compare_pixels.py temp/webgl2/three-gallery-native.png
```

The runner uses Puppeteer and a temporary loopback HTTP server for ESM.
It writes screenshots and an audited call/query/shader manifest under the
chosen output directory. The checked-in reference is under
`test/demo/scene3d/reference/`, alongside all gallery resources.

The partial API is declared in `lambda/dom/webgl_methods.def`;
`webgl_constants.def` and `webgl_parameters.def` define constants and query
result shapes. The advertised extensions are `WEBGL_lose_context`, and, after actual float-target capability probes,
`EXT_color_buffer_float` and `OES_texture_float_linear`. Phase III adds local
HTML image and canvas sources for 6/7/9-argument texture uploads, including source
rectangles, skip/flip/premultiply state and tightly packed copies. Video,
PBO-offset uploads, compressed formats and advanced WebGL2 APIs remain outside
the selected profile. Unsupported methods/extensions are not stubs.

The native fixture additionally verifies default-FBO resolve state,
immutable snapshot lifetime, lazy drawing-buffer discard and ES100/300 shader
adaptation. CPU/GPU-accounting, shader normalization counts and retained surface generations are asserted through `assert_render_stats`; debug runs are correctness checks only.

Phase III adds `images.html` (PNG quadrants, dynamic CanvasTexture, alpha,
unpack preservation, source rectangles, detached dimensions, 24 queued images
and replacement failure), `formats.html` (half-float target/readback, sampled
depth texture and float linear filtering) and `native-animation.html` (the
native bridge versus the pinned mixer, scheduling, GC and lifecycle). The native
runner also covers OrbitControls input/capture/cancel, CSS-scaled raycasts at
1x/2x, native bone/morph deformation, independently controlled SVG/native
playback, PBR loss during crossfade, resize and two complete contexts.

```sh
CHROME_HEADLESS_SHELL=/path/to/chrome-headless-shell \
  node test/webgl/capture-browser.cjs temp/webgl3/browser --phase3
LAMBDA_SCENE3D_CAPTURE_DIR=temp/webgl3 ./test/test_radiant_view_gtest.exe \
  --gtest_filter=Scene3dTest.ThreePbrEnvironmentShadowsAndMultipassProduceRealPixels
node test/webgl/animation-oracle.mjs
```

The last command regenerates C++ fixed-time expected values from the unmodified
pin. Reproduce the 144-frame release measurement with:

```sh
make build-release-compile
make -C build/premake config=release_native lambda test_radiant_view_gtest
LAMBDA_SCENE3D_MEASURE_FRAMES=1 ./test/test_radiant_view_gtest.exe \
  --gtest_filter=Scene3dTest.ThreePbrEnvironmentShadowsAndMultipassProduceRealPixels \
  --gtest_output=xml:temp/webgl3/release.xml
```

The optional 144-frame benchmark runs inside `lambda.exe` through the `measure_frames` event. Its `pbr-metrics.json` artifact lives under the per-launch `scene3d_artifacts` directory recorded in GTest properties. It records frame/sampling p50 and p95, vector-cache metadata and final CPU/GPU accounting. Logs, phase timings and `events.json` use the shared View report directory. The same cache and resource growth bounds remain asserted. `LAMBDA_SCENE3D_CAPTURE_DIR` still captures the PBR samples and imported model poses; capture paths are relative to each isolated launch workspace. Debug measurements are rejected by the host executable. Never run `make release` in a worktree.
[Historical validation and raw metrics](../demo/scene3d/reference/phase3/validation.json)
record the earlier cost and bounded owned storage.

On 2026-10-12, three paired release runs on the M4 under shared load reduced the
44 integration cases from 60.482/59.445/57.151 seconds to
22.234/20.811/20.750 seconds (median 59.445 → 20.811 seconds, 65% lower).
Each run retained all 779 event-sim assertions. CPU sampling identified repeated
filesystem canonicalization while reading loaded Three.js imports; registry
lookups now probe existing canonical keys before resolving aliases. The native
Scene3D, WebGL context-loss, pixel, resource-growth and retained-surface checks
remain in the same cases. Before the View merge, the unified runner completed the integration target
(44 cases plus 10 negative controls) and 19 native unit tests in 12.106 seconds
with the existing worker configuration; all 73 passed. Reproduce serial suite
timing with a release host:

```sh
RADIANT_TEST_EXECUTABLE=./lambda.exe ./test/test_radiant_view_gtest.exe \
  --gtest_filter='Scene3dTest.*' --gtest_output=json:temp/scene3d-timing.json
```

The unchanged 144-frame PBR benchmark reduced frame p50/p95 from
265.879/275.879 ms to 38.920/45.099 ms; animation sampling p50 fell from
2.853 ms to 0.324 ms. Vector-cache entry growth and CPU/GPU byte growth were
zero in both runs. These are local measurements under shared load, not CI
timing thresholds.
