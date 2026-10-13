# CellScope

A native Lambda Scene3D port of the gallery in
[cclank/cell-architecture-studio](https://github.com/cclank/cell-architecture-studio),
pinned to commit `1cab982e7a0f96af854a696430c0724707764358`.

```sh
./lambda.exe demo cellscope
# Equivalent direct entry:
./lambda.exe view test/demo/cellscope/cellscope.ls
```

![Native CellScope](preview.png)

The initial **plant cell** is a real textured mesh: **11,382 vertices and
9,152 triangles**, reduced from upstream's `plant-cell-first001.glb`. Its
irregular wall, organelles, authored normals, UVs, and original 4096px color
texture are retained. `plant.bin` is **237 KB / 231 KiB** (90% smaller than the
first port), using mesh reduction and `KHR_mesh_quantization`; small structures
show more facets when enlarged. The bundled model occupies about **2.9 MB** including its
original JPEG; the native
viewer uploads the plant texture at 2048px and the much larger white-blood-cell
model's texture at 1024px to leave room for geometry and render
targets within Scene3D's existing GPU budget.

Only the plant model is stored in this repository. Other published models load
on selection from the pinned upstream GitHub revision, using Lambda's HTTP
input cache under `temp/cache/`:

| Specimen | Model source | Approximate download |
|---|---|---:|
| Plant | Local reduced glTF, binary buffer, and JPEG | None |
| White blood cell | `white-blood-cell-user.glb` | 56.6 MB |
| Neuron | `neuron-nih.glb` | 2.9 MB |
| Bacterial cell wall | `bacteria-wall-nih.glb` | 0.5 MB |
| Animal cell | `animal-cell-nih.glb` | 1.5 MB |
| Muscle | Full upstream procedural geometry, expressed in Lambda | None |

The first remote selection can take time to download, decode, and mount.
Failures display an error with **Retry model**. Remote specimens and their
thumbnails need a network connection on first use. Camera changes reuse the
selected model; switching between imported specimens replaces the viewport's model.
The three NIH meshes have no source textures or vertex colors. The demo applies
upstream's `applyAssetVertexColors` palette and spatial color variation, with
the same linear-light color mixing. They retain the published geometry. The
bacterial asset depicts a Gram-positive wall cross-section, not a whole bacterium;
its broad, plain surfaces are part of that source model.

Muscle preserves upstream's seven myofibril bundles, twelve sarcomeres per
bundle, and all three differently colored bands per sarcomere (252 band meshes).
It also retains the capsule membrane, three nuclei with nucleoli, four
mitochondria, and four curved reticulum tubes. Radii, lengths, positions,
tessellation, and the 80-by-12 centripetal Catmull–Rom tubes follow the Three.js
builder, including arc-length spacing. Epithelial is omitted from this gallery.

Select an organelle to read its description, properties, and a short fact.
**Focus** dims unselected muscle structures; **Cutaway** reduces the muscle
membrane opacity, as in upstream. Both controls apply only to Muscle;
the imported models have no organelle segmentation. **Compare specimens**
opens a second viewport with a synchronized camera. Search filters the gallery;
the star saves specimens for the current session.

Drag to orbit, Shift-drag to pan, and scroll to zoom. When a scene is focused,
use arrow keys to orbit, `+` / `-` to zoom, `R` to reset, or Space to toggle
rotation. Dragging stops automatic rotation. Toolbar buttons provide the same
zoom, reset, and rotation controls.
Zoom supports about 30 `+` clicks from the initial view. After the camera
reaches its clearance distance, further zoom narrows the field of view;
the camera stays outside the specimen. Comparison views use the same zoom.

The application, event handlers, and frame callbacks are Lambda. The
`lambda.scene3d` package describes retained geometry and materials (**D7.2.4**);
handlers update DOM attributes (**S12.1.3**, **D4.5.1v4**). GLB decoding uses the
native input layer and Input-owned values (**D4.1.3**). Runtime operation needs
no React, Three.js, JavaScript application, or npm installation.

Models are illustrative and not to scale; imported organelles cannot be picked individually.
The upstream quiz, tutor, notebook, progression, and export features are outside
this demo. Scene3D uses Lambert lighting, so upstream's PBR highlights,
environment lighting, and contact shadows are not reproduced.

`mod_assets.ls` owns sources and lazy mounting; `mod_colors.ls` ports upstream's
NIH vertex coloring; `mod_data.ls` contains specimen
information; `mod_model.ls` handles state; `mod_geometry.ls` / `mod_scene.ls`
build procedural geometry and scene frames; `mod_ui.ls` / `mod_present.ls`
build and update the page; `cellscope.ls` handles events.

The upstream code's MIT notice is in [LICENSE.upstream](LICENSE.upstream).
The adapted Three.js curve algorithms retain their [MIT notice](LICENSE.three).
Model provenance and the reproducible plant preparation command are in
[assets/plant/README.md](assets/plant/README.md). The upstream asset ledger
identifies plant and white-blood-cell files as user-provided and does not give
them a separate asset license; the code license is not asserted to cover them.

## Verification

From the repository root:

```sh
./test/test_input_model_gtest.exe
./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/*cellscope_*'
./test/test_radiant_view_gtest.exe --gtest_filter='Scene3dTest.CellScope*'
```

Golden checks verify exactly one local model, four pinned web sources, the
plant's actual mesh/texture data, quantized attributes and strict 250 KB buffer
budget, the complete muscle structure, procedural
normals and winding, and numeric references from Three.js for tubes and colors. Native
UI fixtures cover desktop, 1024px, and 390px layouts without downloading remote
3D models. To verify all four published GLBs, including actual rendered pixels:

```sh
LAMBDA_HEADLESS_GLFW_WINDOW=1 ./lambda.exe view test/demo/cellscope/cellscope.ls \
  --headless --event-file test/demo/cellscope/tests/cellscope_remote.json \
  --event-result temp/cellscope-remote-result.json
```

To refresh local thumbnails and the desktop preview, replay
`tests/cellscope_smoke.json` with the same flags, then copy
`temp/cellscope-{plant,muscle}.png` to `assets/` and
`temp/cellscope-desktop.png` to `preview.png`. Remote-model previews remain web URLs.
