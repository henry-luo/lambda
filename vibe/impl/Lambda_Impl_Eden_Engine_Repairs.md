# Eden engine repairs

2026-10-28. Working tree based on `d53f1a043`; no formal ruling changes.
Finite scope: deterministic procedural replay, the native Meadow reset replay,
and demonstrated blockers from the concurrent Eden simulation/model work.
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

## Native replay investigation — still open

The strict Meadow reset/hide-show assertions remain unchanged. A reduced page
starts paused; a second reduction retains a constant Scene3D value and changes
only one sun intensity. Both reproduce. Removing all animation clips still
reproduces. Ground/pond and animal-only reductions pass; static grass, tree and
flower subsets reproduce. Plain standalone instanced box/sphere batches pass.

Initial and restored native mesh geometry/pose/instance keys, light values,
camera, shadow transform and depth-map pixels match. Actual main Lambert-program
uniform values and GPU vertex/instance-buffer bytes also match across replay.
Reading shadow depth before the main pass changes the result to exact replay;
reading it after the finished main image does not. This is diagnostic evidence,
not an accepted synchronization/cache workaround. No tolerance, golden update,
capture delay, motion/shadow disablement, or extra context reset is accepted.
Scratch reductions and logs live under `temp/engine-eden/`.

The retained-camera reduction now compares the exact same baked source after a
write of the existing `fov:40`; projection/shadow-pass counters stay unchanged.
Actual per-draw VAOs, indices, attribute formats/divisors, uniforms, sampler
bindings and ordinary depth/cull/blend/sample state match. After-image raw
HDR extraction preserves the failing original assertion: sample 0 is exact,
while RGB differs in samples 1–3; alpha and all four per-sample depth images
are exact. GPU index bytes match their retained uploads too. Switching the
HDR depth attachment from a renderbuffer to a fixed-location multisample
texture does not fix replay.

A runtime-selected full-shader probe (no added vertex outputs) reproduces in
normals/positions and exact half-float-safe primitive IDs, predominantly in the
terrain draw; adding a flat vertex output itself changes the behavior and is
not a reliable control. Terrain alone, grass alone and terrain with each
single tested patch replay exactly. Terrain plus 30 distinct patches passes,
32 fails; 40 identical copies of an 81-instance patch passes, 60 fails. Thus
no individual species or spatial distribution is required. A separate direct
OpenGL grid/depth-to-MSAA fixture is exact, with and without repeated context
switches, so a generic driver-transition defect is not established.

A conditional bone-palette sampler was bound to the active depth attachment in
an unskinned shadow draw. Explicit white fallback removes that real feedback
hazard but does not fix the replay; it must not be presented as the root cause.
The retained-frame repair remains open.

The next reduction exported the original GPU buffers, exact float uniforms and
GLSL into an independent GLFW/OpenGL program. It reproduces without Lambda or
Radiant code, shadow rendering, context switching, animation, or texture
sampling. Combining the grass buffers reduces it to two draws: the indexed
terrain and one 4,106-instance blade draw. A 23-line vertex shader uses only
matrix transforms; an 11-line fragment shader outputs the normal and retains
other interpolants through a runtime-zero term. There is no inverse,
normalization, derivative, nonfinite normal, singular matrix, or out-of-range
attribute/index access. Initializing disabled generic inputs and explicitly
unbinding the HDR texture before writing do not fix it. `GL_NO_ERROR` is returned.

On Apple M4, OpenGL `4.1 Metal - 90.5`, the compact case changes
`[0,3484,7597,10135]` pixels in raw samples 0–3. Dropping the extra interpolants
makes all four exact. Removing the per-sample display pass reduces but does not
eliminate the failure. The stronger control qualifies every interpolant with
`sample` under GLSL 400 and enables `SAMPLE_SHADING` with
`MinSampleShading(1.0)`; it still changes `[0,2654,8781,11216]` pixels and returns
`GL_NO_ERROR`. Ordinary unspecified interpolation locations do not explain it. The self-contained approximately 480 KB source/input bundle is
`temp/engine-eden/repro-msaa/`, with source, shaders, buffer assets, build/run
instructions and a strict comparator. This establishes a concrete provider-level
invariance failure candidate, pending independent API/spec audit; it does not
authorize a shader-layout workaround. The relevant external contract is
[OpenGL 4.1 Appendix A.3 Rule 4](https://registry.khronos.org/OpenGL/specs/gl/glspec41.core.pdf)
and the [GLSL 4.10 interpolation rules](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.10.pdf).

All diagnostic engine exports, state queries, readbacks, stage probes and
counterfactuals have been removed. `gl_core.cpp` matches its pre-diagnostic
baseline byte for byte. The only retained native repair explicitly supplies the
bone-palette fallback in both color and shadow passes: sampler branches do not
exempt potential accesses from the framebuffer feedback rule
([ARB_framebuffer_object §4.4.3](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_framebuffer_object.txt)).
This real feedback safety repair is separate from the unresolved pixel replay.

## Qualification

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
