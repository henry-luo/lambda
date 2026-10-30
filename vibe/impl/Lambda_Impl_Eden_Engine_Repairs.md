# Eden engine repairs

2026-10-28. Runtime/palette qualification used base `d53f1a043`; the later
coverage repair uses base `e80352983` plus the existing Scene3D directory move.
No formal ruling changes.
Finite scope: deterministic procedural replay, the native Meadow reset replay,
and demonstrated blockers from the Eden simulation/model work. Runtime and
palette-binding repairs are qualified below. Direct diagnosis resolved the two
Meadow image comparisons with the explicitly approved coverage compatibility fix.
Authority: **S1.6**, **S12.1.1v3**, **D2.4.1–D2.4.3**, **D4.3.1**,
**D5.2.3**, **D5.3.3**, **D2.11.4**, **D4.5.1v4–D4.5.3v2**.

## Dynamic scalar argument ownership — LR07-50

`lambda_dynamic_call` copied argument Items into precise roots, but a wide
numeric Item could still point into the argument List's owned scalar tail.
The List's data allocation can compact during a callback (**D4.3.1**);
rooting a copied numeric Item does not give that interior payload a stable
home. A four-argument erased call reproduced a live u64 changing from
`4316279033673738697` to reused storage while the callee allocated records.
A direct generator call retained its key and replayed exactly.

The dynamic ABI now snapshots wide scalar payloads into the caller's number
extent before optional/rest adaptation or invocation. This uses the existing
explicit C/host ownership helper (**D5.2.3**), preserves precise roots for GC
objects, and checks number-stack exhaustion. Scalar results may safely alias
those caller-owned homes; nothing points into a temporary native array.
No Procart random algorithm, expected result, or execution-tier override changed.

`dynamic_call_scalar_ownership.ls` covers u64, i64 and an out-of-band tiny float,
allocation inside a four-argument erased call, scalar return aliases, repeated
calls, and optional/rest adapters. The preserved pre-fix executable fails it;
the fixed executable passes on interp/AUTO/JIT with collection every 37
allocations, and on JIT with collection every allocation plus freed-memory
poisoning. The unchanged full foliage replay and neighboring dynamic application
fixture pass focused qualification.

## Raising procedure assignments — LR07-51

Eden's audio synthesizer exposed a separate MIR construction failure: its
numeric filter update generated `ursh` with a double operand. Reduction removed
all audio/native imports and retained a float accumulator in a procedure that
can raise. The assignment producer returned its raw RHS register while
publishing an Item descriptor. The procedural side-effect consumer also ignored
the descriptor and unconditionally tested the Item tag.

Assignment statements now publish the post-store binding's actual carrier via
the existing binding descriptor helper. Procedural error guards inspect boxed
Item results; native scalar/pointer carriers cannot contain ItemError
(**D2.4.1–D2.4.3**). Explicit assignment admission and handled-error suppression
remain in their existing owners. No vendor code or C-text back end changed.

`proc/float_assignment_descriptor.ls` calls a numerical filter through a dynamic
procedure value and checks its three-sample output against the recurrence,
including a declared-float neighbor. It fails to compile on the preserved
pre-fix executable and passes the fixed interpreter/JIT. The model worker also
confirmed that the original forced-JIT animal-life fixture, including its
imported synthesizer, passes 28 checks on the repaired executable.

## Multisample coverage preservation

The original Eden Meadow required byte-identical pixels after restoring daytime
and after hiding/re-showing animals. Both comparisons failed while physical
state, generated descriptions, geometry, instance buffers, uniforms, camera,
shadow transforms and depth images matched. The affected RGB values were in
multisample samples 1–3; sample 0 and alpha were exact. Removing animation,
projection replacement and shadow rendering did not eliminate the failure.

The standalone reduction uses only two OpenGL draws: indexed terrain and 4,106
instanced grass blades. It reproduces without Lambda/Radiant, texture sampling
in the material shader, or shadow rendering. On Apple M4, OpenGL
`4.1 Metal - 90.5`, later frames incorrectly copy a terrain triangle's color into
samples outside its geometric coverage. This is a provider coverage failure,
not different procedural output or unspecified interpolation locations.

Independent confirmation projects the submitted vertices with the exact float
matrices, queries the GPU's four sample positions, and compares geometric
containment against `gl_PrimitiveID`. Twelve samples comfortably inside triangles
match the first frame; six acquire the wrong primitive on the second frame.
With explicit coverage forwarding, both frames match all twelve. The full
normal-color reproduction also becomes byte-identical for every sample over
three frames. GLSL 400 alone, fresh framebuffers/programs, higher-precision color
and depth, disabled depth testing, explicit sample state and per-sample shading
do not repair the failure. These controls distinguish the coverage fix from
shader-version or interpolation changes.

The built-in color fragment shader (`radiant/scene3d.cpp` in the base layout,
`radiant/scene3d/scene3d.cpp` in the concurrent directory move) now assigns
`gl_SampleMask[0] = gl_SampleMaskIn[0]`, with a GLSL 400 header. This forwards the
rasterizer's coverage explicitly; the output mask is ANDed with geometric
coverage and cannot add samples ([OpenGL 4.1 §§3.9, 4.1.3](https://registry.khronos.org/OpenGL/specs/gl/glspec41.core.pdf),
[ARB_gpu_shader5](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_gpu_shader5.txt)).
The renderer already uses GLSL 400 for per-sample display conversion and caps
its native targets at four samples, so one mask word covers every supported
sample. Basic, Lambert, Standard, Physical, point, line and sprite materials
share this body; existing discards still apply. Custom shaders and shadow
programs retain their authored behavior.

The user explicitly approved this graphics-provider compatibility exception to
AGENTS.md rule 1 on 2026-10-28: “Implement and test the coverage fix.” No pixel
tolerance, capture delay, warm-up render, context reset, expected image or
animation/shadow disablement was introduced. Native graphics ownership remains
**D4.5.1v4–D4.5.3v2**; no semantic ruling changed.

The independent bone-palette feedback hazard found earlier remains separately
repaired: an unskinned shadow draw must bind the white fallback instead of the
active depth attachment. That safety repair alone did not fix Meadow replay.
All diagnostic engine exports, state queries, readbacks and shader probes were
removed; `gl_core.cpp` is unchanged by this coverage repair.

Permanent regression `Scene3dTest.MeadowRestoresInitialMultisampleCoverage` uses
the existing Procart Meadow page: pause, snapshot, change/reset daylight, then
hide/restore grass. Its eleven events include visible-change assertions and two
zero-tolerance comparisons against the first snapshot. It passes 4/6 assertions
on the preserved pre-fix executable and 6/6 after the repair. Eden's unchanged
67-event replay similarly changes from 40/42 to 42/42. Native Scene3D units pass
81/81, and the registered Meadow/outdoor garden replays both pass.

Ignored local evidence is under `temp/eden-msaa-fix/`: the standalone variants,
`coverage-reference.json`, `verify-coverage.py`, before/after Procart results and
native test logs. The earlier standalone input bundle remains under
`temp/engine-eden/repro-msaa/`. These scratch artifacts may be absent in fresh
checkouts; the registered regression requires no scratch assets or Eden checkout.

## Qualification

### Earlier runtime and palette qualification

Focused runtime gate: 7/7, covering both new cross-tier cases, original foliage,
dynamic application, defect/rejected-error flow and typed call layout. `make test-lambda-baseline` reports 8,378/8,383: all 2,112 input checks pass,
MIR emission 277/277, GC stress 348/348 and ratchet 20/20 pass. Four failures are
sandbox-denied loopback listeners and pass a focused host-access rerun (4/4).
The remaining failure is the inherited Greetings popup undo/redo mismatch;
the unchanged foliage gate now passes. This accounts for 8,382 passing checks
and one inherited failure, without claiming a clean aggregate run. No Test262
or other harness was relaxed. The native gate result follows below.

Radiant aggregate qualification: 6,471 passing checks, six inherited CSS
layout failures, 140 partial cases and two skipped. Native Scene3D units pass
81/81; UI automation passes 743/743. Those aggregate runs preceded the renewed
native diagnostic probes. The probes are now removed. The palette repair's
required Radiant milestone rerun has the same 6,471 passing checks, six inherited
layout failures, 140 partial cases and two skips; Scene3D 81/81 and UI automation
743/743 pass. Its log is `temp/engine-eden/radiant-palette-qualification.log`.
The clean main binary is SHA256
`4b419b9e9194fa143ddbbf4e7edb031560a49b17fbe213b3397efef6df6c498d`.
Both self-contained provider variants return 0 (`GL_NO_ERROR`) while the strict
comparators return 1; results are in `repro-msaa/verification.json`.
The unchanged reduced engine replay still reports 4/5 (the strict image fails).
The full Eden Meadow smoke on this clean binary remains 40/42 across 67 events;
its two strict reset/hide-show image assertions are unchanged. The log is
`Eden/temp/engine-palette-meadow.log`. The native replay investigation
also has a baked Mark + standalone page reproduction independent of Eden
imports, biological generation and animation: `_baked_replay.ls` with
`static-grass.mark` under `temp/engine-eden/`; it still fails exact replay. A direct DOM camera write of the existing fov also fails
while projection-generation and shadow-pass deltas both remain zero; the failure
therefore does not require projection/resource replacement. A matched 600px
viewport/capture reproduces it as well.

### Coverage repair qualification

The preserved pre-fix executable is SHA-256
`eca113ce3b37f7dfc9bc09069fd1b6890e694b6a02fc43313f638b0d53dfb98f`.
The repaired debug executable is SHA-256
`d1be5d331d8536bd7a2960782b49d11bcd7152bdc3635f9086326a83a2bf3983`.
These are functional checks on Apple M4; no performance or other-provider
qualification is implied. `make test-radiant-baseline` completed with 6,471
passing checks, seven reported failures, 140 partial cases and two skipped.
Six failures match the earlier CSS text/inline/sizing layout failures. The
additional `EventSimRenderTest.UnknownOperationFails` aggregate failure conflicts
with its captured passing GTest output/JSON; three focused executions also pass.
An isolated Radiant View rerun confirms the negative control passes. Native
units pass 81/81 and UI
automation 743/743. The baseline log is
`temp/eden-msaa-fix/radiant-baseline.log`; this is not a clean aggregate claim.

Eden's broad smoke exceeded the outer runner's 360-second budget after Terrain
37/37, Models 66/66, Meadow 42/42 and Ecosystem 42/42 passed. Animal observation 18/18, habitat 33/33 and localization 66/66 then passed
separately, including two byte-identical localization checkpoints. Combined
coverage is 304/304 assertions across 574 events. The first four suites used
`d1be5d331d8536bd7a2960782b49d11bcd7152bdc3635f9086326a83a2bf3983`;
the resumed suites used the concurrently rebuilt executable
`a7e0d62cdf934fb2059da0471b74d73db78545612dec7a210f0cd7919c4d89fa`.
This is neither an uninterrupted all-suite run nor qualification of one
immutable binary. Eden's `temp/coverage-all-smoke-result.json` combines the
results; the timeout and per-suite logs remain preserved.

### Concurrent workspace changes and pinned follow-up

Other workspace work added a shared Scene3D runtime/coordinator and rebuilt
`lambda.exe` and the native runners during later qualification. Those edits
are preserved; they are outside this coverage repair. To avoid attributing
results to a moving executable, the follow-up pins:

- Main executable SHA-256
  `36a33ac5e4253e8ee5543b3e2f49b89bd8c15685c0cf770648d726eb3adaa382`:
  Eden Meadow 42/42 and Procart coverage replay 6/6 pass.
- Native unit executable SHA-256
  `739aeeb289cd1e9f034ad40784f066cc2b82f385c9849b1b751334facbeb40c5`:
  83/83 pass, including the two newly added runtime checks. Its copied binary
  uses the existing module library directories through `DYLD_LIBRARY_PATH`.

The isolated broader View rerun has 243 passes, two failures and one skip. Both
failures repeat on the pinned main executable in three focused runs each:
`NativeClipUsesDocumentSchedulerAndParksAfterClampedFinish` leaves scheduler
count 1 where 0 is expected; `RingworldAnimatesAndKeepsOrbitPanZoomIndependent`
advances animation time after a pause. Both passed in the earlier coverage-only
baseline; the later failures occur after the concurrent runtime changes and
are not claimed repaired by the sample-mask fix. The original two Meadow image
comparisons remain fixed. No assertion or harness was relaxed.

Follow-up evidence: `temp/eden-msaa-fix/view-isolated/`, `pinned-*.json/log`,
`pinned-units.log`, and Eden's `temp/coverage-pinned-meadow-result.json`.
Further runtime/coordinator qualification belongs to that separate change;
this record does not transfer earlier passing evidence to future rebuilds.

## Structural map grouping — LR07-52

2026-10-29. Found while adding Eden ED12 reproductive plants. **S5.6.1v2** requires grouping
by value equality and **S5.6.2** requires equal keys to hash alike. The native
summer scene exhausted the unchanged 4,096-resource quota. Its saved Mark
projection contained 2,253 meshes with only 56 geometry and three texture
resources. Separately allocated, equal surface maps created separate batches.

A ten-leaf standalone Procart probe produced ten meshes while the surface maps
compared `== true`. `lambda_item_hash` recursively hashed arrays but fell back to
raw Item addresses for maps, and `lambda_item_compare` did the same. Numeric
arrays nested inside map keys also fell through by physical representation.

The map hash now visits sorted keys and recursively hashes field values using
canonical numeric hashes. Map key comparison delegates to the existing recursive
value equality. Boxed and numeric-array storage share a canonical sequence hash
and comparison. Precise roots retain the map/array, current read and rooted key
list across allocating reads and recursive hashes (**D5.3.3**). No stack scanning
or graphics quota change is involved.

Permanent fixtures: `test/lambda/group_map_keys.ls` covers independent map
allocations, field reordering, nested maps/arrays, numeric representation and
null fields. `procart/surface_coordinates.ls` pins one mesh for ten independently
built equal surface bindings. The original preserved executable fails both
probes. This repairs materialized map/array keys; the wider SO50 collection
comparison-error boundary and other representation families remain separate.

The same saved summer state drops from **2,253 meshes to 58**, retaining 56
geometries and three textures. Largest opaque instance batch is 15,126,
within the unchanged 16,384-instance limit. A fixed executable is preserved at
`temp/eden-species/lambda-fixed.exe`, SHA-256
`163054c690a5358c807c6f747a41792b3e68d69d76b715da5ed412e9d4c9b539`.
The grouping, organ and surface-coordinate fixtures pass **9/9** combinations
across interpreter/AUTO/JIT with collection every 37 allocations and freed-memory
poisoning (`temp/eden-species/grouping-checks.json`). The required complete Lambda baseline passes **8,395/8,395**
(`temp/eden-species/lambda-baseline-final.log`). The expanded Eden yearly replay passes **42/42** across 98 events
(`../eden/temp/ed12-yearly-fixed.log`), including the summer image that previously
failed at the unchanged graphics resource quota.

Final Eden qualification with that executable: **75/75** fixture/tier runs,
**42/42** yearly native assertions, and **29/29** flowering/fruiting native
assertions, including exact stage/save replay and English/Chinese mobile UI.
Eden evidence: `temp/ed12-fixtures-final.log`, `temp/ed12-yearly-fixed.log`,
`temp/ed12-fruiting-fixed.log` under the sibling checkout. No tolerance,
expected image, resource quota or sample-count change was used.

## Reentrant numeric carrier proof — LR07-53

2026-10-29. Authority: **D2.4.1–D2.4.3** (representation authorities and
conversion), **D8.2.4v2** (the common AST child/index contract). No ruling changed.

Eden's bounded-batch ecosystem worker crashed forced JIT before `main`, while
interpreter and AUTO passed. Reduction removes imports, tasks, handlers and
optional parameters: `var total=0; while(total<n) { let step=min(2,n-total);
total=total+step }` still crashes the preserved release.

The AST is acyclic. LLDB identifies repeated entry to the same
`mir_numeric_join_proven` binary node: recurrence analysis reads the assignment,
follows `step` to its initializer, asks the `min` carrier for the subtraction's
carrier, and starts a fresh numeric proof. Each new proof resets its binding
visiting set and budget. The borrowed-var AST scan at the top of the overflow
stack is a victim of this semantic analysis cycle, not a broken child edge.

The numeric proof owner now keeps a compiler-local stack of active expression
identities. Re-entering the same active question fails that native-lane proof
closed; the existing boxed numeric path remains available. Independent later
questions are still analyzed, and the original within-proof recurrence handling
is unchanged. No recursion cap, AST mutation, vendor patch or Eden algorithm
change was introduced.

`proc/numeric_join_reentry.ls` and its paired golden cover bounded min/max,
floating and nested-math recurrences. The preserved release crashes; the fixed
interpreter/AUTO/JIT each pass six result checks. Eden's unchanged
`tests/procedural/ecosystem_job.ls` passes all 36 assertions on all three tiers
with the fixed debug executable. Required full baseline and final release
qualification are recorded below when complete; scratch evidence lives in
`temp/eden-jit-loop/`.

Full required Lambda baseline passes **8,408/8,408**, including 2,112/2,112 input
checks, MIR emission 279/279, GC stress 350/350, and 1,911/1,911 script/parity
checks (`temp/eden-jit-loop/lambda-baseline.log`). No exclusions, goldens or
budgets were relaxed. Release compilation uses `make build-release-compile`
without cleaning shared test artifacts; the original performance executable
remains preserved.

Final release qualification: both the six-check recurrence regression and the
36-check original Eden worker pass on interpreter/AUTO/JIT (six fixture/tier
runs, 126 assertions; `temp/eden-jit-loop/release-checks.json`). The corrected
release is `temp/eden-performance/lambda-engine-fixed-release.exe`, SHA-256
`25082cca98a7f41484eb8eb7c994c0d62fe0dcbfca170751bd7e21be4a5ad675`.
Main `lambda.exe` is byte-identical and remains the release build; its debug
validator marker is absent. The original performance release remains unchanged
at SHA-256 `0c0bdca95370b702f3fa27bbcdf1b7927d2318a5d779081b998fe5f6e33f104b`.
No Eden source or unrelated physics/Scene3D changes were modified.

## UI attribute buffer ownership — LR08-14

2026-10-29. Authority: **D3.4.3v5/D3.4.5** (shape rebuilds),
**D4.5.1v4/D4.5.2** (document-region ownership). No ruling changed.

Eden's landscape inset exposed a runtime `pool_free` of an arena pointer.
The reduction needs only an element with four attribute spreads: add an int,
replace it with text, add another field, then replace the first with an int.
LLDB stops in `container_rebuild_data_install`, reached through literal spread
and field retyping. The first retype migrated the data to the runtime pool and
set `is_data_migrated`; the next extension installed document-arena data without
clearing that flag. The final retype consequently tried to free arena data as
runtime-pool data. The pool's ownership check correctly rejected the free.

UI element retyping now allocates from the same document arena as extension.
The common allocator predicate excludes GC-owned containers even in a UI
context, and the migration flag records only runtime-pool ownership. Neither
pool validation nor the field-layout rule changed. The GC unit regression
alternates retyping and extension, checks the actual arena owner after each
write, and verifies both retained field values.

## Numeric DOM and SVG presentation attributes

The inset's opaque camera rectangle had a separate cause. Lambda templates
admit number Items for attributes; geometry already reads those values, but the
SVG presentation reader accepted only String. Numeric `fill-opacity:0.13`
therefore appeared absent and used its initial value, 1. The DOM text getter
likewise omitted numeric attributes, including `tabindex:0`.

The existing DOM stored-attribute seam now formats numeric Items through the
shared `format_number` formatter and interns the resulting text with the
existing document attribute-value store. The source Item stays numeric; callers
receive stable document-owned text (**D4.5.1v4/D4.5.2**). SVG presentation fallback
uses that seam while preserving CSS declaration and animation precedence.
No template spelling or numeric value was changed to hide the defect.

`DomAttributeValueTest` verifies integer/float text, unchanged source types,
bounded repeated reads, immutable saved text, mutation and removal. The native
`svg_presentation_numeric_attrs` replay checks numeric focus, fill/stroke opacity,
stroke width, exact numeric/string pixel parity, GC, restoration and removal.
Its paired Lambda source/golden also contains the ownership reduction. Before
the anonymous-flex row was added, the preserved prior release reported
**11/17**, emitted the pool warning and painted opacity as 1.

Full Eden integration exposed a missing case in that first repair: the new
numeric fallback called `ElementReader::get_attr` on a typeless anonymous flex
text item. Its String getter already admitted absent backing, but the typed
getter dereferenced a null TypeMap. The shared typed getter now returns null
for absent type/data/key, preserving **S7.1.1v3**; no layout node is fabricated
or skipped. A flex row reduces the intermediate host's crash to the standalone
native fixture. The direct anonymous-node unit check covers that same read
without a graphics context. Full Eden initial layout passes on corrected debug
and release with zero pool warnings, and the expanded native replay passes
**17/17**. Focused GC ownership tests pass **3/3** and DOM attribute tests **3/3**.

Unsupported complex carriers keep absent native text, rather than entering the
real-number formatter's unknown-carrier fallback. The existing DOM bridge
represents a present attribute with no supported text as an empty String;
that convention is unchanged. The final native fixture adds this assertion and
passes **18/18** on interpreter/AUTO/JIT with collection every allocation and
freed-memory poisoning. The previous repaired release passes the other 17
assertions and fails precisely that unsupported-carrier check.

Full required Lambda baseline passes **8,410/8,410**, including input
**2,112/2,112** (`temp/eden-inset/lambda-baseline-final.log`). This run qualifies
the final ownership, reader and real-number text paths; the only subsequent
behavior change is the complex-carrier exclusion, qualified by the focused
three-tier native replay and the pending Radiant aggregate. The first partial
Lambda gate was stopped when integration exposed the anonymous-node crash;
it is not a completed qualification.

Eden integration on release SHA-256
`b6d96c5bc9cff8b0c82c8f749ef5adf2265d3c00f5bbf37a114b948c0c30893b`
passes landscape **32/32** (43 events), living growth **58/58** (73 events),
and the shared Procart Meadow viewer **37/37** (51 events), with zero pool
warnings in all three. Desktop/mobile captures confirm the translucent frame
and thin outline; nine focused Eden fixture/tier runs also pass.

The complex-guard release adds only the complex exclusion and comment normalization
to that integrated host. It is pinned at
`temp/eden-inset/lambda-inset-final-release.exe`, SHA-256
`5fb1fcca49d636280ff6e1ab97719b1bed7de439be1c061395fc93bd1c853269`.
Full Eden startup passes on it without pool warnings; the 18-check native
replay passes all three tiers as described above. Both releases were built
non-clean with `make build-release-compile`; prior performance executables
remain unchanged. Required Radiant aggregate qualification is recorded below;
evidence is under `temp/eden-inset/`. No Eden source or unrelated physics
implementation was changed by these repairs.

### Standalone document owner and final gate

The first Radiant attempt found two imported-model units asserting in
`MarkBuilder`: their native document has a document pool but intentionally has
no populated Mark `Input`. Read-only numeric text needs document ownership,
not an Input builder. The existing attribute interner now keeps separate
document-pool and Input-owned caches. Converted reads use the existing
`dom_document_create_string` helper on the node's storage owner; publication
back into Mark still makes/reuses an Input-owned String. A same-text read→write
test checks that these owners cannot alias. Native documents never need a
fabricated input merely to read an attribute (**D4.5.1v4/D4.5.2**).

Both previously aborting imported-model units pass **2/2**. The expanded
DOM attribute suite passes **4/4**, including a native document with an empty
Input and the anonymous-layout-node case. Full Eden startup and the 18-check
native replay pass again on interpreter/AUTO/JIT with forced poisoning GC;
none emits the pool warning.

This ownership release is pinned at
`temp/eden-inset/lambda-inset-owner-release.exe`, SHA-256
`df95491e55ce0cc1802b773c8f28addd28b0d67a2a0b10d9daf3aebfce7f7a32`.
The earlier Radiant attempt was stopped after the demonstrated failures while
UI automation was unfinished, and is not a complete aggregate result.
Its layout stage reported 4,725 pass, 136 partial and 413 fail. A full identical
layout stage on the preserved pre-inset release produces exactly the same
counts in every category: 407 failures in the now-included writing-modes suite,
plus six in text/inline/sizing. The old-host control and per-case rows are
`temp/eden-inset/layout-original.json`. Exact identity comparison confirms
**413 old, 413 current, zero new, zero resolved** (`layout-difference.json`
and `layout-owner-failures.json`).

The complete required Radiant run reports **8,079 pass, 422 fail, 140 partial,
two skipped** (`temp/eden-inset/radiant-baseline-owner.log`). Native Scene3D
units pass **91/91**, DOM UI **165/165**, and text/query **361/361**. This is
not a clean aggregate claim. Besides the identical 413 CSS failures, seven UI
children exited 137 without machine results; every one passes isolated against
the immutable gate-built host. The other UI failure was a Blade geometry
expectation, explained below. The remaining native View failure exposed the
Scene3D text-schema guard below. The eight failed UI selections pass **8/8**
in the isolated follow-up, alongside three scheduler checks.

The generic DOM text seam must not widen the Scene3D schema. Previously
`color:42` was rejected as non-text; numeric DOM reflection let it reach the
later material-color parser instead. The existing `scene3d_text` owner now
checks the raw String/Symbol lane before using reflected text. The unchanged
negative case, mixed typed scene and outdoor garden pass **3/3** after this
repair; no expected diagnostic changed (**D2.4.3**, **D7.3.5**).

Blade's three-row textarea was formerly read as absent and used the two-row
default: 66px control height and 445px panel height. Correct numeric reflection
gives 90px and 469px. Independent initial-layout controls with textual `rows:3`
produce 90px/469px on both old and repaired hosts; textual `rows:2` produces
66px. The old 433px panel expectation happened to include 445px within its
30px tolerance. Its expectation is now 469px with the same tolerance, and
explicit authored-rows/control-height assertions prevent ignoring the attribute
again. The standalone numeric fixture also compares numeric/text textarea
geometry. No layout behavior was changed to fit that expectation.

Concurrent physics work briefly produced a seven-argument declaration beside
a six-argument caller; that build stopped without a code change from this
repair. Its owner completed the caller before the final gate. Source hashes and
timestamps are in `gate-before-files.json`, `gate-built-files.json` and
`gate-after-files.json`; the engine-fix sources stayed unchanged. Physics remains
outside this repair's ownership. The verified owner release also passes Eden's
final landscape replay **32/32** over 43 events with zero pool warnings.

### Current-source startup release

After qualification, `make build-release-compile` builds current coherent source
without cleaning or overwriting a newer release with an older pin. Main
`lambda.exe` is byte-identical to
`temp/eden-inset/lambda-inset-current-release.exe`, SHA-256
`a14a2c54951ce6163dda74dc609f050b3172babaef437c5ad2a1f992a8ce715d`.
The release marker follows the binary timestamp; build and source hashes are
recorded under `temp/eden-inset/`.

Full Eden startup and the final landscape replay (**32/32**, 43 events, zero
pool warnings) pass on this exact release. The expanded presentation/ownership
replay passes **19/19** on each of interpreter/AUTO/JIT with collection every
allocation and freed-memory poisoning; all four logs have zero pool warnings.
Blade passes **13/13**, including explicit rows/control-height checks.
The unchanged strict Scene3D admission, mixed typed scene and outdoor garden
replays pass **3/3**. The final negative-case fix and Blade semantic expectation
are qualified by these focused follow-ups; the raw broad report above remains
preserved rather than being relabeled as a green uninterrupted run.

The original performance executable remains SHA-256
`0c0bdca95370b702f3fa27bbcdf1b7927d2318a5d779081b998fe5f6e33f104b`;
the earlier LR07-53 release remains
`25082cca98a7f41484eb8eb7c994c0d62fe0dcbfca170751bd7e21be4a5ad675`.
No Eden source, shared demo viewer or physics implementation was edited by
these repairs. The current main executable is left as release.

### Task-triggered pure view projection (2026-10-29)

Dense ecosystem completion exposed a host scheduling defect. The task's
notification synchronously committed January 31 to view state, then reactive
projection ran on that task's 2 MiB activation stack and faulted with E308
(`Stack overflow`). The task boundary contained the fault, leaving the DOM at
January 1. Dirty lookup and context ownership were correct; the pure view body
started but never returned. The same scene built from ordinary input on the
host stack succeeded.

Under **S12.1.1v3 / S12.1.3**, state writes remain in procedural handlers and
projection remains pure. **D5.1.1v3 / D5.4.1** require the task's activation and
the quiescent document host to keep their own execution ownership. A task-side
settle now preserves the dirty render-map entry and marks the existing
document-owned frame resource pending. The native host pump drains that flag
after callbacks unwind through the existing reconciliation/rebuild path.
Embedding ancestors expose a pending child without idle document-tree scans;
each actual drain enters its document's canonical evaluator. Direct DOM writes
and reads retain their existing synchronous path. Ordinary input reconciliation,
RAF requests, transaction boundaries, stack budgets and image expectations are
unchanged. The retained resource contains no GC pointer (**D4.5.2 / D4.5.3v2**).

The new `test/ui/template_task_reconcile` fixture generates a bounded 600-step
pure recipe, a 40-level scene hierarchy and 400 blade instances. The preserved
release fails 4 of its 11 assertions; the repaired isolated release passes all
11, including date commitment, nonempty rendering, exact image retention across
Run/Pause and locale changes, and a second task completion. The unchanged dense
Eden monthly replay passes 3/3 after the repair, versus 2/3 before it. The new fixture also passes 11/11 under explicit interpreter and JIT. Existing
task-created iframe, nested dispatch/restyle, lazy state and named frame
request/cancel/detach checks pass 20/20. The unchanged dense Eden growth replay
passes 58/58 across 74 events, including four monthly tasks and later Run/Pause
and locale changes. The selected completion profile records 8,672.777 ms of
pure transformation, 35.486 ms patching and 81.978 ms layout on the release host
under concurrent aggregate CPU load; correctness is qualified, instantaneous
regeneration is not claimed. Integer-cast lint and diff checks pass. Required
combined main aggregate gates and final main release qualification remain with
the coordinating build owner. Artifacts and the isolated release receipt live
under `temp/reactive-density/`.
