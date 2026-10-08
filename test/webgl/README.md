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
result shapes. Only `WEBGL_lose_context` is advertised. The current upload
surface uses numeric ArrayBufferView data; DOM image/video/canvas overloads,
PBO-offset overloads, packed/compressed formats and advanced WebGL2 APIs are
outside this initial profile. Unsupported methods/extensions are not stubs.

The native fixture additionally verifies default-FBO resolve state,
immutable snapshot lifetime, lazy drawing-buffer discard and ES100/300 shader
adaptation. CPU/GPU-accounting and normalization timings are GTest XML
properties in a release run; debug runs are correctness checks only.
