# Common function, representation, MIR, and frame API

**Status:** Phases 0 through 7 implemented (2026-07-20). Phase 7 retains an
explicit, measured GC fallback for ownerless persistent numeric Items; the
long-term no-standalone-numeric-heap direction remains deferred.

**Scope:** MIR-direct compilation for Lambda and LambdaJS

**Baseline revision:** `09dfbcc9551817278ef6a290c321fb22a740abac`

**Out of scope:** C2MIR, native helper `RootFrame` APIs, and changes to the GC algorithm

Related design records are `Lambda_Design_Stack_Rooting.md`,
`Lambda_Design_Unified_AST.md`, `Lambda_Stack_MIR.md`, and
`Lambda_Stack_JS_MIR.md`. `Lambda_Type_Double_Boxing.md` is normative for the
float `Item` discriminator and packed-zero encoding used by this design.

This document specifies the common compiler API that sits between the
unified AST and MIR-direct code generation. The API has four cooperating parts:

1. common function analysis;
2. common representation-demand analysis;
3. common MIR lowering and `MirEmitter` frame management;
4. common runtime/generated-call metadata.

The main objective is performance. In particular, the design must preserve the
new rooting model's **safepoint-current canonical slots** without recreating
eager boxing, unconditional root publication, or per-instruction root traffic.
Correctness remains mandatory, but the common layer must express correctness in
a way that permits zero-root numeric functions, native scalar lanes, direct
internal calls, and late elimination of unused frame machinery.

The implementation now completes this architectural direction. `MirEmitter`
owns the one physical generated-frame state, call bookkeeping and frame
finalization are shared, and immutable per-variant analysis drives generated
call ABI and scalar-home decisions for both Lambda and LambdaJS.

**Scalar-storage and return decision:** legacy callee-frame donation is
replaced by **activation-owned canonical scalar homes**. Phase 7 realigns the
type set: `INT64` and `UINT64` have no inline form and use number
homes while transient, while `DTIME` becomes always heap-backed. A generated
callee returns a transient numeric `Item` through a **caller-donated canonical
scalar return home**: it copies the one-word payload into the supplied home,
restores its complete number extent, retags the returned `Item`, and returns
normally. Repeated production therefore reuses homes by peak simultaneous
liveness instead of retaining one slot per dynamic call or conversion.

The general direction beyond Phase 7 is that `DOUBLE`, `INT64`, and `UINT64`
have no standalone heap-owned payload representation. Their type tags become a
strong non-GC invariant: known values of those types require neither GC roots
nor collector-side object handling. Phase 7's GC scalar cells for ownerless
persistent boundaries are explicitly transitional.

The hidden return-home ABI exists only between generated implementation bodies.
Public and runtime-indirect entries keep their existing public value ABI and
enter through checked wrappers. This separation is established before the
hidden argument is enabled.

### Implementation result

Phases 0 through 7 are complete. The completed implementation provides:

- C-compatible value/ABI/effect contracts and immutable function variants;
- one normalized import/direct-call path and one conservative raw-call funnel;
- one `MirEmitter` physical-frame owner, prologue, finalizer, verifier, root
  coloring path, and scalar-home coloring/fixup path for both languages;
- checked public wrappers separated from bound generated bodies, with the
  hidden caller-home operand confined to internal direct calls;
- normal/error lane adoption, discard scratch, tail-home validation, wrapper
  heap rehoming, and full callee watermark restoration;
- full-domain `INT64`/`UINT64` transient homes, unsigned caller-home transfer,
  destination-owned scalar storage, and `ushr` lowering for Lambda and
  LambdaJS. The high-`u64` corruption repair landed in this implementation
  round, but its magnitude-based promotion policy was later superseded by the
  type-directed number model and is tracked in `Lambda_Impl_Numbers.md`;
- GC-owned datetime objects with no number-home return or relocation lane;
- a counted `lambda_item_heap_rehome()` fallback only for Item-only numeric
  persistence without a natural destination owner;
- conservative metadata and representation fallback where an edge is not yet
  profitable to specialize, without a second frame or call implementation.

The legacy callee-frame donation path, per-language physical frame mirrors,
write-through/eager differential mode, per-opcode GC event telemetry, stale GC
analysis structures, and obsolete direct-call compatibility helpers have been
deleted.

Final release-generated frame profile:

| Representative implementation frame | Baseline locals | Final locals | Baseline MIR instructions | Final MIR instructions | Change |
|---|---:|---:|---:|---:|---:|
| Lambda `_frame_review_0` | 47 | 43 | 101 | 78 | -22.8% |
| LambdaJS `_js_frameReview_149_body` | 53 | 50 | 136 | 112 | -17.6% |

The new LambdaJS public wrapper is intentionally separate and contains 18
locals and 40 MIR instructions; it is not charged to the implementation-body
comparison. The raw `lambda/`, `lib/`, and `test/` source scope is 7 lines
smaller than the baseline (246,362 to 246,355 lines). Including the updated GC
audit tool, the complete source/test/tool change is still one line smaller.

Final verification:

| Gate | Result |
|---|---|
| `make build-test` | passed |
| `make test-lambda-baseline` | 3,494/3,494 passed (1,389 runtime + 2,105 input baseline) |
| `make test262-baseline` | 40,261/40,261 fully passed; 0 non-fully-passing, 0 failed, 0 regressions, 0.0s retry time |
| `make test-gc-rooting` | JS JIT, randomized forced-GC, JS MIR interpreter, and Lambda MIR Direct passed; metadata/root audits clean |
| release million-iteration probes | Lambda wide returns, Lambda local boxing, and LambdaJS wide returns remained bounded and correct |
| final `make release` | passed after profiling instrumentation was removed |
| legacy-symbol and `git diff --check` audits | clean |

## 1. Executive decision

The common interface is a pipeline, not a single large frame object:

```text
unified AST + language profile + call metadata
                    |
                    v
       immutable FnAnalysis / FnVariantAnalysis
                    |
                    v
       immutable representation-demand plan
                    |
                    v
               MirFunctionPlan
                    |
                    v
       stateful MirEmitter / MirFrameState
                    |
                    v
                   MIR
```

The ownership rules are:

- `FnAnalysis` describes semantic facts about a source function.
- `FnVariantAnalysis` describes one callable implementation of that function,
  such as a public wrapper, internal boxed body, native direct body, or resume
  entry.
- representation-demand analysis decides which value representation is needed
  at each producer, binding, edge, call boundary, and return boundary.
- common call metadata describes the ABI value classes and observable effects
  of runtime helpers, system functions, and generated direct callees.
- `MirEmitter` is the sole owner of mutable MIR function/frame state, including
  root candidates, call safepoints, number-stack state, epilogue state, and
  late root-slot assignment.
- language lowering owns language semantics. JavaScript completion routing,
  TDZ, `eval`, `with`, `this`, `new.target`, generators, and async state
  machines do not become generic frame mechanisms.

This gives Lambda and LambdaJS one physical generated-frame dialect while
retaining separate semantic lowering where the languages actually differ.

## 2. Goals and non-goals

### 2.1 Goals

The API should:

- analyze common function facts once and expose immutable queryable results;
- distinguish semantic type from physical representation;
- box or unbox only at a consumer boundary that demands it;
- derive GC treatment from semantic value class, not just MIR register type;
- make every call pass through one effect-aware emission path;
- emit one canonical root/number-frame prologue and epilogue;
- keep canonical root slots current at safepoints, not after every instruction;
- support checked external entries and proven-bound internal entries;
- eliminate root-frame machinery when a function has no rootable live value;
- retain scalar returns across number-stack restoration without forced boxing;
- bound activation scalar storage by peak simultaneously live values rather
  than dynamic call or conversion count;
- preserve public and runtime-indirect function ABIs while allowing an internal
  generated ABI to carry hidden frame arguments;
- preserve tail-call optimization by forwarding the original caller-owned
  return home on a true tail edge;
- produce diagnostics and MIR-count statistics from the common layer;
- allow Lambda and LambdaJS to migrate independently behind compatibility
  adapters.

### 2.2 Non-goals

This proposal does not:

- make Lambda and JavaScript share coercion, truthiness, error, or completion
  semantics;
- move JavaScript catch/finally/generator/async routing into `MirEmitter`;
- replace native C/C++ exact handle scopes;
- redesign general persistent or async ownership outside the scalar-storage
  realignment in Phase 7;
- change non-local-unwind restoration at runtime entry boundaries;
- introduce a new cross-function tail-call optimization; the API only defines
  ownership when an existing or future lowering emits a physical tail edge;
- redesign the GC allocator or collector; Phase 7 may change which scalar
  payloads use it, but not how it operates;
- change C2MIR. C2MIR remains a compatibility path and is left as-is.

## 3. Current implementation and the missing boundary

| Area | Shared today | Still duplicated or incomplete |
|---|---|---|
| Function analysis | unified AST has `AstFuncNode::analysis`; `FnAnalysis` carries captures, parameter evidence, and async facts | entry variants, call effects, return representation, binding storage, and per-value representation demand are not common analysis products |
| Representation | shared scalar-return modes and semantic GC value classes exist | most representation decisions are made while lowering; eager boxing and language-specific holder/version locals remain possible; current callee donation can retain one slot per newly returned wide scalar call, while repeated cold local boxing can also grow the temporary number extent |
| MIR emitter | MIR construction, import caching, semantic root events, common root finalization, and scalar-return rehoming are shared | Lambda and JS own separate frame arrays, frame enter/exit logic, epilogue state, and call wrappers |
| Metadata | `JitImportMetadata` records GC effect, re-entry effect, return class, and packed argument classes | exception effect and ABI representation are implicit; direct generated calls and runtime imports do not yet use one normalized call descriptor |
| Rooting | common finalizer can assign slots and insert writes from semantic events | both transpilers collect and feed those events through their own mutable frame implementations |

The common rooting finalizer should be treated as the first implementation of
the new API, not as its endpoint. The final architecture must remove the two
per-language frame dialects around it.

## 4. Layer ownership

The design deliberately separates facts that are often conflated:

| Fact | Owner | Mutable during MIR emission? |
|---|---|---:|
| source binding, capture, parameter evidence | common function analysis | no |
| language-specific dynamic-scope or completion requirement | language profile analysis | no |
| actual representation and consumer demands | representation-demand analysis | no |
| helper ABI and effects | common metadata registry | no |
| MIR register, label, root home, liveness, inserted write | `MirEmitter` | yes |
| activation scalar-home candidate, liveness, and colored number slot | representation analysis / `MirEmitter` | analysis facts no; physical slot yes |
| catch target, finally completion, generator state | language lowering | yes |

Two boundaries are especially important:

1. `FnAnalysis` must never contain `MIR_reg_t`, MIR labels, instruction
   pointers, or assigned root slots. That would make an AST analysis result
   depend on one lowering attempt.
2. `MirEmitter` must not infer JavaScript semantics from node kinds. It consumes
   an explicit plan and values annotated with semantic representation classes.

## 5. Common function analysis

### 5.1 Analysis unit

`FnAnalysis` remains attached to `AstFuncNode`, but becomes the immutable owner
of all language-neutral function facts. A source function may have multiple
callable variants, so entry-specific facts belong to `FnVariantAnalysis`.

Illustrative API types follow. Names and packing may change during
implementation; the ownership boundary should not.

```c
typedef enum FnEntryKind {
    FN_ENTRY_PUBLIC_WRAPPER,
    FN_ENTRY_BOXED_BODY,
    FN_ENTRY_NATIVE_BODY,
    FN_ENTRY_RESUME
} FnEntryKind;

typedef enum FnErrorLane {
    FN_ERROR_LANE_NONE,
    FN_ERROR_LANE_CONTEXT_ITEM
} FnErrorLane;

typedef enum ScalarReturnClass {
    SCALAR_RETURN_NONE,
    SCALAR_RETURN_I64,
    SCALAR_RETURN_U64,
    SCALAR_RETURN_F64,
    SCALAR_RETURN_DYNAMIC
} ScalarReturnClass;

typedef struct FnEffectSummary {
    bool may_gc;
    bool may_reenter;
    bool may_set_exception;
    bool may_return_error;
    bool may_suspend;
    bool has_unknown_call;
} FnEffectSummary;

typedef struct FnEntryAnalysis {
    FnEntryKind kind;
    bool can_use_bound_context;
    bool has_dynamic_scope;
    bool requires_arguments_object;
    bool is_external_entry;
} FnEntryAnalysis;

typedef struct FnReturnLaneAnalysis {
    TypeId semantic_type;
    ValueRep abi_rep;
    ScalarReturnClass scalar_class;
    bool may_need_caller_scalar_home;
} FnReturnLaneAnalysis;

enum {
    FN_RETURN_HOME_NORMAL = 1u << 0,
    FN_RETURN_HOME_ERROR = 1u << 1,
};

typedef struct FnReturnAnalysis {
    FnReturnLaneAnalysis normal;
    FnReturnLaneAnalysis error;
    FnErrorLane error_lane;
    uint8_t scalar_home_lane_mask;
} FnReturnAnalysis;

typedef struct FnVariantAnalysis {
    FnEntryAnalysis entry;
    FnEffectSummary effects;
    FnReturnAnalysis result;
    FnParamAnalysis* params;
    int param_count;
    FnBindingAnalysis* bindings;
    int binding_count;
    FnValueAnalysis* values;
    int value_count;
} FnVariantAnalysis;
```

The existing capture, parameter-evidence, and async fields remain valid inputs.
They should be folded into, rather than copied beside, the expanded model.

`scalar_home_lane_mask` is the union of all mutually exclusive return lanes that
may carry a transient `INT64`, transient `UINT64`, or out-of-band `FLOAT`
payload. One hidden home is sufficient for the normal/error contract because
only one lane is live on an exit. A function with a native normal return can
still need the hidden home when its Lambda error lane is a boxed `Item`. Native
scalar returns with no boxed error lane and proven-safe `Item` returns do not
carry the hidden parameter. Phase 7 adds `UINT64` to this set and removes
`DTIME`; datetime results are already persistent heap Items and require no
number home. The shipped Phase 0-through-6 enum still has `SCALAR_RETURN_DTIME`
and lacks `SCALAR_RETURN_U64` until that phase lands.

### 5.2 Required analysis sequence

The common driver should run these stages in order:

1. binding and capture discovery;
2. language-profile semantic facts, such as dynamic-scope or arguments-object
   requirements;
3. initial type and parameter evidence;
4. conservative call-graph and callable-variant discovery;
5. a joint strongly-connected-component fixed point over parameter and return
   types, representation demand, callable-variant selection, and effects;
6. entry, return, and physical frame-plan derivation from the converged result.

Effects have two layers. Semantic/profile effects describe source operations
independent of physical representation. Lowering effects add calls introduced
by boxing, coercion, heap rehoming, wrappers, and other selected ABI mechanics.
Only the converged union may be published as `FnVariantAnalysis::effects` or
used to classify a generated direct call. Unknown, indirect, or cross-module
calls remain conservative until metadata proves otherwise. This prevents a
native-looking caller from treating a callee as `NO_GC` before representation
lowering has introduced an allocating conversion.

The correctness fixed point is monotone: summaries may only widen toward boxed
representation, unknown class/effect, persistent ownership, or `MAY_GC` until
they stabilize. Profitability choices run after correctness convergence and may
decline a specialization, but may not make a summary less conservative and
restart an oscillating specialize/de-specialize cycle.

### 5.3 Function variants

At minimum, analysis must distinguish:

- a public checked wrapper using the existing externally callable value ABI;
- an internal boxed body using `Item` source arguments plus any hidden generated
  frame arguments, including the scalar return home;
- a native direct body used only by verified generated callers;
- a resume entry when a language profile lowers suspension to a state machine.

The wrapper and implementation bodies share source analysis but have different
ABI, reachability, effects, and frame plans. For example, a JavaScript
`JsFunction::func_ptr` points to the public wrapper and remains callable through
the existing `Item(Item...)` runtime dispatcher. The wrapper allocates a local
return home when necessary, calls the internal boxed body, heap-rehomes any
value that escapes the wrapper activation, and only then restores its frame.
Generated direct calls may target the boxed or native body and may use the
hidden generated ABI.

Return-home demand is variant-specific. An internal body publishes the lane
mask described above; after adopting and heap-rehoming, its public wrapper
publishes a zero mask and no scalar-home operand because every outward `Item`
is inline or heap-owned.

A cross-module edge may target an internal body only when the producing module
publishes the exact immutable call contract and the consumer verifies it.
Otherwise it targets the public wrapper. Resume/scheduler entries similarly use
checked public/persistent ownership unless a synchronous generated edge proves
the internal resume contract; suspension never carries an activation home.

`FnEntryKind` describes ABI/reachability; `MirEntryMode` from section 8.3
describes context-binding proof. They are orthogonal. A bound implementation
body may skip binding `Context` again, but it may not skip per-activation root
or number-stack reservation. Only callers proved to have a valid bound context
may reference that entry. Public wrappers are always checked entries.

### 5.4 Language-profile contribution

The common analyzer should provide traversal, dataflow, call-graph, and storage
machinery. `LangProfile` contributes narrowly scoped semantic policy:

- seed representation demand for language-specific operators;
- classify language coercions separately from physical conversions;
- report dynamic-scope, TDZ, arguments, completion, or suspension facts;
- select legal entry variants and return/error lanes;
- finalize profile-specific effect facts.

The profile must not allocate root slots or emit MIR. Conversely, the common
analyzer must not grow branches for JS-specific AST behavior merely to appear
unified.

Profile-only analysis data should remain in a profile-owned extension or side
table, reachable through the existing function-extension mechanism. A fact is
promoted into `FnAnalysis` only when both backends consume it with the same
meaning. This prevents the shared structure from becoming a renamed copy of
`JsFuncCollected`.

## 6. Common representation-demand analysis

### 6.1 Why representation is separate from type

A semantic number may be represented as an `Item`, `I64`, or `F64`. A pointer-
typed MIR register may be a GC-managed object, a non-GC runtime pointer, or an
opaque value. Therefore neither `TypeId` nor `MIR_T_P` alone is enough to decide
boxing, ABI, or rooting.

The common representation vocabulary should be small and language-neutral:

```c
typedef enum ValueRep {
    VALUE_REP_NONE,
    VALUE_REP_ITEM,
    VALUE_REP_I64,
    VALUE_REP_F64,
    VALUE_REP_RAW_GC_POINTER,
    VALUE_REP_RAW_NON_GC_POINTER
} ValueRep;

typedef enum BindingStorage {
    BINDING_STORAGE_REGISTER,
    BINDING_STORAGE_SCOPE_ENV,
    BINDING_STORAGE_MODULE,
    BINDING_STORAGE_PERSISTENT
} BindingStorage;

typedef struct FnValueAnalysis {
    AstNode* producer;
    TypeId semantic_type;
    ValueRep actual_rep;
    uint32_t demand_mask;
    bool is_exact_constant;
    uint64_t constant_bits;
} FnValueAnalysis;

typedef struct FnBindingAnalysis {
    NameEntry* name;
    TypeId semantic_type;
    ValueRep canonical_rep;
    JitValueClass value_class;
    BindingStorage storage;
    uint32_t escape_flags;
} FnBindingAnalysis;
```

`demand_mask` represents the accepted representations of consumers. It is not
a root-liveness mask. Root homes and liveness belong only to MIR lowering.
The representation plan also contains definition and edge identities for
binding versions and control-flow joins; an `AstNode*` lookup is a convenience
index, not the identity of every dynamic definition. This is required for
assignments, loop-carried values, and joins whose incoming values have distinct
scalar-home provenance.

### 6.2 Demand sources

Demand is seeded from:

- operator inputs and results;
- branches and language-profile truthiness operations;
- runtime call metadata for each parameter and return;
- container, environment, module, and global stores;
- capture and escape boundaries;
- public-wrapper, internal-boxed, and native-direct call ABIs;
- return boundaries;
- `eval`, `with`, reflection, or other dynamic access reported by a profile.

Demand then propagates backward through definitions, phi-like joins, and
binding versions. A join chooses one canonical representation or inserts exact
edge conversions when retaining multiple incoming representations is cheaper.

### 6.3 Canonical binding rules

The default decisions are:

- keep a binding native when all meaningful consumers accept the same native
  representation;
- use `Item` when a binding must live in a scope environment, module slot,
  persistent container, dynamic-eval-visible location, or public value ABI;
- for mixed native and boxed consumers, retain the native canonical value and
  box at boxed consumers when lifetime and reuse make that cheaper;
- choose a boxed canonical value only when boxed consumers, escape, or lifetime
  dominate;
- pre-box exact constants when the representation is stable and no allocation
  is needed at runtime;
- invalidate cached conversions on assignment;
- never assume a temporary number-stack box remains valid across restoration.

This removes the need for source-binding version locals whose only purpose is
to compensate for eager or repeated representation changes. Versioning should
remain only when source semantics or control-flow merging requires it.

### 6.4 Conversion versus coercion

The API must keep these operations distinct:

- **representation conversion:** `F64` to an exactly equivalent numeric `Item`;
- **language coercion:** JavaScript `ToNumber`, Lambda type conversion, string
  concatenation, or another semantic operation.

Only exact representation conversions belong in common `MirEmitter` helpers.
Language coercions remain profile/runtime operations with normal call metadata.

Suggested query interface:

```c
const FnValueAnalysis* fn_value_analysis(const FnVariantAnalysis* fn, AstNode* node);
const FnBindingAnalysis* fn_binding_analysis(const FnVariantAnalysis* fn, NameEntry* name);
ValueRep fn_consumer_rep(const FnVariantAnalysis* fn, AstNode* consumer, int operand);
bool fn_edge_needs_rep_conversion(const FnVariantAnalysis* fn, AstNode* from, AstNode* to);
```

### 6.5 Activation scalar-home demand and coloring

Representation analysis determines whether any boxed value may contain an
activation-owned number-stack scalar. This applies to generated-call results,
runtime-import results, exact representation conversions, control-flow joins,
and normal or error return lanes. It records a logical home requirement, not a
physical slot number. MIR lowering later computes exact lifetimes and colors
non-overlapping values onto the same physical home.

```c
typedef enum ScalarHomeDemand {
    SCALAR_HOME_NONE,
    SCALAR_HOME_ACTIVATION_ITEM
} ScalarHomeDemand;

/* added to FnValueAnalysis from §6.1 */
ScalarHomeDemand scalar_home_demand;
```

The coloring target is the maximum number of simultaneously live
pointer-backed scalar `Item`s, not the number of calls or assignments:

```text
r = f()          home 0
consume(r)
r = f()          home 0 reused

a = f()          home 0
b = g()          home 1, because a remains live
consume(a, b)
```

Liveness is computed on the finalized MIR control-flow graph, including loop
backedges, exceptional/profile exits, and edge adoptions. Lexical AST order or a
linear event sequence is insufficient for coloring.

The home decision follows payload provenance rather than the source operation.
An inline scalar or heap-owned scalar needs no activation home. A dynamic
`Item` with unknown scalar provenance is classified once and adopted into its
assigned home before any temporary watermark is restored. `em_require_rep()`
must create and record this logical home when lowering introduces a conversion
that did not exist as an AST producer.

At an escape boundary, the value leaves activation storage:

- environment, module, persistent container, or async/generator state: copy to
  storage owned by that destination, or use the section 15 fallback when the
  destination has no declared scalar owner;
- return to another generated activation: copy to the caller-provided scalar
  return home;
- discarded value: no persistent home is required;
- native direct return: no boxed home is required.

When every lane that may carry a pointer-backed scalar is semantically
discarded, a generated call uses one shared fixed discard scratch home owned by
the caller activation. The home need not participate in ordinary value liveness
because no returned `Item` is observed, but it remains non-null so the callee
never constructs a dangling return value. If, for example, the normal value is
discarded but a Lambda scalar error lane remains observable, the call uses a
colored home with the error value's real lifetime instead. The scratch slot is
reserved only in functions that contain a fully discarded scalar call and is
never shared with a live value home.

This is the number-stack analogue of root-slot coloring, but the two classes
must remain distinct. Scalar homes contain raw one-word numeric payloads and
are never GC roots.

## 7. Common call metadata

### 7.1 Normalized descriptor

The current metadata is a sound base, but GC value class and MIR ABI
representation must be explicit and separate. Exception behavior must also be
separate from GC behavior.

```c
typedef enum JitExceptionEffect {
    JIT_EXCEPTION_MAY_SET,
    JIT_EXCEPTION_PRESERVES,
    JIT_EXCEPTION_CLEARS
} JitExceptionEffect;

typedef enum JitAbiRep {
    JIT_ABI_VOID,
    JIT_ABI_ITEM,
    JIT_ABI_I64,
    JIT_ABI_F64,
    JIT_ABI_POINTER
} JitAbiRep;

typedef enum JitNumberStackEffect {
    JIT_NUMBER_STACK_PRESERVES,
    JIT_NUMBER_STACK_MAY_ALLOCATE
} JitNumberStackEffect;

typedef struct JitAbiValue {
    JitAbiRep abi_rep;
    JitValueClass value_class;
} JitAbiValue;

typedef enum JitArgEffect {
    JIT_ARG_BORROWED = 0,
    JIT_ARG_MAY_CAPTURE = 1u << 0,
    JIT_ARG_MAY_WRITE_THROUGH = 1u << 1,
    JIT_ARG_PERSISTENT_STORE = 1u << 2,
    JIT_ARG_EFFECT_UNKNOWN = 1u << 3
} JitArgEffect;

typedef struct JitAbiArg {
    JitAbiValue value;
    uint16_t effects;
} JitAbiArg;

typedef enum JitReturnTransport {
    JIT_RETURN_NONE,
    JIT_RETURN_MIR_RESULT,
    JIT_RETURN_CONTEXT_ERROR
} JitReturnTransport;

typedef struct JitReturnLane {
    JitAbiValue value;
    JitReturnTransport transport;
    ScalarReturnClass scalar_class;
    bool may_use_scalar_return_home;
} JitReturnLane;

typedef struct JitCallEffects {
    JitGcEffect gc;
    JitReentryEffect reentry;
    JitExceptionEffect exception;
    JitNumberStackEffect number_stack;
} JitCallEffects;

typedef struct JitCallMetadata {
    JitCallEffects effects;
    JitReturnLane normal_result;
    JitReturnLane error_result;
    const JitAbiArg* abi_args;
    uint16_t abi_arg_count;
    uint16_t source_arg_count;
    int16_t scalar_return_home_arg_index;
    uint8_t scalar_home_lane_mask;
    uint32_t flags;
} JitCallMetadata;
```

`JIT_EXCEPTION_CLEARS` means guaranteed clear on return and cannot also set an
exception. A helper that conditionally clears, conditionally sets, or has mixed
paths is classified `MAY_SET`; this may lose an optimization but cannot suppress
a required poll. Generated normal control flow does not invoke ordinary helpers
while an unhandled exception is active.

The public implementation may retain packed argument classes and an inline fast
path for the current small-call limit. The normalized descriptor must not bake
that limit into generated-function metadata: direct source functions can have
more parameters, so the descriptor uses a pointer and count. `abi_args` lists
every physical MIR call operand, including environment, receiver, variadic,
resume-state, and scalar-home operands; `source_arg_count` retains the
language-visible arity. The form above is the interface consumed by analysis
and `MirEmitter`.

`normal_result` and `error_result` describe distinct, mutually exclusive
language-visible lanes. JavaScript exception state remains an effect, not a
second return lane. The current Lambda error lane uses
`JIT_RETURN_CONTEXT_ERROR`; a native normal result therefore still carries an
explicit boxed error contract. `scalar_home_lane_mask` is the union of lanes
that may use the one shared home. The context-error protocol writes zero on a
normal exit and the boxed error Item on an error exit; the caller reads and
tests it immediately after the call before emitting any operation that could
overwrite the shared lane.

`scalar_return_home_arg_index` identifies the hidden generated-call ABI operand
and is `-1` when absent. It is not counted among source-language parameters and
is never present on a public wrapper ABI. The canonical generated-body order is
existing hidden environment/receiver operands, source operands, existing
variadic or resume-state operands, then the scalar home as the final operand.
Static/runtime imports that do not implement this generated ABI keep the index
at `-1`; any returned `Item` with unknown scalar provenance must be adopted by
the common call emitter before a temporary watermark is restored or the value
crosses a generated frame boundary.

Argument effects are part of representation correctness, not only
optimization. `JIT_ARG_BORROWED` proves that a native or activation-owned value
does not escape the call. Persistent capture/store requires a persistent owner:
the GC heap in Phases 0 through 6, and destination-owned storage or the selected
fallback under Phase 7. Unknown arguments conservatively combine capture,
write-through, and persistent-store behavior until audited.

### 7.2 Metadata sources

One resolver should normalize metadata from:

- static runtime helper declarations;
- `SysFuncInfo` system-function entries;
- direct generated callees, synthesized from `FnVariantAnalysis`;
- indirect or unresolved callees.

Indirect and unresolved calls default to:

- `MAY_GC`;
- may re-enter generated/runtime code;
- may set the active language's exception/error state;
- may allocate activation-temporary number payloads;
- the public boxed value ABI when the call is dynamically callable;
- unknown semantic argument and return classes;
- unknown argument effects, including possible persistent capture.

The machine ABI itself may never be guessed: an unresolved call without a known
public signature is a compile-time error or must use a profile-provided runtime
dispatcher. This fail-closed default is intentional. Optimization requires
positive proof.

### 7.3 Metadata invariants

- `NO_GC` helpers must also be proven non-reentrant with respect to allocating
  code.
- allocation and coercion helpers that can allocate are always `MAY_GC`.
- a helper with a no-GC fast path and allocating slow path is `MAY_GC` unless
  the paths are exposed as separately audited imports.
- declared ABI representation must match the emitted MIR call signature.
- a boxed `Item`, raw GC pointer, and raw non-GC pointer are never interchangeable
  merely because all use a machine word.
- argument capture/write effects must be conservative before representation
  demand chooses activation or persistent ownership.
- exception effect does not imply GC effect, and GC effect does not imply
  exception effect.
- an import classified `JIT_NUMBER_STACK_MAY_ALLOCATE` produces only
  activation-temporary number payloads; any value it stores persistently must
  already be copied into declared destination-owned storage or the selected
  persistent fallback.
- an import may expose an activation-owned scalar only through a declared return
  lane. An `Item` written through an argument must have persistent ownership
  before the import returns unless a future metadata extension declares that
  out-result and gives the emitter an adoption point.
- the common emitter records effects; language lowering decides how an error or
  exception is observed and routed.
- a `CONTEXT_ERROR` item that may carry an activation scalar is adopted into
  the caller home before the callee restores its number extent or publishes the
  context lane.

The build should report unknown or conservative imports, ranked by emitted call
count. This makes metadata audit an evidence-driven optimization process.

### 7.4 Generated-function and finalized-frame metadata

“Common metadata” includes more than the runtime import registry, but the
different lifetimes must not be collapsed into one mutable structure:

1. `FnAnalysis` and `FnVariantAnalysis` are immutable semantic metadata.
2. `JitCallMetadata` is the normalized ABI/effect contract used at call sites.
3. finalized frame metadata is an output of MIR emission used for verification,
   diagnostics, and profiling.

For a generated callee, the call descriptor is synthesized from its immutable
variant analysis before its MIR body is necessarily emitted. After finalization,
the emitter may publish a compact result such as:

```c
typedef struct MirFunctionMetadata {
    const char* debug_name;
    FnEntryKind entry_kind;
    bool bound_context_entry;
    JitCallMetadata call;
    uint16_t root_slot_count;
    uint16_t scalar_home_count;
    uint16_t temporary_number_slot_count;
    uint32_t mir_instruction_count;
    uint32_t safepoint_count;
    uint32_t root_store_count;
} MirFunctionMetadata;
```

This finalized record must not feed back into semantic analysis. It is suitable
for the generated-module verifier, MIR profiling reports, and assertions that a
bound body or zero-root function has the expected physical shape. It need not
be retained in production runtime memory unless an actual runtime consumer is
identified.

There should be no Lambda-only and JS-only copies of these schemas. A profile
may attach opaque debug data, but ABI, effects, entry mode, slot counts, and
emission statistics are common fields with common meanings.

## 8. Common MIR lowering and `MirEmitter`

### 8.1 Shared lowering driver versus language lowering

The common layer should provide a small lowering context and shared structural
operations. It should not attempt to replace both language transpilers with one
node-kind switch.

```c
typedef struct MirLoweringContext {
    MirEmitter* emitter;
    const FnVariantAnalysis* analysis;
    const LangProfile* profile;
    void* profile_state;
} MirLoweringContext;
```

Common lowering owns:

- binding definition, lookup, assignment, and representation transitions;
- argument preparation and metadata-aware call emission;
- common structural control flow where unified AST semantics are identical;
- direct-call variant selection;
- return-value preparation and routing to the physical epilogue;
- conversion caching and invalidation;
- communication with frame/root liveness tracking.

Profile lowering owns:

- language operators whose semantics differ;
- coercion and truthiness;
- property and environment semantics;
- JS exception/completion, TDZ, `eval`, `with`, class, generator, and async
  behavior;
- Lambda error-lane propagation and other Lambda-only semantics.

Both sides return or consume `MirValue` and use the same `MirEmitter`. A profile
handler must not reach around the emitter to create its own call safepoint or
root store. If a node family needs three near-identical profile handlers, its
shared structural shape should be extracted into common lowering with narrow
semantic callbacks.

### 8.2 Semantic MIR value

Language lowering should exchange semantic MIR values instead of naked
registers wherever a value can survive, cross a call, or change representation.

```c
typedef enum ScalarPayloadProvenance {
    SCALAR_PROVENANCE_NONE,
    SCALAR_PROVENANCE_INLINE,
    SCALAR_PROVENANCE_HEAP,
    SCALAR_PROVENANCE_ACTIVATION_HOME,
    SCALAR_PROVENANCE_UNKNOWN
} ScalarPayloadProvenance;

typedef struct MirValue {
    MIR_reg_t reg;
    MIR_type_t mir_type;
    TypeId semantic_type;
    ValueRep rep;
    JitValueClass value_class;
    int gc_home_id;
    int scalar_home_id;
    ScalarPayloadProvenance scalar_provenance;
} MirValue;
```

`gc_home_id == 0` means that no stable GC home is currently required;
`scalar_home_id == 0` means that no fixed number payload home is required. The
namespaces and slot arrays are separate. A home ID is lowering state, not
function-analysis state. Short local arithmetic helpers may still accept naked
MIR registers when their representation is unambiguous.

Copies retain scalar provenance and home identity. A control-flow join either
uses one logical home for every incoming edge or inserts an exact edge adoption;
it never silently merges Items that point into different temporary extents.
An unknown/dynamic value is classified and adopted at the first boundary that
requires a stable activation home.

### 8.3 Immutable function plan

Analysis is converted into a compact plan before MIR emission:

```c
typedef enum MirEntryMode {
    MIR_ENTRY_CHECKED,
    MIR_ENTRY_BOUND_INTERNAL
} MirEntryMode;

typedef struct MirReturnLanePlan {
    MIR_type_t mir_type;
    ValueRep rep;
    ScalarReturnClass scalar_class;
    bool may_use_caller_scalar_home;
} MirReturnLanePlan;

typedef struct MirReturnPlan {
    MirReturnLanePlan normal;
    MirReturnLanePlan error;
    FnErrorLane error_lane;
    uint8_t scalar_home_lane_mask;
    bool accepts_caller_scalar_home;
} MirReturnPlan;

typedef struct MirFunctionPlan {
    MirEntryMode entry_mode;
    MirReturnPlan result;
    int fixed_number_scratch_slots;
    bool color_scalar_homes;
    bool allow_zero_root_elision;
    bool has_dynamic_scope;
    const char* debug_name;
} MirFunctionPlan;
```

The plan contains policy results, not mutable counters or MIR objects.
`fixed_number_scratch_slots` covers ABI/error scratch requirements known before
lowering. Colored scalar-home count is finalized from MIR liveness and inserted
into the prologue later, like root-slot count.

Entry kind is carried beside this plan from `FnVariantAnalysis`. A public
wrapper is always `MIR_ENTRY_CHECKED` and never accepts a caller home. Internal
boxed/native bodies may be checked or bound according to call-edge proof.

### 8.4 Mutable frame state

`MirEmitter` owns a `MirFrameState` for the current function. It replaces the
separate Lambda and JS `side_frame_*` state and owns:

- runtime, root-stack, and number-stack base registers;
- frame anchor and checked-entry insertion point;
- common epilogue label and return registers;
- root candidates and stable homes;
- scalar-home candidates, lifetimes, coloring, and the incoming caller-home
  parameter;
- scalar-home address fixups and the optional discard scratch home;
- definition, use, and temporary-lifetime events;
- call sites and safepoints;
- environment write-back actions;
- scalar-return adoption state;
- assigned root slots and late-inserted stores;
- per-function MIR/root/call statistics.

Nested generated functions use nested emitter/function contexts rather than
overwriting one global frame state.

### 8.5 Proposed lifecycle API

> **Implementation note (2026-07-20):** the shipped surface kept granular
> primitives (`em_frame_dispose/suspend/restore`, `em_finalize_frame_prologue`,
> `em_finalize_scalar_homes`, `em_finalize_function_metadata`, root finalizers)
> composed per language (`jm_begin/finish_function_frame` for JS; Lambda's
> `begin_function_epilogue`/`finalize_side_root_frame`); the monolithic
> `em_function_begin`/`em_function_finish` names below were not introduced.

```c
bool em_function_begin(MirEmitter* em, const MirFunctionPlan* plan);

int em_gc_home_new(MirEmitter* em, JitValueClass value_class);
MirValue em_define_binding(MirEmitter* em, int gc_home_id, MirValue value);
void em_note_use(MirEmitter* em, MirValue value);
void em_note_temp_end(MirEmitter* em, MirValue value);

typedef enum MirFrameRefKind {
    MIR_FRAME_REF_NONE,
    MIR_FRAME_REF_LOCAL_SCALAR_HOME,
    MIR_FRAME_REF_DISCARD_SCRATCH,
    MIR_FRAME_REF_INCOMING_CALLER_HOME
} MirFrameRefKind;

typedef struct MirFrameRef {
    MirFrameRefKind kind;
    int logical_home_id;
} MirFrameRef;

int em_scalar_home_new(MirEmitter* em);
void em_note_scalar_home_use(MirEmitter* em, int scalar_home_id);
MirFrameRef em_scalar_home_ref(MirEmitter* em, int scalar_home_id);

MirValue em_require_rep(MirEmitter* em, MirValue value, ValueRep required);

typedef struct MirCallOptions {
    MirFrameRef scalar_return_home;
    uint8_t observed_return_lane_mask;
    bool is_tail_call;
} MirCallOptions;

MirCallResult em_call_import(
    MirEmitter* em, const JitImport* import,
    const MirValue* args, int arg_count, const MirCallOptions* options);
MirCallResult em_call_direct(
    MirEmitter* em, const FnVariantAnalysis* callee,
    const MirValue* args, int arg_count, const MirCallOptions* options);

void em_emit_return(MirEmitter* em, MirValue value, MirValue error_value);
void em_begin_epilogue(MirEmitter* em);
void em_function_finish(MirEmitter* em);
```

The exact signatures can use existing project containers and result structs.
The important rule is that language code cannot emit a call that bypasses the
metadata lookup and frame event recording.

`MirCallOptions::scalar_return_home` is an opaque reference to a home in the
caller's activation, not an early physical slot number. A direct generated call
passes its finalized address through the hidden ABI. If
`observed_return_lane_mask` has no intersection with the callee's scalar lane
mask, the emitter uses its shared discard scratch reference. A true tail call
forwards the current function's incoming caller-home reference; it must not pass
a home from the activation that the tail edge is about to remove.

For an observed scalar lane, lowering supplies a local logical home or, on a
proven tail edge, the incoming home. When every scalar lane is discarded, the
observed mask selects the discard scratch and the explicit reference must be
`NONE`. Other combinations are verifier errors.

During ordinary lowering, `em_scalar_home_ref()` creates an address-producing
MIR instruction with a placeholder displacement and records a
`MirScalarHomeFixup`. `em_function_finish()` colors logical homes, patches every
recorded displacement, inserts the final fixed-prefix reservation, and verifies
that no unpatched scalar reference remains. MIR operands are mutable before
module finalization in the current backend; if that changes, the equivalent
implementation is a late-inserted address definition consumed by the already
emitted call. Assigning one physical slot per logical ID is not an acceptable
fallback because it violates the liveness-bounded contract.

A static/runtime import keeps its declared ABI; when its result may be a
pointer-backed scalar, the emitter adopts the returned payload into that same
home immediately after the call. For an import classified
`JIT_NUMBER_STACK_MAY_ALLOCATE`, the emitter snapshots the pre-call number
watermark, adopts the result, and restores that watermark. Internal helper
temporaries therefore cannot accumulate across loop iterations.

`em_call_import()` and `em_call_direct()` perform the same common mechanical
work after resolving their metadata source:

1. validate actual ABI representations against metadata;
2. enforce argument ownership, including persistently rehoming activation
   scalar Items that may be captured or stored beyond the call;
3. record arguments as live through the call and end only the temporaries whose
   metadata permits it;
4. record the call as a safepoint when it may GC;
5. emit the call;
6. read the normal and error transports before either can be overwritten;
7. adopt a possible pointer-backed scalar from the active result lane into the
   selected caller home;
8. record the semantic return value and its definition;
9. return effect and lane information to language lowering.

It does **not** emit a JavaScript exception poll or Lambda error propagation.
The relevant profile consumes the returned exception/error effect and emits its
own semantic control flow.

### 8.6 Function lifecycle

The canonical lowering sequence is:

1. `em_function_begin()` creates the anchor, bases, return plan, and initially
   empty root/number frame.
2. Language lowering emits the body through common value and call APIs.
3. Every semantic return routes to the common epilogue through
   `em_emit_return()`.
4. `em_begin_epilogue()` freezes ordinary body lowering while roots are still
   valid.
5. Profile-specific cleanup that may call or allocate is emitted.
6. Environment values that outlive the frame are rehomed or ownership is
   transferred.
7. A pointer-backed scalar from the active normal or error return lane is copied
   into the caller-provided canonical home and retagged when the return plan
   requires it. A context error lane is published only after adoption.
8. The complete callee number-stack watermark is restored; no callee slot is
   retained by the return.
9. The root-stack watermark is restored.
10. The common return is emitted.
11. `em_function_finish()` computes GC-root and scalar-home liveness, colors
    their separate slot spaces, inserts only required safepoint-current root
    writes, and materializes or removes the common checked/bound frame
    prologue.

All cleanup that can allocate must happen before root restoration. No language
epilogue may append a `MAY_GC` call after `em_function_finish()` begins final
teardown.

Tail-call lowering is part of this lifecycle. A tail edge may forward the
incoming caller home only when its result lanes and error propagation exactly
match the current function's outward contract. Self-tail recursion lowered as
a loop keeps the current activation and defers writing the incoming home until
the final exit. A physical cross-function tail edge must first perform all
profile cleanup, persistently rehome or reject every argument that points into
the outgoing activation, and restore that activation's root and number
watermarks; only then may it jump while forwarding the original incoming home.
Reusing the
outgoing fixed prefix for a callee while arguments still point into it is
forbidden. A call that converts, catches, wraps, or otherwise observes a result
is not a physical tail edge and uses an ordinary local home.

### 8.7 Caller-donated canonical scalar return homes

The present callee-frame donation fixes dangling return pointers but does not
bound repeated-call retention. A loop that repeatedly receives a new wide
scalar can retain one donated callee slot per iteration until the caller
returns, even when only the latest result is live. Range detection prevents
unnecessary re-donation of ancestor/heap values; it does not solve this
accumulation.

The common API therefore uses caller-donated canonical homes:

```text
caller number frame

[colored scalar homes][discard scratch][ABI/error scratch] | temporary extent
       ^
       +-- hidden scalar_return_home argument to callee

callee number frame begins above the caller's complete fixed-home area
```

The caller selects a liveness-colored home for a potentially pointer-backed
boxed result and passes its address as a hidden ABI argument. Normal and Lambda
error lanes are mutually exclusive and may share it. The callee uses the
following return protocol for whichever lane is active:

```text
if returned Item has a pointer-backed number payload:
    raw = *Item.payload                    // before restoring callee extent
    *scalar_return_home = raw
    Item = Item.tag | scalar_return_home

side_number_top = callee_number_frame_base // restore the complete extent
return Item
```

Because the destination is a fixed caller-owned home, copying is safe whether
the source payload came from the callee extent, an ancestor number frame, or a
heap representation. No source-address range comparison is required. Repeated
loop calls overwrite the same colored home once the preceding value is dead;
space is bounded by peak simultaneously live scalar values.

The Phase 7 scalar representation set is deliberately narrow:

- `INT64`, with no inline encoding;
- `UINT64`, with no inline encoding;
- out-of-band `FLOAT` (the tiny/subnormal boxed residue);
- a dynamic `Item` only when runtime classification selects one of those
  representations.

`DTIME` is deliberately absent. Phase 7 makes every datetime payload a GC-heap
object, even while local, because datetime is rare, heap ownership removes its
activation-lifetime machinery, and an object representation may grow beyond
one word without changing the generated-call ABI. The same scalar homes are
used by local conversions and imported results; "return home" names the
generated-call ABI role, not the only source of an activation scalar.

This was a representation and ownership change, not merely a return-classifier
edit. Phase 7 changed all producers, consumers, storage boundaries, runtime
classifiers, and tests together: compact inline `INT64`, heap-backed transient
`UINT64`, and number-stack `DTIME` are removed.

#### Float discriminator contract

`Lambda_Type_Double_Boxing.md` is the representation authority. The common
return-home implementation must reuse its canonical discriminator rather than
open-code an `inline double?` test followed by separate `+0?` and `-0?` tests.

- The universal self-tagged-double boundary uses exactly one
  `ITEM_DBL_MASK` test. On ARM64 this is the designed `tst`-immediate plus
  branch.
- The boxed out-of-band arm must be carried forward as representation
  provenance whenever lowering performed the boxing itself. In that case the
  return path already knows a pointer payload exists and performs no second
  float classification.
- For a generic `Item` that has lost provenance, the shared representation
  layer exposes one canonical `float_item_has_pointer_payload()` predicate.
  Packed `+0`/`-0` are payloads `0`/`1`, so the cold tagged-FLOAT arm uses one
  payload discriminator (`payload > 1`), never two equality comparisons.

Thus the common emitter consumes one float representation predicate; it does
not reproduce the current multi-test classifier in every function epilogue.

#### ABI and lifetime rules

- Native direct scalar returns do not use a scalar home.
- A native direct scalar return with a boxed Lambda error lane may still need a
  home for that error lane.
- A proven-safe `Item` return contract does not use a scalar home.
- An internal boxed body that may return a pointer-backed scalar on either lane
  accepts the hidden home argument.
- Public wrappers and runtime-indirect function pointers retain the public ABI;
  in Phases 0 through 6 they reserve a local home, call the internal body, and
  heap-rehome escaping results before restoring their activation. Phase 7 uses
  the persistent policy selected in section 15 for Item-only outward results.
- The hidden home is an ABI `POINTER` with
  `JIT_VALUE_RAW_NON_GC_POINTER`; it is never published as an `Item` or GC root.
- A runtime import that may allocate number payloads is bracketed by a caller
  watermark; its result is adopted before that watermark is restored.
- A call whose scalar-capable lanes are all discarded uses the caller's shared
  fixed discard scratch home. An observable error lane receives a normal
  liveness home even when the normal result is discarded.
- A physical tail call forwards the current function's incoming caller home;
  it never returns through a home owned by the removed activation, and none of
  its arguments may retain that activation's homes after teardown.
- Environment/container writes copy numeric payloads into destination-owned
  scalar storage at the ownership boundary when that destination has a natural
  owner.
- Async/generator suspension never preserves a pointer to an activation home;
  suspended state uses storage owned by the persistent state object. The
  fallback for a persistent destination without such storage is the explicit
  open decision in Phase 7.
- Recursion and re-entry are safe because each caller activation owns a distinct
  fixed-home area.

The runtime verifier can assert that a required home is non-null, aligned, lies
inside the bound number-stack reservation, and is below the callee number-frame
base. Those checks cannot prove immediate-caller ownership from the pointer
alone. The generated-module verifier supplies that proof statically: every
ordinary direct edge must pass a finalized fixed home from its current
activation, every tail edge must forward the incoming home, and every public
edge must target a checked wrapper. It also rejects a boxed pointer-backed
scalar leaving an implementation body whose ABI omitted the home.

### 8.8 Late root and frame materialization

Root-slot count is not known reliably at AST analysis time. It depends on MIR
definitions, uses, conversions, and call positions. Therefore:

- analysis classifies values and storage;
- `MirEmitter` records semantic events while lowering;
- the common finalizer computes liveness at safepoints;
- stable homes receive slots only if live at at least one safepoint;
- stores are inserted only where a slot must become current before collection;
- if no root slots survive, the root prologue and epilogue are omitted.

This is the performance-critical interpretation of safepoint-current canonical
slots. It forbids both missing publication and always-current publication.

Scalar-home finalization runs beside, not inside, GC-root finalization. It
colors all overlapping scalar lifetimes, patches logical-home references, adds
the optional discard scratch plus fixed error/ABI scratch slots, and reserves
the resulting number-frame prefix once per activation. The prefix layout is
`colored homes`, `discard scratch`, then `ABI/error scratch`; temporary
allocation begins above the complete prefix. It does not use collecting
safepoints because raw scalar payloads are outside the GC domain.
Its CFG construction and reachability rules should reuse the existing common
root-finalizer utilities rather than introduce a second control-flow parser.

### 8.9 Checked and bound entries

`MIR_ENTRY_CHECKED` is used for public, indirect, or otherwise unproven entry.
It verifies or binds the active runtime context and reserves required frame
storage.

`MIR_ENTRY_BOUND_INTERNAL` is used only for a direct generated call whose caller
is proven to have the same valid bound runtime context. It skips redundant
context binding, but still creates a distinct activation and reserves any
required number/root slots.

The module verifier should reject:

- exporting a bound-only body;
- storing its address in an indirect-call slot;
- calling it from a mismatched context;
- a public wrapper that omits the checked entry;
- storing an internal boxed/native body in a public runtime function pointer;
- a caller-home fixup that does not resolve inside the caller's fixed prefix;
- a non-tail edge that forwards an ancestor's incoming home, or a tail edge that
  passes a home owned by the activation it removes.

### 8.10 Overflow and failure behavior

Frame-capacity failure must be fail-closed. The plan supplies the correct
language-visible failure lane or sentinel. If producing that failure requires a
boxed error Item, it follows the same error-lane home contract. The generated
function cannot continue normal execution with an unreserved root or number
frame.

The overflow path is uncommon and may be cold, but it remains part of the
common physical frame API. Language profiles decide how its failure is surfaced.

## 9. End-to-end examples

### 9.1 Native numeric function

For a function equivalent to `return a + b` where analysis proves both
parameters and the return are `F64`:

- the native-body variant uses `F64` parameters and return;
- representation demand keeps the addition and result native;
- no value has a GC value class;
- no call is a GC safepoint;
- finalization assigns zero root slots and removes the root frame;
- a public wrapper boxes only when its public caller needs `Item`.

The common API must make this the natural result, not a special-case peephole.

### 9.2 Captured dynamic value

For a value stored in a scope environment and live across a `MAY_GC` call:

- function analysis marks scope-environment storage and escape;
- representation demand selects `Item` as the canonical binding representation;
- lowering gives it a stable home;
- the common call API records the safepoint;
- finalization assigns a root slot and inserts a write only when the current
  definition must be published before that safepoint.

### 9.3 JavaScript call that may throw

For a JavaScript runtime helper classified as `MAY_GC` and
`JIT_EXCEPTION_MAY_SET`:

- `em_call_import()` records uses, call, result, and the GC safepoint;
- the returned call result reports the exception effect;
- JS lowering emits its required exception check and completion routing;
- the root finalizer protects values live at the call;
- `MirEmitter` does not know the active catch/finally target.

This keeps exception optimization possible without contaminating the common
frame layer with JavaScript control-flow semantics.

### 9.4 Repeated wide-scalar return

For:

```text
result = null
repeat 1,000,000 times:
    result = make_wide_scalar()
```

representation/liveness analysis assigns `result` one scalar home. Every call
passes that same home once the previous value is dead. A pointer-backed result
is copied into the home, the callee restores its complete number extent, and
the next iteration overwrites the same word. Number-stack use is therefore
`O(peak live scalar values)`, not `O(call count)`.

If each result is appended to a persistent container instead, the values are
genuinely live and are copied into storage owned by that container. That growth
is semantic ownership, not leaked activation storage. The shipped
Phase 0-through-6 fallback may heap-rehome some such values; Phase 7 requires
owner-backed storage where the destination has a natural owner.

### 9.5 Local wide-scalar conversion

For a loop that converts a native `I64`, `U64`, or out-of-band `F64` to `Item`,
`em_require_rep()` assigns a logical activation home to the converted value.
The cold boxing helper may allocate temporary payload storage, but the emitter
adopts the payload into that home and restores the helper watermark. Repeated
assignment reuses the colored home once the preceding definition is dead, so
local conversion has the same `O(peak live scalar values)` bound as call
results. Phase 7 applies this to small and wide `I64`/`U64` alike because neither
integer type retains an inline `Item` representation.

### 9.6 Lambda normal/error return lanes

For a function with a native success result and a boxed `T^E` error lane,
analysis describes both lanes. If `E` can be a pointer-backed scalar, the
internal body accepts one hidden home even though the normal result is native.
On failure it adopts the error into that home before restoring the number frame
and publishing `Context::mir_return_lane`. The public wrapper reads that lane
immediately, heap-rehomes the error, republishes the heap-owned Item, and only
then ends its own activation; an intervening call may not overwrite the lane.
That sentence records the shipped Phase 0-through-6 behavior; Phase 7 replaces
the final heap rehome only if the public boundary gains another explicit
persistent-owner contract.

### 9.7 Public, discarded, and tail calls

- a JavaScript function object points to its public checked wrapper, never the
  hidden-argument body;
- a call with no observable scalar-capable lane uses the caller's one discard
  scratch home, while an observable Lambda error receives a colored home;
- an ordinary direct call uses a colored home in the current activation;
- a physical tail call forwards the incoming caller home;
- self-tail recursion lowered to a loop writes the incoming home only on final
  exit.

## 10. Required invariants

The implementation should assert these invariants in debug builds:

1. **Immutable analysis:** no MIR register, instruction, label, or root slot is
   stored in `FnAnalysis` or representation analysis.
2. **Representation truth:** every `MirValue` has one actual representation;
   consumer demand is recorded separately.
3. **Semantic rooting:** rootability follows `JitValueClass`, never pointer-sized
   MIR type alone.
4. **Conservative unknowns:** unresolved calls and unknown value classes remain
   safe until audited; unknown argument effects imply possible persistent
   capture.
5. **One call path:** every generated call is emitted through the common
   metadata-aware API.
6. **One frame owner:** language transpilers do not maintain parallel root
   candidate, call-site, or slot arrays.
7. **Safepoint currency:** a canonical root slot is current at every reachable
   collecting safepoint where its value is live.
8. **No redundant currency:** stores are not emitted merely to keep slots current
   between safepoints.
9. **Cleanup order:** no `MAY_GC` operation occurs after generated roots are
   restored.
10. **Bound-entry proof:** bound-only entries cannot be reached through public or
    unverified call edges.
11. **Public ABI isolation:** public and runtime-indirect function pointers name
    checked wrappers, never hidden-argument implementation bodies.
12. **Zero-root elision:** a function with no rootable live-at-safepoint value
    emits no root-stack reservation.
13. **Profile boundary:** JavaScript completion and Lambda error semantics are
    not inferred by the physical frame API.
14. **Complete lane contract:** normal and error transports are both described;
    their union determines whether the hidden caller home is required.
15. **Caller-owned scalar home:** a boxed pointer-backed scalar from the active
    normal or error lane is copied into the caller-provided home before the
    callee number watermark is restored.
16. **Complete callee restore:** returning a scalar never leaves
    `side_number_top` inside the callee extent.
17. **All-producer coverage:** calls, imports, conversions, joins, and error
    lanes use the same activation-home rules.
18. **Liveness-bounded homes:** physical scalar-home count is determined by
    peak simultaneous liveness, and a home is not reused while its prior value
    remains live.
19. **Finalized references:** every logical scalar-home reference is patched to
    the final fixed prefix before module finalization; no early physical slot
    assumption survives.
20. **Discard scratch isolation:** the shared discard home is non-null, never
    aliases a live value home, and is used only when every scalar-capable return
    lane is unobserved.
21. **Tail-home forwarding:** a physical tail edge forwards the incoming caller
    home, restores the outgoing frame, and passes no argument that still points
    into that frame; an ordinary edge uses a home owned by its current
    activation.
22. **No scalar/root alias:** scalar homes contain raw numeric payloads and are
    never registered or scanned as GC root slots.
23. **Float representation authority:** float return classification uses the
    canonical single discriminator defined by `Lambda_Type_Double_Boxing.md`;
    epilogues do not duplicate packed-zero tests.
24. **No activation escape:** persistent, container, environment, async, and
    generator ownership never retains an activation scalar-home pointer.
25. **Uniform 64-bit integer boxing:** after Phase 7, neither `INT64` nor
    `UINT64` has an inline `Item` encoding; transient boxed values use number
    homes regardless of magnitude.
26. **Datetime heap ownership:** after Phase 7, every `DTIME` payload is
    GC-heap-owned and no datetime `Item` points into a number frame, caller
    home, container scalar tail, or async scalar sidecar.
27. **Persistent-owner boundary:** an `INT64`/`UINT64` value crossing an
    activation boundary is copied into storage owned by the destination. A
    boundary with no declared owner must use a GC scalar cell under Phase 7; it
    may never retain the activation pointer.

Non-local unwind remains protected by runtime-entry watermark snapshots and
restoration. Native C/C++ helpers continue to use exact native handles. Values
that survive a generated activation continue to require persistent or async
ownership; a generated root slot is not persistent storage.

## 11. Proposed file ownership

The final names may follow existing build conventions, but responsibilities
should be divided as follows:

| File/module | Responsibility |
|---|---|
| `lambda/value_rep.h` | C-compatible `ValueRep`, ABI representation, and semantic value-class vocabulary |
| `lambda/function_analysis.hpp/.cpp` | common function traversal, call graph, variants, effects, and immutable `FnAnalysis` construction |
| `lambda/representation_analysis.hpp/.cpp` | demand seeding/propagation, joins, canonical binding representation, scalar-home demand/liveness, and conversion plans |
| `lambda/sys_func_registry.h/.c` | runtime/system-function metadata declaration, normalization, validation, and diagnostics |
| `lambda/mir_emitter_shared.hpp/.cpp` | `MirValue`, call emission, mutable frame state, GC-root and scalar-home coloring, finalization, and prologue/epilogue materialization |
| Lambda and JS profile modules | semantic demand seeds, coercion rules, error/completion routing, dynamic-scope and suspension facts |

If adding files creates unnecessary build churn, the same responsibilities may
initially remain in existing modules. The ownership boundary matters more than
the physical split.

## 12. Migration plan

**Completion:** Phases 0 through 7 are implemented. The staged wording below
is retained as the migration record and dependency order.

### Phase 0: freeze contracts and add diagnostics

- define `ValueRep`, dual-lane normalized call metadata, per-argument ownership
  effects, entry kinds, and invariants;
- report call counts by GC/re-entry/exception classification;
- report unknown argument effects and unresolved public signatures;
- report per-function MIR, local, root-slot, root-store, scalar-home,
  temporary-number-slot, and safepoint counts;
- preserve current emitted behavior.

### Phase 1: common metadata-aware call API

- make both transpilers resolve all imports through one metadata path;
- move use, temporary, result, and safepoint recording into
  `em_call_import()`/`em_call_direct()`;
- model normal/error transports without changing the emitted ABI;
- preserve existing ownership-boundary rehomes and diagnose where the new
  metadata would require an additional one; activation of those new rehomes
  waits for Phase 5 representation lowering;
- retain language-specific exception/error checks;
- validate ABI representation at every emitted call.

This phase removes duplicated bookkeeping without changing representation.

### Phase 2: common function and representation analysis in shadow mode

- expand `FnAnalysis` and add `FnVariantAnalysis`;
- run the joint type/representation/variant/effect fixed point without initially
  changing emitted MIR;
- compute activation scalar-home demand for calls, imports, conversions, joins,
  and error lanes in shadow mode;
- compare proposed representations with actual lowering choices;
- log mismatches and unknown/conservative reasons.

Shadow mode is important because a representation change can alter both ABI and
GC behavior even when source results remain identical.

### Phase 3: move frame state into `MirEmitter`

- introduce `MirFunctionPlan` and the canonical lifecycle;
- move scalar-home candidates, coloring, discard scratch, and symbolic-address
  fixups into the common frame owner without enabling the hidden ABI;
- adapt the current JS frame begin/finish path to the common owner while keeping
  JS completion routing outside it;
- migrate Lambda onto the same lifecycle, including its error lane;
- delete the per-language root candidate, call-site, slot, and frame arrays only
  after both paths pass equivalence checks.

Compatibility wrappers may remain briefly, but they must forward to the single
common state rather than mirror it.

### Phase 4: establish public-wrapper and implementation-body variants

- emit public checked wrappers for every externally or runtime-indirectly
  reachable function while preserving the current public ABI;
- point JavaScript function objects and other public function pointers only at
  those wrappers;
- emit a distinct internal boxed body and represent any already-generated
  native body as a separate internal variant without changing current
  selection; boxed scalar returns still use legacy donation in this phase;
- teach the module verifier to enforce entry reachability and checked/bound
  context rules;
- verify indirect calls, closures, methods, cross-module calls, constructors,
  and resume entries before changing the internal ABI.

This phase creates the ABI boundary needed by caller homes. The hidden argument
must not be added while a runtime-indirect pointer can still target the body.

### Phase 5: enable representation-demand lowering and caller homes

- introduce `MirValue` at binding, call, and return boundaries;
- replace eager boxing with `em_require_rep()` at actual demand points;
- let the common representation analysis select native-body variants and exact
  scalar return modes;
- add the hidden caller-home ABI only to internal variants whose normal/error
  lane union may return a pointer-backed scalar;
- replace callee-frame slot retention with copy-to-caller-home followed by full
  callee number-watermark restoration;
- adopt local conversions and imported results into colored homes before
  restoring temporary watermarks;
- implement discard scratch and tail-home forwarding;
- update public wrappers in the same change to reserve/adopt a local home and
  heap-rehome normal or error Items that escape their activation;
- make float adoption consume the canonical single discriminator from
  `Lambda_Type_Double_Boxing.md`;
- remove holder/version locals made obsolete by canonical representation;
- measure MIR count and runtime after each class of conversion changes.

### Phase 6: cleanup and final enforcement

- use bound direct bodies on verified internal edges;
- enforce reachability in the module verifier;
- reject every unresolved scalar-home fixup and every metadata entry that
  claims a non-conservative argument effect without audit evidence;
- remove legacy per-language frame helpers and stale metadata tables;
- update the stack MIR and rooting design documents to the final API names.

### Phase 7: realign `INT64`, `UINT64`, and `DTIME` storage — implemented

- removed the compact inline `INT64` encoding and its producer, decoder,
  discriminator, GC-filter, and container special cases;
- made transient `INT64` and `UINT64` producers use the same one-word number
  homes regardless of value magnitude;
- added `UINT64` to exact and dynamic scalar-home classification, imported-result
  adoption, generated return lanes, caller-home transfer, discard scratch, and
  heap/persistent rehoming;
- removed `DTIME` from number-stack allocation, scalar-home classification,
  caller-home transfer, owned scalar tails, and number-frame rebasing;
- made every datetime constructor and producer return a GC-heap-owned datetime
  object, including runtime materialization of datetime literals/constants,
  retaining the same semantic `DTIME` Item tag while allowing the heap object
  layout to grow beyond 64 bits later;
- made arrays, maps/objects, closure environments, module/global bindings,
  async/generator frames, and task/promise state own their persistent
  `INT64`/`UINT64` payloads instead of retaining activation pointers;
- audited every Item-only public return, opaque retaining call, singleton runtime
  slot, external embedding boundary, and cross-thread handoff using the
  scenario list in section 15;
- used an immutable GC scalar cell for every persistent `DOUBLE`/`INT64`/`UINT64`
  slot that has no natural owner; retain the audit counts so this interim
  fallback can be revisited;
- recorded the general no-heap direction for `DOUBLE`/`INT64`/`UINT64`: no
  standalone heap payload, no GC-root classification, and no collector object
  cases after the transitional persistent fallback is removed. This is a
  deferred goal, not part of Phase 7 acceptance;
- deleted the transitional representation paths after old and new
  encodings cannot be mixed inside one runtime.

Phase 7 changed the shared runtime `Item` representation. C2MIR does not gain
the hidden caller-home ABI; it remains a compatibility path against the new
runtime producers and decoders.

## 13. Verification and acceptance gates

Every migration phase should pass:

- `make build`;
- `make build-test`;
- `make test-lambda-baseline` for Lambda changes;
- JavaScript transpiler and Node preliminary tests for JS changes;
- `make test262-baseline` with zero new failures and zero retry-only results;
- precise-only forced-GC tests with conservative native scanning disabled where
  supported;
- forced collection at each audited `MAY_GC` boundary;
- non-local-unwind watermark restoration tests;
- million-iteration wide-scalar return loops proving constant number-stack use;
- million-iteration local wide-scalar boxing loops proving the same bound;
- simultaneous-live scalar return tests proving home coloring does not
  overwrite an older value;
- native-success/boxed-error `T^E` tests for both normal and error exits;
- public and indirect wrapper tests proving the hidden ABI is never exposed;
- discarded-result loops proving one scratch slot is reused;
- self-tail-recursive tests preserving the current loop transform, plus
  incoming-home forwarding tests whenever a physical cross-function tail edge
  is enabled;
- captured-argument tests proving persistent stores copy scalar payloads into
  destination-owned storage or the selected fail-closed fallback;
- inline, packed-zero, and out-of-band float return tests pinned to
  `Lambda_Type_Double_Boxing.md`'s canonical encodings;
- Phase 7 representation tests proving small, zero, boundary, and full-width
  `INT64`/`UINT64` values are never emitted in an inline Item form;
- Phase 7 repeated local, imported-result, normal/error-return, discarded, and
  tail-call tests for both signed and unsigned 64-bit values;
- Phase 7 container, closure, module/global, async/generator, task/promise,
  exception-slot, public-return, embedding, and cross-thread lifetime tests;
- Phase 7 datetime tests proving every payload is GC-managed, survives all
  publication boundaries, never points into the number stack, and is accessed
  through an object layout that is not assumed to be one word;
- release-build performance measurements. Debug builds must not be used for
  performance conclusions.

### Phase 7 execution record (2026-07-20)

The completed scalar realignment passed `make build` and `make build-test`.
`make test-lambda-baseline` completed with 1,389 runtime tests and 2,105 input
baseline tests passed, with 0 failed. The captured `make test262-baseline` run
completed 40,261/40,261 baseline tests fully passed, with 0 non-fully-passing,
0 failed, 0 regressions, and 0.0 seconds in retry processing.

The debug-MIR control compares the same implementation body immediately before
and after the relevant Phase 7 lowering cleanup. Lambda
`_phase7_dtime_return_107` fell from 31 instructions/21 locals to 22
instructions/15 locals (29.0% fewer instructions). LambdaJS
`_js_phase7BigIntReturn_146_body` fell from 17 instructions/10 locals to 15
instructions/9 locals (11.8% fewer instructions). These are body-frame counts,
so the reduction is not hidden in a public wrapper.

The common API is complete only when:

- Lambda and JS no longer own separate generated root-frame state;
- call metadata and safepoint recording have one implementation;
- generated frame prologue/epilogue mechanics have one implementation;
- public wrappers and internal bodies have verifier-enforced distinct ABIs;
- common representation demand prevents avoidable boxing and holder locals;
- zero-root functions emit no root-frame operations;
- repeated calls and local conversions reuse caller/activation homes and leave
  no temporary or callee number extent retained after the boundary;
- scalar-home count matches peak live scalar demand rather than dynamic call
  count;
- normal and error return lanes both satisfy the scalar ownership contract;
- discarded and physical tail calls satisfy their scratch/forwarding contracts;
- implementation-body MIR and local counts are equal to or lower than the
  pre-migration equivalent for representative numeric, dynamic,
  exception-heavy, closure, async, and container workloads; public-wrapper
  counts and aggregate code size are reported separately;
- release `test262` wall time improves without weakening precise-GC checks.

## 14. Final design boundary

The common API should answer four questions and only those four:

1. **Function analysis:** what does this function and each callable variant
   semantically require?
2. **Representation demand:** in what physical form is each value required at
   each boundary?
3. **Metadata:** what ABI and effects does every call have?
4. **MIR/frame lowering:** how are those immutable decisions materialized into
   one safe and efficient generated activation?

Language profiles answer what operations mean. `MirEmitter` answers how proven
values, calls, roots, and returns occupy a generated frame. Keeping that line
sharp is what allows the implementation to be both shared and fast.

## 15. Numeric scalar representation realignment

**Status:** implemented Phase 7; GC scalar-cell fallback is active and counted
for persistent slots without a natural owner. The general direction is that `DOUBLE`,
`INT64`, and `UINT64` ultimately have no standalone heap representation and are
not GC-rootable value classes; that final step is deferred.

Before Phase 7, small `INT64` values could be inline, wide `INT64` and `DTIME`
could use the number stack, and `UINT64` was heap-backed. Phase 7 replaced that
split with a type-consistent ownership model. Integer magnitude no longer
selects a boxed representation, signed and unsigned 64-bit integers use the
same lifetime mechanics, and datetime leaves the numeric stack entirely.

### 15.1 Target storage matrix

| Situation | `INT64` | `UINT64` | `DTIME` |
|---|---|---|---|
| Small/local value | no inline form; number home when transient, or retain an already-persistent representation | no inline form; number home when transient, or retain an already-persistent representation | GC heap |
| Wide transient value | number home | number home | GC heap |
| Generated-function return | caller-donated number home | caller-donated number home | ordinary heap-owned `Item`; no caller home |
| Array/environment storage | destination-owned scalar storage | destination-owned scalar storage | GC pointer stored in the destination |
| Persistent slot without natural owner | GC heap for Phase 7; future non-GC ownership deferred | GC heap for Phase 7; future non-GC ownership deferred | GC heap |

Out-of-band `DOUBLE` already follows the number-home and caller-home protocol.
For persistent slots without a natural owner it uses the same interim GC scalar
cell as `INT64`/`UINT64`; removing that last numeric heap case is part of the
deferred goal in section 15.6.

"No inline form" is normative. Zero, one, values inside the float64 safe-integer
band, and full-width values follow the same `INT64`/`UINT64` ownership rules.
This removes value-dependent representation branches and prevents a small value
from silently bypassing the same return, storage, and lifetime protocol used by
a large value.

This does not remove the separate inline encodings for `LMD_TYPE_INT`, compact
`LMD_TYPE_NUM_SIZED` values, or self-tagged floats. The realignment is specific
to the full-width `LMD_TYPE_INT64` and `LMD_TYPE_UINT64` types.

A number home is one raw 64-bit payload word. It is not a GC root. Signedness is
semantic metadata carried by the `Item` tag and analysis; it does not require a
different physical home. A transient value that is already backed by verified
persistent storage may remain there, but a transient producer must not allocate
on the GC heap merely because the integer is small or unsigned.

### 15.2 Datetime policy

Every `DTIME` payload is GC-heap-owned. Datetime is expected to be rare enough
that eliminating its number-stack classifier, caller-home lane, environment
sidecar cases, and relocation logic is more valuable than avoiding its
allocation. Generated functions return the already-persistent datetime Item
through their ordinary value ABI.

The heap object, not the tagged `Item`, defines datetime's payload layout.
Consumers must use datetime accessors rather than assuming that the object is
exactly one 64-bit word. This permits future timezone, calendar, precision, or
other representation data to expand the object without adding a wider number
home or changing the caller-home ABI.

### 15.3 What counts as destination-owned scalar storage

A destination is a natural owner when its lifetime already defines how long the
stored value remains valid and its layout can reserve one unscanned payload word
per stored integer. Examples include:

- array/list scalar tails and typed-array element buffers;
- typed map/object fields and sidecars for dynamically typed fields;
- closure environments and `with`/dynamic-scope environments;
- module and global binding tables;
- async/generator frames and resumable spill records;
- task, promise, message, callback, and event records;
- exception/completion records that are modeled as owned runtime objects.

The owner stores the `Item` tag/pointer in its scanned value region and the raw
integer payload in an unscanned region, or stores type and raw payload directly
in a typed field. Moving or resizing the owner must rebase any interior pointer.
Reading a value out of movable owner storage copies it into the current
activation home unless the owner is proven stable for the complete borrow.

Destination ownership is different from GC ownership. The containing object
may itself be GC-managed, but the individual integer need not be a separate GC
allocation.

### 15.4 Persistent slots without a natural owner

The phrase means that the current boundary transports or retains only a bare
`Item` and provides neither a caller-donated home nor a storage object whose
lifetime and payload capacity are part of the contract. The live audit must
cover at least these scenarios:

| Scenario | Why no natural owner exists today | Possible explicit contract |
|---|---|---|
| Public or runtime-indirect function return | the public ABI returns one `Item`; the callee cannot know how long an arbitrary caller retains it | heap fallback, caller-supplied persistent result home, or an owned return handle |
| External embedding/plugin API | foreign code may copy and retain an `Item` after the Lambda entry returns | documented borrow-only API, explicit retain/copy handle, or heap fallback |
| Opaque or unaudited native callee that may retain an argument | the generated caller cannot see the callee's destination or lifetime | require retaining-call metadata and an owner/copy callback, otherwise heap fallback or reject the call |
| Singleton runtime/TLS slot | slots such as the current JS exception value hold one `Item` but no companion payload word | add an owned scalar sidecar/record, or use heap fallback |
| Cross-thread handoff without a message owner | a raw `Item` may outlive both the sending activation and its context | require an owned message/transfer object, or use a process-safe heap/handle |
| Legacy callback, queue, cache, or registry storing bare Items | the structure has storage for the tag/pointer but no scalar payload or rebase contract | extend the entry layout with owned payload storage, or use heap fallback |

Several of these are "no owner in the current layout," not intrinsically
ownerless. Internal runtime slots, queues, tasks, and exception state should
normally be converted to explicit owner records. Public and foreign Item-only
ABIs are the strongest cases for retaining a universal fallback because their
consumer lifetime is not visible to the producer.

`Context::mir_return_lane` is not automatically persistent storage. If the
generated/public wrapper consumes it before restoring the producing activation,
it is a transport lane and can use the same caller-home protocol. If a runtime
path permits the lane to survive that restoration, it must instead acquire an
explicit owner or use the selected persistent fallback.

### 15.5 Interim GC fallback decision

GC allocation is not required by out-of-band `DOUBLE`, `INT64`, or `UINT64`
semantics. Their payloads contain no outgoing GC references. What is required
is a stable address whose lifetime is at least as long as every retained `Item`
that points to it.

Within the runtime, separate per-numeric-scalar GC allocations can be eliminated
if every persistent destination supplies owner-backed payload storage. GC heap
is therefore not required for arrays, environments, modules, async state,
tasks, promises, messages, or exceptions once those owners are complete and
audited.

With the current one-word public and foreign `Item` ABI, however, some fallback
is still required unless that ABI is strengthened. Phase 7 therefore uses an
immutable GC scalar cell for every persistent `DOUBLE`, `INT64`, or `UINT64`
value whose destination has no natural owner. This is the fail-closed rule for
public returns, foreign retention, unaudited retaining calls, singleton slots,
and ownerless cross-thread handoff.

This is an interim lifetime decision, not a claim that numeric payloads belong
in the GC domain. It lets Phase 7 realign local, return, and destination-owned
storage without simultaneously changing every public or foreign lifetime ABI.
`lambda_scalar_heap_rehome_count(INT64/UINT64/FLOAT)` counts actual fallback
allocations, rather than treating a numeric tag as proof of heap ownership.
The audit inventory therefore keeps the remaining edge cost visible behind the
single rehome helper.

The future alternatives remain:

1. **Caller-supplied persistent home:** extend the public/embedding ABI with a
   result or retained-argument home whose lifetime is guaranteed by the caller.
2. **Owned non-GC scalar handle/cell:** return an explicitly retained object or
   handle with a release/ownership contract rather than a borrowable bare
   `Item`.
3. **Reject persistent retention:** keep an Item-only API borrow-scoped and
   require callers that retain a value to copy it through an ownership API.

The preferred direction remains destination-owned storage for all auditable
internal paths, with the GC scalar-cell fallback confined to public, foreign,
or genuinely opaque retention edges. The fallback cannot be removed merely by
assuming callers consume values immediately; every such edge needs a
mechanically checkable lifetime contract.

The decision gate is an inventory with counts for each remaining heap rehome:
public return, retained argument, singleton slot, embedding boundary, and
cross-thread transfer. The current counter is per numeric type; split
per-boundary attribution is a future diagnostic refinement. A future phase may
remove the numeric GC fallback only if that inventory reaches zero or every
remaining edge is changed to an explicit home/handle/borrow contract. Until
then the GC heap remains the required fail-closed behavior.

### 15.6 Deferred direction: non-heap, non-GC numeric scalar classes

The long-term goal is that `DOUBLE`, `INT64`, and `UINT64` payloads never require
a standalone heap allocation, whether GC-managed or otherwise.
Inline/self-tagged doubles remain inline; transient out-of-band doubles and
64-bit integers use activation/caller homes; destination-owned values use owner
sidecars or typed fields; and the remaining public/foreign cases gain an
explicit non-heap lifetime contract. `DTIME` is not part of this goal and
remains intentionally GC-heap-owned.

"Non-heap scalar" does not prohibit a raw numeric payload from being embedded
inside a heap-managed array, environment, task, or other destination owner. The
owner may be a GC object; the numeric payload is not a separate heap object, and
the numeric `Item` is never the edge that keeps the owner alive. Reading such a
payload out of its owner produces an activation/caller-home representation.

The final GC invariant is:

- `DOUBLE`, `INT64`, and `UINT64` tags never encode a GC-managed address;
- a statically known value of any of those types never receives a GC root home;
- dynamic `Item` marking rejects those runtime tags before pointer recovery;
- the collector has no scalar-object trace/sweep case for those types;
- numeric boxing, return transfer, and persistent copying introduce no GC
  allocation effect;
- only the containing destination owner, when it is a GC object, participates
  in rooting and tracing.

This removes three value classes from both generated GC-root analysis and the
collector's managed-object domain. Dynamic `ANY` values can still require a GC
root because they may contain strings, containers, functions, errors,
datetimes, or other GC-owned values; at runtime an actual numeric tag is simply
ignored by the marker.

A donated-slot-derived protocol is retained as one option. The terminology is
important: the retired protocol described in section 8.7 is **callee-frame
donation**, which retained callee slots and could grow by call count. The active
generated return protocol is **caller-donated canonical homes**, which is
bounded by simultaneous liveness. A future persistent protocol could extend
caller-provided homes beyond generated activations or transfer ownership of a
donated non-GC cell, but it must not simply restore the retired callee-frame
retention behavior.

Any future donated persistent home must provide:

- an explicit owner and lifetime that survives the producing activation;
- deterministic release, reuse, or transfer instead of watermark pinning;
- space bounded by genuinely live persistent values, not calls made;
- a public/embedding ABI that distinguishes borrowed from retained results;
- safe context and cross-thread transfer rules;
- no scanning of raw numeric payload words as GC roots.

The exact protocol is deliberately deferred. Phase 7 should preserve enough
diagnostics to identify the remaining GC scalar-cell sites and revisit them
without reopening transient scalar-home correctness.

## 16. Post-implementation optimization choices

Apart from the proposed Phase 7 realignment, there is no open correctness issue
in the Phase 0-through-6 implementation. The following optional refinements
retain conservative production fallbacks and can be measured independently:

1. **Profitability thresholds:** the correctness fixed point determines legal
   representations; release measurements still need to choose when a native
   variant or cached conversion is worth its code size.
2. **Cross-module metadata format:** a compact serialized or linker-visible
   form may be added for verified internal cross-module calls. Until then those
   edges use public wrappers.
3. **Scalar-reference fixup mechanism:** the current MIR permits displacement
   patching before finalization; a late address-definition scheme remains an
   equivalent fallback if backend APIs change.
4. **Registry audit coverage:** unaudited imports and argument effects stay
   conservative and safe, but their call-count diagnostics will determine the
   order in which metadata is refined.
5. **Physical file split and container packing:** the responsibilities in
   section 11 are fixed, while exact filenames, inline capacities, and project
   container layouts may follow measured build and hot-path costs.

None of these optional refinements permits exposing an internal ABI, omitting
an error lane, retaining activation storage across an ownership boundary, or
weakening the rooting invariants.
