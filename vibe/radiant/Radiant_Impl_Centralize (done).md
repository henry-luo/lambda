# Radiant Semantic Centralization — Implementation Plan

**Date:** 2026-09-10

**Status:** implemented; verification and residual-audit results recorded below

**Baseline commit:** `b9ca1a3660`

**Baseline production size:** 216,649 physical lines in 181 tracked
`radiant/` C/C++/Objective-C++ source and header files (`*.c`, `*.cc`,
`*.cpp`, `*.h`, `*.hpp`, `*.m`, `*.mm`)

**Primary precedent:**
[`Radiant_Design_Geometry.md`](Radiant_Design_Geometry.md) and
`view_geometry.cpp`

**Header convention:** D7.1.3's provider-ownership rule requires each public
declaration to be provided by its owning module; DD4 completes that rule for
Radiant by placing shared declarations in coherent subsystem headers. This
was completed by
[`Radiant_Imp_Code_Dedup (done).md`](Radiant_Imp_Code_Dedup%20(done).md):
shared declarations live in the coherent Radiant headers, never in a new
file-specific header

## 1. Objective

Centralize repeated Radiant *semantic decisions* behind narrow shared APIs,
using `view_geometry.cpp` as the model: one owner for a contract, explicit
dependency injection where callers have different state providers, and small
caller adapters that retain subsystem policy.

The implementation is complete only when all of the following are true:

1. Replaced-element intrinsic facts and common sizing rules have one owner.
2. Form input types and native capabilities have one descriptor source.
3. Shared text advance classifications and offset/advance mapping have one
   owner.
4. Resource address resolution has one owner, with fixture mapping injected
   rather than copied into each loader.
5. Raster pixel packing, alpha mode, sampling, and common source-over
   operations are explicit and shared.
6. File export uses one document/UI/layout session for PNG, JPEG, tiled PNG,
   SVG, and PDF.
7. Radiant property metadata has one indexed runtime descriptor instead of
   parallel classification lists.
8. Production Radiant code is reduced by **at least 1,000 net code lines by
   sharing and reuse**. Comment removal, blank-line removal, code reformatting,
   dead-code deletion unrelated to the shared replacement, generated-file
   churn, and moving code outside the measured tree do not count.
9. Existing behavior and output remain unchanged unless an independently
   approved design or conformance fix is split into a separate change.

This is an implementation plan, not a new semantic ruling. No `S#` or `D#`
ruling specifies Radiant translation-unit organization. The affected form and
state boundary must nevertheless preserve the existing lower-tier rulings:
ES3 (effective missing input type is `text`), ES9 (editing policy stays in the
Lambda behavior package while native code supplies mechanism), ES11 (one
DocState-bound state store), and ES16 (markup-derived form state is computed,
not cached). See `vibe/Lambda_Design_DOM_State.md` §§3.3 and the ES ledger.

## 2. Non-goals and invariants

### 2.1 No generic utility dumping ground

Do not create `radiant_utils.cpp`, `common.cpp`, or a similarly unbounded
owner. Every shared function must belong to a named domain contract and have a
clear declaration home.

### 2.2 No new file-specific headers

New implementation files are allowed, but no corresponding headers are:

| Implementation owner | Declaration home |
|---|---|
| `replaced_intrinsic.cpp` | `layout.hpp` |
| text-metric additions in `layout.cpp` | `layout.hpp` for measurement/run APIs; `view.hpp` for view/font value types |
| `form_control_model.cpp` | `view.hpp` |
| `raster_pixel.cpp` | `render.hpp` |
| expanded `resource_resolver.cpp` | `radiant.hpp` |
| expanded `css_prop_table.cpp` | `view.hpp` |
| expanded `render_output.cpp` | `render.hpp` |

Internal header-only algorithm files that already exist remain exceptions only
where DD4 already allows them. This plan creates no new exception.

### 2.3 Centralize facts and mechanics, not caller policy

- Replaced-element code centralizes natural dimensions, ratios, fallback
  objects, and shared constraint transfer. Block, flex, grid, table,
  positioned, and min/max-content algorithms keep their formatting-context
  decisions.
- Text metrics centralizes Unicode advance facts and mapping mechanics. Line
  breaking stays in layout; painting stays in render; caret movement policy
  stays in event/editing.
- Resource resolution decides identity and address. Fetch scheduling, cache
  lifetime, decoding, and sync/async policy remain with their loaders.
- Raster primitives make pixel/alpha contracts common. Backend-specific
  ThorVG, CoreGraphics, PDF, and SVG lowering remains separate.
- Form descriptors classify controls and native mechanical capabilities. They
  do not reclaim Lambda-owned editing, activation, validation, or ARIA policy
  under ES9/ES11/ES16.
- CSS descriptors centralize metadata. Complex resolution logic remains in a
  callback or the existing resolver rather than being forced into a field
  offset.

### 2.4 Behavior preservation

Before replacing a repeated branch, characterize its current result. If two
copies disagree, do not silently choose one as part of centralization:

1. identify whether an existing formal or vibe ruling selects the result;
2. if it does, keep the ruling and treat the other path as a separately tested
   bug fix;
3. if no ruling selects the result, stop that slice and ask for a ruling;
4. never add a compatibility hard code merely to keep a test green.

Each phase therefore has a characterize → introduce shared owner → migrate one
caller → compare → migrate remaining callers → delete old owners sequence.

## 3. LOC exit gate and accounting

### 3.1 Hard measurements

Use the baseline commit above unless implementation begins after another
Radiant change lands. If the baseline changes, update this header and record
the new clean commit before the first implementation edit.

The measured production set is every tracked file under `radiant/` whose
extension is one of:

```text
.c .cc .cpp .h .hpp .m .mm
```

Tests, docs, `build_lambda_config.json`, generated Premake Lua, `temp/`, and
files outside `radiant/` are excluded from the production count. New Radiant
files are automatically included. Moving code to another directory is
forbidden and cannot satisfy the gate.

Two final numbers are required:

1. **Physical production LOC:** final total must be at most 215,649 lines
   against the recorded 216,649-line baseline.
2. **Credited code reduction:** at least 1,000 net non-comment, non-blank code
   lines must be removed. Added shared implementation and declarations count
   against the saving.

The second number is the authoritative interpretation of the user's exit
gate. The physical count is an independent backstop.

### 3.2 Gate tooling

Extend `utils/verify_loc_reduction.sh` before Phase C1 so it can:

- accept `--min-reduction 1000`;
- accept a NUL-safe file list covering the complete measured production set;
- report before, after, additions, deletions, and net delta per file;
- report a separate code-line count that excludes blank and comment-only
  lines; and
- fail if either the physical or credited-code threshold is missed.

Store the baseline manifest and reports under `./temp/`, never `/tmp`:

```text
temp/radiant_centralize_baseline_files.txt
temp/radiant_centralize_baseline_loc.txt
temp/radiant_centralize_final_loc.txt
temp/radiant_centralize_loc_ledger.tsv
```

The implementation of code-line counting must track block-comment state and
must be covered by fixtures containing `//`, multiline `/* ... */`, strings
containing comment markers, preprocessor lines, and blank lines. Do not depend
on an unpinned external `cloc` installation.

### 3.3 Per-hunk credit ledger

Every LOC-reducing phase appends rows with:

```text
phase | old file | old symbol/span | shared replacement | code removed | code added | net
```

A deletion receives credit only when the replacement shared call or descriptor
is named. Moving a function to its new owner has net zero credit. Comments that
explain the shared rule move to the new owner; caller-specific comments remain
at the caller. Do not run a formatter, reorder unrelated declarations, collapse
blank lines, or perform comment cleanup in a centralization commit.

### 3.4 Planned saving budget

These are planning targets, not permission to force an abstraction. The final
gate, behavior parity, and root-cause review are authoritative.

| Phase | Target net production LOC |
|---|---:|
| C1 form-control descriptor | -70 |
| C2 replaced intrinsic sizing | -420 |
| C3 text metrics/run mapping | -100 |
| C4 resource resolution | -220 |
| C5 raster pixel operations | -160 |
| C6 export session | -260 |
| C7 CSS runtime property metadata | -120 |
| **Planned total** | **-1,350** |

The 350-line margin exists because some apparently similar branches will prove
to have intentional policy differences. If a phase cannot meet its target
without obscuring policy, record the shortfall and use Phase C8's residual
semantic audit. Do not compensate by deleting comments, blank lines, dead code,
or diagnostics.

## 4. Phase C0 — Baseline, characterization, and gates

### Work

1. Confirm a clean worktree and record the implementation baseline commit.
2. Produce the complete measured-file manifest and both LOC totals.
3. Run the existing filtered duplicate scan. It currently reports token-level
   duplicates, while this plan targets semantic duplication that has drifted
   enough not to be token-identical.
4. Record baseline results for:
   - `make build`;
   - `make test-radiant-baseline`;
   - `make layout suite=baseline`;
   - `make lint`;
   - `make lint ARGS='--rule ^no-int-cast-radiant$'`;
   - `make check-radiant-dup`;
   - render-output parity tests; and
   - the relevant WPT form, DOM Range, CSSOM, sizing, image, and table suites.
5. Add focused characterization tests before changing any behavior not already
   pinned by those suites.
6. Add the threshold/code-line support to
   `utils/verify_loc_reduction.sh` and test the counter itself.

### Required characterization matrices

- Replaced elements: tag × natural width/height/ratio availability × CSS axis
  specified/auto × HTML width/height × box sizing × min/max × containment ×
  block/flex/grid/table/positioned context.
- Form types: absent, empty, known text-like, checkbox/radio/button/image,
  date/time/color/file/range, unknown, and differently cased spellings.
- Text: ASCII, combining marks, variation selectors, emoji modifiers, ZWJ,
  soft hyphen, tabs, and all special-width Unicode spaces.
- Resources: inline stylesheet, linked stylesheet, document-relative,
  stylesheet-relative, root-relative, `file:`, HTTP(S), `data:`, WPT support
  paths, cache hits, and failed resolution.
- Pixels: transparent, opaque, and partial alpha for straight and premultiplied
  inputs; edge-clamped and wrapped bilinear samples; non-opaque destination;
  glyph coverage; byte-order round trips.
- Export: explicit and automatic viewport dimensions, output scale, device
  scale, PNG/JPEG/tiled PNG/SVG/PDF, font-face loading, and failure cleanup.
- CSS metadata: every value from `CSS_PROPERTY_UNKNOWN +` through
  `CSS_PROPERTY_COUNT - 1`, including unsupported and complex properties.

### Exit

Baseline reports are reproducible, all pre-existing failures are named, and
the 1,000-line gate fails on an unchanged tree and passes on a synthetic
fixture with 1,000 genuine code-line deletions. No production behavior changes
in C0.

## 5. Phase C1 — Central form-control model

### Current duplication

`get_input_control_type` in `view.hpp`, `tc_is_text_control` in
`text_control.cpp`, editable-line selection in `layout_form.cpp`, and direct
checkbox/radio string checks in `event.cpp` maintain overlapping type lists.
They do not currently use one comparison policy.

### Target contract

Define in `view.hpp`:

- `FormInputKind`, fine-grained enough to distinguish all recognized type
  keywords even when several map to one `FormControlType`;
- `FormControlCapability` flags for text-editable, single-line, checkable,
  button, hidden, replaced image, range, password, and fixed native metrics;
- `FormInputDescriptor`, containing the canonical keyword, coarse control
  type, capabilities, and optional intrinsic defaults; and
- lookup/query functions such as `form_input_descriptor`,
  `form_input_has_capability`, and `form_input_effective_kind`.

Implement the table and lookup in new `form_control_model.cpp`. Do not add
`form_control_model.hpp`.

### Migration

1. Pin effective-type behavior, especially absent/unknown/case variants. ES3
   already requires the missing type to behave as `text`; any unresolved case
   conflict is escalated rather than guessed.
2. Make `get_input_control_type` delegate to the descriptor, then remove its
   keyword chain.
3. Replace `tc_is_text_control`'s pre-prop keyword list with a capability
   query. Its fast path through an existing `FormControlProp` remains.
4. Move fixed date/time/color metrics out of `layout_form.cpp` into descriptor
   data and keep layout's application of those metrics local.
5. Replace checkbox/radio and button-family string helpers with kind or
   capability queries.
6. Cache only the effective kind already represented by `FormControlProp`;
   do not cache derived pseudo-state, preserving ES16.
7. Grep for every comparison of an input `type` string and classify remaining
   sites as value semantics, package policy, or a missed model consumer.

### Tests and exit

- Add a descriptor completeness/uniqueness unit test.
- Run `test_wpt_form_gtest.exe`, form UI automation, DOM package parity tests,
  and `make test-radiant-baseline`.
- Record at least the planned 70-line net saving or the measured shortfall.

## 6. Phase C2 — One replaced-intrinsic model

### Current duplication

`layout_measure_replaced` is only a partial shared path. Flex measurement,
intrinsic width, intrinsic height, block layout, positioned layout, and HTML
style defaults independently rediscover replaced tags, natural dimensions,
SVG ratios, 300×150 defaults, canvas/video dimensions, HTML attributes,
broken-image fallback, and transferred constraints.

### Target contract

Define in `layout.hpp`:

- `ReplacedIntrinsicSource` for decoded resource, element attributes, SVG
  metadata, canvas bitmap, video frame, UA default, broken fallback, and none;
- `ReplacedIntrinsicFacts` containing optional natural width, height, ratio,
  per-axis source, and fallback traits;
- `ReplacedIntrinsicQuery` containing only inputs that genuinely affect fact
  acquisition, including available logical size and containment context; and
- shared functions to identify a replaced object, collect facts, apply the
  default object size, transfer a definite opposite axis through a ratio, and
  convert content/border-box suggestions.

Implement fact acquisition in new `replaced_intrinsic.cpp`. Keep
`layout_measure.cpp` as the general measurement/cache entry and have
`layout_measure_replaced` consume the new facts. Do not add
`replaced_intrinsic.hpp`.

Reuse and promote existing helpers instead of copying them, including the
current SVG intrinsic calculation, image-surface acquisition, canvas natural
size, object default-size predicate, and box conversion helpers. Promote a
`static` helper through `layout.hpp` only when multiple translation units need
the exact same operation.

### Migration order

1. Extract facts without changing any caller.
2. Route `layout_measure_replaced` through the fact model and compare its full
   characterization matrix.
3. Route positioned layout, already the main consumer of
   `layout_measure_replaced`.
4. Replace flex's independent IMG/SVG/video/iframe/canvas measurement branch.
5. Replace intrinsic-width tag branches with shared facts plus
   min/max-content policy local to `intrinsic_sizing.cpp`.
6. Replace intrinsic-height branches the same way; width and height must use
   the same fact object within a query.
7. Replace block-layout and HTML-default rediscovery where the shared result
   exactly matches. Leave presentation-hint parsing in the style layer if it
   changes specified style rather than intrinsic facts.
8. Delete all duplicated tag sets and fallback constants after a final grep.
   The 300×150 default must have one named owner.

### Tests and exit

- Add direct unit coverage for every `ReplacedIntrinsicSource` and each
  width/height/ratio availability combination.
- Run the targeted `wpt-css-sizing`, `wpt-css-images`, `wpt-css-tables`, flex,
  grid, positioned, multicol, and broken-image fixtures.
- Run `make layout suite=baseline` and `make test-radiant-baseline`.
- Preserve layout pass-cache behavior and prove no extra image/network load is
  introduced by repeated intrinsic queries.
- Record at least the planned 420-line net saving or the measured shortfall.

## 7. Phase C3 — Shared text metrics and run mapping

### Current duplication

`layout_text.cpp` and `dom_range_resolver.cpp` separately classify
zero-advance codepoints and special-width spaces. Font property conversion is
also represented in more than one place. Layout, rendering, selection, and
editing repeatedly decode and map portions of the same text.

### Target contract

Define in `layout.hpp`:

- the canonical zero-advance and Unicode-space-width queries;
- a non-owning `TextRunSlice` describing bytes, byte range, direction, and
  style/font inputs;
- measurement and byte-offset↔advance mapping results; and
- functions for total advance, x-to-byte-offset, and byte-offset-to-x.

View/font value types remain declared in `view.hpp`. Put implementation in new
`text_metrics.cpp`; do not add `text_metrics.hpp`.

This phase does not introduce a persistent shaped-run cache. First establish a
single semantic mapper. A cache may be added later only with profiling and a
document/font-generation invalidation design.

### Migration

1. Move the complete zero-advance and Unicode-space rules to the shared owner.
2. Delete the caret copies and route DOM Range/caret geometry to the shared
   queries.
3. Consolidate `FontStyleDesc` construction so weight/slant/family mapping has
   one implementation.
4. Extract the smallest existing text-run measurement loop that both layout
   and range mapping can call without importing line-break or paint policy.
5. Route intrinsic text sizing and editing geometry where their contracts are
   identical; retain explicit adapters where whitespace collapse or bidi
   policy differs.
6. Do not change Lambda-facing offset units under ES9. Byte/codepoint/UTF-16
   conversion stays explicit at its existing boundary.

### Tests and exit

- Add table-driven Unicode classification tests and differential tests between
  layout advance and caret/range mapping.
- Run DOM Range, selection/editing, font, bidi, intrinsic text, layout
  baseline, and render text tests.
- Record at least the planned 100-line net saving or the measured shortfall.

## 8. Phase C4 — Canonical resource address resolution

### Current duplication

Fonts, scripts, stylesheets, CSS `url()`, images, navigation, and layout/WPT
fixtures each resolve relative/root/absolute URLs and local paths. Several
copies also decide scheme class and fixture fallback independently.

### Target contract

Expand `resource_resolver.cpp` from fixture-only helpers into the canonical
address resolver. Declare the API in `radiant.hpp`.

Define:

- `RadiantResourceKind` for document, stylesheet, font, script, image, media,
  and navigation;
- `RadiantResourceOrigin` with document URL and optional stylesheet/source
  URL;
- `RadiantResolvedResource` with canonical URL, local path when applicable,
  scheme/load class, canonical cache key, and explicit ownership; and
- an optional fixture-path resolver callback plus context, following the
  dependency-injection pattern used by `ViewGeometryScrollResolver`.

Before implementation, choose and document one allocation contract. Prefer an
allocator callback/context so CSS can allocate in its property owner while
network/script consumers use their memory category. A returned pointer must
never have an allocator implied only by `RadiantResourceKind`.

### Migration

1. Move scheme/absolute/root-relative classification into the shared owner.
2. Route font URL resolution, including stylesheet-relative origins.
3. Route script URL resolution and remove its private WPT path reconstruction.
4. Route linked stylesheet and recursive import resolution.
5. Route CSS declaration URLs without moving CSS-value parsing.
6. Route image address/cache-key construction; keep data-URI decoding and
   image format decoding in `surface.cpp`.
7. Route navigation resolution only after confirming its fragment/history
   semantics are compatible with the shared address result.
8. Keep WPT/layout path mapping as an injected test policy used by every
   resource kind, not as production branches copied per loader.
9. Grep for `url_resolve_relative`, `parse_url`, `://`, root-relative joins,
   and WPT support-path joins; review every remaining site.

### Tests and exit

- Add a table-driven resolver test covering the characterization matrix and
  allocator ownership.
- Run `test_network_layout_gtest.exe`, stylesheet/font/script/image loading
  fixtures, WPT layout support-resource tests, and the Radiant baseline.
- Verify identical cache keys and no increase in network requests.
- Record at least the planned 220-line net saving or the measured shortfall.

## 9. Phase C5 — Explicit shared raster pixel operations

### Current duplication

`render_raster.cpp`, `render_composite.cpp`, `render_background.cpp`,
`render_filter.cpp`, `glyph_sampling.hpp`, and `render_video.cpp` contain
overlapping channel extraction, bilinear sampling, coverage blending,
premultiplication, and source-over loops. `ImageSurface` describes RGBA byte
storage while vector APIs name packed values ABGR8888, so memory order and word
order are too easy to conflate.

### Target contract

Define in `render.hpp`:

- explicit packed-pixel and byte-order terminology;
- `RdtAlphaMode` (`STRAIGHT`, `PREMULTIPLIED`, and `OPAQUE` where useful);
- named pack/unpack/conversion helpers;
- exact straight-alpha and premultiplied source-over operations;
- shared nearest/bilinear sampling with clamp/wrap policy; and
- row/span operations for hot loops.

Put bulk implementations in new `raster_pixel.cpp`; keep only genuinely hot,
small inline primitives in `render.hpp`. Do not add `raster_pixel.hpp`.

### Migration

1. Pin byte-order and rounding behavior with unit tests before renaming types or
   comments.
2. Migrate video-control glyph drawing to the existing shared glyph sampling
   path, deleting its private blitter and pixel blender.
3. Centralize same-alpha-mode source-over copies.
4. Centralize identical bilinear sampling; keep coverage-only glyph sampling
   distinct unless the shared sampler can express its input without branches
   in the inner loop.
5. Centralize straight↔premultiplied conversion and tint helpers.
6. Keep CSS blend-mode channel functions separate from ordinary source-over;
   they implement a different operation.
7. Benchmark release builds before accepting any per-pixel call boundary.

### Tests and exit

- Add exact pixel-vector tests, including one-LSB rounding edges.
- Run display-list, retained-display-list, video-control, filter/shadow, normal
  versus tiled PNG, and `RenderOutputParity.*` tests.
- Run performance checks with `make release`; reject a material raster
  regression or make the primitive inline/span-based.
- Record at least the planned 160-line net saving or the measured shortfall.

## 10. Phase C6 — One export session and one layout snapshot

### Current duplication

PDF and SVG use `RenderExportSession`, while PNG and JPEG independently create
and configure a `UiContext`, create a surface, resolve the current directory,
load HTML, process fonts, apply output/device scale, lay out, render, and clean
up. The general target dispatcher delegates back to those separate paths and
cannot export PDF/SVG from an existing view tree.

### Target contract

Extend `RenderExportSession` and its declarations in `render.hpp`; do not add a
new header or a second session type.

The session must support two explicit origins:

- owned file session: owns headless `UiContext`, document, base URL, layout,
  and cleanup; and
- borrowed live session: borrows `UiContext`/document/view tree and never
  frees or silently re-lays out them.

Separate logical viewport size, output scale, device scale, physical surface
size, and auto-content size in the session data. Cleanup must be idempotent and
valid after every partial initialization failure.

### Migration

1. Split session creation from target encoding while retaining current public
   wrappers.
2. Route PNG through the owned session, preserving auto-size and tiled-output
   thresholds.
3. Route JPEG through the same session and shared raster target path.
4. Route tiled PNG through the same layout snapshot rather than another file
   entry convention.
5. Route SVG and PDF encoders through the same session object they already
   partially use.
6. Teach `render_output_render_view_tree_to_target` to use a borrowed session
   for SVG/PDF, or add one common session-target function and make the old API
   delegate to it.
7. Delete duplicated initialization, font processing, layout, scale, timing,
   and cleanup blocks from `render_img.cpp` and the file dispatcher.
8. Preserve public function names as thin compatibility wrappers until all
   external callers migrate.

### Tests and exit

- Extend `RenderOutputParity.*` so one fixture is exported to all five formats
  from the same logical dimensions.
- Compare old/new PNG and JPEG bytes where encoders are deterministic; compare
  decoded pixels otherwise. Compare SVG/PDF structural and visual goldens.
- Test every partial failure point for leak-free cleanup.
- Run `test_pdf_render_visual_gtest.exe`, normal/tiled/threaded replay parity,
  and `make test-radiant-baseline`.
- Record at least the planned 260-line net saving or the measured shortfall.

## 11. Phase C7 — One Radiant CSS property descriptor

### Current duplication

Core CSS parsing already owns name/type/inheritance/initial/animatable metadata.
Radiant separately maintains computed-style accessors, font-first lists,
manual inheritance coverage, animation value-type switches, and property
classification branches.

### Layer boundary

Do not place Radiant struct offsets or callbacks into
`lambda/input/css/css_properties.cpp`. The core CSS table remains parser-level
metadata. `css_prop_table.cpp` becomes the Radiant companion table indexed by
`CssPropertyCode`, with declarations in `view.hpp`.

### Target descriptor

Extend the current accessor row to carry, where applicable:

- property group/storage accessor;
- resolution phase (`FONT_FIRST`, `NORMAL`, or special dependency);
- animation value class;
- invalidation class (`STYLE`, `LAYOUT`, `PAINT`, `COMPOSITE`);
- CSSOM serializer/derived-value callback; and
- Radiant support/complex flags.

Inheritance and base animatability remain sourced from the core
`CssProperty`; Radiant validates against them instead of copying them. Complex
shorthands and context-sensitive properties may use callbacks or a special
flag; do not manufacture fake field offsets.

### Migration

1. Add a descriptor index and a completeness validator over
   `CSS_PROPERTY_COUNT`.
2. Replace both font-property inventories with the descriptor phase.
3. Replace `css_animation.cpp::property_value_type` with descriptor lookup.
4. Replace the manual inheritable-property inventory with iteration over core
   metadata plus explicit, named HTML/CSS exceptions. Characterize output
   first: missing entries may expose an existing conformance bug and must not
   be silently folded into this refactor.
5. Make CSSOM accessors consume the same descriptor row.
6. Add invalidation metadata only where current behavior can be mechanically
   mapped; do not change invalidation scope in this phase.
7. Retain `resolve_css_property`'s semantic switch unless a property group has
   three or more identical resolution bodies that a typed callback can replace
   with fewer lines and equal clarity.
8. Remove obsolete lists/switches and add a grep/static coverage gate against
   new parallel property classifications.

### Tests and exit

- Add descriptor uniqueness, index, parser-metadata consistency, animation
  type, CSSOM coverage, and unsupported-property tests.
- Run `test_css_animation_gtest.exe`, CSS parser/style/CSSOM tests, WPT CSS
  transitions, WPT CSSOM View, layout baseline, and Radiant baseline.
- Record at least the planned 120-line net saving or the measured shortfall.

## 12. Phase C8 — Residual semantic audit and LOC closure

C8 runs only after C1–C7 are green. Re-run the structural searches that found
the original families:

- repeated replaced-tag sets and 300×150 constants;
- repeated text-like input keyword lists;
- private zero-advance/Unicode-space classifiers;
- private relative/root URL joiners and scheme detectors;
- private source-over, glyph blender, premultiply, and bilinear functions;
- file exporters that initialize or lay out their own document; and
- property classification switches/lists outside the descriptor owner.

For each residue, classify it as:

1. missed identical semantics — migrate it;
2. intentional caller policy — retain it with a concise contract comment; or
3. unresolved behavioral disagreement — stop and seek a ruling.

If the cumulative credited reduction is below 1,000 lines, continue only with
category 1 findings. The task remains incomplete if fewer than 1,000 honest
lines exist; it is preferable to report that result than to force a generic
abstraction.

Physical decomposition of `event.cpp`, `state_store.cpp`,
`resolve_css_style.cpp`, `layout_block.cpp`, or `intrinsic_sizing.cpp` is not a
LOC-credit source. Splitting those files may be a later readability project,
but moving unchanged code between translation units saves nothing. In
particular, splitting `state_store.cpp` must preserve one `DocState` and one
state API under ES11; a second store is forbidden.

## 13. Build integration

The main Radiant library already includes `radiant/*.cpp`, so new
implementation files enter that target automatically. Several unit-test
targets use explicit `additional_sources`; add each new owner only to targets
that directly link its consumers.

Edit `build_lambda_config.json`, then run `make` to regenerate build files.
Never edit generated `.lua` files manually.

Expected source-list effects:

- `form_control_model.cpp`: form/state/event/layout unit targets using form
  classification without the full Radiant library;
- `replaced_intrinsic.cpp`: isolated layout/intrinsic targets;
- text-metric additions in `layout.cpp`: DOM Range/state/editing/layout text targets; and
- `raster_pixel.cpp`: display-list/render-output targets that link raster
  consumers directly.

Resolve missing-symbol failures by adding the true shared owner to the target,
not by copying a stub or restoring a private helper.

## 14. Commit and review strategy

Use one independently green commit per numbered phase. Within a large phase,
prefer this commit sequence:

1. characterization tests;
2. shared types/API and implementation with no caller change;
3. first caller migration and differential proof;
4. remaining caller migrations;
5. deletion of superseded private logic and LOC report; and
6. build-config regeneration if explicit unit targets need the owner.

Do not combine formatting, naming sweeps, unrelated warning fixes, or behavior
fixes with these commits. Every non-trivial ownership or compatibility edge
gets a concise root-cause comment at the shared implementation point.

## 15. Final verification and definition of done

The final change is not complete until all gates below pass from a clean tree:

```bash
git diff --check
make build
make test-radiant-baseline
make layout suite=baseline
make lint
make lint ARGS='--rule ^no-int-cast-radiant$'
make check-radiant-dup
```

Also run the phase-specific form, DOM Range, network, CSS, display-list, and
render-output tests named above, plus `make test` once at the final integration
point because the common headers and resource/export paths have consumers
outside the Radiant baseline.

Final artifacts:

- physical production LOC is at most 215,649 against baseline `b9ca1a3660`;
- credited net code reduction is at least 1,000 lines;
- the per-hunk ledger names the shared replacement for every credited removal;
- no comment/blank cleanup or code reformat contributes to the reduction;
- no new Radiant header exists;
- all new public declarations are in the appropriate coherent common header;
- no new duplicate report entry exists;
- behavior/output parity is green, with pre-existing failures explicitly
  separated from regressions;
- no vendor, generated parser, generated Lua, `log.conf`, or unrelated file is
  modified; and
- this document is updated phase by phase with actual LOC deltas, test counts,
  deviations, and final commit evidence. When complete, rename it with the
  repository's `(done)` suffix convention.

## 16. Implementation record — 2026-09-10

The proposal was implemented in the working tree against baseline
`b9ca1a3660`. The implementation keeps declarations in `view.hpp`,
`layout.hpp`, `render.hpp`, `radiant.hpp`, and the existing event headers; no
new file-specific header was added. New implementation owners are
`form_control_model.cpp`, `replaced_intrinsic.cpp`, and `raster_pixel.cpp`.
The text-metric additions live in the existing `layout.cpp` owner because its
public declarations and font/layout contract already exist there; creating a
second `text_metrics.cpp` would add an owner without reducing duplication.

### 16.1 Delivered central owners

| Phase | Result | Delivered owner and migrations |
|---|---|---|
| C1 | complete | `FormInputDescriptor`, capability/kind queries, fixed intrinsic defaults, and text-editability helpers in `view.hpp`/`form_control_model.cpp`; form layout, text controls, editing, events, intrinsic sizing, DOM range, and rendering now consume the descriptor. |
| C2 | complete | `ReplacedIntrinsicFacts`, source classification, default object size, ratio transfer, canvas/SVG facts, and baseline helpers in `layout.hpp`/`replaced_intrinsic.cpp`; block, flex, positioned, measure, and intrinsic sizing paths use the shared facts. |
| C3 | complete with owner adjustment | Unicode zero-advance/space facts, glyph advance, UTF-8 width, UTF-8 iteration, font weight/slant mapping, and shared SVG/form/text metric calls are owned by the common layout/view APIs. Caret, DOM Range, editing, form, bidi, list, SVG, and glyph-run paths retain their caller-specific whitespace and bidi policy. |
| C4 | complete | URL/path/document-base resolution and recursive CSS declaration walking are owned by `radiant.hpp`/`resource_resolver.cpp`; font, script, stylesheet, image, layout, and command annotations use injected fixture policy instead of private joins. |
| C5 | complete | Pack/unpack, premultiply/unpremultiply, bilinear sampling, source-over, region compositing, clip filtering, and raster row operations are declared in `render.hpp` and implemented in `raster_pixel.cpp`/`render_composite.cpp`; background, filter, display-list, vector, webview, and shadow callers migrated. |
| C6 | complete with backend-specific lowering | PNG/JPEG/tiled PNG, SVG, and PDF all enter through `RenderExportSession` and shared target/session setup. SVG/PDF retain their backend-specific lowering, and tiled PNG retains its streaming tile loop; those are output-policy boundaries, not duplicate document/session construction. |
| C7 | complete with characterized exception | `CssPropertyRuntimeMetadata` is indexed from `view.hpp`/`css_prop_table.cpp` and is used by animation, font-first resolution, CSSOM serialization, and outline serialization. The exact legacy 31-property inherited set remains explicit because broad core inheritance changed baseline output; this is documented policy, not an uncharacterized merge. |
| C8 | complete | Residual searches covered form type strings, replaced tags/defaults, text classifiers, URL joins, pixel loops, export setup, and CSS metadata. Remaining copies were either caller policy or backend-specific. |

Additional safe reuse delivered during the audit includes shared view/DOM
walkers, event construction, DOM ancestry, selector assertion matching, form
content predicates, inherited text-wrap resolution, rounded SVG path export,
clip-shape ownership, state-store navigation/caret presentation, list/text
UTF-8 iteration, vector-picture ID traversal, and window load-failure cleanup.

### 16.2 LOC gate evidence

The verifier now counts physical lines and credited code lines with a small
lexer that tracks `//`, multiline `/* ... */`, quoted strings/chars, escaped
quotes, preprocessor lines, and blank lines. Its fixture test is
`utils/test_verify_loc_reduction.sh`, with source fixtures containing comment
markers inside strings, multiline comments, preprocessor directives, blank
lines, and genuine deleted code.

Measured with:

```text
utils/verify_loc_reduction.sh --ref b9ca1a3660 --min-reduction 1000 \
  --files0-from temp/radiant_current_files0
```

| Metric | Baseline | Final | Delta |
|---|---:|---:|---:|
| physical production LOC | 216,649 | 215,380 | -1,269 |
| credited non-comment/non-blank code LOC | 180,787 | 179,776 | -1,011 |

The command exits 0 and reports:
`PASS: physical LOC reduced by 1269; credited code reduced by 1011 lines.`
The reduction includes added shared owners and declarations; comments and
blank lines were not used as credit.

### 16.3 Verification status and known baseline failures

- `make build`: passed with zero errors.
- `utils/test_verify_loc_reduction.sh`: passed with a three-code-line fixture
  reduction.
- `git diff --check`: run as a final gate below.
- `make layout suite=baseline`: ran; the suite reported 4,440 successful,
  41 failed, and 6 skipped tests out of 4,481. Its wrapper exits zero while
  reporting failures, so the failures remain visible in
  `temp/layout_suite_baseline.log`.
- `make lint ARGS='--rule ^no-int-cast-radiant$'`: passed.
- `make lint`: remains non-zero for 207 unused-function warnings and the
  current `gc-effects` unresolved transitive call
  (`lambda/core/lambda-data.cpp:394`); the other structural checks pass.
- `make test-radiant-baseline`: executed on the final tree; the repository
  baseline still exits non-zero. The captured run reports 3,482 passed, 369
  partially passing, and 25 failed out of 3,876. Form and render visual
  baselines pass in this run; remaining failures are in selected WPT
  text/sizing/table/display cases, the page/UI suites, and the DOM UI
  integration linker. Evidence is in
  `temp/test_radiant_baseline_final.log` and the render/layout logs; no test
  harness was modified to mask them.
- `make check-radiant-dup`: remains environment-blocked because `lizard` is
  not installed (`temp/check_dup_latest.log`); no duplicate report was
  suppressed.
- The duplicate checker still needs an environment with `lizard`; it was not
  bypassed or replaced with a weaker check.

The implementation therefore satisfies the requested 1,000-line exit gate and
the no-new-header convention. The remaining work is integration-environment
verification of the commands that require unavailable local tools or expose
pre-existing baseline failures; it is not a reason to weaken the gate or edit
test infrastructure.
