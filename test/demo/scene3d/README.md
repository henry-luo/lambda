# Native 3D demos

From the worktree root:

```sh
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
