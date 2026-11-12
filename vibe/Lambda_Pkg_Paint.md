# Lambda Paint Package — `lambda.paint`

> **Status:** proposal, 2026-11-09, requested by the user after a research discussion of artistic-effect libraries (G'MIC, painty), Krita's brush engines and libmypaint. Before drafting, the user decided four points on 2026-11-09: the new engine **replaces** `lambda.image`'s native round-dab brush (PT13); the native media scope covers **dabs with smudge and blending, pigment mixing, wet media and bristle brushes** (PT5); interactive painting ships first as a **new `lambda demo paint` with pen pressure and tilt** (PT10); and automated conversion targets **oil/impasto, watercolor, pencil/ink sketch and impressionist dabs** (PT11). A second round on 2026-11-09 ruled that native kernels **write in place** through `var` borrows (PT7), placed the engine at **`radiant/radiant/paint/`** (PE2), and accepted that **`lambda-cli` has no paint engine** (PT15). The same round asked how a painting loads without replaying its strokes, whether replay can be batched, and whether the GPU can paint. The answers are PT12, PT16 and PT17. The user then ruled that the CPU engine is the pixel reference and that GPU output is accepted within tolerance (PT17). PT12, PT16 and the rest of PT17 remain proposals. Everything not marked USER is proposed.
>
> **Implementation:** the user asked to proceed on 2026-11-09 and chose a `.kra`-like ZIP container for painting files (PT12). M0 is implemented: the engine kernels, the `radiant.paint_*` bridge, the package core, and `lambda.image`'s brush running on `lambda.paint`. M1's engine and brush parts are implemented too: smudge, blend modes, tips and grain in the engine, and libmypaint dynamics with `.myb` import in the package (PT6). Both were committed on 2026-11-09. M1e, macOS pen capture with the W3C pen fields and `dom.pointer_samples` (PT10, PE6), is implemented. So is M1f, the interactive session API and the first `lambda demo paint` (PT10). M1g, saving and opening `.painting` files as stored pixels plus PNG export (PT12), is implemented too, and so are bitmap tips and paper grain (M1i). M1h, the `var` procedures, landed on 2026-11-11. M1 still lacks the latency measurement and the MyPaint parity captures. M2 (pigment and bristle) was implemented on 2026-11-12, uncommitted: the pigment medium with KM mixing, drying and impasto lighting, and bristle tips over both media, in the engine, the bridge, the package and the demo. The [implementation record](impl/Lambda_Impl_Paint.md) has the evidence and the next steps.
>
> **Scope:** end-to-end painting in Lambda: brushes, strokes, canvases, media, interactive stroke-by-stroke painting, and automated conversion of a raster or vector image into a painted image. The per-pixel kernels form a small native engine in the Radiant project, specified by the companion [Radiant Paint Engine proposal](../../radiant/vibe/Radiant_Design_Paint_Engine.md) (ledger PE#). Out of scope: pixel-filter effect collections (the G'MIC approach), neural style transfer and other learned models, 3D painting, vector drawing (`lambda.editor.draw`), and full MyPaint or Krita application parity.
>
> **M3 continuation:** the native wet step, dry deposit and Lambda `var` bridge are implemented for f32 and float64 sheets (**D3.2.1**, **D7.6.4**). Round, MyPaint and bristle strokes write pigment and water; a watercolor session presents, steps and keeps sheet snapshots. The procart watercolor demo uses the native step. Paint demo integration, live-sheet file storage and remaining qualification are in the implementation record; M3 is not yet closed.
>
> **Spec linkage:** **D9.1.1v4 / D9.1.3v2** (Radiant depends on lambda-input, never on the Lambda runtime); **D9.2.1v2 / D9.2.2** (headless profiles); **D9.3.1–D9.3.4** (packages and the `lambda.*` root); **S17.2.1, D7.6.1–D7.6.2** (system functions); **D3.2.1, S11.1.1v3, S11.4.1v5** (typed arrays and their contracts); **S9.1.2, S9.2.2v2, S9.1.3v3, D5.4.6** (value semantics, views and `var` borrows); **S12.1.1v3** (`fn`/`pn`); **S13.1.3v2** (tasks and workers); **D5.5.2, D6.3.3** (Radiant borrows; native rooting); **S7.4.5v2, S17.8.1** (errors and options); **S5.4.3** (element equality). This proposal changes no formal ruling.
>
> **Authority:** package design ledger PT1–PT17 and open items PT-O2, PT-O4–PT-O6, PT-O8 and PT-O11. PT-O1, PT-O3, PT-O7, PT-O9 and PT-O10 are resolved, and their superseded wording is in [Appendix S](#appendix-s-superseded-rulings). Painting is a new area, so it gets its own series ([Doc Convention §4](../doc/Doc_Convention.md#4-working-design-docs-vibe)). Engine decisions are PE# in the Radiant record. Where this record and a formal ruling disagree, the ruling wins.

## 0. Summary

- **Lambda owns painting; native code owns pixels.** Brush dynamics, stroke construction, image analysis, stroke planning, sessions, history and the document format are Lambda. A small context-free C++ engine under `../radiant/radiant/paint/` runs only the loops that execute once per pixel: dab compositing with smudge, pigment mixing, the wet-media step, bristle trails, presentation and stroke scoring (PT1).
- **The split is libmypaint's own.** libmypaint separates the *brush* (settings, inputs, tracking, spacing, dab emission) from the *surface* (`draw_dab`, `get_color`). Lambda implements the brush. The engine implements the surface and the media that libmypaint lacks.
- **Both workflows meet at the stroke.** A stroke is data: a typed array of samples plus brush, colour and medium. The pen produces strokes interactively and planners produce them automatically. Rendering, replay, undo, tests and the file format are shared (PT2, PT12).
- **Typed arrays are the data model.** Canvas tiles, dab tables, samples, brush tips, paper, bristle state, pigment tables and analysis fields are all `ArrayNum` values (**D3.2.1**). There is no opaque native canvas object (PT3).
- **Canvases are sparse tiled layers with structural sharing.** An edit replaces only the touched 64×64 tiles, so an undo snapshot is simply the previous layer value (PT4). Interactive procedures write those tiles in place through `var` borrows; pure rendering forks each touched tile once per call (PT7).
- **A painting loads as pixels, not as a replay.** As in Krita's `.kra`, a saved painting stores its layer tiles and a flattened preview. The stroke log is optional provenance (PT12).
- **Replay is batched and can use the GPU.** When strokes must be re-rendered, a whole pass becomes one dab table and one native call. Canvas-independent dabs render tile-parallel, bit-identical to sequential rendering, and the GPU can draw them as instanced quads. The CPU engine remains the reference (PT16, PT17).
- **Analysis and planning stay in Lambda.** This answers the user's question 4. Dense analysis runs on the typed-array image toolkit, which is already native and vectorized. Per-stroke planning is branchy logic that the JIT handles well. A kernel moves to native code only through a measured gate, and a generic analysis kernel then joins the typed-array toolkit, not the paint engine (PT9).

## 1. Purpose and starting point

The package supports two workflows:

1. **Interactive painting.** A user paints stroke by stroke with a mouse or pen. Pressure, tilt and speed drive the brush, and each stroke appears with display-frame latency.
2. **Automated conversion.** A raster image (a photograph) or a vector image (SVG, a drawing) is analysed, a stroke plan is computed for a chosen style, and the plan is rendered with simulated media.

Several pieces already exist, each built for one consumer:

| Piece | Where | Today | In this proposal |
|---|---|---|---|
| `img.paint_stroke`, `img.paint_stroke_step` | `lmd/package/image.ls` → `lambda/runtime/image_edit.cpp` (`edit_dab`, `edit_stroke`, `edit_stroke_step`) | Round dab with size, hardness, spacing and flow; gray coverage over a u8 Raster; continuation that forks touched tiles | Re-implemented over `lambda.paint`; the native dab code is retired (PT13) |
| `procart.watercolor` | `lmd/package/procart/watercolor.ls` | Curtis 1997 simulation ported from Washes: `pn` passes over one `float[]` sheet of 21 planes (PG18, proposed) | Lambda reference and parity oracle for the native wet step (PT14) |
| Typed-array image toolkit | `lambda/runtime/lambda-vector.cpp`, [Typed Array §10](Lambda_Typed_Array.md#10-image-processing) | `blur`, `convolve`, `median_filter`, `erode`, `dilate`, `resize`, `affine_warp`, `label`, `histogram`, `otsu`, `grayscale`, `as_float`, `as_ubyte`, `load`, `save` | Analysis substrate (PT9) |
| `procart.rng`, `procart.noise` | `lmd/package/procart/` | Keyed counter-based randomness (PG3); noise fields | Dab jitter, stroke order, bristle placement and paper textures (PT8) |
| `dom.set_canvas_pixels` | Radiant retained `<canvas>` | Whole-bitmap upload of a `(H, W, 4)` u8 array; at most 4096 px per side and 4,194,304 px | Display path; `dom.set_canvas_pixels_rect` (M1) writes a dirty rectangle (PE7) |
| `lambda.image` document model | `lmd/package/image.ls` | image → stack → layer → raster; PNG/PSD export | Export target for finished paintings |
| Photos brush tool | `test/demo/photos/` | Pointer samples → `paint_stroke_step` preview | Keeps working on the migrated brush (PT13) |
| Radiant pointer events | `../radiant/radiant/event.hpp` | Position, buttons and movement; M1e adds pointer type, pressure, tilt, twist and coalesced samples (macOS) | Pen capture (PE6) |

## 2. Architecture (PT1)

```
  pen / mouse samples ───┐                    ┌── raster image ── analysis ──┐
                         │                    └── vector image ── geometry ──┤
                         ▼                                                   ▼
               stroke continuation ◄──── stroke document (<painting>) ◄── style planner
                         │                                                (fn)
          brush dynamics (Lambda): tracking, spacing, input curves, keyed jitter
                         │  dab table f32[N, F]
 ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┼ ─ ─ lambda.sys.paint_* bridge (Lambda runtime) ─ ─ ─ ─ ─ ─
                         ▼        views over typed-array storage
          paint engine (../radiant/radiant/paint, context-free C++)
            dabs + smudge │ pigment (KM) │ wet step │ bristles │ present │ score
                         │
           layers = tiled typed arrays ──► present ──► <canvas> / lambda.image / PNG
```

| Layer | Owner | Contents |
|---|---|---|
| `lambda.paint` package | Lambda (`lmd/package/paint.ls`, `lmd/package/paint/`) | Brushes, dynamics, strokes, sessions, history, media configuration, analysis, planners, vector input, the document format and presentation policy |
| Bridge | Lambda runtime (system functions) | Validates arguments, forks or un-shares touched tiles, builds storage views, roots values across the call (**D6.3.3**), maps engine status codes to error values (**S7.4.5v2**) |
| Engine | Radiant (`../radiant/radiant/paint/`) | Per-pixel kernels over caller-owned views. No GC values, no Lambda runtime headers, no DOM or layout types (**D9.1.1v4, D9.1.3v2**) |

**PT1 — The placement rule.** Work whose inner loop runs once per pixel of canvas storage goes to the engine. Work that runs once per sample, dab, bristle or stroke stays in Lambda.

Colour pickup is the one sequential dependency that crosses this line: each dab's smudge colour depends on pixels written by the previous dab. The engine therefore consumes a whole dab table and runs the smudge state machine inside its loop. Lambda supplies each dab's smudge parameters and receives the final smudge state, so a stroke split across several calls behaves like one call.

*Why:* per-pixel work dominates the cost. A 600-px stroke with a 40-px radius at 0.15-radius spacing deposits about 100 dabs of about 5,000 pixels each, so roughly 500,000 pixel operations. A layered painting of a 1-megapixel photograph contains on the order of 10⁵ strokes and 10⁸–10⁹ pixel operations. Per-pixel Lambda code cannot meet that. `procart.watercolor` measured a pure vectorized Lambda pass at 4–8× slower than a JIT `pn` loop on a debug build (PG18), and a JIT loop is still slower than a vectorized C++ loop. Per-dab and per-stroke work is 10³–10⁵ times smaller, more varied and more stylistic. Users customise brushes and styles at that level, and it needs Lambda's data handling.

## 3. Design decisions

### PT2 — One stroke pipeline for both workflows

A stroke is an element:

```
<stroke brush: "round_oil", color: [0.62, 0.31, 0.12], medium: 'pigment', seed: 41, samples: …>
```

`samples` is an `f32[N, 7]` array with columns `x, y, pressure, tilt_x, tilt_y, twist, time`. Interactive strokes and planned strokes take the same path: stroke → brush dynamics → dab table → engine. This has four consequences:

1. A planner's output can be previewed, edited and re-rendered with other brushes or media without re-planning.
2. An interactive painting can be replayed from its recorded strokes, including at a different resolution.
3. Tests compare plans as exact data and compare renders within a tolerance.
4. One brush definition serves both workflows. Planners emit synthetic pen samples (pressure ramps for tapered ends, for example), so a brush responds to a planned stroke as it does to a human one.

### PT3 — Typed arrays are the data model

| Data | Shape and kind |
|---|---|
| Layer tile | `f32[64, 64, C]`, with C fixed by the layer format (PT4) |
| Stroke samples | `f32[N, 7]` |
| Dab table | `f32[N, F]`, with F fixed by the engine's versioned field layout ([PE Appendix A](../../radiant/vibe/Radiant_Design_Paint_Engine.md#appendix-a-dab-table-layout-v1-draft)) |
| Brush tip | `f32[h, w]` coverage |
| Paper texture | `f32[h, w]` height, tiled across the canvas |
| Pigment table | `f32[M, 2B]`: absorption K and scattering S per band |
| Bristle state | `f32[B, G]`: per-bristle offset, length and paint load |
| Wet sheet | `f32[P, h, w]` or `float[P, h, w]`: planar planes over an active window |
| Smudge state | `f32[K, 4]`: premultiplied bucket colour and alpha (libmypaint's recentness field arrives with `smudge_length_log`) |
| Analysis fields | `f32[H, W]` or `f32[H, W, k]` (luminance, orientation, coherence, error); region labels as `i32[H, W]` |

Brushes, pigments, palettes, styles, plans and sessions are ordinary maps and elements that hold these arrays. Arrays cross the engine boundary as views over their storage. Pixels are copied only when the bridge forks a shared tile (PT4).

### PT4 — Layers are sparse tiled typed arrays with structural sharing

A layer is a map `{width, height, tile: 64, format, blend_space, tiles}`. `tiles` lists tile arrays in row-major tile order, and `null` marks an empty tile: transparent for `rgba`, clean for `pigment`. Rendering returns a new layer whose touched tiles are new arrays and whose untouched tiles are shared with the input. Undo history keeps earlier layer values and pays only for touched tiles. Captured snapshots keep their values (**S9.1.2, S9.2.2v2**). `lambda.image` already uses this technique natively for `paint_stroke_step`, which forks only touched tiles; here the tiles are visible to Lambda as typed arrays.

*Why tiles:* a dense `f32[4096, 4096, 4]` is 256 MB. One dab on a shared dense array would copy all of it, so snapshots and previews would be unaffordable. 64 is libmypaint's tile size, and one RGBA f32 tile is 64 KB. Empty canvas regions cost nothing.

*Why f32:* repeated low-opacity build-up needs more than 8 bits, and the Kubelka–Munk and wet-media arithmetic is floating point. f32 is the engine's single working kind. Conversion to u8 sRGB happens only at presentation and export. A u16 working kind for very large canvases is PT-O4.

| Format | Channels | Meaning |
|---|---|---|
| `rgba` | 4 | Premultiplied RGBA in the layer's blend space |
| `pigment` | 3B + 2 | Wet film: thickness-weighted K and S per band, which makes deposit additive, plus film thickness. Underneath: substrate reflectance per band (paper, then dried paint) and dried relief height. B = 3 initially. Drying folds the film into the substrate by Kubelka–Munk layer composition, so glazes stay transparent and impasto relief persists ([PE8](../../radiant/vibe/Radiant_Design_Paint_Engine.md#pe8--pigment-medium)) |
| `coverage` | 1 | A mask: selections and wash-mode stroke coverage |

`blend_space` is `'linear'` (linear light, the default) or `'srgb'` (sRGB-encoded values, as libmypaint blends by default and as the current `lambda.image` brush does). See PT-O5.

The **wet sheet** is the one dense structure. Its stencils read neighbours across what would be tile borders on every sub-step, so it is a dense `f32[P, h, w]` or `float[P, h, w]` over a bounded active window that grows and shrinks with activity. When it dries it deposits pigment into the `pigment` layer's tiles. This keeps the layout and active-region tracking that `procart.watercolor` has already proven. For M pigments the layout has `12 + 3M` planes: eight fixed planes, M suspended, M deposited, M pigment scratch and four shared blur planes. Three pigments give the reference's 21 planes. The float64 lane lets its existing `float[]` stamps and native steps share one buffer: a float write to an f32 array can widen it under **S9.2.1**, so converting on every frame would defeat in-place stepping. Both lanes use the same kernel and are compared against the reference.

### PT5 — Three media, two tip models (USER, 2026-11-09)

The user selected all four media capabilities. They factor into three media and two tip models:

| Medium | Engine work | Lambda work |
|---|---|---|
| **Pixel** (`rgba`) | Dab masks; normal, erase, lock-alpha, colorize and posterize; blend modes; smudge buckets; build-up and wash modes | Brush settings, colours, blend-mode choice |
| **Pigment** (`pigment`) | Kubelka–Munk deposit; pickup and smudge in K/S space; thickness deposit; KM-over-paper display with impasto lighting | Pigment definitions (`pigment(rgb, …)` → K, S), palettes, lighting |
| **Wet** (sheet → `pigment`) | Curtis 1997 step: velocities, divergence relaxation, outward flow (edge darkening), pigment advection, adsorption and desorption with granulation, capillary spread, evaporation; deposit into the pigment layer | Water and pigment loads, paper, drying schedule, active-window policy |

| Tip model | Idea | Reference |
|---|---|---|
| **Dab tip** | A stroke is a sequence of dab impressions, procedural (radius, hardness, ellipse) or bitmap, modulated by paper grain | libmypaint; Krita's pixel brush engine |
| **Bristle tip** | Many hairs, each tracing its own sub-path with its own paint load, depletion and pickup | Krita's bristle engine; Strassmann 1986 |

Both tip models work with the pixel and pigment media. The wet medium receives water and pigment from dabs and bristle trails alike.

*Implemented (M2, 2026-11-12).*
- **Pigment layers.** `paint.layer(w, h, {format: 'pigment', paper})` is a clean film over the sRGB paper. `paint.pigment(colour)` gives the `{K, S}` whose opaque film shows the colour, using procart.watercolor's conversion.
- **Pigment strokes.** On a pigment layer a stroke turns its colour into its pigment, and brushes take `amount` (pigment load) and `thickness` (body, default `amount`). Wet strokes mix by concentration, so blue over yellow is green.
- **Drying and lighting.** `paint.dried(l)`, `paint.dry_layer(var l)` and the session's `paint.dry(var s)` fold the wet film into the paper, so later strokes glaze over it. `paint.lighting(options)` lights the relief when presenting, and a flat film keeps its colour.
- **Bristle brushes.** `paint.brush({kind: 'bristle', ...})` draws one trail per hair, with keyed placement, lag, depletion and pickup, on either medium; the stroke's continuation carries the bristle state.
- **Sessions and files.** `paint.session(w, h, {medium: 'pigment', lighting})` paints oil interactively, and painting files keep a pigment layer's bands and substrate.
- **Demo.** `lambda demo paint` has a Medium menu (Pixel, Oil), Dry and Light actions, and Bristle and Oil brushes.

Engine details are in PE8, PE10 and PE7's implementation notes, and decisions in the [implementation record](impl/Lambda_Impl_Paint.md).

### PT6 — Brushes follow libmypaint's model

A brush is a map of settings. Each setting has a base value plus input mappings: piecewise-linear curves over the inputs pressure, fine and gross speed, random, stroke progress, direction, tilt declination and ascension, barrel rotation, custom input and view zoom. The settings cover slow tracking (position smoothing), spacing by radius and by time, offsets and jitter, elliptical dabs, colour shifts, smudge, eraser and lock-alpha. Bristle brushes add bristle count, spread, length variance, stiffness, paint load, depletion and pickup.

`paint.brush_from_myb(json)` converts a MyPaint brush file (`.myb`, JSON version 3) into a brush and reports the settings it does not support. MyPaint brush packs then work unmodified wherever their settings are supported. libmypaint is a reference for behaviour and for the settings vocabulary. It is not vendored or linked (AGENTS rule 16; the procart PG7.5 precedent).

Lambda evaluates the dynamics. Where a setting depends pointwise on its inputs (input curves, jitter from keyed randomness), evaluation is vectorized across a batch of samples (**S10.2.1**). Where the dependence is sequential (tracking smoothing, spacing accumulation), evaluation is a short JIT loop. Smudge runs in the engine (PT1).

### PT7 — Effects and in-place native writes (USER, 2026-11-09)

Planning, analysis and dynamics are `fn`. Rendering has two forms:

| Form | Effect | Used by |
|---|---|---|
| `paint.render(painting)`, `paint.render_pass(layers, strokes)` | `fn` returning new layers. A whole pass is one native call (PT16). The bridge forks each touched tile once per call, and the engine then writes in place into tiles the call owns. | Automated conversion, replay, tests |
| `paint.stroke(var layer, …)`, `paint.feed(var session, …)`, `paint.wet_step(var sheet, …)` | `pn` system procedures with `var` parameters that write in place | Interactive sessions, live wet media |

The user ruled that native kernels write in place. In-place writes follow the existing `var` protocol, so no semantic ruling changes:

1. The caller passes the place through a caller-owned, rooted home (**D5.4.6**).
2. The bridge un-shares one level at a time down to the touched storage: the layer map, its tile array, then each touched tile. A level is copied only when its shared bit is set (**D5.4.1, D5.4.2**). A tile shared with an undo snapshot is copied on its first write in a stroke. Later writes in the same stroke go in place.
3. The bridge takes storage pointers after its last allocation point and lends them to the engine for that call only (**D5.4.4, D5.5.2**). The engine allocates no GC values, so no collection can move or free the storage during the call.
4. The engine validates every view and charges the whole work budget before its first write, so a failed call leaves the place unchanged. The bridge publishes the final value through the home (**S9.1.3v3**).

The registration form is the new part. In today's system-function registry, only `push` and `splice` mutate a `var` argument natively. M1 adds a general system-procedure form with `var` parameters to the registration checklist (**D7.6.2**). It ratifies that form as a formal design point together with the implementation (AGENTS rule 17). The user ratified the form on 2026-11-11 as **D7.6.3\*** and **D7.6.4\*** (Sys_Func RF7). A native `var` parameter declares a bound, not an exact type (**S9.1.3v4**), because host modules cannot name package types such as `Sheet`.

*Why two forms:* a `fn` can never call a `pn` (**S12.1.1v3**), so pure rendering cannot use the `var` procedures. Batching keeps the pure form cheap, because its copy cost is paid once per call rather than once per stroke. Forking the wet sheet on every step would allocate about 48 MB per step for a 1-megapixel, 12-plane sheet, so live wet painting needs the in-place form. A pure watercolor render runs its whole simulation inside one call, on a sheet that the call owns.

*As implemented (2026-11-11, M1h).* `paint.stroke(var layer, brush, colour, points, options)` and `paint.feed(var session, pointer, samples)` are procedures returning `{rect, work}`. Until `render` and `render_pass` land (PT16), `paint.stroke_step` is the pure form. Both procedures call `radiant.paint_stroke` on their own place. A Lambda `pn` between them would also borrow a member place such as `s.layer` in place unless a plain argument may alias it (**D7.6.3**; LR12-45, fixed 2026-11-11). A wash composites coverage over the stroke's source, so its preview is a new layer on both forms. The gain holds while nothing else holds the session. `paint.frame(var session)` is in place too: it returns `{rect, pixels}` and clears the dirty rectangle through the borrow, because a new session built by a frame would share the layer. `lambda demo paint` keeps its session in its view state and updates it with member writes (`session.token = 0`, `paint.feed(session.ps, …)`), so its batches write the stroke's tiles in place (38 of 57 tiles in the replay; the rest are first touches). On AUTO the same holds since `radiant.paint_present` is registered as only reading its arguments (**D7.6.5**).

### PT8 — Determinism

All randomness is keyed through `procart.rng` (PG3): dab jitter by (stroke seed, dab index), stroke order by (plan seed, pass), bristle placement by (stroke seed, bristle index). The engine contains no random source. The same source, style, options and seed produce equal plans (**S5.4.3**). Planners use `exp`, `atan2` and `sqrt`, so plans fall in the procart tolerance tier across platforms (PG2). The engine computes in f32 and is also tolerance tier across machines. On one build, a replay reproduces the pixels bit for bit.

### PT9 — Analysis and planning are Lambda

This answers the user's question 4: should image analysis and stroke planning be Lambda script or C/C++?

**Recommendation: write both in Lambda, over typed arrays. Move individual analysis kernels to native code only through a measured gate.**

1. **Most analysis is already native.** Dense analysis consists of stencils and element-wise arithmetic: pyramids (`blur` + `resize`), gradients (`convolve` with Sobel kernels), the structure tensor (products plus `blur`), orientation and coherence (closed-form eigenvectors through vectorized `sqrt` and `atan2`, **S7.10.5v3**), colour conversion to Lab, k-means assignment, connected components (`label`). The typed-array toolkit runs these as vectorized C++ (Typed Array §10–§11). Lambda code that calls them pays the boundary cost once per whole array.
2. **Planning is per-stroke logic, not per-pixel arithmetic.** It is branchy and stylistic: choosing where strokes start, tracing them along a field, deciding when to stop, ordering layers, linking edges into polylines, generating hatching streamlines. A painting has 10⁴–10⁵ strokes of 10–50 steps each, so 10⁵–10⁷ simple operations, which the JIT handles in seconds. Planners are also the part users and future styles change most, so they belong in readable, pure, deterministic and testable Lambda.
3. **C++ planners would duplicate the data model.** Strokes, brushes and plans would need parallel C++ structures, which rule 13 forbids, and the engine would become a planning framework rather than a pixel kernel library.

| Analysis step | Implementation |
|---|---|
| Gaussian pyramid, blur, resize | Existing toolkit |
| Gradients, structure tensor, orientation, coherence | Existing toolkit plus vectorized arithmetic |
| sRGB ↔ linear ↔ Lab | Vectorized arithmetic |
| Canny edges | Gradients, non-maximum suppression with masks, hysteresis through `label` |
| k-means colour quantization | Vectorized assignment and update steps |
| Edge tangent flow (ETF) | Lambda reference; **native candidate** (iterative, data-dependent weights) |
| Flow-based difference of Gaussians (FDoG) | Lambda reference; **native candidate** (line integrals along the ETF) |
| Edge-preserving smoothing (bilateral, Kuwahara) | Lambda reference; **native candidate** |
| SLIC superpixels | Lambda reference; **native candidate** |
| Edge linking, simplification, stroke tracing, hatching streamlines | Lambda (per-stroke logic) |
| Scoring candidate strokes against the target | **Engine** (`paint_score`, PE5): it is dab rasterization |

**The gate.** It follows procart PG7: an analysis kernel moves to native code only when a release-build benchmark misses its budget (§5) by at least 5× after the vectorized Lambda formulation has been tried. The Lambda version stays as the oracle and the fallback. A generic analysis kernel (ETF, bilateral, SLIC) belongs to the typed-array image toolkit, because image analysis is not painting and other packages need it. Only stroke scoring belongs to the paint engine, because it rasterizes dabs.

### PT10 — Interactive session (USER, 2026-11-09)

```
pointerdown  → paint.begin(session, pointer, brush, color)      continuation per pointerId
pointermove  → paint.feed(session, pointer, samples)            dabs rendered, dirty rect grows
frame        → paint.present(session, dirty, view)              → dom.set_canvas_pixels_rect(canvas, pixels, rect)
pointerup    → paint.end(session, pointer)                      one history entry per stroke
```

- **Latency.** The target is pointer sample to presented pixels within one display frame (§5). Event handlers accumulate samples; the frame handler, scheduled with `dom.request_frame` as in Photos, renders and presents once per frame.
- **History.** One undo entry per stroke, as `lambda.image` §4.3 already specifies. When a stroke ends, its pixels are merged into the layer, as Krita does (§7.1). The history keeps the previous layer value, whose untouched tiles are shared, never a replay of strokes. Redo is supported. History is bounded by a memory budget, and the oldest entries are dropped first.
- **Multiple pointers.** Each `pointerId` has its own continuation.
- **Pen input.** The Lambda DOM adapter exposes the W3C Pointer Events fields `pointerType`, `pressure`, `tangentialPressure`, `tiltX`, `tiltY` and `twist`. A mouse reports pressure 0.5 while a button is down and 0 otherwise, as W3C specifies. Coalesced samples matter for pens, which report at 120–240 Hz. A JS-style `getCoalescedEvents()` allocates one event object per sample, so Lambda instead exposes them as one `f32[N, 7]` array in the stroke sample layout: `dom.pointer_samples(evt)` (name proposed, PT-O8). Radiant captures the samples (PE6). The Lambda DOM adapter owns their script surface (**D9.1.1v4**).
  - *Implemented (M1e).* Native pointer events carry the six fields. A pen reports `pointerType` `"pen"` and `pointerId` 2; the mouse keeps pointer 1, and pointer capture follows the pointer that pressed. An eraser is a pen whose contact is button 5 and buttons bit 32 (Pointer Events 3 §4.1.1.2). `dom.pointer_samples(evt)` returns the samples a move coalesces, oldest first, with the event itself last; a press or release gives one row. Each row is `x, y, pressure, tilt_x, tilt_y, twist, time`, with `x, y` in viewport CSS pixels like `clientX` and `time` in seconds on the clock of `evt.timeStamp`, so the last row's time is `evt.timeStamp / 1000`. Another event with a pointer position gives its own point as one row, and any other event gives no rows. JS sees the same fields; `getCoalescedEvents()` is not provided (PT-O8).
- **Display.** The `<canvas>` is viewport-sized. Zoom and pan are presentation parameters (`view: {scale, offset}`), so canvases larger than the 4096-px upload limit can still be painted. Only the dirty rectangle is converted and uploaded.
- *Implemented (M1f).* The session functions are `paint.session`, `begin`, `feed`, `end`, `cancel`, `frame` (the sketch's `present`, renamed because `present` already converts a layer), `undo`, `redo`, `clear`, `mark`, `color_at` and `in_layer`, plus `adjust_brush` for the palette's size and opacity. The history budget counts the bytes of the tiles each entry keeps (256 MiB by default). Two limits remain:
  - One stroke paints at a time: a press from another pointer first ends the stroke in progress, so the multiple-pointer bullet above is not met yet.
  - The demo's canvas bitmap is the layer, and zoom and pan resize and move it with CSS `left`, `top`, `width` and `height` (never a transform, so rectangle writes keep their region repaint). Canvases are therefore limited to 4096 px per side. The viewport-sized presentation above is still the target for larger canvases (PT-O11).

**The demo** lives in `test/demo/paint/` and runs as `lambda demo paint`. It provides:

- A brush palette that includes imported `.myb` brushes.
- A colour picker that can sample the canvas.
- A media switch: pixel, oil (pigment with impasto) and watercolor (wet).
- Paper selection.
- Layers: paper, paint layers and the wet sheet.
- Undo and redo, zoom and pan.
- Save as PNG or as a `.painting` document.
- An "auto paint" panel that runs the automated workflow on an opened image and shows its progress pass by pass.

### PT11 — Automated conversion (USER, 2026-11-09)

| Function | Effect |
|---|---|
| `paint.analyze(source, options)` | Pure: analysis fields |
| `paint.plan(source, style, options)` | Pure: a `<painting>` stroke document (PT12) |
| `paint.render(painting, options)` | Pure: layers |
| `paint.convert(source, style, options)` | `render(plan(...))` |

Conversion is long-running, so the demo runs it in a worker (**S13.1.3v2**). Planners emit their passes in coarse-to-fine order, and the worker sends a preview after each pass.

| Style | Analysis | Planning | Rendering | Reference |
|---|---|---|---|---|
| **Oil / impasto** | Gaussian pyramid per brush size; colour difference in Lab | Layered coarse-to-fine curved strokes. In each layer, grid cells whose mean error exceeds a threshold start a stroke at their maximum-error point. The stroke follows the isophote (perpendicular to the gradient) until its colour departs from the reference. Stroke order is keyed-random. | Pigment medium; round, flat or bristle tips; thickness gives impasto lighting | Hertzmann 1998 |
| **Impressionist** | Orientation from the smoothed structure tensor; edge map | Jittered grid of short strokes along the orientation field, clipped at edges, with keyed jitter in colour, size and angle. A pointillist variant places dots, optionally with complementary-colour jitter. | Pixel or pigment medium; flat dab tips | Litwinowicz 1997 |
| **Pencil / ink** | Edge tangent flow; FDoG lines; tone map | Contours: link line pixels into polylines, simplify, taper. Tone: hatching streamlines along the ETF or at fixed angles, with density and cross-hatch layers chosen by tone band. | Pixel medium; pencil tip with paper grain, or ink tip with pressure taper | Kang et al. 2007; Winkenbach & Salesin 1994 |
| **Watercolor** | Edge-preserving abstraction; regions from k-means in Lab plus `label`; tonal order of regions | Per region, washes from light to dark; wet-in-wet where neighbouring regions are close in colour; glazes for shadows; dry-brush details along strong edges | Wet medium over the pigment layer; paper texture, granulation, edge darkening | Curtis et al. 1997 |

**Vector input.** An SVG, or a vector layer from `lambda.editor.draw` or `lambda.image`, is both rendered and read as geometry. The rendering supplies the colour target. The geometry supplies exact structure:

- Outlines become contour strokes along their paths.
- Filled shapes become planner regions with exact masks.
- Fill colours, or samples of gradients, become target colours.
- A shape's principal direction or medial axis orients its fill strokes.

Exact masks replace segmentation, which is the noisiest analysis step, so vector input generally plans better than raster input.

### PT12 — A painting stores its pixels; strokes are optional provenance

A saved painting stores its layer pixels, as Krita's `.kra` and OpenRaster do. Loading decodes the layers in one pass and never replays strokes, so load time depends on canvas size and layer count, not on stroke count. A painting is a `<painting>` element with attributes `width`, `height`, `seed` and `render_key`. Its children are:

- `<assets>`: brushes, pigments, papers and tips, the latter as typed arrays.
- `<layer>` elements: format, blend space and tiles. Empty tiles are omitted, and stored tiles are compressed.
- `<preview>`: a flattened sRGB image for thumbnails and immediate display, like `.kra`'s `mergedimage.png`.
- Optionally, `<strokes>`, the stroke log in painting order. It contains `<stroke>` elements, `<dry steps: n>` markers that give wet media time to flow and dry, optional `<pass>` grouping for progressive preview, and each stroke's resolved dab colours (PT16).

Strokes are provenance, not the source of truth for display. They serve:

- re-rendering at another size, or with other brushes or media;
- time-lapse playback;
- editing a planner's output before it is rendered.

`render_key` combines the engine's dab-layout version with a hash of the stroke log, the assets and the render options. When it matches, a replay on the same build reproduces the stored pixels (PT8).

A planner's fresh output is a painting with strokes and no layer pixels until it is rendered.

**File format (USER, 2026-11-09): a ZIP container like `.kra`.** A `.painting` file holds these entries:

| Entry | Contents |
|---|---|
| `mimetype` | `application/x-lambda-painting`, stored first and uncompressed, as ODF, OpenRaster and `.kra` do |
| `painting.mark` | The `<painting>` element, with each layer's tiles referenced by entry name instead of inlined |
| `layers/<id>.tiles` | Each layer's present tiles as little-endian samples with a small tile index; ZIP deflate compresses them |
| `preview.png` | The flattened sRGB image, like `.kra`'s `mergedimage.png` |
| `thumbnail.png` | A small preview for file browsers |
| `strokes.mark` and `strokes/<n>.bin` (optional) | The stroke log, with sample and resolved-colour arrays as binary entries |

Reading and writing use Lambda's native ZIP support from the archive work. No external tool is involved. A rendered painting exports through `lambda.image` as raster layers to PNG or PSD. That join happens in the demo or user script, not in `lambda.paint` (PT13 explains why).

*Implemented (M1g).* `paint.save_painting`, `open_painting`, `export_png`, `painting_tree` and `painting_document` live in `lmd/package/paint/painting.ls`. The current file holds one paint layer:
- **Entries.** `mimetype` comes first, but deflated. The ZIP writer applies one compression method to every entry, so a stored `mimetype` waits on lambda-input ledger LI-004.
- **Document.** `painting.mark` has `version: 1`, `width`, `height`, `seed`, `paper` and an empty `render_key`, then `<assets>` (empty), `<layer id: "paint", format, sample, blend_space, tile, tiles: "layers/paint.tiles">`, `<preview>` and `<thumbnail>`.
- **Tiles.** `layers/<id>.tiles` is the `LPT1` blob written and read by `radiant.paint_tiles_encode` and `radiant.paint_tiles_decode`. A 32-byte header gives version, tile size, the tile grid, channels and the sample kind. Then come the ascending cell indices of present tiles and their samples, little-endian. Decoding rejects a blob that does not match the layer.
- **Previews.** `preview.png` and `thumbnail.png` are encoded through the image system functions (`lambda.sys.image_raster`, `lambda.sys.image_encode`), so `lambda.paint` still does not import `lambda.image`. The same encoder serves `export_png`; PSD export remains a `lambda.image` join. Image rasters hold at most 16 million pixels, so a larger painting gets a scaled `preview.png` (the `<preview>` element gives its size), and `export_png` reports an error for it. A fully painted 4096 × 4096 layer saves and reopens through the raised ZIP quotas (debug build: 3.9 s, 3.1 GB peak).
- **Assets.** `<assets>` holds `<paper kind, seed, size>` when the session's texture was built from a spec, and opening rebuilds it. A texture given as a raw array is not stored.
- **Not yet.** There is no stroke log, no brush or tip assets, and no second layer. Those arrive with the planners and media (M2–M4). A blob above 1 GiB is rejected; that is a 4096 × 4096 rgba f32 layer with every tile painted.

### PT13 — `lambda.paint` replaces `lambda.image`'s brush (USER, 2026-11-09)

`img.paint_stroke` and `img.paint_stroke_step` become wrappers over `lambda.paint`, keeping their public contracts:

- **Brush.** A default round dab brush in the pixel medium with `blend_space: 'srgb'`, which reproduces the current sRGB-encoded mixing.
- **Wash mode.** Stroke coverage accumulates as the current kernel accumulates it, and composites once against the frozen pre-stroke source at the stroke's opacity. This is the existing guarantee that splitting a stroke across frames does not compound opacity (**S9.2.2v2**).
- **Options.** `size`, `hardness`, `spacing`, `flow`, `opacity`, `color` and `mode` keep their names and ranges. `selection` and `origin` become a coverage mask. The 128-million candidate-pixel work bound carries over.
- **Coverage, not pixels (as built, 2026-11-09).** Only the stroke's coverage moves to `lambda.paint`: linear-profile wash dabs accumulate on a u8 coverage layer. `image_edit.cpp` keeps its paint, erase and retouch composite and reads those coverage tiles in place, recompositing only each batch's changed rectangle onto the previous preview. The Raster's samples are never converted. The continuation still returns a gray-Raster `coverage`. Its `distance` field is now the stroke's arc length, its `next` field is the next dab index, and `layer` carries the coverage tiles.

`edit_dab`, `edit_stroke` and `edit_stroke_step` in `lambda/runtime/image_edit.cpp` are deleted once the Photos goldens and replays pass on the new path. `paint_fill`, `gradient`, selections and the wand stay in `lambda.image`, because they are not brush work.

**Dependency direction.** `lambda.image` imports `lambda.paint`. `lambda.paint` never imports `lambda.image`, which avoids a package cycle. The two exchange pixels as typed arrays (`raster.pixels`, `img.from_array`).

### PT14 — `procart.watercolor` stays the reference

The native wet step is a fresh C++ implementation of the Curtis model. It follows `procart.watercolor`'s Lambda port, which derives from Washes (MIT), so the Washes notice travels with any derived code. The Lambda implementation remains the oracle: a parity test runs both on the same inputs within the tolerance tier (PG7.3). `procart.watercolor` may call the native step when it is available and fall back to Lambda otherwise (PG7's fallback pattern), so the procart demo becomes faster with no API change. PG18's open ruling is unaffected (PT-O6).

### PT15 — Packaging and the native bridge

The entry module is `lmd/package/paint.ls`, imported as `import paint: lambda.paint` (**D9.3.4**). Modules live under `lmd/package/paint/`:

| Module | Contents |
|---|---|
| `brush.ls`, `myb.ls` | Brush settings, input curves, defaults, `.myb` import |
| `dynamics.ls` | Tracking, spacing, input evaluation, dab-table construction |
| `stroke.ls` | Stroke elements, continuations, synthetic pen samples |
| `layer.ls` | Tiled layers, formats, tile forking, flatten, typed-array import and export |
| `media.ls` | Pigments, papers, wet-sheet configuration, presets |
| `session.ls` | Interactive sessions, history, dirty rectangles |
| `present.ls` | Presentation views and display conversion |
| `analyze.ls` | Pyramids, fields, edges, segmentation |
| `plan/oil.ls`, `plan/impression.ls`, `plan/sketch.ls`, `plan/watercolor.ls` | Style planners |
| `vector.ls` | Geometry to strokes and regions |
| `document.ls` | `<painting>` construction, validation, replay |

The native bridge is a small set of batch-shaped functions in the existing `radiant` host module, implemented in Lambda's Radiant adapter (`lambda/module/radiant/radiant_paint_adapter.cpp`). M0 provides `radiant.paint_dabs`, `paint_wash`, `paint_import`, `paint_present` and `paint_capabilities`. Later milestones add `paint_bristles`, `paint_wet_step`, `paint_wet_deposit`, `paint_sample` and `paint_score`. `lambda.paint` imports `radiant` and wraps them.

*Why not `lambda.sys.paint_*`:* system functions are enumerated in lambda-input's `lambda.h`. Lambda work must not edit lambda-input sources (AGENTS.md project boundaries), and the adapter already links only where Radiant links. They follow the system-function contracts all the same (**D7.6.1–D7.6.2**), and unknown options fail (**S17.8.1**).

The engine is available wherever Radiant is linked: `lambda.exe` and `lambda-headless` (**D9.2.1v2** profiles B and C). `lambda-cli` omits Radiant (**D9.2.2**), and the user accepted on 2026-11-09 that it has no paint engine. There, `paint.capabilities()` reports that the engine is absent and rendering returns an explicit error (**S7.4.5v2**). Planning, analysis and reading stored paintings still work there, because they are Lambda.

### PT16 — Batched replay (proposed)

Replay is the exception (PT12), but planners, re-rendering and time-lapse need it to be fast.

1. **One call per pass.** Lambda evaluates the dynamics of every stroke in a pass into one dab table, vectorized across the batch. The engine renders that table in one call (PT7), so the boundary cost and tile forking are paid once.
2. **Per-pixel order, not global order.** When dabs do not read the canvas, a pixel's result depends only on the ordered sequence of dabs that cover it. The engine bins dabs by tile, keeping stroke order within each bin, and renders tiles in parallel. The result is bit-identical to sequential rendering ([PE4](../../radiant/vibe/Radiant_Design_Paint_Engine.md#pe4--dab-kernel)).
3. **Resolved colours make smudge replayable.** Smudge reads pixels that earlier dabs wrote, which serializes it. When a stroke is first rendered, interactively or by a planner pass, the engine returns each dab's resolved colour after smudge and colour shifts, and the stroke log keeps it. A replay with resolved colours treats those dabs as non-reading, so they parallelize and reproduce the original exactly on the same build. Without resolved colours, for example with a new brush or at another resolution, smudging strokes render sequentially in their original order.
4. **Wet media run their simulation.** Time is part of the medium. A wet pass is one call that contains its strokes and drying steps. Each step is data-parallel over the active window.
5. **Level of detail for previews.** A preview replays at reduced scale, with positions and radii scaled, and then refines at full resolution in the background, as Krita's Instant Preview does (§7.1). Interactive sessions use the same idea for very large brushes.
6. **Mask reuse.** Runs of dabs with equal quantized masks share one mask (PE4).

*Scale:* a painting of 10⁵ strokes makes about 5 × 10⁶ dabs and 10⁹ covered pixels. That is about 5 s on one core at the §5 dab throughput, under 1 s across 8 cores, and less on the GPU (PT17). Decoding stored layers takes tens of milliseconds, which is why loading never replays (PT12).

### PT17 — GPU backend (proposed)

The GPU can paint strokes, and a GPU backend is proposed as an accelerator behind the same dab tables. Its design is [PE13](../../radiant/vibe/Radiant_Design_Paint_Engine.md#pe13--gpu-backend-proposed); the package-visible rules are:

- **What runs well on the GPU.** Each canvas-independent dab becomes one instanced quad. Graphics APIs blend overlapping fragments in submission order, so one instanced draw reproduces the sequential per-pixel order. Pigment deposit is additive (PT4), so its order does not matter. Bristle segments are thin quads. The wet step is fragment-shader passes over float textures, which Radiant's `gl_core` already uses for simulation state. Smudge runs on the GPU only with resolved colours (PT16); otherwise it stays on the CPU.
- **Typed arrays stay authoritative (PT3).** GPU textures are caches. Dirty tiles are read back into the layer at commit points (stroke end, pass end, save), never per dab, and readbacks are batched because each one stalls the pipeline.
- **The CPU engine is the reference (USER, 2026-11-09).** It runs everywhere, including `lambda-headless`, which has no OpenGL (**D9.2.1v2** profile C). It is also bit-reproducible on one build. GPU output is accepted within tolerance of the CPU result, because devices and drivers round differently. It is never the authority for stored pixels unless a render opts in. Goldens and parity tests run on the CPU.
- **Selection.** `backend: 'auto' | 'cpu' | 'gpu'`. `'auto'` uses the GPU when a context supports blending on float targets and the batch is large enough to repay its uploads.

## 4. API sketch

This sketch is illustrative; names are proposed.

```lambda
import paint: lambda.paint

// a brush: base values plus per-input curves (libmypaint's model, PT6)
let ink = paint.brush({
    radius_log: {base: 1.2, pressure: [[0.0, -0.6], [1.0, 0.4]]},
    hardness: 0.9,
    opaque: {base: 0.8, pressure: [[0.0, 0.0], [1.0, 0.2]]},
    slow_tracking: 2.0
})^

// automated conversion (PT11)
pn main() {
    let pencil = paint.brush_from_myb(input("brushes/pencil.myb", 'json'))^   // MyPaint brush
    let photo = load("garden.jpg")^                     // (H, W, 3) u8
    let plan = paint.plan(photo, 'oil', {seed: 7})^      // <painting>, pure
    let layers = paint.render(plan)^                     // tiled f32 layers, pure
    save(paint.flatten(layers)^, "garden_oil.png")^      // (H, W, 4) u8 sRGB
}
```

In the demo's handlers (PT10), with `session` held as Photos holds its state:

```lambda
on pointermove(evt) {
    if ((not paint.drawing(session, evt.pointerId))^) { return 'pass' }
    session = paint.feed(session, evt.pointerId, dom.pointer_samples(evt))^
    if ((session.token == 0)^) { session = {*:session, token: dom.request_frame(owner_of(evt), "paint_frame")} }
    return 'prevent-default'
}
```

## 5. Budgets

These are proposed acceptance targets on release builds (AGENTS rule 10), not measured claims.

| Path | Budget |
|---|---|
| Interactive: pointer sample → presented pixels | ≤ 16.7 ms with 120 Hz pen input, a 2048×1536 canvas and a 60-px pixel-medium brush |
| Engine dab throughput | ≥ 200 M covered pixels/s (pixel medium); ≥ 50 M/s (pigment, B = 3) |
| Lambda dynamics | ≤ 2 µs per dab, amortized over a batch |
| Wet step | ≤ 8 ms per step for a 512×512 active window |
| Oil conversion, 1 MP photo, 4 brush sizes | ≤ 10 s |
| Impressionist or sketch conversion, 1 MP | ≤ 5 s |
| Watercolor conversion, 1 MP | ≤ 30 s |
| Load a saved 4096×4096 painting with 4 layers | ≤ 0.5 s, with no replay |
| CPU replay of 10⁵ strokes at 1 MP | ≤ 2 s on 8 cores |
| GPU replay of the same painting | ≤ 0.5 s, including readback |

## 6. Milestones and acceptance

These are finite delivery boundaries. A full implementation plan belongs in `vibe/impl/` when implementation is requested.

| Milestone | Deliverable | Acceptance |
|---|---|---|
| **M0 — Core** | Engine skeleton, bridge, tiled layers, pixel medium with procedural dab tips (hardness, anti-aliasing, ellipse), build-up and wash modes, `rgba` present; `lambda.image` brush migrated and old kernel deleted (PT13) | Photos goldens and replays pass unchanged; analytic dab-coverage tests pass; untouched tiles stay shared after a stroke |
| **M1 — Interactive** | `lambda demo paint`; system procedures with `var` parameters and in-place writes, ratified into the formal design (PT7); macOS pen pressure, tilt and coalesced samples; libmypaint dynamics; `.myb` import; smudge buckets; blend modes; history; dirty-rect upload; bitmap tips and paper grain; saving and loading paintings as stored pixels (PT12) | Latency budget met; a set of MyPaint brushes renders a scripted stroke set within tolerance of reference captures from MyPaint; replaying a recorded session reproduces its pixels exactly on one build |
| **M2 — Pigment and bristle** | Pigment medium (KM, B = 3), thickness and impasto lighting, bristle tips over both media | Blue over yellow mixes toward green; KM identity tests pass; bristle depletion is visible along a long stroke; budgets met |
| **M3 — Wet media** | Native wet step and deposit, stepped in place for live painting (PT7) | Parity with `procart.watercolor` within tolerance; wet budget met; the procart watercolor demo runs on the native step unchanged |
| **M4 — Auto: oil and impressionist** | `analyze`, oil and impressionist planners, stroke logs, worker conversion with per-pass preview; batched replay with tile binning, tile-parallel rendering, resolved colours and level-of-detail previews (PT16) | Equal plans for equal seeds; budgets met; plans re-render with another brush without re-planning; tile-parallel replay is bit-identical to sequential replay; a replay with resolved colours reproduces the stored pixels |
| **M5 — Auto: sketch, watercolor, vector** | Pencil/ink and watercolor planners; vector input | SVG input plans contours along its paths and fills inside exact masks; budgets met |
| **M6 — GPU backend** | GPU replay of dabs, pigment deposit, bristles and the wet step; batched readback at commit points (PT17) | GPU output is within tolerance of the CPU reference on the M0–M5 fixtures; GPU replay budget met; `'auto'` falls back to the CPU without a GL context |

Use focused checks during each increment. Run `make test-lambda-baseline` at milestone boundaries, and Radiant's baseline when its event or canvas integration changes. Every new golden-driven `.ls` fixture gets its `.txt` golden (AGENTS rule 8). Reference captures and scratch output stay under `./temp/`.

## 7. Prior art

| Source | What it offers | What we take |
|---|---|---|
| [libmypaint](https://github.com/mypaint/libmypaint) (ISC) | Brush/surface split; settings × inputs model; `.myb` format; 64-px tiles; smudge buckets | Architecture (PT1), brush model and vocabulary (PT6), `.myb` import, tile size. Reference only; not vendored |
| [Krita](https://invent.kde.org/graphics/krita) (GPL) | Paint-op architecture; dab cache; wash and build-up modes; color-smudge and bristle engines | Concepts only: wash mode, bristle tips, dab-cache idea (PE). No code |
| painty | Stroke-based rendering with Kubelka–Munk pigments | Algorithm reference for oil planning and KM mixing |
| G'MIC | Large pixel-filter collection | Out of scope: filters approximate paint and lack strokes |
| Washes (MIT) | Curtis watercolor simulation in JS | Already ported as `procart.watercolor`; the oracle for PT14 |
| Haeberli 1990, *Paint by Numbers*; Hertzmann 2003, *A Survey of Stroke-Based Rendering* | Foundations of stroke-based rendering | Framing of PT2 and PT11 |
| Hertzmann 1998, *Painterly Rendering with Curved Brush Strokes of Multiple Sizes* | Layered coarse-to-fine curved strokes | Oil planner |
| Litwinowicz 1997, *Processing Images and Video for an Impressionist Effect* | Oriented, edge-clipped short strokes | Impressionist planner |
| Kang, Lee and Chui 2007, *Coherent Line Drawing* | ETF and FDoG | Sketch analysis |
| Winkenbach and Salesin 1994, *Computer-Generated Pen-and-Ink Illustration* | Stroke textures, tone by hatching | Hatching planner |
| Curtis et al. 1997, *Computer-Generated Watercolor* | Shallow-water pigment flow, KM glazing | Wet medium and watercolor planner |
| Baxter et al. 2004, *IMPaSTo* | Interactive paint model with thickness | Pigment medium and impasto |
| Strassmann 1986, *Hairy Brushes* | Per-bristle stroke model | Bristle tips |
| Achanta et al. 2012, *SLIC Superpixels* | Fast region segmentation | Watercolor regions, native candidate |

Licences are recorded only to bound reuse. Every native kernel is written fresh from published algorithms.

### 7.1 How Krita handles large paintings

Krita keeps no strokes. Each stroke is rasterized into its layer's tiled pixel data as it is painted, so cost grows with canvas size, layer count and colour depth, not with stroke count. Its techniques, and where this proposal adopts each one:

| Krita technique | Effect | Here |
|---|---|---|
| Tiled paint devices with 64-px tiles | Memory follows the painted area | PT4 |
| Each stroke merged into its layer's pixels as it is painted | Nothing to replay on load or redraw | PT10, PT12 |
| Undo by per-tile snapshots; a bounded undo stack whose older states can be swapped out | Undo costs only touched tiles | PT4, PT10 |
| Tile compression, and a swap file beyond a configured memory limit | Documents larger than RAM | PT-O11 |
| `.kra`: a ZIP holding `maindoc.xml`, each layer's LZF-compressed tiles, a flattened `mergedimage.png` and a preview | Fast load and thumbnails, and interchange without re-merging | PT12 |
| A cached projection of the composited layers, refreshed by dirty regions | Display cost follows the change | PE7 |
| Multithreaded pixel brush: dabs rendered in parallel and applied in order | Throughput without reordering | PE4, PE12 |
| Instant Preview: paint first on a scaled-down copy, finish the full-resolution stroke in the background | Big brushes on big canvases stay responsive | PT16 |

Sources: [Krita Instant Preview](https://docs.krita.org/Instant_Preview), [Krita performance settings](https://docs.krita.org/Performance_Settings), [Krita `.kra` format](https://docs.krita.org/general_concepts/file_formats/file_kra.html), [multithreaded pixel brush commit](https://mail.kde.org/pipermail/kimageshop/2017-September/014999.html).

## 8. Open items

- **PT-O2 — Native analysis kernel placement.** Context-free generic kernels (ETF, bilateral, SLIC) could live in lambda-input's `lib/` with Lambda adapters, or directly in `lambda-vector.cpp` with the existing toolkit. Decide when the first kernel passes the gate.
- **PT-O4 — u16 working kind.** u16 halves memory for very large canvases at the cost of a second kernel instantiation. Defer until measurements show the need.
- **PT-O5 — Default blend space.** Linear light is physically better for alpha build-up. `.myb` brushes were tuned for sRGB blending, so parity with MyPaint needs `'srgb'`. The proposed default is linear, with `.myb` imports setting `'srgb'`.
- **PT-O6 — PG18 interplay.** With a native wet step, the Lambda `pn` sheet becomes a reference implementation. Does PG18 get ratified as "a reference may be procedural"?
- **PT-O8 — Coalesced-sample API.** `dom.pointer_samples(evt)` returning `f32[N, 7]`, or a W3C-shaped `getCoalescedEvents()`. M1e implements `dom.pointer_samples` under the proposed name; the name and whether JS also gets `getCoalescedEvents()` remain open.
- **PT-O11 — Very large canvases.** Compressing tiles in memory, and spilling cold tiles and old undo states to disk beyond a memory budget, as Krita does. Deferred until canvases beyond about 8K × 8K are a goal.

## Appendix A. Implementation notes

- **Bridge.** System functions and `var`-parameter system procedures in the Lambda runtime own all `ArrayNum` handling: element-kind and shape validation (**S11.4.1v5**), tile forking and un-sharing (PT7), view construction and error mapping. Values the engine reads are rooted for the call through `RootFrame`/`Rooted` (**D6.3.3**). No collection can occur during an engine call, because the engine allocates no GC values, so storage pointers stay valid for the call and never outlive it (**D5.5.2**).
- **Engine boundary.** The engine includes only lambda-input `lib/` headers. Radiant's include and link checks enforce this (**D9.1.3v2**).
- **Build.** Build declarations go through `build_lambda_config.json` and Radiant's `build_targets.json`. Generated Lua is never edited.
- **Tests.** Package goldens go under `test/lambda/paint/`, the demo under `test/demo/paint/`, and engine unit tests in Radiant's owner-local GTest suites. Add the new suites to [`test/README.md`](../test/README.md).
- **Reuse.** Use the keyed RNG and noise from `procart`, colour helpers from `lmd/package/color.ls`, and the typed-array toolkit. Do not add parallel copies (AGENTS rule 13).

## Appendix S. Superseded rulings

The second 2026-11-09 round replaced these first-draft positions. The current text is in the sections named.

- ~~**PT7 (first draft).** The bridge always forks touched tiles; native writes through a `var` borrow wait on PT-O1.~~ The user ruled in-place native writes; PT7 now defines both forms.
- ~~**PT-O1.** Native writes through a `var` borrow: (a) a general registration form for system procedures with `var` parameters, (b) the wet step stays in Lambda `pn` passes, or (c) a native sheet resource; needs a USER ruling before M3.~~ Resolved by the user as in-place writes; option (a) is the mechanism (PT7).
- ~~**PT12 (first draft).** A painting is its stroke list; pixels are derived by replaying the strokes.~~ A painting now stores its layer pixels, and the stroke log is optional provenance (PT12).
- ~~**PT-O9.** Accept that `lambda-cli` cannot render, or move the engine to lambda-input.~~ The user accepted that `lambda-cli` has no paint engine (PT15).
- ~~**PT-O3.** Bridge naming: `lambda.sys.paint_*`, or a built-in module that must not share the package path.~~ Settled during implementation by the lambda-input boundary: the bridge is `radiant.paint_*` in Lambda's Radiant adapter (PT15).
- ~~**PT-O7.** Painting storage format: a ZIP container like `.kra` and OpenRaster, or a single Lambda document with binary payloads.~~ The user chose a ZIP container like `.kra` (PT12).
- ~~**PT-O10.** GPU authority: the CPU engine as reference with GPU output accepted within tolerance, or GPU output as authoritative pixels.~~ The user ruled that the CPU engine is the reference (PT17).
- ~~**Engine location `../radiant/paint/`** at the Radiant repository root.~~ The user placed it at `radiant/radiant/paint/` (PE2).
