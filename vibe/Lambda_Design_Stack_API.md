# Common function, representation, MIR, and frame API

**Status:** proposal

**Scope:** MIR-direct compilation for Lambda and LambdaJS

**Revision reviewed:** `79c4070826cccf433cfd48c4b8320a61ce762318`

**Out of scope:** C2MIR, native helper `RootFrame` APIs, and changes to the GC algorithm

Related design records are `Lambda_Design_Stack_Rooting.md`,
`Lambda_Design_Unified_AST.md`, `Lambda_Stack_MIR.md`, and
`Lambda_Stack_JS_MIR.md`. `Lambda_Type_Double_Boxing.md` is normative for the
float `Item` discriminator and packed-zero encoding used by this proposal.

This document proposes the common compiler API that should sit between the
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

The proposal completes an architectural direction that is only partly realized
today. `MirEmitter`, semantic root candidates, the shared root finalizer, and
scalar-return rehoming are already common. Function analysis, frame ownership,
call-site bookkeeping, prologue/epilogue emission, and representation choices
are still split between Lambda and LambdaJS.

**New scalar-return decision:** the current callee-frame donation is replaced by
a **caller-donated canonical scalar return home**. The caller reserves a fixed,
liveness-colored number slot and passes its address to any generated callee
whose boxed result may contain a pointer-backed scalar. The callee copies the
one-word payload into that home, restores its complete number extent, retags the
returned `Item`, and returns normally. Repeated calls therefore reuse caller
homes instead of retaining one new donated slot per call.

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
  such as a generic boxed entry or a native direct body.
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
- bound scalar-return storage by peak simultaneously live values rather than
  dynamic call count;
- produce diagnostics and MIR-count statistics from the common layer;
- allow Lambda and LambdaJS to migrate independently behind compatibility
  adapters.

### 2.2 Non-goals

This proposal does not:

- make Lambda and JavaScript share coercion, truthiness, error, or completion
  semantics;
- move JavaScript catch/finally/generator/async routing into `MirEmitter`;
- replace native C/C++ exact handle scopes;
- change persistent or async ownership rules;
- change non-local-unwind restoration at runtime entry boundaries;
- redesign the GC allocator or collector;
- change C2MIR. C2MIR remains a compatibility path and is left as-is.

## 3. Current implementation and the missing boundary

| Area | Shared today | Still duplicated or incomplete |
|---|---|---|
| Function analysis | unified AST has `AstFuncNode::analysis`; `FnAnalysis` carries captures, parameter evidence, and async facts | entry variants, call effects, return representation, binding storage, and per-value representation demand are not common analysis products |
| Representation | shared scalar-return modes and semantic GC value classes exist | most representation decisions are made while lowering; eager boxing and language-specific holder/version locals remain possible; current callee donation can retain one slot per newly returned wide scalar call |
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
| scalar return-home candidate, liveness, and colored number slot | representation analysis / `MirEmitter` | analysis facts no; physical slot yes |
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
    FN_ENTRY_GENERIC,
    FN_ENTRY_DIRECT_BODY,
    FN_ENTRY_RESUME
} FnEntryKind;

typedef enum FnErrorLane {
    FN_ERROR_LANE_NONE,
    FN_ERROR_LANE_VALUE_ERROR
} FnErrorLane;

typedef enum ScalarReturnClass {
    SCALAR_RETURN_NONE,
    SCALAR_RETURN_I64,
    SCALAR_RETURN_F64,
    SCALAR_RETURN_DTIME,
    SCALAR_RETURN_DYNAMIC
} ScalarReturnClass;

typedef struct FnEffectSummary {
    bool may_gc;
    bool may_reenter;
    bool may_raise;
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

typedef struct FnReturnAnalysis {
    TypeId semantic_type;
    ValueRep abi_rep;
    ScalarReturnClass scalar_class;
    bool needs_caller_scalar_home;
    FnErrorLane error_lane;
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

`needs_caller_scalar_home` is true only for a boxed ABI that may return an
out-of-line `INT64`, out-of-band `FLOAT`, or `DTIME` payload. Native scalar
returns and proven-safe `Item` returns do not carry the hidden home parameter.
`UINT64` is intentionally absent: its current representation is heap-backed,
not number-stack-backed, and moving it would require a separate representation
and GC contract change.

### 5.2 Required analysis sequence

The common driver should run these stages in order:

1. binding and capture discovery;
2. language-profile semantic facts, such as dynamic-scope or arguments-object
   requirements;
3. type and parameter-evidence fixed point;
4. call-graph and effect propagation;
5. representation-demand analysis;
6. entry, return, and physical frame-plan derivation.

Recursive strongly connected components need a fixed point for parameter,
return, and effect summaries. Unknown, indirect, or cross-module calls remain
conservative until metadata proves otherwise.

### 5.3 Function variants

At minimum, analysis must distinguish:

- a generic externally callable entry using the public value ABI;
- a native direct body used only by verified generated callers;
- a resume entry when a language profile lowers suspension to a state machine.

The generic wrapper and direct body may share the source analysis but have
different ABI and frame plans. For example, a generic JavaScript entry may
accept and return `Item`, while a direct body accepts and returns `F64`.

A bound direct body may skip binding `Context` again. It may not skip
per-activation root or number-stack reservation. Only callers proved to have a
valid bound context may reference that entry.

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

### 6.2 Demand sources

Demand is seeded from:

- operator inputs and results;
- branches and language-profile truthiness operations;
- runtime call metadata for each parameter and return;
- container, environment, module, and global stores;
- capture and escape boundaries;
- generic and direct call ABIs;
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
  persistent container, dynamic-eval-visible location, or generic ABI;
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

### 6.5 Scalar return-home demand and coloring

Representation analysis also determines whether a boxed call result may contain
a pointer-backed number-stack scalar. It records a logical home requirement,
not a physical slot number. MIR lowering later computes exact lifetimes and
colors non-overlapping values onto the same physical home.

```c
typedef enum ScalarHomeDemand {
    SCALAR_HOME_NONE,
    SCALAR_HOME_BOXED_RETURN
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

At an escape boundary, the value leaves activation storage:

- environment, module, persistent container, or async/generator state: copy to
  the owning heap representation;
- return to another generated activation: copy to the caller-provided scalar
  return home;
- discarded value: no persistent home is required;
- native direct return: no boxed home is required.

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

typedef struct JitCallEffects {
    JitGcEffect gc;
    JitReentryEffect reentry;
    JitExceptionEffect exception;
    JitNumberStackEffect number_stack;
} JitCallEffects;

typedef struct JitCallMetadata {
    JitCallEffects effects;
    JitAbiValue result;
    const JitAbiValue* args;
    uint16_t arg_count;
    ScalarReturnClass scalar_return_class;
    bool has_scalar_return_home;
    uint32_t flags;
} JitCallMetadata;
```

The public implementation may retain packed argument classes and an inline fast
path for the current small-call limit. The normalized descriptor must not bake
that limit into generated-function metadata: direct source functions can have
more parameters, so the descriptor uses a pointer and count. The form above is
the interface consumed by analysis and `MirEmitter`.

`has_scalar_return_home` describes a hidden generated-call ABI argument. It is
not counted among source-language parameters. Static/runtime imports that do
not implement this generated ABI keep it false; their returned `Item` must be
adopted by the common call emitter or an appropriate wrapper before it can
cross a generated frame boundary.

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
- unknown semantic argument and return classes.

This fail-closed default is intentional. Optimization requires positive proof.

### 7.3 Metadata invariants

- `NO_GC` helpers must also be proven non-reentrant with respect to allocating
  code.
- allocation and coercion helpers that can allocate are always `MAY_GC`.
- a helper with a no-GC fast path and allocating slow path is `MAY_GC` unless
  the paths are exposed as separately audited imports.
- declared ABI representation must match the emitted MIR call signature.
- a boxed `Item`, raw GC pointer, and raw non-GC pointer are never interchangeable
  merely because all use a machine word.
- exception effect does not imply GC effect, and GC effect does not imply
  exception effect.
- an import classified `JIT_NUMBER_STACK_MAY_ALLOCATE` produces only
  activation-temporary number payloads; any value it stores persistently must
  already be heap-rehomed.
- the common emitter records effects; language lowering decides how an error or
  exception is observed and routed.

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
typedef struct MirValue {
    MIR_reg_t reg;
    MIR_type_t mir_type;
    TypeId semantic_type;
    ValueRep rep;
    JitValueClass value_class;
    int gc_home_id;
    int scalar_home_id;
} MirValue;
```

`gc_home_id == 0` means that no stable GC home is currently required;
`scalar_home_id == 0` means that no fixed number payload home is required. The
namespaces and slot arrays are separate. A home ID is lowering state, not
function-analysis state. Short local arithmetic helpers may still accept naked
MIR registers when their representation is unambiguous.

### 8.3 Immutable function plan

Analysis is converted into a compact plan before MIR emission:

```c
typedef enum MirEntryMode {
    MIR_ENTRY_CHECKED,
    MIR_ENTRY_BOUND_INTERNAL
} MirEntryMode;

typedef struct MirReturnPlan {
    MIR_type_t mir_type;
    ValueRep rep;
    ScalarReturnClass scalar_class;
    bool accepts_caller_scalar_home;
    FnErrorLane error_lane;
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

### 8.4 Mutable frame state

`MirEmitter` owns a `MirFrameState` for the current function. It replaces the
separate Lambda and JS `side_frame_*` state and owns:

- runtime, root-stack, and number-stack base registers;
- frame anchor and checked-entry insertion point;
- common epilogue label and return registers;
- root candidates and stable homes;
- scalar-home candidates, lifetimes, coloring, and the incoming caller-home
  parameter;
- definition, use, and temporary-lifetime events;
- call sites and safepoints;
- environment write-back actions;
- scalar-return adoption state;
- assigned root slots and late-inserted stores;
- per-function MIR/root/call statistics.

Nested generated functions use nested emitter/function contexts rather than
overwriting one global frame state.

### 8.5 Proposed lifecycle API

```c
bool em_function_begin(MirEmitter* em, const MirFunctionPlan* plan);

int em_gc_home_new(MirEmitter* em, JitValueClass value_class);
MirValue em_define_binding(MirEmitter* em, int gc_home_id, MirValue value);
void em_note_use(MirEmitter* em, MirValue value);
void em_note_temp_end(MirEmitter* em, MirValue value);

int em_scalar_home_new(MirEmitter* em);
void em_note_scalar_home_use(MirEmitter* em, int scalar_home_id);

MirValue em_require_rep(MirEmitter* em, MirValue value, ValueRep required);

typedef struct MirCallOptions {
    int scalar_return_home_id;
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

`MirCallOptions::scalar_return_home_id` identifies a home in the caller's
activation. A direct generated call passes its address through the hidden ABI.
A static/runtime import keeps its declared ABI; when its result may be a
pointer-backed scalar, the emitter adopts the returned payload into that same
home immediately after the call. For an import classified
`JIT_NUMBER_STACK_MAY_ALLOCATE`, the emitter snapshots the pre-call number
watermark, adopts the result, and restores that watermark. Internal helper
temporaries therefore cannot accumulate across loop iterations.

`em_call_import()` performs the common mechanical work:

1. validate actual ABI representations against metadata;
2. record argument uses and temporary lifetime boundaries;
3. record the call as a safepoint when it may GC;
4. emit the call;
5. adopt a possible pointer-backed scalar result into the selected caller home;
6. record the semantic return value and its definition;
7. return effect information to language lowering.

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
7. A pointer-backed scalar return is copied into the caller-provided canonical
   home and retagged when the return plan requires it.
8. The complete callee number-stack watermark is restored; no callee slot is
   retained by the return.
9. The root-stack watermark is restored.
10. The common return is emitted.
11. `em_function_finish()` computes GC-root and scalar-home liveness, colors
    their separate slot spaces, inserts only required safepoint-current root
    writes, and materializes or removes the checked prologue.

All cleanup that can allocate must happen before root restoration. No language
epilogue may append a `MAY_GC` call after `em_function_finish()` begins final
teardown.

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

[scalar home 0][scalar home 1][other fixed homes] | temporary extent
       ^
       +-- hidden scalar_return_home argument to callee

callee number frame begins above the caller's complete fixed-home area
```

The caller selects a liveness-colored home for a potentially pointer-backed
boxed result and passes its address as a hidden ABI argument. The callee uses
the following return protocol:

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

The scalar representation set is deliberately narrow:

- non-inline `INT64`;
- out-of-band `FLOAT` (the tiny/subnormal boxed residue);
- `DTIME`;
- a dynamic `Item` only when runtime classification selects one of those
  representations.

`UINT64` remains heap-backed under the current value model and is not a scalar
home representation. Adding it requires a separate value-representation and GC
decision, not merely another return classifier case.

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
- A proven-safe `Item` return does not use a scalar home.
- A boxed entry that may return a pointer-backed scalar accepts the hidden home
  argument, including conservative generic/indirect entries.
- The hidden home is an ABI `POINTER` with
  `JIT_VALUE_RAW_NON_GC_POINTER`; it is never published as an `Item` or GC root.
- A runtime import that may allocate number payloads is bracketed by a caller
  watermark; its result is adopted before that watermark is restored.
- Generic external wrappers reserve/adopt a home before entering the generated
  ABI and heap-rehome a result that escapes the entry activation.
- A discarded result needs no long-lived home.
- Environment/container writes heap-rehome the payload at the ownership
  boundary.
- Async/generator suspension never preserves a pointer to an activation home;
  suspended state uses persistent heap ownership.
- Recursion and re-entry are safe because each caller activation owns a distinct
  fixed-home area.

The verifier asserts that a required home is non-null, aligned, belongs to the
caller activation, and lies below the callee number-frame base. It also asserts
that a boxed pointer-backed scalar cannot leave an entry whose ABI omitted the
home.

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
colors all overlapping scalar lifetimes, adds fixed error/ABI scratch slots,
and reserves the resulting number-frame prefix once per activation. It does
not use collecting safepoints because raw scalar payloads are outside the GC
domain.

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
- a generic wrapper that omits the checked entry.

### 8.10 Overflow and failure behavior

Frame-capacity failure must be fail-closed. The plan supplies the correct
language-visible failure lane or sentinel. The generated function cannot
continue normal execution with an unreserved root or number frame.

The overflow path is uncommon and may be cold, but it remains part of the
common physical frame API. Language profiles decide how its failure is surfaced.

## 9. End-to-end examples

### 9.1 Native numeric function

For a function equivalent to `return a + b` where analysis proves both
parameters and the return are `F64`:

- the direct-body variant uses `F64` parameters and return;
- representation demand keeps the addition and result native;
- no value has a GC value class;
- no call is a GC safepoint;
- finalization assigns zero root slots and removes the root frame;
- a generic wrapper boxes only if a generic caller needs `Item`.

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
genuinely live and are heap-rehomed at the container boundary. That growth is
semantic ownership, not leaked activation storage.

## 10. Required invariants

The implementation should assert these invariants in debug builds:

1. **Immutable analysis:** no MIR register, instruction, label, or root slot is
   stored in `FnAnalysis` or representation analysis.
2. **Representation truth:** every `MirValue` has one actual representation;
   consumer demand is recorded separately.
3. **Semantic rooting:** rootability follows `JitValueClass`, never pointer-sized
   MIR type alone.
4. **Conservative unknowns:** unresolved calls and unknown value classes remain
   safe until audited.
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
11. **Zero-root elision:** a function with no rootable live-at-safepoint value
    emits no root-stack reservation.
12. **Profile boundary:** JavaScript completion and Lambda error semantics are
    not inferred by the physical frame API.
13. **Caller-owned scalar home:** a boxed pointer-backed scalar is copied into
    the caller-provided home before the callee number watermark is restored.
14. **Complete callee restore:** returning a scalar never leaves
    `side_number_top` inside the callee extent.
15. **Liveness-bounded homes:** physical scalar-home count is determined by
    peak simultaneous liveness, and a home is not reused while its prior value
    remains live.
16. **No scalar/root alias:** scalar homes contain raw numeric payloads and are
    never registered or scanned as GC root slots.
17. **Float representation authority:** float return classification uses the
    canonical single discriminator defined by `Lambda_Type_Double_Boxing.md`;
    epilogues do not duplicate packed-zero tests.
18. **No activation escape:** persistent, container, environment, async, and
    generator ownership never retains an activation scalar-home pointer.

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

### Phase 0: freeze contracts and add diagnostics

- define `ValueRep`, normalized call metadata, and invariants;
- report call counts by GC/re-entry/exception classification;
- report per-function MIR, local, root-slot, root-store, scalar-home,
  temporary-number-slot, and safepoint counts;
- preserve current emitted behavior.

### Phase 1: common metadata-aware call API

- make both transpilers resolve all imports through one metadata path;
- move use, temporary, result, and safepoint recording into
  `em_call_import()`/`em_call_direct()`;
- retain language-specific exception/error checks;
- validate ABI representation at every emitted call.

This phase removes duplicated bookkeeping without changing representation.

### Phase 2: common function and representation analysis in shadow mode

- expand `FnAnalysis` and add `FnVariantAnalysis`;
- compute representation demand without initially changing emitted MIR;
- compute scalar return-home demand and value lifetimes in shadow mode;
- compare proposed representations with actual lowering choices;
- log mismatches and unknown/conservative reasons.

Shadow mode is important because a representation change can alter both ABI and
GC behavior even when source results remain identical.

### Phase 3: move frame state into `MirEmitter`

- introduce `MirFunctionPlan` and the canonical lifecycle;
- move scalar-home candidates and coloring into the common frame owner;
- adapt the current JS frame begin/finish path to the common owner while keeping
  JS completion routing outside it;
- migrate Lambda onto the same lifecycle, including its error lane;
- delete the per-language root candidate, call-site, slot, and frame arrays only
  after both paths pass equivalence checks.

Compatibility wrappers may remain briefly, but they must forward to the single
common state rather than mirror it.

### Phase 4: enable representation-demand lowering

- introduce `MirValue` at binding, call, and return boundaries;
- replace eager boxing with `em_require_rep()` at actual demand points;
- enable native direct-body variants and exact scalar return modes;
- add the hidden caller-home ABI only to boxed variants that may return a
  pointer-backed scalar;
- replace callee-frame slot retention with copy-to-caller-home followed by full
  callee number-watermark restoration;
- make float adoption consume the canonical single discriminator from
  `Lambda_Type_Double_Boxing.md`;
- remove holder/version locals made obsolete by canonical representation;
- measure MIR count and runtime after each class of conversion changes.

### Phase 5: checked-wrapper/direct-body split and cleanup

- emit generic checked wrappers only where externally reachable;
- make wrappers reserve/adopt scalar homes and heap-rehome values that escape
  their entry activation;
- use bound direct bodies on verified internal edges;
- enforce reachability in the module verifier;
- remove legacy per-language frame helpers and stale metadata tables;
- update the stack MIR and rooting design documents to the final API names.

C2MIR is not changed in any phase.

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
- simultaneous-live scalar return tests proving home coloring does not
  overwrite an older value;
- inline, packed-zero, and out-of-band float return tests pinned to
  `Lambda_Type_Double_Boxing.md`'s canonical encodings;
- release-build performance measurements. Debug builds must not be used for
  performance conclusions.

The common API is complete only when:

- Lambda and JS no longer own separate generated root-frame state;
- call metadata and safepoint recording have one implementation;
- generated frame prologue/epilogue mechanics have one implementation;
- common representation demand prevents avoidable boxing and holder locals;
- zero-root functions emit no root-frame operations;
- repeated scalar-return calls reuse caller homes and leave no callee number
  extent retained after return;
- scalar-home count matches peak live scalar demand rather than dynamic call
  count;
- MIR and local counts are equal to or lower than the pre-migration equivalent
  for representative numeric, dynamic, exception-heavy, closure, async, and
  container workloads;
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
