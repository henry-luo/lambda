# Lambda MIR Memory Usage — Editor.js Analysis

Date: 2026-08-14  
Status: diagnostic finding; no implementation change  
Scope: LambdaJS MIR Direct lowering and MIR-interpreter linking for the Editor.js UI fixture

## 1. Executive finding

The approximately 430 MB attributed to
`transpile_js_to_mir_with_preamble_len()` is not one buffer and is not primarily
an Editor.js heap, event-loop, or GC leak. It is the inclusive cost of compiling
and retaining a very large MIR program.

The dominant mechanisms are:

1. The 569,587-byte Editor.js bundle lowers to 4,238 MIR functions and 760,783
   executable pre-link instructions.
2. MIR instructions are individually allocated, comparatively large objects.
   On the measured arm64 build, `MIR_op_t` is 48 bytes and the base
   `MIR_insn` containing one operand is 80 bytes.
3. `MIR_link()` canonicalizes memory operands even when the MIR interpreter is
   selected. For Editor.js this adds 540,492 instructions in the three largest
   normalization paths, retaining 91,054,624 bytes (86.8 MiB) in those paths
   alone.
4. After linking, 1,323,025 live non-label instruction allocations account for
   approximately 224 MB. Labels, 363,752 local registers, function records,
   names, value-numbering tables, hash tables, AST pools, and allocator capacity
   are additional.
5. The interpreter avoids machine-code generation, but it does not avoid MIR
   simplification or the retained generic MIR representation.

This is representation amplification plus retention, not unbounded growth per
event-loop turn. Directly emitting the normalized instruction triples in
LambdaJS would only move the allocations earlier and would not solve the memory
problem.

## 2. Normative constraints

This report does not change a language or architecture ruling. Any subsequent
optimization must preserve these formal decisions:

- **D8.1.1**: compilation flows through typed AST to MIR Direct, and MIR-interp
  is the sole non-JIT execution path. There is no production AST-walking
  interpreter.
- **D5.3.1**: generated code uses safepoint-current canonical root slots; root
  stores are proportional to dirty live homes at `MAY_GC` boundaries.
- **D5.3.4**: rootability is semantic rather than inferred from MIR register
  type, and final rooting policy lives in `MirEmitter`.
- **D5.3.5**: argument roots use a fixed suffix of the side-root frame.
- **D8.4.3**: fallible JS/Jube helpers return an ERROR-tagged `Item`; MIR tests
  that returned tag and routes the same value through try/catch/finally.
- **D8.5.1**: imported-module caching is L1; lazy code generation is an approved
  experiment, not permission to weaken ownership or lifetime requirements.

Consequently, eliminating root-frame accesses, omitting fallibility tests, or
destroying MIR while callable function objects still reference it would be
incorrect even if it reduced RSS.

## 3. Workload and environment

The main measurement used:

```text
test/editable-editors/build/editorjs.js       569,587 bytes
test/editable-editors/fixtures/editorjs/typing.html
```

Capture environment:

```text
Commit:       e4f04868b
Platform:     Darwin 25.3.0 arm64
Build:        release, NDEBUG
Executable:   lambda.exe, 22 MB
```

Performance and memory conclusions in this report come from the release build,
as required by the repository performance-testing rule. Malloc Stack Logging
adds its own overhead, so its absolute process footprint is used for allocation
attribution, not as the normal-run RSS baseline.

The phase capture command was:

```sh
VIEW_MEM_STAGES=1 \
VIEW_MEM_STATS=1 \
LAMBDA_JS_MIR_VOLUME_STATS=1 \
LAMBDA_JS_MIR_PHASE_PROGRESS=1 \
./lambda.exe js test/editable-editors/build/editorjs.js \
  --document test/editable-editors/fixtures/editorjs/typing.html
```

A canonical finalized MIR artifact was captured separately with:

```sh
LAMBDA_MIR_DUMP_PATH=./temp/editorjs-finalized.mir \
./lambda.exe js test/editable-editors/build/editorjs.js \
  --document test/editable-editors/fixtures/editorjs/typing.html
```

The dump was used only for the analysis and was removed afterward.

## 4. Phase-level memory profile

The normal release capture reported:

| Phase | Time | Process footprint |
| --- | ---: | ---: |
| Function entry | — | 7 MB |
| Tree-sitter parse complete | 49 ms | 22 MB |
| Typed AST complete | 36 ms | 46 MB |
| AST-to-MIR complete | 1,996 ms | 504 MB |
| Imports loaded | — | 520 MB |
| MIR link complete | 118 ms | 392 MB |
| `js_main` complete | — | 412 MB |
| Cleanup complete | — | 149 MB footprint / 430 MB resident |
| Peak | — | 600 MB |

The important transition is typed AST to finalized MIR: the process footprint
grows by approximately 458 MB. Parsing and typed-AST construction are visible
but are not the dominant memory consumer.

The lower post-link footprint does not imply that linking allocated nothing.
Linking both creates retained normalized instructions and releases or purges
transient working storage. Peak footprint and retained post-link allocations
therefore have to be measured separately.

The cleanup `footprint`/`resident` divergence is macOS allocator and VM
accounting, not proof of 430 MB of reachable application objects. Live-allocation
inspection is required to distinguish allocated heap from mapped, reserved, or
dirty pages.

## 5. Finalized pre-link MIR volume

The MIR dump was 32,643,636 bytes and 924,261 lines. Its structural counts were:

| Metric | Count |
| --- | ---: |
| MIR functions | 4,238 |
| Executable instructions, excluding labels | 760,783 |
| Labels | 92,098 |
| Local registers | 363,752 |
| Maximum locals in one function | 29,026 |

The largest function is consistent with a very large top-level/module body and
its lowering-generated temporaries. A large single function is especially
expensive because register arrays and value-numbering tables are sized per
function and grow with its maximum register number.

### 5.1 Opcode distribution

The most frequent pre-link opcodes were:

| Opcode | Count |
| --- | ---: |
| `mov` | 282,649 |
| `call` | 111,969 |
| `eq` | 85,846 |
| `jmp` | 68,040 |
| `bf` | 49,907 |
| `bt` | 45,526 |
| `ursh` | 44,353 |
| `add` | 23,530 |
| `or` | 17,026 |
| `and` | 9,158 |
| `ret` | 8,476 |
| `ugt` | 6,404 |
| `ne` | 3,846 |

The combination of calls, tag extraction (`ursh`), comparisons, branches, and
moves is consistent with JS dynamic semantics, D8.4.3 error-lane tests, boxed
value handling, and precise-root write-back around `MAY_GC` calls.

### 5.2 Function-entry amplification

Small source probes showed the fixed cost of callable-function lowering:

| Probe | MIR functions | Executable instructions |
| --- | ---: | ---: |
| Empty script | 1 | 64 |
| One empty function | 3 | 99 |
| `function f(a) { return a; }` | 3 | 212 |
| One property read | 3 | 222 |
| One property call | 3 | 210 |
| Ten empty functions | 21 | 402 |

An ordinary JS function commonly produces an internal body and a public callable
entry in addition to `js_main`. This approximately doubles function-level frame,
symbol, and lookup-table overhead for function-heavy vendor bundles before the
function body itself is considered.

These probes do not imply that wrappers can be removed indiscriminately. JS
functions may escape through objects, closures, callbacks, reflection, or host
boundaries, so removal requires escape/call-shape proof rather than a source-size
heuristic.

## 6. MIR object representation cost

The measured structure sizes were:

| Type | Size |
| --- | ---: |
| `MIR_op_t` | 48 bytes |
| `struct MIR_insn` | 80 bytes |
| `MIR_mem_t` | 32 bytes |
| `MIR_var_t` | 24 bytes |
| `struct MIR_func` | 136 bytes |

`MIR_new_insn_arr()` allocates each instruction separately as:

```text
sizeof(struct MIR_insn) + sizeof(MIR_op_t) * (operand_count - 1)
```

Therefore the requested payload is already 80 bytes for a one-operand
instruction, 128 bytes for two operands, and 176 bytes for three operands,
before allocator size classes, bookkeeping, and fragmentation.

The Malloc Stack Logging snapshot contained large populations in the matching
allocation classes:

| Allocation population | Retained bytes |
| --- | ---: |
| 834,902 allocations at 160 bytes | 127.4 MiB |
| 341,833 allocations at 192 bytes | 62.6 MiB |
| 145,245 allocations at 96 bytes | 13.3 MiB |
| 59,036 allocations at 112 bytes | 6.3 MiB |

This is an object-count problem as much as a byte-volume problem. More than one
million individually allocated instructions incur allocator metadata and poor
locality in addition to their nominal payload.

### 6.1 Register metadata

Each MIR temporary also creates or grows retained metadata:

- a `MIR_var_t` record;
- a register descriptor;
- a register name string;
- name-to-register and register-to-name lookup entries;
- per-function register arrays, grown geometrically;
- value-numbering state used during simplification.

For 363,752 locals, `MIR_var_t` plus an equivalently sized register descriptor
already establishes a lower bound of roughly 17.5 MB before names, table
capacity, hash entries, alignment, or allocator overhead.

Two individual live `realloc` blocks observed beneath lowering/error-lane stack
ancestry were 16,793,600 and 11,927,552 bytes. They are consistent with
large-function array or lookup-table growth, but the generic MIR allocator does
not tag the exact table. They should not be assigned to a more specific owner
without dedicated allocator instrumentation.

## 7. Allocation snapshot

The Malloc Stack Logging run was paused before
`jm_destroy_mir_transpiler()`, after linking and execution while compiler-owned
state was still inspectable. Logging overhead raised the process totals to:

| Metric | Snapshot |
| --- | ---: |
| Physical footprint | 579.8 MB |
| Peak footprint | 668.7 MB |
| Live malloc storage | 448.4 MB |
| Live allocation nodes | 2,114,237 |
| Non-object Lambda allocations | 407.1 MB / 1,986,719 nodes |
| Render surfaces | 14.7 MB |

The dominant live call tree was:

| Owner | Live allocation result |
| --- | ---: |
| `MIR_new_insn_arr` | about 224 MB / 1,323,025 allocations |
| `MIR_new_label` | about 7.87 MB / 83,925 allocations |
| Tree-sitter parse allocations | about 14.5 MB / 145,107 allocations |
| MIR data items | about 1.86 MB / 11,966 allocations |
| Ordinary Lambda hash maps | about 6.0 MB / 6,420 allocations |

The 224 MB instruction figure includes the link-generated instructions discussed
below. Contributor figures such as frame finalization and error-lane emission
are subsets of that instruction total and must not be added to it again.

## 8. Root cause: link-time memory-operand expansion

### 8.1 Why interpreter mode still simplifies MIR

`MIR_link()` unconditionally runs `simplify_func(ctx, item, TRUE)` over each MIR
function before installing either a JIT or interpreter interface. Selecting
`MIR_set_interp_interface` skips machine-code generation, but it does not skip
this generic simplification.

The MIR interpreter's `push_mem()` requires:

```text
memory displacement == 0
memory index == 0
```

LambdaJS, however, naturally emits displaced accesses such as:

```text
i64:112(context)
i64:56(js_root_frame)
```

These encode `Context` fields, root slots, number homes, argument roots, captured
environment state, and object data efficiently in the pre-link MIR.

### 8.2 The transformation

For a memory operand using a base plus a nonzero displacement, MIR simplification
can insert:

```text
MOV displacement_immediate -> displacement_register
ADD base, displacement_register -> address_register
MOV [address_register] <-> value_register
```

The original instruction then consumes a plain register. Memory operands already
usable by a move may only need address normalization; a memory operand embedded
in another operation may need the additional load/store normalization.

The three dominant live `MIR_link` allocation stacks were:

| Simplification site | Instructions | Bytes |
| --- | ---: | ---: |
| Displacement/address-related `MIR_MOV` | 254,498 | 40,719,680 |
| Three-operand address `MIR_ADD` | 142,997 | 27,455,424 |
| Displacement `MIR_MOV` | 142,997 | 22,879,520 |
| **Total** | **540,492** | **91,054,624** |

The matching 142,997 displacement moves and address additions prove at least
142,997 full base-plus-displacement address normalizations. The remaining
111,501 load/store normalizations are memory operands that did not require both
address-construction steps.

Pre-link there were 760,783 executable instructions. At the paused post-link
snapshot there were 1,323,025 live allocations from `MIR_new_insn_arr`, a net
increase of 562,242. The three identified normalization stacks explain about 96%
of that net increase. Other simplification paths and the removal/replacement of
some original instructions explain the remainder.

This expansion is not function inlining. Initial link-path attribution can look
like inlining because `MIR_link()` also has an inline-processing pass, but the
allocation return addresses resolve to `simplify_op()` memory normalization.

## 9. Why Editor.js triggers the worst shape

Several effects multiply rather than merely add:

1. **Many functions.** Roughly two MIR entries per ordinary callable JS function
   create thousands of independent function records and register tables.
2. **Many calls.** There are 111,969 pre-link calls. Potentially fallible helpers
   require the D8.4.3 tag test and completion routing.
3. **Precise GC frames.** Rootable values crossing `MAY_GC` calls require D5.3.1
   canonical slots and D5.3.4 semantic liveness. Those slots are displaced
   memory accesses before linking.
4. **Fixed argument-root suffixes.** D5.3.5 call argument homes add more
   frame-relative stores, clears, and loads.
5. **Large functions.** One function has 29,026 locals, making geometrically
   grown register/value-numbering structures materially expensive.
6. **Generic-MIR normalization.** Each displaced memory operand can become up to
   three separately allocated instructions, and each added temporary expands
   register metadata again.

The frame finalization path accounted for approximately 154,927 instruction
allocations / 24.1 MB in its stack ancestry. Error-lane tests and routing
accounted for approximately 71,917 instruction allocations / 13.2 MB. These are
not leaks: they are repeated compiler products required by the current lowering
shape, and they overlap the 224 MB instruction total.

## 10. What the 430 MB attribution means

`transpile_js_to_mir_with_preamble_len()` is a wrapper around the complete core
compile/execute lifecycle. An allocation profiler assigning approximately
430 MB to it is reporting descendants of that call, including:

- source ownership, parsing, and typed AST construction;
- LambdaJS lowering data structures;
- the generic MIR module, functions, instructions, labels, operands, registers,
  names, and data items;
- link simplification and its value-numbering/lookup working state;
- interpreter interfaces and lazily generated interpreter code;
- realm/module initialization performed inside the same core call;
- temporary allocator capacity and fragmentation.

It does not mean that the wrapper itself calls `malloc(430 MB)`, that 430 MB is
all reachable after cleanup, or that the GC owns the majority of it.

Three different numbers must remain separate:

- **Live heap:** allocations that still have owners at the sampling point.
- **Process footprint/RSS:** committed or resident pages, including allocator
  slack and system frameworks.
- **Peak footprint:** live plus transient work at the high-water mark.

Conflating them makes allocator page retention look like an object leak and can
also hide a genuinely retained compiler representation.

## 11. Causes ruled out

### 11.1 Event-loop leak

The large jump occurs during AST-to-MIR and linking, before repeated event-loop
turns could explain it. The memory stays approximately stable while the fixture
is idle. The event loop can retain legitimate callbacks and handles, but it is
not the source of the compiler-attributed 430 MB.

### 11.2 GC heap leak

Approximately 416 MB remained live after initialization outside the ordinary GC
object categories, and the dominant stacks resolve to MIR allocation routines.
Forcing GC cannot reclaim a live MIR context referenced by compiled/interpreted
function objects. This is an ownership/representation issue, not missing tracing
of Editor.js objects.

### 11.3 Rendering surfaces

Surfaces accounted for approximately 14.7 MB in the instrumented snapshot. They
matter to complete UI memory but cannot explain hundreds of megabytes attributed
to transpilation.

### 11.4 Parser or AST dominance

The footprint was 46 MB after the typed AST and 504 MB after MIR lowering.
Tree-sitter had approximately 14.5 MB live in the stack-logging snapshot. Parser
and AST improvements may still be useful, but they are not the primary Editor.js
memory lever.

### 11.5 MIR inlining

The largest link-generated instruction stacks resolve to operand simplification,
not the inline pass. Disabling or tuning inlining would not address the measured
540,492-instruction normalization expansion.

### 11.6 Full-UI system mappings

Under UI debugging, process tools also showed lazy VM mappings such as roughly
80 MB of CoreGraphics/font state, a 64 MB Lambda-side root reservation, other
32 MB zones, and the render surfaces. These inflate whole-process RSS but are
separate from the live allocation stacks under the transpiler.

For context, previous whole-workload peaks were approximately 658–674 MB for the
Editor.js UI fixture, 644 MB for direct Editor.js execution, 38 MB for the
baseline process, 880 MB for CodeMirror, and 533 MB for ProseMirror. These
comparisons reinforce that large compiled vendor bundles dominate, but they are
not directly interchangeable with the 430 MB inclusive allocation attribution.

## 12. Fix directions

### 12.1 Highest-value architectural target

The interpreter needs a compact execution artifact whose lifetime is independent
of the generic, individually allocated MIR instruction graph. A viable flow is:

```text
typed AST
  -> MIR Direct
  -> linked/canonicalized interpreter code
  -> compact owned interpreter artifact
  -> release generic MIR instructions and simplifier metadata
```

Today MIR interpreter code is generated per function and the MIR context remains
the callable function owner. Safely discarding generic MIR therefore needs an
explicit lifetime/API contract. If that capability belongs in MIR itself, it is
upstream/vendor work and must not be patched directly in `lambda/mir/`; project
approval and an auditable patch under `patches/` are required.

A Lambda-owned compact interpreter artifact is an alternative, but it is a
larger architecture change governed by D8.1.1 rather than a local allocation
optimization.

### 12.2 Lazy per-function lowering

Cold vendor bundles frequently declare thousands of functions but execute only
a subset during page initialization. Deferring body lowering—not merely JIT
machine-code generation—would avoid constructing most MIR instructions at all.

This needs a stable callable trampoline or indirection boundary because direct
MIR symbol calls and escaped JS function objects must converge on the same
eventual body. Source/AST lifetime, closure metadata, module state, error
routing, and debug information all need explicit ownership. D8.5.1 permits lazy
codegen experimentation but does not settle those ownership issues.

### 12.3 Lambda-side MIR volume reduction

Secondary opportunities should be evaluated by instruction and local-register
counts, not only elapsed time:

- prove when a function cannot escape and omit an unnecessary public wrapper;
- coalesce error-lane tests only where the tracked D8.4.3 lane state proves a
  second test redundant;
- reduce redundant frame-relative loads/stores while preserving D5.3.1 dirty
  liveness and D5.3.4 semantic rooting;
- limit temporary-register creation in tag tests and completion routing;
- split pathological giant lowering functions only where control-flow,
  completion, and lexical-environment semantics remain exact.

These can reduce both pre-link instructions and the number of operands presented
to link simplification. They will not, by themselves, change the interpreter's
requirement for normalized memory operands.

### 12.4 Approaches that do not fix the root cause

Do not treat any of these as the solution:

- forcing GC after transpilation;
- periodically draining the event loop;
- pre-normalizing every displaced memory operand into the same three MIR
  instructions;
- disabling precise roots or using conservative native-stack scanning;
- deleting MIR contexts while heap function objects may still call them;
- using source-size thresholds to skip arbitrary Editor.js functions;
- patching vendored MIR in place.

## 13. Recommended measurement gates for a fix

A memory fix should be evaluated in release mode against the same Editor.js
fixture and should record all of the following:

1. Source bytes, MIR functions, pre-link executable instructions, labels, and
   locals.
2. Post-link live instruction and label allocations.
3. Count and bytes of instructions created by the three `simplify_op()` memory
   normalization sites.
4. Footprint after AST, after MIR emission, after imports, after link, after
   execution, and after cleanup.
5. Live malloc bytes and node count before compiler destruction and after it.
6. GC heap bytes, event-loop task/handle counts, and render-surface bytes as
   separate categories.
7. Correctness coverage for Editor.js lifecycle, paste, empty-data, block-ID,
   typing, and teardown behavior.

At minimum, success should mean a material reduction in post-link live MIR bytes
and normal-run peak footprint without increasing repeated-turn memory, changing
observable JS behavior, weakening D5 rooting invariants, or leaving callable
functions with dead code owners.

## 14. Source map

Relevant implementation points at the time of capture:

| Area | Location |
| --- | --- |
| Preamble wrapper | `lambda/js/js_mir_entrypoints_require.cpp:1654` |
| AST-to-MIR phase | `lambda/js/js_mir_entrypoints_require.cpp:957` |
| Pre-link volume count | `lambda/js/js_mir_entrypoints_require.cpp:1077` |
| Interpreter/JIT link selection | `lambda/js/js_mir_entrypoints_require.cpp:1143` |
| MIR link and unconditional simplification | `lambda/mir/mir.c:1970` |
| Displacement materialization | `lambda/mir/mir.c:3447` |
| Base-plus-displacement address add | `lambda/mir/mir.c:3485` |
| Load/store normalization | `lambda/mir/mir.c:3506` |
| Interpreter normalized-memory invariant | `lambda/mir/mir-interp.c:166` |
| MIR instruction allocation | `lambda/mir/mir.c:2171` |
| Function register tables | `lambda/mir/mir.c:531` |
| LambdaJS frame setup/finalization | `lambda/js/js_mir_hashmap_scope_utils.cpp:503` |
| Error-lane test and routing | `lambda/js/js_mir_completion.cpp:320` |
| Formal compilation pipeline | `doc/Lambda_Formal_Design.md`, D8.1.1 |
| Formal root invariants | `doc/Lambda_Formal_Design.md`, D5.3.1–D5.3.5 |
| Formal merged error ABI | `doc/Lambda_Formal_Design.md`, D8.4.3 |

## 15. Conclusion

Editor.js exposes a scaling limit in the current MIR-interpreter representation:
a medium-sized function-heavy source bundle becomes a million-object linked MIR
graph. Precise rooting and JS error semantics generate many legitimate
frame-relative operations; generic MIR linking then converts those compact
displaced operands into separately allocated address, load/store, and temporary
register instructions. The linked generic graph remains owned for the lifetime
of callable functions.

The principal memory win will come from avoiding or releasing that retained
generic graph—not from GC tuning, event-loop cleanup, parser micro-optimization,
or moving the same normalization into the LambdaJS emitter.
