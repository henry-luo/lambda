# Scientific chart implementation

**Status:** substantial first implementation, 2026-10-13; the complete proposal
is not yet delivered. Design authority:
[Lambda_Design_Chart_Plot](../Lambda_Design_Chart_Plot.md). User guide and gallery:
[Lambda_Chart_Plot](../../doc/Lambda_Chart_Plot.md),
[scientific.ls](../../test/demo/chart/scientific.ls).

Source packages follow **D7.2.1–D7.2.4**. Analysis and presentation construction
are pure (**S12.1.1v2**); applied camera state follows **S9.1.4/S12.1.3**.
Failures are value errors (**S7.4.1**). Rendering uses the existing Radiant
boundary (**D7.5.3**) and document-owned native resources (**D4.5.1v4/D4.5.2**),
without new retained GC borrows or conservative stack scanning (**D5.3.3**).
Numerical modules reuse the math kernels already landed in `78906d684`.

## Delivered

| Area | Implementation |
|---|---|
| Dataflow | Tagged grid, curvilinear, mesh, prepared-mesh, signal, and voxel inputs survive data resolution; record transforms/facets reject topology they cannot preserve. Signal transforms retain their analysis envelope. |
| Topology | Shape/field/mask/index/ID checks; planar overlap/nonmanifold validation; fixed grid diagonals; adjacency and spatial bins; vertex/face/cell sampling; exact-sign planar Delaunay with stable ties. |
| Scientific SVG | Flat mesh coloring, grid-cell quads, diagonal-aware wireframes, stitched isolines, triangle-clipped bands, measured collision-filtered contour labels, common scales/guides/style/tooltips. |
| Signals | All eight requested methods; real/complex input, explicit sampling, windows/detrending, calibrated amplitudes/power, centered bins, zero padding, shared missing windows, phase unwrapping, dB display, paired cross estimates and lag conventions. Grouped record outputs preserve channel IDs. |
| Streamlines | Adaptive arc-length RK3(2), field metric, segment/domain splitting, explicit or deterministic automatic seeds, occupancy and closure checks, two-direction assembly, work limits and termination diagnostics. Segment magnitude controls appearance; arrows follow equal-length positions. |
| Native 3D | Points, lines, bars, vectors, gridded surfaces, indexed meshes, hidden-line wireframes, exposed voxel faces; normalized continuous scales and categorical bar axes; aspect ratios; opaque colors and optional Lambert lighting. |
| Cameras/picking | Shared pure scene-camera projection, clipping, ray, triangle, billboard, ribbon and control helpers; perspective/orthographic native parity; source IDs, barycentric mesh values and nearest visible picks. |
| Interaction | Applied per-instance orbit, pan, zoom, keyboard/reset and pointer cancellation; `chart_pick` events; analysis/connectivity reused for camera changes. |
| Composition | Static mixed concat/repeat and record facets compose complete independent 2D/3D figures. SVG-only entry points reject 3D specifications. |
| Export | Native scene snapshots lower through the shared image paint command into SVG/PDF; PNG uses the existing raster path. Full Radiant lazily acquires a graphics host only on scene snapshot demand. Invalid/unavailable scenes fail exports. |

`chart/mesh.ls`, `mesh_contour.ls`, `scientific.ls`, `signal.ls`, `stream.ls`,
and `plot3d.ls` share existing record grouping, geometry clipping, scales,
styling, text measurement and collision helpers. `scene3d/camera.ls` owns the
projection/control mathematics. Native changes add orthographic projection,
`view_height` animation admission, opaque depth-only material drawing, and
scene image callbacks for the vector export walker.

The hidden-line depth prepass disables color writes and applies polygon bias;
visible edges then use normal depth testing. Native GL state is restored after
the draw. A failed live scene remains inspectable; the fatal render flag is
reserved for file export so diagnostic event tests and live repair can run.

## Verification

- `make build` and `make build-radiant-baseline` succeed.
- The final chart golden suite passes **126/126** on the interpreter. An
  earlier complete forced-JIT chart run also passed **126/126**; the five new
  scientific tests subsequently passed **5/5** on JIT. Final signal/3D identity,
  picking, composition and group-budget additions passed **2/2** on forced JIT.
- The five new golden scripts return an empty failure list. Assertions cover
  interpolation, Delaunay identities/degeneracy, masks, contours, stream
  boundaries/circles/work limits, spectral calibration/Parseval/lag/phase,
  complex input, missing windows, group limits, 3D clipping/cameras/picking,
  bars, voxels, and composition.
- Forced collection and freed-memory poisoning pass **3/3** interpreter tests
  for mesh, signal and 3D (`LAMBDA_GC_FORCE_EVERY=1`,
  `LAMBDA_GC_POISON_FREED=1`).
- Native pixel tests verify distance-independent orthographic size and depth
  clipping; depth-only materials hide rear geometry without painting over the
  background; SVG export embeds scene pixels and rejects an invalid camera.
  The full native scene unit suite passes **22/22** in the second baseline run.
- `ScientificChartCamerasAndPickingStayIndependent` passes all **13 events**:
  source picking, orbit changes, a second unchanged instance, and Home reset.
  The existing typed-text invalid-scene diagnostic test also passes after
  correcting export-only fatality.
- The six-panel gallery was rendered and visually inspected in PNG and PDF;
  SVG includes both native scene image payloads alongside vector guides.
  PDF image inspection confirms viewport-sized images, not a page bitmap.

The aggregate Radiant gate is **not clean**. The first run had 5,423 passes,
351 partial results, one skip, and two failures: a live invalid-scene regression
(fixed and independently retested) and Markdown `md_nuxt-readme`. The latter
has a 6px inline-link y difference against its saved reference, reproduced
individually without changing its baseline. The second run passed the native
scene suite and the corrected live case, but retained the Markdown mismatch,
timed out in WPT input events, and failed two concurrently edited Cellview
layout fixtures. The disk filled during this run; chart-owned temporary
runtime logs were removed afterward. These aggregate failures are not masked.
The isolated WPT input-event rerun completes with **20 passes, 12 skips, zero
failures**; skips are the harness's existing capability/assertion admissions.
The Radiant integer-cast lint rule and `git diff --check` pass.

## Remaining proposal requirements

| Delivery stage | Remaining work |
|---|---|
| 1: descriptors | Public topology-preserving subset/mask adapters; stronger validation of externally supplied prepared-cache values; broader extreme-range diagnostics. |
| 2: meshes | Adaptive smooth scalar coloring with error/budget contract; globally joined contour-band rings, under/over bands, curvature-aware labels and precise 2D geometry picks. |
| 3: signals | Grouped spectrogram envelope/composition, derived dimensional unit labels, configurable timestamp tolerances and exhaustive numerical-overflow diagnostics. |
| 4: streams | Spatial occupancy and adjacency-driven traversal for large fields; broader boundary/separatrix/offset stress coverage. Current edge scans and point-list occupancy are bounded but not release-benchmarked. |
| 5: figures | Camera-aware exterior ticks/axis titles/grid planes, fully resolved shared layer/figure guides and selection state, fixed-topology 3D frame interpolation/live playback, linked cameras and complete accessibility controls. |
| 6: production evidence | Unavailable-host visible overlay, explicit export-time camera/frame controls beyond static/endpoints, headless-profile build verification, release timing/memory/large-mesh measurements and wider cross-platform pixel coverage. |

Smooth shading and intermediate 3D timeline frames explicitly return errors;
they do not silently approximate the unimplemented contract. Grouped
spectrogram analysis directs callers to facet records first. Connectivity
changes are not interpolated. Current static composition uses independent
complete figures, without promising shared legends or linked selections.

Known presentation limits: filled bands are clipped per source triangle, so
anti-aliasing seams can be visible; 3D tick placement currently uses fixed box
edges; transformed SVG exports can log a streamed backdrop transform-stack
diagnostic even when the final vector scene is correct. The latter needs a
persistent backdrop transform state in the shared export path, rather than
a chart-local workaround.

No release performance claims are made from debug test durations. No formal
semantics/design ruling was changed by this implementation.
