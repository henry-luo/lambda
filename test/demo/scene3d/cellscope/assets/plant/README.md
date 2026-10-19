# Bundled plant specimen

This is the only bundled 3D model in cellscope. `plant.gltf` references
`plant.bin` and `plant-basecolor.jpg`; keep the three files together.

Source: [`public/models/plant-cell-first001.glb`](https://github.com/cclank/cell-architecture-studio/blob/1cab982e7a0f96af854a696430c0724707764358/public/models/plant-cell-first001.glb).
The upstream [asset ledger](https://github.com/cclank/cell-architecture-studio/blob/1cab982e7a0f96af854a696430c0724707764358/docs/ASSETS.md)
identifies it as a user-provided file (`first001.glb`), without a separate asset
license. Upstream's MIT code notice is retained in `../../LICENSE.upstream`;
that notice does not establish licensing for this model.

Preparation welds, simplifies at ratio 0.004 / error 0.01, quantizes, prunes,
and centers the source using glTF Transform and meshoptimizer. The
`KHR_mesh_quantization` extension stores positions on a 14-bit grid in
normalized signed shorts and authored normals in normalized signed bytes.
UVs use 16-bit normalized unsigned shorts; texture seams are preserved.
UV rounding is at most 1/131070, less than 0.032 texels at
the original 4096px texture resolution. The exact JPEG bytes are preserved.
The result has 11,382 vertices and 9,152 triangles; the source has
1,039,994 vertices and 1,943,049 triangles.

`plant.bin` is **237,024 bytes (237 KB / 231 KiB)**, down 53% from the previous
503,752-byte buffer and 90% from the first 2,383,652-byte port. The preparation
script enforces a strict **less than 250,000 bytes** budget. Lambda's glTF
loader decodes quantized attributes through its existing accessor reader and
node transforms; the model requires `KHR_mesh_quantization` support.
Some small structures have more visible facets when enlarged. The JPEG
remains a separate 2.7 MB file; the complete local model is about 2.9 MB.
[manifest.json](manifest.json) records source and texture SHA-256 hashes and
pinned tool versions, reduction settings, and the binary size.

Rebuild from the repository root (development tools only):

```sh
mkdir -p temp/cellscope-source
curl -L --fail \
  https://raw.githubusercontent.com/cclank/cell-architecture-studio/1cab982e7a0f96af854a696430c0724707764358/public/models/plant-cell-first001.glb \
  -o temp/cellscope-source/plant.glb
npm install --prefix temp/cellscope-tools --cache temp/npm-cache \
  --ignore-scripts --no-audit --no-fund \
  @gltf-transform/core@4.5.0 @gltf-transform/extensions@4.5.0 \
  @gltf-transform/functions@4.5.0 meshoptimizer@1.0.1
node test/demo/scene3d/cellscope/assets/prepare_plant.mjs \
  temp/cellscope-source/plant.glb test/demo/scene3d/cellscope/assets/plant temp/cellscope-tools
```

See [glTF Transform simplify](https://gltf-transform.dev/modules/functions/functions/simplify)
for the reduction options and [KHR_mesh_quantization](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_mesh_quantization)
for compact attribute storage. The renderer's 2048px texture upload cap leaves the
bundled JPEG untouched.
