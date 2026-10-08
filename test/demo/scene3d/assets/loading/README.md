# Asset-loading fixtures

These original models and the checker image are dedicated to the public domain
under CC0. `_generate.py` regenerates every asset using Python 3's standard library.

- `concave.obj` references `materials/palette.mtl`, which resolves
  `textures/checker.png` relative to the MTL directory. Its seven-corner polygon
  uses relative position indices, independent UV/normal indices and a smoothing group.
- `articulated.gltf` and `articulated.bin` exercise strided accessors, normalized
  colors/weights, reversed joint order, nonidentity authored inverse binds, a
  transformed skinned mesh, sparse morph data and an alpha-bearing PNG in a
  buffer view. The default OPAQUE material ignores that image's alpha.
  `embedded.gltf` contains the same buffer as a data URI.
- glTF `Wave` has LINEAR rotation, CUBICSPLINE translation and STEP morph weights.
  Static matrix and quaternion markers make transform errors visible.
- `puppet.a3d` has two bones and a `Wave` action with partial overrides at
  0, 500, 1000, 1500 and 2000 milliseconds. Missing overrides retain the previous pose.
  One color uses lowercase hex to cover case-insensitive color decoding.

Geometry is deliberately small so fixed-time rendered assertions can cover
binding, interpolation, texture orientation and recovery without large assets.
The supported rendering profile is documented in WebGL design §15, following
D4.1.3/D7.2.4 and the shared animation ownership boundary in S12.1.1v2.
