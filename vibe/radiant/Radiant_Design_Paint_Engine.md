# Radiant Paint Engine — Proposal

> **Status:** proposal, 2026-11-09, requested by the user as the native half of the [Lambda Paint Package proposal](../Lambda_Pkg_Paint.md) (ledger PT#). The user's rulings recorded there apply here: the engine replaces `lambda.image`'s native round-dab brush (PT13); its media scope covers dabs with smudge and blending, pigment mixing, wet media and bristle brushes (PT5); and Radiant adds pen pressure and tilt for a new `lambda demo paint` (PT10). A second round on 2026-11-09 ruled that kernels **write in place** into storage lent through `var` borrows (PT7), placed the sources at **`radiant/paint/`** (PE2), and accepted that `lambda-cli`, which omits Radiant, has no paint engine. Batched tile-parallel replay (PE4, PE12) and a GPU backend (PE13) answer that round's questions and are proposals. Within PE13, the user ruled that the CPU kernels are the pixel reference and that GPU output is accepted within tolerance.
>
> **Implementation:** M0 landed on 2026-11-09, uncommitted. It comprises `radiant/paint/paint.hpp` and the `paint_view.cpp`, `paint_dab.cpp` and `paint_present.cpp` kernels: views, the bounds query, procedural dabs in both profiles, build-up, wash, import and presentation. `test/test_paint_gtest.cpp` covers them, and its 11 cases pass. M1's engine part landed the same day, also uncommitted, in `paint_smudge.cpp` and extensions to the other kernels: smudge buckets, resolved colours, bitmap tips, paper grain, the six blend modes, colorize, posterize, colour shifts and `paint_sample`. `test_paint_gtest` now has 17 passing cases. The PE7 dirty-rectangle canvas write landed in `canvas_2d.cpp`. All of this was committed on 2026-11-09. PE6 pen capture on macOS followed on 2026-11-10; its note closes PE6. M2's engine part landed on 2026-11-12, uncommitted: the pigment medium (PE8) in `paint_pigment.cpp`, impasto lighting (PE7 step 5) and bristle trails (PE10) in `paint_bristle.cpp`. `test_paint_gtest` has 30 passing cases plus a disabled release throughput benchmark. Progress is tracked in the Lambda [implementation record](../impl/Lambda_Impl_Paint.md). Everything not marked USER is proposed; this note and the record say what is implemented.
>
> **Scope:** a minimal, context-free C++ library of per-pixel painting kernels under `radiant/paint/` in this repository, an optional GPU backend for the same work, and the two Radiant services painting needs: pen input capture and dirty-rectangle canvas upload. Brushes, dynamics, strokes, planning, analysis, sessions and file formats are Lambda's and are out of scope here (PT1, PT9).
>
> **Spec linkage:** **D9.1.1v4** (Radiant owns native event collection and rendering; Lambda owns event handling; Radiant depends on lambda-input, not on the Lambda runtime); **D9.1.3v2** (no runtime imports; include and link checks); **D5.5.1, D5.5.2** (allocation substrate; Radiant never keeps a GC pointer); **D6.3.3** (rooting stays in the Lambda bridge); **S9.1.3v3, D5.4.1–D5.4.6** (`var` borrows and copy-on-write, which govern in-place writes; PT7). See [Formal Design](../../doc/Lambda_Formal_Design.md). This proposal changes no formal ruling.
>
> **Authority:** engine design ledger PE1–PE13 and open items PE-O2–PE-O5 and PE-O7–PE-O8. PE-O1 and PE-O6 are resolved or superseded; see [Appendix S](#appendix-s-superseded-rulings). It shares the paint area with PT# in the Lambda record. Radiant-owned defects found while implementing go to the [Radiant Issue Ledger](../Radiant_Issue_Ledger.md).

## 0. Summary

- **A kernel library, not a paint program.** The engine runs only the loops that execute once per pixel: dab masks and compositing with smudge, pigment (Kubelka–Munk) deposit and display, the wet-media step, bristle trails, presentation and stroke scoring. Everything that runs once per sample, dab or stroke is Lambda (PT1).
- **libmypaint's surface, written fresh.** The engine plays the role of libmypaint's `MyPaintSurface` (`draw_dab`, `get_color`), batched over a dab table, plus the media libmypaint lacks. libmypaint, Krita and the papers are references only.
- **Views over caller storage.** Every input and output is a view over typed-array storage that the Lambda bridge owns and roots. The engine allocates no caller-visible memory, keeps no pointer past a call (**D5.5.2**), and includes nothing from the Lambda runtime (**D9.1.3v2**).
- **Two Radiant services.** First, pen capture: pressure, tilt, twist, pointer type and coalesced high-rate samples, macOS first. Second, a dirty-rectangle upload into the retained `<canvas>`, updating only that part of the GPU texture.
- **Batched and parallel replay.** A pass is one call. Canvas-independent dabs are binned by tile and rendered tile-parallel, bit-identical to sequential rendering (PE4, PE12).
- **An optional GPU backend.** It is built on `gl_core` and renders dabs as instanced quads in submission order, deposits pigment additively and runs the wet step as shader passes. The CPU kernels remain the reference (PE13).

## 1. Engine design

### PE1 — Role and boundary

The engine's inputs are storage views, parameter structs and a dab table. Its outputs are pixels written into views it was given, plus small result structs: dirty bounds, final smudge or bristle state, work used and a status code. It knows nothing about Lambda values, the GC, the DOM, layout or windows.

| In the engine | Not in the engine |
|---|---|
| Dab masks (procedural and bitmap), paper grain | Brush settings, input curves, tracking, spacing (Lambda dynamics) |
| Compositing modes; build-up and wash; smudge buckets | Stroke construction, continuations, history |
| Pigment deposit, pickup, drying and KM display | Pigment definitions and palettes |
| Wet-media step and deposit | Wet scheduling and active-window policy |
| Bristle trails | Bristle placement (keyed RNG) |
| Presentation of a rectangle; candidate-stroke scoring | Image analysis and stroke planning (Lambda and the typed-array toolkit, PT9) |

The engine belongs in Radiant because Radiant owns rendering and native event collection (**D9.1.1v4**), the presentation kernel feeds Radiant's retained canvas, and the user placed it here.

### PE2 — Placement and build (USER, 2026-11-09)

- **Sources.** `radiant/paint/`, beside `radiant/scene3d/` (USER, 2026-11-09). `radiant/paint/paint.hpp` is the public header. The implementation is split by kernel family: `paint_view.cpp`, `paint_dab.cpp`, `paint_smudge.cpp`, `paint_pigment.cpp`, `paint_wet.cpp`, `paint_bristle.cpp`, `paint_present.cpp` and `paint_score.cpp`. The GPU backend (PE13) adds `paint_gpu.cpp` and its shaders.
- **Build.** Add `radiant/paint/*.cpp` to the `radiant` target's `source_patterns` in `build_targets.json`, as `radiant/scene3d/*.cpp` is listed. The engine compiles into `libradiant.a` rather than a separate archive.
- **Includes.** The CPU kernels include only lambda-input `lib/` headers (memory, logging, checked arithmetic) and their own headers. An include check enforces this, so the kernels stay testable without Radiant's view, DOM or event code. Only `paint_gpu.cpp` may include `gl_core.hpp`.
- **Tests.** An owner-local `test/test_paint_gtest.cpp` joins `make run-radiant-baseline`.

### PE3 — Views, not owners

```c
typedef enum PaintElem { PAINT_U8, PAINT_F32 } PaintElem;      // u16 deferred (PT-O4)

typedef struct PaintView {          // one 2-D or 3-D typed-array storage window
    void* data;
    PaintElem elem;
    int32_t height, width, channels;
    int64_t row_stride, pixel_stride;   // in elements; channels are contiguous
} PaintView;

typedef struct PaintTiles {         // a sparse tiled layer (PT4)
    PaintView* tiles;               // tiles_x * tiles_y; data == NULL is an empty tile
    int32_t tiles_x, tiles_y, tile_size;
    uint32_t format;                // PAINT_FMT_RGBA, _PIGMENT, _COVERAGE
    uint32_t bands;                 // B for the pigment format
} PaintTiles;
```

Writes use a two-phase protocol, so the bridge can preserve value semantics:

1. **`paint_dab_bounds(dabs, params) → {write tiles, read tiles}`** is a pure query. Read tiles add smudge-pickup and bristle-spread margins.
2. **The bridge** makes every write tile unique. A tile is copied only when it is shared; otherwise it is written in place (PT7, USER 2026-11-09). The bridge allocates zeroed tiles for empty write tiles and passes the resulting views. Pure renders get fresh tiles that the call owns. Interactive and wet procedures reach their tiles through a `var` borrow.
3. **`paint_dabs(...)`** writes only into the write tiles and reads only the read tiles. A write outside the declared set is a defect, and the engine reports it as an error rather than performing it.

The bridge validates element kinds and shapes against Lambda contracts. The engine validates every view again (PE11), because it is a C ABI and must not trust its caller.

### PE4 — Dab kernel

`paint_dabs(tiles, dabs, params, smudge_state)` renders a dab table in order. Each row follows the versioned layout in [Appendix A](#appendix-a-dab-table-layout-v1-draft).

- **Mask.** Procedural masks follow libmypaint: radius, two-segment hardness falloff, elliptical aspect ratio and angle, and analytic anti-aliasing over the edge. Bitmap tips (`f32[h, w]` from the caller's tip set) are sampled bilinearly under the dab's scale, rotation and aspect.
- **Paper grain.** Optionally, the canvas-space paper height modulates coverage, so pencil and dry-brush strokes skip the paper's valleys.
- **Modes.** Normal (premultiplied source-over), erase, lock-alpha, colorize and posterize, plus blend modes multiply, screen, overlay, darken, lighten and add. Each mode is a separate straight-line loop selected outside the pixel loop (PE12).
- **Build-up and wash.** Build-up composites every dab directly, as libmypaint does. Wash, from Krita's painting modes, accumulates per-stroke coverage into a `coverage` tile set as `c ← 1 − (1 − c)(1 − mask·flow)`, as the current `lambda.image` kernel does. `paint_wash_composite(source, coverage, color, opacity) → destination` then composites once against the frozen pre-stroke source. This preserves `img.paint_stroke_step`'s guarantee that splitting a stroke across frames does not compound opacity (PT13).
- **Smudge.** K buckets (`f32[K, 4]`: premultiplied colour and alpha), as in libmypaint 2. For a dab with smudge enabled, the engine samples the canvas under the pickup footprint (PE5) and mixes the result into the selected bucket according to `smudge_length`. The dab colour is then the brush colour mixed with the bucket colour by `smudge`, and the dab's HSV and HSL shifts apply after that mixing. The buckets are returned to the caller, so a stroke may span calls.
- **Resolved colours.** On request, `paint_dabs` also writes each dab's resolved colour after smudge and colour shifts to an output table: `f32[N, 3]` for `rgba`, or `f32[N, 2B]` K and S for `pigment`. Given such a table as input, it uses those colours and skips pickup, so the replay of a smudging stroke becomes canvas-independent (PT16).
- **Tile binning.** For a canvas-independent run of dabs, the kernel bins dabs by the tiles their masks cover, keeping table order within each bin, and renders bins independently. Each pixel still receives the same operations in the same order with the same arithmetic, so the result is bit-identical to sequential rendering. A run containing pickup without resolved colours renders sequentially, because pickup footprints cross tile borders.
- **Mask reuse.** Following Krita's dab-cache idea, consecutive dabs with equal quantized radius, hardness, aspect, angle and sub-pixel phase may reuse one mask. This is added only if profiling shows mask generation matters (PE12).

### PE5 — Sampling and scoring

- **`paint_sample(tiles, x, y, radius, aspect, angle)`** returns the mask-weighted premultiplied average under a footprint, with libmypaint's `get_color` semantics. M1 implements the round footprint, `paint_sample(tiles, x, y, radius)`, with libmypaint's 0.5 sampling hardness. In pigment layers it returns K, S and thickness. The smudge loop, the colour picker and planners use it.
- **`paint_score(target, tiles, dabs, groups, out)`** evaluates candidate strokes without committing them. For each group of dabs (one candidate stroke), it renders into a scratch copy of the group's footprint and writes the error change `Σ w·|target − after|² − Σ w·|target − before|²` to `out`. Planners use it to accept or reject strokes, in the style of Hertzmann's *Paint by Relaxation* (2001). The footprint is bounded, and scratch memory comes from a per-call arena released before return.

### PE6 — Pen input capture

Today `MousePositionEvent` carries a position, movement deltas and a timestamp. GLFW, the platform layer, reports no tablet axes, and Radiant has no pressure, tilt, pointer type or coalesced samples.

**Event fields.** Pointer motion and button events gain the following fields, named after W3C Pointer Events:

| Field | Range |
|---|---|
| `pointer_type` | mouse, pen, eraser, touch |
| `pressure` | 0–1 |
| `tangential_pressure` | −1–1 |
| `tilt_x`, `tilt_y` | −90° to 90° |
| `twist` | 0–359° |
| `pointer_id` | Stable for a pen while in proximity |

A mouse reports pressure 0.5 while a button is down and 0 otherwise, with zero tilt, as W3C specifies.

**Coalesced samples.** The native samples that arrive between two dispatched move events for one pointer are kept in order, each with its own position, axes and timestamp, in a bounded per-pointer buffer attached to the dispatched event. The buffer holds 256 samples; on overflow the oldest are dropped and a flag is set. Dispatch frequency does not change.

**macOS first.** An `NSEvent` local monitor for mouse-moved, mouse-dragged, tablet-point and tablet-proximity events is installed beside GLFW. It follows the existing `*_mac.mm` plus `*_mac_static.cpp` pattern used by clipboard and IME. It reads `pressure`, `tilt`, `rotation` and `tangentialPressure`, and from proximity events `pointingDeviceType` (pen, eraser, cursor) and `uniqueID`. It stamps samples on `radiant_platform_time` and feeds event collection. GLFW remains the source of every other event. Windows (`WM_POINTER`) and Linux (XInput2 or libinput tablet axes) follow later (PE-O4).

**Ownership.** Radiant collects the samples. The Lambda DOM adapter exposes them to scripts as W3C fields and as the `f32[N, 7]` coalesced-sample array (**D9.1.1v4**; PT10, PT-O8).

**Implementation (M1e).**

- *Event fields.* `MousePositionEvent` in `event.hpp` gains `pointer_type` (`RdtPointerType`: mouse, pen, eraser, touch), `pointer_id`, `pressure`, `tangential_pressure`, `tilt_x`, `tilt_y`, `twist`, and `samples`, a pointer to the `RdtPointerSamples` the move coalesces. A zeroed event is a mouse, so every existing producer is unchanged. Radiant keeps a pen's raw pressure; the DOM adapter derives a mouse's W3C pressure from its buttons.
- *Coalescing.* `RdtInputBatch` (`input_batch.hpp`) appends every submitted move to its 256-sample buffer and hands a copy to the dispatched move, so a dispatch that reenters the host cannot see the next run. A move from a different device dispatches the pending run first, and overflow drops the oldest sample and sets `dropped`. The dispatched event is the run's last sample.
- *macOS capture.* `pen_mac.mm` (built through `pen_mac_static.cpp`; other platforms get a mouse-only fallback) is called from the GLFW cursor and button callbacks. It reads the axes of `NSApp.currentEvent` when that event is a tablet point. A local monitor tracks tablet proximity, the only source of the device kind, and turns tablet points that carry no mouse motion (pressure or tilt changes while the pen rests) into moves. While a pen is in proximity, AppKit's mouse coalescing is off, so every sample reaches the batch. A sample's AppKit timestamp is moved onto `radiant_platform_time`, so queued samples keep their spacing.
- *Axis conventions.* AppKit's tilt is −1..1 with y positive away from the user, so `tilt_y` is negated to W3C's positive-toward-the-user. AppKit's rotation is counterclockwise, so `twist` is `360 − rotation` modulo 360. These signs follow the AppKit documentation; they have not been checked against a physical tablet yet.
- *Simulation.* `event_sim` accepts `pointerType` (`mouse`, `pen` or `eraser`), `pressure`, `tiltX`, `tiltY`, `twist` and `tangentialPressure` on `mouse_move`, `mouse_down` and `mouse_up`. A `mouse_move` may list `samples`, rows of `dx, dy, pressure, tiltX, tiltY, twist, t_ms` relative to its point. They run through an `RdtInputBatch`, so the replay exercises the window's coalescing path.
- *Tests.* `test/test_radiant_pointer_samples_gtest.cpp` checks coalescing, device and button flushes, and overflow. Lambda's `test/ui/paint/pen_input.json` checks the script surface end to end.

### PE7 — Presentation and dirty-rectangle upload

**The `paint_present` kernel** composites a destination rectangle at a view scale and offset, in this order:

1. Paper colour and tiled texture.
2. Pigment layers: KM reflectance of the wet film over the substrate, converted from bands to linear RGB by a B×3 matrix.
3. `rgba` layers, in order, with opacity and blend mode.
4. An optional wet overlay showing suspended pigment.
5. Impasto lighting from relief and film thickness: normals by central differences, then a diffuse term and a specular term for a given light.

The result is converted to sRGB u8 with straight alpha, which is what the retained canvas upload accepts. Zoomed-out views downscale with a box filter over the covered source pixels. Presentation is pure: it reads layers and writes only the output view.

**Dirty-rectangle upload.** The retained canvas upload replaces the whole bitmap, and every canvas change used to repaint the whole page. Painting needs a write that leaves the other pixels unchanged and repaints only what changed. The existing size limits and the no-retained-borrow rule (**D5.5.2**) stay. Radiant owns the surface update; the Lambda DOM adapter owns the rectangle argument.

M1 implements this as `dom.set_canvas_pixels_rect(canvas, pixels, [x0, y0, x1, y1])`, a separate operation because DOM operations have a fixed arity, over the canvas engine operation `write_pixels`. The renderer notes, each time the window's own recording draws a 2D canvas, the bitmap's placement and clip in surface CSS pixels. A rectangle write maps through that placement, widens by one source pixel for filtered sampling, and marks the result as presentation damage, so the next frame clears and replays only that region and uploads only that part of the page texture. A canvas drawn under a transform, a filter, an offscreen capture or another document has no placement, and its writes repaint in full. A write during a frame that is already dirty or repainting in full adds its region to that frame. `test/ui/paint/canvas_rect.json` compares the region frames with full renders under `RADIANT_DAMAGE_VERIFY=1` and finds them identical.

Presenting directly into the retained canvas surface, without the u8 intermediate, is PE-O2.

*Implemented (M2, lighting).* `paint_present` lights a pigment layer when `PaintPresentParams.has_lighting` is set. The height is `relief·(h + t)`, the dried relief plus the wet film. Normals come from central differences, clamped at the layer's edges. The shade is `1 + diffuse·(max(N·L, 0)/L_z − 1)` and the highlight `specular·max(0, (N·H)^n − H_z^n)`, both relative to a flat normal, so a flat film shows its exact KM colour. A pigment layer is opaque and takes no background. An rgba layer refuses lighting. Steps 1, 3 and 4 (paper texture, several layers, the wet overlay) are not composited by the engine yet; the package presents one layer.

### PE8 — Pigment medium

A `pigment` layer has 3B + 2 channels per pixel:

| Channels | Meaning |
|---|---|
| `tK_b`, `tS_b` | Thickness-weighted absorption and scattering of the wet film, per band |
| `t` | Film thickness. The film's own coefficients are `K = tK / t` and `S = tS / t` |
| `R_b` | Reflectance of the substrate (paper, then dried paint), per band |
| `h` | Dried relief height |

B = 3 initially, which is the RGB-band KM that Curtis and `procart.watercolor` use. Spectral bands are later work; the band count is already a parameter of every pigment kernel.

- **Deposit and mixing.** A dab carrying pigment p from the caller's pigment table (`K_p`, `S_p`) with amount `a` mixes into the film by concentration weighting. In stored form this is purely additive: `tK += a·K_p`, `tS += a·S_p`, `t += a`. Deposits therefore commute, which helps tile-parallel and GPU rendering (PE13), and no division happens until display. Pickup and smudge remove and carry the same three quantities.
- **Display.** Reflectance of a film of thickness `t` over substrate `R_g` follows Kubelka's layer formula:

  `R = (1 − R_g·(a − b·coth(b·S·t))) / (a − R_g + b·coth(b·S·t))`, where `a = 1 + K/S` and `b = √(a² − 1)`.

  Guarded limits handle `t → 0`, giving `R → R_g`, and `S → 0`.
- **Drying.** `paint_pigment_dry(tiles, rect, rate)` folds the film into the substrate by KM layer composition. It moves `t` into `h` as relief and clears the film, so later glazes stay transparent and the relief persists.

*Implemented (M2).* `PAINT_FORMAT_PIGMENT` tiles hold `tK[B], tS[B], t, R[B], h`, with B from 1 to 8 (3 in use). `PaintTiles` gains `bands` and `substrate`, the reflectance an empty (clean) tile shows. `PaintDabParams` gains `pigment_bands`, `pigments` (a `(count, 2B)` table of K then S per unit amount) and `pigment_count`.
- **Deposit.** A dab on a pigment target names a pigment row. `amount` scales the pigment (`tK += c·amount·K`, `tS += c·amount·S`) and `thickness` the body (`t += c·thickness`), with `c = mask·opacity·flow` after grain. With both equal this is the deposit above. Erasing scales the film by `1 − c·(1 − eraser)`.
- **Smudge.** Buckets carry film (2B + 1 fields). Pickup samples the mask-weighted mean film under the footprint, as rgba pickup samples colour. A smudging dab moves the film toward the bucket's by `c·smudge` and deposits brush paint with the rest.
- **Refused.** Lock alpha, colorize, posterize, blend modes and colour shifts have no meaning for a film and are refused on pigment targets. So is resolved-colour output until batched replay (PT16).
- **Display.** Kubelka's formula reads only `tK/tS` and `tS`, so the film's thickness cancels and nothing divides by `t`. A pure absorber (`tS → 0`) gives `R_g·e^{−2tK}`, and an optically deep film (`b·tS > 40`) gives `R∞ = 1/(a + b)`. For B ≠ 3 a `(B, 3)` band matrix is required.
- **Drying.** `rate` in (0, 1] folds that fraction: the substrate becomes the reflectance of `rate` of the film over it, and the relief gains `rate·t`. Two partial dryings equal one full drying, because KM layers of one material compose.
- **Tests.** Clean tiles show their substrate; the white, black, pure-absorber and R∞ limits hold; deposit is additive and commutes; blue over yellow mixes green; drying keeps appearance and a glaze over dried paint stays a glaze; lighting leaves flat films unchanged and shades slopes; smudge carries film; refusals write nothing. In a release build the pigment dab kernel covers about 97 M px/s at B = 3, against the 50 M budget.

### PE9 — Wet medium

`paint_wet_step(sheet, paper, params, window, steps)` advances a dense planar sheet `f32[P, h, w]` or `float[P, h, w]` over an active window. The implementation supports both lanes through one kernel so the existing `procart.watercolor` float64 sheet does not need per-step conversions (D3.2.1, S9.2.1). The planes follow `procart.watercolor`: paper height, water saturation, pressure, velocities u and v, suspended pigment g_k and deposited pigment d_k for each pigment k, and scratch planes. The caller supplies the scratch planes as part of the sheet, and `paint_wet_layout(pigments) → P` fixes the plane count, so the step itself allocates nothing. For M in 1–4,096, P is `12 + 3M`: eight fixed planes, M suspended, M deposited, M pigment scratch and four shared blur planes. The three-pigment layout stays identical to `procart.watercolor`.

Each step runs the Curtis 1997 sequence in the reference's order:

1. Move water: update velocities with the paper slope, relax divergence, and flow outward to darken edges.
2. Move pigment by semi-Lagrangian advection.
3. Transfer pigment by adsorption and desorption, with staining and granulation.
4. Capillary flow.
5. Evaporation.

The call returns the bounds of remaining activity so that Lambda can grow or shrink the window. `paint_wet_deposit(sheet, window, pigment_tiles)` moves deposited pigment into the pigment layer when an area dries.

*Presentation and direct dry (M3).* `paint_wet_present` combines suspended and deposited sheet pigment with the underlying three-band pigment film through the shared PE8 Kubelka–Munk renderer, without mutating either owner. It checks the sheet, pigment table, rectangle and count-scaled work before writing (**D5.5.2**, **D7.6.4**). `paint_wet_dry` runs the same evaporation/settling stage as a wet step with zero evaporation, without advecting the sheet; it supports both numeric lanes.

The kernel is written fresh from the paper and the Lambda reference, carrying the Washes MIT notice for derived parts (PT14). Parity with `procart.watercolor` within tolerance is the acceptance test. Live painting steps the sheet in place through a `var` borrow (PT7). A pure watercolor render runs all of its steps inside one call, on a sheet that the call owns.

### PE10 — Bristle tips

`paint_bristles(tiles, segments, state, params)` draws one stroke's bristle trails:

- **State.** Per bristle, in `f32[B, G]`: offset in the brush frame, stiffness and length factor, paint load (colour or pigment index plus amount), pickup colour and an alive flag.
- **Motion.** For each segment between consecutive dab positions, a bristle's position is the dab centre plus its offset, rotated by the dab angle and spread by pressure, lagging by its stiffness.
- **Drawing.** Each bristle draws an anti-aliased thin segment whose ink is load × pressure. It depletes its load by the amount deposited and picks up canvas colour when pickup is enabled.
- **Output.** Trails go into `rgba` or `pigment` tiles. The updated state is returned, so a stroke may span calls. The caller places bristles with keyed randomness; the engine adds none.

*Implemented (M2).* `paint_bristle_bounds` and `paint_bristles` take the brush path as a dab table, of which x, y, radius, aspect, angle, opacity, flow, eraser, aa and grain apply. Tips, smudge and recolouring are refused, and the colour columns are not read.
- **State layout v1.** One f32 row per bristle (`PaintBristleColumn`): `ox, oy` (brush frame, in radii; `oy` is divided by aspect), `stiffness` in [0, 0.99], `width` in pixels, `load`, `x, y` (the tip), `placed`, then the ink. The ink is straight `r, g, b` on rgba and paint per unit coverage `tK[B], tS[B], t` on pigment, so a row has 11 or 10 + 2B columns.
- **Motion and drawing.** At each row the target is the row centre plus the offset rotated by the row angle. The tip moves `1 − stiffness` of the way and draws an anti-aliased segment without its end cap, so consecutive segments do not double their joints. Strength is `opacity·flow·min(1, load)`. The first row only places a tip.
- **Depletion and pickup.** After a segment of length `L`, `load −= depletion·L·strength`. Before drawing, pickup mixes the canvas at the new tip into the ink: straight colour on rgba, and on pigment the film's composition at the bristle's own body thickness.
- **Order.** Rows run in path order and bristles by index within a row, so a path split across calls draws exactly what one call draws. One geometry pass serves the bounds, the tile-presence check and drawing, so they agree on every box.
- **Tests.** The first row only places; load falls monotonically and the trail fades along a long stroke; a split path equals the whole; pickup mixes; trails deposit film on pigment; refusals.

*Wet-sheet extension (M3).* The same PE10 geometry and segment traversal writes to a PE9 wet sheet in f32 or float64. A wet bristle carries the selected pigment amount and water; pickup mixes those two components from the sheet at the tip, while other pigment channels remain on the sheet. Depletion and split-stroke ordering follow PE10. Bounds, sheet and state are validated before either value is written (**D5.5.2**, **D7.6.4**). Erasing and recolouring remain unsupported.

### PE11 — Numerics, determinism, bounds and errors

- **Determinism.** Kernels compute in f32 with a fixed order per pixel: dabs in table order, bristles by index. They contain no randomness. Tile-parallel execution (PE4) preserves the per-pixel order, and data-parallel steps write disjoint outputs, so a replay on one build is bit-identical at any thread count. Release builds target the host architecture, so FMA contraction and vector widths can differ across machines; cross-machine results are tolerance tier (PT8).
- **Validation.** Every view is checked before use: non-null data where required, strides covering extents, element sizes with overflow-checked products, and tile counts matching dimensions. Dab tables are checked for column count, row limits and finite values.
- **Bounded work.** Each call charges candidate pixels against a caller-provided budget. The caller carries the remaining budget across calls of one stroke, as the current `lambda.image` kernel carries its 128-million candidate-pixel limit.
- **Errors.** Kernels return status codes with a short reason. The Lambda bridge maps them to error values (PT15). No kernel aborts or logs to stdout; diagnostics use `log_debug` with a `paint:` prefix.

### PE12 — Performance approach

- **Vectorization.** Inner loops are straight-line spans over contiguous rows, clipped to each dab's bounding ellipse. Each mode and element kind gets its own loop, generated from one template with the switch hoisted out. These loops are written for compiler auto-vectorization, following the typed-array kernels ([Typed Array §11](../Lambda_Typed_Array.md#11-performance-model)). A loop that silently falls back to scalar code counts as a regression.
- **Layout.** Iteration runs tile by tile. Empty tiles are skipped for reads. Dab-mask reuse is added only on evidence (PE4).
- **Threading.** M0 is single-threaded. Batched replay (M4) adds tile-parallel rendering of canvas-independent dab runs (PE4). The wet step and presentation are data-parallel over rows or tiles. Smudge without resolved colours stays sequential. Krita's multithreaded pixel brush faced the same constraint: work can run in parallel, but application must stay ordered. The thread pool is PE-O5.
- **GPU.** The optional GPU backend (PE13).
- **Measurement.** All budgets ([Lambda record §5](../Lambda_Pkg_Paint.md#5-budgets)) are measured on release builds.

### PE13 — GPU backend (proposed)

A GPU backend renders the same dab tables, bristle segments and wet sheets with Radiant's `gl_core` (`native_gl_*`). That layer already provides shader programs, instanced draws, RGBA32F "exact" float targets for simulation state, and region upload and readback.

| Work | GPU form |
|---|---|
| Canvas-independent dabs | One instanced quad per dab. The fragment shader evaluates the mask, tip texture, grain and colour, and fixed-function blending applies premultiplied source-over, erase or lock-alpha. OpenGL blends overlapping fragments in primitive order, instances included, so one instanced draw reproduces the sequential per-pixel order. |
| Pigment deposit | Additive blending of `tK`, `tS` and `t` (PE8), which is order-independent |
| Smudge | Resolved colours from the first render (PE4); otherwise the run stays on the CPU |
| Non-separable blend modes | Destination reads by ping-ponging between two targets per batch of non-overlapping dabs; otherwise the CPU path |
| Bristles | Instanced thin quads |
| Wet step | One fragment-shader pass per sub-step over ping-ponged float targets |
| Presentation | A shader composite of the layer textures. Presenting the result directly through the retained canvas, without a readback, is PE-O8. |

Rules:

- **CPU kernels are the reference (USER, 2026-11-09).** They run everywhere, including `lambda-headless`, which replaces OpenGL with a null backend (**D9.2.1v2** profile C), and they are bit-reproducible on one build. GPU output is tolerance tier against them, and tests compare the two within stated tolerances.
- **Typed arrays stay authoritative.** Textures are caches. Dirty tiles are read back at commit points (stroke end, pass end, save), batched, and preferably asynchronously, because a synchronous readback stalls the pipeline. There are no per-dab readbacks.
- **Capability gate.** The backend needs a context that supports blending into float targets. Without one, `'auto'` falls back to the CPU (PE-O7).
- **Radiant changes.** `NativeGlDraw` gains an explicit blend state. Today it has only a `transparent` flag; it needs over, erase, additive and lock-alpha, the last using separate colour and alpha factors. Float-target blending gets a capability query, and region readback gets an asynchronous form for commit points.

## 2. Testing

`test/test_paint_gtest.cpp` covers:

| Area | Checks |
|---|---|
| Dab masks | A hard dab's summed coverage is close to πr²; masks are symmetric; anti-aliased edges stay within one pixel |
| Modes | Mode identities hold, for example erase after paint at full opacity restores transparency |
| Wash | Wash composites equal one-shot compositing of the accumulated coverage |
| Smudge | Smudging a uniform colour leaves it unchanged; bucket state round-trips across calls |
| Pigment | White and black limits hold; a zero-thickness film shows the substrate; drying preserves appearance |
| Wet | Pigment is conserved within tolerance; no NaN or infinity appears; activity bounds shrink as the sheet dries |
| Bristles | Load decreases monotonically along a stroke |
| Presentation | Opaque sRGB u8 → linear → sRGB u8 round-trips exactly |
| Views | Validation rejects malformed views and undeclared writes, including under fuzzed shapes and strides |
| Replay | Tile-parallel output equals sequential output bit for bit; a replay with resolved colours equals the original smudged render |
| GPU | GPU output stays within tolerance of the CPU reference for each kernel family; the test skips when no capable context exists |

Release-build benchmarks cover each kernel. Parity tests that need the Lambda runtime (against `procart.watercolor` and the old `lambda.image` brush) live in Lambda's suites.

## 3. Milestones

Engine work maps onto the Lambda record's milestones:

| Lambda milestone | Engine deliverable |
|---|---|
| M0 Core | PE2 build and include check, PE3 views and bounds query, PE4 procedural masks, modes, build-up and wash, PE7 `rgba` presentation, PE11 validation |
| M1 Interactive | PE4 bitmap tips, grain, smudge and resolved-colour output; PE5 sampling; PE6 macOS pen capture with coalescing; PE7 dirty-rectangle upload and GPU sub-update; in-place writes through the bridge's `var` procedures (PT7) |
| M2 Pigment and bristle | PE8, PE10, impasto lighting in PE7 |
| M3 Wet | PE9 |
| M4–M5 Automated | PE4 tile binning and tile-parallel replay; PE5 scoring; data-parallel wet step and presentation |
| M6 GPU | PE13 |

## 4. Open items

- **PE-O2 — Present into the surface.** Writing presentation output directly into the retained canvas surface removes one copy of the dirty rectangle. It needs a native surface-access seam, so it waits for measurements.
- **PE-O3 — Blend-mode set.** Proposed: the libmypaint set plus multiply, screen, overlay, darken, lighten and add. Extending it toward Photoshop's set is long-tail work.
- **PE-O4 — Pen input on Windows and Linux.** `WM_POINTER` and XInput2 or libinput, scheduled after macOS proves the event model.
- **PE-O5 — Thread pool.** Which worker facility runs tile-parallel replay, the wet step and presentation: an existing Radiant pool or one owned by the engine. The engine must never block the UI thread.
- **PE-O7 — Float-target blending.** Which devices and macOS OpenGL drivers blend into RGBA32F targets, and whether RGBA16F is precise enough for long build-ups when RGBA32F cannot blend. Measure before M6.
- **PE-O8 — GPU-resident interactive canvas.** During live painting, keep the layer on the GPU and present it directly, reading back only at stroke end. This must fit the GPU compositor's existing presentation and readback rules.

## Appendix A. Dab table layout v1 (draft)

One `f32` row per dab. The column count and order are versioned (`PAINT_DAB_LAYOUT_VERSION` 1, fixed at M0 on 2026-11-09). Layout v1 has 34 columns: column 32 selects the falloff profile and column 33, added in M1 before any layout was published, the blend mode. Columns whose features a version does not implement must hold their neutral value (0, or −1 for `tip` and `pigment`), and the engine refuses anything else rather than ignoring it.

| Col | Field | Meaning |
|---|---|---|
| 0–1 | `x`, `y` | Centre in canvas pixels |
| 2 | `radius` | Pixels |
| 3–4 | `aspect`, `angle` | Ellipse ratio (≥ 1) and angle in radians |
| 5 | `hardness` | 0–1 |
| 6 | `aa` | Anti-aliasing width in pixels |
| 7 | `opacity` | Per-dab alpha (libmypaint's `opaque` after dynamics) |
| 8 | `flow` | Wash-mode coverage gain |
| 9–11 | `r`, `g`, `b` | Brush colour, linear, before smudge |
| 12 | `eraser` | 1 paints, 0 erases (libmypaint's `alpha_eraser`) |
| 13–15 | `lock_alpha`, `colorize`, `posterize` | Mode amounts |
| 16 | `posterize_levels` | Level count |
| 17–20 | `smudge`, `smudge_length`, `smudge_radius`, `smudge_bucket` | Smudge mix, update rate, pickup radius factor, bucket index |
| 21–25 | `dh`, `dl`, `dhsl_s`, `dv`, `dhsv_s` | Colour shifts applied after smudge |
| 26 | `tip` | Tip index; −1 is procedural |
| 27 | `grain` | Paper-grain strength |
| 28 | `pigment` | Pigment-table index on a pigment layer; −1 on rgba and coverage layers (M2) |
| 29 | `amount` | Pigment load per unit coverage on a pigment layer (M2); wet-medium paint load later |
| 30 | `water` | Wet-medium water load (PE9; must be 0 until M3) |
| 31 | `thickness` | Paint body per unit coverage on a pigment layer, which becomes relief (M2) |
| 32 | `profile` | Falloff: 0 is libmypaint's two-segment curve over squared distance; 1 is a linear cone with a flat core of radius `hardness`, used by `lambda.image`'s brush |
| 33 | `blend` | 0 normal, 1 multiply, 2 screen, 3 overlay, 4 darken, 5 lighten, 6 add (`PaintBlend`) |

## Appendix B. Mapping from libmypaint

| libmypaint | Here |
|---|---|
| `MyPaintBrush`: settings, inputs, tracking, spacing, `stroke_to` | Lambda `paint.dynamics` (PT6) |
| `MyPaintSurface.draw_dab(x, y, radius, r, g, b, opaque, hardness, alpha_eraser, aspect_ratio, angle, lock_alpha, colorize, …)` | One dab-table row, consumed by `paint_dabs` (PE4) |
| `MyPaintSurface.get_color(x, y, radius, …)` | `paint_sample` (PE5); also used inside the smudge loop |
| `MyPaintTiledSurface`, 64-px tiles, fix15 premultiplied | `PaintTiles` over f32 typed-array tiles (PT4, PE3) |
| Smudge buckets (2.x) | `smudge_bucket` column and `f32[K, 4]` state (PE4) |
| `.myb` settings files | `paint.brush_from_myb` (PT6) |

## Appendix S. Superseded rulings

The second 2026-11-09 round replaced these first-draft positions. The current text is in the sections named.

- ~~**PE2 / PE-O1.** Sources at `paint/` in the repository root, with `radiant/paint/` as the alternative.~~ The user placed them at `radiant/paint/` (PE2).
- ~~**PE3, PE9.** The bridge forks shared write tiles until PT-O1 allows in-place writes; stepping the wet sheet in place needs PT-O1.~~ The user ruled in-place writes through `var` borrows (PT7); PE3 and PE9 now describe both paths.
- ~~**PE8.** Store K and S per band and mix with a weighted average on each deposit.~~ Thickness-weighted storage makes deposit additive (PE8).
- ~~**PE11, PE12.** No threads in v1.~~ Tile-parallel replay preserves per-pixel order (PE4, PE11, PE12).
- ~~**PE-O6.** GPU wet simulation is not planned, because it would move authoritative state off typed arrays.~~ PE13 proposes a GPU backend that keeps typed arrays authoritative and treats textures as caches.
