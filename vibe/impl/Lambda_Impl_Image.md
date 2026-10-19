# Lambda Image Package — Implementation

**Started:** 2026-10-16. **Status:** initial native `u8` profile implemented;
photo-scale milestone outstanding. Implementation authorized by the user.
Design: [Lambda_Pkg_Image](../Lambda_Pkg_Image.md).
Public API: [Lambda_Image](../../doc/Lambda_Image.md).

The selected schema uses still-image root stacks, frame-owned animation
stacks, nested stacks for groups and image-scoped `assets`. The implementation
preserves **S9.1.2 / S9.2.2** snapshots, **D7.4.5v2** virtual element semantics
and **D5.3.3** precise rooting. This is an implementation record; the proposal's
full target vocabulary is not a claim that all Photoshop behavior is supported.

## Delivery boundaries

| Milestone | Delivered behavior | Remaining limits |
|---|---|---|
| 1. Working Raster | Native owned `u8`, dense inputs and shared COW tiles for region/paint edits; gray/gray-alpha/RGB/RGBA; Raster Velmt; canonical materialization; declared non-enumerable channel projections; array/channel/region snapshots; PNG/JPEG import | High-depth storage is outstanding |
| 2. Editable composition | Foreground-first layers; isolated nested stacks; integral placement; visibility/opacity; gray alpha masks with independent extents/default/inversion; crop/resize/adjust/rotate/flip/fill/stamp; tree replacement/reordering; editor snapshot history reuse; requested-region composition | One active mask per node; normal source-over only; retained transforms and enabled effects need baking |
| 3. Animation | Frame-owned stacks; GIF patch/disposal normalization; exact zero delays and total plays; frame editing; full-frame GIF output with exact local palettes | At most 255 opaque colors plus transparency; binary alpha and exact centisecond timing; no implicit quantization; other temporal formats need separate profiles |
| 4. PSD interchange | Version-1 RGB/grayscale8 read; raw/RLE/ZIP/prediction decode; raw layered write; Unicode names, groups, empty layers, user masks; regenerated IDs/composite; export report; original-byte session passthrough; `input(...,'psd')`/automatic detection and runtime adoption | PSB, native text/smart objects/effects, pass-through groups, group masks, inverted-mask writing and opaque block reuse are not qualified; strict rendering/export rejects unsupported features |
| 5. Photo scale/precision | Photos M2 adds shared COW tiles, native painting/coverage and true region composition | Effect halos, high-depth adapters, ICC conversion and broader blending remain work; photo-scale budgets are unqualified |

## Implementation map

- `lib/raster.*`: context-free owned samples, dense region reads and normal
  source-over composition. Colors composite in linear light and return straight
  sRGB samples. There is one authoritative pixel owner.
- `lambda/runtime/image_raster.*`: GC-accounted native Raster Velmt, traced
  metadata, declared channel interface, validated snapshots and replacement
  edits. A checked write publishes once; failed validation publishes nothing.
- `lambda/runtime/image_composite.cpp`: bounded composition, frame selection,
  geometry, masks and strict unsupported-feature checks.
- `lambda/runtime/image_document.cpp`: shared codec adapters, immutable source
  capture, timing normalization, format capability profiles and encoding.
- `lib/image_gif_write.cpp`: exact palette writer over the existing giflib
  encoder sources. No vendored source was changed.
- `lib/image_psd.*`: bounded byte reader/writer shared by the runtime and
  static Input route. Unknown visual dependencies remain in the source asset
  and block strict output rather than being silently erased.
- `lambda/runtime/image_psd.cpp` and `lambda/input/input-psd.cpp`: runtime
  owners versus Input-owned materialized samples. The latter never embeds
  untraced GC Velmts in an arena (**D4.1.2 / D5.3.3**).
- `lmd/package/image.ls`: public constructors, inspection, editing, composition,
  interchange and capability/report wrappers (**D7.2.4v3**).
- `lmd/package/image/edit.ls`: sessions reuse `lambda.editor.mod_history`;
  adjacent updates sharing a group undo as one stroke. Original-byte export
  requires equality with the session's captured original document.

ICC-tagged samples have an `unknown` working identity until actual color
conversion is available; original source bytes and extracted PSD ICC bytes
are assets. Strict output does not label those samples sRGB. PSD opacity
and mask defaults round to the format's eight-bit fields. Export rewrites
supported records and regenerates layer IDs; it does not claim lossless
opaque-block preservation. A layered file without a merged preview remains
editable; a merged-only file imports as one raster layer.

## Root causes resolved during qualification

- The GIF helper searched only file-level loop extensions, but giflib can
  attach pre-image extensions to SavedImages. One shared lookup now scans
  both locations and distinguishes an absent extension from infinite looping.
  Encoded delays are retained separately from the legacy playback clamp.
- GIF disposal-to-background uses transparent clearing when the background
  index is marked transparent. Offset patches and keep/background/previous
  disposal match Pillow reference pixels.
- StrBuf allocation is libc-owned; returning it through the image API's
  `image_free` caused an allocator mismatch. The writer now transfers bytes
  through the image allocator's ownership boundary.
- Imported MIR boxed calls omitted the CW33 borrow-home protocol, so a
  procedural Raster replacement stayed in the callee. Imported and dynamic
  boxed edges now reuse the same preparation/transport helpers, then reload
  the caller's binding (**S9.1.3 / D8.1.1**). Positional, named and defaulted
  region-write calls are covered.
- Numeric-array Input construction shares a dense arena factory with deep
  copy. Shape extraction/validation moved from the runtime vector unit into
  the data layer so Input-only executables do not acquire a runtime link
  dependency (**D1.9 / D4.1.2**).
- A PSD group mask must mark the group unsupported while its coverage is
  outside the decoder/render profile. A native regression prevents an imported
  masked group from silently becoming an unmasked group.

## Verification — 2026-10-16

Scratch and generated artifacts stay in `temp/`; no performance claims use
this debug build.

- Native `test_image_io_gtest`: **30/30 pass**, including all existing codec
  checks, four PSD compression modes, truncation/deflate-tail rejection,
  animated GIF transparency/timing/plays and unsupported PSD group masks.
- Image package fixtures: **nine scripts**, each checked normally and with
  `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`. Every script has its
  expected `.txt`; cases cover snapshots, failed writes, composition, masks,
  history, frames, PSD/GIF round trips, materialization/adoption and source
  passthrough.
- Focused GTest discovery over image/physics/`cow_var` fixtures: **37/37 pass**.
  The physics checks also cover extraction of the shared rooted value builder.
- Pillow independently opens generated PSD/GIF output, verifies composite
  samples and checks GIF zero delay, 30 ms delay and finite loop metadata.
  Permanent disposal goldens were generated independently with Pillow.
- `git diff --check` and fixture/document checks pass.
- `make test-lambda-baseline` ran to completion: **2,112/2,112 Input checks
  pass**, **5,184/5,394 runtime checks pass**. All 248 Lambda MIR-emission
  checks pass. The aggregate is not green. Failures include JS tests expecting
  legacy emission against MVP-LMD, JS ownership/regex expectations, a JS
  optimization timeout, three JS stress references, binary JS bridging,
  LaTeX corpus timeout and finder I/O expectations. No image fixture failed
  that run. These failures were left visible; tests/budgets were not weakened.

Local evidence: `temp/image_native_tests.log`, `temp/image_focused_gtest.log`,
`temp/image_lambda_baseline.log` and `test_output/*_results.json`.
The shared checkout contains unrelated ongoing edits; this implementation
preserves them and does not assert that the aggregate failures were proven
present in a clean baseline.

## PSD viewer follow-up — 2026-10-16

The package's PSD I/O initially had no connection to the CLI/window format
admission lists or document-loader router. The reported `view ...psd` command
therefore failed before reaching the image decoder.

The finite fix admits `.psd` case-insensitively and registers the stateless
`lambda.doc.image` transform (**D7.2.4v3**). It reads through `lambda.image`
and presents a fresh composition as an in-memory PNG in the existing image
document DOM. It also reaches the shared `layout`/`render` document route.
Unsupported rendering features remain checked; no merged-preview fallback
silently replaces editable content.

Acceptance cases: the supplied sunset PSD opens; the permanent masked/grouped
fixture shows its red/blue/red layers despite a deliberately stale green
merged preview; uppercase `.PSD` works; malformed PSD fails; neighboring PNG,
JPEG and SVG viewers keep working. Verification uses the new package golden,
headless pixel/geometry assertions and the required Lambda/Radiant gates.
Focused verification passes: seven native viewer cases (PSD, PNG, JPEG, GIF,
SVG and XHTML parity), 27 image-related Lambda goldens, and the new package
fixture under forced GC. The PSD's seven DOM/geometry/pixel assertions pass
both normally and under forced GC. Uppercase input succeeds; malformed input
returns nonzero. `layout` succeeds, and native `render` produces the expected
fixture pixels and an 800 × 500 sunset export identical to its package preview.

`make test-radiant-baseline` was attempted but stopped during linking:
`test_map_gtest` and `test_scene3d_unit_gtest` cannot resolve
`radiant_clipboard_mac_init` from the ongoing clipboard changes in this shared
checkout. This is a qualification limit; the PSD fix does not change those
sources or their build configuration. Logs are in
`temp/image_samples/view_{focused_native,focused_lambda,psd,radiant_baseline}.log`.

`make test-lambda-baseline` completed: Input **2,112/2,112** and runtime
**5,188/5,397** pass. The script runner is **1,606/1,607**, with
`binary_js_bridge` its only failure; every image fixture, including
`proc_image_view`, passes. The aggregate also reports JS emission/ownership/
optimization/regex failures, three JS stress references, and a PDF binary-schema
interpreter crash. The shared checkout has concurrent edits, so these totals
are qualification evidence rather than a clean-baseline attribution. Full log:
`temp/image_samples/view_lambda_baseline.log`. No unrelated tests or budgets
were changed by this fix.
