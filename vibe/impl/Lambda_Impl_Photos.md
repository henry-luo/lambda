# Lambda Photos — Implementation

**Started:** 2026-10-19. **Status:** layered editor with tiled painting, retained composition/projects,
retouch/actions, editable artwork/styles, channels, tabs, batch and incremental brush previews; aggregate gates have qualification limits, on
`codex/photos-20261019`, worktree `temp/photos-worktree/`.

Design: [Photos proposal](../Lambda_Demo_Photos.md). Existing contracts:
**S9.1.2 / S9.2.2** immutable snapshots, **S12.1.3** pure views and procedural
handlers, **D5.3.3** precise roots, **D4.5.2** owned pixel transport,
**D7.2.4v3** shipped package paths. No formal ruling changes.

## Active checkpoint

Current delivery includes M2 raster interaction, the M3 composition/project core
and the bounded M4 design/retouch/channel/workspace/action workflows. Remaining
profile extensions and M5 work are listed explicitly below. Initial
M0/M1 acceptance covered three-image placement, grouping/masks/movement, undo/redo
and strict layered PSD save/reopen with failed file operations preserving state.

Reuse `lambda.image` storage/composition/PSD and `lambda.editor.mod_history`.
The demo owns shell/file dialogs; generic commands and gestures live under
`lambda.editor.raster`. Keep the existing singular photo demo usable.

Verification budget: focused package goldens across execution tiers, PSD data
round trips and independent reader checks, one native event replay covering
retained controls/canvas and file failure, then required package baseline.
Build a worktree-local host; shared dependency links follow Developer Guide §7.

| Milestone | State |
|---|---|
| M0 reuse and measurement | Complete for the bounded initial profile; optimized timings below |
| M1 layered workspace | Implemented; focused qualification passed; broader failures below |
| M2 tiles, selections and painting | Implemented; native/package/UI and aggregate results recorded below |
| M3 transforms, reversible effects and rich persistence | Implemented core: affine, eight blends, clipping/pass-through, effects, histogram and versioned project data; focused checks below |
| M4 retouch, type/shapes, channels and actions | Bounded core implemented: retouch/actions, embedded-font multiline text, shapes/basic paths, three layer styles, saved coverage channels, tabs and sequential file batches; long-tail exclusions below |
| M5 high depth/color and scale qualification | Pending |

## Delivered behavior

The `lambda.editor.raster` facade has stable layer IDs, foreground-first tree
rows, checked commands, masks, grouping/ungrouping, adjacent-layer merging,
document/image resizing and shared history. Commands retain immutable snapshots
and restore targets on undo. Gesture previews record one completed move and
create no edit for a canceled or zero-distance drag. The native view adapter
retains its canvas while copying current composition pixels.

Photos supplies Open/Place, layer/property/history docks, mask targeting, editable
adjustments, fit/zoom/pan, keyboard/touch movement, protected document replacement,
atomic project/PSD saving and PNG export. `demo photos` is registered. The existing
singular photo demo and its attributed sample assets are reused without copying
its image-processing pipeline.

The command table is shared by dispatch, validation and history labels. File
procedures use statement-position handlers and capture successful replacements
through their success arm (**S7.6.7v4**); expression-position procedure handlers
do not provide a session value. PSD saving refuses editor locks because the
current image writer cannot preserve them.

Final command review reproduces and closes two gaps: insertion now checks a
destination group's locked ancestors, and lock/visibility values must be boolean.
Layer ancestry and parent paths reuse `mod_source_pos.path_is_prefix` and
`mod_doc.parent_path`, including movement and hit testing. The regression script
checks the locked destination and locked ancestor as well as invalid properties.

## Native ownership repair

The file-dialog replay exposed four leaked `FormControlProp` allocations. A
script can create a heap-owned form before layout; retained restyling called the
item-property teardown, cleared its only pointer and left that storage orphaned.
`radiant/view_pool.cpp` now distinguishes item-layout reset from final release
and retains the form owner during reset (**D4.5.1v4**). The new
`RadiantFormControls.ScriptCreatedFormSurvivesRetainedStyleReset` test failed
before the fix, then passed together with the existing open-select reset test.
Evidence: `temp/photos-controls-before.log`, `temp/photos-controls-after.log`.

The close-preview replay also found that the native guard rejected every
`lambda view` document: only the `lambda edit` launcher selected application
input routing. An explicit guard request from the presented top-level document
now selects that routing, including when the guard is initially unarmed. This
lets Photos own Escape after saving and protects native window close; packages
still own Save/Discard/Cancel (**D7.2.5 / S12.1.3**; detailed window contract in
[Radiant Edit Mode §7](../radiant/Radiant_Design_Edit_Mode.md#7-top-toolbar-and-application-input)).
Photos cancels pending preview/capture/frame state before checking committed
dirty content, so a clean document can close during a draft. The new lifecycle
replay failed five of seven assertions before the host repair and passes all
seven with forced GC/poisoning afterwards. Evidence:
`temp/photos-lifecycle-before.log`, `temp/photos-lifecycle-fixed-host.log`.

## M1 verification (historical)

- Three package/procedure golden tests pass in automatic and eager-JIT execution;
  forced GC and poisoned-free runs pass. The core and gesture scripts contain
  41 and 13 semantic checks, plus the procedural file fixture. The release host
  passes the three goldens with forced GC and poisoned frees as well.
- The original three native replays pass all 42 assertions with both the debug
  and release hosts; the added close-preview replay brings release coverage to
  four workflows and 49 assertions. Forced-GC/poison runs also pass. They cover three-image
  placement, grouping, masks, edits, undo/redo, layered save/reopen, PNG export,
  failed opens, unsaved replacement, cancellation, touch and keyboard/modal focus.
  The final allocation-tracked smoke teardown reports no leaks. Evidence:
  `temp/photos-package-final-review.log`, `temp/photos-package-auto-final.log`,
  `temp/photos-ui-final-review.log`, `temp/photos-ui-baseline-final.log`,
  `temp/photos/teardown.log`.
- Pillow independently opens `temp/photos/unmasked.psd` and verifies its extent,
  three layer names/order, offsets and merged pixels. The masked fixture also
  opens; Pillow does not expose its masked layer through its simple layer list,
  so it is used as a merged-pixel check rather than a complete mask oracle.
- Strict `interp` qualification is blocked by the shared image package's
  unsupported multidimensional index path. The existing unmodified
  `test/lambda/image_composition.ls` reproduces E501 before execution. Automatic
  and eager-JIT support are recorded separately; no test or interpreter policy
  was weakened. Evidence: `temp/photos-base-interp.log`.
- The runner's baseline suite list now includes `photos` explicitly (manifest
  tags alone do not include a named suite). Runner configuration tests pass with
  `TMPDIR="$PWD/temp" node test/test_test_run.js`. The CLI catalog lists Photos.
- `make lint ARGS='--rule ^no-int-cast-radiant$'` passes. Golden pairing,
  documentation links and `git diff --check` pass.

## M1 aggregate qualification limits

`make test-lambda-baseline` exits 2: input tests pass **2112/2112**, runtime
tests pass **5775/5776**, including all **1625/1625** core script tests and
**315/315** MIR forced-GC tests. The remaining failure is
`BinarySchemaTest.LambdaPdfOffsetsAndImageStreamsAreValidInBothTiers`:
`test/lambda/pdf/output_resources.ls` exits successfully but its expected stdout
substring is missing in both tiers. A serial rerun reproduces the failure.
Evidence: `temp/photos-lambda-baseline.log`, `temp/photos-pdf-control.log`.

`make run-radiant-baseline` with native graphics/local-server access exits 2:
the report records **5949 passed, 6 failed, 146 partial, 1 skipped**. All
**164/164** text-query tests (including the new ownership regression),
**208/208** view tests, **501/501** paged-media tests and **28/28** scene tests
pass. Failures remain visible:

- `PageLoad/PageLoadTest.LoadWithoutCrash/bootstrap_5_kitchen_sink_` reaches the
  existing 15-second limit during the parallel gate. Three later serial native
  loads pass in 3.71–3.80 seconds; this does not erase the aggregate failure.
- `CssCascadeMemory.PageLoadAndRecascadeStayWithinBaseline`: jQuery UI CSS-role
  live allocation is 381630 bytes against the 373940-byte budget.
- UI replays `svg_foreign_object`, `svg_foreign_object_css`,
  `svg_foreign_object_interaction` and `svg_foreign_object_namespaces` fail.

The broad UI run precedes explicit inclusion of the Photos suite; its new coverage
is qualified separately above. Layout/render runners use their existing partial
and expected-failure policy. The initial sandboxed Radiant attempt could not bind
its local server or use native graphics; the unrestricted rerun is the result
reported here. Evidence: `temp/photos-radiant-unrestricted.log`,
`temp/photos/bootstrap-controls.json`. These are outstanding qualification
failures; no baseline fixture, timeout or budget was weakened, and no claim is
made that all failures predate this branch.

After the application-window repair, the release host is checked again with
the full baseline UI selection plus Photos, `edit` and `note`: **694/701 workflows
pass** (697/704 GTests including the three scheduler checks). All four Photos
workflows and the explicit edit close/save/cancel cases pass. Besides the same
four SVG failures, the extra editing suite exposes
`edit_edit_md_math_scroll`, `edit_edit_md_view_only` and
`edit_edit_md_view_only_keys` failures. Evidence:
`temp/photos-ui-baseline-final.log`. This supplemental run is not a passing
aggregate gate and does not change the earlier Lambda/Radiant verdicts.

The final standalone native-view regression run passes **210/210 tests** with
the rebuilt release host, including the existing photo editor, file loading,
gameplay, 3D and teardown coverage. Evidence:
`temp/photos-native-view-final.log`. The final Photos-only replay passes all
**49 assertions** with forced GC and poisoned frees after command review;
automatic package execution and eager-JIT forced-GC goldens both pass.

## Optimized baseline measurement

`make build-release-compile` succeeds in the worktree. The benchmark refuses the
debug-only validator trace and records binary/package/scenario SHA-256 values in
`temp/photos/performance/results.json`. The measured host is macOS 26.3 arm64,
10 logical CPUs, automatic execution, without forced GC. Measured source revision:
`ed8df3a4f82063498276a06588420949fc54633c`;
binary SHA-256:
`350338320190f22ae2e5a588b93d4df42677e10c7bff4c2478b35e69fcb7a54e`.
Package source SHA-256:
`17ce97a86a426f858b2e6701800be9bf6b5265eea2619d68c700f36aaf25ae9d`.
This final measurement runs after the qualification subprocesses have finished.

Three separate processes each construct eight 1024×768 layers sharing an input
raster, move the active layer sixteen times, then encode a 28312680-byte PSD:

| Phase | Median |
|---|---:|
| Construct/place | 2566 ms |
| Sixteen moves including eager validation/composition | 8713 ms |
| PSD encode | 683 ms |
| Whole process | 12255 ms |

Maximum child-process RSS is **161988608 bytes (154.5 MiB)**. This is process
peak memory, not unique raster residency. The first process is cold; later
processes may use filesystem/compiler caches. No input-to-display latency is
measured, and no M5 latency/scale target is claimed. Full-composition validation
on each committed move is a measured cost for M2's region-processing work.

## M2 continuation — 2026-10-19

Shared `lambda.image` now provides sparse tiles backed by `ByteStorage`;
region/stroke writes detach touched tiles and preserve old Raster/channel
snapshots (**S9.2.2 / D4.1.4v5**). Dense inputs remain shared spans until edited.
GC accounting follows physical byte-store lifetime rather than charging each
history snapshot for the full logical image. Region composition allocates the
requested viewport and group scratch directly. PSD/GIF readers of Raster samples
use the same tile-aware accessors.

Native edit kernels implement rectangle/ellipse/even-odd polygon coverage,
connected/global color selection, add/subtract/intersect/invert, box-filter
feather, brush/eraser, selection-clipped fill, linear/radial gradients, extent
mapping and sample extraction/mixing. Stroke spacing carries across pointer
segments; flow accumulates coverage while stroke opacity applies once. Paint can
expand a layer or independent mask within canvas/buffer budgets, preserving
existing pixels and mask origin. Storage diagnostics are procedural because
refcounts can change after collection (**S12.1.1v3**).

The reusable editor exposes eleven tools, normalized settings, selection history,
selection-to/from-mask, mask invert/apply, copy/cut/paste and crop-to-selection.
The demo coalesces draft rendering to frames and presents selection coverage in
a separate retained canvas. Selection history does not dirty document pixels;
completed/canceled strokes share the existing transaction path (**S12.1.3**).

Focused evidence so far: native image I/O **34/34** pass, including cross-tile
sharing/detachment and allocation failure; image paint kernel goldens, procedural
storage diagnostics and four Photos package fixtures pass with forced GC and
poisoned frees. The new native painting replay passes **14/14 assertions**,
including actual red pixels, undo/redo, marquee-to-mask, mask erasing and PSD
reopen. All five Photos replays pass **63/63 assertions** with forced GC and
poisoned frees. Evidence: `temp/photos-m2-native.log`, `temp/photos-m2-targeted.log`,
`temp/photos-m2-ui-paint.log`, `temp/photos-m2-ui-all.log`.

At the M2 checkpoint, M3–M5 remained pending; subsequent entries supersede
retained transforms/effects, rich-project persistence and retouch/actions status.
The remaining-work inventory at that checkpoint included type/shapes,
actions/multiple documents, and high-depth/color/scale qualification. This
checkpoint does not claim complete PhotoCraft feature parity.

## M3 retained composition and editable persistence

The shared compositor now evaluates eight linear-light blend modes, source-local
six-value affine matrices with premultiplied interpolation, ordered retained
color/filter effects, base-alpha-preserving clipping chains and pass-through
stacks. Layer masks follow affine geometry. Group opacity/masks interpolate the
backdrop for pass-through evaluation. Group effects and pass-through clipping
bases require explicit isolation; specialized content remains unsupported.
Composition scratch admission includes filter/group/clipping buffers.

The raster package adds relative/matrix transforms, Transform gestures with one
history transaction, flips, rasterization, blend/clipping/isolation commands and
ordered effect editing. Applying a mask excludes retained effects from its alpha
bake, preventing double processing. Image resize conjugates retained affine
matrices, and merge/ungroup refuse combinations that would change backdrop blend
behavior silently (**S9.1.3 / S9.2.2**).

`lambda.image.project` packs raw binary raster samples into versioned Mark data,
retaining hidden RGB, gray channels, IDs, masks, locks, transforms/effects and
source assets. It checks a conservative payload estimate before packing, caps
encoded/decoded payloads at 128 MiB and rejects malformed/unknown versions.
Read/write are procedural and publication is atomic (**S12.1.1v3**); decode parses
data without evaluation. This first format has no compression/deduplication.
The PSD writer now explicitly rejects retained attributes it cannot serialize.

Photos provides project saving/opening, explicit PSD export, transform/blend and
effect controls, and a histogram derived from its presented raster. Quick
adjustments add editable effects. Focused package tests include 51 composition
checks and project file persistence, plus existing photo/image tests; 11 cases
pass under forced GC and poisoned frees. All 34 native ImageIO checks pass.
The composite UI replay covers gray/red pixels, transform undo/redo, effect
toggle and editable project reopening. Its pixel checks were strengthened to
native RGB ranges and the actual bounded preview geometry during M4 review;
final full-replay results and aggregate qualification follow below.

A native effect probe caught an iterator borrowing a temporary `MapReader`; the
adapter now keeps that reader alive for the option scan (**D5.3.3**). The existing
multiply-rejection fixture now asserts the newly supported rendered pixel.

## M4 retouch and actions — implemented subset

Shared `image_edit` brush evaluation now also computes clone, neighborhood
color-matched heal, dodge/burn, sponge, blur and sharpen. It samples immutable
pre-stroke data, reuses sparse coverage/tile writes and bounds neighborhood work.
Tonal edits preserve alpha. Clone/Heal can sample a frozen merged snapshot while
painting onto a new transparent layer; aligned offset, source and target state
restore on undo/cancel (**S9.1.2 / S9.2.2**). Healing is explicitly a local
color/texture match, without a Poisson/content-aware claim.

The reusable action module records portable plain command data on one declared
active layer, captures resolved brush/fill settings, rebinds same-layer selections
on replay, validates intermediate replacements and publishes a single history
transaction. Unsupported commands, target/history switches while recording and
oversized/invalid actions fail explicitly. File procedures parse versioned Mark
data and publish `.laction` files atomically, capped at 8 MiB (**S12.1.1v3**).
The demo retains the action library across document replacement.

Retouch acceptance covers exact clone/color-match/tone pixels, immutable sources,
selection clipping, neutral neighborhood operations, aligned-source restoration,
cancel and one-stroke history. Action acceptance covers target rebinding,
resolved brush parameters, pointer-stroke capture, one-undo replay, failed replay
rollback, plain-data validation and save/reload. Automatic and eager-JIT focused
runs pass under forced GC and poisoned frees; final counts and gates follow.

The retouch acceptance exposed a shared MIR index-routing bug: the dynamic/defect
receiver guard called the one-index fallback before reaching N-D dispatch, so
`raster.b[y,x]` could return a row. N-D routing now precedes that scalar fallback;
`fn_index_nd` already checks boxed receivers (**S8.2.4v3 / S7.1.1v3**). A standalone
`ndim_any_receiver` fixture and the retouch pixel checks cover the regression.
Strict interpreter execution still rejects N-D indexing (E501); no interpreter
workaround or harness exception was introduced.

Further review adds transform gesture scale/rotate/cancel/commit and no-drag
checks, identity-reset PSD eligibility and an effect-plus-mask apply check.
Identity reset clears retained geometry; applying a mask keeps an editable
effect and evaluates it once. New UI pixel checks use native RGB bounds and
assert the bounded 3× fitted preview before sampling.

At the retouch/actions checkpoint, M4 remained incomplete: editable
text/shapes/basic paths/styles, a channel palette,
multi-document tabs and file-batch orchestration remain. M5 high-depth/ICC and
photo-scale limits remain pending. Project and retouch work retain the shared
16-million-pixel/64-MiB raster limits; no photo-scale performance claim is made.

The native replay parser also omitted modifiers for drag events. Common event
decoding now reads the same numeric/string modifier fields for every event kind,
replacing duplicated pointer/key decoding. The retouch replay checks Alt source
selection, exact clone pixels, Shift-drag rotation geometry and action-file
roundtrip. This repairs event delivery at the simulation boundary instead of
adding a Photos-only input workaround (**S12.1.3**).

## Expanded focused qualification

- The 16 selected package/runtime cases pass with forced GC and poisoned frees;
  the eager-JIT pass of the 10 Photos/new dynamic-index cases also passes.
  Photos fixtures contain 41 core, 13 interaction, 30 paint, 51 composite,
  21 retouch and 16 action semantic checks, with procedural PSD/project/action
  round trips. The final composite review includes mask/effect, transform
  gestures, identity reset and metadata-preserving flattening.
- Seven Photos UI workflows cover **93 assertions** under forced GC/poisoning:
  composite 14, files 10, gestures 14, lifecycle 7, painting 14, retouch/actions 16,
  smoke 18. The full replay first exposed three Alt-source failures; after the
  shared modifier-parser repair the retouch/action workflow passes all 16,
  including exact clone colors and 90-degree transformed bounds. Other six
  workflows pass the full replay. Evidence: `temp/photos-expanded-ui.log`,
  `temp/photos-expanded-retouch-final.log`.
- `make build-test` succeeds. `make test-library` passes **1681/1681** with native
  test access, including all 34 ImageIO cases and runner-contract checks.
  An earlier sandbox run passed 1675 and failed six socket/trust-store cases;
  all seven scoped RDB tunnel/trust checks and the entire native rerun pass.
- The Radiant integer-cast lint passes; document/fixture pairing and whitespace
  checks are clean. Full Lambda/Radiant and optimized qualification follow.

## Expanded aggregate qualification

`make test-lambda-baseline` reports **5783/5785** runtime checks passing and
**2112/2112** input checks. The native MIR GC stress suite passes **315/315**.
The core script executable passes **1633/1634**; all Photos cases pass.
The earlier `BinarySchemaTest.LambdaPdfOffsetsAndImageStreamsAreValidInBothTiers`
stdout failure remains. A second failure is
`math_test_math_includegraphics`: its PNG metrics are invalid after the full
50-script LaTeX/math batch. It passes alone, twice in one process, after the
immediate preceding HTML script, and in each smaller partition tested. The exact
50-script replay reproduces the error. No workaround, expected-output change or
claim of a pre-existing cause was made. Evidence:
`temp/photos-expanded-lambda-baseline.log`,
`temp/photos-lambda-baseline-summary.json`,
`temp/photos-math-full-batch.txt`, `temp/photos-math-full-batch.log`.

`make test-radiant-baseline` reports **5956 passed, 6 failed, 146 partial,
1 skipped**. All seven Photos workflows pass together in this aggregate,
including all 93 assertions. Native views pass 208/208; UI automation passes
647/651 workflows. The same six failures reported at M1 remain observable:
Bootstrap kitchen-sink page loading, CSS cascade live memory 381630 bytes versus
373940 budget, and four SVG foreignObject workflows (base/CSS/interaction/
namespaces). The snapshot comparator passes 51/51. Evidence:
`temp/photos-expanded-radiant-baseline.log`,
`temp/photos-radiant-baseline-summary.json`. These aggregate gates are not green;
no baseline budget, expected output or inventory was weakened.

## Integer-placement rendering optimization

The first expanded release benchmark (functional checkpoint `28ada37f3`) measured
2984 ms construction, 9794 ms for 16 moves, 677 ms PSD encoding and 14273 ms total,
with 224657408 bytes maximum child RSS. The common integer-placement case was
paying inverse affine and premultiplied bilinear sample work despite requiring
exact source reads. The shared raster blender now selects exact source/mask
reads when the affine is an integral translation; fractional/rotated/scaled cases
retain interpolation. This changes no blend or alpha equation.

Three additional pixel checks cover retained integer translation, a local mask,
requested-region agreement and the fractional interpolation boundary. The
composite fixture now has 49 semantic checks. The optimized host passes all 16
focused package/runtime cases under forced GC and poisoned frees, and all 34
ImageIO cases pass in the optimized native executable. The broader aggregate
results above precede this exact-sampling optimization; final focused UI and
measurement results follow. No interactive-latency claim is inferred from the
headless benchmark.

PSD profile checks also cover disabled editable effects: they are refused instead
of being silently discarded, while project reopen retains their disabled state
and original appearance. The composite fixture now contains 51 semantic checks.

Final workflow review also repairs selection coverage during document resizing.
Image resizing resamples coverage with the pixels; canvas resizing reframes it
with zero outside coverage. Previously the mask kept its old dimensions, so its
overlay stretched and subsequent selection combination could reject mismatched
extents. Four semantic cases cover growth, combination after growth, image
scaling, immutable old coverage and undo restoration (**S9.2.2**). The paint
fixture now has 30 checks.

## Final optimized checkpoint

Native/package checkpoint: `743eec4c1324c1acd777835e1dc34dcc40422a37`. Built with
`make build-release-compile` in the worktree (never `make release`). The current
`lambda.exe` is optimized; switching back to debug requires removing that host
before `make build`, per Developer Guide §7.

- Binary SHA-256: `7265208ff7082b300b7edb829597ebcb6694932dec6ba5bec75ca110240fd38d`.
- Bundled Lambda-package SHA-256: `e47ed317286ecc0f125ee0df33ed0353617e0711dc6cb557788f04fbd16398fb`.
- Platform: `macOS-26.3-arm64-arm-64bit`; `arm64`, 10 logical CPUs.
- Scenario: 3 processes, 8 shared 1024×768 layers, 16 moves, layered PSD encoding.
- Median construction: **2528 ms**; moves: **8372 ms**; encoding: **666 ms**;
  wall: **12297 ms**. Peak child RSS: **225443840 bytes (215 MiB)**.
- Report: `temp/photos/performance/final-results.json`. The earlier expanded
  measurement was 14273 ms total; exact integer sampling removes the measured
  affine overhead. RSS is higher than the M1 measurement; neither this test nor
  storage diagnostics establishes a photo-scale memory or presented-frame target.

The final optimized native executable passes all 34 ImageIO checks; optimized
package checks pass all 16 selected cases with forced GC and poisoned frees.
The composite and retouch/action native replays pass 30/30 assertions on the
optimized host with the same stress knobs. Final selection-resize cases are
included in the 16-case package pass. Evidence:
`temp/photos-expanded-fast-native.log`, `temp/photos-final-package.log`,
`temp/photos-expanded-release-ui.log`, `temp/photos-final-jit.log`.

The delivered checkpoint includes M2, the M3 composition/project core, and the
retouch/actions subset of M4. Typed artwork/styles, channel UI, multi-document
tabs, file-batch orchestration, high depth/ICC and larger-scene qualification are
still pending. Aggregate Lambda/Radiant failures above remain open and visible.

Final eager-JIT requalification also passes all 16 selected cases with forced GC
and poisoned frees after the selection-resize repair. The final optimized UI
subset passes all 30 assertions. The worktree retains the release host for the
demo; generated reports and images remain under `temp/photos/`.


## M4 authored artwork and workspace workflows — 2026-10-20

This checkpoint closes the selected M4 core workflow, while retaining explicit
profile limits. Shared image values remain authoritative (**S9.1.2 / S9.2.2**),
font acquisition/file publication are procedural (**S12.1.1v3**), and native
helpers use rooted callers and owned native buffers (**D5.3.3 / D4.5.2**).
There are no formal ruling or virtual-carrier ABI changes.

The finite delivery covers editable design above a photo, reusable coverage,
independent document sessions, and file action replay. Acceptance checks use
specified pixels and bounds, edit/undo/reopen behavior, retained font/path/style
parameters, failure rollback, per-tab histories and protected closes. Focused
package/native tests and native event replays precede the required Lambda and
library gates; qualification is recorded below.

### Authored image content

`lambda.image` now constructs and evaluates text/vector layers directly. Native
artwork uses Lambda's existing embedded-font outline API and ThorVG backend;
preview, region rendering, explicit rasterization and flat export consume the
same authored content. Font data comes from image-scoped assets, with errors for
missing fonts/glyphs and unsupported contextual/RTL text. The bounded text profile
supports explicit newlines, kerning, font size, line height and left/center/right
alignment. It does not perform implicit file reads or system substitution.

Vectors retain rectangle/rounded-corner/ellipse parameters or absolute M/L/Q/C/Z
path arrays, with fill/stroke and optional linear gradient. Numeric dimensions
and source clipping are explicit. Masks, blends, effects and affine transforms
work on authored layers; image resizing composes an affine rather than replacing
authored geometry with pixels. Selection-to-mask and mask-to-selection work on
untransformed artwork. Source painting and applying a mask into artwork pixels
require explicit rasterization.

Ordered shadow, glow and color-overlay styles evaluate after adjustments/source
masking and before affine placement. Shadow/glow use a bounded box blur; their
explicit local filter outset contributes to selection/rasterization bounds.
Disabled styles retain validated parameters without changing mask evaluation.
Applying a raster mask excludes retained effects/styles from the bake so each
runs once afterward. Group styles require rasterization. Rich projects retain
all authored data and font bytes; this PSD profile refuses artwork, all retained
styles (including disabled ones) and saved coverage channels.

The dock provides Add/Apply Text, shapes/path commands, fill/stroke and embedded
font controls, and add/edit/toggle/remove style controls. Automatic font capture
and Add Text share the existing history group so one Undo removes both. The
explicit Embed Font command remains independently undoable.

### Channels, tabs and batch

Component previews and up to 64 named coverage channels share the Raster API.
Selection save/load, composite-component capture, rename/delete and checked
component replacement use the common command registry. Saved coverage lives in
project metadata and follows crop/image/canvas resizing. Preview changes do not
dirty image content; undoing away a viewed saved channel returns to composite.

`lambda.editor.raster.workspace` retains up to 16 independent sessions with
stable tab IDs, camera/selection/tool state, history and saved checkpoints.
New/Open retain their existing guarded replacement behavior; New Tab/Open in Tab
preserve the current document. Close Tab protects its document; window close
visits every dirty document, including inactive tabs. Save/Discard handles one
such document at a time, and Cancel retains remaining tabs. Action libraries
remain available across tab changes.

Reusable file procedures moved from the demo to `lambda.editor.raster.files`.
`lambda.editor.raster.batch` runs 1–32 explicit source/target jobs with the same
checked action replay and atomic file publication. Jobs select an explicit layer
ID or the first leaf. Preflight rejects invalid job data, duplicate normalized
URL destinations and destinations matching any input. Lexical URL checks do not
resolve filesystem link identity. Each job returns success/error details; the
caller chooses continuation or stop at first failure. Earlier successful files
remain published if a later job fails. The demo exposes that same data-driven
batch path and outcome report. It is synchronous, without background scheduling.

### Shared repairs found by workflow checks

Native Raster equality previously materialized two complete u8 pixel arrays for
structural dirty/history comparisons. A sampled GC stress replay identified
those allocations in `element_semantic_eq → raster_field → raster_snapshot`.
Native-to-native sample comparison now reads immutable owned storage directly;
tag/namespace/other attributes retain their normal checks (**S5.4.3**).
Materialized/native comparison keeps the general relation, hidden RGB remains
significant, and ordinary container NaN/error behavior is unchanged
(**S5.1.5**). Dense/sparse/shared-tile and canonical-materialization cases qualify
this exact specialization; no generic container identity shortcut was added.

The first design replay exposed a procedural empty-map branch parsed as a block;
explicit map expressions repair the actual inspector request construction. Add
Style also passed an edit-only index to the strict add-command schema; the UI
now builds the matching request. Neither command validation nor assertions were
weakened. Empty text is accepted as zero normalized children under **S2.6.2**,
with an empty inspector value; clearing, reopening and undoing preserve that
canonical content rather than requiring a string node that element construction
necessarily omits.

Tab dirty-marker font metrics initially changed the bar height during pointer
input. This shifted the canvas between pointer samples and introduced unwanted
move history in the existing gesture/close tests. A stable bar/button height and
line height keep canvas geometry fixed. Existing gesture cancellation and close
assertions pass after the repair. Subsequent dirty-tab close dialogs focus Cancel
when activation changes the document.

### Remaining profile work

This is not full PhotoCraft parity. Complex text shaping/bidirectional layout,
rich text runs, paragraph wrapping, on-canvas pen handles, the wider style
catalogue and native PSD text/vector/style interchange remain outside this core.
Tabs are an in-memory workspace, not serialized workspace restoration. Batches
block the UI while their bounded list runs. High depth, ICC conversion,
background jobs, larger scenes and input-to-present latency qualification remain
M5 work. Existing 16-million-pixel/native raster budgets are unchanged. Earlier
release measurements above identify older revisions and do not qualify this new
package's performance.


### M4 qualification record

The final optimized host SHA-256 is
`3b1500b3024c14140531cc67ac8561928d3a30ce61e3c340225f9d0dc55b2d43`; the debug validator marker is absent.
`make build-test` and `make build-release-compile JOBS=4` succeed. The latter
uses the worktree-safe release path from Developer Guide §7, without `make release`.

| Check | Result |
|---|---|
| Focused package/runtime selection, automatic tier | **23/23**, final optimized host, forced GC and freed-memory poisoning |
| Same selection, eager JIT | **23/23**, final optimized host, forced GC and poisoning |
| Native ImageIO | **35/35**, including dense/sparse/shared-tile/hidden-RGB sample equality |
| Full library runner after rebuilding tests | **1682/1682**, native access for loopback/trust-store fixtures |
| Input baseline | **2112/2112** |
| Full MIR GC stress suite | **315/315** |
| Lambda runtime baseline | **5790/5792**, with the two previously recorded failures below |
| Core script corpus within that gate | **1640/1641**; every Photos case passed |
| Native Photos UI | All **11** workflows qualified; current inventory **168** assertions, as detailed below |

The optimized full UI run passes **164/164** assertions. The final design replay
adds ordinary-space text and its undo checks, passing **31/31** on the final host;
these replace that fixture's 27 earlier assertions, giving the 168-assertion
current inventory. Other ten fixture paths are unchanged by the last localized
font-index correction. Earlier forced-GC UI runs cover painting, masks, retouch,
projects, channels and workspace workflows; the tab geometry regressions were
repaired and their original 21 assertions pass again.

The final text script has 16 semantic checks: multiline ink, embedded bytes,
authored update/undo, rasterization/project pixels, missing-font/script refusal,
canonical empty content, empty-project reopening, whitespace and ordinary words.
Empty/no-outline bodies produce a transparent owned Raster. Known cmap glyphs
use `font_visit_glyph_index_path`, whose contract accepts valid empty outlines;
the codepoint outline API intentionally reports no visited contours for spaces.
This fixes the caller rather than special-casing a particular whitespace character.

The aggregate runtime failures remain:

- `BinarySchemaTest.LambdaPdfOffsetsAndImageStreamsAreValidInBothTiers`, missing
  the expected result from `test/lambda/pdf/output_resources.ls` in both tiers.
- `math_test_math_includegraphics`, failing in the corpus batch. Its isolated
  rerun passes on the optimized host, consistent with the previously recorded
  sequence-sensitive failure. It is not claimed fixed.

The broad gate precedes the last localized empty-body/font-index corrections;
the final auto/JIT forced-GC selection and design replay qualify those changes.
No test inventory, golden expectation, assertion or budget was weakened. No
Radiant engine source changed in this continuation; earlier Radiant aggregate
limits remain historical observations, while the Photos replay was rerun.

Logs and reports are kept under `temp/`: `photos-m4-lambda-baseline.log`,
`photos-m4-lambda-summary.json`, `photos-m4-library/`,
`photos-m4-qualified-auto.log`, `photos-m4-qualified-focused.log`,
`photos-m4-final-ui.log`, `photos-m4-qualified-design.log`,
`photos-m4-math-isolated.log`, and `photos-final-native.log`. The library gate is
the exact `test-library` runner (`node test/test_run.js --target=library --raw`),
with separate output artifacts so it can run while the runtime stress gate finishes.
The final UI capture is `temp/photos/design-text.png`.

## Brush preview performance — 2026-10-20

The selected milestone addresses mouse strokes falling behind on ordinary
photo-sized canvases. Acceptance is exact whole-stroke equivalence for pressure,
spacing, flow, opacity, selection clipping, masks, branching/cancellation and
source expansion; one history transaction per stroke; and native previews that
avoid repeatedly recompositing a complete unaffected scene. The verification
budget is focused AUTO/JIT tests with forced collection/poison, the complete
Photos UI suite, the Lambda baseline and optimized native A/B replays. Larger
canvas/device qualification and nonlocal filter optimization remain separate.

The original path rebuilt coverage from the first point on every frame,
composited the whole document, uploaded it, then rebuilt the histogram and
inspector/tab presentation. An optimized headless phase probe at 1024×768 found
raster editing increasing from about 0.3 to 5.3 ms over a 120-point stroke; native
publication added substantially more work than the raster-only probe. This
established presentation and cumulative replay as the causes to address.

Implemented behavior:

- `image_edit.cpp` shares one dab/pixel kernel between whole strokes and
  `image.paint_stroke_step`. Continuations carry owned cumulative coverage,
  previous preview, last pressure point, spacing remainder and cumulative work.
  New coverage recomputes only changed bounds against the frozen source;
  frame subdivision does not compound stroke opacity. Snapshots remain immutable
  (**S9.1.2 / S9.2.2**), source bindings use value equality (**S5.4.3**), and native
  wrappers use precise roots (**D5.3.3**).
- Brush/erase interaction resumes this state. Changed settings or growing source
  coordinates restart from the original source and complete point sequence.
  Actions continue to capture that ordinary command; preview state does not enter
  authored image content or stroke history.
- `raster.render.frame` checks the exact preceding scene before updating a cached
  composite through existing `image.render_region` and `image.with_region`.
  Effects/styles, stale caches and other tools use full composition. Region
  changes preserve clipping/blend dependencies at each affected pixel.
- The native view avoids rewriting unchanged surface/target geometry. During
  frame callbacks the demo updates stage pixels/overlays, then refreshes the
  histogram, inspectors and workspace shell at gesture completion/cancellation
  (**S12.1.3 / D4.5.2**). Pending callbacks are cancelled at pointer-up.
- `test/demo/photos/benchmark_brush.py` instruments disposable copies under
  `temp/`, identifies the optimized binary/package, alternates saved/current
  sources and separates edit/publication timing. The application has no profiling
  output in its normal event handlers.

Qualification and final measurements follow at the completed checkpoint.
Remaining costs include full canvas projection/upload, final document validation,
retouch replay and full previews for effect/style scenes. These measurements do
not establish hardware input-to-display latency or the full M5 budget.

The native frame profile exposed a second, larger cost outside the Lambda
handler: same-size bitmap uploads and repeated diagnostic attributes entered
DOM reconciliation, spending about 256 ms in layout per frame. Fixing the
handler alone would have left that visible lag in place. The canvas upload
adapter now reconciles attributes only when width/height attributes actually
change. The common canvas pixel-generation path requests repaint directly;
bitmap ownership and intrinsic-size updates retain their existing contracts
(**D4.5.2 / D5.3.3**). Preview frames and initial brush/eraser/retouch publication
update only the stage, leaving inspector diagnostics until the transaction
boundary. A native trace then recorded 40 brush callbacks with zero DOM
mutations, zero commits and zero layout time, with stable retained view storage.

The canvas bridge regression now checks repeated frame uploads under forced GC,
retained colors and resize-dependent CSS geometry. Its native companion asserts
zero reconciliation/layout for the three bitmap-only callbacks, without using a
machine-dependent time threshold. The broader verification budget consequently
includes the Radiant baseline as well as the Lambda baseline. Forced full-
viewport CPU paint remains measurable separately from these callback results;
the benchmark's `--paint` mode verifies every frame's pixels and records both.

### Qualification checkpoint

`make build-radiant-baseline JOBS=4 TEST_JOBS=4` followed by
`make run-radiant-baseline` completed: **5,964 passed, 5 failed, 146 partial,
1 skipped**. The five failures reproduce previously recorded limits: CSS cascade
live bytes **381,630 vs 373,940** and the four SVG foreign-object UI variants.
The page-load lane passed **111/111**, including its previously flaky case.
This is a qualified aggregate result, not an all-green baseline.

The Photos workflow suite passed **12 workflows / 181 assertions** after the
native canvas change. An additional proportional-fit resize check passed
**5 assertions**: identical displayed width/height must still update the input
scale when document dimensions change. These 13 workflows also passed within
the broad UI aggregate. The native canvas regressions passed **2/2**, including
three bitmap-only callbacks with zero reconciliation/layout and a real resize
that updates selector-dependent geometry. Radiant integer-cast lint passed.

Changed output-sample bounds now gate region updates after opacity and selection
clipping. A fully transparent or fully clipped stroke still advances cumulative
coverage/work, but does not composite or upload unchanged pixels. Focused image
and editor tests include these no-change cases alongside the source-value,
pressure, mask, expansion, clipping/blend and filter/style-fallback cases.

`make test-lambda-baseline JOBS=4 TEST_JOBS=4` completed: **7,904/7,906 passed**,
including **2,112/2,112 input tests** and **315/315 forced-GC stress cases**.
The script runner passed **1,642/1,643**; its sequence-sensitive
`math_test_math_includegraphics` failure and the separate
`BinarySchemaTest.LambdaPdfOffsetsAndImageStreamsAreValidInBothTiers` failure
match the prior checkpoint. No new aggregate failure was introduced or masked.
Logs/reports are under `temp/photos/performance/{radiant-gate,lambda-gate}`.

After restoring the optimized host, the focused image/Photos set passed
**23/23 in AUTO and 23/23 in eager JIT**, with both
`LAMBDA_GC_FORCE_EVERY=1` and `LAMBDA_GC_POISON_FREED=1`. This includes the final
changed-sample bounds implementation. Logs: `qualified-auto.log` and
`qualified-jit.log` in the same performance directory. The release binary is
identified by the final replay report; a debug host is rejected by the benchmark.

### Optimized replay results

Final command:

```sh
python3 test/demo/photos/benchmark_brush.py --paint --samples 24 --repetitions 3 \
  --layers 1 8 --reference-dir temp/photos/performance/reference/app \
  --reference-home temp/photos/performance/native-before/lmd \
  --output-dir temp/photos/performance/paint-final
```

Report: `temp/photos/performance/paint-final/results.json`. The saved demo/package
sources are from `ed4238cfc`; both versions use the same optimized host. Each
1024×768 case runs three alternating processes per version, 24 brush callbacks
per process, in a 1280×820 viewport. The single layer is opaque; the eight-layer
case uses full-canvas RGBA layers with alpha 0.7. All **288 per-preview pixel
checks** passed, along with each run's error/history assertions.

Numbers below are medians of the three process medians, in milliseconds.
Combined CPU cost is computed per callback/paint pair before aggregation.

| Case | Previous callback + CPU paint | Current callback + CPU paint | Reduction |
|---|---:|---:|---:|
| One opaque layer | 295.927 | 72.076 | 4.1× |
| Eight translucent layers | 807.482 | 28.366 | 28.5× |

For the eight-layer case, the current callback median is **4.127 ms**, with
**0 ms layout** and **0 DOM mutations** in every measured brush callback. Native
editing accounts for **0.182 ms**, publication **3.623 ms**, and CPU paint
**24.194 ms**. These component medians need not sum to the paired median.

Absolute times varied with concurrent system load. Single-layer process medians
ranged from 284.550–984.863 ms previously and 28.761–271.159 ms currently.
The eight-layer medians were steadier: 802.625–817.655 ms previously and
26.970–28.494 ms currently. All repetitions remain in the report; these are
comparative CPU workload measurements, not a hardware input/display or universal
60 Hz claim. Forced full-viewport paint remains a substantial residual cost.

Host: macOS 26.3, arm64, 10 logical CPUs; AUTO execution backend, without forced-GC
benchmark knobs. Release SHA-256:
`97eec0421e3dffd31f6d22efc64d163961fea2469ea582e8a7d93b1afe18d462`.
Current package SHA-256:
`9c92830e917e41eff1ea1bbab4fa29ad2acec33b911a6d75f9c04e14d7bb7902`.
The report also identifies both demo/package snapshots. Callback timestamps match
native frame records explicitly, excluding activation/input-drain turns. CPU
paint profiles include record/replay; OS input queuing and actual display refresh
remain outside the measurement.
