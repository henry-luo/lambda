# Radiant Layout & CSS Simplification — Round 2

**Date:** 2026-08-16
**Status:** proposal (analysis complete, implementation not started)
**Scope:** `radiant/` layout + CSS style resolution (~80.3K LOC measured below). Paint, events, editing, state are out of scope.
**Predecessor:** `vibe/radiant/Radiant_Layout_Simplification.md` (round 1, 2026-08-09, −1,030 LOC realized). Round 1 was intra-file dedup and explicitly excluded the CSS resolver. Round 2 is structural: it unifies parallel engines and parallel tables, not just repeated lines.
**Governing rulings:** **D7.1.1** (all work stays inside the `radiant` archive layer; the one exception — keyframes parsing — moves *down* into `lambda/input/css`, which the layering DAG permits) and **D4.5.1v3** (all new structs live in existing arenas/view pools/scratch; no new ownership graph, no GC'd layout state).

## 1. Measured baseline (2026-08-16)

| Area | Files | LOC |
|---|---|---:|
| CSS resolution | resolve_css_style, resolve_htm_style, css_cascade, css_prop_table, css_animation, view_prop_defaults/ensure | 13,238 |
| Block/driver | layout.cpp, layout_block, block_context, layout_box/constraints/containing_block/percentages/measure/pass/abs_children | 14,841 |
| Intrinsic sizing | intrinsic_sizing.cpp | 6,799 |
| Flex | layout_flex, layout_flex_multipass, layout_flex_measurement | 6,791 |
| Grid | layout_grid, layout_grid_multipass, grid_*.cpp/hpp | 7,719 |
| Table | layout_table, layout_table_caption, layout_table_metadata | 7,521 |
| Inline/text | layout_inline, layout_text, layout_bidi | 6,393 |
| Positioned/multicol/misc | layout_positioned, layout_multicol, layout_list/form/custom/counters/alignment/debug | 9,507 |
| Headers | layout.hpp, view.hpp | 7,479 |
| **Total in scope** | | **80,288** |

## 2. Root-cause diagnosis

Four independent audits (CSS resolution; flex/grid; block/intrinsic; table/multicol/positioned/inline) converged on five root causes. Everything below is evidence-backed with file:line spans verified against the current tree.

1. **Measurement is a parallel engine, not a mode.** `measure_element_intrinsic_widths` (intrinsic_sizing.cpp:2709–5755, 3,046 lines) re-implements block, inline, table, grid, and flex layout a second time; `calculate_max_content_height` (:5869–6734, 865 lines) is a third, self-described "simplified estimation" block engine with a lossy margin-collapse fork. Meanwhile the machinery for one code path already exists and works: `RunMode::ComputeSize`, `LayoutMeasureScope` (layout_pass.cpp:153), `AvailableSize::MIN_CONTENT/MAX_CONTENT`, and `layout_block_content` already honors intrinsic available space (layout_block.cpp:6992–7001). The twin engines persist anyway, and they disagree (measured ≠ committed geometry is a documented bug class, RAD_05 §8.2).
2. **CSS property application is code, not data.** `resolve_css_property` is a 184-case, ~1,710-line switch, while a descriptor table with exactly the needed shape — `CssPropAccessor {id, group_kind, offset, value_kind}` (view.hpp:67) — already drives the *serialization* direction. Three more private mini-tables (`CssSimpleKeywordSpec`, `CssKeywordSlotSpec`, `css_animation_length_slot`) re-encode the same property→slot mapping. css_animation.cpp additionally re-parses CSS **from raw strings** with four hand-rolled parsers (keyframes bodies are stored as text), including a serialize-then-reparse round trip for transitions, with real unit bugs (`rotate(0.5turn)`, `translateX(2em)` resolve wrong).
3. **Geometry is written per-axis by hand where the axis abstraction stops.** The flex core and positioned layout use `LayoutAxisRefs`/`LayoutAxisPair` well; the remaining column/row mirror blocks are concentrated in `grid_enhanced_adapter.hpp` (a 390-line function whose column and row halves are line-for-line mirrors), fieldset handling (3 copies × writing modes), and the ratio-transfer/margin blocks of layout_block.cpp.
4. **Every flow container re-rolls the same scaffold.** The "set content bounds → setup font/line-height → text-indent → line_reset → loop `layout_flow_node` → final `line_break` → derive height" sequence is hand-copied in 6 places (block inner content, table cell, caption ×2 — two of them character-identical — multicol, and the abs-block path which correctly reuses the canonical one). Same story for: replaced-element sizing (6 copies of the CSS Images §5.1 ladder, 300×150 literals at 10+ sites), tag classification (5 divergent tables), alignment distribution, order sort, pass-cache prologue, whitespace scanning.
5. **Residue from retired designs.** Verified dead: `TrackSizingContext` (grid_sizing_algorithm.hpp:58–97), `create_grid_area()`, write-only fields (`FlexLineInfo::total_flex_grow/shrink/baseline`, `needs_reflow` ×2, `auto_row/col_cursor`), stale decl `adjust_row_text_positions_final`. Misleading naming: the "enhanced" grid prefix implies a legacy path that no longer exists. Three coexisting measurement caches; dual `alloc_*`/`ensure_*` prop families. RAD_02/03/05/06/08/09/10 all describe deleted code.

## 3. Target design — five unifications

The round-1 principle stands: one owner per concern, no generic formatting-context vtable. Flex §9, grid §11, and table §17 keep separate algorithm bodies; what unifies is measurement, property application, axis math, scaffolding, and the caches around them. New policy for round 2, per the project direction: **retire replaced code completely — no compatibility wrappers or forwarding shims survive a phase** (round 1 phase 0 kept "compatibility adapters"; round 2 deletes them at the end of each phase).

### U1. One layout engine: measurement = layout in `ComputeSize` mode

**Design.** Intrinsic sizing stops being a shadow engine. `measure_element_intrinsic_widths` becomes: cache probe → run the real per-FC algorithm under `LayoutMeasureScope` with `AvailableSpace::make_min_content()/make_max_content()` → cache store. `calculate_max_content_height`'s element branch becomes `layout_measure_content_height(lycon, block, width)` = real layout under `AvailableSpace::make_width_definite(width)`, reading back `block->height`. intrinsic_sizing.cpp keeps what is genuinely its own: text measurement kernels, keyword/aspect-ratio helpers, cache policy (~2,300 LOC end state, from 6,799).

Sub-designs, each with a single owner:

- **Table columns:** one `table_measure_columns(LayoutContext*, DomElement* table, TableIntrinsicMode)` in new `layout_table_intrinsics.cpp` returning `{col_min[], col_max[], col_percent[]}`; consumed by BOTH `table_auto_layout` Step 2 (today `measure_cell_widths`, layout_table.cpp:5663–5966) and the intrinsic table branch (today intrinsic_sizing.cpp:3754–4460, a full second engine with verbatim-equivalent inline-run accumulation). The shared inline-run walker also replaces `measure_anonymous_cell_run`.
- **Flex main-size:** the §9.9.1 shrink-to-fit clamp moves into intrinsic_sizing's flex branch (its one missing rule); then the re-implementations in layout_flex_measurement.cpp:1138–1364 (`FlexIntrinsicAccumulator`) and layout_flex.cpp:841–981 delegate to it. The non-container case in `calculate_item_intrinsic_sizes` already delegates — proving the pattern.
- **Grid:** the intrinsic grid branch (fixed-track sum with its own pad/border/max-width re-clamp, intrinsic_sizing.cpp:4477–4577) routes to real track sizing in ComputeSize.
- **Replaced elements:** new `layout_replaced.cpp` owning the CSS Images §5.1 ladder once:

```c
// layout.hpp — tier-3 value types
typedef struct ReplacedNaturalSize {
    float width, height, ratio;
    bool has_width, has_height, has_ratio;
} ReplacedNaturalSize;
ReplacedNaturalSize layout_replaced_natural_size(LayoutContext* lycon, ViewBlock* block); // per-tag probe: img/svg/canvas/video/iframe/form + UA defaults table (the ONLY place 300x150/300x54 literals live)
SizeF layout_replaced_used_size(LayoutContext* lycon, ViewBlock* block, ReplacedNaturalSize natural, AvailableSpace space); // specified -> ratio transfer -> natural -> fallback, once
```

  Retires the six copies: layout_block.cpp:6301–6814 replaced region, `layout_inline_svg` (:4387–4532), the canvas auto-size trio (:355–808), `layout_measure_replaced`'s private defaults table, the intrinsic Phase-3 short-circuit, and the height-estimator replaced branch.
- **One measurement cache:** the linear-scan `MeasurementCacheEntry` array on ViewTree (layout.hpp:2592, layout_flex_measurement.cpp:466–553) and the width-only intrinsic piggyback fields fold into the existing 9-slot per-element `LayoutCache` (`layout_pass_cache_get/store_for_space`). This also fixes the latent cross-mode bug: flex writes border-box/content-box meanings into the shared entry while grid writes max-content/min-content into the same fields — readers cannot tell which convention an entry holds.
- **Text kernel:** one `TextMeasureCursor` iterator (header-local `layout_text_measure.hpp`) for the per-codepoint sequence (decode → transform → small-caps → space width → glyph → kerning → letter-spacing), used by the main loop, `measure_first_word_width`, `text_has_line_filled`, and the intrinsic gates (`intrinsic_apply_small_caps` etc. duplicate `layout_text.cpp`'s helpers today).

**Retires completely:** the monolith's table/grid/flex/font-cascade branches; `calculate_max_content_height`'s element branch incl. its margin-collapse, flex-wrap, grid-row, replaced, and tag-list forks; `measure_cell_widths` + `measure_anonymous_cell_run`; `FlexIntrinsicAccumulator` + the §9.9.1 loop copy; the six replaced-sizing copies; the ViewTree measurement-cache module; the intrinsic small-caps/latin-run gate duplicates. **Estimated −4,200 LOC (range 3,800–5,000).**

### U2. One CSS property descriptor: extend `CssPropAccessor` with resolve + animate legs

**Design.** One immutable descriptor row per property, in css_prop_table.cpp, serving all four directions the engine needs (the struct's own comment already anticipates this: "Jube's record dispatch can index the same immutable descriptors").

```c
// view.hpp — extend the existing row; plain and pointer-free apart from callbacks
struct CssPropAccessor {
    CssPropertyCode id;
    PropGroupKind group_kind;      // + ensure-fn table indexed by group_kind
    uint16_t offset;
    CssPropValueKind value_kind;
    uint8_t flags;                 // + CSS_PROP_ACCESSOR_INHERITED, HAS_FLAG_FIELD
    uint16_t has_flag_offset;      // paired has_* bool, 0 if none (generated, not hand-set)
    uint8_t resolve_kind;          // NONE|ENUM|ENUM_MAPPED|PX|NUMBER|INTEGER|LEN_PCT|COLOR|STRING
    uint8_t keyword_rule;          // reuse CssSimpleKeywordRule (accept-group / remap)
    const CssKeywordRemap* remap;  // optional keyword->stored-enum table
    uint8_t anim_value_kind;       // NONE|FLOAT|LEN_PCT|COLOR|TRANSFORM — drives interpolation
    CssPropSerializeFn serialize;
    CssPropDeriveFn derive;
};
```

One generic `resolve_direct(accessor, lycon, span, value)` mirrors the existing `serialize_direct`: ensure prop group (via a `PropGroupKind→ensure` table), resolve by `resolve_kind`, write through `offset`, set the paired has-flag, and handle `inherit` generically (copy parent's group+offset slot) — only computed-value special cases (font-size em-compounding, line-height) stay bespoke. The ~16 remaining mechanical keyword cases, 45 manual `= value->data.keyword` writes, and ~15 hand-rolled inherit bodies become rows. Genuinely custom logic **stays as code**: `BACKGROUND` layers, `FONT`/`FLEX` shorthands, grid templates/placement, `VERTICAL_ALIGN`, axis sizing, counters, `LIST_STYLE`.

Companion moves:

- **`CssLenPct` value type** replacing eleven private micro-structs/paths for "length-or-percent into {value, is_percent}" (`CssBackgroundComponent`, `ResolvedInsetValue`, `parse_border_radius_component`, `CssFlexBasisValue`, …):

```c
// layout.hpp, beside CssQuadValues
typedef struct CssLenPct { float value; bool is_percent; bool is_auto; bool valid; } CssLenPct;
CssLenPct css_resolve_len_pct(LayoutContext* lycon, uintptr_t property, const CssValue* value, uint8_t flags);
```

- **Parse `@keyframes` once.** Keyframe bodies are parsed at stylesheet-parse time into `CssDeclaration`/`CssValue` lists (in `lambda/input/css`, the layer that owns declaration interpretation), so css_animation.cpp's four string parsers (`parse_color_value`, `parse_transform_func/value`, `parse_aspect_ratio_value`, `parse_keyframes_content` machinery) and the transition serialize-then-reparse round trip are deleted; capture/interpolation resolve through `resolve_color_value`/`resolve_transform_function`/`resolve_length_value` like every other property. Fixes the angle/length unit bugs.
- **Animation's five per-property switches** (`property_value_type`, `css_transition_value_type_for`, `css_animation_length_slot`, `apply_animated_value`, `css_transition_read_used_value`) collapse onto `anim_value_kind` + group/offset. Adding a property row makes it animatable for free.
- **One color story:** `parse_html_color` gains named colors via the existing `css_enum_by_name` + `css_named_color_to_rgba` (fixes the documented `bgcolor=` bug); the rgb()/hsl() legacy-vs-modern component walks inside `resolve_color_value` share one iterator; one width/style/color shorthand classifier replaces the three copies (border, outline/column-rule, text-decoration).
- **One prop-alloc family:** `alloc_grid_prop`/`alloc_flex_prop` get `GRID_PROP_DEFAULT`/`FLEX_PROP_DEFAULT` rows in view_prop_defaults.cpp like the other 15 prop types; `ensure_*` gains a ViewTree overload so css_animation.cpp's local re-implementations die; the `alloc_*` family retires, `ensure_*` is the family.
- **One inheritance list:** derive `inheritable_props[]` from `css_property_is_inherited` so the two lists cannot drift (full unification of the inheritance loop stays out of scope — it encodes computed-value semantics).
- The Chromium 13/16 monospace quirk (3 hand-maintained copies) and the ×4 bgcolor-attr pattern in resolve_htm_style.cpp each get one helper; the imperative HTML-attr architecture itself stays (deliberate per RAD_02 §5).

**Retires completely:** `CssSimpleKeywordSpec` + `CssKeywordSlotSpec` tables (become rows), `css_animation_length_slot`, all four animation string parsers + `skip_ws`/`parse_float` machinery, the transition text round-trip, ~16 keyword cases + ~15 inherit bodies from the big switch, 11 len-pct micro-copies, the duplicate monospace-quirk and bgcolor blocks, the `alloc_*_prop` family. **Estimated −1,300 LOC.**

### U3. Finish the axis abstraction where the mirrors actually are

**Design.** No storage rewrite of prop structs in this round (see §7 deferred); the win is applying the existing house pattern (`LayoutAxisRefs`, `LayoutAxisPair`, axis-taking helpers — already proven in flex core and positioned/sticky) to the concentrated mirror sites:

- `run_enhanced_track_sizing` column/row halves → one `run_axis_track_sizing(TrackArray& tracks, const TrackArray& defs, ContribArray&, AxisSizingOptions opts)` called twice; `AxisSizingOptions {available, gap, gap_is_percent, min_max_constraint, cap_auto_growth, float* out_intrinsic}`. Also resolves today's unexplained col-pass asymmetry (`col_available` vs `col_available2` fed to different stages).
- Placement mirror pairs: `extract_grid_item_info` col/row cascades → `placement_from_authored(...)`; `resolve_negative_lines_in_items` → one per-placement helper; `place_definite_row_item`/`place_definite_column_item` → `place_single_definite_axis_item(matrix, item, AbsoluteAxis, ...)` (the file's own `place_indefinite_item` already shows the axis-generic pattern). `collect_item_contributions` col/row branches parameterize by axis, keeping the two real differences explicit.
- Track math: `grid_tracks_total(const TrackArray&, float gap)` + `grid_track_line_position(...)` beside `compute_track_offsets`; positioning/abs paths read the `track.offset` already stored instead of recomputing prefix sums (6 copies today, including a 4th recompute in baseline re-alignment).
- `MarginChain` value type for sign-aware margin collapse, replacing ~8 open-coded max/min pairs and the two lossy forks (vertical-writing, intrinsic estimator):

```c
// layout.hpp
typedef struct MarginChain {
    float pos, neg;
    float value() const { return pos + neg; }
    static MarginChain of(float m);
    MarginChain join(MarginChain other) const;  // CSS 2.1 8.3.1: max(pos), min(neg)
} MarginChain;
MarginChain layout_block_bottom_chain(ViewBlock* block);
```

- Box-model micro-invariants: `layout_set_border_box_size(ViewBlock*, LayoutAxis, float)` maintaining the width/content_width pair (~15 hand-rolled reconstructions today); adopt `layout_axis_decoration_start/end` at the 23 manual `border.X + padding.X` sums; adopt `non_auto_margin_start/end()` where re-derived; route the 18 inline `base * percent / 100` sites through `layout_resolve_deferred_percentage` (kills base-selection drift).
- Fieldset: extract `layout_fieldset.cpp` with one axis-parameterized `fieldset_place_legend()` replacing the three writing-mode treatments (~290 LOC today); halve the two mirror ratio-transfer blocks in layout_block.cpp (:7043 vs :7185) via `LayoutAxisRefs`.
- Positioned: `layout_abs_block`'s `<img>` fix-up re-derivations call the existing `resolve_abs_auto_margins_axis` + `get_static_position_direction` + `recalculate_right_positioned_x`; post-shrink-to-fit text re-alignment reuses `line_align`.

**Retires completely:** the row-pass mirror of track sizing, `PctColInfo`/`PctRowInfo` twins, the col/row placement pairs, `grid_track_total` + `grid_track_positions` + `calculate_grid_line_positions` recomputes, the fieldset triplication, the abs-img hand math, the open-coded collapse pairs. **Estimated −900 LOC.**

### U4. One flow-box scaffold + shared utilities

**Design.**

```c
// layout.hpp
typedef struct FlowBoxSetup {
    float content_width, content_height;
    float origin_x, origin_y;
    bool is_bfc;
} FlowBoxSetup;
void layout_setup_flow_box(LayoutContext* lycon, ViewBlock* box, const FlowBoxSetup* setup); // font, line-height, block font metrics, text-indent(+percent), line_reset, align/direction
float layout_flow_children(LayoutContext* lycon, ViewBlock* box); // layout_flow_node loop + final line_break; returns advance_y
```

Six sites converge on it (block inner content stays canonical; table cell keeps its border-collapse insets and percentage pass; both caption sites collapse onto a shrunken `relayout_table_caption`; multicol keeps its float prescan as a pre-hook). Explicitly NOT a vtable driver — form/list/custom/counters keep their own entry shapes (re-affirmed from round 1; their bodies share almost nothing beyond this scaffold).

Companions, each one-owner:

- **Multicol line projection:** `MulticolLineItem {view, rect, x, y, height, line_index, is_text}` + `multicol_collect_line_items(...)` + `multicol_place_line_items(...)`, replacing the three parallel projectors (fragmented-inline ~270, mixed-direct ~200, inline-only ~130 LOC — each with its own line-advance estimator, widows look-ahead, and column repositioning; two keep 512-entry stack arrays). The outer spanner/group loop unifies the same way (`multicol_run_flow_groups(...)` with the three existing lambdas — the file's `multicol_distribute_flow_group` callback design already proves the pattern).
- **layout_block.cpp decomposition:** extract `layout_flow_atomic_inline_into_line(...)` (~430-line tail half) and `layout_flow_block_into_parent(...)` (~380-line half) so `layout_block` becomes a ~250-line spine matching RAD_03 §4.1's steps; extract `layout_replaced.cpp` (U1) and `layout_fieldset.cpp` (U3); merge the twice-written clearance/float-offset re-check and `block_context_float_bottom` vs `block_context_recompute_lowest_float_bottom`.
- **One alignment distributor:** hoist `grid_content_distribution` into layout_alignment.cpp as `compute_content_distribution(int32_t alignment, float free_space, int count) -> {offset, spacing}`; flex's two hand-rolled sequences, `flex_direct_text_alignment_target`, and the manual center/end math in `layout_flex_abs_after_child` fold in (grid's abs path already uses the shared helper).
- **Small shared owners:** `layout_sort_views_by_order(View**, int)` (2 stable insertion sorts); `layout_resolve_percentage_gap(float* gap, bool* is_percent, float pct, float base)` (5 copies); `layout_pass_cached_size(...)`/`layout_pass_finish(...)` for the FLEX/GRID entry prologue/epilogue; one canonical `layout_tag_is_default_inline(NameId)` + `layout_element_is_replaced` pair retiring the 5 divergent tag tables (incl. two strcmp lists — today the same element classifies differently per subsystem); `layout_inline_atomic_item_baseline(...)` retiring the third copy of the baseline decision tree in table cells; BR-clear calls `block_context_clear_y`; one ASCII-whitespace scanner; `table_for_each_row_with_metadata(...)` walker under the three metadata→view sync loops; one `table_resolve_explicit_axis_size()` for the fixed/auto resolver copies; table's private DOM surgery (5 functions) calls `DomNode::append_child/insert_before/remove_child` (or documents why it must bypass hooks); one `format_additive_counter(...)`; one recursive single-text-line predicate.

**Retires completely:** the caption twin (`layout_table.cpp:4050–4106` vs `layout_table_caption.cpp:7–104` — character-identical lines today), the three multicol projectors + duplicated group loop, the five tag tables, the three whitespace scanners, the three metadata walkers, the DOM-surgery quintet, the duplicated baseline/order/gap/prologue blocks. **Estimated −1,500 LOC.**

### U5. Retire residue; rename to match reality

- Delete verified-dead: `TrackSizingContext` struct, `create_grid_area()`, `FlexLineInfo::total_flex_grow/total_flex_shrink/baseline`, `FlexContainerLayout::needs_reflow`, `GridContainerLayout::needs_reflow/auto_row_cursor/auto_col_cursor`, decl `adjust_row_text_positions_final`, the `(void)`-cast `require_loadable_face_source` params, the two never-read counter tables, `get_span_value_ex`'s discarded parameter. (~120 LOC, zero risk.)
- Rename the misleading "enhanced" grid layer (there is no legacy path left; the header itself notes the dead parallel driver was removed): `grid_enhanced_adapter.hpp` → `grid_integration.hpp`; drop `enhanced_` from `run_enhanced_track_sizing`/`resolve_track_sizes_enhanced`/`EnhancedGridTrack`.
- **Doc refresh** (stale claims verified): RAD_02 (~13.9K/248-case figures; now 8,156/184), RAD_03/04/05 (deleted `IntrinsicSizeCache`/`LayoutOutput`/twin box helpers; `layout_block` size/location), RAD_06 §10.1 (bidi IS live — `layout_bidi_line` called from layout.cpp:2503), RAD_08 (retired global cache, fixed triplication, removed logging, stale sizes), RAD_09 (auto-fit collapse IS live; `resolve_grid_template_areas` gone), RAD_10 (function inventory, caption sync note).

## 4. Retirement ledger (what must be gone at the end)

No forwarding wrappers survive. Each phase's exit criterion includes grep-zero on the retired symbol.

| Retired | Replaced by |
|---|---|
| intrinsic table/grid/flex/font branches of `measure_element_intrinsic_widths` | real FC algorithms under `ComputeSize` + `table_measure_columns` |
| element branch of `calculate_max_content_height` (incl. margin/flex/grid/replaced forks) | `layout_measure_content_height` = real layout under `LayoutMeasureScope` |
| `measure_cell_widths`, `measure_anonymous_cell_run` | `table_measure_columns` + shared inline-run walker |
| `FlexIntrinsicAccumulator`, flex §9.9.1 loop copy | intrinsic flex branch (canonical, gains the §9.9.1 clamp) |
| 6 replaced-sizing copies incl. `layout_inline_svg`, canvas trio, `layout_measure_replaced` defaults | `layout_replaced_natural_size` + `layout_replaced_used_size` |
| ViewTree `MeasurementCacheEntry` module + intrinsic piggyback fields | 9-slot `LayoutCache` |
| `intrinsic_apply_small_caps` / latin-run gate duplicates, `measure_first_word_width` kernel copy | `TextMeasureCursor` + promoted `layout_text` helpers |
| `CssSimpleKeywordSpec`, `CssKeywordSlotSpec`, `css_animation_length_slot` | `CssPropAccessor` rows (`resolve_kind`, `anim_value_kind`) |
| animation string parsers (`parse_color_value`, `parse_transform_func/value`, `parse_aspect_ratio_value`, keyframes text machinery) + serialize/reparse | parse-once keyframes → `CssDeclaration`; shared resolvers |
| ~16 keyword cases, ~15 inherit bodies, 11 len-pct micro-copies | `resolve_direct` + generic inherit + `CssLenPct` |
| `alloc_*_prop` family; css_animation local ensure copies | `ensure_*` family + defaults rows + ViewTree overload |
| row-pass mirror of track sizing; col/row placement pairs; 3 track-sum + 3 line-position copies | `run_axis_track_sizing`, axis-generic placement, `grid_tracks_total`/`grid_track_line_position` |
| fieldset triplication; abs-`<img>` hand math; open-coded collapse pairs; vertical/intrinsic margin forks | `layout_fieldset.cpp`; existing abs helpers; `MarginChain` |
| caption twin; 3 multicol projectors + group-loop copy; 5 tag tables; 3 whitespace scanners; 3 metadata walkers; DOM-surgery quintet; baseline tree 3rd copy; order/gap/prologue/distribution copies | U4 owners |
| dead structs/fields/decls; "enhanced" naming | deleted / renamed |

## 5. Execution plan

Phases are independently landable, behavior-neutral unless a fixture proves an existing defect, and each must show a strictly negative `./utils/verify_loc_reduction.sh --ref <pre-phase> <touched files>` — a pure move claims no reduction. Gates per phase: `git diff --check`, `make check-int-cast`, `make build` (0 warnings), `make layout suite=baseline` / `make test-layout-baseline`, plus `make test-radiant-baseline` for phases changing pass ownership; geometry diffs are defects unless spec-backed with a fixture.

| Phase | Content | Net LOC | Risk | Extra proof |
|---|---|---:|---|---|
| R2.0 | U5 dead code + stale decls (no renames yet) | −120 | none | build only |
| R2.1 | U4 small owners: alignment distributor, order sort, gap, pass prologue, tag tables, whitespace, BR-clear, baseline helper, metadata walker, DOM surgery, counter/misc | −500 | low | flex/grid/table suites |
| R2.2 | U2 property table: accessor extension + `resolve_direct` + keyword/inherit rows + `CssLenPct`; animation switches onto `anim_value_kind`; ensure/alloc unification; color/quirk/classifier dedup | −800 | low-med | CSSOM serialization tests, animation/transition fixtures |
| R2.3 | U2 keyframes parse-once (crosses into `lambda/input/css`); delete string parsers + round trip | −350 | med | animation suite + new unit-bug fixtures (`0.5turn`, em translate) |
| R2.4 | U3 axis: `run_axis_track_sizing`, placement pairs, track math, `MarginChain`, box micro-helpers, percent routing, fieldset module, abs-img | −900 | med | grid suite, margin-collapse + writing-mode fixtures |
| R2.5 | U1 stage A: `layout_measure_content_height` replaces height-estimator element branch; one measurement cache | −950 | med | flex/grid percentage-height fixtures; perf spot-check (release build) |
| R2.6 | U1 stage B: `table_measure_columns`; intrinsic table/grid branches deleted | −1,100 | med-high | table baseline, colspan/caption/border-collapse fixtures |
| R2.7 | U1 stage C: flex intrinsic 3×→1; text kernel cursor; replaced-sizing module; monolith shrinks to probe/dispatch/finalize | −1,800 | high (staged) | full baseline ×2, intrinsic-vs-committed differential fixtures |
| R2.8 | U4 structural: flow-box scaffold (6 sites), caption dedup, multicol projectors + group loop, layout_block tail split | −1,000 | med | multicol/table/caption suites |
| R2.9 | U5 renames + RAD_02–RAD_10 doc refresh | 0 | none | build, grep-zero old names |

**Planned realized reduction: ~7,500 LOC net (conservative floor ~5,500; stretch ~9,000)** on the 80.3K scope, i.e. roughly −9%, with headers also shrinking as retired decls leave layout.hpp. Round-1 experience (planned ≥544, delivered 1,030) suggests these evidence-backed estimates are attainable; the floor assumes R2.7 lands only partially.

Not counted in LOC but part of the outcome — **behavioral fixes that fall out** (each needs a fixture *before* the refactor lands so the fix is provable): keyframes/transition unit bugs (`rotate(0.5turn)`, `translateX(2em)`); `bgcolor=` named colors; measured-vs-committed height divergence (RAD_05 §8.2 class); flex/grid measurement-cache convention collision; multicol's lossy `max()` margin collapse with negative margins; tag-classification disagreements (e.g. `label` inline in one table, absent in another); the grid col-pass `col_available`/`col_available2` asymmetry (resolve deliberately, either way).

## 6. Direct answers to the round-2 brief

- **Simplified design:** one layout engine with a measurement *mode* instead of three engines; one property descriptor table serving resolve/serialize/animate/inherit instead of one table + three mini-tables + five switches + four string parsers; one owner per repeated computation (replaced sizing, table columns, line projection, alignment distribution, flow scaffold).
- **C+ struct-based classes** (all in common headers, per house convention — structs with methods, no `std::`): extended `CssPropAccessor` (view.hpp), `CssLenPct`, `ReplacedNaturalSize`, `MarginChain`, `FlowBoxSetup`, `MulticolLineItem`, `AxisSizingOptions` (+ existing `LayoutAxisRefs`/`AvailableSpace`/`LayoutCache` promoted to sole owners of their concerns). Modified: `FlexLineInfo`/`FlexContainerLayout`/`GridContainerLayout` (dead fields removed), `TrackArray` consumers.
- **Retire old ones completely:** §4 ledger; per-phase grep-zero exit criterion; no compatibility wrappers.

## 7. Non-goals (re-affirmed and new)

- No generic formatting-context vtable; flex/grid/table sizing algorithms remain separate spec machines (flex §9.7 iterates items, grid §11 iterates tracks — a shared abstraction over `FlexLineInfo` vs `EnhancedGridTrack` would be artificial). The only cross-FC sharing is the measurement mode, alignment distribution, and the generic "distribute proportionally over eligible tracks" loop if it proves clean (~40 LOC, low priority).
- No merging of flex measurement vs multipass files (speculative vs committed writes stay separate); the multipass files are pipeline stages, not parallel implementations — verified.
- No wholesale CSS-inheritance-loop unification (list derivation only); no declaration-synthesis rewrite of resolve_htm_style (deliberate imperative UA-style architecture per RAD_02 §5).
- **Deferred, revisit after R2.7:** axis-indexed storage in `BlockProp` (`AxisConstraints size[2]` replacing the ~28 paired given/min/max/percent/fit-content fields, which would reduce `LayoutAxisRefs::bind_constraints` to array indexing) and a logical-axis rewrite of the block spine (the ~1,000-line vertical-writing bolt-on). Both are correct end-states but touch every consumer; U1 first shrinks the consumer surface by thousands of lines, making the storage rewrite a later, smaller diff.
- Direct-text-in-flex unification (three mechanisms → anonymous-item path, ~180 LOC) is **conditionally in**: last slice of R2.7, only with dedicated text-in-flex fixtures green; it is behavior-adjacent.

## 8. Verification instrumentation to add first

- A differential fixture mode asserting `measure_element_intrinsic_widths(elem) == layout width under ComputeSize min/max-content` on the layout suite corpus, run before/during R2.5–R2.7 — the twin engines' disagreements become visible before each deletion, and the corpus tells us which branches are actually exercised.
- Fixtures for each §5 behavioral fix, committed before the phase that fixes them.
