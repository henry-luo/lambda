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
