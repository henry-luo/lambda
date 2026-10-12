# JS MVP: Test262 parity with full LambdaJS

**Date:** 2026-10-12. **Status:** P0 captured; P1 in progress. Full parity remains open.

**Design:** [JS_MVP_Lmd §32](../jube/JS_MVP_Lmd.md#32-test262-parity-with-full-lambdajs).
This plan follows the user's interpreter-only eval/Function decision and
reuse-first requirement. The implemented increment and its measured limits
are recorded in §9; the remaining work packages remain requirements.

## 1. Target and starting evidence

Bring MVP to the exact passing population in
[test262_baseline.txt](../../test/js262/test262_baseline.txt): currently
**40,261 tests**. At P0, freeze its bytes, the Test262 revision, canonical
harness/includes, selected variants, skip policy, runner and engine revisions.
The baseline currently records corpus revision
673e9bacbe28590f501e2dcd817aadcc31899191. A later LJS baseline change requires an
explicit manifest delta; it must not silently change this phase's denominator.

The preceding implementation passes 162 MVP groups normally and under forced
GC/poison, 670 extracted cases on Node and MVP, 6,709 Lambda baseline cases and
40,261 shared LambdaJS Test262 cases. The last figure exercises full LJS,
not MVP. The phase-entry MVP run is now recorded in §9.
Counts here describe the starting evidence, not future fixed suite sizes.

Existing MVP support includes nominal objects, prototype mutation, descriptors,
accessors, proxies, sparse arrays, closures, synchronous generators/iterators,
exceptions, core BigInt arithmetic and Symbols. Complete these implementations
and their interactions; do not treat them as absent subsystems.

The target includes every current passing case, including applicable Annex B,
async, module, buffer/Atomics and weak-reference cases. Current LJS exclusions
remain exclusions. Cross-realm, Intl tests absent from this baseline, host-GC
requirements excluded by the runner, and excluded newer proposals do not enter
the target implicitly. General Node/DOM compatibility is separate work.

## 2. Governing contracts

| Concern | Authority and requirement |
|---|---|
| Guest semantics and reuse | **S1.11**, **D1.3v3**: ECMAScript observations over the existing Lambda/lib substrate; reuse before adding a semantic adapter |
| Dynamic source | **D8.1.3v24**, clauses v20–v21: eval, Function-family constructors and applicable $262.evalScript execution use the retained AST interpreter, regardless of caller tier |
| Representations | **D2.6.4**, **D2.6.9v3**, **D2.7.1**: shared arrays, nominal Maps and destination-owned scalar homes; no numeric GC cells |
| Shape/name identity | **D3.4.3v5–D3.4.8**, **S8.2.2v5**, **D4.6.1v4**: immutable shared shapes and canonical UTF-8/lossless WTF-8 keys |
| Inheritance and captures | **D2.6.10**, **D6.2.2v2–D6.2.3v2**: stable Lambda nominal ancestry and Function storage; JS captures mutable cells while Lambda captures retain their own semantics |
| Ownership and completion | **D1.4v4**, **D5.2–D5.3**, **D5.4.1**: explicit completions, precise roots and one correctly owned evaluation context through reentry |
| Suspension and modules | **D5.1.1v3**, **D7.3.1–D7.3.4**, **D8.1.3v24**: existing activations, event-loop owner and module registry, with JS-specific binding/evaluation semantics |
| Optimization | **D8.2.1–D8.2.6**, **D8.4.1v2**: retained semantic facts guard shared physical operations; no universal is_normal flag or whole-unit replay |

This phase applies the existing dynamic-source ruling to MVP; it does not
change a formal ruling. If implementation needs a different ruling, revise
both formal and working design records before implementing that difference.

## 3. Execution boundary and interpreter integration

### 3.1 Routing

Ordinary admitted source remains MVP MIR. Direct/indirect eval and the Function,
GeneratorFunction, AsyncFunction and AsyncGeneratorFunction constructors reuse
the existing JS AST interpreter. This applies to subsequent calls, nested
functions and resumable bodies created from that dynamic source as well as
the initial creation. Retain the existing dynamic-source interpreter pin;
call-count/loop thresholds must not promote these bodies to MIR. A dynamically
created function may call an existing MVP MIR function through its normal
callable boundary; this does not change either body's execution policy.

This is the specific exception to the earlier no-JS-runtime dependency rule.
It authorizes an audited interpreter entry and its required dependencies, not
full-LJS fallback for an unsupported ordinary script. Admission failures must
remain visible, without replaying effects. Static ES modules are part of the
MVP implementation; dynamic import of a source module follows that module
path rather than treating its entire body as eval source.

Start from js_dynamic_code.cpp, js_interp.hpp/cpp and js_interp_env.h. Reuse
source parsing, early-error rules, retained JsScript ownership, completion
handling and dynamic-source policy. Reuse the shared parser/AST for source
admission; a second parser, interpreter or copied semantic walker is outside
this plan.

### 3.2 Live environments

Direct eval must see and update the caller's current lexical/variable bindings,
TDZ/const state, arguments aliases, this, new.target, home object and available
private names. Preserve strict-eval isolation, sloppy var/function publication,
declaration conflicts and Annex B behavior. Shadowed eval and indirect eval
must follow their actual call semantics. Non-string eval returns its argument.
Function constructors resolve free names in their creation realm's global
environment rather than capturing their caller's locals.

Prefer the existing durable environment and mutable capture-cell storage:
GcEnvironmentStorage and gc_environment_storage_read/store, used by
js_interp_env_slot_read/store. Materialize only bindings that an affected
environment can observe. A MIR entry may adapt to the existing direct-eval
bridge, but eval closures and reentrant callbacks must share the live cells.
A detached snapshot with final copy-back alone cannot satisfy callbacks that
observe or mutate those bindings during eval. Escaped closures retain their
environment and source after the originating MIR frame returns.

Global and separately evaluated script bindings share one execution-owned
environment authority. Keep declarative lexicals distinct from global object
properties. Object environments for with use the normal property kernel,
including Symbol.unscopables, proxies and abrupt reads. Ordinary with bodies
remain an MVP language feature; they are not a blanket interpreter fallback.

### 3.3 Values, callables and ownership

Audit representation compatibility before passing an MVP Item into the shared
interpreter. A common type tag does not prove JsFunction/MvpLmdCallable or
nominal metadata compatibility. Reuse existing operation entries where their
contracts fit; otherwise add an explicit compatible profile/option or extract
the common operation once, preserving the full-LJS default. Keep this dispatch
inside the interpreter/bridge boundary and affected generic operations.

Both sides must observe the same object identity, aliases, prototype links,
descriptors, scalar values and mutable cells. Plain objects retain nominal
Lambda Maps and arrays retain shared array storage. Do not copy the object
graph, create a second realm/global graph, reinterpret incompatible callable
records, or convert ordinary containers to VMap to make the bridge work.
Cross-boundary getters, proxy traps, constructors and callbacks enter the
existing callable/construct contracts exactly once, with the right receiver
and new.target. Preserve thrown-value identity and native error prototypes.

Reuse one EvalContext/heap/module authority and precise RootFrame/Rooted/RootSpan
ownership. Interpreter state, dynamically retained source, closures and parked
activations remain traced through their actual owners. Never invoke the
current fresh-execution-only mvp_lmd_execute recursively as an eval shortcut.
Teardown and abandoned activations must release the same owners once.

## 4. Reuse map

Apply this order in each work package: existing untyped Lambda/lib function;
small explicit option preserving Lambda defaults; shared neutral extraction;
only then a narrow MVP semantic helper. Search for static equivalents and
promote one to its module header rather than copying it. Full-LJS code is a
semantic implementation reference; outside the approved interpreter boundary,
its runtime dependency graph is not automatically an admissible MVP helper.

| Feature | Existing storage/kernels and code to inspect | JS-specific work |
|---|---|---|
| Bindings/arguments | runtime/gc_environment.h, runtime/lambda-root-frame.hpp, runtime/side_stack.h; MVP binding/closure code; js/js_interp_env.h | Mapped aliases, environments, declaration and lookup rules |
| Objects/classes/errors | lambda-data.hpp, type/shape transitions, existing MVP objects/reflect/proxy/runtime_classes files | Correct intrinsic identities, class/private-name semantics and construction |
| Arrays/collections | Array/ArrayNum/SparseArrayMap, runtime/lambda-vector.cpp, existing MVP array and ordered-map operations | Holes, generic receivers, iterators, species and SameValueZero |
| Numbers/strings/JSON/Date | core/lambda-decimal.hpp, lib/utf.h, lib/datetime.h, input/input-json.cpp, JSON formatting/escaping and existing MVP adapters | JS conversions, formatting, callbacks, reviver/replacer and Date rules |
| RegExp | js/js_regexp_compile.h, js/js_bt_regex.h, existing MVP RegExp records | d/v results, observable symbol protocols, lastIndex and species |
| Typed arrays/buffers | Existing ArrayNum allocation/lane operations; js/js_typed_array_carrier.hpp and js/js_typed_array.cpp for shared-layout audit | Views, brands, offsets, detach/resize/transfer and bounds semantics |
| Dynamic source | js/js_dynamic_code.cpp, js/js_interp.hpp/cpp, retained JsScript and shared environments | The audited MVP-compatible entry described in §3 |
| Async/jobs | runtime/activation.h, runtime/suspended_activation.hpp, runtime/async.h, js/js_event_loop.h/cpp | Promise resolution/reactions, async completion and iteration; share the host loop |
| Modules | runtime/module_registry.h/cpp, runtime/module_ast_prebuild.hpp and existing source resolution/cache | JS linking, live imports, namespaces, cycles and async evaluation |
| Weak references | Existing precise collector and full-LJS weak-reference registration/cleanup implementation | Audit/extract neutral weak edges, ephemerons and cleanup jobs; no second collector |

Paths in this table are under lambda/ unless they begin lib/. This is a reuse
inventory, not a claim that every listed full-LJS function can be called
unchanged. JS iterator/proxy/Promise/module semantics must not be replaced by
similarly named Lambda operations with different observable behavior.

## 5. Implementation work packages

| Package | Dependencies | Deliverable | Required gate |
|---|---|---|---|
| P0: target and runner | None | Freeze target; add an explicit MVP backend to the existing Test262 preparation/orchestration path; capture failures by feature and phase | Original harness, variants and negative phases preserved; unsupported/crash/timeout outcomes remain failures |
| P1: core runtime identities | P0 | Native error hierarchy/cause/AggregateError, primitive wrappers, complete core intrinsic constructor/prototype metadata, generic call/apply/bind and reflection | Canonical assertion/property helpers execute; thrown constructor identity and abrupt getters remain observable |
| P2: language environments | P1 | Mapped/unmapped arguments, general array/object patterns/rest/defaults, tagged templates, with/object records, script/global binding ownership | Aliasing, TDZ, parameter scopes, IteratorClose, strict/sloppy and Annex B groups; forced GC |
| P3: interpreter boundary | P1–P2 | Eval/Function-family source routing, live environment/callable compatibility, retained source and interpreter pin | Direct/indirect scope, escaped closures, reentrant mutation, both call directions, dynamic-body MIR audit |
| P4: class completion | P1–P2; P3 for eval/private-name interactions | Class expressions/nesting/computed members, private brands/methods/accessors, static blocks, built-in subclasses and alternate new.target | Initialization order, computed-key effects, return/throw rules, private-name identity and construction across tiers |
| P5: synchronous library | P1–P2; P4 for full species/subclass interactions | Complete Array/String/Number/BigInt/Math/Date/JSON/Map/Set/Symbol/global APIs; RegExp d/v and symbol protocols | Every target family, generic receivers, descriptors, conversion order and mutation/reentry tests |
| P6: buffers and weak storage | P1, P4–P5 as needed | Remaining typed arrays, buffers/DataView, views/detachment/resizing/transfer/shared memory, weak collections and weak-reference records | View aliasing, bounds/brands, iterator/species behavior and precise lifetime tests; cleanup-job integration joins P7 |
| P7: async and shared-memory jobs | P2–P3, P6 for Atomics | Promises/combinators/jobs, async functions/generators/for-await, Atomics wait/notify/waitAsync and finalization jobs | $DONE completion, reaction ordering, thenable reentry, suspended roots, agent/Atomics cases and no lost/double completion |
| P8: ES modules | P2 and shared registry; P7 for async completion | Linking/live bindings/cycles/namespaces, import.meta, dynamic import and top-level await | Resolution/link/runtime negative phases, cyclic initialization, namespace descriptors, async dependency failures |
| P9: aggregate parity | P0–P8 | Final target run, shared gates and performance comparison with frozen provenance | All frozen target tests/variants pass; no masked failures, whole-unit fallback or unreported performance gaps |

P3 first delivers ordinary eval/Function and synchronous generator constructors;
async constructor execution completes with P7. P6 first delivers synchronous
storage and weak-key operations; job-dependent cleanup/Atomics complete with
P7. P8 synchronous linking may proceed before P7, but top-level await and
dynamic-import fulfillment require its job semantics. None of these staged
milestones constitutes final parity.

Primary MVP edits belong in mvp_lmd_mir.cpp and the existing runtime/library,
objects/reflect/proxy, arrays/strings/regexp, functions/generators and class
modules. Extend js_ast_* binding/parameter facts and the shared front end
before introducing parallel walkers. Host/runner integration belongs in
lambda/main.cpp and test/test_js_test262_gtest.cpp. Shared changes belong with
their existing Lambda owner; edit build_lambda_config.json only if a source
addition is necessary. Do not edit generated parsers or vendored dependencies.

## 6. Helper and option disclosure

This plan identifies existing functions and candidate API seams; concrete
helper signatures follow the reuse audit. Before coding each package, disclose
the existing function being reused or extended and any genuinely new adapter needed.
Include internal utilities and newly exported static functions in that report.

Likely API seams to audit are: interpreter entry/profile options; live binding
transport and callable dispatch; Promise/job completion on shared activations;
buffer/view storage operations; weak-edge tracing and module binding access.
Prefer extending the current owner over adding another layer for each feature.
For every proposed seam, record:

- Signature, caller and semantic difference from the existing function.
- Default behavior preserved for untyped Lambda and full LJS.
- Transitive dependencies, including the interpreter exception where relevant.
- Allocation/reentry effects, roots, scalar adoption and retained ownership.
- Guard/proof changes, error completion and planned focused acceptance cases.

Resolve routine implementation choices using this approved direction. If an
audit exposes an incompatible ABI or a required new architecture outside it,
record the concrete incompatibility before seeking a further decision.

## 7. Protecting ordinary performance

Keep the no-is_normal policy: use the existing shape/property facts, element
kinds, callable identities and operation-specific retained proofs. Feature-free
functions should retain their native lanes, direct storage/calls and minimal
roots. Create environment/arguments/interpreter state only where observable;
do not add dormant eval frames or a universal interpreter check to every call.

Dynamic source can mutate globals, prototypes, constructors, methods and live
locals that a former closed-unit proof treated as stable. A shared object may
reach interpreted code through a callee even when its allocating function has
no eval expression. Propagate that exposure through the retained call/escape
facts. End affected borrow/layout proofs at reentrant calls; revalidate after
return or keep that operation generic. Never retain a data pointer or borrowed
scalar home across such reentry. Prefer stable environment cells to repeated
whole-frame synchronization. Test these effects on both normal and throwing
exits, including eval-created closures called after the eval returns.

Freeze a fresh release control before implementation. Preserve §31's recorded
Raytrace3D phase-entry gap (about 1.8%) as preexisting; distinguish incremental
changes from that older comparison. Use unchanged sources/oracles, pinned MVP
MIR, self-reported timing, alternating candidate/control/identical-control-peer
orders, and recorded binary/config/source hashes. Dynamic-source workloads
remain deliberately interpreted internally; record that routing explicitly.

Screen every currently supported non-jq workload (including Hashmap), confirm
suspected regressions with longer paired captures, and inspect control noise
and host load before attribution. Keep the four long jq benchmark workloads
historical-only: no fresh execution, preflight or timing under the user's
existing exclusion. New focused generator/async correctness cases are separate.
Run correctness/build/profile jobs outside accepted timing windows. Publish no
new Result series entry as an incidental effect of this implementation.

## 8. Validation and completion

### 8.1 MVP conformance evidence

Reuse the existing test preparation, canonical harness/includes, skip policy
and negative-phase expectations. Add backend selection and result provenance;
do not alter assertions or mask failures in test_js_test262_gtest. Do not count
MVP capability rejection as the expected SyntaxError or runtime throw. Expose
the applicable $262 hooks, including evalScript, detachArrayBuffer and agents;
async jobs must complete through the real $DONE protocol. Keep source strings
intact for eval, toString, comments and source-sensitive tests.

Report per-test/variant outcomes and feature groups: pass, admission gap,
wrong value/throw/phase, crash, timeout and runner failure. Also record which
tests exercised the approved dynamic interpreter entry. A test whose ordinary
body ran through full LJS fallback is not an MVP pass. Preserve failing cases
and raw results; never reduce the target set to improve the percentage.

The final run must cover every frozen passing-baseline ID and its required
variants. Diagnostic retries remain visible and do not count as a clean gate.
The existing full-LJS baseline remains separately green; neither backend's
success substitutes for the other.

### 8.2 Ownership and shared-engine gates

For each package, cover focused normal execution and forced-GC/poisoned-free
execution. Include cross-tier identity, direct-eval live writes, escaped
closures, boxed/scalar aliasing, getters/proxy traps during conversion, buffer
resize/detach during callbacks, iterator cleanup and suspended activation
teardown. Verify ordinary containers still use the shared Lambda layouts.

After shared runtime/compiler changes, run make test-lambda-baseline and make
test262-baseline. Run the MVP-specific target population separately. Preserve
exact logs and pass/fail totals, and replay environmental failures without
silently replacing an unsuccessful aggregate result. Node's independent
oracle is useful for focused ECMAScript cases; the Node baseline is not added
to this task's default gates.

### 8.3 Final artifacts and status

Keep temporary manifests, binaries, MIR audits, raw outputs and timing samples
under temp/. Record the frozen source/target revisions, dependency/helper audit,
per-package evidence, known failures and final performance intervals here.
The MVP design holds the brief current status and links to this record.

- [x] P0: frozen target and actual MVP failure inventory (includes crashes,
      timeouts and missing results, without counting them as passes).
- [ ] P1–P2: intrinsic identities and complete language environments.
- [ ] P3: audited interpreter-only dynamic source with live binding interop.
- [ ] P4–P6: class/library/buffer/weak-storage completion.
- [ ] P7–P8: async/jobs/Atomics/finalization and ES-module completion.
- [ ] P9: exact-population MVP parity and independent shared-engine gates.
- [ ] Performance review: no newly confirmed regressions; §31's older gap
      and historical-only jq evidence explicitly retained until resolved.

Implementation must settle the exact interpreter ABI/profile and weak-edge
integration from the reuse audits. Those are engineering work items, not
permission to weaken live binding semantics or create a second interpreter,
heap, collector, event loop or module registry. Additional platforms remain
unverified until their own correctness and release evidence is recorded.

## 9. Implementation record — core identity increment

### 9.1 Implemented scope

P0 adds `--mvp-lmd` to the existing Test262 runner and
`js-test-batch --runtime=mvp-lmd` to the host. The runner preserves the original
assertion sources/includes and requires the expected negative phase and type.
Admission failures, crashes and timeouts cannot satisfy negative tests. The
host separately verifies `$DONE`; a source-defined `setTimeout` cannot bypass
completion. Capability/allocation failures are retained on the execution owner,
so a `finally` return cannot turn an engine failure into a passing test; genuine
JS throws retain normal completion precedence (**D1.4v4**).
`LAMBDA_TEST262_EXE` pins the child binary without changing the LJS
default route or rewriting its baseline. An interrupted MVP worker retires
after recording its failing case; unexecuted batch collateral remains visible.

The first P1 increment adds native Error/EvalError/RangeError/ReferenceError/
SyntaxError/TypeError/URIError/AggregateError objects, `cause`, descriptors,
generic `Error.prototype.toString`, native-error materialization at catch and
preserved thrown identity. Construction uses ordinary prototype lookup and
the existing iterator collector for AggregateError. Nominal ancestry stays
separate from live JS prototype links (**D2.6.10**).

String/Number/Boolean wrappers and BigInt/Symbol boxing use nominal Lambda Maps
and private shape fields, with scalar adoption through existing destination
homes (**D1.3v3**, **D2.7.1**). String indices describe UTF-16 code units and
are nonwritable/nonconfigurable; the same property kernel enforces their
attributes. Explicit String/Number conversions retain their JS differences
from implicit conversion. JSON unboxes wrappers after `toJSON`. Generic
`apply` accepts array-like objects; `bind` observes prototype, own length and
name in order and preserves their descriptors and constructor behavior.

This increment also fixes two reproduced defects in the reused Lambda BigInt
conversion: a 64-byte buffer truncated large finite double integers, and
`strtoull` accepted signs/whitespace after a hexadecimal prefix. No second
numeric representation or parser was introduced.

**Still open:** complete intrinsic identity/metadata coverage, canonical
property-helper execution (`Function.prototype`, arguments), the rest of P1,
and P2–P9. Eval/Function-family execution has **not** been implemented in MVP
by this increment. Its required route remains the shared interpreter under
**D8.1.3v24**; ordinary unsupported source still fails admission. No bridge
reinterprets `MvpLmdCallable` as an LJS callable.

Unbound `eval` and `Function` references now explicitly fail admission until
that bridge exists; a missing-global ReferenceError must not accidentally
satisfy a negative test. Shadowed bindings retain ordinary behavior. Extending
the newly exposed native Error/primitive constructors also fails with an
explicit capability error until P4 can initialize both the derived nominal
type and native payload. Direct native construction with an alternate
`new.target` still observes its JS prototype. Ordinary class inheritance is
unchanged (**D2.6.10**).

### 9.2 Reuse and helper audit

All additions/options below were disclosed before coding. Existing Lambda
and full-LJS defaults are retained except the two shared BigInt bug fixes.

| Entry | Reuse / semantic boundary | Effects and ownership |
|---|---|---|
| `error_construct`, `error_to_string` | Existing nominal Maps, descriptor kernel, prototype lookup, string conversion/concat and iterator collector | May allocate/reenter; `RootFrame` owns intermediate object, prototype, message, cause and list |
| `plan_library_class` | Extracted the repeated nominal constructor/prototype shape setup | Compile-owned pool metadata; no managed allocation or JS callback |
| `mvp_lmd_box_primitive`, `primitive_construct`, `primitive_method` | Shared Map fields/scalar homes, UTF-16 iteration, existing numeric/BigInt formatting | Box allocation has no JS callback; construction/method conversion may reenter; scalar payload is adopted before dropping its wrapper owner |
| `primitive_payload` | One private-shape lookup shared by methods, JSON and object tags | No allocation/reentry; borrowed result requires a rooted owner or immediate scalar adoption |
| `mvp_lmd_numeric_string_span` | Extracted the existing JS numeric whitespace scan for Number and BigInt | No allocation/reentry; borrowed byte span used synchronously |
| `mvp_lmd_named_set(..., descriptor_flags=-1)` | Existing DefineOwnProperty for explicitly requested intrinsic attributes | Old Set default preserved; arguments rooted across definition |
| `mvp_lmd_native_function(..., name=NULL)` | Existing callable preparation and descriptor storage for reflected native names | Old lazy default preserved; named function stays rooted while metadata is allocated |
| `mvp_lmd_library_class_initialize(..., immutable_metadata=false)` | Existing class initializer for builtin prototype/name/length attributes | Optional stricter attributes, existing default unchanged |
| `mvp_lmd_primitive_to_number(..., explicit_number=0)`; ToString flag bit 2 | Explicit Number permits BigInt after ToPrimitive; explicit String permits a directly supplied Symbol | Defaults retain implicit conversion; MIR import audits include the option; numeric extraction remains guarded before a generic call |
| Existing `mvp_lmd_function_forward` / `function_method` | Shared sequence length/index operations and existing closure-environment storage | Generic apply snapshots values into precise root/number slots; bind retains captured arguments and observes target metadata through the property kernel |
| `mvp_lmd_diagnostic` optional phase/type outputs; `MvpLmdHost::test262` | Existing execution owner and result publication | Host-only completion checks root the result; minimal scripts reuse program-owned canonical names |
| Existing `mvp_lmd_fail` and result publication | Retain the first capability/allocation failure as kind/site on the existing library execution state | No managed pointer or new root; reconstruct the engine diagnostic after the script returns, outside its self-timed interval; ordinary JS errors remain overridable |

Errors/wrapper families are allocated only for source that admits/observes
them. Ordinary functions retain existing scalar/call/shape proofs; this does
not add an `is_normal` flag or an interpreter branch to every call
(**D8.4.1v2**). Rooting uses the shared precise owner model (**D5.2–D5.3**).

### 9.3 Evidence and remaining gates

Evidence root: `temp/mvp_test262_parity/`. The phase-entry head is
`f8825bd1463fe0ae4b249c5eaa8ddd5cb8cc5db4`; `control/manifest.json` freezes the
40,261-case target, Test262 revision, harness/configuration hashes and release
control. Control SHA-256:
`80c14d22effc27bb6b1edee21a5409ccf4320a61d195876f2adfb4ec8b276750`.

| Population | Evidence at this checkpoint |
|---|---|
| Full phase-entry MVP target | **8,888 passed / 40,261**; 20,588 admission failures, 8,783 runtime failures, 4 parse failures, 27 crashes, 11 timeouts and 1,960 missing batch results |
| Error/primitive baseline subset | Final `core10`: **669 / 1,750**, versus **324 / 1,750** initially; 345 gains, zero lost passes, 507 admission and 574 runtime failures, no crashes/timeouts/missing results |
| Additional NativeErrors/apply/bind subset | Final `core10`: **78 / 231**, versus **21 / 231** initially; 57 gains, zero lost passes; 145 admission and eight runtime failures, no crash/timeout/missing result |
| Focused MVP groups | Final `core10`: **172/172** normally and with `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`, including mixed primitive/object arithmetic, the engine-failure/finally reproducer and its normal-TypeError control |
| Supported non-jq workloads | **84/84** output/backend preflight on the intermediate candidate, including Hashmap; staged paired captures and their limitations are recorded in §9.5 |
| Shared full LJS gate | **40,261/40,261** on pinned `core5`, zero regressions; subsequent runtime changes are MVP-only, and the shared BigInt code is unchanged since that gate |
| Shared Lambda gate | **6,737/6,738**; the debug LaTeX phase-3 corpus case times out at 60 seconds, including isolated replay. The same case passes on both frozen release binaries with matching output (control 30.840 s, candidate 30.312 s) |

The full MVP count is the **phase-entry** inventory, not a post-change parity
claim. It also predates the explicit eval/Function admission and host completion
checks, so its passing outcomes are diagnostic, not audited parity. Its raw
run and manifests remain in `initial/`; the subset selection and intermediate
artifacts are in `candidate/`. The retained increment is frozen under `core10/`
with source patch, binary and source hashes. Candidate SHA-256:
`f145cfbc4d0e2ee3b6a9ea3ca84d9a882f9daa2f45c6538d8f2c815d45d088ce`.
`core6` is the preceding performance-screen candidate. `core7` retains engine
failures through finalization; `core10` also restores JSON's early return when
neither wrappers nor `toJSON` hooks are possible. The intervening `core8` and
`core9` lowering experiments were rejected; their performance evidence is
retained separately.
Do not combine separate population counts into an invented aggregate pass total.

A concurrent-build diagnostic run of the core subset on `core4` recorded four
10-second timeouts and 217 unexecuted batch results. It introduced no new
value/throw failures; it is not accepted as a clean conformance run. The
three String cases pass on both pinned binaries in individual replay; the
complete subset was subsequently repeated cleanly on `core10`. The intervening
`core6` run also overlapped unrelated host tests (649 passes, two timeouts,
96 missing results) and is retained separately. Neither contended run is
substituted for the clean final result. Exact
BigInt output for `1e100`, the largest finite binary64 integer, Unicode
whitespace and invalid radix syntax matches Node (`core4/bigint-oracle.json`).

The four jq workloads remain historical-only. §31's older Raytrace3D gap is
still open. No Result-series publication or baseline weakening accompanies
this increment.

### 9.4 Interpreter integration audit

The existing `js_interp_execute_direct_eval_source` and indirect entry in
`js_interp.hpp` already implement the dynamic-source policy. Their retained
interpreter calls LJS property, conversion and call operations in
`js_interp.cpp`; these operations cannot consume MVP records merely because
both sides carry an `Item` (**D1.3v3**, **D8.1.3v24**).

`JsFunction` owns `invoke`, `construct`, `JsCallableCode`, an optional
`JsAstBody` and `JsInterpEnv`. `MvpLmdCallable` instead retains its MVP program,
class recipe and shared `Function::closure_env`. Its compiler's
`binding_cell`/`binding_item` use mutable one-element arrays for captures;
uncaptured locals may remain MIR registers. `JsInterpEnv` describes contiguous
durable slots through `GcEnvironmentStorage`. These are compatible lifetime
building blocks, but not interchangeable record layouts (**D6.2.2v2**,
**D6.2.3v2**, **D5.2–D5.3**).

P2/P3 must therefore materialize observable MIR bindings into the existing
durable storage, link interpreter reads/writes to those live cells, and select
the correct property/call/construct operations at the approved interpreter
boundary. Escaping eval closures and callbacks must retain that shared owner.
No source execution bridge or new interpreter helper was added in this
increment; copying an object graph, substituting a snapshot/copy-back scheme,
or casting an MVP callable to `JsFunction` remains outside the design.

### 9.5 Performance evidence and open regressions

All measurements use frozen release binaries, pinned MVP MIR, unchanged
workloads/oracles and script self-reported execution time. The runner rotates
candidate/control/identical-control-peer order and retains raw output, source
hashes, MIR and bootstrap intervals. This is a staged capture across candidates,
**not** one final-candidate run over all 84 workloads:

- `paired/`: `core6`, 78 workloads with five measured rounds each. A brief
  unrelated compiler process appears in the host monitor; affected screen
  samples remain diagnostic. Hyphen has one measured round, after which the
  driver was explicitly stopped (`screen-stop.json`); its repeated compilation
  takes about 50 seconds per process. That one round is insufficient for a
  performance conclusion.
- `confirm/`: `core7`, 15 rounds each for Havlak, JSON, Navier–Stokes,
  object_fields, brainfuck and nqueens. For Havlak/JSON/brainfuck/nqueens, both
  candidate/control intervals include parity; object_fields
  remained within about 0.3% in median. Navier–Stokes remained slower.
- `remaining/`: `core10`, five rounds each for log_pipeline, microdiff,
  prettier_ast, text_search and three_way_merge, with matching output.
- `text-confirm/`: final `core10`, 15 rounds each for prettier_ast and
  three_way_merge. Prettier AST's suspected regression did not reproduce:
  **517.792 / 519.152 / 518.945 ms** (candidate/control/peer), with both comparison intervals
  including parity. Three-way merge remains slower: **1,497.601 / 1,480.233 /
  1,486.249 ms**. Candidate/control is **1.0117×**, interval **[1.0045, 1.0159]**;
  candidate/peer is **1.0076×**, interval **[1.0014, 1.0229]**. No concurrent
  build/test job appears in the host monitor.
- `final-navier/`: final `core10`, 15 rounds, **32.354 ms** candidate versus
  **31.692 ms** control and **31.807 ms** identical-control-peer. Candidate/control
  is **1.0209×**, with two-sided 95% paired-bootstrap interval **[1.0142, 1.0296]**;
  candidate/peer is **1.0172×**, interval **[1.0099, 1.0232]**. The host monitor
  recorded no concurrent build/test job in this capture.

**The no-new-regression gate remains open.** The new Error constructor enables
library-aware lowering even for a cold checksum failure. MIR inspection shows
additional guarded ToPrimitive/ToNumber paths in both slower workloads;
this identifies a compiler specialization boundary to investigate, not a proven attribution
to one helper. Expanding primitive case dispatch (`core8`) and adding local
number-pair guards (`core9`) both made Navier–Stokes slower and were reverted.
Their raw measurements remain in `navier-fix/` and `numeric-guard/`.

The next repair should retain numeric value/region facts independently of
intrinsic registration and place guards at a justified region boundary
(**D8.2.1–D8.2.6**, **D8.4.1v2**). Keep observable object coercion and exceptional
paths intact. Ordinary JSON's early return is restored when neither wrappers
nor observable `toJSON` hooks are possible. The four jq workloads were not
executed, and §31's earlier Raytrace3D gap remains a separate open item.
