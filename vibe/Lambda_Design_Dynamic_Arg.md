# Dynamic function arguments in Lambda and LambdaJS

## Status

- **LambdaJS:** implemented. Dynamic calls use exact rooted adapter spans; the
  former fixed `padded_args[]` buffer is retired.
- **Core Lambda:** implemented for MIR Direct. Core Lambda enforces the
  language-level maximum and routes boxed dynamic calls through one checked,
  exact-rooted dispatcher.
- **Lambda/LambdaJS interop:** deliberately deferred. The required boundary
  rules are recorded here so neither runtime guesses the other's native ABI.
- **AOT compatibility:** deliberately deferred. This design does not add an
  entry-ABI version or define compatibility with previously compiled modules.

This document consolidates the implemented LambdaJS design from
`Lambda_Impl_JS_Dynamic_Arg.md` and defines the corresponding Core Lambda
policy. The general compilation and root-frame model remains documented in
`Lambda_Design_Compiling.md`.

### Implemented Core Lambda boundary

- `LAMBDA_MAX_FUNCTION_ARGS` is 16. The AST rejects the 17th actual or formal
  slot with `ERR_FUNCTION_ARGUMENT_LIMIT`; runtime-created calls perform the
  same check before adapter allocation or pointer dispatch.
- `Function` now contains the unversioned `entry_abi` field plus named
  bit-fields. MIR Direct publishes every Core first-class function through its
  `_b` wrapper and marks it as a boxed function or procedure; raw direct JIT
  entries are not dynamically callable.
- The dispatcher roots `[function, source actuals...]` in a `RootSpan`, then
  materializes a second exact span only for optional/rest adaptation. The old
  native `physical_args[]` marshal buffer is retired from the active runtime.
- The sole native arity dispatcher supports 0 through 16 boxed Item operands.
  It checks entry kind, source signature, `var` parameters, context ownership,
  scalar-home metadata, and argument count before casting `ptr`.
- The public variadic wrapper receives its collector as boxed `Item` and casts
  it to `List*` only while entering the raw body.

## Decision summary

| Concern | Core Lambda | LambdaJS |
|---|---|---|
| Source actual-argument count | At most `LAMBDA_MAX_FUNCTION_ARGS` (16) | Dynamic `Item* + argc` span has no fixed capacity |
| Declared function parameters | At most 16 physical user slots | Context wrapper: 32 formals; native non-context wrapper: 16, or 15 with an environment |
| Statically known call | Individual fixed ABI operands; native specialization is allowed | Individual wrapper operands when the target and semantics are proven |
| Dynamic call boundary | Boxed `Item` operands dispatched through one arity-checked C++ thunk family | Contiguous `Item* args, int argc`, followed by wrapper adaptation |
| Dynamic call of an unboxed/foreign entry | Rejected by explicit `FunctionEntryAbi` before the function pointer is cast | Not applicable to the JS generic boundary |
| Argument adaptation storage | Exact precise-root spans; no native `physical_args[]` owner | Exact borrowed or owned adapter span; no `padded_args[]` |

## Remaining fixed implementation limits

The Core Lambda ceiling of `LAMBDA_MAX_FUNCTION_ARGS` is intentional language
design and is not repeated here as a migration target. The following limits are
still hard-coded implementation, optimization, or interop boundaries.

### Runtime ABI boundaries

- **LambdaJS context wrappers:** `JS_MIR_CONTEXT_CALL_MAX_ARITY` is 32. A
  generated JS wrapper with more than 32 physical formals is rejected by the
  C++ wrapper dispatcher. This does not cap the generic JS source-actual span:
  `Item* args + argc` is dynamically sized and may contain more than 32 actuals
  when JS semantics permit it (for example, through rest handling).
- **LambdaJS legacy native callbacks:** the non-context `P0`...`P16`
  function-pointer family accepts at most 16 Item operands, or 15 user
  operands when a closure environment occupies one operand. This applies to
  host callbacks created through the non-MIR `js_new_function`/
  `js_new_closure`/
  `js_new_method_function` path, not ordinary generated JS source functions.
- **Core-to-JS export bridge:** the current bridge publishes only
  `js_call_export_0_into` through `js_call_export_8_into`. A Core-to-JS
  direct-symbol call with more than eight operands therefore remains an
  interop limitation. It is separate from the unbounded JS adapter span and is
  deferred with the broader interop work.

### Compiler and optimization metadata

- **JS native specialization:** `JS_MIR_TYPED_PARAM_LIMIT` is 16. Functions
  with more parameters remain valid JS functions but do not receive the fixed
  native-specialization metadata and use the boxed path.
- **JS constructor-shape inference:** constructor property metadata has 16
  slots. Discovering a 17th property disables this optimization; it does not
  limit the object's actual property count.
- **JS P6/type-inference scratch state:** the native/type-inference metadata
  still has 16 parameter/evidence slots, and return inference has at most 32
  local-name slots (`local_names[32][128]`). Formal and alias names now remain
  AST-owned `String` references rather than copied `param_names[16][128]`
  buffers. These are analysis limits, not JS call limits.
- **MIR argument-register bookkeeping:**
  `MIR_SHARED_MAX_FUNCTION_ARGUMENT_REGS` is 32. It bounds emitter-side
  recording of incoming argument registers only; it is not a runtime ABI
  ceiling.
- **Frozen legacy C transpiler:** the old named-argument reorder path keeps a
  `MAX_PARAMS` 32-entry array. This applies to the legacy C transpiler path,
  not MIR Direct.
- **Closure analysis trackers:** JS closure read-back and TDZ trackers each
  have a 512-entry capacity, and tracked names are copied into 128-byte slots.
  The tracker clamps or reports overflow; this is not a limit on JS formal
  parameters or on the runtime closure object itself.

### Remaining source-name staging buffers

There is no language-level maximum identifier length in the AST/name pool, but
several compiler boundaries still stage source-derived names in fixed C buffers:

- JS function/local/capture metadata commonly uses `char[128]`; function body
  names use `char[144]`.
- JS closure scope-environment keys use dynamically allocated entries of
  `char[64]` and are copied with bounded `snprintf`, so long keys can be
  truncated and potentially collide.
- Core MIR formal-name staging still uses buffers such as `pname[64]` and
  `prefixed[68]`, in addition to broader `char[128]` scratch names.
- Shared MIR import/scope metadata also contains `name[128]` fields.

These are remaining name-truncation/collision risks and should be removed or
changed to stable mapped identifiers in a follow-up. The `%r<hex-serial>`
scheme already removes source-name dependence for generated MIR registers, but
it does not yet remove every source-derived formal, local, or environment-name
buffer.

### Retired argument buffers

The active JS implementation no longer contains a fixed `padded_args[]`,
`arguments_param_names[16][128]`, or JS `physical_args[]` argument marshal
buffer. Any old Core `physical_args[8]` occurrence is inside retired `#if 0`
code and is not an execution-time limit. Adapter spans are exact-sized rooted
spans.

## Why the runtimes differ

C and MIR can call a function pointer when the prototype and every operand are
known while compiling the call. They cannot make an ordinary native call whose
number of register/stack operands is discovered from `argc` at runtime. Doing
that requires a generic array ABI, an interpreter/libffi-style bridge, or a
precompiled family of fixed signatures.

## Internal MIR register names

MIR names are NUL-terminated C strings and are compared by spelling.  They are
an implementation detail, not LambdaJS or Core Lambda binding identities.
Therefore the shared internal-register allocator uses this compact scheme:

```text
%r<lowercase-hex-register-serial>
```

For example, the 42nd generated register in one MIR function is `%r2a`.  The
serial is the existing monotonic per-function register counter, so it already
distinguishes lexical shadows and temporaries.  `%` is accepted as an initial
MIR name character but cannot begin an ordinary Lambda or JavaScript source
identifier; this keeps generated registers distinct from source-derived formal
names during the remaining migration.

The register allocator must not embed source spellings in these names.  Source
spelling and lexical scope remain the responsibility of each language's binding
resolver, which maps a resolved binding to its `MIR_reg_t`.  External ABI names
(imports, exports, and runtime symbols) remain exact C-string names.  Direct
source-derived MIR formal names are a separate follow-up: they must be mapped
or retained by parameter index before the compiler-wide source-name buffers can
be retired.

LambdaJS already has the generic dynamic boundary:

```text
Item* args, int argc
```

The JS dispatcher can therefore retain an arbitrary source-actual span, select
the target, and adapt that span to the selected fixed wrapper.

Core Lambda's statically resolved ABI instead passes individual operands:

```text
Context*, arg0, arg1, ... [, scalar_result_home]
```

Native scalar arguments may be unboxed. A dynamic Core Lambda call cannot
construct an arbitrary version of that signature at runtime. This is a limit
of the current native-call architecture, not an intrinsic maximum imposed by
MIR on statically known calls. The Core language nevertheless chooses a fixed
maximum so declarations, direct calls, and dynamic calls have one portable
contract.

## LambdaJS: implemented adapter-span design

### Two distinct spans

Every generic JS invocation distinguishes:

1. the immutable **source-actual span**, containing exactly the values written
   by the caller and the original `argc`; and
2. the **invoke-adapter span**, containing the fixed operands expected by the
   selected compiled wrapper.

They cannot be treated as one mutable buffer. For example:

```text
source call              f(x, y, z)
source-actual span       [x, y, z]       argc = 3
callee                   function f(a, ...rest)
invoke-adapter span      [x, [y, z]]     wrapper operand count = 2
```

The source span remains visible through JS `arguments`; rest lowering changes
only the wrapper operands.

### Borrowed and owned adapters

`JsCallAdapterSpan` has two modes:

- A non-rest call with enough actuals borrows the required prefix of the
  caller's active rooted argument suffix. A native caller is first copied into
  the generic dispatcher's exact rooted source span.
- Missing-formal padding, every rest transformation, and any source whose
  ownership is not proven use a new exact `RootSpan`. Missing operands become
  JS `undefined`; the rest array is installed in its adapter root before an
  element push can allocate.

The generated caller suffix owns exactly its source-actual extent. The runtime
does not infer writable capacity from adjacent side-root-stack cells and does
not grow the caller's frame after the fact. An owned span is reserved with the
exact count required by that invocation and is released in LIFO order on every
exit.

### Dispatch order and rooting invariants

The final target and source sequence are selected before adapter construction.
This ordering is required because proxy traps, bound-argument merging,
constructor routing, and other JS semantics may replace the target or the
actual list.

Before any allocating operation:

- the function, receiver, and saved call state are precisely rooted;
- non-prerooted native actuals are copied into an exact source root span; and
- no GC-capable adapter value depends solely on a native C++ local or array.

`js_pending_call_args` and `js_pending_call_argc` always describe the immutable
source span. Only the selected wrapper reads the invoke-adapter span.

### What is and is not bounded

The adapter has no fixed capacity. Dynamic JS actual count is bounded only by
representable `argc`, address space, and successful exact root reservation.

The following are separate fixed-wrapper ABI limits:

- compiled context-ABI wrapper: 32 declared physical formals;
- native non-context callback: 16 user operands, or 15 when a captured
  environment occupies the first native operand.

These numbers validate a selected native wrapper. They are not argument-array
sizes and do not constrain the generic JS `Item* + argc` source span. Direct,
statically proven wrapper calls remain outside the adapter path.

## Core Lambda language contract

### One authoritative maximum

Define one public constant in a shared Lambda ABI/semantic header:

```text
LAMBDA_MAX_FUNCTION_ARGS = 16
```

Sixteen is chosen instead of 32 because it is already the widest uniformly
analyzed Core shape: direct-call staging and native parameter-specialization
metadata currently model 16 user positions. The implementation must turn that
existing width into an explicit checked language contract, not preserve its
current silent truncation behavior. Choosing 32 would require first making all
of those compiler analyses scalable or 32-wide and would still double the
native thunk surface. The limit may be revised by a future language change,
but it is not independently tunable in one lowering path.

The literal `16` must not be repeated as independent compiler or runtime
policy. Static assertions must keep the constant representable by
`Function::arity` and all current compiler metadata.

The maximum applies to both:

- **formal user slots:** fixed/default parameters plus one slot for a rest
  collector; and
- **source actuals:** positional and named actual values after any runtime
  expansion has been resolved.

The rest marker is represented separately in the current AST, but it consumes
one physical user slot. Consequently, a variadic function may have at most 15
fixed parameters plus its rest collector. Hidden ABI operands do not count:

- `Context*`;
- closure environment;
- method receiver when represented as a hidden operand;
- region capability; and
- scalar-result home.

Compiler staging must distinguish user slots from total native operands.
Buffers that also contain hidden operands are exact-sized or use a derived
capacity such as `LAMBDA_MAX_FUNCTION_ARGS + required_hidden_count`; they must
not reduce the user limit or introduce another independent literal maximum.

Defaults do not add an actual argument. A known spread counts its expanded
values; an expansion whose size is not statically known receives the runtime
check.

### Static diagnostics

Semantic analysis, before MIR lowering, rejects:

- a declaration or function type with more than 16 physical user slots;
- a direct call expression with more than 16 actuals;
- a dynamic call expression with more than 16 syntactically known actuals;
- a variadic declaration whose fixed parameters plus rest collector exceed
  16;
- an unresolved dynamic call containing named arguments; and
- a dynamic call to a signature with `var`/inout parameters when that signature
  is statically known.

The compiler must use one shared arity-validation helper across named
functions, function expressions, procedures where applicable, function types,
imports, named-argument normalization, and pipe-injected arguments. Lowering
must assert that validation has run before indexing staging arrays; it must not
silently truncate with conditions such as `i < 16`.

The diagnostic should name the function or call site, report the actual count
and the maximum, and recommend one of these alternatives:

- replace a long fixed signature with a smaller fixed prefix and `...rest`;
- pass the long data set as one array/list; or
- use a map/object when the values have meaningful names.

A rest parameter permits a variable call shape but does not bypass the
16-actual language limit. Data sets longer than 16 values must be aggregated
into an array/list/map argument.

Named arguments remain supported for a statically resolved target, where the
compiler normalizes them to positional parameter order. Runtime target values
do not currently retain the parameter-name metadata required for that
normalization, so unresolved dynamic named calls are deferred and rejected.
Likewise, dynamic `var`/inout calls are deferred because an Item span carries a
value rather than a writable caller location.

### Runtime backstop

Static checking cannot protect calls originating in native embedders,
corrupted metadata, runtime spread expansion, or a future foreign bridge. The
common runtime dispatcher therefore rejects, before adapter construction or
function-pointer casting:

- negative or unrepresentable `argc`;
- `argc > LAMBDA_MAX_FUNCTION_ARGS`;
- formal physical count above the same maximum;
- invalid/truncated `Function::arity` or `TypeFunc` metadata;
- a `var`/inout signature; and
- an entry kind that does not match ordinary versus task/procedure invocation
  mode.

Exceeding the language maximum produces `ERR_FUNCTION_ARGUMENT_LIMIT` as an
ordinary Lambda error. An invalid non-function value remains
`ERR_INVALID_CALL`; a valid but unboxed/foreign entry produces
`ERR_UNSUPPORTED_DYNAMIC_ABI`; and a count that is within the language maximum
but outside the callee's semantic required/maximum range remains
`ERR_ARGUMENT_COUNT_MISMATCH`.

## Core Lambda callable-ABI metadata

### Current audit

`Function` currently records:

| Metadata | Meaning |
|---|---|
| `FN_FLAG_BOXED_RET` | `Function::ptr` returns legacy `RetItem` rather than `Item` |
| `FN_FLAG_MIR_PUBLIC_ABI` | the entry requires a trailing caller-owned scalar-result home |
| `FN_FLAG_MIR_CONTEXT_ABI` | the generated entry requires `Context*` first |
| `closure_env` | a hidden environment operand must be supplied when non-null |
| `fn_type` | semantic required/default/rest and return information |
| `FN_FLAG_SYS_REF` | the value identifies a heterogeneous builtin and is not a generic dynamic-call entry |

None of these states that every language-visible parameter accepted by
`Function::ptr` is a boxed `Item`. In particular, `FN_FLAG_BOXED_RET` describes
only the return carrier. Using it as evidence about parameters would permit an
undefined native function-pointer cast.

MIR compilation already creates an all-Item `_b` public wrapper for typed
parameters, optional/default parameters, rest parameters, and other entry
shapes that require adaptation. First-class references prefer that wrapper.
An all-untyped raw body may be published directly because its raw operands are
already Item-compatible. Therefore wrapper name or `_b` suffix is also not a
sufficient runtime test.

### Required entry-ABI metadata

`LMD_TYPE_FUNC` is shared by Core Lambda, Python, Jube, and host adapters. A
single boxed-parameter flag would not establish that the value belongs to the
Core Lambda call protocol. Add a compact, unversioned `FunctionEntryAbi` type
and proper structure field:

```c
typedef uint8_t FunctionEntryAbi;

enum {
    FN_ENTRY_ABI_UNKNOWN = 0,
    FN_ENTRY_ABI_LAMBDA_DIRECT_ONLY,
    FN_ENTRY_ABI_LAMBDA_BOXED_FUNCTION,
    FN_ENTRY_ABI_LAMBDA_BOXED_PROCEDURE,
    FN_ENTRY_ABI_FOREIGN,
    FN_ENTRY_ABI_HOST_ADAPTER,
};
```

The current structure has a one-byte `flags` member at offset 3 followed by
four alignment-padding bytes at offsets 4 through 7. Replace that layout with
real C fields rather than treating padding as hidden storage:

```c
struct Function {
    uint8_t type_id;
    uint8_t arity;
    uint8_t closure_field_count;
    FunctionEntryAbi entry_abi;

    union {
        uint32_t flags;  // whole-word initialization/copy only
        struct {
            uint32_t returns_ret_item : 1;
            uint32_t has_kwargs : 1;
            uint32_t is_generator : 1;
            uint32_t is_coroutine : 1;
            uint32_t is_system_function_ref : 1;
            uint32_t requires_scalar_result_home : 1;
            uint32_t requires_runtime_context : 1;
            uint32_t reserved : 25;
        };
    };

    void* fn_type;
    fn_ptr ptr;
    void* closure_env;
    const char* name;
    struct Context* runtime_context;
};
```

This consumes the complete eight-byte prefix without explicit padding:

```text
offset 0       type_id
offset 1       arity
offset 2       closure_field_count
offset 3       entry_abi
offset 4..7    flags union
offset 8       fn_type
```

Add layout assertions for `entry_abi`, `flags`, `fn_type`, and the remaining
pointer fields. The pointer offsets and total `Function` size remain unchanged
on the existing target ABIs. This implementation does not add an entry-ABI
version.

Remove the `FN_FLAG_*` mask macros and migrate code to named fields:

```c
fn->requires_runtime_context = 1;
if (fn->requires_scalar_result_home) { /* ... */ }
if (fn->returns_ret_item) { /* ... */ }
```

The raw `flags` word exists only to zero, initialize, or copy the complete
union. Code must not define replacement mask macros or encode/decode individual
fields through shifts. Bit-field layout is not serialized or used as a durable
AOT ABI.

`FN_ENTRY_ABI_LAMBDA_BOXED_FUNCTION` means that every language-visible operand
accepted by the address in `Function::ptr` has the boxed `Item`
representation required by ordinary Core dynamic dispatch.
`FN_ENTRY_ABI_LAMBDA_BOXED_PROCEDURE` has the same operand guarantee but may be
entered only through the procedure/task invocation protocol.

The enum says nothing about return carrier, context, closure environment, or
result home. The named bit-fields describe those orthogonal ABI facts. The
dispatcher validates their combination after it validates `entry_abi`.

Publication rules are fail-closed:

- generated `_b` public wrappers publish boxed-function or boxed-procedure
  entry kind according to the source declaration;
- raw all-Item entries publish a boxed kind only when compiler ABI analysis
  proves every exposed parameter is Item-compatible;
- closures publish a boxed kind only when their user operands are
  Item-compatible; their hidden environment is described separately;
- raw typed/native-specialized entries publish `LAMBDA_DIRECT_ONLY`;
- foreign-language functions and heterogeneous native functions publish
  `FOREIGN` or `HOST_ADAPTER` and do not enter the Core dispatcher;
- builtin identity objects remain non-callable through the generic Core
  dispatcher; and
- generic `to_fn*`/`to_closure*` constructors require an explicit entry kind or
  default to `UNKNOWN`. They validate arity before narrowing it to
  `Function::arity`.

The publisher derives `entry_abi` and all callable-ABI fields from the selected
entry's `FnVariantAnalysis` contract, not merely from a `uses_wrapper` boolean.
A raw all-Item entry can still require `Context*` or a scalar-result home, so a
boxed entry kind does not permit those other fields to be omitted.

Every Core publication site must be migrated: local first-class references,
function expressions, closures, module namespace exports, bound object
methods, task-root closures, and Jube adapters that deliberately implement the
Core boxed protocol. Python and other language-owned constructors publish
their own non-Core kind and keep their language dispatcher.

The compiler may retain both addresses: static calls use the optimized raw
entry, while a first-class `Function` publishes the boxed entry. `entry_abi`
describes the address actually stored in `Function::ptr`, not the source
function in the abstract.

Before any cast or call, ordinary dynamic dispatch requires
`FN_ENTRY_ABI_LAMBDA_BOXED_FUNCTION`. A valid Function carrying
`FN_ENTRY_ABI_UNKNOWN`, `FN_ENTRY_ABI_LAMBDA_DIRECT_ONLY`, a procedure kind, or
a foreign/host entry returns `ERR_UNSUPPORTED_DYNAMIC_ABI`; it is never called
speculatively. Task dispatch separately requires
`FN_ENTRY_ABI_LAMBDA_BOXED_PROCEDURE`.

Do not infer an entry ABI from `fn_type`, pointer identity, return type,
function name, `_b` suffix, or source type alone.

### Boxed rest boundary

The public `_b` wrapper must take every user slot as `MIR_T_I64`/`Item`,
including the synthesized rest-collector slot. It receives a boxed List Item,
validates/converts that value to the raw body's `List*`, and only then calls the
raw entry.

The current public wrapper declares `_vargs` as `MIR_T_P`; leaving it that way
would make an all-Item C++ thunk call through a mismatched prototype even
though both carriers happen to be 64 bits on current targets. Hidden
`Context*`, environment/self, and scalar-home operands keep their explicitly
typed native representations.

## One Core dynamic dispatcher

### Public entry flow

All current entry points become thin façades over one internal authority:

```text
fn_call / fn_call_into / fn_call0..3 / callback helpers
  -> validate Function identity, entry ABI, and invocation mode
  -> validate language MAX and semantic arity
  -> establish exact source roots
  -> reuse the source span or build an exact invoke-adapter span
  -> select context/env/return-home/return-carrier ABI
  -> invoke the one fixed-arity thunk family
  -> normalize the result and restore roots
```

The runtime must not retain separate 0..8 switches in `fn_call`, context
switches in `fn_call_into`, non-context switches, and another group in each
specialized helper. One dispatcher translation unit owns all native
function-pointer casts.

This centralization is scoped to Core Lambda. LambdaJS and Python retain their
language-specific generic dispatchers. A Jube/native adapter participates only
when it deliberately publishes a Core boxed entry kind.

The unavoidable signatures for arities 0 through 16 should be generated from
one canonical arity list. A shared thunk template or generated X-macro family
may inject the orthogonal hidden operands (`Context*`, environment, and result
home), but there is only one runtime arity selection and one maintained list
of supported arities. Public helpers prepare their input and call this
authority; they contain no function-pointer switch.

The dispatcher receives an explicit invocation mode. Ordinary mode accepts
only boxed functions. Task/procedure mode accepts only boxed procedures and
preserves suspension semantics. Sharing the physical thunk family does not
make a procedure callable through an ordinary function expression.

The switch is checked native adaptation, not the language definition. The
language constant is validated first, and no `default` branch is allowed to
fall through to a mismatched cast.

### Core adapter spans and GC ownership

Core Lambda does not currently have LambdaJS's required contiguous caller
argument suffix. Direct Core calls root GC-capable boxed values in ordinary
function root slots, while native scalars remain in registers. The dynamic
dispatcher therefore may not assume adjacent caller slots or reuse unrelated
root-frame cells.

Use a Core `LambdaCallAdapterSpan` over exact `RootSpan` reservations. The
initial implementation deliberately has no borrowed-source mode:

- copy every dynamic source sequence into an exact source-root span before any
  operation can allocate;
- reuse that newly rooted source span when it already has the selected wrapper
  shape;
- otherwise reserve an exact invoke span, fill omitted positions with
  `ITEM_MISSING_ARGUMENT`, and place the materialized rest `List*` in its final
  root slot before pushing elements; and
- keep the source span immutable until the call completes.

Core has no LambdaJS-style contiguous argument suffix, and the current
`List*`/small-helper inputs do not carry a precise-root-span capability. A
future explicitly prerooted Core API may add borrowing, but the dispatcher
must never infer it from pointer location or nearby side-root cells.

This retires `physical_args[8]` as both capacity and temporary GC ownership.
There is still a deliberate language maximum, but storage remains exact for
the invocation; the maximum is not implemented as an unrooted `Item[MAX]`
buffer.

All normal, error, raised-error, and allocation-failure exits restore side-root
watermarks exactly. The function object, source list, adapter values, rest
list, and result home must remain live under forced collection.

### Direct calls remain optimized

Statically resolved calls do not route through the dynamic dispatcher. MIR
continues to select the raw typed/native entry when argument representations
are proven and otherwise calls the boxed public wrapper. The compiler still
passes individual operands and can retain native scalars in registers.

The language arity check nevertheless applies to direct calls. It is a source
and module contract, not merely a limitation of first-class calls.

### When Lambda uses dynamic dispatch

Argument count does not decide whether a call is direct or dynamic. A call is
direct when MIR lowering resolves the callee to a specific Lambda declaration
or import and can emit that entry's fixed prototype. It may select either the
raw specialized body or the boxed public wrapper, but it is still a direct MIR
call.

Lambda uses the dynamic dispatcher when the callee identity is a runtime
value, including:

- a function-valued parameter;
- a function stored in a local variable, collection, map field, or object
  member when lowering cannot prove the exact target;
- a function selected by an `if`, match, lookup, or other runtime expression;
- a closure returned from or passed through another function; and
- higher-order runtime callbacks such as collection operations.

For example:

```text
fn add(a, b) => a + b
add(1, 2)                       // direct

fn apply(f, x) => f(x)
apply(add, 1)                   // apply is direct; f(x) inside apply is dynamic

let selected = condition then add else other
selected(1, 2)                  // dynamic unless a future optimization proves one target
```

The current lowerer uses `fn_call0_into` through `fn_call3_into` for small
dynamic calls and `fn_call_into` with a List for larger ones. Under this
design, those names are only thin input façades over the same central
dispatcher; the 0..3 choice is not a different call semantic.

## Lambda/LambdaJS dynamic-call interop (deferred)

No interop implementation is authorized by this design. A future bridge must
make the callee language and entry ABI explicit; neither side may reinterpret
the other's function-object layout or cast a raw foreign pointer.

Required boundary behavior:

- **JS calling Lambda:** preserve the JS source-actual span, translate values
  under an explicit interop policy, enforce Core's 16-actual maximum, require a
  boxed Core entry, and translate the resulting Lambda error into the chosen JS
  exception/result semantics.
- **Lambda calling JS:** Core source syntax remains limited to 16 individual
  actuals, even though the JS generic boundary can hold more. A long data set
  crosses as one collection until a future explicit spread/bridge API is
  designed. The bridge then supplies a rooted JS `Item* + argc` span.
- **Current export bridge limit:** the existing Core Lambda-to-JS export bridge
  only publishes `js_call_export_0_into` through `js_call_export_8_into`.
  Therefore a Core-to-JS direct-symbol call with more than eight operands is
  not supported today. This is an interop-only limitation, not a LambdaJS
  adapter-span limit; replace the fixed wrapper family with an explicit rooted
  span bridge as part of the deferred interop work.
- **Context and heap ownership:** a span rooted in one runtime/context is not
  automatically valid in another. Cross-context invocation must either prove
  a shared heap/root domain or copy through a bridge-owned exact root span.
- **Defaults, rest, `this`, constructors, named arguments, errors, and scalar
  result homes:** each is a language semantic, not a raw ABI coincidence, and
  must be translated by the bridge.
- **Imports:** the current cross-language direct-symbol path must eventually
  publish an explicit bridge descriptor and callable-ABI metadata instead of
  assuming that a foreign exported symbol is a boxed Lambda entry.

## Other dynamic-call issues that must be addressed

| Issue | Required decision |
|---|---|
| Runtime spread expansion | Count after expansion and reject above 16 before adapter allocation or dispatch. |
| Named arguments | Statically resolved calls normalize by name. Unresolved dynamic named calls are deferred and rejected rather than guessed as positional. |
| `var`/inout parameters | Deferred. A boxed value span does not carry a writable caller location, so dynamic calls to such signatures are rejected. |
| Procedures, generators, and coroutines | Use an explicit invocation mode and entry kind. Task mode may invoke a boxed Lambda procedure; ordinary mode rejects it. Other protocols remain language-owned. |
| Builtins and native callbacks | Heterogeneous native signatures remain non-callable through the Core generic dispatcher unless an adapter deliberately publishes the Core boxed protocol. |
| Optional/default evaluation | Root earlier actuals and the function before evaluating an allocating default. Preserve error short-circuit behavior before unboxing. |
| Rest construction | Preserve the immutable actual sequence, root the rest list before any push can collect, and pass it to the public wrapper as a boxed Item. |
| AOT compatibility | Deferred. Do not add an `entry_abi` version or promise compatibility with previously compiled AOT modules in this implementation. |
| Reflection | Report source fixed/rest shape and the language maximum separately from hidden physical operands. |
| Context ownership | Reject or bridge calls whose `runtime_context`/heap is incompatible with the active caller; never move raw rooted pointers between heaps. |
| Error consistency | Centralize error construction and stack capture so every façade returns the same error kind and message for the same failure. |
| C2MIR | The legacy C2MIR path is frozen. This implementation applies to MIR Direct; C2MIR receives no new ABI work. |

## Implementation outline for Core Lambda

1. Add `LAMBDA_MAX_FUNCTION_ARGS` and central semantic validation. Replace
   silent `i < 16` truncation with checked invariants derived from the constant.
2. Replace the existing `Function.flags` byte plus alignment padding with the
   explicit `entry_abi` field and 32-bit flags/bit-fields union. Remove
   `FN_FLAG_*` masks, preserve pointer offsets, and migrate every Function
   publisher and consumer to the named fields.
3. Make the public variadic wrapper accept its rest slot as boxed `Item` and
   convert it to `List*` only inside the wrapper.
4. Introduce the always-owned exact Core source/adapter-span helper and remove
   `physical_args[8]`.
5. Implement the single 0..16 boxed function-pointer thunk family and route
   `fn_call*`, `fn_call*_into`, concurrency callbacks, vector callbacks, and
   other Core dynamic users through it with explicit ordinary/task mode.
6. Reject unresolved dynamic named arguments and dynamic `var`/inout
   signatures. Delete the displaced switches and literal 7/8 limits while
   keeping direct MIR calls on their specialized path.
7. Add boundary, entry-kind rejection, forced-GC, default/rest, closure,
   procedure-mode, result-home, module, and cross-platform tests.

## Acceptance criteria

- A declaration or call with 17 Core Lambda argument slots fails statically
  with a precise diagnostic; 16 succeeds when its other semantics are valid.
- A runtime-created or spread-expanded Core call with 17 actuals returns
  `ERR_FUNCTION_ARGUMENT_LIMIT` without invoking the pointer.
- A dynamic call to an unboxed/direct-only Core entry returns
  `ERR_UNSUPPORTED_DYNAMIC_ABI` before any function-pointer cast.
- Unknown, foreign, host-only, and wrong invocation-mode entry kinds are
  rejected before any function-pointer cast.
- `Function` contains explicit `entry_abi` and named flag bit-fields, has no
  explicit padding-storage convention, and preserves the existing pointer
  offsets. Runtime code uses direct fields rather than `FN_FLAG_*` masks.
- Typed/native functions remain dynamically callable through a published
  boxed wrapper and remain directly callable through their raw optimized entry.
- A public variadic wrapper receives its rest collector as boxed `Item` and
  converts it internally; the central user-operand thunk remains uniformly
  Item-typed.
- Closure environment and context/result-home operands do not reduce the 16
  user-slot contract.
- Optional/rest adaptation has no native `physical_args[]` buffer and survives
  forced GC at every argument/default/rest allocation point.
- Exactly one Core runtime component owns the C++ function-pointer arity
  dispatch; all public and callback entry points delegate to it.
- Unresolved dynamic named calls and dynamic `var`/inout calls fail closed;
  statically resolved named calls and ordinary direct calls remain supported.
- LambdaJS retains its exact adapter-span behavior and supports source actual
  spans larger than its fixed wrapper-formal limits where JS semantics allow.
- Cross-language dynamic calls remain rejected or routed through existing
  explicitly supported bridges until the deferred interop design is
  implemented.
- AOT compatibility and `entry_abi` versioning remain outside this
  implementation.
