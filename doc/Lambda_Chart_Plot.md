# Scientific Charts

The chart package supports scientific grids and meshes, steady 2D streamlines,
signal analysis, and native 3D figures. Import individual numerical modules to
compute results without rendering. These functions return values or `error`
(**S12.1.1v2**, **S7.4.1**); camera interaction belongs to each applied view
(**S9.1.4/S12.1.3**).

This is the implemented subset of the
[scientific plotting design](../vibe/Lambda_Design_Chart_Plot.md). Remaining
features are listed at the end.

## Entry points

```lambda
import chart: lambda.chart.chart
import mesh: lambda.chart.mesh
import stream: lambda.chart.stream
import signal: lambda.chart.signal
```

| Function | Returns |
|---|---|
| `chart.figure(spec, viewport = null)` | SVG for 2D; a positioned native scene and SVG guides for 3D |
| `chart.interactive(spec, viewport = null)` | Applied view; 3D orbit/pan/zoom and nearest source picking |
| `chart.figure_frame(spec, frame, viewport = null)` | Existing 2D frame sampling; static figures and exact timeline endpoints for 3D |
| `mesh.prepare(data, options = {})` | Validated, reusable topology with connectivity and spatial index |
| `mesh.triangulate(points, options = {})` | Deterministic planar Delaunay mesh |
| `mesh.sample(data, positions, options = {})` | Piecewise-linear samples with face/cell IDs and barycentric weights |
| `stream.trace(field, options = {})` | Paths, settings, work counts, and per-seed termination reasons |
| `signal.analyze(data, options = {})` | `{data, metadata, diagnostics}` |

`chart.render` and `render_spec` still return SVG; they reject 3D inputs with
guidance to use `chart.figure`. Record transforms cannot reorder or flatten
scientific topology. The `signal` transform preserves analysis metadata in the
resolved dataflow's `_analysis` value.

## Meshes and contours

```lambda
let field = {
    kind: "grid", x: [0, 1, 2], y: [0, 1, 2],
    vertex_fields: {height: [[0, 1, 4], [-1, 0, 3], [-4, -3, 0]]}
};
chart.figure({width: 420, height: 300, data: field,
    mark: {kind: "contour", levels: [-2, 0, 2], labels: true},
    encoding: {color: {field: "height", dtype: "quantitative"}}})
```

Rectilinear grids use increasing `x` and `y` arrays. Vertex matrices are
`[Ny][Nx]`; `cell_fields` matrices are `[Ny-1][Nx-1]`. `curvilinear` uses two
coordinate matrices instead; cells must be convex and positively oriented.
Grids split each quad along the same diagonal. `coordinate_fields` supplies
optional output aliases, such as `{x:"time", y:"frequency"}`.

A triangular mesh uses `kind:"mesh"`, `vertices:[{id,x,y,...}]`, and
`faces:[{id,vertices:[0,1,2],...}]`. Connectivity is zero-based. Authored IDs
survive preparation; omitted IDs use original indices. Duplicate positions,
invalid indices, active degenerate triangles, duplicate faces, overlapping
planar interiors, and nonmanifold planar edges return errors. Delaunay uses
the exact-sign orientation/incircle kernels specified by **S7.10.5v3**.

Use `vertex_mask`/`cell_mask` matrices or mesh `masked:true` attributes.
Masked vertices remove incident faces. Null/nonfinite field values mask only
geometry using those fields. Mesh colors use `association:"vertex"` by
default, with `"face"` or `"cell"` for constant face/cell values. Current
vertex coloring is flat at the triangle's mean scalar value.

2D marks are `mesh`, `wireframe`, `contour`, `contour_band`, and `streamline`.
Contours require a vertex scalar in the color encoding. `levels` is an
increasing array or a positive desired tick count. Isolines join by topology;
filled bands clip triangles to successive thresholds. Wireframes omit grid
diagonals unless `diagonals:true`.

```lambda
let prepared = mesh.prepare(field);
mesh.sample(prepared, [[0.5, 0.5], [10, 10]], {fields: ["height"]})
// The second query returns null outside the domain.
```

Preparation defaults to 4,096 vertices and 8,192 faces; configure
`max_vertices`/`max_faces` explicitly. Contours enforce `max_fragments`
(100,000 by default). Exceeding a limit returns an error.

## Steady streamlines

```lambda
let flow = {kind:"grid", x:[-1,0,1], y:[-1,0,1],
    vertex_fields:{u:[[1,1,1],[0,0,0],[-1,-1,-1]],
                   v:[[-1,0,1],[-1,0,1],[-1,0,1]]}};
stream.trace(flow, {seeds:[{id:"orbit",x:0.5,y:0}],
    direction:"forward", max_length:2.5, rtol:0.0001})
```

`components` defaults to `["u","v"]`. `direction` is `forward`, `backward`, or
`both`. `metric_scale:[sx,sy]` defines the integration metric. Adaptive RK3(2)
uses `atol`, `rtol`, `min_step`, `max_step`, and `initial_step`; topology checks
clip every accepted segment at holes and domain boundaries. Automatic seeding
uses `density` and occupancy checks when `seeds` is omitted.

Paths report `forward`/`backward` termination: `boundary`, `masked`, `outside`,
`stagnation`, `closed`, `occupied`, `max_length`, `step_limit`, or
`tolerance_unmet`. `diagnostics.incomplete` distinguishes work/accuracy limits.
The mark rejects incomplete results unless `allow_partial:true` is explicit.
Use `arrows` and `arrow_size` for direction; color/size encodings can use
per-segment `magnitude`.

Limits include `max_seeds`, `max_steps`, `max_vertices`, `max_total_steps`, and
`max_total_vertices`. Analysis never silently thins input fields.

## Signal analysis

```lambda
let wave = {kind:"signal", sample_rate:64, unit:"V",
    samples:[for (i in 0 to 127) math.sin(2*math.pi*8*i/64)]};
let spectrum = signal.analyze(wave, {method:"spectrum"});
chart.figure({data:wave, transform:[{type:"signal", method:"spectrum"}],
    mark:{kind:"line"}, encoding:{
        x:{field:"frequency",dtype:"quantitative",title:"Frequency (Hz)"},
        y:{field:"amplitude",dtype:"quantitative",title:"Amplitude (V)"}}})
```

Supported methods: `spectrum`, `periodogram`, `welch`, `stft`, `spectrogram`,
`csd`, `coherence`, and `correlation`. Paired methods use `reference` with
matching rate, start time, and length. Complex signals supply
`samples:{real:[...],imag:[...]}` and require `sides:"twosided"`.

Records use `field`, optional `time`, and optional `groupby:[...]`; timestamps
must be uniformly sampled. Sorting requires `sort:true`. Group keys are
preserved on output records and in metadata. Facet records before producing
separate spectrogram grids.

| Option | Behavior |
|---|---|
| `window` | Rectangular for spectrum; periodic Hann otherwise; Hamming, Blackman, or explicit coefficients supported |
| `segment_length`, `overlap`, `n_fft` | Shared segmented FFT; full-record spectrum/periodogram reject segment/overlap options |
| `detrend` | `none`, `constant`, or `linear` |
| `sides` | One-sided real output or centered two-sided output |
| `scaling` | `density`: divide by sample rate × window energy; `spectrum`: divide by squared window sum |
| `boundary` | `drop`, or one final `pad_zero` window |
| `missing` | `error`, or omit affected windows with `segment` and report them |
| `quantity` | Spectrogram power, amplitude, or phase |
| `display` | `{type:"db",reference:1,floor:-80}`; raw values remain available |
| `normalization` | Correlation `none`, `biased`, `unbiased`, or `coefficient` |
| `max_lag` | Correlation's maximum positive/negative lag |

Spectrum rows contain raw real/imaginary coefficients, calibrated amplitude,
phase, and power. Power methods use `value`; STFT retains raw coefficients.
One-sided interior bins are doubled; DC and Nyquist are not. CSD uses
`conj(X)*Y`. Coherence uses averaged auto/cross estimates and is null for a
zero denominator. Positive correlation lag means the reference is later.

Spectrograms return a cell-associated grid with `value` and `display_value`
fields. Missing windows remain null columns, preserving their time gaps:

```lambda
chart.figure({data:wave,
    transform:[{type:"signal",method:"spectrogram",segment_length:32,overlap:24}],
    mark:{kind:"mesh",association:"cell"},
    encoding:{color:{field:"value",dtype:"quantitative"}}})
```

Sample and FFT limits default to 1,048,576; the time-frequency cell limit is
1,000,000. Use `max_samples`, `max_fft`, and `max_cells` to change them.

## Native 3D figures

```lambda
chart.interactive({width:420,height:300,data:field,
    coordinate:{type:"cartesian3d",aspect:"equal",
        camera:{type:"orthographic",view_height:3.5}},
    mark:{kind:"surface"},
    encoding:{z:{field:"height",dtype:"quantitative"},
        color:{field:"height",dtype:"quantitative",scale:{scheme:"viridis"}}}})
```

Marks: `point`, `line`, `vector`, `bar`, `surface`, `mesh`, `wireframe`, and
`voxel`. Gridded surfaces encode height through `z`; arbitrary meshes carry
vertex x/y/z positions. Vectors use `components:["u","v","w"]`. Bars accept
x2/y2/z2 extents, including categorical x/y bands. Voxels use increasing edge
arrays `x`, `y`, `z` and boolean `occupied[z][y][x]`; interior faces are omitted.

Data axes map into a shared normalized scene. `aspect` is `auto`, `equal`
(linear axes), or a positive three-number ratio. The scene camera is Y-up;
data z maps to scene y. Cameras support `perspective` and `orthographic`,
`position`, `target`, `up`, near/far planes, `fov`, and `view_height`.

Geometry is opaque and depth-buffered. `lighting:"none"` preserves scalar
colors; `"lambert"` adds lighting. Hidden wireframe edges use an opaque depth
prepass; `xray:true` explicitly disables that prepass. Screen-sized points,
lines, and arrowheads participate in depth testing. `max_triangles` defaults
to 100,000 and `max_voxels` to 100,000.

Drag to orbit, Shift-drag or middle-drag to pan, and scroll to zoom. Arrow keys
pan; +/- zoom; Home, R, or double-click resets the camera. Pointer cancellation
restores the initial pose. Clicking emits `chart_pick` with the nearest source
ID, record, view index, barycentric weights, and mesh position/field values.
Camera changes reuse prepared analysis and connectivity.

PNG uses native pixels. SVG/PDF embed each scene image with vector guides;
they do not flatten the whole page into one image. Export requires the full
Radiant build with an available native graphics host and fails if a requested
scene cannot render. Pure topology/signal/stream calculations need no GPU.

Run the gallery with:

```sh
./lambda.exe view test/demo/chart/scientific.ls
./lambda.exe render test/demo/chart/scientific.ls -o temp/scientific.svg
```

## Remaining proposal work

Smooth scalar shading, globally joined filled-band rings and under/over bands,
topology subset/mask transforms, camera-aware exterior guide placement, shared
mixed-figure guides/selections, 3D frame interpolation and live playback,
unavailable-host overlays, and release-scale performance validation remain
open. Mixed concat/repeat/facet currently compose independent static figures;
grouped spectrograms require faceting first. See the
[implementation record](../vibe/impl/Lambda_Impl_Chart_Plot.md) for exact limits
and verification evidence.
