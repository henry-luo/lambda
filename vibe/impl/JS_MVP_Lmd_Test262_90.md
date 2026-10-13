# JS MVP: 90% of LJS Test262 coverage

**Date:** 2026-10-14. **Status:** in progress; initial P1/P2/P3 increments and native private-class execution are implemented. The corrected pin passes **24,803/40,261 IDs (61.6%)** after every required variant, plus focused, forced-GC and shared baseline gates. Another **11,432** passes are needed for 90%; performance acceptance remains open. Earlier captures remain build-specific evidence.
**Design:** [JS_MVP_Lmd §34](../jube/JS_MVP_Lmd.md#34-ninety-percent-of-ljs-test262-coverage).
**Scope:** extend the implemented MVP toward 90% of the frozen LJS passing
population, preserving ordinary benchmark performance. This plan stages the
remaining [full-parity roadmap](JS_MVP_Lmd_Test262_Parity.md); full parity remains
open beyond this milestone. No formal semantics or design ruling changes.

## 1. Target, entry evidence and counting

The target is the **40,261 distinct IDs** in
[test262_baseline.txt](../../test/js262/test262_baseline.txt), not all upstream
Test262 tests. At the documented baseline, `ceil(0.90 × 40,261) = 36,235`.
The maximum unsuccessful population is **4,026 IDs**. Keep the same corpus,
includes, required variants, exclusions, negative phases/types and async policy.

| Evidence | Recorded value and limit |
|---|---|
| LJS baseline | 40,261 IDs; corpus revision `673e9bacbe28590f501e2dcd817aadcc31899191`; baseline file SHA-256 `fcee29d073129156d50872b1a88acea5df313a04806134eaaddfc42f5f21365a` |
| Phase33 MVP required-mode audit | 21,747 IDs: 19,221 positive and 2,526 negative; candidate SHA-256 `e2804254cf893b003aa38397514bd93426227cdb8ac454fe049e17cf3cb2f95f` |
| Gain required from that audit | 14,488 distinct fully passing IDs |
| Navier follow-up | Compiler fix and shared/focused gates completed; the complete MVP required-mode audit was not repeated on that pin |
| Benchmark population | 84 non-jq workloads with output/backend contracts; four jq workloads retain historical evidence only |

These are archived results, not a current-head conformance claim. Their detailed
provenance is in the [phase33 record](JS_MVP_Lmd_Test262_5000.md#counting-and-comparison)
and its Navier follow-up. P0 captures the actual entry build and rechecks the
baseline manifest. A changed LJS population requires an explicit manifest delta;
do not silently alter the denominator or exclusions to reach the target.

Count an ID only when **all required modes pass on the same pinned candidate**.
Keep the canonical harness-selected result separate from the additional mode
audit, and report positive/negative totals separately. Correct negative outcomes
must match their expected phase/type; unsupported admission, crashes and timeouts
are unsuccessful. Keep async completion and module parse/link/evaluate outcomes
distinct. Do not combine passes from different candidate builds.

Archived selected-mode failures identify large work areas: class declarations
and expressions have **5,340** unsuccessful cases; TypedArray,
TypedArrayConstructors, ArrayBuffer and DataView have **2,846**; RegExp has
**746**. Class cases also depend on async and dynamic-source facilities. These
figures describe enclosing families, not independent gain predictions. Fresh
root-cause triage determines the implementation order within each package.

## 2. Governing boundaries

| Concern | Contract |
|---|---|
| Reuse | **D1.3v3**: reuse applicable untyped Lambda/lib substrate; preserve guest semantics and existing Lambda/full-LJS defaults |
| Representations | **D2.6.6v3**, **D2.6.9v3**, **D2.7.1**: retain shared container/nominal contracts and scalar ownership; no ordinary-object VMap conversion |
| Keys and shapes | **D3.4.3v5**, **S8.2.2v6**, **D4.6.1v4**: shared shapes and canonical UTF-8/lossless WTF-8 key identity |
| Class ancestry and captures | **S2.1.3v2**, **D6.2.3v2**: preserve Lambda ancestry and immutable Lambda captures; JS captures share mutable cells |
| Dynamic source | **D8.1.3v24**: eval, Function-family constructors and applicable `$262.evalScript` use the existing retained AST interpreter, including subsequent dynamic-body calls |
| Lifetime and completion | **D1.4v4**, **D5.2–D5.3**, **D8.4.3v2**: precise owners/roots and explicit completion through every frame |
| Suspension | **D5.1.1v3**, **D1.3v3**: shared activation infrastructure and one host event-loop authority |
| Specialization | **D8.4.1v2**: immutable compile-predicted fast paths with guards and shared semantic operations on a miss; no mutable per-site caches |

The existing [interpreter boundary contract](JS_MVP_Lmd_Test262_Parity.md#3-execution-boundary-and-interpreter-integration)
continues to apply. A common Item tag does not establish compatible callable or
class metadata. Audit the bridge before passing values between MVP and the
interpreter; use a compatible entry/profile and shared operations as needed.
Retain object identity, aliases, prototypes, receivers, new.target, thrown values
and one global/environment authority across calls. No copied object graph,
second interpreter or whole-script replay on unsupported ordinary syntax.

The remaining 10% is an explicit residual inventory, not a new exclusion list.
Keep existing LJS exclusions; general Node/DOM expansion and proposals outside
the frozen target do not enter this phase. Shared-memory concurrency and
finalization may remain residual only if the actual audited target is met.

## 3. Packages and dependencies

P0, P1, P2 and the first P3 Array increment are **in progress**; the remaining
packages are **pending**. P1–P6 are planned feature work; P7 chooses
additional fixes from measured residuals. Crossing the numeric target does not
silently mark unfinished packages complete. Publish any remaining family limits.

| Package | Dependencies | Deliverable and acceptance focus |
|---|---|---|
| P0: inventory and controls | None | Fresh canonical/required-mode MVP audit, immutable entry executable, frozen sources/oracles and failure classification |
| P1: environments and dynamic source | P0 | Remaining with/global/declaration/Annex B behavior; live eval bindings and interpreted Function-family bodies; reentrant mutation and escaped closures |
| P2: classes and construction | P1 for eval/private-name interactions | Private fields/methods/accessors/static brands, builtin subclasses, super/new.target and initialization order; async members finish with P5 |
| P3: synchronous library | P1–P2 where semantics depend on them | Array/String/Object/Reflect, numeric/Date/JSON/collections/globals, RegExp protocols and supported flags; generic receivers and effect order |
| P4: typed storage | P2–P3 for subclass/species behavior | Numeric/BigInt typed arrays, buffers, DataView, offsets/aliasing, indexed properties and target-required detach/resize behavior |
| P5: async and jobs | P1; P2 for class members | Promises/reactions/combinators, async functions/arrows/methods, async generators and for-await; shared activations, job order and precise suspended roots |
| P6: modules | P1 and shared registry; P5 for async evaluation | Linking, live bindings, cycles, namespaces, import.meta, dynamic import and top-level await |
| P7: residual and aggregate closure | P0–P6 | Common remaining failures/crashes, weak/storage/shared-memory facilities where needed, final required-mode audit and performance acceptance |

P1 first delivers synchronous dynamic code; async Function-family constructors
complete with P5. Synchronous module linking can be implemented before P5,
but dynamic-import settlement and top-level await require its job semantics.
P4 starts with fixed-length buffers and proves ownership before adding mutation
of backing-store state. Treat each increment as a separately validated change.

### P1. Environments and interpreter bridge

Reuse retained source/AST and binding facts. Direct eval observes live lexical
and variable bindings, TDZ/const, arguments aliases, this, new.target, home
objects and available private names. Preserve strict isolation, sloppy var
publication, shadowed eval and indirect/global resolution. Function constructors
resolve free names globally and retain the dynamic-source interpreter pin.

Use existing durable environment cells where bindings can escape or be observed.
Snapshot/copy-back alone cannot satisfy a callback that mutates a binding during
eval. Test both MIR-to-interpreter and interpreter-to-MIR calls, returned dynamic
closures, thrown-value identity, getters/proxy reentry and GC after the originating
frame exits. Complete with/unscopables and Annex B behavior through the existing
property/environment operations. Feature-free calls retain ordinary local storage.

### P2. Classes

Extend existing class plans and nominal metadata with class-evaluation-owned
private identities, rather than spelling-based public properties. Check repeated
class evaluation, nested scopes, static/instance private brand distinction,
methods/accessors, `#name in value`, receiver validation and private names in eval.
Preserve private/public initializer order and derived-constructor return/throw rules.

Builtin subclasses need correct internal instance storage as well as prototype
links. Validate Array/Map/Set/Error and later typed-buffer subclasses, alternate
new.target and species constructors. Preserve Lambda's ancestry contract and
ordinary public-field offsets. Async/private-member combinations join P5's gates.

### P3. Synchronous library

Complete existing operations before adding parallel implementations. Cover
generic Array methods including flat/flatMap, holes/inherited indices, species,
callback this/mutation and abrupt completion. Finish String/primitive conversion,
UTF-16 observations, Object/Reflect descriptors, numeric/Math/global conversions,
Date rules, JSON reviver/replacer and ordered Map/Set/Symbol behavior.

RegExp work includes custom exec, symbol protocols, lastIndex, captures/groups,
replacement callbacks, species and the supported d/v/Unicode behavior in the
frozen population. Reuse existing compile/match kernels. Test reentrant conversion
and observable operation order; do not bypass overridden methods for speed.
Vendor code remains outside the edit scope.

### P4. Typed arrays, buffers and views

Audit shared typed-array carriers and numeric storage before selecting an API.
Retain one backing-store owner across views, with correct byte offsets, element
widths, bounds, endian behavior, brands and numeric/BigInt conversion. Complete
constructor/prototype reflection, canonical numeric-index behavior, iteration,
methods and species. Cover view aliasing, buffer detachment, resize and transfer
where required by the frozen target; no stale data pointer survives a callback
that can change the store. Preserve fixed-length numeric fast paths.

The initial reuse audit found that `ByteBufferHandle`, `ArrayNum` buffer views
and the typed trailing carriers already provide the required storage ownership.
The exported LJS constructors also depend on LJS class/prototype metadata and
coercion; they are not directly compatible with MVP's nominal packed shapes.
Extract or parameterize those existing allocation/view operations before adding
the MVP semantic adapter (**D1.3v3**, **D2.6.9v3**, **D5.3.3**). Keep the current
numeric-array specialization where its compile-time proofs hold. This is a
reuse audit; the general P4 adapter is not implemented yet.

### P5. Promises, async bodies and iteration

Implement Promise resolution/assimilation, reactions/combinators and observable
job order through the existing event loop. Cover throwing then getters, reentrant
thenables and single settlement. Ordinary async bodies remain MVP MIR; dynamic
async bodies remain in the shared interpreter under D8.1.3v24.

Reuse activations for await/yield suspension, async-generator request queues and
for-await including AsyncFromSync adaptation. Test return/throw/IteratorClose
through suspended finally blocks, job/continuation errors and abandoned activations.
Run forced collection while frames and closures are parked. Add no Promise or
suspension allocation to ordinary synchronous functions.

### P6. Modules

Reuse source resolution, retained module ownership and the module registry;
implement JS binding/link/evaluation semantics over them. Test live imports,
TDZ/cycles, ambiguous star exports, namespace descriptors, import.meta and cached
module identity. Dynamic import and top-level await must propagate dependency
failures and settle through the shared job owner. Preserve canonical parse,
resolution and runtime negative phases. A source module uses the MVP module
path rather than whole-body eval as an implementation shortcut.

### P7. Residual closure

After each major package, update the complete failure map and root-cause groups.
Prioritize shared defects and interactions, including existing crashes. Candidate
remaining work includes weak-key semantics, ephemerons, WeakRef/finalization jobs,
shared buffers and Atomics; use existing collector and host scheduling owners.
Do not claim these facilities implemented from constructor presence alone.

Finish with at least 36,235 audited IDs and an explicit list of every remaining
failure/crash/timeout/missing outcome, its family and known dependency. Existing
crashes remain unsuccessful and visible; no new crash/timeout IDs are accepted.
Report actual gains without extrapolating from a selected family.

## 4. Reuse inventory and helper disclosure

Start with these existing owners. Paths below are relative to the repo root;
the [full-parity reuse map](JS_MVP_Lmd_Test262_Parity.md#4-reuse-map) provides
additional detail. An entry is an audit starting point, not proof of ABI compatibility.

| Area | Existing code to inspect first |
|---|---|
| Binding/call lowering | `lambda/js/mvp-lmd/mvp_lmd_mir.cpp`, `mvp_lmd_functions.cpp`; shared `lambda/js/js_ast_*` facts |
| Interpreter/environment | `lambda/js/js_dynamic_code.cpp`, `js_interp.hpp`, `js_interp_env.h`; `lambda/runtime/gc_environment.h` |
| Class/property semantics | Existing MVP `mvp_lmd_runtime_classes.cpp`, `mvp_lmd_objects.cpp`, `mvp_lmd_reflect.cpp`, `mvp_lmd_proxy.cpp` |
| Library/RegExp | Existing MVP arrays/strings/library/regexp modules; `lib/utf.h`, `lib/datetime.h`, Lambda numeric/JSON kernels; `lambda/js/js_regexp_compile.h`, `js_bt_regex.h` |
| Typed storage | Lambda ArrayNum/lane operations; `lambda/js/js_typed_array_carrier.hpp`, `js_typed_array.cpp` |
| Async/lifetime | `lambda/runtime/activation.h`, `suspended_activation.hpp`; `lambda/js/js_event_loop.h`; existing MVP generators |
| Modules/weak ownership | `lambda/runtime/module_registry.h`/`.cpp`; existing full-LJS module and collector weak-registration operations |

Apply this order: reuse a function; add a small option preserving existing
defaults; extract a shared neutral algorithm; only then introduce a narrow new
adapter. Promote a reusable static helper to its module header instead of copying
it. Full-LJS transitive runtime dependencies require an explicit compatibility
audit, especially outside the approved interpreter boundary.

Before coding each increment, disclose every new helper or promoted API to the
user and add its name/signature, reused owner, callers, semantic difference,
allocation/reentry effects, roots/completion contract and acceptance cases here.
Keep this inventory updated as implementation decisions become concrete.

### P1 object-environment helper inventory

These additions were disclosed before coding. **D1.3v3**, **D6.2.3v2** and
**D5.2–D5.3** govern shared algorithms, mutable JS capture cells and precise roots.

| Helper / shared data | Owner, callers and contract |
|---|---|
| `Item js_with_binding_is_visible_with_ops(Item key, Item object, Item unscopables_key, const JsWithBindingOps*)` | Extracts LJS's existing HasBinding / unscopables algorithm in `js_globals.cpp`. Both LJS and MVP supply property access, object classification and truthiness operations. Property callbacks may allocate/reenter/throw; four explicit roots retain inputs and the unscopables value. Returns a boolean Item or propagates the error. MVP supplies canonical NameRef keys; LJS supplies its existing property keys. |
| `Item mvp_lmd_with_binding(Item owner, Item key, Item value, int64_t operation)` | MVP adapter in `mvp_lmd_functions.cpp`; MIR imports it only when used. Delegates resolution to the shared algorithm and saved-base reads/writes/deletes to existing MVP property operations. Get/SetBindingValue rechecks HasProperty after resolution. Three roots retain owner/key/value across reentry. |
| `bool with_scope_boundary(AstNode*, AstNode* child, NameScope*)` | Compiler-only lexical shadowing predicate over existing AST scopes. No guest allocation or runtime effect. |
| `void plan_with_bindings(MvpLmdProgram*)` | Compiler-only AST plan. Builds reference views and captures statement-owned object cells through the existing closure machinery. Fresh cells per statement execution preserve escaping closures. Units without `with` return immediately and allocate no plan. |
| `LmdReference with_reference(LmdCompiler*, LmdBinding*, AstNode*)` | Emits resolution before RHS/argument effects and retains the selected object in a precisely rooted MIR register. The lexical fallback keeps its original declaration identity. |
| `LmdValue with_reference_operation(LmdCompiler*, LmdReference, AstNode*, int, LmdValue, bool allow_missing)` | Emits saved-reference access or lexical/global fallback, preserving strict errors, typeof-unbound behavior, deletes and call receivers. Uses existing boxing/ownership and explicit error propagation. |
| `js_array_unscopable_names[]` | Promotes LJS's existing immutable name table from `js_runtime.cpp`; both runtimes use it when materializing Array prototype metadata. No duplicate name list or new per-object flag. |

Acceptance cases include nested scopes and TDZ, var/destructuring effect order,
closure escape and per-iteration cells, calls/spread/tags/optional calls, primitive
boxing, abrupt completion, throwing getters/proxy Has/Get ordering, deleted
bindings and non-map unscopables objects. Dynamic-source admission remains
unchanged until the interpreter bridge has its own compatibility proof.

Annex B call assignment targets extend the existing compiler `reference`,
read/write and pattern lowering; no new helper. The shared early-error pass
already admits sloppy call targets and rejects strict ones. Evaluate the call,
then emit the existing ReferenceError completion before RHS evaluation or
numeric conversion. Retain an invalid-reference marker only in compiler data
so unreachable lowering never casts a call AST as a member AST. Logical
assignments require a simple target even in sloppy mode; tighten that existing
shared early-error condition. Cover simple/compound assignment, prefix/postfix
updates, for-in/of, iterator closing, a throwing call, and logical/strict early
errors. Ordinary references emit no additional runtime checks
(**D1.3v3**, **D8.4.3v2**).

The expanded family also exposed an entry-build crash on `() => ({}) = 1`:
the shared early-error function visitor passed concise bodies to statement
traversal, which skipped them. Select its existing expression visitor for
non-block bodies, preserving function strictness/parameter/private-name context.
Both runtimes now reject those invalid bodies during parsing rather than letting
an invalid assignment reach lowering. This adds no helper or runtime work.

Private-name validation also reuses `js_eval_private_resolve` before an LJS
runtime is active. Its existing lookup now returns no binding when that owner
is absent, fixing the entry crash without creating another runtime or admitting
unsupported private-element execution. The shared unary-expression validator
rejects deletion of private fields, including parenthesized/nested-arrow forms.
Both changes extend existing functions; no new helper (**D1.3v3**, **D8.1.3v24**).

The shared assignment-target predicate also rejects `new.target` and
`import.meta`, whose identifier-shaped AST carrier does not make them writable
bindings. Add pattern/call admission options to that existing predicate and
reuse it for compound assignment and updates; loop targets retain the same
validation. Preserve sloppy call-target and logical-assignment distinctions.
Focused cases cover direct/parenthesized assignment, updates and for-in/of.
No new helper or runtime work is introduced (**D1.3v3**, **D8.4.3v2**).

ImportCall uses the call AST carrier but is outside Annex B's sloppy call-target
extension. The same predicate rejects it before execution. The expanded entry
crash replay also found that named-function self cells were installed only after
parameter defaults. Move the existing `scope_initialize` for that name scope
before parameter evaluation, retaining it through the body instead of resetting
it. Default-created/escaped closures, strict versus sloppy writes, generator
creation and abrupt defaults use the same precisely rooted Lambda capture cells
(**D6.2.3v2**, **D5.2–D5.3**). No new helper, runtime field or allocation is added.

### P2 class helper inventory

**Private-element increment (2026-10-13):** retain shared shape/key contracts
(**D3.4.3v5**, **S8.2.2v6**), nominal ancestry (**S2.1.3v2**) and
**D5.2–D5.3** precise ownership. New compiler
`private_binding(program, site, name, declaring_function)` resolves lexical
private names and creates hidden bindings using the existing capture cells.
New `mvp_lmd_private_element(owner, key, value, operation)` reuses unique private
NameRefs, own-shape lookup/store and accessor calls for brand checks, reads,
writes and installation. Private methods/accessors use class-owned traced
template Maps; extend `mvp_lmd_class_initialize_instance` with a static-method
installation option. No extra dispatch is added to ordinary public reads.
Reuse `js_proxy_private_slots` by promoting it with optional allocator/revoked
access options, preserving full-LJS defaults; private elements belong to the
proxy itself and survive revocation. Focused gates include repeated class
evaluation, nested lexical identities, static/instance brands, initializer
order, return-object stamping, reflection invisibility and forced GC.

Follow-up uses the existing shared function-fact walker for class heritage and
computed keys: these execute in the enclosing function and must retain its
`this`, arguments and eval observations. Field initializers and method bodies
remain separate activation scopes (**D6.2.3v2**, **D8.4.1v2**). Extend the shared
early-error walker for reserved class names, forbidden noncomputed element names,
private-name expression positions and initializer `arguments`/`super()` restrictions
through arrows. Quoted keys compare their full length, preserving embedded NULs.
New compiler-only `js_ast_class_element_key(element, computed, statics)` shares
field/method key extraction across validation, function facts and MVP planning
(**D1.3v3**). It replaces repeated extraction blocks and adds no runtime allocation.
Anonymous class names use class metadata,
including when an explicit constructor method is present.

Class admission also checks the existing constructor closure capacity before
lowering. Field keys and initializers that already exceed the eight-bit closure
layout previously incurred large compilation work before the runtime rejected
them. Such classes remain unsupported and unsuccessful in Test262; this check
neither changes the runtime limit nor counts a timeout as a pass. Boundary cases
cover public and private fields. No new helper or ordinary runtime check is added.


`int mvp_lmd_error_kind(const TypeNominal*, const MvpLmdLibraryState*)` was
disclosed before coding. It consolidates Error-family recognition for class
admission and exception classification using Lambda's existing
`lambda_nominal_derives_from` (**S11.3.1v2**, **D2.6.9v3**). It allocates nothing,
does not reenter and returns the most specific native Error family or `-1`.
Native Error constructors retain their existing conversion/cause/iterator order;
an actual declared subtype supplies the allocation shape. Default derived
constructors delegate native allocation to the existing builtin entry instead
of dereferencing a nonexistent source constructor body. Validate explicit and
default inheritance, public initializers, AggregateError, alternate new.target,
returned objects, Error tags, thrown identity and the retained nominal chain.

Error branding uses an optional private `error_data_key` in the existing library
state, disclosed before coding. Native Error allocation alone installs its own
private shape slot with `name_pool_create_unique_private` and `map_shape_set`;
existing construction roots retain the instance across allocation. Object tags
check that own slot without prototype traversal or proxy dispatch. This preserves
nominal ancestry while distinguishing `Reflect.construct(Plain, [], ErrorSubtype)`
from actual Error allocation (**D1.3v3**, **D2.6.9v3**, **D5.2–D5.3**). Error
prototypes and objects inheriting from Error instances have no ErrorData brand.
No new helper or ordinary-container flag is introduced.

### P3 Array reuse inventory

No new runtime helper. Extend `mvp_lmd_array_visit` with backwards find modes
and flatMap dispatch. Extend `mvp_lmd_array_flatten(Item, double, int64_t,
Item* mapping = nullptr)` with an optional borrowed `[callback, thisArg]` pair;
the visitor's existing roots retain both values. Its existing `flatten_into`
kernel specializes mapping at compile time, so ordinary flat iteration keeps
its current roots and loop. Mapping runs only at the outer level, before
IsArray/LengthOfArrayLike and destination writes; recursive flattening does not
reapply it. Reuse Lambda scalar-home adoption, species, property and invocation
operations (**D1.3v3**, **D2.6.6v3**, **D5.2–D5.3**). Validate holes versus
find's Get semantics, inherited indices, length snapshots, species/proxy effects,
callback receiver/abrupt completion and scalar results across forced GC.

The first Array candidate measured a repeatable map-lookup difference despite an
unchanged MIR instruction stream and unchanged native Map-helper instructions
apart from addresses. The later build-provenance correction in §7 invalidates
code-regression attribution from that mixed-build comparison. The existing cached read path still sends
writes of the same live entry through a native call. Extend that same guarded
ordinal path to `set`, reusing `array_index_write` for the private entry array's
owned scalar storage; no new helper or cache is introduced (**D8.4.1v2**,
**D2.6.6v3**). Resolve guards after argument effects, check live entry/key identity,
retain the existing miss path, and validate clear/delete/reinsert, object identity,
subnormal scalar lifetimes, zero signs and chaining before timing again.

The matching-release follow-up exposed repeatable knucleotide overhead: its
fresh substring keys miss the raw-identity guard, after which native lookup
repeats the live-entry checks and compares equal string content. Reuse the
existing `mvp_lmd_same_value_zero` on cached-write identity misses, exposing its
existing implementation with C linkage and an `int64_t` result for MIR. This
API adjustment was disclosed before coding; no new helper or cache is added.
The import does not allocate, collect or reenter; it borrows both Items and
reuses the existing number, string and BigInt comparisons. A byte-equal string
already matches the canonical stored key; other spellings retain native
canonicalization on a miss (**D3.4.3v5**, **D8.4.1v2**, **D5.2–D5.3**).
Validate fresh substring keys, scalar ownership, NaN, signed zero, BigInt and
UTF-16 surrogate concatenation before timing the new pin.

An experimental cold-setup change moved the repeated `js_ast_collect_parameter_facts`
and `ast_index_parent` work out of `mvp_lmd_function_new` allocation. Formal
length and non-constructibility were computed in the existing compile plan
instead. It added no helper or callable/container field (**D1.3v3**,
**D8.4.1v2**), but the matching 31-round sieve result stayed at 0.090 versus
0.087 ms. The experiment was reverted; `callable1/` preserves its patch, pin
and timing. The precise cause of the cold-layout difference remains unproven.

## 5. Protecting ordinary performance

Retain direct scalar lanes, dense-array storage and proven object/call paths.
Create descriptor/private/buffer/interpreter/async metadata only where its
semantics require it. No universal is_normal flag, blanket environment allocation,
dormant async frames or mutable per-site caches (**D8.4.1v2**).

Propagate dynamic-source and callback exposure through retained call/escape facts;
only affected bindings, properties and storage assumptions lose their proofs.
The Navier fix is a regression case: distinct static global-receiver fields must
not invalidate unrelated local numeric-key proofs. Conversely, a reachable alias
that can mutate a relevant binding or prototype must retain the semantic path.
Revalidate affected storage after reentry; never preserve speed with stale proofs.

Inspect emitted MIR size, allocation/metadata counts and helper admission when
an increment changes a hot path. These explain regressions; benchmark self-times
establish performance acceptance. No benchmark-specific semantic shortcuts.

## 6. Validation and performance gates

### Entry and per-increment checks

Read [Developer Guide §7](../../doc/dev/Developer_Guide.md#7-worktrees-and-agent-gotchas)
before building. Freeze the entry source revision, dirty diff if any, optimized
release executable, dependent modules, flags, host and relevant hashes. Verify
the actual build configuration; candidate and controls must match. An optional
release-profile comparison requires release-profile controls too.
Use the existing MVP backend route in the canonical Test262 runner. Preserve its
sources/includes, expected phases/types, timeouts and async completion policy.

For each increment, reproduce its root cause before changing code, run focused
semantic and forced-GC/poisoning checks, then canonical affected-family tests in
every required mode. Cover generic, specialized and reentrant paths. Existing
entry passes must remain passing; new failure/crash/timeout outcomes require
individual quiet replay with original logs retained. Fix runtime causes, never
modify `test_js_test262_gtest` to hide them.

Run the shared `make test-lambda-baseline` and `make test262-baseline` gates for
runtime integration and at final closeout. The full-LJS result is separate from
the MVP audit. Pin executables so builds cannot replace an active validation
binary, and record dependent module provenance. Restore the ordinary release
executable after aggregate gates using the documented checkout-safe procedure.

### Benchmark protocol

1. Freeze all **84 non-jq** sources, wrappers, workload sizes, expected outputs
   and oracles from the existing MVP benchmark contract. Keep the entry control
   and an identical control copy; preserve relevant older phase controls too.
2. Use matching optimized release MVP MIR builds and script self-reported time. Verify
   backend admission and oracle output for every measured run. Compilation and
   startup timings, if collected, are separate diagnostics.
3. Reuse `test/benchmark/run_paired_benchmarks.py` and existing MVP orchestration
   under `test/benchmark/js_mvp_lmd/`; retain raw samples, source/binary hashes,
   warmup policy and run ordering. Do not overlap timing with builds or suites.
4. After each increment, run contracts and interleaved timings for affected
   families. At final closeout, measure **all 84**, not just selected hot tests.
   Use at least seven measured rounds for the whole-population screen.
5. Repeat suspected regressions for **31 interleaved rounds**, comparing the
   candidate with both controls and reporting paired bootstrap confidence
   intervals. Investigate control/control drift before attribution. Retain
   inconclusive results and repeat when necessary; noise is not a pass.
6. A confirmed individual slowdown blocks completion, even if the geometric mean
   improves. No blanket percentage allowance is introduced. Carry older gaps
   against their original controls; a new entry baseline cannot erase them.

Retain the Navier follow-up improvement and the previously deferred tiny
text-search difference as separate historical evidence. That deferral does not
permit a new attributable text-search regression in this phase. Earlier Fasta,
three-way-merge, Raytrace3D and phase-control uncertainties remain tracked until
resolved against their own controls; fresh-entry parity alone cannot close them.

Do not execute, preflight or time `text/jq_bf`, `text/jq_mix`, `text/jq_records`
or `text/jq_tree`. They retain historical evidence under the user's instruction.
State that fresh performance acceptance covers 84 workloads, not all 88.

### Final required-mode audit

Run the complete frozen target and all additional required variants against one
final immutable MVP candidate. An ID passes only if all modes and completion
requirements pass. Use the established isolated metadata overlay for additional
modes without modifying original test sources, includes or metadata. Recheck
all entry passes, and report gains/losses, positive/negative counts and the exact
residual set. Preserve batch and individual outcomes; retries must remain visible.

Any production-source change after this audit creates a new candidate and
requires the affected gates and final audit to be established for that candidate.
Whole-target coverage and performance must refer to the same implementation.

## 7. Evidence and completion record

Keep temporary captures under `temp/mvp_test262_90/`, with separate entry and
candidate directories. Record build/source/dependent-module hashes, commands,
corpus/runner/metadata/ID hashes, outcomes, strict-mode audit, helper disclosures,
benchmark samples, control comparisons and gate exits. The final plan update
summarizes evidence and links artifacts; it must not mix candidate results.

Completion checklist:

- [ ] P0 entry audit and benchmark controls captured.
- [ ] P1–P6 implemented and their interaction/forced-GC cases validated.
- [ ] At least **36,235/40,261** distinct required-mode passes on the final build.
- [ ] Zero lost entry passes and no new crash/timeout IDs; all residuals recorded.
- [x] Shared Lambda and full-LJS baseline gates pass on the latest increment.
- [ ] All 84 non-jq contracts and paired timing gates pass; no confirmed new regression.
- [ ] Historical performance residue, feature limits and jq exclusions reported separately.
- [x] Latest increment source/binary provenance checked and ordinary release executable restored.

Coverage, feature completion and performance acceptance are separate reported
statuses. Do not call the whole phase complete while a required gate is open.
This plan does not request a Result-series publication, commit or baseline update.

### 2026-10-13: first P1 increment

Implemented `with` object environments, lexical shadowing/TDZ, captured object
environments, saved-reference writes and call receivers, and unscopables checks.
Array prototype metadata uses the promoted LJS name table. Shared early-error
validation now visits the object expression and body and rejects declarations
in statement position. The handwritten C parser preserves the `let [` lookahead
restriction across a newline. These implement existing guest contracts;
**D1.3v3**, **D6.2.3v2** and **D5.2–D5.3** remain unchanged.

| Evidence | Current result |
|---|---|
| Fresh entry pin | `temp/mvp_test262_90/entry/lambda.exe`, SHA-256 `32a6174ad5e6769bdbe0df2b8e911b09ccd7770f084e7d5f4104be436aefb394`; release-profile build at `9bf35142ebad937b6bd96521d970cf7cb9abf2f7`, with manifest/source/module hashes |
| Candidate pin | `temp/mvp_test262_90/with3/lambda.exe`, SHA-256 `e299ca7422f507b5dd7e47542b2c0ae2faecb3b96aa79d5de3d842a84fb54858`; same build profile plus this increment |
| Affected Test262 population | **122/212** audited distinct passes: **106 positive, 16 negative**; all **8 additional required variants** pass; entry **10/212**, gain **112**, zero lost passes or crashes/timeouts in this family |
| Focused MVP gate | **198/198**; selected forced-GC/freed-memory-poison checks **6/6**; includes absence of the new helper from an ordinary unit's emitted MIR |
| Lambda baseline | `make test-lambda-baseline JOBS=2 TEST_JOBS=2`: **6,818/6,818**, exit 0 |
| Full-LJS baseline | `make test262-baseline JOBS=2 TEST_JOBS=2`: **40,261/40,261**, zero regressions, exit 0 |
| Whole-target entry census | Running against the immutable entry pin; replay and required-mode closure still pending |
| Benchmark gate | All **84 non-jq** source/oracle/backend contracts pass on candidate and both identical entry controls. Paired self-time capture is pending; no no-regression claim yet |

Evidence: `temp/mvp_test262_90/with3/{manifest.json,family-audit.json,family/,strict/}`,
`all-focused-pinned.log`, `forced-gc-pinned.log`, and the phase-root baseline logs.
Earlier `with1`/`with2` captures retain intermediate failures; they do not supply
the candidate count. The affected-family gain is not added to an older whole-suite
count to manufacture a current aggregate result.

The 90 remaining affected-family cases include eval/dynamic-source and async
dependencies and two typed-array prototype-chain cases. The interpreter bridge,
remaining P1 behavior, and P2–P7 are still open. The **36,235-case goal has not
been reached or claimed**, and full-phase performance acceptance is pending.

### 2026-10-13: Error subclasses and Array visitors

Native Error subclasses now preserve their declared Lambda nominal ancestry
while the native allocator retains Error's conversion, cause and aggregate
iteration order. Default derived construction delegates to native constructors
without reading a nonexistent source AST. Exception classification reuses Lambda
ancestry (**S11.3.1v2**, **D2.6.9v3**); the subsequent brand correction below makes
object tags depend on actual Error allocation.

Array `flatMap`, `findLast` and `findLastIndex` extend the existing visitor and
flattening operations. Mapping runs before flattening each result; reverse find
uses Get even for holes. Species, inherited indices, callbacks, proxy effects and
scalar ownership retain the shared operations and precise roots.

The combined candidate is `temp/mvp_test262_90/arrays1/lambda.exe`, optimized
release-profile SHA-256
`b7e0e5ac71c4fc56726da4bfabaf1acb9820f471cb4c420ea8c5576ffb595352`.
Its manifest retains source hashes and the entry manifest; `family-audit.json`
records the required-mode intersection and every gained/lost/residual ID.

| Affected family | Entry | Candidate | Gain |
|---|---:|---:|---:|
| with / unscopables | 10/212 | 122/212 | 112 |
| Error / NativeErrors / AggregateError | 156/194 | 193/194 | 37 |
| flatMap / findLast / findLastIndex | 8/72 | 63/72 | 55 |
| Combined distinct IDs | **174/478** | **378/478** | **204** |

All **269 additional required modes** were checked on this candidate; **259**
pass, and the ten unsuccessful IDs also fail their canonical mode. The audited
378 comprises **362 positive and 16 negative** cases. There are **zero lost
entry passes and zero crashes/timeouts in these families**. This is an affected
population result, not a whole-target coverage count.

Focused MVP checks pass **201/201**, selected forced-GC/poison checks **11/11**,
and the fresh Lambda baseline **6,821/6,821**. All **84 non-jq** output/backend
contracts pass on the candidate and both identical entry controls. The fresh
full-LJS baseline passes **40,261/40,261**, with zero regressions. This intermediate
candidate was superseded after the paired map-lookup regression below; logs and
contracts remain under `arrays1/`.

The entry canonical census has finished: **21,293 pass, 18,933 fail, 25 crash
and 10 timeout** outcomes. Its **997 unresolved IDs** include missing batch
results reported as failures. Individual replay and additional required modes
remain pending; these raw outcomes are not an audited entry coverage count.

Residuals include `new Function` in the AggregateError fallback case, eight
resizable-buffer Array cases and a typed-array own-property case, in addition to
the 90 with-family residuals. No admission or harness workaround was introduced.
The interpreter audit confirms that `js_interp_call_function` expects a
`JsFunction` payload/call activation, while MVP uses `MvpLmdCallable`; direct
casts are invalid. The bridge still needs a compatible execution profile,
shared property/call/global authority and live environment-cell integration
before eval/Function admission can open (**D8.1.3v24**, **D6.2.3v2**).

### 2026-10-13: Map writes, Error branding and assignment targets

The Array candidate's 31-round paired map-lookup run measured **+2.7%** against
entry, with a positive lower confidence bound against both identical controls.
The later provenance correction identifies this as a mixed-build comparison,
which cannot establish a code regression. The native helper and emitted MIR
were unchanged apart from addresses. The existing cached ordinal write path
now reuses Array's owned scalar store after live key/entry guards. On `map1`
(`f841c21cade613ee2b4169dca8f591e38b6dc563b5ad75ecdae4be0507271f98`), 31 rounds
measure **6.349 vs 7.421 ms**, **14.4% faster**; the 95% paired interval against
entry is **13.4–16.1% faster**. Map iteration is unchanged and knucleotide's
**+0.27%** interval includes parity. These measurements apply to this intermediate
pin. The wider seven-round screen also retained a
text-search **+0.22%** difference and inconclusive small differences requiring
separate follow-up; no all-workload parity claim follows from the Map result.

ErrorData now uses the private allocation brand described in §4. Alternate
new.target alone cannot give an ordinary object an Error tag. Inheritance,
proxies, own keys, Error prototypes and forced GC are covered by focused cases.

Assignment-target work fixes the reproduced entry crashes on sloppy calls and
invalid concise arrow bodies. The shared early-error pass additionally rejects
call targets in logical assignments. The expanded conformance population is
**803 IDs**, including **325 assignment-target cases**, with **572** additional
required modes. The pinned `arrow1` candidate is under validation; its source,
binary and test provenance are retained separately from `annex2`'s failing
intermediate captures. No Test262 source, baseline or canonical harness changed.

Individual replay of the entry census's **997 unresolved IDs** records
**455 pass, 282 fail, 254 crash and 6 timeout** outcomes. Combined with the
original resolved cases, the canonical entry result is **21,748 pass,
18,253 fail, 254 crash and 6 timeout**, with no missing IDs. The 260 crash/timeout
cases and the whole-target additional-mode audit remain open. Keep both captures
and their manifests; this canonical total is not a fully audited coverage count.

### Expanded increment candidate (private1)

`temp/mvp_test262_90/private1/lambda.exe` is the optimized release-profile MVP
MIR pin, SHA-256
`e814c1a28f21b0adccd501604118b06722bf1cee18e170a9b593d4c30b4b5161`.
The source manifest and patch, unchanged dependent modules, canonical runner,
metadata and per-mode outcomes are recorded with it.

| Affected family | Entry | Candidate | Gain |
|---|---:|---:|---:|
| with / unscopables | 10/212 | 122/212 | 112 |
| Error / NativeErrors / AggregateError | 156/194 | 193/194 | 37 |
| flatMap / findLast / findLastIndex | 8/72 | 63/72 | 55 |
| Assignment targets | 311/325 | 322/325 | 11 |
| Private-name validation entry crashes | 0/200 | 200/200 | 200 |
| Combined distinct IDs | **485/1,003** | **900/1,003** | **415** |

All **772 additional required modes** were checked; **760** pass, and the twelve
unsuccessful IDs also fail the canonical mode. The audited 900 contains
**376 positive and 524 negative** cases, with **zero lost entry passes and no
crashes/timeouts in this population**. The private-name gain is early-error
validation; private-element execution remains pending. Entry comparisons reuse
only captures from the frozen entry SHA and include its separately recorded
with-mode audit. `family-audit.json` lists every gain/loss/residual and capture hash.

Focused checks pass **202/202**, including construction branding and assignment
effects; forced-GC/freed-memory-poison checks pass **15/15**. Fresh shared gates
pass **6,822/6,822** for Lambda and **40,261/40,261** for full LJS, with zero LJS
regressions. The ordinary release executable is restored. The seven-round
**84 non-jq** screen passes every output/backend contract; eleven suspected
differences have 31-round repeats. A subsequent provenance audit found that
the entry pin is regular release, despite its original manifest saying
release-profile. Those mixed-build timing comparisons remain diagnostic evidence,
not the final performance gate; see the correction below.
The complete 90% required-mode audit and P1–P7 feature closure remain open.

Artifact correction: a later validation accidentally reused the historical
`map1/` directory. Its top-level executable/manifest/focused captures were
overwritten and its family capture may be mixed. `map1/ARTIFACT_COLLISION.json`
quarantines that directory; do not use it as a coherent candidate gate. Its
historical timing subdirectories remain unchanged and identify their original
SHA. Their archived `maps31/candidate.exe` matches that SHA exactly and restored
the historical top-level executable; its overwritten manifest and family
capture remain quarantined. Frozen `entry/`
and `self1/` are unaffected. The replacement `map_equal1/` starts fresh, and its
orchestration refuses to reuse an existing output directory.

### Benchmark provenance correction and cold setup follow-up

The entry SHA `32a6174ad5e6769bdbe0df2b8e911b09ccd7770f084e7d5f4104be436aefb394`
matches the ordinary release restoration recorded in
`temp/mvp_navier_regression_20261013/gates.json`. The entry capture copied
`lambda.exe` after a release-profile build, whose actual output was
`lambda-profile.exe`. Preserve the original manifest and every capture;
`entry/build-provenance-correction.json` records this mistake and
`entry/benchmark-control-release.json` supplies the corrected build metadata.
The Navier source manifest matches the entry's changed JS sources; the binary
identity and canonical conformance outcomes remain valid, but the original
build label and matched-build performance attribution do not.

The same increment's regular release pin is
`private1-release/lambda.exe`, SHA
`d2c3b5013d69fe873377b47948bcc0e4dc85f0ed8da63f1c929fbe2245b91042`,
from the successful full-LJS gate. Its source hashes match `private1`.
Matching-release 31-round repeats record:

| Workload | Candidate / entry ms | Difference | 95% paired interval vs entry |
|---|---:|---:|---:|
| Richards | 70.881 / 71.044 | −0.23% | −0.56% to +0.33% |
| Sieve | 0.090 / 0.088 | **+2.27%** | **+2.27% to +4.60%** |
| knucleotide | 3.741 / 3.735 | +0.16% | −0.29% to +0.83% |
| Map lookup | 6.459 / 7.320 | **−11.76%** | −12.66% to −10.64% |

Sieve's interval is positive against both identical controls, so its regression
remains open. MIR differs only in embedded addresses, and intrinsic bootstrap
is outside entry timing. Separate amplified/shortened diagnostic sources locate
the difference in cold class/setup work, with steady-state loop timing at parity.
Those diagnostics do not replace the frozen benchmark. Moving immutable callable
metadata into the compile plan did not resolve it and was reverted (§4).
Matched-release performance acceptance remains pending.

### Conformance increment candidate (self1)

The meta-property/ImportCall validation fixes and named-function default-capture
repair are pinned in `temp/mvp_test262_90/self1/lambda.exe`, regular optimized
release MIR, SHA-256
`2b346ad9c68765cef5087a627493936b2036cd618b212591f5bc9c7c4cffa459`.
The four named-function/generator crashes were reproduced on `private1`;
`named-self/backtrace.log` shows the generated read through an uninitialized
capture cell. The existing name-scope initialization now precedes defaults.
The initial meta-property test failure remains under `meta2/`; pattern-form
meta properties are handled by the shared target predicate, not a test change.

| Affected family | Entry | Candidate | Gain |
|---|---:|---:|---:|
| with / unscopables | 10/212 | 122/212 | 112 |
| Error families | 156/194 | 193/194 | 37 |
| New Array visitors | 8/72 | 63/72 | 55 |
| Assignment targets | 311/325 | 324/325 | 13 |
| Private-name validation | 0/200 | 200/200 | 200 |
| Other recorded entry crashes | 0/47 | 47/47 | 47 |
| Combined distinct IDs | **485/1,050** | **949/1,050** | **464** |

All **812 additional required modes** were executed; **802** pass, with ten
IDs also failing the canonical mode. The audited total is **380 positive and
569 negative** cases. There are **zero lost entry passes, crashes, timeouts or
missing outcomes in this population**. All **254 previously recorded entry
crash IDs** now pass every required mode. The six entry timeout cases remain
outside this selected replay. This is not a whole-target coverage count.

Focused checks pass **203/203** and forced-GC/freed-memory-poison checks pass
**16/16**. Shared gates pass Lambda **6,823/6,823** and full LJS
**40,261/40,261**, with the exact release SHA restored after the gates. The
matching-release 31-round repeat shows Map lookup **14.4% faster** and Richards
and Map iteration at parity. Sieve remains **2.27% slower** (0.090/0.088 ms),
and knucleotide is **0.99% slower** (3.777/3.740 ms); both have positive paired
intervals against both identical controls. Performance acceptance remains open.
The broad screen completed **78/84** workload contracts before it was explicitly
stopped for the cached Map-write follow-up; raw samples and the termination
record remain under `self1/screen84/`. This partial screen is not a final gate.
The
previous `meta3` increment passed Lambda **6,822/6,822** and LJS
**40,261/40,261**; its timing was explicitly deferred when the remaining-crash
replay identified the final two repairs. No timing was executed for that
superseded pin, and the defer record is retained separately from actual failures.

The remaining 101 selected failures comprise 90 with cases, one AggregateError
dynamic-Function case, nine Array/typed-storage cases and assignment to the
unsupported eval global. Private-element execution, the interpreter bridge,
remaining P1–P7 scope, the 90% audit and complete performance acceptance remain open.

### Cached-write follow-up (map_equal1)

The SameValueZero reuse is pinned in `temp/mvp_test262_90/map_equal1/lambda.exe`,
ordinary optimized release MIR, SHA-256
`9a5c237b7a9e712781c0aeebd58cd7edf16567bf7b0b2bc23e1bbdb5b761598a`.
Fresh focused checks pass **203/203** and forced-GC/poison checks **16/16**,
including fresh-string cached writes and the MIR import. The independent
required-mode audit preserves exactly the `self1` gain and residual ID sets:
**949/1,050**, **+464**, zero lost entry passes and no crashes/timeouts/missing
outcomes. Shared gates pass Lambda **6,823/6,823** and full LJS
**40,261/40,261**, with zero LJS regressions and the exact pinned release SHA
restored. The matching-release 31-round comparison records:

| Workload | Candidate / entry / identical peer ms | Difference vs entry | 95% paired interval vs entry |
|---|---:|---:|---:|
| knucleotide | 3.729 / 3.830 / 3.835 | **−2.64%** | **−4.19% to −0.50%** |
| Map lookup | 6.413 / 7.294 / 7.327 | **−12.08%** | **−13.11% to −10.38%** |
| Richards | 71.338 / 71.368 / 71.300 | −0.04% | −0.42% to +0.41% |
| Sieve | 0.091 / 0.088 / 0.088 | **+3.41%** | **+1.12% to +4.60%** |

The knucleotide and Map lookup improvements are significant against both
identical controls; this resolves the newly identified cached-write overhead.
The seven-round screen passes all **84/84 non-jq output/backend contracts**.
Twelve workloads have an initial 31-round repeat; seven have a subsequent
31-round repeat selected from the screen and initial uncertainty (16 distinct
workloads altogether). Keep both captures, not only the favorable one.

The follow-up records:

| Workload | Candidate / entry / identical peer ms | 95% paired interval vs entry | 95% paired interval vs peer |
|---|---:|---:|---:|
| Sieve | 0.090 / 0.087 / 0.087 | **+2.27% to +3.45%** | **+2.27% to +3.45%** |
| Permute | 0.517 / 0.511 / 0.510 | +0.20% to +1.77% | 0.00% to +1.96% |
| Spectralnorm | 1.170 / 1.173 / 1.173 | −8.36% to +1.82% | −8.36% to +8.74% |
| Parse integers | 51.628 / 51.705 / 51.718 | −4.75% to +4.45% | −4.72% to +0.40% |
| Text search | 5,119.758 / 5,110.537 / 5,110.347 | −0.054% to +0.508% | −0.029% to +0.464% |

Sieve's approximately **3 µs** cold-setup slowdown remains confirmed. Permute is
positive against one control but inconclusive against the other. Spectralnorm
and parse-integers retain wide intervals/control variation; neither is accepted
as a stable parity result. The text-search screen's +0.24% difference becomes
**+0.18%** in the repeat with both intervals crossing parity, so it is not a
confirmed new regression. Strings and Tak also have intervals crossing parity
against both controls. Other initial suspects have no confirmed slowdown in
their repeat. All captures retain exact sources/oracles and script execution
timing; the archived CLI entry-time contracts remain unchanged where applicable.
No jq workload was executed. Performance acceptance and the complete phase
remain open; this does not claim 90% coverage or regression-free completion.

Final increment provenance checks cover all **1,845** entry source hashes
(the **14** changed sources exactly match the candidate manifest), all **18**
frozen module files, the benchmark candidate hashes and the restored ordinary
release executable. `map_equal1/increment-record.json` records these checks and
the explicit incomplete phase/performance status. `git diff --check` passes.

### 2026-10-14: private class elements

Native private fields, methods, accessors and brand checks now use class-owned
private NameRefs, shared shapes and mutable capture cells (**D3.4.3v5**,
**S8.2.2v6**, **D6.2.3v2**). Each class evaluation gets fresh identities; methods
precede field initialization. Private access validates the actual receiver,
including constructor-returned arrays/functions and revoked proxies, without
public prototype traversal or proxy traps. Public reflection excludes the slots.
The initializer and private-element imports retain precise roots and scalar
ownership (**D5.2–D5.3**).

Follow-up fixes cover private-name scope and early errors, enclosing function
observations in computed class keys, anonymous constructor names and embedded-NUL
property names. A generated Unicode class exposed compilation work for metadata
that cannot fit the existing closure layout. Admission now checks that known
capacity before lowering; oversized classes remain unsupported, not passes.

The corrected release pin is `private_exec6/lambda.exe`, SHA-256
`40f4aaa327a8553db5b95881b57bd4763ca80798097c59cebf625f7370b39195`.
Its manifest records source, runner and all 18 frozen module hashes. The matched
entry for this increment is `private_entry/lambda.exe`, ordinary release at
`dfaae60cd9c6c3e9d630e557e354abd4dc0b21a2`, SHA-256
`14e80b9e4cd7836c742eedcdeb0e33d22ef7530b904f2c57cb8dc524d274c4fb`.
The older phase-entry controls and unresolved performance differences remain
separate; this fresh control does not close them.

| Gate on the corrected pin | Result |
|---|---|
| Focused MVP suite | **206/206** |
| Forced GC and freed-memory poisoning | **206/206** |
| Lambda baseline | **6,841/6,841** |
| Full-LJS Test262 baseline | **40,261/40,261**, zero regressions |
| Quiet replay of 24 development timeouts | **10 pass, 8 fail, 6 timeout**; the remaining timeout IDs exactly match phase entry |
| Complete MVP canonical capture, after isolated replay | **24,804 pass, 15,451 fail, 6 timeout**; no crashes or missing outcomes, zero lost entry canonical passes |
| Additional required variants, after isolated replay | **23,028 pass, 1 fail** out of 23,029; no crashes, timeouts or missing outcomes |
| Fully passing distinct IDs | **24,803/40,261 (61.6%)**: **21,708 positive, 3,095 negative** |
| Matched-release benchmark screen | **84/84** output/backend contracts; seven measured rounds and one warmup per binary |
| Timing follow-up | **31 measured rounds** for all eight screen suspects, plus Sieve against the original phase-entry control; performance acceptance remains open |

The complete audit proves **3,056 gains over phase-entry canonical results**.
Its sole additional-mode failure, `language_global_code_S10_1_7_A1_T1_js`
(`delete this` in strict mode), also fails on the unchanged matched control.
It remains unsuccessful in the count; this pre-existing variant gap is distinct
from losing an entry canonical pass. There are **no new crash/timeout IDs**.
All **949** previously audited initial-increment passes are retained, as are all
**5,432** earlier private-class development passes; the latter comparison adds
**169** class-family passes. These are separate regression comparisons, not
inputs to the current pin's coverage count.
The canonical capture retains its original 250 missing outcomes and ten timeouts;
all 260 were replayed individually with the original timeout. The additional-mode
capture similarly retains and replays all 99 interrupted outcomes. No original
capture is rewritten or combined with another binary's passes.

`whole-audit.json`, `audit-review.json` and `residual-inventory.json` under
`private_exec6/` record the result, control confirmation and every unsuccessful
ID with its modes, family and declared features. The largest remaining families
are language statements (**3,396**), expressions (**3,148**), TypedArray (**1,437**),
RegExp (**746**), TypedArrayConstructors (**714**), Promise (**627**), Date (**526**)
and DataView (**504**). Metadata features overlap: **4,438** residual IDs declare
async iteration. These are inventories, not additive gain predictions.

The corrected pin passes **5,601/8,396** class-family IDs in every required mode:
**4,464 positive, 1,137 negative**. Earlier `private_exec3` class evidence and
`private_exec4` development census/timeout captures remain preserved separately.
Evidence lives under `temp/mvp_test262_90/`; no pass counts are combined across pins.

The matching-release repeats use script self-times (or the exact archived CLI
entry-time contract), fresh processes and rotating candidate/control/identical-peer
order. Warmup round zero is preserved but excluded from medians and intervals.
No jq workload was executed. All eight suspect workloads retain correct output:

| Workload | Candidate / increment control / identical peer ms | 95% paired interval vs control | 95% paired interval vs peer |
|---|---:|---:|---:|
| Pidigits | 0.341 / 0.339 / 0.337 | −0.29% to +2.38% | +0.30% to +2.72% |
| Splay | 177.720 / 177.349 / 177.542 | −0.31% to +0.48% | −0.36% to +0.46% |
| Dense array | 57.222 / 57.245 / 57.652 | −0.74% to +0.73% | −1.00% to +0.08% |
| Map lookup | 6.507 / 6.435 / 6.429 | −0.05% to +2.24% | −0.03% to +2.76% |
| Object delete | 0.846 / 0.852 / 0.855 | −3.12% to +1.42% | −3.64% to +0.59% |
| JSON generation | 4.492 / 4.517 / 4.517 | −1.43% to +0.91% | −1.04% to +0.51% |
| Hyphen | 43.482 / 43.391 / 43.335 | −0.35% to +0.96% | −0.15% to +0.83% |
| Three-way Merge | 1,499.723 / 1,489.017 / 1,482.656 | −0.02% to +1.45% | +0.69% to +1.79% |

Pidigits and Three-way Merge remain positive against one control and inconclusive
against the other. Map lookup retains wide intervals crossing parity. Their
control/peer intervals also cross parity; that does not establish stable parity
for the candidate. The other initial suspect signals are not confirmed by these
repeats. Keep both the screen and repeats; uncertainty is not a performance pass.

The separate original-control Sieve repeat is **0.090 / 0.087 / 0.087 ms**, with
**+2.27% to +3.45%** intervals against both identical controls. Its existing
approximately **3 µs** slowdown remains confirmed. The normalized archived MIR
instruction streams match after replacing wide immediates, but that comparison
does not prove equal runtime values or native-helper behavior; the cause remains
unresolved. Other older gaps retain their original controls and status.

`increment-record.json` records the final audit, timing intervals and incomplete
acceptance status. Final provenance checks cover **1,843** entry source hashes,
exactly **10** changed production sources plus the focused-test source, all **18**
frozen module files, the runner and restored ordinary release binary. Documentation
alone changed after capture. `git diff --check` passes.

Dynamic-source bridging, remaining builtin/realm construction, library and typed
storage, async execution and modules remain open. The **36,235/40,261** milestone
and complete performance acceptance have not been reached.
