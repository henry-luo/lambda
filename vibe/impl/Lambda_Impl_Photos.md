# Lambda Photos — Implementation

**Started:** 2026-10-19. **Status:** initial layered-editor implementation; focused qualification passed, aggregate gates have failures, on
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
| M2 tiles, selections and painting | Pending |
| M3 transforms, reversible effects and rich persistence | Pending |
| M4 retouch, type/shapes, channels and actions | Pending |
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

## Current verification

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

## Aggregate qualification limits

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

M2–M5 remain pending: sparse tiles/region evaluation, brush and selection tools,
retained transforms/effects, rich-project persistence, retouch/type/shapes,
actions/multiple documents, and high-depth/color/scale qualification. This
checkpoint does not claim complete PhotoCraft feature parity.
