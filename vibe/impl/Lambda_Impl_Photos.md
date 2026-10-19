# Lambda Photos — Implementation

**Started:** 2026-10-19. **Status:** layered editor with tiled painting, retained composition/projects,
retouch and actions; focused qualification is expanding and aggregate gates have failures, on
`codex/photos-20261019`, worktree `temp/photos-worktree/`.

Design: [Photos proposal](../Lambda_Demo_Photos.md). Existing contracts:
**S9.1.2 / S9.2.2** immutable snapshots, **S12.1.3** pure views and procedural
handlers, **D5.3.3** precise roots, **D4.5.2** owned pixel transport,
**D7.2.4v3** shipped package paths. No formal ruling changes.

## Active checkpoint

M0/M1: deliver a reusable raster command/session API and a native layered Photos
workspace. Acceptance: place three images; group/reorder/mask/move; undo/redo;
save/reopen layered PSD with current pixels; failed opens/saves preserve state.

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
| M2 tiles, selections and painting | Core implemented; focused native/package checks and 63 UI assertions pass; aggregate qualification follows the next native batch |
| M3 transforms, reversible effects and rich persistence | Implemented core: affine, eight blends, clipping/pass-through, effects, histogram and versioned project data; focused checks below |
| M4 retouch, type/shapes, channels and actions | Partial: retouch and single-layer action recording/replay implemented; typed artwork/styles, channels, tabs and batch orchestration remain |
| M5 high depth/color and scale qualification | Pending |

## Delivered behavior

The `lambda.editor.raster` facade has stable layer IDs, foreground-first tree
rows, checked commands, masks, grouping/ungrouping, adjacent-layer merging,
document/image resizing and shared history. Commands retain immutable snapshots
and restore targets on undo. Gesture previews record one completed move and
create no edit for a canceled or zero-distance drag. The native view adapter
retains its canvas while copying current composition pixels.

Photos supplies Open/Place, layer/property/history docks, mask targeting, baked
adjustments, fit/zoom/pan, keyboard/touch movement, protected document replacement,
atomic PSD saving and PNG export. `demo photos` is registered. The existing
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

At the M2 checkpoint, M3–M5 remained pending; subsequent entries supersede retained transforms/effects, rich-project persistence and retouch/actions status. Outstanding work includes type/shapes,
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
adjustments add editable effects. Focused package tests include 49 composition
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

M4 is not complete: editable text/shapes/basic paths/styles, a channel palette,
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
  Photos fixtures contain 41 core, 13 interaction, 26 paint, 49 composite,
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
