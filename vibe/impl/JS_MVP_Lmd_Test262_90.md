# JS MVP: 90% of LJS Test262 coverage

**Date:** 2026-10-15. **Status:** coverage milestone verified on archived `parallel38`: **37,445/40,261 IDs (93.0056%)** pass every required variant, **1,210** above the 90% target. No original whole-entry passes are lost; four later selected controls remain archived timeout losses. Pin45's eval/replacement followup passes **647/647** focused tests normally and under forced GC and **616/616** selected required-mode cases, regaining those controls. Watchdog recovery, final integration and a fresh whole audit remain open. The latest post-repair release replay attempts **88 benchmarks**: **84** pass, four jq workloads terminate with **SIGBUS**, and **12 performance regressions** block acceptance. All captures stay build-specific.
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
| Archived entry benchmark population | 84 non-jq workloads with output/backend contracts; four jq workloads then retained historical evidence only |

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

P0–P5 are **in progress**; P6–P7 are **pending**. P1–P6 are planned feature work; P7 chooses
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

P3 Date follow-up (implementation in progress): promote the existing
`js_date_time_clip`, extract `js_date_components_time` and `js_date_parse_text`,
and parameterize existing getter/setter dispatch with `JsDateOps` through
`js_date_method_with_ops` / `js_date_setter_with_ops`. The policy owns conversion,
internal-slot access and errors; LJS retains its defaults. MVP reuses its existing
constructor/value adapters and adds `date_method` / `mvp_lmd_date_initialize`.
The existing `mvp_lmd_to_primitive` hint gains an ordinary-conversion option for
Date's symbol protocol. Private Lambda shape fields hold instance DateValue;
the Date prototype has no DateValue slot. Preserve declared subclass ancestry
and exact rooting
(**D1.3v3**, **D2.6.9v3**, **D5.2–D5.3**, **D6.2.3v2**). Install extended methods
only in units where Date is observable. The buffer7 audit below predates this
follow-up and does not validate the changed sources. Shared setter components
now remain doubles through normalization before TimeClip; this avoids premature
32-bit narrowing for large or infinite inputs.

### P4. Typed arrays, buffers and views

The first implementation increment shares ArrayBuffer/DataView operations via
`JsBufferViewOps`, `js_arraybuffer_{construct,resize,transfer,slice}_with_ops` and
`js_dataview_{construct,operation}_with_ops`. LJS retains its default operations;
MVP supplies existing coercion, nominal shapes, prototype lookup and completion
operations. The existing carrier accessors `js_get_arraybuffer_ptr` and
`js_get_dataview_ptr_from_map` are promoted for physical GC ownership. Existing
`js_to_index_i64` / `js_to_index_int` conversions accept the selected Number operation before
index validation. MVP adds `buffer_construct`, `buffer_method` and
`mvp_lmd_buffer_initialize`; the immutable policy uses small callback adapters.
All callback boundaries require rooted owners/arguments and fresh backing-store
bounds checks. This follows **D1.3v3**, **D2.6.9v3**, **D5.2–D5.3**; it introduces
no new ordinary-object flag or change to numeric-array MIR specialization.
The slice policy also provides species lookup/construction; physical bounds are
revalidated after callbacks. Buffer/view subclasses retain their declared Lambda
nominal ancestry separately from the physical JS brand (**S2.1.3v2**). Test262
host mode exposes real `$262.detachArrayBuffer` through `byte_buffer_detach`;
it does not synthesize passing outcomes or alter the harness. Pending host
`evalScript`, `createRealm` and agent operations fail with an MVP capability
completion, including access through aliases/computed keys. They cannot produce
spurious negative-test passes by calling an undefined method.

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
numeric-array specialization where its compile-time proofs hold. The follow-up
below implements that adapter; general method/species algorithms remain open.

### P4 typed-view follow-up (implementation in progress)

Benchmark timing is deferred at the user's request while other sessions build
and test. The Date pin and its interrupted strict-mode captures remain separate
from this new increment; no new aggregate or performance claim is made.

Under **D1.3v3**, **D2.6.9v3**, **D5.2–D5.3**, **S2.1.3v2** and
**D8.4.1v2**, this increment reuses physical typed carriers, ByteBufferHandle,
ArrayNum views, canonical numeric-key parsing and existing iteration/property
operations. Existing numeric-only constructor proofs retain their ArrayNum path;
observable typed storage uses nominal packed Maps, with no ordinary-object flag.

| Helper or extension | Reuse, semantic boundary and lifetime |
|---|---|
| `JsTypedArrayOps`; `js_typed_array_construct_with_ops` | Shared typed allocation/copy/view algorithms select frontend shape/prototype, property and iterable operations. Constructor inputs, newTarget/prototype, source/result and coercion values have precise roots; no stale byte pointer spans callbacks. LJS keeps its existing constructor entry and defaults. |
| `js_typed_array_new_with_ops`, `js_typed_array_from_buffer_with_ops`, `js_typed_array_from_array_with_ops` | Parameterize the existing static allocation/copy bodies. Existing exported LJS functions remain wrappers. Validate multiplication before allocating and share buffer ownership rather than copying view state. |
| `js_typed_array_set_numeric_with_ops` | Existing Number/BigInt storage algorithm with frontend conversion/error operations; primitive numeric writes retain the leaf path. Coercive writes reacquire bounds and storage after callbacks. |
| `mvp_lmd_iterator_get_with_method`, `mvp_lmd_iterator_collect_with_method` | Extend existing iterator operations to consume the already-read iterator method exactly once; retain the current MIR import ABIs through wrappers. |
| `mvp_lmd_iterator_method`; collector array-like/property options | Extract the existing GetMethod lookup for constructors and Array.from. Extend the collector for indexed input, CreateDataProperty and final length assignment; Array.from supplies its selected constructor and preserves mapper/iterator/length effect order. |
| `mvp_lmd_sequence_iterator_create` | Extract the existing sequence/ordered iterator allocation. An explicit sequence projection reuses its carrier for typed values, keys and entries; ordered cursor leases keep their existing lifecycle. |
| `js_typed_array_species_create_with_ops`; `js_typed_array_subarray` | Consolidate the existing length/buffer species construction paths, sharing branded-result, bounds, content-type and minimum-length validation. MVP supplies property/construct callbacks; LJS retains its resolver. Subarray reuses buffer index conversion, captures length before coercion, and lets the selected constructor validate source bounds. |
| `mvp_lmd_typed_array_initialize`, `typed_array_construct`, `typed_array_method` | MVP native adapters for shared storage, constructor/prototype metadata, builtin subclasses, getters and supported generic methods. No full-LJS callable or prototype metadata is passed to MVP. |
| `mvp_lmd_typed_array_index`, `mvp_lmd_typed_array_property`, `mvp_lmd_typed_array_store` | Reuse `js_ta_key_canonical_numeric`, shared typed loads/stores and existing descriptor/receiver operations. Canonical invalid numeric keys terminate indexed lookup; ordinary strings and symbols retain normal shape/prototype behavior. |
| `js_ta_key_canonical_numeric`, `js_ta_numeric_index_valid`; `typed_intrinsic` | Promote the existing shared numeric-key helpers to the runtime header; check bounds before narrowing a numeric index. The compiler-only intrinsic lookup selects the common element descriptor. |

`mvp_lmd_receiver_identity` promotes the existing iterator identity callback for
`Symbol.species`; unlike Object.valueOf, this getter preserves primitive receivers.
Native assignment returns use the existing number-stack allocator for wide
scalar homes that must outlive the C frame (**D5.2–D5.3**). Merely publishing unused
constructors on the global object does not invalidate ordinary-array fast-path
proofs. Primitive typed-array inputs perform ToIndex before reading
newTarget.prototype; object inputs observe that prototype before their input
conversion. The unchanged Test262 constructor harness also exercises array-like
Array.from, so its missing indexed-input path is part of this increment.

Acceptance covers element widths and BigInt conversion, constructor order and
iteration, buffer aliases, detach/resize during conversion, canonical indices,
descriptors/own keys/integrity, inheritance, and forced collection. General
TypedArray method/species coverage remains explicit until its own checks pass.
Subarray includes subclasses, default/custom/proxy species, two-argument
length-tracking construction, and detached/shrunken source callbacks. Named
view properties and iterator/subarray methods require observable typed storage
when typed constructors are used; numeric-only programs retain the existing
ArrayNum proof. No ordinary object representation or runtime flag changes.

P4 callback follow-up: promote `js_typed_array_callback_algorithm` and its kind
enum and expose the existing default buffer policy; extend `JsTypedArrayOps`
with callable/invocation operations. Merge the
existing typed map/filter loops into that visitor, reusing species construction,
typed stores and `JsRootedItems`. MVP publishes the eleven callback methods only
with observable typed storage. Snapshot internal length, retain callback values
and filter selections across reentry, and preserve borrowed Array hole behavior
(**D1.3v3**, **D5.2–D5.3**, **D8.4.1v2**). Ordinary Array visitors and numeric-only
loops retain their current implementation. The selected frozen cohort contains
550 callback-method IDs; entry MVP lacks these methods. Timing remains deferred.

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

### Buffer/view increment (2026-10-14; conformance validated, performance acceptance open)

Implements ArrayBuffer and DataView construction/reflection, aliased numeric and
BigInt loads/stores, endian handling, fixed/tracking views, resize, transfer,
species-aware slice and the Test262 detachment host operation. The shared
`ByteBufferHandle`, carrier layouts, external-memory accounting and DataView
kernels remain the storage authority. Operation policies retain each front end's
coercion, prototype and error authority (**D1.3v3**, **D2.6.9v3**). Carrier tracing
and finalization use the physical kind; callbacks retain exact roots and refresh
payload pointers (**D5.2–D5.3**). Buffer/view subclasses keep the compatible
declared Lambda nominal shape independently of mutable JS prototype links
(**S2.1.3v2**, **D6.2.3v2**).

The final candidate is `buffer7/lambda.exe`, SHA-256
`1f503b64a5389f7cc369ac2c2a76068b76d7501123e8c2afe9ca339d51cd0feb`.
Its matched ordinary-release control is `buffer_entry/lambda.exe`, SHA-256
`ea38cde8d81efc8328a6e6755d761f7c22b18a21974cf5a1700275b19e34983a`,
built from turn-entry HEAD `a7ecfa079809c1309f32be8046784d2f1c7d5d8f`.
Both paths are under `temp/mvp_test262_90/`; the previous executable and its
evidence remain intact. The final same-pin audit records:

| Gate | Result |
|---|---|
| Focused / forced and poisoned GC | **211/211** each |
| Shared Lambda/Input baseline | **6,850/6,850** |
| Full-LJS Test262 baseline | **40,261/40,261**, zero regressions |
| Canonical MVP outcomes after isolated replay | **25,407 pass, 14,848 fail, 6 timeout**; no crashes or missing outcomes |
| Additional required strict modes after isolated replay | **23,631 pass, 1 fail** out of 23,632 |
| Fully passing distinct IDs | **25,406/40,261 (63.1%)**: **22,311 positive, 3,095 negative** |
| Gain over the preceding complete audit | **603**, with **zero lost passes** |
| Remaining to the 90% target | **10,829** |

All **593/695** buffer/view-family passes now pass every required mode on the
final pin. The other ten gains cover Object.seal and builtin subclassing.
The sole extra-mode failure is the existing strict `delete this` case; the six
timeout IDs are unchanged from the preceding audit. No new crash/timeout IDs
were introduced. The original canonical capture retains **300 interrupted
outcomes and 12 timeouts**, all replayed individually. The additional-mode
capture preserves **72 interrupted outcomes and 3 timeouts**; all 75 pass when
replayed individually. These replays use the same executable and timeout;
original captures remain intact.

The matched ordinary-release screen passes **84/84** output/backend contracts,
with seven measured rounds per lane. Two independent **31-round** repeats cover
the same eleven suspects, with rotating candidate/control/identical-peer order.
The first repeat is noisy, including substantial identical-control variation;
both captures remain evidence. No jq workload was executed.

| Follow-up | Candidate / control / peer, ms | Paired 95% difference intervals |
|---|---|---|
| `object_retype_escaped`, second 31-round repeat | 5.221 / 5.141 / 5.171 | +0.54–2.49% / +0.02–2.03% |
| Same workload, 99-round follow-up | 5.199 / 5.172 / 5.166 | +0.10–0.97% / −0.06–1.10% |
| Sieve, second original-control 31-round repeat | 0.091 / 0.088 / 0.088 | +2.25–3.45% / +1.12–3.41% |

The retype follow-up is about **0.5–0.6%** slower by medians, positive against
one control and inconclusive against the other. Its MIR instruction forms match
after four address relocations; the loop still calls the unchanged
`map_shape_set_text` and has no buffer operation. That does not establish equal
native execution or a cause, so no speculative runtime patch was made. The
other ten second-repeat intervals do not establish a slowdown against both
controls. The older Sieve **3 µs** difference remains confirmed; older Pidigits,
Three-way Merge, Map lookup and §32 gaps retain their original controls/status.

`buffer7/increment-record.json` records all six timing captures and incomplete
acceptance. Provenance checks cover **1,843** entry source hashes, exactly
**eight** changed production sources plus the focused-test source, the runner,
**18** frozen modules and the restored release executable. Only documentation
changed after capture. Coverage and performance completion remain separate;
neither the 90% milestone nor complete performance acceptance is reached.

At this buffer7 audit, general typed-array storage/reflection and SharedArrayBuffer/Atomics
were still unimplemented in MVP. Existing numeric ArrayNum MIR specialization is retained;
its successful numeric-length programs do not acquire view/storage indirection.
The dynamic-source bridge, async/jobs and module packages remain open.

### Date increment (2026-10-14; required-mode audit incomplete, timing deferred)

The `date3/lambda.exe` ordinary-release MVP MIR pin has SHA-256
`0e1a1b26dddb3becacaf36ddb0b16da6a8daad483198f9aa2404cd6ab1803133`.
The shared policy/kernels described in P3 provide calendar construction, string
parsing, getters, setters, formatting and TimeClip. MVP supplies branded private
shape storage, declared subclass construction, reflection, generic toJSON and
Symbol.toPrimitive. The Date prototype has no DateValue slot; forged objects and
proxies fail branded methods. Internal-slot updates still work on frozen Dates.

Shared setters now normalize double components before narrowing, fixing the
reproduced `setUTCMilliseconds(Infinity)` / `setUTCMilliseconds(2147483648)`
conversion to `2147483647`. Invalid-date setters retain callback writes when the
operation must return NaN without storing. Calendar conversion preserves the
existing minute-offset handling of historical local dates. Exact receiver and
argument roots remain live across reentrant coercion (**D5.2–D5.3**).

The frozen candidate passes **215/215** focused checks and **215/215** with forced
GC and poisoning. All **609/609** selected Date/Annex B/subclass cases pass their
canonical mode, without crashes or timeouts. Their **551** candidate gains over
buffer7 still require the complete additional-mode audit. The shared gates pass
**6,854/6,854 Lambda/Input** and **40,261/40,261 LJS Test262**, with zero LJS
regressions. The interrupted whole-population audit remains incomplete; the user
deferred timing and requested continued feature work. Buffer7 remains the latest
complete audit and performance record. Earlier date1 captures
remain diagnostic evidence, including the corrected prototype, length and
historical-timezone failures. The corpus and Test262 harness are unchanged.


### Typed-view increment (2026-10-14; whole-population audit and timing deferred)

Under **D1.3v3**, **D2.6.9v3**, **D5.2–D5.3** and **D6.2.3v2**, the
P4 adapter now uses shared typed carriers and buffer storage for all numeric and
BigInt constructors, aliases, indexed reflection, getters, iteration and subclass
construction. Array.from also reuses the existing collector for array-like input,
custom constructors, mapper effects and CreateDataProperty. General typed-array
methods, the dynamic-source interpreter bridge, realms and SharedArrayBuffer/
Atomics remain open.

The preceding `typed7/mvp-typed7.exe` pin has SHA-256
`07ed741dfabc0c96e98466e2ced6c4a1e90ffa73b2311ebfed3c543ee7cc896b`.
It passes **222/222** focused and forced/poisoned-GC checks. Of **248** selected
Test262 IDs, **220** pass every required mode, including all **46** frozen
Array.from IDs. The original additional-mode capture had **88** unresolved
outcomes (82 missing batch results and six timeouts); all **88** pass individually
on that same pin with unchanged limits. Its full-LJS baseline passes
**40,261/40,261**, zero regressions. Earlier Lambda/Input validation is
**6,859/6,860**, with `latex_test_latex_phase3_corpus` still timing out in an
isolated replay; that replay also reports a worker segmentation fault after the
timeout. This is unresolved shared-gate evidence, not a passing gate.

The follow-up `typed9/mvp-typed9.exe` pin has SHA-256
`8ec4cdd80287bd01a62b053b16af720b842bbae43addb965c0d3a34f4135ae62`.
It adds subarray and the shared species kernel, retains argument order and
length-tracking aliases, and validates custom species results. A direct numeric
constructor followed by a view method now selects observable storage before
MIR emission. Its **223/223** focused and forced/poisoned-GC checks pass, and
full-LJS Test262 passes **40,261/40,261**, zero regressions. The selected cohort
now has **292/315 IDs passing every required mode**, including **64/67** subarray
IDs and all 220 preceding selected passes: **72 additional confirmed passes**.
All **280** additional required strict modes pass after replay. The original
canonical capture has one timeout and three missing results; the three missing
results pass individually. The original strict capture has one timeout and nine
missing results; all ten pass individually with unchanged limits. Original
captures remain intact.

One canonical case, `TypedArray/prototype/subarray/resizable-buffer.js`, still
hits the 10-second limit in isolated replay. The preceding typed8 pin rejected
that same case at admission. A faithful standalone source assembly also exceeds
a separate diagnostic limit; that diagnostic is not counted as conformance
evidence. The 22 other unsuccessful IDs report admission failures. The same-source shared Lambda/Input baseline now passes **6,862/6,862**,
including the previously timed-out LaTeX case. The earlier failures remain
archived; the new pass alone does not establish their cause.

Resizable-buffer corpus cases that include `resizableArrayBufferUtils.js`
remain blocked by its `new Function` subclass factory. The harness is unchanged;
the retained-interpreter bridge (**D8.1.3v24**) must resolve this capability gap. Focused tests
exercise resize/detach semantics directly, without counting that as corpus
coverage. Buffer7 remains the last complete whole-population audit. No benchmark
or jq execution was performed, and no new aggregate percentage or absence of
performance regression is claimed.


#### Shared MIR compilation follow-up

The resizable-subarray diagnostic repeatedly stalls before execution. A release
sample, symbolized with a linker map whose text bytes exactly match typed9,
locates the dominant scan inside `em_finalize_semantic_root_write_back`:
every liveness change searches every basic block for predecessors. The adjacent
publication analysis already builds an indexed predecessor list.

Reuse that builder as `em_root_build_predecessors`, with
`MirRootPredecessors` and `em_root_free_predecessors`. Both liveness and
publication consume the same ascending edges and unchanged fixed-point rules
(**D5.3.1**, **D8.6.1**). Reuse the existing successor matrix for the later dirty
state pass instead of rebuilding it. This changes compiler analysis work and
allocation only; acceptance must retain exact roots, GC stress, MIR guards and
both shared baseline gates. A native test compares the sparse traversal with
its dense edge oracle across empty graphs, cycles, word boundaries and an
indirect-jump-shaped row. No Test262 source, harness or timeout is changed.


The compiler follow-up is pinned as `typed10/mvp-typed10.exe`, SHA-256
`9c22ba21ca75209453d4b7f1a1a36ce46141ca223f7ccc681339e3a8e58b6ed3`.
It passes **223/223** focused MVP checks, **223/223** forced/poisoned-GC
checks and the new native predecessor-order check. The large resizable-subarray
case initially still times out under its unchanged 10-second corpus limit.
After the Lambda gate finishes, isolated canonical replays resolve all **28**
interrupted/missing outcomes: **22 pass and six report admission failures**,
including that resizable-subarray case. The original capture (**270 pass,
41 fail, four timeouts**, including 24 missing batch tails) remains intact.
The resulting canonical tally is **292 pass, 23 admission failures**. This
removes one sampled compiler bottleneck without attributing all timeout
variability or treating an unsupported case as passing. Full-LJS Test262 passes
**40,261/40,261**, zero regressions; the same-source Lambda/Input baseline passes
**6,863/6,863** (4,751 runtime and 2,112 input checks). All **280** additional
required strict variants pass on their first isolated run. Thus **292/315**
selected IDs pass every required mode, including **64/67** subarray IDs, with
zero lost selected passes from typed7 or typed9. The **72** gains over typed7
are retained on this compiler pin. The remaining **23** IDs still require the
dynamic-Function harness factory or realm support; none is counted as passing.

Evidence is under `temp/mvp_test262_90/typed10/`: the frozen manifest and patch,
original and isolated selected outcomes, required-mode outcomes, focused/GC
logs, shared-gate logs and their archived suite artifacts. `increment-record.json`
checks source/binary/corpus identity and required-mode completeness. Buffer7
remains the last complete whole-population audit. Benchmark timing remains
deferred; this increment does not close the 90% or performance acceptance gates.

### Typed callback increment (2026-10-14; validation in progress)

The eleven typed callback methods now share `js_typed_array_callback_algorithm`
across LJS and MVP, including the existing map/filter walks. `JsTypedArrayOps`
supplies callable/invocation operations; the existing default buffer policy is
visible to the shared algorithm. Species creation and typed conversion/stores
reuse the preceding kernels (**D1.3v3**, **D2.6.9v3**). Precise roots retain the
receiver, callback, thisArg, accumulator, current value, callback result and
output; selected filter scalars and returned scalars receive durable number
homes (**D5.2–D5.3**). The walk captures internal length, preserves callback/
species order and propagates conversion exceptions. Borrowed Array methods keep
their existing hole policy. Ordinary Array visitors are unchanged.

Focused testing found that callback-name planning queried the Map method table,
leaving numeric constructors such as `new Uint8Array(0).reduce(...)` on raw
storage. Querying the Array method table fixes this capability failure and
preserves the required TypeError. The failed `typed_methods1` pin and reproducer
remain archived. The corrected `typed_methods2/mvp-typed-methods2.exe` pin has
SHA-256 `d0b6c5c6f57a58c6b4ad2f71c8d5e84acd361d24f3829ba424a197ffc8480044`
at HEAD `a8d254c0e48001904e449c4ddae5c34470132365`. It passes **227/227** focused
and **227/227** forced/poisoned-GC checks. The selected cohort contains **550**
callback IDs plus **292** preceding selected passes. After unchanged-limit
isolated replay, **807/842 IDs pass every required mode**, including **515/550**
callback IDs and all **292** preceding passes. All **779** additional required
strict variants pass on their first run. The initial canonical capture
(**785 pass, 53 fail, four timeouts**) and isolated replay (**22 pass, eight
fail**) remain archived; the initial failures include 26 missing batch tails.
All 35 remaining IDs fail admission in cases using the unchanged resizable
buffer harness's dynamic `Function` subclass factory. They remain unsuccessful.
Full-LJS Test262 passes **40,261/40,261**, zero regressions. The initial
Lambda/Input gate passes **6,880/6,881**: `latex_test_latex_phase3_corpus`
reaches its 60-second limit. Three isolated runs on unchanged binaries pass
under that same limit. The complete rerun with the existing
`AST_TUNE_BATCH_WORKERS=2` control also passes **6,880/6,881**, with the same
timeout. This does not establish a cause or close the shared aggregate gate.
Original artifacts remain under `gate-artifacts/`; isolated replays and the
complete rerun are separate. No timeout or test source was changed.

A final planning review reproduces a capability failure for
`new Uint8Array(2)` followed by `a[key](...)`, where `key` selects `map`.
An array-initialized typed view passes the same call, identifying the raw-storage
selection as the cause. Computed method calls now select observable typed storage
even without a known spelling; numeric element reads retain the preceding rule
(**D8.4.1v2**). The follow-up `typed_methods3/mvp-typed-methods3.exe`, SHA-256
`f490df0026636d2bf2e75b1ab85207101e29bfabeee402155fa59fb386cfb466`, passes
**227/227** focused and **227/227** forced/poisoned-GC checks, including computed
callback dispatch and numeric non-callable TypeError. Its selected audit was
interrupted explicitly for the following alias fix; the partial capture is
retained and is not counted as a completed audit.

The raw adapter's pre-existing computed named-key read limit also affected
saving `a[key]` into an alias before invoking it. Both forms and their results
are retained under `typed_methods2/`. The inferred-key follow-up extracts
`plan_typed_storage` and `reset_representation_facts` from existing blocks. After
type inference, a typed receiver with a potentially non-numeric key selects
observable storage and reruns the existing solver once before choosing ABIs.
The storage decision is monotone; numeric/optional-numeric reads retain their
preceding specialization and capability boundary (**D1.3v3**, **D8.4.1v2**).
No runtime helper or ordinary-container flag is added. Direct and returned
computed method aliases have focused checks. The follow-up
`typed_methods4/mvp-typed-methods4.exe`, SHA-256
`65ab0e2c7da8cf8db6aa39dacc0605bb6a6767eab0e06722c4f0e236ecc139b4`,
passes **227/227** focused and **227/227** forced/poisoned-GC checks. Both direct
and saved computed-method reproducers complete successfully on this pin.
Its fresh selected canonical run passes **807/842** with **35 admission
failures**, no timeouts and no missing outcomes; all **779** additional required
strict variants pass. Thus **807/842 IDs pass every required mode** on this pin,
including **515/550** callback IDs and all **292** preceding selected passes.
No isolated replay is needed. The unsuccessful ID set equals `typed_methods2`'s
35 dynamic-Function harness cases. Full-LJS Test262 passes **40,261/40,261**,
zero regressions. The Lambda/Input gate completes at **6,880/6,881**, with the
same LaTeX corpus timeout; this pin does not close aggregate acceptance. Its
whole-population required-mode audit is running against the immutable source
snapshot. Neither selected audit is a new whole-population coverage result.
Benchmark timing remains deferred.

#### Completed typed_methods4 whole-population audit

The immutable `typed_methods4` pin completes its whole audit at
**27,597/40,261 distinct required-mode passes (68.5%)**: **24,502 positive** and
**3,095 negative**. Canonical modes finish at 27,598 passes, 12,648 failures and
15 timeouts after isolated replay. Additional required modes finish at 25,780
passes and one failure. The strict replay resolves all 186 interrupted outcomes
without changing limits; original captures remain available.

Relative to buffer7, this pin gains **2,192** IDs and loses **one**:
`language_expressions_multiplication_bigint_arithmetic_js` times out in the
audit and a subsequent isolated replay. The same isolated control passes on
buffer7. Its cause remains under investigation; it is not excluded or counted
as passing. **8,638** more fully passing IDs are needed for the 90% target.
The source snapshot, `whole-audit.json`, `residual-inventory.json`, and separate
`lost-pass-replay/` captures are under `temp/mvp_test262_90/typed_methods4/`.
Newer pins are not mixed into this count. Timing remains deferred.

### Parallel interpreter, library and async integration (2026-10-14)

Three coding workers own the interpreter bridge, library/compiler work and
async/module adapters. They deliver temp-only patches with source hashes and
focused cases. The integrating agent reviews, applies and builds private
release pins, then runs normal, forced/poisoned-GC, selected required-mode and
shared baseline gates. The frozen target remains **36,235/40,261**; selected
cohorts never replace a whole-population audit. Timing remains deferred.

#### Integrated architecture

- **Interpreter and closures:** `JsInterpHostOps` routes the shared AST
  interpreter through MVP property, call, construction and binding operations.
  Function/eval bodies remain interpreted. MIR direct eval exposes the existing
  canonical Array cells through exact-rooted environment records, preserving
  reference resolution before RHS evaluation. Parameter-expression functions
  project separate PARAMETERS and BODY_VARS records; the promoted
  `js_interp_initialize_body_vars` and one
  `mvp_lmd_dynamic_initialize_body` adapter perform the existing instantiation
  transfer. Default closures retain parameter cells while body vars and hoists
  use their own cells. Plain functions retain their prior path
  (**D5.2–D5.3**, **D5.3.3**, **D6.2.3v2**, **D8.1.3v24**).
- **Suspension and jobs:** native Promise reactions, async/generator activations,
  for-await, dynamic suspended Function bodies and host timers reuse existing
  jobs, request queues, exact roots and forwarding/storage helpers. Public
  Promise expandos use MVP maps while their native exotic carrier is retained.
  Argument packs and scalar results have durable owners across suspension and
  callback domain restoration (**D2.6.9v3**, **D5.2–D5.3**).
- **Library reuse:** typed-array methods and static constructors, generic locale
  strings, weak collections, Set operations, grouping and RegExp protocols
  parameterize existing algorithms. RegExp matchAll iterators share the native
  algorithm; iterator prototypes share the existing hierarchy. Primitive
  String protocol dispatch and split coercion ordering preserve observable
  evaluation order. Immutable program proofs retain the ordinary RegExp bulk
  lane (**D1.3v3**, **D3.4.8**, **D8.4.1v2**).
- **Modules:** source loading, resolution/cache, linking, instantiation and
  evaluation use the shared registry and Lambda module-state slabs. Namespace
  reflection resolves live export cells, including TDZ and ambiguity; only this
  exotic uses VMap. import.meta is cached and precisely rooted per module.
  Shared handwritten JS parsing now accepts empty exports and quoted export
  aliases, with decoded string names retained by the AST. Teardown releases the
  module registry while its runtime state is still alive
  (**D7.2.1**, **D8.5.1v7**, **D5.2–D5.3**).
- **Compiler size:** `emit_iterator_error_pad` factors exceptional iterator
  cleanup once per lexical iterator. Normal loop paths gain no instructions;
  nested catches/finally and destructuring retain their existing close/error
  precedence. Exact root liveness stays in the shared MIR emitter; no vendor
  optimizer, harness timeout or GC ownership rule is changed
  (**D5.1.1v3**, **D5.2–D5.3**, **D8.4.1v2**).

#### Current evidence and open gates

Final integration is progressing through immutable release pins. Completed
selected audits are evidence for their exact populations only:

| Pin / cohort | Every required mode | Remaining evidence |
|---|---:|---|
| parallel12 protocol/async | 5,890/6,063 | Later primitive String.match repairs verified separately |
| parallel28 typed methods/eval/library | 4,529/4,973 | Four distinct timeout IDs across canonical/strict modes; no prior selected passes lost |
| parallel33 class/object/for-in/RegExp | 144/148 | Two older RegExp syntax/name gaps and explicit Unicode-sets/realm boundaries; no prior selected passes lost |
| parallel37 final RegExp residuals | 8/8 | Canonical and additional strict variants all pass; the two preceding syntax/name gaps are fixed |

The latest complete whole audit remains `typed_methods4/whole-audit.json`:
**27,597/40,261 (68.5%)**, **8,638** short of the **36,235** target. Its older
`required-record.json` is a selected 807/842 cohort and must not be used as the
whole baseline. The parallel18 canonical diagnostic and isolated replay ended
at 35,615 PASS, 4,606 FAIL and 40 TIMEOUT, but were superseded before additional
required modes. They establish no new aggregate coverage. The partial parallel33
whole run was stopped when shared LJS verification exposed a real regression;
its captures remain marked cancelled. Do not add gains from different pins.

The superseded `parallel37` runtime pin, SHA-256
`67c01292fa424fb18833bb60aec5db4d6d7568ed32688ff64a01c2bef2e549e3`,
passes **565/565** focused checks both normally and under forced/poisoned GC.
Its shared LJS baseline passes **40,261/40,261**, zero regressions or unstable
outcomes. Both dedicated LJS RegExp fixtures pass AST and MIR with normal and
forced/poisoned collection. The final eight RegExp candidates pass **8/8**
canonical and **8/8** additional strict variants, with no prior selected pass
lost. Its whole audit was cancelled after the complete native JS suite exposed
an async-iterator helper ABI regression. No partial result is used for coverage.
Its Lambda/Input aggregate is **7,019/7,022**: a stale Map/Set freeze golden,
an infinite legacy async-iterator fixture, and the previously observed LaTeX
corpus timeout. The full native suite then finished **202/203**, identifying the
separate raw-iterator ABI mismatch described below. These are superseded checks.

The direct backtracker suite passes **51/51** after two native-fixture
corrections. Their unchanged assertions failed against both the exact
predecessor source and parallel37: non-Unicode incomplete hex escapes and `k`
without named captures are valid identity escapes under
[ECMA-262 Annex B.1.2](https://tc39.es/ecma262/multipage/additional-ecmascript-features-for-web-browsers.html#sec-regular-expressions-patterns).
The corrections retain the original inputs, verify literal matching/nonmatching,
and add Unicode and named-reference rejection controls. Six exact programs pass
Node, predecessor MVP and predecessor LJS; original failure captures are retained.
No Test262 harness/corpus or runtime expectation was relaxed. The fixture-only
correction and final whitespace/comment cleanup are recorded separately from the
immutable runtime source snapshot.

For huge descending quantifier bounds, the focused fixture follows
[ECMA-262 §22.2.1.1](https://tc39.es/ecma262/multipage/text-processing.html#sec-patterns-static-semantics-early-errors):
compare the mathematical decimal values before matcher saturation. Node 22.13.0
accepts the tested descending pair after saturation; that oracle discrepancy is
retained separately. The other 14/15 MVP programs and the shared program pass Node.
Benchmark timing remains deferred; no jq workload has been run.

#### Final delegated increments and repairs

- **Class and dynamic source:** shared early-error validation handles method homes,
  derived-constructor super calls, nested lexical arrows and constructor spelling.
  The existing class name scope supplies strictness to anonymous/named heritage,
  computed names and bodies. Hosted object methods/accessors reuse existing
  descriptors and method homes. Calls preserve eval/call/apply forwarding context
  while actual source calls install their own initializer context. With/module
  eval callers remain explicit capability boundaries (**D1.3v3**, **D6.2.3v2**,
  **D8.1.3v24**).
- **Nominal-parent lifetime:** forced collection exposed an untraced dynamic base
  constructor behind the shared nominal link. One trailing parent Item in the
  existing dynamic constructor closure now preserves that edge even after both
  public prototype links change. Existing closure counts/tracing own it; closed
  static classes keep their prior layout/path. The unchanged original fixture
  and two controls all crashed on parallel33 and pass the Node oracle
  (**D3.4.8**, **D5.2–D5.3**, **D5.3.3**).
- **Hosted for-in:** the shared key snapshot and liveness policy handle inherited
  keys, primitive boxing, absent proxy keys, deletion and suspension. Existing
  loop scopes/completions and activation roots own the snapshot and cursor. The
  MIR receiver is boxed once only when mutable prototypes make primitive
  enumeration observable. Deep chains no longer stop at an arbitrary 64-link
  cutoff; ordinary closed paths retain existing proofs (**D1.3v3**, **D5.2–D5.3**,
  **D8.4.1v2**).
- **Module lifecycle:** shared registry cleanup now unregisters `cjs_module`
  alongside its other descriptor Item roots. This pre-existing omission was
  reproduced against parallel35 archives by checking the exact stale slot before
  requesting collection; the corrected fixture passes normally and with forced/poisoned GC.
  No new root cache or module representation is introduced (**D5.3.3**, **D7.2.1**).
- **RegExp:** `JsRegExpIndicesOps` shares result-pair construction, unmatched
  captures, null-prototype groups and named-pair identity. A direct dense-array
  read regressed five LJS cases because retained LJS can use companion property
  storage; existing rooted `get_key` fixes all five. The shared compile operation
  preserves native brands, original pattern/flags, coercion order, payload
  ownership and strict lastIndex failure ordering. No-d paths allocate no indices
  scratch. The decoder extraction reuses existing UTF primitives and StrBuf,
  preserves original public spelling, and checks quantifiers in the existing
  parser. Unicode sets and new realms remain separate boundaries (**D1.3v3**,
  **D5.3.3**, **D8.4.1v2**).
- **Compiler size:** shared exceptional iterator cleanup reduces the includes
  reproducer from 1,865 IteratorClose calls to two without adding normal-path
  instructions. Cold top-level property reads then reduce its finalized main
  from 142,142 to **77,724 MIR lines**; capture-by-index drops from 97,184 to
  **59,734**. Capture-by-index now passes at 7.35 seconds under the unchanged
  ten-second limit; five of six TypedArray probes still time out. Existing
  scalar retention and sized-number widening cover the C helper boundary.
  No vendor optimizer, harness limit or ownership rule is changed
  (**D5.1.1v3**, **D5.2–D5.3**, **D8.4.1v2**).

The final candidate must pass normal/forced-GC checks, the shared LJS gate and
Lambda/Input verification before closure. Measure all frozen IDs and additional
required modes on that same immutable candidate. No ordinary-object flag, VMap
conversion, second closure representation or conservative stack scan is
introduced. Performance acceptance remains open until the deferred matched
benchmark runs are completed.

#### Integrated native audit and iterator API restoration (2026-10-15)

The complete native JS suite exposed an actual contract regression that source
Test262 did not exercise: `js_async_iterator_close_result(Item iterator)` had
been repurposed to accept the new private record. A native caller's ordinary
Map was consequently read as an Array. The original helper now again calls
`js_iterator_call_return(iterator, false)`. The disclosed
`js_async_iterator_record_close_result` names the existing record operation;
interpreter, MIR, MVP and generator-delegation callers use it explicitly.
Both imports retain may-GC/reentrant/boxed-Item metadata. No type guessing,
algorithm duplication or ordinary-call overhead is introduced
(**D1.3v3**, **D5.2–D5.3**, **D8.4.3**).

The original failing native assertion is unchanged. A new control checks absent
and undefined record results, a guest Array passed through the raw API, and
import metadata. The immutable `parallel38` CLI SHA-256 is
`8747e0880ae733144dd4849b67ca8dc307ff92036b72345b2b7da7f2b36140c1`.
It passes **565/565** MVP focused tests both normally and with forced/poisoned
GC, **204/204** complete native JS tests, **51/51** backtracker tests and **5/5**
affected native groups under forced GC. The shared LJS gate passes
**40,261/40,261**, zero regressions or unstable outcomes. The final Lambda/Input
aggregate is **7,224/7,225** (Input **2,112/2,112**, runtime **5,112/5,113**), with
only `latex_test_latex_phase3_corpus` retaining its previously observed timeout.
Native JS **204/204**, JS files **503/503** and MVP **565/565** also pass in that
aggregate. Current runtime source hashes still match the immutable pin.

The fresh whole audit is running. Canonical and all 37,098 eligible additional
strict modes are measured independently on the same immutable binary; final
coverage intersects their outcomes. Strict work started after the broad baseline
finished. Its three workers use ten-case batches to reduce unexecuted work after
the existing timeout/signal retirement path. Canonical batches retain fifty
cases. Every case keeps its existing deadline, and unresolved outcomes are
replayed individually. A separate final verifier checks mode manifests, source,
runner/corpus/log hashes, recomputes the intersection and compares earlier fully
verified cohorts before publishing coverage. Runtime, archive, fixture and patch hashes
are retained under `temp/mvp_test262_90/parallel38/` and the delegated patch
packages. Selected and whole required-mode records remain separate.

Two additional local fixture repairs preserve their original intent:

- Frozen Map/Set entries remain mutable; freezing protects ordinary own
  descriptors. The original eight scenarios are retained, with entry-effect
  checks and frozen-property/strict-write controls added. Exact original and
  strengthened programs agree between Node and pinned LJS.
  [ECMA-262 SetIntegrityLevel](https://tc39.es/ecma262/multipage/abstract-operations.html#sec-setintegritylevel)
  and [Map.prototype.set](https://tc39.es/ecma262/multipage/keyed-collections.html#sec-map.prototype.set).
- A true async iterator's result value is passed through. The old close fixture
  returned `done:false` forever and incorrectly relied on an extra implicit
  value await. Its explicit body await now provides the abrupt completion while
  preserving the close assertion. A finite control checks Promise/thenable
  identity, zero then-getter reads and no close on natural completion, in AST
  and MIR. Exact programs agree across Node and both LJS backends.
  [ECMA-262 ForIn/OfBodyEvaluation](https://tc39.es/ecma262/multipage/ecmascript-language-statements-and-declarations.html#sec-runtime-semantics-forin-div-ofbodyevaluation-lhs-stmt-iterator-lhskind-labelset).

These fixture changes alter no Test262 source, harness, metadata, deadline or
baseline. Original failures, sources and independent controls remain archived.
Benchmark timing is still deferred. The historical residual inventory records
1,664 older nonpasses superseded by selected canonical passes and 2,982 queued
candidates; those bookkeeping figures are not current coverage or confirmed
current failures. Only the fresh same-build whole audit can close the 90% gate.

#### Parallel39–41: Array, String and shared-buffer residuals (2026-10-15)

Three coding workers staged independent packages against merged HEAD
`bb7f78bf05360be13d3559dfe5b53f1f27c7c8c8`; the integrating agent owns review,
conflict resolution, builds and final verification. A private release control
from that HEAD has SHA-256
`ed415fb6afd84481da7bdbb5fd222398f5adbea03218f6b0f9d12e1c93799b41`.
The older parallel38 whole audit remains attributable to its own archived build.

- **Array:** parameterize the existing copyWithin/toSorted/toSpliced/with and
  Array.of algorithms with immutable `JsArrayOps`/`JsArrayIndexOp`. Disclosed
  boundaries are `js_array_method_with_ops`, `js_array_of_with_ops`,
  `js_array_default_ops`, `mvp_lmd_array_ops`, and the promoted existing
  `mvp_lmd_buffer_ops`. Reuse frontend property/constructor operations and
  sorting. Preserve holes, strict writes/deletes, intrinsic copying results,
  comparator/coercion order and scalar homes. Reuse the existing rooted
  `js_create_data_property` for non-Array creation, including function-valued
  custom constructor results and Proxy callbacks. Native Array subclass
  allocation is a separate residual.
- **String:** promote existing `JsStringIntrinsicOp`; extract the existing text
  algorithms as `js_string_text_with_ops` with `JsStringTextOps`. Register the
  thirteen Annex B HTML wrappers, normalize, localeCompare, isWellFormed,
  toWellFormed and toLocaleUpperCase. Reuse existing normalization, WTF8, HTML
  rendering and Lambda case conversion. Retain LJS's non-Intl locale policy.
  Empty input still converts and validates normalize's form. replaceAll remains
  a separate reuse audit.
- **Shared buffer:** promote `js_construct_arraybuffer_target`; factor
  `js_arraybuffer_slice_kernel` and parameterize
  `js_sharedarraybuffer_operation_with_ops`. `JsBufferViewKind` selects the
  existing ArrayBuffer/DataView/SAB families. Keep exported wrapper signatures,
  existing carriers/backing storage, physical brands and precise roots. SAB
  species rejects identical backing storage even through another carrier. Its
  optional nominal class and three slots are planned only when observable.
  Atomics, agents and additional realms are excluded.

Reuse and ordinary-path separation follow **D1.3v3**, **D2.6.9v3**,
**D5.2–D5.3**, and **D8.4.1v2**. Container/inline String addresses are nonmoving;
Array/Map data buffers can compact (**D4.3.1**, **D4.3.2v2**). New ownership
boundaries preserve callback-live inputs; they do not imply object relocation.
All existing method IDs and successful lookup prefixes are retained. No new
MIR import, container flag or backing-store representation is introduced.

Before integration, fifteen sampled frozen Test262 IDs fail on parallel38.
The merged control also reproduces missing Array.of indexed fields on a
function result (AST/MIR, normal/forced GC), Proxy descriptor reentry failure
under forced GC (AST/MIR), and empty-input normalization ordering failure in
both backends. Exact source, binary and patch hashes are archived under
`temp/mvp_test262_90/parallel39/`. These are measured controls, distinct from
other source-audited ownership refinements.

The feature union contains **500 frozen IDs**: Array126, String160 and SAB214.
Require normal/forced-GC focused checks, all required modes for those IDs,
complete native/shared LJS and Lambda gates on the integrated pin. Preserve
all Test262 harness, corpus and deadline settings. The ongoing whole audit and
the later candidate's selected results must not be added across builds.
Benchmark timing and performance acceptance remain deferred/open.

The first integrated pin (`parallel39`) passes **600/601** focused tests and
all twelve AST/MIR normal/forced-and-poisoned-GC shared bug controls. Additional
probes reproduce four computed String-method crashes, while numeric and
canonical numeric-string indexing controls pass. The existing planner omitted
primitive prototypes for computed method names, exposing unsupported internal
method tokens. The correction reuses `primitive_property_key` only where a
receiver can be String and the key-domain proof cannot exclude named hooks.
Canonical numeric strings reuse `mvp_lmd_string_key`; optional-family assertions
protect numeric/decimal-affix indexing. New text token arms are omitted only
when planned live prototype functions make them unreachable. No new helper is
introduced (**D1.3v3**, **D8.4.1v2**).

The focused failure is the original metadata program's valid `let of` binding.
Minimal probes fail on both MVP and LJS AST and pass in Node. Existing parser
identifier predicates already admit contextual `of`; lexical-declaration
lookahead incorrectly excluded it. Remove that exclusion, retain legacy `in`
handling, and cover ordinary/strict bindings, references, parameters,
destructuring and loop-head ambiguity with eleven positive and six negative
programs. Keep the original metadata fixture unchanged. Eight frozen for-of
controls enlarge final selection to **508 IDs**. This is an owned handwritten
parser change, not generated or vendor code.

Node22 agrees on all revised Boolean oracles except SAB invalid-maximum ordering.
The current [AllocateSharedArrayBuffer algorithm](https://tc39.es/ecma262/multipage/structured-data.html#sec-allocatesharedarraybuffer)
and frozen `options-maxbytelength-compared-before-object-creation.js` require
RangeError before reading the prototype. Retain that expectation and its exact
Node divergence. A separate small valid request checks prototype-getter abrupt
identity without relying on a huge allocation request.

A concurrent merge advanced HEAD to
`e75979496d85bb9b33aa4cbde62516ef9db50131` during the intermediate build;
its changes relative to the control touch CSS/Radiant, not JS. The final
`parallel40` build records and guards all tracked C/C++/header/config inputs
and HEAD, together with the complete dirty-source snapshot. Its integrated
run passes **612/613** focused tests, **204/204** native JS tests,
**51/51** backtracker tests, **5/5** affected native groups with forced GC,
and twelve repaired computed String/index controls normally and with forced GC.

The remaining new negative group exposes two general shared early-error gaps:
multiple iteration declarators are silently truncated by the loop AST builder,
and sloppy lexical bindings named `let` are accepted, including destructuring.
Five minimal probes reproduce acceptance in both MVP and LJS AST; Node rejects
them. The parser now rejects multiple declarators before `in`/`of`. The existing
reserved-binding walker accepts a lexical policy, propagated through the same
binding-child traversal, and validates declared loop patterns after their
declaration wrapper is stripped. Property keys, default expressions, sloppy
var/parameter names and C-style multiple declarations retain their existing
meaning. These changes run only during validation; no new helper or runtime
path is added (**D1.3v3**, **D8.1.3v24**). The original negative fixtures remain
unchanged, with fourteen positive and twenty-two additional negative controls.

Pin40's selected/forced-GC aggregate work was explicitly superseded after these
defects were reproduced; partial captures remain diagnostic and are not coverage.
Pin `parallel41` passes **616/616** focused checks both normally and with
forced/poisoned GC, **204/204** native JS checks, **51/51** backtracker checks,
**5/5** affected native groups with forced GC, **39/39** direct regression
probes and twelve shared AST/MIR normal/forced-GC controls. Its **519-ID** frozen
selection has **497** fully passing cases in all required modes: canonical
497 PASS / 21 FAIL / 1 TIMEOUT after retained individual replays; all 485
additional required strict variants pass. No prior passing ID within the
selection is lost. Shared LJS records **40,260/40,261** fully passing, one
RegExp character-class case passing only on retry and zero regressions. Lambda
aggregate verification is deferred to the next integrated pin. Whole pin38
continues independently; selected gains are never added to an older whole
score. Benchmark timing remains deferred.

#### Parallel42: selected residuals and bounded entry emission (2026-10-15)

The selected failures drive three scoped followups, delivered by the same coding
workers and integrated by the parent:

- Extract existing Annex B `substr` as disclosed
  `js_string_substr_with_ops(primitive_string, start, length, JsBufferViewOps*)`.
  Reuse relative-index and ToIntegerOrInfinity coercion, UTF16 slicing and the
  existing frontend numeric policy. Adopt both scalar arguments before
  receiver/coercion callbacks; convert length even on empty/out-of-range input
  and clamp infinities before integer narrowing. Append the method/catalog ID.
  Reuse the planned Number prototype adapter for arbitrary inherited reads and
  calls; closed numeric paths remain optional-family-free. Ten exact Node
  oracles agree; pin41 reproduces the original Number capability errors.
- Reuse `direct_define_pattern` before binding lexical iteration patterns.
  Existing scope-definition checks reject repeated decoded bound names after
  the parser strips the declaration wrapper. Property keys/default expressions
  stay outside BoundNames; `var` duplicates and separate lexical scopes retain
  their meaning. Thirteen positive and fourteen negative exact Node oracles
  agree. No helper or runtime operation is added.
- The remaining Array copyWithin/resizable-buffer timeout is compiler cost.
  A retained sample of its exact canonical harness source shows every sampled
  frame in MIR register allocation. Its main function has 60,525 MIR lines,
  19,662 virtual registers and 1,941 calls. Thirty-nine generic read trees
  contribute 12,232 lines; global cache decoders add more. The test operates on
  buffers of at most six elements, so the measured cause is not element copying.
  Add disclosed compiler-only `compact_property_dispatch`: after a deterministic
  4,096-emitted-register budget in a dynamic-source entry, reuse existing
  `mvp_lmd_property_read` for canonical, unknown, unshaped reads and omit new
  local/global cache decoders. This is a compiler cost heuristic; repeated loops
  are not assumed cold. Preserve explicit shared caches, known shape/sequence/
  native/private/with/super and deferred-key exits. Separately compiled and
  inlined functions and closed/small entries retain existing emission. Reuse
  `mvp_lmd_string_key` to classify immutable literal/lexical typed-array keys at
  compile time. Add no runtime helper, ABI, vendor patch or deadline change.
  Disclosed test-only `mvp_compact_entry_source` shares synthetic size pressure
  across semantic, ownership, MIR-growth and unaffected-path controls.

Ownership and dispatch boundaries follow **D1.3v3**, **D4.3.1**,
**D4.3.2v2**, **D5.2–D5.3**, **D8.1.3v24** and **D8.4.1v2**.
The final frozen selection is **527 IDs**, including twelve duplicate-binding
controls with overlaps. Require a guarded private release build, normal and
forced/poisoned focused checks, exact compiler-timeout replay under the unchanged
limit, all required selected modes, full native/shared LJS and Lambda gates.
Record individual replays separately. Benchmark performance acceptance remains
open while timing is deferred. Exact patches, source/binary hashes, Node oracles
and compiler samples are retained under `temp/mvp_test262_90/parallel41/` and
`parallel42/`; this selection does not replace the whole-population audit.

Pin `parallel42` has guarded private release SHA-256
`c007ab9c042557fcaf011e2113630a6d97d35a660bc2af0b19a0b2d10db87392`.
All 1,445 tracked C/C++/header/config inputs and HEAD match its build record;
the existing 58 enum entries and 69 catalog-prefix rows remain unchanged.
It passes **635/635** focused checks normally and with forced/poisoned GC,
**204/204** native JS, **51/51** backtracker, **5/5** affected native GC,
**39/39** regression probes, twelve prior shared controls and forty additional
AST/MIR normal/forced-GC followup controls. Parent Node oracles pass **57/57**.

Selected canonical execution passes **527/527**. Additional strict execution
initially records 501 PASS / 2 TIMEOUT / 10 missing-result FAIL; isolated replay
recovers eleven of twelve unresolved cases. The assembled all-required record is
**526/527**, with 512 strict PASS / 1 TIMEOUT and zero prior selected losses.
The independent verifier reconstructs raw outcomes, unresolved populations,
replays, strict-only metadata and binary/corpus provenance. A later exact strict
copyWithin diagnostic passes at 7.8 seconds; it does not overwrite the retained
timeout or increase this score. Normal MIR shrinks from 60,525 to **45,589** main
lines and from 19,662 to **14,615** virtual registers; this is compiler-size
evidence, not a benchmark measurement.

Shared LJS remains **40,260/40,261** fully passing, with the same RegExp case
passing only on retry and zero regressions. Its original issue is the unchanged
three-second slow classification, distinct from the original case watchdog.
Fresh isolated diagnostics show an older-build timeout and a current-build
signal-11/abort near the watchdog; no candidate-only cause is established.
Relevant RegExp algorithms are unchanged, and the crash diagnostic stays open.
Lambda/Input verification is running; the newly merged music-layout GC stress
fixture hit the baseline idle limit. Preserve that failure and compare the
merged pre-increment release control before attribution.

#### Parallel43: import early errors and guarded inline growth (integration)

The whole pin38 canonical capture drives another grammar residual: **84** frozen
ImportCall syntax negatives. Four minimal uncalled-body probes reproduce the
wrong admission phase or acceptance on pin42; LJS AST/MIR also accepts them and
Node rejects all four. Reuse the owned parser's argument count, spread flag,
lookahead and error reporter to require one/two ImportCall arguments without
spread and reject bare `new import(...)`. Preserve trailing commas, grouped
sequence arguments, parenthesized constructor targets, `import.meta` member
constructors and ordinary properties named `import`. This adds no helper or
runtime/module behavior. Fourteen syntax positives and sixteen negatives agree
with Node; valid two-argument parsing remains separate from MVP import-options
execution admission (**D1.3v3**, **D8.1.3v24**).

Remaining compiler analysis finds nine guarded `MayNeedBigInt` inlines occupying
**16,855/45,589** entry lines and **477/1,558** calls. Method-token fanout is not
the dominant cost. Reuse `compact_property_dispatch` at the existing guarded
candidate loop to stop starting further guarded inlines after the emitted-cost
budget. Preserve direct inlining, already-active inline bodies and separate
functions. The first permitted body may cross the budget; this is not a strict
whole-function line cap. Callee capture, argument snapshots, ABI validation,
generic invocation and roots retain their existing paths (**D5.2–D5.3**,
**D8.4.1v2**). Two new focused groups share the existing test source builder;
ten exact Node oracles agree. No new helper, runtime ABI/import, vendor edit or
deadline change is introduced. Both patches are staged for the next guarded
integration, with compiler-size/required-mode replay and full shared gates still
required. Timing and benchmark performance acceptance remain deferred/open.

Pin42 Lambda/Input baseline records **7,067/7,069**, with all runtime/build-input
guards matching its private build. Input is **2,112/2,112** and runtime is
**4,955/4,957**; the stress executable's aggregate fallback is one failed gate,
not a pass count for its unfinished corpus. Its last case is the merged
`regression_music_layout`; the other failure is the previously observed LaTeX
corpus timeout. A bounded release diagnostic on merged pre-increment control
`parallel39-base` and pin42 passes music layout normally on both and reaches the
60-second diagnostic bound under forced/poisoned GC on both. Preserve the full
baseline idle failure and do not attribute it to the JS increment or call the
broader gate green. This diagnostic does not alter any original deadline.

Parent integration of the two parallel43 patches includes two more positive
nested-spread parser controls (spread inside the specifier array or options
object remains legal). All **42** exact Node controls agree. The frozen union is
**611 IDs**. The next guarded build and all selected required-mode, focused/GC
and shared gates are pending; no gain is added to an archived whole score.

Pin43 runtime SHA-256 is
`99dac6c11d1c67c2734f453573b347dfa8d958a915880b3a70eaf4cfa7080c0d`.
It passes **611/611** selected canonical cases and **589/589** additional strict
variants, with zero lost prior selected passes and no isolated replay needed.
The independent raw/mode/provenance verifier confirms **611/611**. Native JS
**204/204**, backtracker **51/51** and affected native GC **5/5** pass; shared LJS
retains **40,260/40,261** fully passing, one retry-only RegExp result and zero
regressions. Its normal focused run is **639/640** because a newly proposed MIR
shape assertion has two invalid assumptions, while its semantic controls pass.

Parent old/new probes find the original small source has one guarded inline on
both pins (782 pre-generation registers/2,267 lines). Post-generation MIR moves
the literal target into a temporary before `bne`; the test's line-only `bne`
matcher therefore misses the guard. Count body-scoped function references,
which handles the lowered form. Replace the unverified fixed 6,000-line growth
allowance with the separately emitted callee size times added calls. On exact
pre-generation controls, old four/twelve-call growth is 10,893 lines versus an
8 * 1,128 = 9,024-line callee bound; new growth is 4,121. The corrected oracle
rejects the old expansion and accepts the new policy. Original source programs
and semantic assertions stay unchanged. This is a test-only correction, not
a runtime change or Test262 expectation update. Pin43's partial broad baseline
was explicitly superseded after its build completed; owned process-group proof
and failed/partial captures are retained. No partial capture is published green.

The next `parallel44` guarded build differs only in the corrected focused test
and evidence docs. Require fresh normal/forced-GC focused checks, selected modes
and shared gates. Pin43's normal compiler diagnostic now finishes in 3.1 seconds
under its 10-second diagnostic bound; main MIR is **29,981** lines/**9,687**
registers, versus 60,525/19,662 before these compiler changes. Retain this as
compiler-size/deadline evidence, not benchmark performance or whole coverage.

#### Parallel44 verification and archived whole-audit milestone

Pin44 SHA-256 is
`8dc4d7b70b645683c4307020f14d2762c9593bfd67d5434f9c46dafff8ad66d2`.
All **1,445** tracked build inputs match the guarded build, and its runtime
sources equal pin43's. The corrected focused MIR oracle now passes with the
original semantic controls: **640/640** normally and **640/640** under forced/
poisoned GC. Native JS **204/204**, backtracker **51/51**, affected native GC
**5/5**, regression probes **39/39**, previous shared controls **12/12** and
additional String/Number shared controls **40/40** pass. The independent raw/
mode/provenance verifier confirms **611/611** selected all-required passes,
including **589/589** additional strict variants, without isolated replay or
lost prior selected passes (**D1.3v3**, **D5.2–D5.3**, **D8.4.1v2**).

Shared LJS retains **40,260/40,261** fully passing plus the same retry-only
non-whitespace RegExp case, zero regressions. Lambda/Input records
**7,073/7,074**: Input **2,112/2,112**, runtime **4,961/4,962**. The merged
music-layout forced-GC stress executable hits the existing idle limit; the
unfinished executable is one failed aggregate gate, not a green corpus. The
LaTeX corpus passes in this run. Runtime/test source and all tracked build-input
guards remain true before and after the broad baseline. Its ordinary debug
baseline binary is recorded separately from the pinned release executable.

The archived pin38 whole audit has completed on its original executable,
SHA-256 `8747e0880ae733144dd4849b67ca8dc307ff92036b72345b2b7da7f2b36140c1`.
The independent verifier reports **37,445/40,261 (93.0056%)** all-required passes,
**1,210** above the **36,235** target. Canonical results after exact isolated
replay are **37,470 PASS / 2,767 FAIL / 2 CRASH / 22 TIMEOUT**; the **34,411**
required additional strict modes give **34,386 PASS / 23 FAIL / 2 TIMEOUT**.
The all-required pass set contains **34,241 positive / 3,204 negative** IDs;
negative passes retain their expected phase/type. The resulting **2,816**
nonpassing IDs stay explicit. Canonical replay reuses
only the exact complete same-pin isolated prefetch; original logs, mode
metadata, source/corpus/runner hashes and unresolved failures remain retained.
Primary evidence is `temp/mvp_test262_90/parallel38/required-record.json` and
`whole-record-verification.json`, not a sum of selected cohorts.

Pin38 loses no pass from the **27,597**-pass `typed_methods4` whole entry audit
or pin18's canonical controls. Four later selected all-required controls remain
TIMEOUT losses: TypedArray `fill`/`includes` resizable-buffer cases and comment
tests `S7.4_A5`/`S7.4_A6`. Do not broaden the zero-entry-loss claim to these
controls or replace their original timeouts with partial/retry evidence. The
parent replays those exact cases on pin44; workers inspect repeated eval and
shared RegExp costs using existing kernels. No new helper is proposed at this
checkpoint. The merged music stress issue remains diagnostic without a proven
runtime cause.

Pin44's fresh **40,261 canonical / 37,098 strict-eligible** whole audit started
after its broad baseline finished. It retained unchanged cases, runner and
deadlines, then was explicitly superseded by pin45's runtime fixes before
completion; partial logs and owned process-group proof remain. Pin38 establishes the
archived coverage milestone; current-build coverage, zero lost later controls,
the shared baseline gate and benchmark performance acceptance remain separate.
Benchmark timing is still deferred at the user's request, so the phase is not
marked complete (**D8.1.3v24**, **D8.4.1v2**).

#### Parallel45: close repeated-eval and native replacement costs

The parent replays all four archived selected-control losses on pin44 with the
unchanged runner/deadlines, separately in canonical and strict modes. Both
TypedArray cases pass in both modes; both comment/eval cases still TIMEOUT.
Their captures remain in `parallel44/control-regression-{canonical,strict}`.

Source review identifies missing reuse: ordinary LJS recognizes comment-only
eval before parsing, but the hosted MVP direct path only recognizes zero-length
strings and the indirect path parses every String. Thus each empty Script
allocates and retains a complete AST/pool/script. Reuse the existing
`js_eval_source_is_blank(String*)`, promoted to `js_interp.hpp` after disclosure,
in hosted direct/indirect eval. Replace its duplicated scanner with the shared
JS lexer and require EOF at the actual source length, permitting only an initial
hashbang token. Real tokens, four line terminators followed by code, malformed
comments, misplaced hashbangs and non-string identity keep their proper paths.
The borrowed String proof is allocation-free and adds no realm, cache or runtime
helper (**D1.3v3**, **D8.1.3v24**, **D5.2–D5.3**).

The shared retry-only non-whitespace RegExp case takes generic match-record
construction for every wide subject. Reuse the existing guarded native bulk
replacement loop for non-ASCII subjects only when compiled metadata proves one
of the six built-in classes with `+`. Those nonempty runs cannot split an astral
pair; matcher byte spans support the existing literal/template substitution.
Native `exec`, coercion rechecking, writable `lastIndex`, no captures and the
existing Unicode/indices/UTF16-subject exclusions remain. Callbacks retain their
generic UTF16-index path. Precise roots retain immutable, nonmoving String bytes
(**D4.3.1/D4.3.2v2**, **D5.2–D5.3**, **D8.4.1v2**).

Independent source review finds an existing silent one-million-match cutoff in
that bulk replacement loop. Remove it using the matcher's monotone byte-position
contract; an empty ASCII match advances one byte, and a terminal empty match is
processed once before stopping, avoiding end-plus-one overflow. A control with
**1,000,003** wide and ASCII runs checks every replacement and final legacy match
state. No matcher, helper, ABI, vendor, corpus or deadline change is introduced.

Parent Node controls pass **6/6** eval programs and **17/17** replacement
programs. The integrated tests add a shared 23-case byte proof, direct/indirect
and hosted eval semantics, Unicode/templates/override/coercion/GC cases and an
existing-counter optimization assertion. Pin45's guarded release build, SHA-256
`89dfb9fb90f5a1705796f9f20820e72147572cdd09b95ba7ecbe8c3fe053c8c4`,
passes **647/647** focused tests, **205/205** native JS tests, **51/51**
backtracker tests and **6/6** affected native forced-GC groups. Forced/poisoned
focused GC also passes **647/647**. The independently verified selected audit
passes **616/616** all-required IDs, including **594/594** strict variants and
all four archived selected-control losses. Its frozen population extends
pin44's selection with the four archived losses and the official non-whitespace
case. Both official comment/eval cases pass canonical and strict replays; the
TypedArray `includes` case remains timeout-unstable in that replay. The exact
ASCII million-match assertion fails on pin44 and passes pin45 AST/MIR, while
Node agrees with the assertion. Pin44's unfinished audit is superseded with
its original partial evidence retained; owned process-group checks precede
termination. Pin45's waiting whole-audit queue is superseded before starting,
pending the proven signal-ownership fix below; its selected and broad gates are
left running, with their original evidence retained.
Neither selected additions nor structural cost reductions alter pin38's
**93.0056%** score. No new benchmark timing or performance acceptance is claimed.

#### Batch watchdog ownership (integrated; final verification pending)

Pin45's shared LJS baseline reports **40,261/40,261** passing and zero slow cases,
but raw evidence records an initial **SIGABRT** after **516/600** cases.
Eighty-four collateral cases recover and one original crash-point retry passes.
Keep the initial abort explicit rather than describing the run as clean.
The native report identifies a stack-canary failure at `js_strict_equal`'s return;
two OS threads have stack pointers in the same 256 MB execution stack. The
joining launcher and the execution worker both leave process-directed SIGALRM
unmasked, while timeout/crash/MIR recovery records are process globals. A signal
delivered to the launcher therefore jumps onto the still-running worker's stack.
The engine's existing TLS recovery model already forbids this
(**D5.3.6v2**, **D6.3.3v2**); no ruling changes.

Reuse the existing batch handlers and jump records, making the records and
armed flags thread-local. Register one immutable pthread owner before installing
the alarm handler; a TLS signal-safe owner bit selects local recovery. Foreign
alarm delivery forwards to that owner with `pthread_kill`, preserving errno;
forwarding failure exits visibly with 124. Arm a timeout only after its checkpoint
exists. This is one owned CLI-source fix, with no new helper, production API,
vendor edit, case omission or deadline change. Native direct/foreign signal
controls and unchanged Lambda/LJS/MVP batch gates remain required before a
fresh whole audit. An earlier one-second diagnostic deliberately triggers the
fault on pin45; raw AST stderr is corrupted. That diagnostic does not replace
the ordinary Test262 deadlines or reclassify any original result.

Pin45's broad Lambda/Input baseline completes **7,395/7,395**: runtime
**5,283/5,283**, Input **2,112/2,112**, including the previously timeout-unstable
**313/313** GC sweep, **92/92** JS optimization contracts and **1,527/1,527**
core runtime cases. All tracked build-input and runtime/test source guards match
its private build before/after the run. Its shared LJS initial abort remains
qualified despite these green source-matched gates.

The parent applies the one-file signal fix only after pin45's baseline finishes.
Pin46 adds the original escaped-Unicode crash point to a **617-ID** frozen
selection. New private release and deterministic native ownership checks,
ordinary timeout/collateral controls, required-mode conformance and shared
baselines are pending. No benchmark timing or phase-completion claim is added.


### Performance verification resumed (2026-10-15; gate failed)

The user requests fresh benchmark verification. Queue46 is stopped while still
waiting, before starting its whole audit; completed conformance evidence is
preserved. A concurrent merge advances HEAD to
`47cffd6163532ebba8764c11195afa0acc644ade`, changing 45 tracked shared build inputs
relative to pin46. Pin47 therefore regenerates the build files and captures a
fresh private `release_native` executable and archives, with all 1,445 production
input hashes guarded. Its CLI SHA-256 is
`47f21fd566c742634137b15f89b61d62cf1f0c14ea104df4d4456a067fa99d19`;
focused checks pass **647/647**. These are current benchmark-source checks,
not a refreshed whole-Test262 or shared-baseline claim.

All **84/84 non-jq** output/backend contracts pass, with seven measured rounds
per lane against the corrected ordinary-release phase-entry control
`32a6174ad5e6769bdbe0df2b8e911b09ccd7770f084e7d5f4104be436aefb394`
and an identical copy. Sources, workloads and oracles are unchanged. Fresh
processes rotate through all six lane orders; script self-times and the original
60 CLI entry self-timers exclude initial compilation. Wall times remain separate.
No jq benchmark is executed or preflighted. The **31-round** followup selects
31 suspects and completes **28** before further testing is stopped at the user's
request. Hyphen retains partial longer samples; Microdiff and Text Search retain
the seven-round screen only. None is presented as a completed confirmation.

The completed confirmations establish eight large regressions:

| Benchmark | Previous ms | Pin47 ms | Ratio |
|---|---:|---:|---:|
| Fannkuch | 0.271 | 46.189 | 170.44x |
| Spectralnorm | 1.068 | 241.136 | 225.78x |
| Base64 | 11.934 | 507.221 | 42.50x |
| Brainfuck | 45.390 | 2401.849 | 52.92x |
| Primes | 2.042 | 794.057 | 388.86x |
| Triangl | 154.516 | 16745.468 | 108.37x |
| Mandelbrot | 0.479 | 1.410 | 2.94x |
| Nqueens | 0.942 | 14.474 | 15.37x |

Hashmap also regresses **3.87%** (502.256/483.526 ms), with 95% ratio intervals
**1.0324–1.0448** and **1.0335–1.0597** against the two controls. Navier–Stokes
regresses **3.91%** (33.354/32.100 ms), with intervals **1.0312–1.0444** and
**1.0328–1.0440**. Fast-diff regresses **5.04%** (603.016/574.100 ms), also
confirmed against both controls. Hashmap and Navier's control/control intervals contain parity. Bounce and
CD's large screen warnings disappear in the longer capture; the original noisy
samples remain intact. An aggregate improvement cannot close this gate.

Read-only source/MIR review establishes two typed-storage admission causes:
constructor arguments are checked before parameter inference, and typed method
names are collected without receiver kinds. The latter includes ordinary
`output.join()` and typed `fill`, despite existing raw fill lowering. Both
permanently promote the unit to generic typed storage, losing raw allocations
and adding property/coercion dispatch to hot loops and other Array/String calls.
Use solved receiver/argument facts and the existing monotone promotion/replay;
reuse raw typed fill only with adequate semantic proofs. Unknown receivers,
method escapes, nonprimitive coercions, buffer/view reflection, detach/resize,
species and prototype observation retain shared full storage (**D8.4.1v2**).
No new helper or normality flag is needed for this proposed repair.

The separate text review identifies extra RegExp dispatch/rooting/exec lookup
for Hyphen and extra protocol/accessor guards for Fast-diff. These are concrete
additional paths, with unmeasured timing shares; Text Search and Microdiff's
causes remain unresolved. Any repair must retain callback semantics, explicit
completions and precise roots (**D8.4.3v2**, **D5.2–D5.3**).

Captures and the verification report are under
`temp/mvp_test262_90/parallel47/performance/`: `screen84/comparison.json`,
`repeat31/comparison.json`, `verification.json`, `typed-array-root.json`,
`text-review/DIAGNOSTIC.md`, `user-stop.json`, and `REPORT.md`/`REPORT.json`. Earlier phase-control
gaps remain separate; no mixed release/profile timing comparison clears them.
No production source was changed during the timing capture. Pin38's archived
**93.0056%** coverage score remains unchanged.

### Repairs after the timing stop (2026-10-15)

The user directs implementation from the completed evidence, with no further
tests. The repair retains shared Lambda representations and existing operations:

- **Eight typed-array regressions:** choose raw versus full storage after solving
  constructor arguments and receiver kinds. Ordinary Array/String methods no
  longer promote unrelated typed allocations; primitive typed `fill` reuses the
  existing raw lowering. Unproven native observations, inherited properties,
  observable views and callbacks retain full storage. Extend the existing
  property-key proof with a numeric/undefined domain option; no benchmark names
  or workload-specific admission rules.
- **Optional keys:** raw `array[undefined]` uses the existing `ArrayNum` Map
  attribute face and shape transitions, separately from its numeric buffer
  (**D2.6.6v3**, **D3.4.3v5**). An option on the existing property-map helper enables
  that face only for named reads/writes; reflection still requires full storage.
  Existing type inference joins possible named-store
  value kinds so arbitrary attributes cannot acquire a numeric ABI. Named stores
  keep the original RHS; only indexed stores convert it. Existing precise GC
  tracing and compaction cover both faces (**D5.3**).
- **Navier–Stokes:** proven primitive property keys retain the native Number to
  String path even when Symbols are available. This removes added numeric boxing
  and scalar homes; Symbol/object/unknown keys keep complete ToPropertyKey.
- **Hashmap:** skip native-ancestry validation when the constructor already is
  the caller's admitted ordinary allocation class. Different `new.target` values
  retain the ancestry walk and live prototype lookup.
- **Fast-diff:** reuse canonical names and initial RegExp accessor layout metadata
  under a shape identity guard, checking live descriptors/getters on every use.
  The related primitive-string `RegExp.test` path also avoids general protocol
  dispatch after checking the live `exec`, prototype and `lastIndex` contracts.
  No mutable per-site cache is added (**D8.4.1v2**).

No new runtime helper or container normality flag is introduced. These changes
remove identified extra work; its timing contribution and the amount recovered
remain unmeasured. The 11 confirmed regressions remain the pin47 evidence, not
new results for these repairs. A shared compiler-local predicate distinguishes
possible typed-value exposure from the narrower numeric-index proof, retaining
the existing string-index primitive proof used by Base64. Host timer return
inference requires an unexposed direct `performance.now` call; mutation, aliases,
callbacks and global reflection disable it.
Implicit coercion, including template interpolation, retains full storage when
the value could expose typed-array behavior.

**Conservative residual:** standalone `test/benchmark/r7rs/mbrot2.js` can still
select full storage when concatenating its unknown nested-matrix result for
failure output. Proving that result numeric requires closed-call aggregate
forwarding analysis. The frozen measured `prior_r7rs_mbrot.js` returns the result
directly and lacks that branch, so this observation does not establish a
remaining promotion in the measured workload. No source or oracle is changed.

**Compile-only integration:** private `release_native` build `perf_fix50`
compiles and links successfully on HEAD
`7666ede5a8b2d27752d20f0e6ad9158601dc57af`, with all **1,451** tracked production
input hashes and HEAD unchanged during the build. Executable SHA-256:
`4bc998925aa055aacddc2a2d4269cbf323d005fe76d589f582521a8a39c9471c`.
The concurrent merge since pin47 changes none of the repaired production files.
Source snapshots, patch, build log and provenance are under
`temp/mvp_test262_90/perf_fix50/`. No executable is run: neither prior correctness
results nor prior timings certify this modified build.

### Full benchmark replay after repairs (2026-10-15)

Private optimized `release_native` HEAD
`43ac706671b58dc2f7dc5852bbe70f94a91d7338`, executable SHA-256
`1c5a3dd7ea2a08f91e15bc00d9d969ee171f30319771ed70e79ab44b67ceec17`.
All **71 standard workloads and 17 microbenchmarks** are attempted, using the
byte-identical archived sources, original self timers and exact output oracles.
MIR is selected explicitly; initial compilation and process time are separate.

**84/88 pass** their output checks. All four jq candidates (`jq_bf`, `jq_mix`,
`jq_records`, `jq_tree`) terminate with **SIGBUS** after 59–64 seconds without
emitting an execution time. The exact Result9 release passes all four checksum
checks. These are correctness regressions and have no candidate speed ratios.

The non-jq screen uses seven rounds with two identical controls. Twelve smaller
or uncertain suspects receive 15 rounds; six further seven-round comparisons use
the later phase-entry release to check recorded gains newer than Result9.
Hyphen retains seven rounds because each process spends 30–50 seconds compiling;
its control/peer variation prevents a confirmed slowdown. Large seven-round
regressions require both ratios above 1.10 and both 95% lower bounds above 1.05.

**12 performance regressions are confirmed.** Ten compare against Result9;
Hashmap and Text Search compare against the later phase-entry release:

| Workload | Slowdown | Previously in pin47's confirmed list |
| --- | ---: | --- |
| Splay | 3.07x | no |
| Fast-diff | 3.02x | yes |
| Triangl | 1.78x | yes |
| Microdiff | +47.6% | no |
| Text Search | +14.2% | no |
| Brainfuck | +11.6% | yes |
| DeltaBlue | +6.2% | no |
| Prettier AST | +6.1% | no |
| Object deletion | +4.1% | no |
| Hashmap | +3.7% | yes |
| NBody | +2.5% | no |
| Formatted output | +2.2% | no |

The earlier orders-of-magnitude typed-array losses disappear, but Triangl and
Brainfuck retain smaller losses. Fannkuch, Spectralnorm, Deriv, FibFP and
Three-way merge do not confirm their initial screen differences in the longer
Result9 comparisons. Text Search is faster than Result9 in this capture, while
the separate entry replay exposes its later regression; reference selection
therefore matters.

The saved-evidence audit verifies **2,740 successful launches, four failed
launches and 310 successful backend artifacts**. Source, executable and module
guards match; production inputs remain unchanged after the build. Published
Results3–9 match their original Git blobs. Competing build activity is recorded,
so individual intervals and identical-control variation remain part of the
interpretation. No implementation or corpus change is made during this replay.

Artifacts: `temp/mvp_perf_all_20261015/REPORT.md`, `REPORT.json`,
`validation.json`, the four capture directories and private build provenance.
Benchmark correctness and performance acceptance fail on this candidate. No
fresh whole-Test262 audit is inferred from the benchmark output checks.

Two initial release debugger replays of `jq_records`, excluded from benchmark
counts and timings, reproduce an instruction-fetch protection fault in writable,
non-executable memory. `crash-diagnosis.json` retains those observations.

### Direct regression repairs (2026-10-15)

Work continues without coding agents. Sources, self timers and published
Results3–9 remain unchanged. Artifacts are under
`temp/mvp_perf_repair_20261015/`.

- **ARM64 jq crash:** generated `parse` spans 1,297,248 bytes. Its backward
  conditional branch needs displacement -1,279,884; truncating the signed
  19-bit instruction offset instead lands 2 MiB beyond the target, in
  non-executable memory. The user-approved vendor repair widens overflowing
  conditions to an inverted short branch and long unconditional branch,
  re-emitting until offsets and switch-table alignment settle. Distant label
  addresses use a rebased literal. Ordinary short instructions retain their
  encodings. The delta is recorded in
  `patches/mir-aarch64-long-branches.patch`; `make verify-mir-patches` passes.
  All **248/248** MIR emission tests pass, including the new >1 MiB reproducer.
- **Second jq crash:** after the branch repair, `tokenise` exposes an independent
  inference error: bare lexical declarations omit their initial `undefined`
  from a subsequently numeric domain. The existing transfer now includes that
  initialization, including block re-entry. The reduced tokenizer passes;
  complete jq replay remains pending. No interpreter fallback is introduced
  (**D8.1.3v24**).
- **Triangl:** align ordinary optional-index inference with its existing closed
  array proof, then retain numeric payload/presence through raw typed-array
  reads. Named typed attributes join their own integer ranges; fractional,
  signed-zero, NaN and arbitrary named values preserve their representations
  (**D2.2.5**, **D2.6.6v3**, **D3.4.3v5**). Seven paired release-MIR rounds give
  **155.019 ms** repaired, **278.262 ms** pre-fix, **155.083 ms** Result9.
  Brainfuck remains **48.310 ms** versus **42.967 ms** Result9. Pin `round2`
  SHA-256 is `d4fc9ed3fa03e68d701a15c27f1c20b7a060667c081230407bb389e99e1b3d53`.
- **Fixed-key literal experiment, withdrawn:** unknown value kinds discard the
  entire allocation plan. A trial retained it with Lambda `TypedItem` slots and
  the existing field reader/writer, preserving scalar snapshots and initializer
  order (**D3.4.3v5**, **D5.2–D5.3**). The first seven-round capture reduced Splay
  from 191.374 to 176.728 ms and Microdiff from 150.249 to 144.698 ms, but Fast-diff
  increased from 633.260 to 674.119 ms. A follow-up with an identical-binary peer
  recorded competing builds/tests and was cancelled. The experiment is removed
  from the retained compiler; `round4/candidate.patch` preserves it for later
  investigation. No performance acceptance is claimed from the cancelled run.

No new runtime helper is added. The `round2` MVP suite passes **647/649**;
the two failures require an obsolete capability error for missing typed-array
named properties. Exact pre-fix, repaired and Node replays all return
`undefined`; the assertions now check that value. The trial `round4` suite passes
**650/650**, and five focused ownership/domain checks pass with collection forced
on every allocation and freed memory poisoned. After withdrawing the literal
experiment, all **1,452 production inputs** again match the `round2` build
snapshot; its SHA-256 is
`d4fc9ed3fa03e68d701a15c27f1c20b7a060667c081230407bb389e99e1b3d53`.
The final MVP suite passes **650/650** with the updated assertions and new
fixtures; the five focused checks also pass under forced collection and freed
memory poisoning. Logs: `mvp-final-tests.log` and `final-forced-gc.log`.

The broader Lambda baseline reports **7,429/7,430**: the LaTeX phase-3 corpus
times out at 60 seconds during the loaded run, then passes unchanged in isolation
in **29.055 seconds**. The Test262 baseline reports **40,259/40,261 fully passing**,
with two slow Unicode identifier cases passing on its isolated retry and **zero
functional regressions**. The harness retains those two as non-fully-passing;
neither harness nor thresholds are changed.

Records and mix finish with exact checksums on `round4`. Its unfinished bf/tree
replay is cancelled when the optional experiment is withdrawn; cancellation is
not a runtime failure. Records also passes on the retained `round2` binary with
exact checksum **878885883**, preserving its MIR backend and original source.
Final replay: `jq-final/comparison.json`; complete validation inventory:
`validation.json`. Mix, bf and tree are not rerun on the final retained binary.
Overlapping validation and explicitly recorded pauses exclude these jq runs
from performance acceptance. Eleven previously confirmed non-jq regressions
remain open; no full-population MVP Test262 score is refreshed.

### Eleven-regression follow-up (2026-10-15)

The next direct repair pass uses immutable release-MIR executables, archived
benchmark sources/oracles and their original self timers. Published Results3–9
remain unchanged. Evidence is under `temp/mvp_perf_eleven_20261015/`.

Implemented repairs follow **D8.4.1v2**, **D3.4.3v5** and **D5.2–D5.3**:

- Recover function identity, scalar domains and fixed prototype setup only when
  the source proves that reflection cannot mutate or escape the relevant global
  bindings. Dynamic code, reflective writers, computed names without a proof,
  intrinsic aliases and property iteration targets keep conservative dispatch.
  The new compiler helper is `plan_fixed_global_bindings`; it reuses the existing
  representation solver, key proof and builtin catalog. Published functions
  retain their boxed entry ABI (**D8.1.3v24**).
- Give `for…in` targets their string domain while joining other writes. Skip
  scalar-home work for proven nonnumeric bindings and closed nonnumeric returns.
  Optional numeric equality and immediate-integer equality retain undefined,
  NaN and signed-zero behavior (**D2.2.5**, **D5.2–D5.3**).
- True, passive ordered loop conditions establish local numeric presence until
  a write. Nested backedges discard enclosing facts; alternative branches start
  from the same facts and intersect them on exit. This removes named-property
  fallback from Brainfuck's guarded typed-array reads without conflating NaN
  and undefined (**D2.2.5**, **D8.4.1v2**).
- Extend that proof to integral local writes: `integer_binding_writes` checks
  every source write, and `ordered_integer_range` proves a bounded integer range
  inside a true ordered loop condition. Fractional, captured and unsupported
  writes keep the original Number path. The existing property-key conversion
  can omit a redundant integer roundtrip for a proven integral domain while
  retaining bounds, NaN/infinity and named-property fallbacks. Integer values
  are materialized at index consumers; arithmetic keeps the binding's physical
  lane. An intermediate eager conversion regressed Triangl by introducing
  float-to-integer/add/integer-to-float on every counter increment.
  The shared compiler helper `materialize_integer_operand` applies the proof
  at array/property indices, character indices and bitwise consumers, reusing
  `scalar_reg`. Optional, fractional and nonfinite operands retain their
  existing conversions.
- Own presence checks return without reading a getter or consulting prototypes.
  Prototype walks reuse the active library through the promoted existing
  `mvp_lmd_get_prototype_with_library` function. Checked global reads resolve an
  own descriptor once; inherited/exotic misses preserve observable Has/Get
  order and restart Get after a trap. The existing property dispatcher uses
  an internal `LMD_PROP_GET_BINDING` operation, with precisely rooted misses.
- The existing species allocator reports whether its result is a private
  intrinsic array. Map/filter, slice/concat/splice, copying reversal and recursive
  flat/flatMap reuse the existing physical array store for those results. Custom
  species, proxies, aliased output and source-side getter effects retain full
  property semantics. The proof is local to the operation; no container flag
  or cache is added.
- Add a `preserve_storage` option to Lambda's existing resolved map setter.
  JS assignment/descriptor paths can retain compatible float, nullable Map and
  generic Item storage, using the existing field writers. This prevents repeated
  whole-global-object reshaping when a counter alternates between integer and
  float encodings. Other callers explicitly keep their previous policy.
- The shared TypeMap field index also records whether any string key begins
  with a digit. A length certificate distinguishes complete summaries from
  unknown or incompletely maintained ones; those retain the original scan.
  Array edit checks can skip scanning every named prototype method, while
  still reading live links, descriptors and extensibility. This is ordinary
  shape metadata, with no per-site cache (**D3.4.3v5**, **D8.4.1v2**).

The retained `integer-consumers` release has SHA-256
`7edac2ef51b9341c5dd810978c216c69bc2015ac9d7926f22b644e7b388fdedc`.
Its rebuilt candidate archives pass **664/664** MVP tests and
**16/16** focused forced-GC/poisoning checks. The same CLI passes **1,957/1,957**
selected canonical Test262 cases and **1,716/1,716** additional strict variants,
without isolated retries. `integer-consumers-test-artifacts.json` records the
actual executable and archive hashes. Earlier private GTest links to a stale
archive are superseded; their counts are not candidate evidence.

The preceding shared-runtime pin additionally passes **4,912 canonical + 4,632
strict** selected cases, after isolated replay of contended watchdog losses.
The full LJS baseline passes **40,261/40,261**. The integrated Lambda baseline
records **7,130/7,132** checks: the pre-existing chart dashboard half-pixel
wordcloud mismatch and an outer GC-suite watchdog remain in that raw aggregate.
The GC timeout collapses 313 cases into one failed row; a separate pinned release
replay passes **313/313**. The LaTeX corpus passes in this aggregate. These broad
gates precede only the final MVP compiler adjustment; shared runtime sources are
unchanged. No golden or harness was changed, and selected variants do not refresh
the archived whole-population MVP coverage score.

Final performance capture: `temp/mvp_perf_eleven_20261015/final-retained/`.
All **15/15** workload outputs pass. Each seven-round comparison rotates the
retained candidate, the pre-repair pin and the historical release, excludes
warmup, preserves the original timing contract and checks source/binary hashes.
Timing uses script self timers or the CLI execution-only timer where that is
the archived contract; process wall time is excluded.
Historical controls use Result9 except Text Search and Hashmap, which use the
Phase34 entry release. Process activity and overlapping background work are
recorded separately; the report retains the raw samples and paired intervals.

| Workload | Retained ms | Pre-repair ms | Historical ms | Historical difference |
|---|---:|---:|---:|---:|
| awfy/deltablue | 53.794 | 54.400 | 50.825 | +5.84% |
| awfy/nbody | 45.728 | 45.886 | 44.015 | +3.89% |
| jetstream/splay | 62.227 | 182.943 | 59.622 | +4.37% |
| js_mvp_lmd/object_delete | 0.873 | 0.866 | 0.819 | +6.59% |
| julia/formatted_output | 103.992 | 104.052 | 103.218 | +0.75% |
| kostya/brainfuck | 42.445 | 47.124 | 41.775 | +1.60% |
| text/fast_diff | 183.222 | 607.139 | 198.428 | -7.66% |
| text/microdiff | 108.575 | 149.557 | 100.604 | +7.92% |
| text/prettier_ast | 512.706 | 550.592 | 515.232 | -0.49% |
| text/text_search | 4222.180 | 5771.065 | 5063.201 | -16.61% |
| jetstream/hashmap | 482.459 | 489.496 | 465.744 | +3.59% |

No competing build/test activity was observed in the final sweep. Busy
system/editor processes overlap some rounds; whole-round sensitivity results
are retained. Every final Text Search round overlaps background CPU activity,
so that row is indicative; earlier quiet captures independently establish its
improvement.

The four controls are retained in the same capture, including Triangl at
121.223 vs 154.481 ms. An intermediate eager integer conversion regressed
Triangl to 209 ms and was rejected; the retained consumer-only conversion
removes that recurrence. Microdiff, Splay, DeltaBlue, NBody, Hashmap and
object-delete still need further tuning against their historical controls.
Brainfuck is near parity (+1.6% with an interval including parity); the small
formatted-output difference varies across captures. Performance acceptance
remains open; this is not a new complete 88-workload replay.

### Remaining-case tuning (2026-10-15)

This continuation compares private release-MIR builds against the clean merged
checkout `1f25b6c53` and the same historical controls. Evidence lives under
`temp/mvp_perf_remaining_20261015/`; Results3–9 and the archived coverage score
are unchanged.

The retained candidate changes existing routes, adding no helper or per-site
cache (**D8.4.1v2**):

- Numeric consumers test the existing property's scalar storage lane before
  loading pointer metadata. Optional, nonnumeric and legacy pointer cases keep
  shared coercion/readers (**D3.4.5**, **D5.2–D5.3**).
- Shared shape lookup accepts a nonzero matching ordinary `NameId` as a proof
  of equality, retaining byte comparison for absent or differing IDs. Shape
  transitions reuse the pooled record's hash; canonicalization, Symbol/private
  identity and chain ownership checks are unchanged (**D3.4.4v4**,
  **D4.6.1v4**, **D3.4.3v5**).
- The fixed-property dispatcher specializes its existing implementation for
  `Get` and callee reads. The compiler's existing closed-unit proof selects
  that route; reflective units keep general dispatch. Both variants share
  the same source for prototype, descriptor and ownership semantics.

Rejected experiments remain captured: resolved prototype-face reuse, inline
null-container reads, pointer-based spelling shortcuts, planned-name allocation/
lexical setter changes, and specializing the reflective `Get` dispatcher.
The last made Microdiff slower despite improving Splay, so only the fixed
dispatcher specialization proceeds. Scaled sampling workloads are diagnostic
only; acceptance uses original benchmark work and self-timing contracts.

The retained `fixed-reads` release has SHA-256
`2c266e7ac7bddb251a8f6afdf623a18c074a1863b38881ee0c22b2f5d6bd7697`.
All **15/15** benchmark outputs pass in the final seven-round rotating comparison;
all source/binary guards match. No new slowdown versus the clean pre-change
control has a 95% paired interval wholly above parity. NBody improves **5.0%**
and Splay **5.3%**, with both historical intervals including parity.

| Workload | Candidate ms | Pre-change ms | Historical ms | Historical difference |
|---|---:|---:|---:|---:|
| NBody | 44.101 | 46.419 | 44.000 | +0.23% |
| Splay | 61.207 | 64.619 | 61.830 | -1.01% |
| DeltaBlue | 53.423 | 53.610 | 50.624 | +5.53% |
| object-delete | 0.862 | 0.858 | 0.814 | +5.90% |
| Microdiff | 110.337 | 110.208 | 101.780 | +8.41% |
| Hashmap | 489.146 | 489.909 | 465.907 | +4.99% |

The historical control is Result9 except Hashmap/Text Search, which retain the
Phase34-entry control. Monitoring records zero competing build/test observations
and busy background processes in **60/96** observations. Raw intervals and
samples remain in `final-retained/comparison.json`; this is a focused comparison,
not a new 88-workload sweep. Four historical gaps therefore remain open.

Freshly rebuilt release archives pass **665/665** MVP tests, **13/13** focused
forced-GC/poisoning checks, **94/94** shared map-building and **23/23** name-pool
tests. The private CLI passes **1,957/1,957** selected canonical Test262 cases
and **1,716/1,716** strict variants without retries. The full LJS baseline passes
**40,261/40,261**, also without retries; the integrated GC stress sweep passes
**313/313**.

The Lambda aggregate records **7,441/7,445**. Three checks fail through
`test/lambda/pdf/output_pages.ls`; the clean pre-change pin and candidate both
produce the same page-fragment traversal failure in interpreter, JIT and auto
tiers. The fourth is the chart dashboard's existing half-pixel wordcloud golden
mismatch: both pins produce byte-identical output. Their separate replays,
untouched baseline failures and provenance are retained in `validation.json`,
`pdf-control-replay.json`, `chart-control-replay.json` and `baseline-results/`.
No golden, test deadline or Test262 harness was changed.


### Remaining-case ownership and projection tuning (2026-10-15)

This continuation starts at `33c0e9c0e` and retains the prior final executable as
its clean control: all 1,454 production input hashes match. It adds no helper,
container flag, public ABI or per-site cache (**D8.4.1v2**).

- Empty JS arrays reuse Lambda's empty `array()` without allocating an unused
  handoff buffer. Nonempty construction delegates rooting to the existing dense
  reserve and sparse preparation helpers. Object headers stay fixed, each
  collecting helper retains its owner, and publication has no intervening
  safepoint (**D4.3.1**, **D5.3.2–D5.3.3**). Root reservation failure transfers
  control through the existing recovery frame; it cannot continue unrooted.
- Key-only projections do not invoke getters, so they keep their descriptor
  snapshot. Value/entry projections retain live rechecks after callbacks.
  New cases cover array holes, sparse and typed indices, mapped arguments,
  hidden/accessor properties, key ordering and mutation during `Object.values`.
- Shared shaped-field reads resolve `LaneStorageDesc` once and reuse it for
  nullable admission and ordinary decoding (**D3.4.6**).
- The existing reflective property template specializes callee reads; ordinary
  Get and other operations retain general dispatch. Shared in-place deletion
  no longer retains a scalar when it only publishes a new layout (**D3.4.5**).

Receiver-kind reuse and earlier hash-mismatch rejection were measured and
removed for lack of a consistent benefit. Benchmark sources, output contracts,
historical controls and published Results3–9 are unchanged. The final paired release-MIR
capture uses nine measured rounds plus an excluded warmup, rotating candidate,
clean pre-change and historical controls. All **15/15** output/backend contracts
pass; no paired 95% interval shows a new regression against the pre-change pin.

| Workload | Candidate ms | Pre-change ms | Historical ms | vs pre-change | vs historical |
|---|---:|---:|---:|---:|---:|
| Object-delete | 0.836 | 0.867 | 0.827 | -3.6% | +1.1% |
| Hashmap | 479.313 | 491.490 | 469.153 | -2.5% | +2.2% |
| Microdiff | 108.190 | 109.905 | 101.336 | -1.6% | +6.8% |
| DeltaBlue | 52.616 | 53.108 | 50.548 | -0.9% | +4.1% |
| Splay | 57.834 | 59.240 | 59.892 | -2.4% | -3.4% |

Object-delete's historical ratio interval is **0.989–1.024**, consistent with
parity. DeltaBlue's small pre-change improvement is inconclusive. Microdiff,
DeltaBlue and Hashmap retain statistically clear historical gaps; performance
acceptance remains open. Historical controls are Result9 except Hashmap/Text
Search, which use Phase34 entry. Monitoring records **0/117** competing build/test
observations and **17/117** with busy background activity. This focused capture
does not refresh the whole-benchmark results or the whole-population MVP score.

Fresh release archives pass **667/667** MVP tests, **94/94** shared map-building
and **23/23** name-pool tests. Forced GC with freed-memory poisoning passes
**17/17** focused checks every five allocations and **3/3** ownership checks
every allocation. The private CLI passes **1,957/1,957** canonical cases and
**1,716/1,716** strict variants without retries. The full LJS baseline passes
**40,261/40,261** with zero regressions and no isolated retries; the integrated
MIR GC stress sweep passes **313/313**. Lambda's combined aggregate records
**7,443/7,447** (runtime **5,331/5,335**, input **2,112/2,112**). The four
failures retain the same two pre-existing causes: three PDF page-walker checks
and the chart dashboard's half-pixel wordcloud golden mismatch. Fresh control
replays confirm PDF fails identically in interpreter/JIT/auto on both pins and
chart output is byte-identical. No expected output, harness or deadline changed.
Raw samples, confidence intervals, provenance, rejected experiments and commands
are under `temp/mvp_perf_remaining2_20261015/REPORT.md`.
