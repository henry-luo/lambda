# Lambda Chart Scientific Plotting — Proposal

**Status:** draft proposal, 2026-10-13. The requested feature scope is 3D
plotting, scientific meshes, streamlines, and signal-analysis plots. The APIs,
defaults, algorithms, and delivery boundaries below are proposed, not claims
of implemented support or ratified changes to the language.

**Scope:** extend `lambda.chart` with scientific data models and analysis,
retaining the existing chart grammar, composition, guides, selections, and
explicit-time animation. Reuse `lambda.scene3d` and Radiant for 3D rendering.

**Relationship:** extends [Lambda Chart Package](Lambda_Pkg_Chart.md), especially
its marks, transforms, coordinates, and scope boundaries. Adoption would revise
that document's §13 exclusion of 3D charts. The existing scope remains current
until adoption; this proposal does not silently supersede it. Native graphics
continue to follow [Radiant 3D and WebGL Design](radiant/Radiant_Design_WebGL.md).

## 1. Formal linkage and design direction

| Subject | Existing authority | Application |
|---|---|---|
| Package organization | **D7.2.1–D7.2.4, D7.2.6** | Ordinary source modules under `lmd/package/chart/`; no new import mechanism. |
| Pure computation | **S12.1.1v2** | Analysis and figure construction consume explicit inputs and return values. They do not advance a clock, write files, or acquire a GPU context. |
| Interactive state | **S9.1.4, S12.1.3, D6.2.3v2** | Camera, selection, and playback state belong to each applied template; handlers own mutation. |
| Ordering and diagnostics | **S6.2.2v3–S6.2.3, S7.4.1** | Stable identities and deterministic tie-breaking; invalid plot specifications return value errors. |
| Native ownership | **D4.5.1v4, D4.5.2, D5.3.3** | Retained scene data is document-owned; native numerical helpers use precise roots across allocation. |
| Host boundary | **D7.4.6, D7.5.3** | Reuse the established Radiant boundary for scene presentation and read-only projection/picking queries. |
| Execution and deployment | **D8.1.1v18, D7.1.4v2** | Analysis agrees across T0, AUTO, and MIR Direct; headless profiles acquire no implicit graphics dependency. |

These formal specifications remain authoritative. The proposal adds package
contracts within them. Any implementation that needs a changed formal ruling
must first revise both the formal spec and the relevant working design under
[Doc_Convention](../doc/Doc_Convention.md); the present draft changes neither.

The design has three parts: numerical analysis produces inspectable values;
marks turn those values into geometry; the existing SVG or scene renderer
presents that geometry. Computing a spectrum must not require a chart, and
moving a camera must not recompute a spectrum or rebuild mesh connectivity.

## 2. Coverage and current foundations

### 2.1 Proposed feature coverage

| Family | Required scope | Deliberately separate work |
|---|---|---|
| 3D plotting | Scatter, lines, surfaces, wireframes, indexed triangle meshes, vectors, bars, occupied voxels; perspective/orthographic cameras; axes, colorbars, orbit/pan/zoom, and picking | Volume ray marching, general CAD, a replacement scene engine, and true vector export of arbitrary 3D scenes |
| Scientific meshes | Rectilinear and curvilinear grids, supplied triangular connectivity, planar Delaunay triangulation, scalar coloring, isolines/filled contour bands, masks, and interpolation queries | Tetrahedral volume meshes, automatic surface reconstruction from arbitrary 3D point clouds, and a finite-element solver |
| Streamlines | Steady 2D fields on grids or triangular meshes, explicit/automatic seeds, adaptive integration, directional arrows, magnitude coloring, and termination diagnostics | Time-dependent pathlines, 3D streamtubes, fluid simulation, and geographic vector-basis transport |
| Signal analysis | Fourier spectra, phase, periodograms, Welch PSD, STFT/spectrograms, cross-spectral density, coherence, and correlation | Audio-device capture, DSP filter design, inverse STFT, wavelets, and automatic resampling of irregular observations |

Matplotlib supplies the capability reference for [3D plots](https://matplotlib.org/stable/api/toolkits/mplot3d.html),
[triangular meshes](https://matplotlib.org/stable/api/tri_api.html), and
[streamlines](https://matplotlib.org/stable/api/_as_gen/matplotlib.axes.Axes.streamplot.html).
The proposed Lambda schema is independent of its Python object model.

### 2.2 Existing mechanisms to extend

The current chart package already has density contours, vector arrows,
polygon/path marks, continuous color legends, analytical transforms, facets,
interaction state, and frame sampling. This proposal generalizes those
mechanisms where their mathematical contracts match.

Two distinctions matter:

- `field.contours(grid, levels)` already accepts a regular grid in the density
  module's representation. The gap is a supported general scalar-field model,
  isoline/band topology, masks, and chart integration; contour mathematics is
  not entirely absent.
- `geometry.triangulate(ring)` triangulates a simple polygon by ear clipping.
  It is useful for polygon rendering, but it is not point-cloud Delaunay
  triangulation or an unstructured scientific mesh model.

Lambda also has `lambda.scene3d`, with a native depth-buffered renderer.
It is not yet a scientific chart API. Chart should provide data scales, mesh
preparation, guides, and record identity around that renderer. The Three.js
controls demonstrated by the WebGL design are not evidence that equivalent
controls already exist on native Lambda chart templates.

## 3. Common data and API contracts

### 3.1 Preserve topology alongside records

Add explicitly tagged data descriptors alongside the current array-of-records
input. Named datasets and `<data values: ...>` may carry either representation.
Only recognized descriptor tags enter the new path; a generic map does not
silently become scientific data.

| Descriptor | Required structure | Association |
|---|---|---|
| `kind: "grid"` | Increasing `x[Nx]`, `y[Ny]`; `vertex_fields` and/or `cell_fields` | Vertex matrices are `[Ny][Nx]`; cell matrices are `[Ny-1][Nx-1]`. |
| `kind: "curvilinear"` | Coordinate matrices `x[Ny][Nx]`, `y[Ny][Nx]`; the same field maps | Coordinates name actual grid vertices; quadrilaterals must be valid and non-overlapping. |
| `kind: "mesh"` | `vertices: [{id, x, y, z?, ...}]`, `faces: [{id, vertices: [i,j,k], ...}]` | Face connectivity uses zero-based vertex indices; `association: "vertex"` or `"face"` selects scalar fields. |
| `kind: "signal"` | `samples`, `sample_rate`, optional `start_time` and `unit` | One evenly sampled real channel; complex input uses the explicit representation in §7. |

Grid field maps associate names with matrices, for example
`vertex_fields: {height: [...], temperature: [...]}`. Normalization exposes
`x`, `y`, and those fields to encodings. `association` is a mark option and
defaults to `"vertex"`; field names cannot shadow reserved identity or
coordinate fields. For cell data, generated records expose cell centers and
their full corner geometry. Connectivity stays separate from those records.
Optional `coordinate_fields: {x: "time", y: "frequency"}` declares aliases for
generated coordinate records; aliases must be distinct and cannot collide with
authored fields. A nonempty grid has at least two vertices on each axis; a
single cell therefore uses two edges on each axis. All matrices are rectangular.

`vertex_mask` and `cell_mask` have their corresponding matrix shapes; mesh
vertices/faces accept `masked: true`. A masked vertex removes all incident
cells/faces for the affected plot. Nonfinite coordinates are errors even if
masked. Null/nonfinite scalar values mask affected geometry; null/nonfinite
signal samples instead follow the explicit signal policy in §7. An unmasked
degenerate face is a diagnostic, not an invitation to silently repair topology.

The normalized value holds records, topology, field associations, masks, and
source identities. It is ordinary immutable Lambda data, not a native handle.
This requires an explicit extension to `dataflow.resolve`, which currently
reduces supported input to records and rejects other shapes.

Record transforms retain their existing contract. Topology-aware transforms
declare their input/output kind. Applying a row filter, sort, aggregate, or
lookup directly to a descriptor returns a diagnostic unless an explicit adapter
defines its topology effect. A mask operation preserves indices; a mesh subset
operation remaps connectivity and records the old-to-new identity mapping.
No transform may leave dangling face indices.

### 3.2 Public entry points

| Proposed API | Result and responsibility |
|---|---|
| `chart.figure(spec, viewport = null)` | Pure static presentation: existing SVG for 2D; a positioned scene-plus-SVG element tree for 3D. |
| `chart.figure_frame(spec, frame, viewport = null)` | The same presentation at explicit time, using existing frame/state conventions. |
| `chart.interactive(spec, viewport = null, options = {})` | Existing entry point extended to scientific plots and per-instance camera state. |
| `lambda.chart.mesh.prepare(data, options = {})` | Validate topology and build reusable interpolation/connectivity metadata. |
| `lambda.chart.mesh.triangulate(points, options = {})` | Planar Delaunay connectivity with stable source identities. |
| `lambda.chart.mesh.sample(prepared, positions, options = {})` | Interpolated field values and contributing cell/face identities. |
| `lambda.chart.stream.trace(field, options = {})` | Streamline geometry and per-seed termination records, without rendering. |
| `lambda.chart.signal.analyze(data, options = {})` | Spectral or correlation result, including axes, units, settings, and diagnostics. |

Existing `chart.render`/`render_spec` retain their SVG return contract and accept
all new 2D marks. A 3D specification passed there returns an actionable value
error directing the caller to `chart.figure`; it never silently changes the
return type. `render_frame` keeps the corresponding SVG-only contract.
`figure` and `figure_frame` delegate to the existing path for 2D views.

Native element and map specifications share one normalization path. Native
`<mark type: "surface">` corresponds to `mark: {kind: "surface"}`. The examples
below are proposed JSON map inputs, deliberately not executable Lambda tests
for APIs that do not exist yet.

### 3.3 Diagnostics, identity, and numerical policy

All new public numerical helpers and chart entry points return a value or
`error` under **S7.4.1**. Adapt propagated errors from reused scene helpers at
the chart boundary; do not mix failure channels according to backend.
Diagnostics identify the operation, option/field, and offending index when
available. Invalid options are checked even for empty inputs.

Authored IDs survive normalization. Default grid identities are integer
index tuples; default mesh identities use original indices; spectral records
use channel/segment/bin tuples; streamlines use seed IDs. Generated fragments
retain the source ID plus a fragment suffix. IDs never depend on formatted
coordinates, camera pose, or the order returned by a native allocator.

Numerical kernels use binary64 computation after explicit conversion, retaining
original values for labels and identity. Nonfinite conversion is diagnosed;
distinct coordinates that collapse to the same binary64 location are reported
as degenerate input rather than silently merged. This is a package computation
policy, not a change to language equality or total ordering (**S5.1.1,
S6.2.2v3–S6.2.3**).

Fixed inputs and options yield deterministic traversal and identities
(**S12.1.1v2**). Numerical accuracy is tested with operation-specific tolerances;
GPU pixels and floating-point last bits are not promised identical across
platforms. Work limits count samples, steps, vertices, and generated fragments,
never elapsed wall time. Exhaustion is explicit; an approximate or truncated
answer cannot masquerade as a complete result.

## 4. 3D plotting

### 4.1 Grammar and marks

Add `coordinate: {type: "cartesian3d", ...}` and positional channels `z`, `z2`.
Existing `point`, `line`, `bar`, and `vector` marks acquire documented 3D
geometry only in this coordinate. New `surface`, `wireframe`, `mesh`, and
`voxel` marks express topology that ordinary record marks do not contain.

| Mark | 3D contract |
|---|---|
| `point` | x/y/z positions; size remains projected area in CSS pixels. |
| `line` | Ordered x/y/z polylines, split by detail/color and missing values; stroke width is in CSS pixels. |
| `bar` | Boxes from x/x2, y/y2, z/z2; categorical x/y bands can supply horizontal extents, and a zero baseline can supply z2. |
| `vector` | x/y/z origin plus component fields selected by `components: ["u","v","w"]`; endpoints are computed in data coordinates before scale/projection. |
| `surface` | Grid vertices with z from a named vertex field; scalar color may use a different field. |
| `wireframe` | Unique grid or mesh edges; grid wireframes omit tessellation diagonals unless explicitly requested. |
| `mesh` | Indexed triangular surface, including overhangs; geometry comes from vertex x/y/z, color from the selected association. |
| `voxel` | Explicit x/y/z edge arrays and an occupancy array `[Nz][Ny][Nx]`; emit only exposed faces, retaining voxel identity. |

The voxel descriptor uses `kind: "voxels"`, three strictly increasing edge
arrays of lengths `Nx+1`, `Ny+1`, `Nz+1`, a boolean `occupied` array, and optional
cell field arrays of the occupancy shape. Sparse inputs may first be expanded
by an explicit bounded adapter; no inference from arbitrary point clouds.

Scientific positions are quantitative. Linear, log, and existing supported
continuous transforms apply independently, including reversed domains.
Categorical band positions are supported for bars; unsupported channel/mark
combinations are diagnosed. Invalid log-domain positions mask geometry under
the same stated rules as other invalid positions.

```json
{
  "width": 640, "height": 440,
  "data": {
    "kind": "grid", "x": [-1, 0, 1], "y": [-1, 0, 1],
    "vertex_fields": {
      "height": [[2, 1, 2], [1, 0, 1], [2, 1, 2]]
    }
  },
  "coordinate": {
    "type": "cartesian3d", "aspect": "equal",
    "camera": {
      "type": "perspective", "position": [3, 2, 4],
      "target": [0, 0, 0], "up": [0, 1, 0], "fov": 45
    }
  },
  "mark": {"kind": "surface", "lighting": "none"},
  "encoding": {
    "x": {"field": "x", "dtype": "quantitative"},
    "y": {"field": "y", "dtype": "quantitative"},
    "z": {"field": "height", "dtype": "quantitative"},
    "color": {"field": "height", "dtype": "quantitative"}
  }
}
```

### 4.2 Camera, aspect, and guides

Chart data convention is right-handed with z vertical. Adapt once to scene3d's
right-handed Y-up frame: `(x,y,z)` maps to scene `(x,z,-y)`. Camera position,
target, and up vectors use the resulting scene frame. Reuse the scene package's
camera names and units: FOV in degrees, rotations in radians. Add orthographic
camera support at the shared scene layer if it is absent; do not emulate it
with an extreme perspective camera.

Domains first center data and map it to bounded plot coordinates. The largest
plot-box dimension spans `[-1,1]`, with other dimensions centered and scaled by
the selected aspect ratios; explicit camera vectors use these normalized units.
`aspect: "equal"` preserves equal physical lengths per data unit for linear axes;
`"auto"` normalizes each axis independently; `[ax,ay,az]` specifies positive
plot-box ratios. Equal aspect with nonlinear scales is rejected unless the
caller explicitly selects equality in transformed coordinates. Large world
offsets are subtracted in binary64 before conversion to renderer floats.

An omitted camera is fitted deterministically to the plot box, with a documented
default viewing direction. Near/far planes are derived from that box unless
explicit; require `0 < near < far`, nonzero view direction, and a nonparallel
up vector. Clip geometry against the camera frustum before projection. No
projected division by a nonpositive camera-plane distance is allowed.

Ticks and domains come from existing scales. Axis spines and grid planes are
scene geometry; projected labels/titles and colorbars use existing measured SVG
text. A shared scene projection query supplies positions, clip status, and
depth. Guide placement uses deterministic candidate edges and the current
collision machinery, reserving margins around the plot. Colorbar values reflect
unlit scalar colors. Lighting defaults to `"none"`; optional Lambert shading
is explicitly a shape cue that alters the displayed colors.

### 4.3 Rendering and presentation

The 3D figure consists of a sized native `<scene3d>` plot viewport and aligned
SVG guides/annotations inside one positioned wrapper. Geometry, camera, and
material descriptions remain values. Radiant owns context acquisition,
depth testing, resource upload, and image presentation under **D7.5.3** and
**D4.5.1v4–D4.5.2**. The chart package does not implement a second rasterizer,
scene graph, shader service, or triangle-centroid painter's algorithm.

One plot shares one depth buffer across its layers. Opaque surface occlusion
must be correct at intersecting geometry. Point billboards and thick line
geometry must also participate in depth testing. Hidden wireframe edges are
hidden by default; an explicit x-ray style may show them. Culling defaults to
two-sided for scientific surfaces because winding alone need not imply a
closed solid.

The initial contract requires opaque scientific geometry. Intersecting
translucent surfaces need a separately specified transparency solution; reject
that unsupported style rather than claim that mesh-level depth sorting is
correct. An optional future transparency mode must describe its approximation.

### 4.4 Controls and picking

Add chart behaviors for orbit, camera pan, dolly/orthographic zoom, reset, and
named camera views. Pointer capture, cancellation, keyboard focus, and reduced
motion use the existing interaction/event infrastructure. Camera state updates
only in handlers (**S12.1.3**); inertia is off by default and, if enabled, uses
explicit sampled time. These controls must work without loading Three.js.

Picking uses the same camera, viewport, clipping, and visible geometry as the
renderer. Surface hits return source face/cell ID, barycentric coordinates,
interpolated values, and data-space position. Point/line tolerance is in CSS
pixels. Choose the nearest visible intersection, not the first SVG bounding
box match. Any shared scene ray-query helper belongs in the scene subsystem;
chart adds the mapping back to records.

Screen rectangle selection explicitly chooses `visible` or `through`; default
is visible. A 3D screen rectangle is not silently converted into independent
x/y/z scale domains. Linked 2D views receive source IDs or a declared data-space
selection. Layered 3D marks share a camera and compatible spatial domains;
independent cameras require separate figure cells.

## 5. Scientific meshes and scalar fields

### 5.1 Topology and interpolation

Scientific meshes carry connectivity through the entire pipeline. Validate
index ranges, repeated triangle vertices, zero area, duplicate IDs/faces,
field lengths, and mask shapes before rendering. Planar field meshes also
reject overlapping faces and non-manifold edges. Embedded 3D surfaces may
overlap in projection; that is a depth problem, not invalid planar topology.

Rectilinear grids support nonuniform spacing. Curvilinear quads must be
non-self-intersecting and convex. Normalize each quad to two triangles with
the fixed `(i,j)` to `(i+1,j+1)` diagonal. The chosen diagonal is metadata,
shared by coloring, contours, sampling, and surface construction; it must not
change with the camera or the scalar values. Cell fields use constant values
over the complete cell. Vertex fields use piecewise-linear interpolation on
the normalized triangles. Bilinear interpolation is not silently mixed into
one consumer of the same field.

Planar Delaunay triangulation uses x/y and retains original IDs. Duplicate
positions are rejected unless the caller explicitly deduplicates with a
defined field-reduction policy. Fewer than three noncollinear points cannot
produce a mesh. Robust orientation/incircle predicates and deterministic
cocircular tie-breaking are required; a global guessed epsilon is not a
substitute. Auto-triangulation fills the convex hull; holes and concave domain
boundaries require supplied connectivity/masks. It is not constrained Delaunay.

`mesh.sample` returns a value record and contributing primitive for each query,
or null outside the domain/inside a mask. Points on shared edges use a stable
face-ID tie-break. It never extrapolates by default. Triangle adjacency and a
spatial index are prepared once and reused for contours, sampling, streamlines,
and source-aware picking.

### 5.2 Marks and contour semantics

Use `mesh` in a 2D Cartesian coordinate for colored cells/triangles, `wireframe`
for unique edges, and new `contour`/`contour_band` marks for scalar level sets.
These cover the roles of Matplotlib's [pcolormesh](https://matplotlib.org/stable/api/_as_gen/matplotlib.axes.Axes.pcolormesh.html)
and [contour](https://matplotlib.org/stable/api/_as_gen/matplotlib.axes.Axes.contour.html)
without inheriting its positional array-shape conventions.

```json
{
  "data": {
    "kind": "mesh",
    "vertices": [
      {"id": "a", "x": 0, "y": 0, "temperature": 10},
      {"id": "b", "x": 2, "y": 0, "temperature": 20},
      {"id": "c", "x": 0, "y": 1, "temperature": 30}
    ],
    "faces": [{"id": "f0", "vertices": [0, 1, 2]}]
  },
  "mark": {"kind": "contour", "levels": [15, 20, 25], "labels": true},
  "encoding": {"color": {"field": "temperature", "dtype": "quantitative"}}
}
```

Mesh coordinates determine positions when x/y encodings are omitted. The scalar
field is the color channel's field, even if its legend is disabled. Levels are
strictly increasing finite values in original field units; an integer requests
that many automatically selected levels using the shared tick logic.

Isolines interpolate triangle-edge crossings and stitch them using topological
edge identities, never rounded-coordinate hashes. Define exact-level vertices,
flat edges, and constant triangles consistently: shared level edges emit once;
a wholly constant region contributes its outer boundary rather than its internal
triangulation. Band intervals are `[lo,hi)`, with the last interval closed at
its upper boundary. Values outside the outer levels are omitted unless an
explicit under/over band is requested. Masks remain holes, including at joins.

The existing density API keeps its existing defaults and filled-superlevel
meaning. Extract reusable edge interpolation/clipping into a shared contour
kernel; do not silently replace overlapping density superlevels with disjoint
scientific bands. Contour geometry exposes ordered paths/rings, level/band,
contributing face IDs, and source-field metadata.

Cell/face coloring is constant. Smooth vertex coloring evaluates the scalar
interpolant before color normalization. SVG uses adaptive subdivision with an
explicit color-error target and fragment budget; scene rendering evaluates the
same scalar mapping in its material path. Exhausting the SVG budget returns a
diagnostic, not an undocumented switch to flat colors or a bitmap. Default flat
vertex coloring uses the triangle's mean scalar and is labeled as that mode.

Contour labels use measured text and path length/curvature constraints. Guides,
annotations, and labels share collision handling. Linear/log color scales,
explicit domains, under/over colors, and missing-data styling use common color
normalization across 2D and 3D rather than separate renderer defaults.

## 6. Streamlines

### 6.1 Field and output contract

The `streamline` mark and `stream.trace` accept a grid or planar triangle mesh
with finite vertex vector components, selected by `components: ["u","v"]`.
The first implementation uses the shared piecewise-linear triangle interpolant.
Cell-associated/discontinuous vector fields require an explicit reconstruction
step; they are not silently averaged. Output is ordered paths with seed IDs,
sample positions, magnitudes, cumulative length, source faces, and termination
reasons for each integration direction.

```json
{
  "data": {
    "kind": "grid", "x": [-1, 0, 1], "y": [-1, 0, 1],
    "vertex_fields": {
      "u": [[1, 1, 1], [0, 0, 0], [-1, -1, -1]],
      "v": [[-1, 0, 1], [-1, 0, 1], [-1, 0, 1]]
    }
  },
  "mark": {
    "kind": "streamline", "components": ["u", "v"],
    "seeds": [{"id": "s0", "x": 0.5, "y": 0}],
    "direction": "both", "max_length": 8, "arrows": 2
  },
  "encoding": {"color": {"field": "magnitude", "dtype": "quantitative"}}
}
```

The field is steady. A path solves the direction of `dp/ds = v(p)/|v(p)|`,
with `s` measured in the declared data-space metric. `metric_scale: [sx,sy]`
supplies positive per-axis conversion factors; default `[1,1]` assumes
commensurate units. Component magnitude is measured in that same metric.
This parameterization traces geometric streamlines, not particle travel time.
`direction` is `forward`, `backward`, or `both`; a combined path contains its
seed once and arrows always point along the original vector field.

### 6.2 Integration, seeds, and termination

Use adaptive embedded Runge–Kutta integration with documented absolute/relative
tolerances and min/max step bounds in the field metric. Triangle crossings
must be localized; no accepted step or displayed segment may jump over a masked
face or a domain boundary. The shared adjacency and sampling index avoid a
full face search at every evaluation. Rendering may simplify the resulting
polyline only within an explicit screen-space error and without bridging holes.

Explicit seeds are processed in authored order; out-of-domain seeds return
per-seed `outside` records. Automatic seeds scan a documented normalized lattice
and use a deterministic occupancy grid for spacing. They require no random
state. `density` controls that lattice/spacing, not integration accuracy.
Changing the viewport can change automatic visual spacing only when requested;
the direct trace helper's default seeds are independent of viewport pixels.

| Termination | Meaning |
|---|---|
| `boundary` / `masked` | Reached the domain or a hole; endpoint is clipped to the crossing. |
| `stagnation` | Vector magnitude is below an explicit threshold. |
| `closed` | Returned near a prior point with compatible direction, within declared closure tolerances. |
| `occupied` | Automatic spacing stopped a path near an already accepted path. |
| `max_length` | Reached the requested geometric trace length. |
| `step_limit` / `tolerance_unmet` | Computation could not finish within its accuracy/work budget. |

`stream.trace` always exposes these reasons. The mark accepts geometric
terminations but treats `step_limit`/`tolerance_unmet` as value errors by default.
`allow_partial: true` explicitly permits those paths and attaches incomplete
status to tooltips and exported metadata. Seed count, total steps, and output
vertices have separate checked limits.

Arrows are positioned at equal path-length fractions and oriented along the
local tangent, reusing shared arrowhead geometry. Color/width can vary with
magnitude; legends retain the actual mappings. Variable appearance subdivides
paths while preserving one streamline identity and continuous joins.
Tracing always happens in data space before axis scaling. The initial display
coordinate is Cartesian, including unequal/log axes; unsupported polar or
geographic field interpretation is diagnosed.

## 7. Signal-analysis plots

### 7.1 Analysis is reusable data

Add a shared spectral kernel and a `signal.analyze` interface. A chart
`signal` transform delegates to it; spectrum/PSD plots use existing `line` marks,
and spectrograms use the new cell-mesh representation. Do not create independent
FFT implementations for each plot or hide analysis inside a mark renderer.

| `method` | Result |
|---|---|
| `spectrum` | Frequency records containing real/imaginary DFT coefficients, calibrated amplitude, and wrapped/unwrapped phase. |
| `periodogram` | One windowed power estimate; density or power-spectrum scaling. |
| `welch` | Mean of windowed segment periodograms, with segment count. |
| `stft` | Complex time-frequency coefficients and segment/window metadata. |
| `spectrogram` | Time-frequency values derived from the same STFT; power, PSD, amplitude, or phase. |
| `csd` | Paired-channel complex cross-power estimates from common segments. |
| `coherence` | Magnitude-squared coherence from the same auto/cross estimates. |
| `correlation` | Auto/cross correlation with explicit lag and normalization. |

The signal descriptor uses a positive finite `sample_rate` in samples/second;
`start_time` defaults to zero seconds. `samples` is a finite real array or
`{real: [...], imag: [...]}` with matching lengths. Paired methods receive a
`reference` signal with identical rate, start time, and sample count; no implicit
padding or alignment. `unit` is descriptive metadata used to label amplitude,
power, and density units. No new language-level complex scalar type is required.

A record adapter accepts `field`, optional `time`, and `groupby`. Explicit time
must increase uniformly within stated absolute/relative tolerances in seconds;
sorting is opt-in. A supplied rate must agree with the timestamps. Irregular
samples, duplicate times, or missing sample values are errors by default.
`missing: "segment"` permits invalid samples but omits every analysis window
touching one and reports omissions; it never closes gaps by concatenating valid
samples. Correlation initially requires complete samples.

### 7.2 Sampling and window options

| Option | Proposed default / rule |
|---|---|
| `window` | Periodic Hann for power estimates/STFT; rectangular for `spectrum`; explicit coefficient arrays also accepted. |
| `segment_length` | `min(256,N)` for segmented methods when omitted; explicit values must lie in `[1,N]` for nonempty input. |
| `overlap` | Half the segment length, rounded down; integer in `[0,segment_length)`. |
| `n_fft` | Segment length, or N for a full-record spectrum; may be larger, never smaller. |
| `detrend` | `constant` for power estimates, `none` for spectrum/STFT; `linear` is optional and explicit. |
| `sides` | `onesided` for real input, `twosided` for complex; reject complex one-sided requests. |
| `boundary` | `drop`: use complete windows only; explicit `pad_zero` permits a final partial window. |
| `scaling` | `density` for PSD/spectrogram power estimates; `spectrum` requests squared-amplitude units. |
| `quantity` | Spectrogram only: `power` (default), `amplitude`, or `phase`; power uses `scaling`, the others use their own definitions below. |

`spectrum` and `periodogram` use the whole record as one segment; reject
segment/overlap options on those methods rather than silently analyzing only
the first window. `welch`, `stft`, `spectrogram`, `csd`, and `coherence` use the
segmented path. Paired methods select the same usable windows from both inputs.
Correlation has its own lag/normalization options and rejects spectral-only
options. Raw `stft` rejects power-scaling and dB-display options; amplitude and
phase spectrograms reject an explicitly supplied power `scaling` option.
Phase spectrograms also reject dB display. Phase is wrapped by default;
`unwrap: true` unwraps along increasing frequency independently per segment.

Segment length and FFT length are distinct: zero padding refines the frequency
sampling but does not increase the measured record's resolving power.
Periodic Hann uses `0.5 - 0.5*cos(2*pi*n/L)` for `L > 1`; `L = 1` uses `[1]`.
Also provide rectangular, periodic Hamming, and periodic Blackman windows with
published coefficient definitions. Explicit windows must match the segment
length, be finite, and have nonzero energy. Amplitude/power-spectrum calibration
also requires a nonzero window sum. Detrending precedes windowing; zero padding
follows it. For a partial final segment, detrend only the valid samples, then
pad to L and apply the declared L-point window; metadata records that coverage.
The hop is `L-overlap`. Under `pad_zero`, process complete windows first, then
at most the next scheduled partial window if it starts before N; do not emit
an unbounded sequence of increasingly padded windows.

Empty input yields an empty typed result after validating option types,
positivity, and mutual compatibility; no data-derived segment is constructed.
A nonempty input with no usable windows is a value error. Two-sided analysis returns bins
sorted by signed frequency, with even-length Nyquist at `-sample_rate/2`.
One-sided bins run from zero through Nyquist when present. Phase at zero
magnitude is null; unwrapping restarts across null bins.

### 7.3 Numerical definitions

For a detrended segment `a[n]`, window `w[n]`, sample rate `Fs`, and FFT length
`K`, define the unnormalized forward transform and window sums:

```text
X[k] = sum(n=0..L-1, a[n] * w[n] * exp(-i * 2*pi*k*n/K))
W1 = sum(w[n])
W2 = sum(w[n]^2)
two-sided PSD[k] = |X[k]|^2 / (Fs * W2)
two-sided power spectrum[k] = |X[k]|^2 / |W1|^2
two-sided calibrated amplitude[k] = |X[k]| / |W1|
```

For real one-sided results, double interior power/PSD bins; double interior
amplitudes for sinusoidal amplitude calibration. Do not double DC or the
even-K Nyquist bin. Raw complex STFT coefficients are never doubled. Power and
amplitude factors are different operations; amplitude is not computed by
taking the square root of an already doubled power spectrum.

Welch averages the per-segment linear estimates before any dB conversion.
Cross-spectrum uses `conjugate(X) * Y`, the same window/segment boundaries and
normalization, and averages complex values before deriving phase. Coherence is
`|Pxy|^2 / (Pxx*Pyy)`; a zero denominator yields null. Clamp only a documented
roundoff-sized excursion outside `[0,1]`; a larger excursion is a numerical
failure. These conventions align with the reference definitions in
[SciPy Welch](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.welch.html),
[CSD](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.csd.html), and
[coherence](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.coherence.html).

STFT times label window centers: `start_time + (start_index + (L-1)/2)/Fs`.
Keep window support intervals separately from display-cell boundaries. For a
spectrogram, display cells meet halfway between adjacent time/frequency centers;
outer time edges extend by half a hop and outer frequency edges by half a bin,
clipped to the declared analysis time/frequency domain. A single time column
uses its window support. Return explicit cell edges and values, so the mesh
renderer never guesses sample spacing. [SciPy's STFT documentation](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.ShortTimeFFT.html)
is a reference for windowed Fourier analysis; Lambda's boundary/time conventions
are those stated here, not inherited defaults.

Display dB is a separate conversion: `10*log10(power/reference)` or
`20*log10(amplitude/reference)`, with a positive explicit reference and optional
floor. Zero maps to the floor when provided, otherwise to a masked display
value; raw analysis retains zero. Do not apply another logarithmic color scale
to dB values by default. Axes and colorbars state Hz, seconds, radians, signal
units, squared units, or squared units/Hz as appropriate.

Correlation uses `Rxy[l] = sum(conjugate(x[n]) * y[n+l])` over valid overlap,
with signed integer lag and lag seconds. `normalization` is `none` (default),
`biased` (divide by N), `unbiased` (divide by overlap count), or `coefficient`
(divide by the square root of the two full-record energies). Zero coefficient
denominator yields null. An FFT implementation must zero-pad enough to produce
linear rather than circular correlation. `max_lag` bounds output explicitly.

### 7.4 Chart adapter example

```json
{
  "data_source": {"name": "recording"},
  "transform": [{
    "type": "signal", "method": "spectrogram",
    "segment_length": 256, "overlap": 192, "n_fft": 512,
    "window": "hann", "scaling": "density",
    "display": {"type": "db", "reference": 1, "floor": -100}
  }],
  "mark": {"kind": "mesh", "association": "cell"},
  "encoding": {
    "x": {"field": "time", "dtype": "quantitative", "title": "Time (s)"},
    "y": {"field": "frequency", "dtype": "quantitative", "title": "Frequency (Hz)"},
    "color": {"field": "display_value", "dtype": "quantitative"}
  }
}
```

The caller supplies `datasets.recording` as a signal descriptor. The transform
emits a grid with explicit edges, cell fields `value` and `display_value`, and
`coordinate_fields: {x: "time", y: "frequency"}`; these aliases resolve to generated cell
coordinates without discarding edge geometry. Frequency-line methods instead
emit records plus analysis metadata. `signal.analyze` returns an envelope
`{data, metadata, diagnostics}`; the transform routes `data` into the next
stage and preserves metadata for guides/tooltips. Later incompatible transforms
are diagnosed using the common data-kind contract.

## 8. Composition, animation, export, and resource limits

### 8.1 Composition and state

Layers share topology by reference when they use the same prepared immutable
data. A contour layer over a colored mesh does not triangulate again. Facets
partition record inputs before mesh construction; an existing mesh must be
partitioned through the explicit subset adapter. Repeat/concat compose complete
figures, including mixed 2D and 3D cells, with existing sizing and guide rules.

3D data/camera animation samples the existing timeline. Fixed-topology vertex
positions and scalar values may interpolate; connectivity changes use a stated
transition such as replacement or crossfade, never interpolate vertex indices.
Avoid unsupported intersecting translucent surfaces during a crossfade by
crossfading completed scene images. The chart timeline owns playback time;
the scene consumes the sampled snapshot rather than running a second clock.
Camera rotation follows the shared scene interpolation conventions.

Signal computation depends on input samples and analysis settings, not camera,
hover, or viewport state. Streamline tracing depends on field/seed/integration
settings; projected labels depend on camera/viewport. Cache these stages
separately through caller-owned prepared values or template-owned state. No
mutable module singleton or cache keyed only by array address is permitted
(**S9.1.4, S12.1.1v2**).

### 8.2 Export and unavailable renderers

2D meshes, contours, streamlines, and signal plots retain standalone SVG output.
The 3D figure's normal static export is a scene image with vector guide overlays:
PNG composites both; PDF/SVG embeds the rendered plot image and retains guide
text/paths where supported. The image is embedded, not an external live GPU
resource. Export samples an explicit figure time and captures the corresponding
scene generation, viewport, background, and density.

This 3D export bridge is required new integration; the existing scene renderer
must not be assumed already exportable through every chart/PDF/SVG path.
Standalone true-vector 3D export and GIF/MP4 encoding are outside this proposal.
No alternate scene renderer is introduced just to preserve vector output.

A build without native graphics can construct/validate scene descriptions and
compute all analysis, but cannot render 3D pixels. Live presentation shows an
explicit unavailable-renderer diagnostic; export returns failure. It must not
produce a successful blank figure. Existing headless profile constraints remain
in force (**D7.1.4v2**). Platform support is reported against actual scene-renderer
validation, not inferred from successful package imports.

### 8.3 Accuracy and bounded work

Expose limits for input vertices/faces, generated contour fragments, streamline
seeds/steps, FFT working size, STFT cells, and scene/export dimensions. Check
dimension products for overflow before allocation. Limits extend rather than
bypass the scene subsystem's existing resource quotas.

Automatic decimation, signal downsampling, mask filling, and field smoothing
are disabled. If added as explicit transforms, they report their parameters and
source mapping. Dense spectrograms may require explicit time/frequency
aggregation to meet SVG budgets; no hidden pixel sampling changes the analysis.
Release benchmarks should establish useful defaults and practical scales before
adoption. This draft sets no unmeasured latency or data-volume promise.

## 9. Alternatives and adoption conditions

**Independent SVG 3D renderer.** It could preserve vector output for small
scenes, but duplicates camera/visibility machinery and either incurs geometric
splitting complexity or produces wrong occlusion. Prefer the existing scene
renderer and state the raster-in-vector export boundary explicitly.

**Python/Matplotlib at runtime.** It would provide broad coverage quickly, but
adds a separate execution/deployment stack and bypasses chart selections and
value composition. Use Matplotlib/SciPy as development references and numerical
oracles, not runtime dependencies.

**Everything as flattened rows.** Convenient for simple heatmaps, but loses
connectivity, masks, and sampling semantics. Keep explicit topology and expose
record views where existing encoding machinery needs them.

**Numerical kernels embedded in chart renderers.** Keep inspectable pure package
contracts and reuse shared `math` primitives for expensive numerical work.
New native kernels use Lambda/lib containers and precise rooting (**D5.3.3**);
no vendor modifications, conservative stack scanning, or retired compiler
backend is part of this work.

Adoption should confirm the new `figure` presentation API, the native 3D/export
boundary, and the explicit numerical policies above. At adoption, reconcile
`Lambda_Pkg_Chart.md` §§4, 6, 8, 11–13 and then the public package reference.
No new chart ruling-ID series is minted here; use section references while the
chart design has no established ledger series. Formal changes, if needed, follow
the linked documentation convention rather than being inferred from this draft.

## Appendix A. Integration map and delivery outline

This is a dependency outline, not an implementation progress record. A detailed
plan and measured results belong in `vibe/impl/` once implementation is requested.

| Existing location | Proposed integration |
|---|---|
| `chart/parse.ls`, `dataflow.ls`, `source.ls` | Normalize tagged data, new channels/camera options, and preserve topology through the typed transform pipeline. |
| `chart/chart.ls`, `composition.ls`, `layout.ls` | Shared figure construction, mixed compositions, viewport sizing, and frame sampling. |
| `chart/mark_renderer.ls`, `primitive.ls` | Dispatch mesh/contour/streamline marks and reuse styling/arrowheads; avoid per-view duplicate dispatch. |
| `chart/geometry.ls`, `field.ls` | Shared clipping/interpolation/contour primitives; preserve the existing density API. |
| Proposed `chart/mesh.ls`, `stream.ls`, `signal.ls` | Coherent topology, tracing, and numerical-analysis APIs, split into private kernels only as complexity requires. |
| `chart/transform.ls`, `statistics.ls`, `records.ls` | Analysis adapters and metadata; reuse grouping/validation without copying statistical helpers. |
| `chart/scale.ls`, `color.ls`, `legend.ls`, `text.ls`, `collision.ls` | Common scalar normalization, measured guides, contour labels, and 3D projected labels. |
| `chart/interaction.ls`, `behavior.ls`, `picking.ls`, `animation.ls` | Camera behavior, source-aware scientific hits, data selections, and sampled transitions. |
| `scene3d.ls`, `scene3d/transform.ls`, Radiant scene services | Reuse geometry/material/camera conventions; extend shared projection, orthographic support, native query/control services, and export as required. |
| `lambda/runtime/lambda-eval-science.cpp`, `lambda/core/lambda-decimal.cpp` | Reuse pure `math.fft/ifft/rfft/irfft`, `solve/lstsq`, and exact-sign `orient2d/orient3d/incircle`; contracts and current domain limits are in `doc/Lambda_Sys_Func.md`. |

The procedural FFT benchmark in `test/benchmark/r7rs/fft2.ls` is useful as
runtime evidence, but is not a reusable pure analysis library. Its `pn` mutation
cannot be called from the proposed `fn` API (**S12.1.1v2**). The shared
`math.fft` family provides the pure production primitive, using radix two and
Bluestein for arbitrary lengths. Signal helpers still own explicit padding,
windowing, frequency grids, and PSD normalization. Do not silently round
`n_fft` or introduce a second production FFT implementation in the package.

| Delivery stage | Dependency and completion condition |
|---|---|
| 1. Data/topology foundation | Descriptor validation, associations/masks, sampling, and mesh records; existing chart goldens stay stable. |
| 2. Scientific 2D meshes | Supplied grids/triangles, Delaunay, coloring, isolines/bands, topology tests, SVG picking, and visual fixtures. |
| 3. Signal analysis | Shared FFT/window kernel, all listed methods, documented normalization, independent numerical oracles, line/spectrogram composition. |
| 4. Streamlines | Reuse stage 1 sampling/adjacency; bounded integration, seeds, diagnostics, variable appearance, and analytic-field tests. |
| 5. 3D figure integration | Shared scene camera/depth path, every listed mark, guides, native controls/picking, and mixed figures. |
| 6. Export and conformance | Required 3D snapshot export, explicit unsupported profiles, lifecycle/resource checks, tier parity, and release measurements. |

Stages 3 and 4 do not depend on 3D rendering. The feature set is complete only
when every required row in §2.1 has passed its acceptance gate; an available
surface demo alone does not establish 3D plotting completeness.

## Appendix B. Validation and acceptance

| Area | Required evidence |
|---|---|
| Data | Shape/index/mask failures; null fields; empty inputs; exact source IDs; transforms cannot corrupt connectivity; input arrays remain unchanged. |
| Meshes | A linear field reproduces exact interpolation and straight contours; saddle/constant/edge-equality cases; masked holes; reversed winding; nonuniform/curvilinear grids; Delaunay degeneracies and cocircular ties. |
| Streamlines | Uniform flow gives straight paths; rotational flow gives circles within tolerance; saddle flow respects separatrices; no hole crossing; forward/backward consistency; seed-order stability; explicit budget failure. |
| Spectra | Impulse, constant, and bin-centered real/complex tones; DC/Nyquist factors; odd/even and prime FFT lengths; phase sign; window gain; Parseval/PSD-integral checks; zero-padding invariance of integrated power. |
| Segmented analysis | Known chirp frequency track; time centers and cell edges; overlap/partial-window rules; missing-window accounting; CSD phase; coherence near one for identical nonzero channels; zero-energy nulls. |
| Correlation | Known shifted pulses with an asserted lag sign; direct-sum oracle against FFT output; normalization and zero-padding prevent circular wraparound. |
| 3D | Known camera projections; equal aspect; perspective/orthographic behavior; near-plane crossings; intersecting opaque surfaces; hidden edges; large coordinate offsets; masked surfaces; exposed voxel faces; source-correct picks. |
| Interaction | Two independent cameras; pointer cancellation; CSS sizing/density; visible versus through selection; 2D/3D linked records; camera motion does not rerun analysis. |
| Export/lifecycle | Fixed-time scene/guide alignment; resize and teardown; generation-correct snapshots; embedded SVG/PDF images; no blank-success fallback; graphics unavailable and quota exhaustion. |

Numerical tests use closed-form answers and small independent direct-sum
implementations as primary checks. Store pinned Matplotlib/SciPy reference
fixtures with generator version/options and tolerances; generating references
is development tooling, not a dependency of ordinary tests. Do not compare
default outputs without aligning sampling, windows, scaling, masks, camera,
and interpolation contracts.

Add package cases under `test/lambda/chart/`, each `.ls` with its matching `.txt`
golden. Reuse the existing chart UI and native scene test harnesses for visible
behavior; share numerical helpers between tests instead of encoding expected
values through the implementation under test. Run package parity in `interp`,
`auto`, and `jit` per **D8.1.1v18**. Changes to Lambda or Radiant require their
respective baseline gates described in [test/README.md](../test/README.md).

Performance evidence uses release builds only and separates preparation,
analysis, geometry construction, upload, and repaint. Include growing mesh
sizes, near-degenerate triangulations, long/closed streamlines, prime-length
FFTs, and dense spectrograms. Check scaling and bounded memory as well as wall
time. Follow [Developer_Guide §7](../doc/dev/Developer_Guide.md#7-worktrees-and-agent-gotchas)
for build ownership; never run `make release` in a worktree. Temporary captures
and oracle-generation output belong under `./temp/`.
