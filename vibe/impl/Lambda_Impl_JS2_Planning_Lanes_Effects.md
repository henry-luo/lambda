# LambdaJS: shared plans, native lanes and scalar loop effects

> **Implementation record — 2026-09-10.** Continues the three follow-up tasks
> in [the struct-authority record](../Lambda_Design_Structs_JS.md), from source base
> `31d19bac9`. Authority: **D1.3**, **D2.4.1–D2.4.3**, **D3.4.5–D3.4.6**,
> **D5.2.3**, **D5.3.1–D5.3.4**, **D5.4.3**, **D6.2.2v2**,
> **D8.4.1v2–D8.4.3v2**, **D8.6.1–D8.6.3**. No ruling or value ABI changes.

## 1. Shared operation and construction planning

`MirConstructionPlan` identifies either Lambda's static `TypeMap` or a JS
module-owned name recipe. `MirFieldAccessPlan` selects that construction,
its field and physical offset. Both lowerings use the same candidate-first,
unique-field-fallback selection, on top of their existing shared indexed
candidate traversal. The plan never owns a runtime receiver, changes a source
contract, or substitutes speculation for a runtime guard (**D2.4.1**, **D8.4.1v2**).

JS object literals and ordinary function constructors use one recipe collector
and module-name owner. Constructors admit the consecutive prefix of plain
`this.name = expression` statements, up to fifteen public fields plus the
existing internal prototype slot. Unsupported definitions or keys stop/refuse
the plan. `new` expressions, constructor `this`, argument and return candidates
can select fields from these recipes.

Two recipe indexes live on the existing per-definition `JsCallableCode`.
Closures keep independent identity and environments. Allocation activates the
callee's owning module while resolving the recipe, so a caller's active module
cannot supply the names (**D5.4.3**, **D6.2.2v2**).

Lambda and JS now use the same `map_alloc_for_type` combined allocation for a
known layout: object and payload are published together. The JS adapter supplies
the extra capacity required by its fixed slots. Both profiles also use the same
`map_field_store` physical writer; the duplicate JS writer is removed.

Constructor reservations remain per instance. A property is absent until the
ordinary semantic writer creates it. Explicit null publication seals the shared
blueprint before a later instance can learn an incompatible native lane.
Parameter initializers, RHS evaluation and inherited setters may create keys in
a different order from the recipe. The shared writer detects a reserved slot
with a later already-published key, detaches its shape and appends that slot in
the enumeration chain. Its payload offset stays unchanged. The same list helper
serves JS's existing delete/reinsert path (**D3.4.5–D3.4.6**).

This preserves escaping/throwing constructors, default parameters, inherited
setters, nonextensibility, explicit replacement objects and `Reflect.construct`.
It does not reserve fields for every class or exotic constructor: those paths
retain their existing allocation and field-initialization semantics.

## 2. Native fast paths

Named accesses now validate the live slot's NameId, ordinal, offset, attributes,
storage domain and presence. Compatible transitions can pass without an exact
root-shape pointer match. The per-activation literal-shape handle cache and its
lazy resolver calls are removed. The guard tests the selected reservation bit,
allowing reads of an initialized slot while another field remains reserved.

Bool, int and string lanes join the existing float, null and container lanes.
Reads share one tag reconstruction path, preserving the int-tagged JS Symbol
identity. Stores retain lane/type checks and the ordinary owned writer on a
miss. Descriptor changes, deleted slots, incompatible layouts and host property
policies still use the semantic kernels (**D1.3**, **D2.4.3**).

Typed-array indexing consumes the runtime's existing `JsTypedArraySpec` table.
The shared emitter supplies width/sign decoding, element addresses, loads and
stores. Coverage is:

| Lane | Native read | Native write |
|---|---|---|
| Int8, Uint8, Int16, Uint16, Int32, Uint32 | Yes | Yes |
| Float32, Float64 | Yes | Yes |
| Uint8Clamped | Yes | Existing clamping kernel |
| Float16, BigInt64, BigUint64 | Existing kernel | Existing kernel |

Every access reloads the live view and buffer. Detached, resizable, shared,
length-tracking and incompatible storage use the existing kernel. Store
admission also proves writable unique byte storage. Integer stores use the
total ToInt32 leaf before narrowing; Float32 stores explicitly round through an
F32 MIR register. Numeric guards reject JS Symbols even though Symbols share
the integer Item tag. Coercing values fall back before raw storage is touched.

A numeric literal requested as F64 now emits a double constant directly. The
usual boxed-literal consumer still receives an immediate Item. This removes the
scratch-memory round trip from native consumers without changing BigInt or
generic coercion semantics.

## 3. Optimization from shared effects

`JIT_IMPORT_PURE_SCALAR_CALL` extends the existing import metadata. Its audited
leaves are total raw scalar operations with no language-visible memory effects,
GC, reentry, exception change or number-stack change. This is a stronger
contract than `NO_GC`; a noncollecting property lookup is not pure.

`em_hoist_loop_scalar_calls` serves Lambda `while` and JS `while`, `for` and
`do…while`. It counts loop register definitions, starts with unmodified function
arguments and immediate constants, and finds the scalar dependency slice needed
by audited pure calls. Only that slice moves before the loop's sole entry.
Unrelated arithmetic is not moved. A zero-trip loop may evaluate a total scalar
leaf early, with no language-visible effect (**D5.3**, **D8.4.3v2**).

Memory operands, unknown calls, mutable loop operands, division, unsafe machine
conversions and pointer-producing calls are excluded. Async/generator lowering
does not invoke the pass because resumptions have additional entries. The pass
unlinks MIR instructions without freeing them, preserving call-record identity.

Permanent MIR fixtures prove that constant JS ToInt32 and Lambda `fabs` calls
precede the loop test, while a changing ToInt32 operand remains inside. Behavior
fixtures cover zero trips, mutation of Math methods, coercion counts and
suspension. Generic Math calls themselves remain generic unless their existing
lowering has already selected an audited raw leaf.

The GC audit script now reads NO_GC metadata macro definitions as well as inline
initializers. Audited C math leaves participate in the same transitive check;
macro use cannot silently omit an import from inspection.

## 4. Validation

The release build passes 40,261/40,261 Test262 baseline cases, 24/24 JS MIR
fixtures, 89/89 Lambda MIR fixtures and 128/128 GC stress tests. The stress
corpus includes the three new JS behavior fixtures and both new MIR loop
fixtures, with JIT collect-always, randomized collection and MIR interpreter
collect-always execution, all with freed-memory poisoning.

The focused Node/Lambda oracle sweep additionally passes at collection intervals
1, 7, 31 and 100, plus an unforced run. Constructor-order cases were added after
the first sweep and pass the permanent stress suite.

The named-access MIR probe grows by nine instructions against the current
control artifact. The reviewed debug ceiling rises by seven instructions to
319 for the module and 258 for `js_main`; release emits four fewer. Root,
root-store, scalar-home and safepoint budgets are unchanged (**D8.6.1**).
Two existing release-only default-budget mismatches remain identical in control
and candidate: `_accumulate` has 128 instructions and the modvar closure has
13 safepoints. Their unrelated budgets are not changed.

The precise-root audit passes. The conservative GC-effects source audit reports
48 findings in both the control catalog and candidate, with no added or removed
findings. These concern the existing pool/native allocation paths under
`js_literal_shape` and `js_set_function_source`; this change does not suppress
them. Artifacts are under `temp/js2-plans/`.

The final `make test-lambda-baseline` run passes **5,273/5,287**, including
**16/16** MIR budget tests. The remaining fourteen failures are the seven
previously failing DNS/TLS/HTTP/net resource cases, each exercised by both a
named regression and the JS file sweep. No tests or network expectations were
disabled or changed. The input baseline separately passes **2,104/2,104**.

## 5. Paired release measurements

The existing paired benchmark runner executed the same twelve JS programs in
alternating control/candidate order, five pairs each, on macOS arm64 on AC power.
There were no concurrent builds or test suites during measurement. Every pair
has matching normalized stdout. Times below are execution medians in ms;
negative change means less execution time, not a change in benchmark work.

| Benchmark | Control | Candidate | Time change |
|---|---:|---:|---:|
| R7RS fib | 13.348 | 13.717 | +2.8% |
| R7RS fibfp | 15.251 | 15.559 | +2.0% |
| R7RS sum | 28.202 | 27.444 | −2.7% |
| R7RS sumfp | 2.459 | 2.433 | −1.1% |
| R7RS fft | 20.945 | 20.948 | +0.02% |
| AWFY nbody | 557.830 | 531.152 | −4.8% |
| AWFY richards | 1,009.715 | 970.852 | −3.8% |
| AWFY cd | 3,743.630 | 3,742.071 | −0.04% |
| BENG binarytrees | 26.726 | 21.979 | −17.8% |
| Kostya primes | 123.484 | 55.266 | −55.2% |
| Larceny gcbench | 605.837 | 477.195 | −21.2% |
| Larceny quicksort | 150.371 | 143.209 | −4.8% |

The geometric mean candidate/control ratio is **0.8924** over this selected
twelve-row set. This is not a full Result42 score or a comparison with untyped
Lambda. Construction-heavy and integer-array workloads benefit most; the first
sample does not establish a general improvement for unchanged numeric kernels.

A separate fifteen-pair repeat checks the small Fibonacci losses: `fib` is
13.310 → 13.452 ms (+1.1%, candidate wins 9/15 pairs), and `fibfp` is
13.191 → 13.124 ms (−0.5%, wins 8/15). All outputs still match. The `fib`
function and module have identical control/candidate MIR instruction counts.
These repeats do not support a consistent 2–3% regression, but neither justify
claiming a Fibonacci speedup.

Reproducibility artifacts:

- `temp/js2-plans/paired.json` and `paired-fib-repeat.json`: command lines,
  per-pair execution/wall samples, checksums and metadata.
- `temp/js2-plans/candidate-source.diff` and `source-manifest.json`: source
  checkpoint; implementation is uncommitted on base `31d19bac9`.
- Control release SHA-256:
  `cd998ccb5eb1b99bf337f3f23ea7650a8a90e70fec164b6ba6bc2d903c3f69ad`.
- Candidate release SHA-256:
  `3c95f7eac9daecba059ac2c17c9f7bb23869415b864c758e9ab38fd528d77464`.
- `lambda-baseline-verified.log`, `test262-final.log`, `gc-stress-final.log`,
  `js-mir-final.log`, `lambda-mir-final.log`, `gc-effects-comparison.json` and
  `release-budget-control.json`: validation and unchanged-failure evidence.

## 6. Remaining scope

This completes the shared planning/physical-storage path for the implemented
field and construction families and expands Number-lane coverage. The wider
JS2 proposal still includes additional operation families, class/exotic
construction plans, Float16/BigInt/clamped-store specialization, and broader
native result inference. Hoisting alias-sensitive field/buffer/bounds guards
and reclaiming scalar storage at loop backedges still require separate lifetime
and invalidation proofs (**D5.2.3**, **D5.3**, **D8.4.1v2**). They are not enabled
by the pure-scalar flag.
