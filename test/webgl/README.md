# Selected native WebGL2 gate

The existing Radiant baseline includes these fixtures through
`test_scene3d_gtest`. Native rendering is required: missing graphics fails the
test instead of counting blank output as a pass.

```sh
make test-scene3d
./test/test_scene3d_gtest.exe --gtest_filter='*WebGl*:*Three*:*Khronos*:*Es100*'
```

| Fixture | Evidence |
|---|---|
| `api.html` | 49 context, brand, typed-view, upload/readback, shader/reflection, indexed-draw, error and resize assertions; two real canvas images |
| `khronos-selected.html` | 100 assertions ported from named, pinned upstream Khronos slices; eight numeric buffer-view types, subranges, actual readback, object lifetimes and RGBA8 flip/premultiply |
| `unavailable.html` | Ordinary windowless/full-host and headless-C captures must be green: WebGL returns null and leaves Canvas 2D available |
| `loss.html` | Trusted cancelable WebGLContextEvents, consumed loss error, invalid old/lost-created wrappers and fresh native resources after restoration |
| `three-lifecycle.html` | Unmodified Three.js startup, indexed geometry, bitmap resize, density, two renderers, resource disposal and native error checks |
| `../demo/scene3d/three-gallery.html` | Unmodified pinned WebGLRenderer/addon, Lambert/basic materials, indexed geometry, texture/transparency, instancing and image output |

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

The partial API is declared in `lambda/module/radiant/webgl_methods.def`;
`webgl_constants.def` and `webgl_parameters.def` define constants and query
result shapes. The advertised extensions are `WEBGL_lose_context`, and, after actual float-target capability probes,
`EXT_color_buffer_float` and `OES_texture_float_linear`. Phase III adds local
HTML image and canvas sources for 6/7/9-argument texture uploads, including source
rectangles, skip/flip/premultiply state and tightly packed copies. Video,
PBO-offset uploads, compressed formats and advanced WebGL2 APIs remain outside
the selected profile. Unsupported methods/extensions are not stubs.

The native fixture additionally verifies default-FBO resolve state,
immutable snapshot lifetime, lazy drawing-buffer discard and ES100/300 shader
adaptation. CPU/GPU-accounting and normalization timings are GTest XML
properties in a release run; debug runs are correctness checks only.

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
LAMBDA_SCENE3D_CAPTURE_DIR=temp/webgl3 ./test/test_scene3d_gtest.exe \
  --gtest_filter=Scene3dTest.ThreePbrEnvironmentShadowsAndMultipassProduceRealPixels
node test/webgl/animation-oracle.mjs
```

The last command regenerates C++ fixed-time expected values from the unmodified
pin. Reproduce the 144-frame release measurement with:

```sh
make build-release-compile
make -C build/premake config=release_native test_scene3d_gtest
LAMBDA_SCENE3D_MEASURE_FRAMES=1 ./test/test_scene3d_gtest.exe \
  --gtest_filter=Scene3dTest.ThreePbrEnvironmentShadowsAndMultipassProduceRealPixels \
  --gtest_output=xml:temp/webgl3/release.xml
```

Debug measurements are rejected. Never run `make release` in a worktree.
[Validation and raw metrics](../demo/scene3d/reference/phase3/validation.json)
record the current cost and bounded owned storage; the rich PBR demo currently
runs at about 3 FPS on the measured M4.
