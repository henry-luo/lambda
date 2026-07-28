# Lambda Runtime Context Register — MIR-Direct Design and Performance POC

**Date:** 2026-07-28
**Status:** Proposal; performance POC first
**Scope:** MIR Direct Lambda and LambdaJS generated code
**Related:** `vibe/Lambda_Design_Runtime_Globals.md`

## 1. Summary

This proposal evaluates keeping the active `EvalContext*` in a MIR global
function register while generated MIR code is executing.

The first question is deliberately narrow:

> Is reserving one native register for `EvalContext*` faster than passing the
> existing hidden context argument between generated functions?

The answer is not assumed to be yes. MIR's register allocator may already keep
the hidden argument in a register and coalesce most forwarding moves. Reserving
a register also reduces the general-purpose register set and can create spills.
The POC therefore exists to measure the trade-off before the runtime adopts a
new internal ABI.

The proposed scope boundary is:

- generated MIR may read `EvalContext*` from a pinned register;
- native C/C++ helpers continue to obtain `EvalContext*` from the canonical
  thread-local `context` binding;
- the pinned register is an internal MIR execution convention, not initially a
  C ABI;
- C-side pinning to the same register remains a possible, separate future
  tuning project;
- C2MIR is frozen and is not part of this proposal.

Two pinned-register designs are retained:

- **V1 — boundary-wrapper design:** each transition that may change or lose the
  active context uses an explicit save/install/restore wrapper;
- **V2 — eval-thread invariant design:** one `EvalContext*` remains bound for
  the entire lifetime of an evaluation thread, so generated MIR shares one
  pinned value and only compiler-unaware C-to-MIR entries require an adapter.

V1 is the conservative initial design and remains the fallback when the
one-context-per-thread invariant cannot be proven. V2 is the preferred
performance POC because it is materially simpler and should eliminate nearly
all dynamic boundary work.

The performance POC implements only the common, controlled paths needed for A/B
measurement. Unsupported or correctness-sensitive flows retain the existing
hidden-context ABI. A production change is considered only if the POC shows a
repeatable material improvement.

## 2. Existing Model

`EvalContext` is the runtime ownership and execution context. The current model
has two relevant transports:

1. native C/C++ runtime code uses the thread-local `context` binding; and
2. generated MIR functions receive an explicit hidden context argument and
   forward it to other generated functions.

Within a generated function, `em->frame.runtime` identifies the MIR value used
for context-field loads and calls that require an explicit runtime pointer.
Direct generated calls generally prepend that value to the callee's arguments.

This is already a sound ABI:

- each call identifies its context explicitly;
- ordinary platform calling conventions handle register allocation;
- reentrant and nested execution can select the correct context at a boundary;
- native helpers remain independent of MIR's chosen hard registers.

The performance cost may also already be small. The hidden context is frequently
live for the whole function, and MIR can assign it to a register. Direct calls
may require no physical move when caller and callee assignments agree.

## 3. Hypothesis

A dedicated context register could improve generated-code performance by:

- removing the hidden context operand from MIR-to-MIR call prototypes;
- removing context forwarding moves that survive allocation;
- freeing the first platform argument register for a language-visible argument;
- making context-field access uniform across generated functions;
- reducing call setup at small, frequently called Lambda and JavaScript
  functions.

It could reduce performance by:

- permanently removing one general-purpose register from allocation;
- increasing spills in register-heavy code;
- extending context-register liveness through all generated code;
- requiring boundary save/install/restore sequences;
- making indirect calls and mixed execution paths more complicated;
- producing different outcomes across register-rich and register-constrained
  targets.

The decision must therefore be based on release-build A/B results, including
both broad Test262 execution and the Lambda/JavaScript benchmark matrix.

## 4. Goals

- Define a MIR-only pinned-register convention for `EvalContext*`.
- Preserve TLS as the canonical way native C/C++ helpers obtain the context.
- Identify every execution flow that a production implementation must handle.
- Build a small, reversible POC that compares the pinned convention with the
  current hidden argument.
- Measure coverage as well as elapsed time so a neutral result can be
  interpreted.
- Use precise GC ownership throughout; the register does not reintroduce native
  stack scanning.

## 5. Non-goals

- Changing `EvalContext` ownership or lifetime.
- Replacing `context` TLS in native C/C++ during the initial design or POC.
- Declaring a C global register variable in the initial implementation.
- Passing a custom context register through third-party C libraries.
- Completing every callback, async, interpreter, unwind, and platform corner
  case before the performance hypothesis is tested.
- Supporting the frozen C2MIR path.
- Treating `EvalContext*` itself as a GC root or weakening precise rooting.
- Optimizing native argument rooting. That is related motivation, but it is a
  separate design problem with separate GC correctness requirements.

## 6. MIR Mechanism

MIR provides:

```c
MIR_reg_t MIR_new_global_func_reg(
    MIR_context_t ctx,
    MIR_item_t func_item,
    MIR_type_t type,
    const char *name,
    const char *hard_reg_name);
```

Conceptually, this declares a function register variable tied to a particular
target hard register. It is the MIR equivalent of a compiler-specific global
register variable; it does not merely request that an ordinary argument happen
to use a register.

For example, a generated function could declare an integer register holding the
pointer bits:

```c
MIR_reg_t runtime_reg = MIR_new_global_func_reg(
    mir_ctx, func_item, MIR_T_I64, "_runtime_ctx", hard_reg_name);
```

The exact type must follow the MIR API and target implementation. `MIR_T_I64`
is the expected portable representation for a 64-bit pointer value in this
convention; the POC must verify it on its selected target.

Important properties:

- the register is fixed, not selected independently for each function;
- MIR removes declared global hard registers from normal allocation;
- MIR-generated prologues and epilogues do not automatically save and restore
  global hard registers;
- every generated module that can execute under this convention must reserve
  the same hard register, even if some functions do not read the context;
- installation and restoration at execution boundaries are runtime ABI work,
  not behavior supplied by `MIR_new_global_func_reg`.

References:

- [MIR functions and global registers](https://github.com/vnmakarov/mir/blob/master/MIR.md#mir-functions)
- [MIR generator treatment of global hard registers](https://github.com/vnmakarov/mir/blob/master/mir-gen.c#L8298-L8299)

## 7. Proposed Internal ABI

### 7.1 V1: two generated entry classes

The design distinguishes two kinds of generated entry:

1. **Pinned internal entry**
   - receives no hidden `EvalContext*` argument;
   - reads `EvalContext*` through the MIR global function register;
   - may be called only when the caller guarantees that the pinned value is
     installed and correct.

2. **Context boundary entry**
   - retains a standard, explicit way to identify the target `EvalContext`;
   - saves the previously pinned value when nesting is possible;
   - installs the target context in both TLS and the pinned register as needed;
   - invokes a pinned internal entry;
   - restores the previous values on every supported exit.

A production implementation may materialize these as separate wrapper and body
functions, or as distinct entry labels if MIR and the native ABI permit it.
The POC should prefer a simple wrapper/body split because it makes the boundary
visible and keeps the experiment reversible.

### 7.2 V1: boundary wrappers

A boundary wrapper is a generated ABI adapter. It is not an existing runtime
class, and it is not an ordinary native helper. It connects code that follows
the standard external ABI to a generated body that assumes the pinned context
register is already valid.

Conceptually, the two signatures are:

```text
external entry: result fn(EvalContext *runtime, user_args...)
pinned body:    result fn(user_args...)
```

The outward wrapper retains the current C-callable prototype, including its
explicit context argument. A top-level wrapper conceptually performs:

```text
external_entry(runtime, user_args...):
    previous_runtime = pinned_runtime_register
    pinned_runtime_register = runtime
    result = pinned_body(user_args...)
    pinned_runtime_register = previous_runtime
    return result
```

The save and restore are required even when the selected register is
nonvolatile. Native callers expect a nonvolatile register to have the same value
after the call; the wrapper temporarily repurposes it but remains conformant by
restoring the incoming value before returning.

Existing native activation code establishes TLS through `EvalContextScope`
before it calls the generated wrapper. The wrapper copies that same explicit
context into the pinned register. Native C/C++ code therefore never needs to
read or write the hard register in the initial design.

For a generated boundary that intentionally switches context, one centralized
activation operation must update TLS and the pinned register as a pair and
restore both as a pair. The exact split between a native activation helper and
the generated wrapper is an implementation choice, but the internal pinned
entry must not run until:

```text
pinned MIR context == context TLS == logical active EvalContext
```

The internal pinned body is not a generally callable function pointer. It must
not be exposed directly to native code, a generic dispatcher, an asynchronous
callback, or code compiled with the hidden-context convention. Those callers
must hold the outward wrapper or another explicitly typed bridge.

Relevant boundary cases are:

| Transition | Required wrapper behavior |
|---|---|
| native C/C++ to MIR | Accept the explicit context, require (and optionally assert) that TLS was established by the native activation scope, save the incoming hard register, install the pinned context, invoke the internal body, restore the hard register on every supported exit, and return |
| hidden-context MIR to pinned MIR | Use a bridge with the old hidden-context prototype; install the hidden context into the pinned register, invoke the pinned body, and restore the previous register value |
| callback or suspended computation resuming in MIR | Recover the callable, realm, generator, or task owner context; establish TLS; then enter through the generated boundary wrapper |
| pinned MIR to a different `EvalContext` | Save the current TLS/register pair, install the new pair, invoke the target through its pinned entry, and restore both values on every supported exit |
| pinned MIR to an ordinary native helper | Normally use no wrapper; the helper reads TLS, ignores the pinned register, and preserves the selected nonvolatile register under the platform ABI |
| native helper reentering MIR | Reentry uses the generated boundary wrapper; it must not jump directly to the pinned body even if another pinned activation exists below it |
| pinned MIR to pinned MIR in the same context | Use no wrapper and pass no hidden context; call the internal pinned body directly |
| pinned MIR to hidden-context MIR | Use an explicit bridge that reads the pinned context and supplies it as the callee's hidden argument |
| pinned MIR returning to native code | Restore the hard-register value observed at the external entry before returning to the native caller |

A V1 implementation needs at least the native-to-MIR outward wrapper and any
bridges required by its selected closed call region. Full V1 production support
additionally requires callback, resumption, cross-context, indirect-call, and
mixed-convention wrappers.

### 7.3 V2: eval-thread invariant design

V2 replaces per-activation context switching with a stronger lifetime
invariant:

> From evaluation-thread initialization until that evaluation thread is torn
> down, TLS `context` always points to the same `EvalContext`, and every pinned
> MIR function executed by that thread observes that same pointer.

V2 depends on all of these assumptions:

- an evaluation thread has exactly one live `EvalContext` during an evaluation
  lifetime;
- the `EvalContext` is bound when the evaluation thread is initialized and is
  not replaced until teardown;
- Lambda cross-thread work travels through the message channel; generated
  functions on different threads never call each other directly;
- a receiving evaluation thread processes the message under its own already
  bound `EvalContext`;
- JavaScript has no built-in shared-memory execution threads that directly call
  generated functions in another evaluation thread;
- all participating MIR-Direct modules reserve the same context register;
- native C/C++ code continues to read `EvalContext*` from TLS;
- no nested runtime, realm, guest, cleanup scope, or callback binds a different
  `EvalContext` on the same evaluation thread while pinned MIR is reachable.

Under these assumptions:

- MIR-to-MIR direct and indirect calls need neither a context argument nor a
  boundary wrapper;
- closures, methods, generators, and async functions need no context transport
  when they resume entirely inside the same pinned MIR execution domain;
- cross-module MIR calls need no wrapper when every module reserves the same
  register;
- MIR-to-native helper calls need no wrapper because native code uses TLS;
- returning from a native helper to its MIR caller needs no wrapper because the
  platform ABI restores the MIR caller's nonvolatile register value;
- cross-thread dispatch does not transfer a register value; the receiving
  evaluation thread uses its own TLS/context-register pair.

This is expected to remove the large majority of V1 wrappers. The POC should
measure the actual ratio instead of treating “99%” as an acceptance premise.

#### 7.3.1 Why C-to-MIR still needs an entry adapter

A nonvolatile or callee-saved register is guaranteed to have its incoming value
when a native C function returns. It is not guaranteed to remain unchanged
during that function's body.

Compiler-unaware C code may legally:

1. save the incoming `r19`/`r15`;
2. use that register for a native local value;
3. call a generated MIR function while the local value is still in the
   register;
4. restore the incoming register only when returning to its own caller.

The nested MIR call would observe the C local value rather than `EvalContext*`.
Consequently, every compiler-unaware C-to-MIR transition needs an adapter even
when TLS and the logical context never change.

The V2 adapter reloads the pinned value at the exact C-to-MIR edge:

```text
c_to_mir_entry(target, explicit_runtime_or_null, user_args...):
    previous_hard_register = pinned_runtime_register
    runtime = context_tls
    assert runtime != null
    assert explicit_runtime_or_null == null
        || explicit_runtime_or_null == runtime
    pinned_runtime_register = runtime
    result = target(user_args...)
    pinned_runtime_register = previous_hard_register
    return result
```

Phase 1 always reloads the hard register from TLS. For internal runtime C code,
the adapter performs no TLS synchronization because TLS is already canonical.
For a public or third-party C API, the API's explicit `EvalContext*` parameter
first establishes the supported TLS activation; the adapter verifies that the
parameter agrees with TLS and then installs the TLS value in the MIR register.
Third-party code never accesses TLS or the pinned register directly.

This is one logical adapter policy, although the implementation may need small
typed or per-arity thunks to preserve each native function signature. All
C-visible generated function pointers must name an adapter, never a raw pinned
body.

#### 7.3.2 Remaining V2 boundary cases

| Transition | V2 handling |
|---|---|
| pinned MIR to pinned MIR, including indirect and cross-module calls | No wrapper when all code reserves the same hard register and uses the pinned ABI |
| pinned MIR to an ordinary native helper and back | No wrapper; the helper reads TLS and the standard ABI restores the MIR caller's register on return |
| native runtime C helper calling or reentering MIR | Use the C-to-MIR adapter, loading the register from TLS |
| MIR interpreter or other native dispatcher calling MIR | Use the C-to-MIR adapter because the interpreter/dispatcher is compiler-unaware C/C++ |
| event loop, DOM event, timer, promise job, or native async callback entering MIR | Use the C-to-MIR adapter at the callback dispatch point; no distinct context-switch wrapper is needed |
| generator or async continuation resumed directly by pinned MIR | No wrapper |
| generator or async continuation resumed by native scheduling code | Use the same C-to-MIR adapter |
| cross-thread message received by an evaluation thread | Bind/verify that thread's lifetime context, then use the C-to-MIR adapter if native dispatch enters MIR |
| public or third-party C API entering MIR | The supported API validates/establishes its explicit `EvalContext*`, then invokes the C-to-MIR adapter |
| hidden-context, older cached JIT, C2MIR, or other unpinned generated code entering pinned MIR | Use an explicit compatibility bridge or declare the combination unsupported |
| pinned MIR entering hidden-context generated code | Use a compatibility bridge that supplies TLS/the pinned value as the hidden argument, or declare the combination unsupported |
| native signal handler calling MIR | Use the C-to-MIR adapter; a handler that does not call MIR needs no context work |
| non-local jump or unwind landing directly inside pinned MIR | Requires a dedicated landing rule or is unsupported; it must not bypass register establishment |
| same-thread code binding a different `EvalContext` while MIR is reachable | Violates V2; use V1 for that flow or remove the rebinding |

Thus the remaining common boundary is not “untrusted C” specifically. It is
**any compiler-unaware native C/C++ code that calls MIR**, including Lambda's own
runtime dispatchers. Merely reading context through TLS does not make a nested
C-to-MIR call safe, because the C compiler may be using the pinned hard register
for another value at that call site.

#### 7.3.3 Required lifetime-invariant audit

The current runtime permits explicit TLS rebinding, so V2 is a proposed stronger
invariant rather than a fact already guaranteed by the implementation.
Examples that must be audited include:

- `EvalContextScope`, `eval_context_bind()`, and `eval_context_restore()` in
  runtime activation and cleanup;
- JavaScript `require`, `eval`, module compilation, and DOM selection scopes;
- Radiant event callbacks;
- Jube and other guest-language activation;
- runner teardown/cleanup and any worker-thread reuse.

Some current bindings may rebind the same canonical pointer or occur while no
pinned MIR is reachable. Those are compatible with V2 after verification.
Any path that binds a different pointer and then reaches MIR must instead:

- be changed to preserve the evaluation thread's lifetime context;
- enter through a V1 context-switch wrapper; or
- remain outside the pinned-register mode.

The POC should add diagnostic assertions/counters:

```text
thread_eval_context is assigned once at evaluation-thread initialization
every active eval_context_bind(next) requires next == thread_eval_context
every C-to-MIR adapter requires context_tls == thread_eval_context
every pinned MIR module reserves the selected hard register
```

Thread-pool reuse is compatible only when each new evaluation lifetime performs
a fresh bind before any MIR entry. The register value is never sent through the
message channel and is never inherited as another thread's logical state.

#### 7.3.4 Phase 1 C-to-MIR edge census

A source audit of the current Lambda and LambdaJS MIR-Direct paths found eight
logical native-to-generated dispatch families. The implementations contain
about 90 relevant typed ABI call arms because argument count, closure environment,
result-home, and context/public ABI shapes are expanded separately.

| Domain | Native-to-generated family | Current choke point | Approximate typed ABI arms |
|---|---|---|---:|
| Lambda | program and imported-module `main` entry | `runtime/runner.cpp`, cached execution in `runtime/transpile-mir.cpp` | 3 |
| Lambda | view/edit template body entry | `runtime/template_registry.cpp`, `runtime/render_map.cpp` | 3 |
| Lambda | first-class generated function dispatch | `fn_call_into()` in `runtime/lambda-eval.cpp` | 17 |
| Lambda | boxed generated-call trampolines | `fn_call_boxed_0_into()` through `fn_call_boxed_8_into()` | 9 |
| LambdaJS | generic generated function dispatch | MIR-context branch of `js_invoke_fn_raw()` in `js/js_runtime.cpp` | 32 |
| LambdaJS | specialized ordinary-call lanes | MIR-context/public instantiations of `js_entry_invoke_body()` in `js/js_runtime.cpp` | 10 |
| LambdaJS | generator and async state-machine entry/resume | generator/async dispatch in `js/js_runtime.cpp` | 5 |
| LambdaJS | script, module, dynamic-function, and eval `js_main` entry | JS MIR entrypoint, module, runtime, and eval lowering files | 11 |
| **Total** | **8 logical families** | | **about 90 arms** |

This is a static source census, not the number of transitions executed by a
workload. It excludes:

- direct MIR-to-MIR calls;
- native function pointers that do not point to generated MIR;
- frozen C2MIR;
- Bash, Ruby, Python, Jube, and other guest-language MIR paths outside this
  proposal's Lambda/LambdaJS scope.

The 90 arms do not require 90 independent hand-written designs. They can use
shared macros/templates or generated typed thunks implementing one adapter
policy. The performance risk is nevertheless real because a few families are
hot:

- every JavaScript dynamic call that reaches the generic or specialized native
  call lane reenters generated MIR;
- Lambda first-class/dynamic calls reenter through `fn_call_into()` or a boxed
  trampoline;
- generator and async state machines reenter on each native-scheduled resume;
- template bodies can reenter repeatedly during rendering.

Top-level `main`, module initialization, and dynamic compilation entries are
usually low-frequency and are unlikely to dominate.

Before implementing the full Phase 1 adapter set, add per-family counters to the
hidden-argument baseline. Record both:

```text
C-to-MIR adapter candidates per workload
generated MIR-to-MIR calls per workload
```

The ratio answers whether adapter overhead can overwhelm the saved hidden
arguments. The Phase 1 release A/B test must include the actual TLS load plus
hard-register save/install/restore sequence; a counter-only or idealized
register-already-correct result is not sufficient.

Where a current C-visible generated ABI already carries an explicit
`Context*`, Phase 1 still treats TLS as canonical and asserts equality. A later
micro-optimization may copy the already loaded explicit argument instead of
issuing a separate TLS address/load sequence, but that must be measured and must
not weaken the entry invariant.

#### 7.3.5 JavaScript `eval()`

Current JavaScript `eval()` is not an exception to the one-`EvalContext`
invariant.

The direct-script path creates a new `MIR_context_t` with `jit_init(0)` so the
dynamically compiled code has its own MIR compilation/code-lifetime container.
It then invokes the resulting `js_main` with the existing TLS `context`:

```text
new MIR_context_t != new EvalContext
js_main((Context *)context) uses the parent evaluation context
```

Expression-form eval and dynamically constructed functions likewise execute
under the current runtime context. Fresh module-variable/global scopes used by
some `node:vm` paths are JavaScript environment state, not a new
`EvalContext`.

Phase 1 handling for eval is therefore:

- every eval-created MIR module reserves the same pinned hard register;
- the native eval compiler/dispatcher enters eval's `js_main` through the
  ordinary C-to-MIR adapter;
- the adapter reloads the same parent `EvalContext*` from TLS;
- closures returned from eval retain callable code/module lifetime but do not
  acquire a child `EvalContext`.

If a future or currently undiscovered eval path actually allocates and binds a
different `EvalContext` on the same thread, that path requires a V1
save/switch/restore wrapper and violates V2. The preferred direction is to keep
eval on the parent `EvalContext`, matching the current direct-eval
implementation.

### 7.4 Generated-function context transport

Context transport should be explicit in compiler metadata rather than inferred
from a null argument or function name. A conceptual policy is:

```text
HIDDEN_CONTEXT     current ABI; context is the first hidden argument
PINNED_INTERNAL    no hidden argument; pinned register is already installed
PINNED_BOUNDARY    wrapper accepts/resolves context and installs the register
```

The POC may implement only the first two policies plus one controlled host
boundary. A production design needs all three.

For V2, `PINNED_INTERNAL` is the normal generated ABI and
`PINNED_BOUNDARY` is needed only for C-visible entry adapters or explicit
V1/compatibility flows.

### 7.5 Emitter integration

For a pinned internal function:

- create the global MIR register when the function is built;
- make `em->frame.runtime` refer to that MIR register;
- reuse existing context-field emission helpers;
- omit the hidden context from eligible direct-call prototypes and operands;
- keep calls to native helpers unchanged.

This limits code duplication. Context-field loads continue to use the existing
emitter abstraction; only the source of `frame.runtime` changes.

### 7.6 Direct calls

A direct MIR-to-MIR call may omit the hidden context only when all of the
following are true:

- caller and callee use the pinned internal convention;
- both reserve the same hard register;
- no boundary may switch `EvalContext` between caller and callee;
- the callee cannot be reached through an entry that expects the old prototype.

If any condition is uncertain, the call uses the existing hidden-context path.
The POC must not guess based on function shape alone.

### 7.7 Indirect calls

An indirect function pointer cannot safely imply a context convention unless
the callable metadata says which entry kind it names. Production function
objects therefore need either:

- a boundary-entry pointer whose ABI is stable; or
- an entry-kind tag plus enough owner/context metadata to select and install
  the correct context.

Under V1, the POC may leave generic and indirect calls on the hidden-context
ABI. Under V2, generated callable objects may point directly to pinned bodies
when all invocation paths are generated MIR; C-visible or mixed-ABI function
pointers must point to the C-to-MIR adapter.

## 8. Native C/C++ Boundary: TLS First

Native C/C++ helpers continue to access the active context through:

```c
extern __thread EvalContext *context;
```

or the platform abstraction that represents the same TLS binding. Generated MIR
does not start passing the pinned register as an undocumented C helper argument,
and native helpers do not read the hard register.

The invariants during ordinary generated execution are:

```text
pinned MIR context == context TLS == logical active EvalContext
```

The transports are nevertheless used by different code:

- MIR instructions use the pinned register;
- native C/C++ helpers use TLS;
- boundary code is responsible for keeping them synchronized.

### 8.1 MIR calling C

The selected POC register should be nonvolatile under the platform ABI. An
ordinary conforming C function must restore the value before returning to MIR,
although it neither knows nor cares that MIR uses it for a context. It may
temporarily use the register inside its own body, which is why any nested
C-to-MIR call still requires the V2 entry adapter.

If a native helper intentionally enters a nested `EvalContext`, calls generated
code, or schedules generated work, it must use a generated-code boundary.
Under V1 that boundary may switch the context pair. Under V2 it is the standard
C-to-MIR adapter and must observe the same evaluation-thread context; binding a
different pointer violates the V2 invariant.

### 8.2 C calling or reentering MIR

All compiler-unaware native-to-generated entries require an adapter, even when
another pinned activation exists below them. This includes callbacks invoked:

- after the original generated activation returned;
- from a third-party library;
- from an event loop or worker thread;
- recursively while another `EvalContext` is active.

Under V1, the wrapper obtains the correct context from its explicit owner
metadata, explicit argument, or TLS and may switch the active context pair.
Under V2, the adapter reloads the pinned register from the invariant TLS context;
only a supported public API may supply the same pointer explicitly.

### 8.3 Why C remains on TLS initially

Some C compilers support pinning a file-scope global register variable. GCC, for
example, supports GNU global register variables such as:

```c
register EvalContext *runtime_context asm("r15");
```

and supports reserving a register with target-specific compiler options such as
`-ffixed-r15`. This is not equivalent to a portable C language feature.

It is intentionally excluded from the initial design because it:

- is compiler- and target-specific;
- changes register allocation for every affected translation unit;
- can conflict with separately compiled libraries unaware of the convention;
- has different hazards for call-saved and call-clobbered registers;
- complicates callbacks, signals, non-local jumps, sanitizers, and unwinding;
- would make the MIR experiment inseparable from a much larger native-runtime
  ABI experiment.

See [GCC global register variables](https://gcc.gnu.org/onlinedocs/gcc-13.1.0/gcc/Global-Register-Variables.html).

### 8.4 Phase 2: pin native C to the same register

If MIR-only pinning proves valuable, a later tuning project may evaluate
compiling selected native runtime code with the same context register. That
could reduce TLS loads in particularly hot helpers and make transitions between
MIR and controlled native code cheaper.

The primary Phase 2 performance goal is to eliminate the Phase 1 reload on
internal runtime C-to-MIR edges. If every native function on a controlled call
path reserves the hard register and preserves its `EvalContext*` meaning, a
native dispatcher can call pinned MIR directly.

Phase 2 does not eliminate every adapter:

| Origin of C-to-MIR call | Phase 2 policy |
|---|---|
| pinned Lambda runtime C calling pinned MIR | no reload when the entire active native call path follows the pinned-C convention |
| pinned native helper called by MIR and reentering MIR | no reload |
| C translation unit compiled without the pinned convention | retain the Phase 1 TLS-to-register adapter |
| third-party library callback into Lambda | retain the adapter because the library may use the hard register temporarily |
| OS, event-loop, or embedding callback with an unpinned native caller | retain the adapter at the first Lambda-controlled entry |
| public C API | retain the supported API adapter unless the caller explicitly adopts a documented custom ABI |
| signal handler or non-local landing | retain dedicated handling or keep unsupported |

#### 8.4.1 How much Phase 1 reload machinery Phase 2 removes

The typed ABI arms remain necessary to marshal different argument counts,
closure environments, and result homes. What Phase 2 can remove is the
TLS-to-hard-register reload and save/restore sequence from those arms.

If the complete Lambda/LambdaJS native runtime execution domain is compiled
under the pinned-C convention, the best-case result is:

```text
8 of 8 logical C-to-MIR families become internally reload-free
about 90 of 90 typed ABI arms omit the Phase 1 reload
```

The adapters move to the outer edge of the pinned domain rather than remaining
on every generated call:

| C-to-MIR family | Phase 2 internal reload | Remaining outer boundary |
|---|---|---|
| Lambda program/imported-module `main` | removed | evaluation/thread entry installs the register once |
| Lambda template body | removed | unpinned UI/render callback entry, if any |
| Lambda first-class generated function dispatch | removed | unpinned caller entering the runtime domain |
| Lambda boxed generated-call trampolines | removed | none for MIR-to-pinned-C-to-MIR calls |
| LambdaJS generic generated dispatch | removed | external/native callback entering JS dispatch |
| LambdaJS specialized ordinary-call lanes | removed | external/native callback entering JS dispatch |
| generator and async state-machine resume | removed | OS/event-loop callback entering the pinned scheduler domain |
| script/module/dynamic/eval `js_main` | removed | top-level evaluation, public API, or unpinned module entry |

This does not mean the runtime has no adapters. It replaces many typed,
potentially hot adapters with a small set of coarse activation adapters:

- evaluation-thread/runtime entry and exit;
- OS or event-loop callback entry;
- third-party/native embedding callback entry;
- public C API entry;
- compatibility entry from unpinned guest or legacy generated code.

A staged Phase 2 can target the hot MIR-to-C-to-MIR dispatch loops first:

| Phase 2 scope | Families made reload-free | Approximate arms | Share of audited arms |
|---|---:|---:|---:|
| Lambda dynamic + boxed dispatch; JS generic + specialized dispatch | 4 of 8 | 68 of 90 | 76% |
| above plus generator/async resume | 5 of 8 | 73 of 90 | 81% |
| complete pinned Lambda/LambdaJS runtime domain | 8 of 8 | 90 of 90 | 100% |

The first row is attractive because these calls commonly have the shape
MIR-to-native-dispatch-to-MIR and can be hot. It is safe only when every native
frame between the pinned MIR caller and reentered MIR follows the pinned-C
convention. Generator/async resume additionally requires the native scheduler
path to be inside a pinned domain established by its outer event-loop adapter.

Compiling only the immediate C helper with a pinned variable is insufficient if
an unpinned function or library can call it while holding another value in the
hard register. Phase 2 safety is a property of the complete active native call
chain up to the next trusted adapter.

This future experiment must be separate from the Phase 1 POC. It would require:

- a supported compiler and architecture matrix;
- one authoritative hard-register mapping shared by MIR and native builds;
- compiler flags reserving the register in every relevant translation unit;
- an audit of all linked code that may execute while the register is live;
- explicit treatment of callbacks through unaware libraries;
- signal, exception, `setjmp`/`longjmp`, sanitizer, profiler, and debugger tests;
- proof that system ABI requirements are not violated;
- A/B results that isolate saved TLS loads from increased register pressure;
- a clean fallback to TLS for unsupported compilers and platforms.

Possible scopes, from least to most invasive, are:

1. tiny hand-written assembly or compiler-specific bridge stubs;
2. a small, closed set of hot native helpers compiled under the custom
   convention;
3. the Lambda native runtime as a whole.

#### 8.4.2 Clang feasibility

Clang can support the Phase 2 experiment, but it is a target- and
compiler-version-specific build convention rather than a portable C feature.
Two mechanisms are required:

1. reserve the hard register in every participating translation unit with the
   target's supported `-ffixed-*` option; and
2. expose the value to selected C/C++ source through a GCC-compatible file-scope
   register declaration, for example:

```c
register EvalContext *runtime_context asm("x20");
```

On the workspace's Apple Clang 17.0.0 / arm64-apple-darwin toolchain, a local
compile probe found:

- `-ffixed-x20` is accepted;
- a global register declaration naming `x20` compiles when that flag is present;
- reading the variable lowers directly from `x20`;
- the same declaration without the fixed-register flag fails in the backend;
- `-ffixed-x19` is rejected for the active macOS target despite appearing in
  that compiler's generic help output.

Current upstream Clang documentation also lists the available fixed-register
options by target, and the exact set differs from this installed Apple Clang.
The build must therefore perform a compile probe rather than infer support from
`clang --help` or compiler family alone.

A standard-ABI C function cannot establish the pinned value for its caller by
assigning the global register variable. For a nonvolatile register, Clang's
normal function epilogue restores the caller's incoming value. Phase 2 still
needs a MIR/assembly activation trampoline to install the context before
entering the pinned-C domain and to restore the external ABI value on exit.
Pinned C code can then read and preserve the installed value.

For AArch64, this suggests:

- keep `r19` as a valid MIR-only Phase 1 candidate;
- use an `x20`/MIR-`r20` candidate for a shared MIR/Clang Phase 2 experiment
  unless the selected production Clang is proven to support `-ffixed-x19`;
- compile every participating native translation unit with the same
  fixed-register option, including LTO builds;
- retain adapters around unpinned libraries and external callbacks.

See the [Clang target-dependent fixed-register options](https://clang.llvm.org/docs/ClangCommandLineReference.html#target-dependent-compilation-options).

The third option should not be assumed desirable. Native code has different
register-pressure and library-interoperation characteristics from generated MIR.
Until such a project is approved and validated, TLS remains the only supported
native C/C++ access path.

## 9. Target Register Selection

The POC should start on one architecture rather than prematurely defining a
cross-platform ABI.

| Target | Provisional register | Reason | Status |
|---|---:|---|---|
| AArch64 | `r19` | nonvolatile under the standard ABI and recognized by MIR | first POC candidate |
| x86-64 | `r15` | nonvolatile under common x86-64 ABIs | future measurement required |
| Windows x64 | TBD | must verify MIR names, unwind data, and ABI interaction | unsupported by first POC |
| other targets | TBD | requires target-specific audit | unsupported by first POC |

The table is a hypothesis, not a committed ABI. Before implementation, the POC
must verify:

- MIR's exact hard-register spelling;
- that the register is not reserved by the OS/platform ABI;
- that it is nonvolatile across ordinary native calls;
- that MIR excludes it consistently from allocation in every generated module;
- generated unwind metadata remains valid.

Apple AArch64 reserves platform register `r18`; it must not be used for this
purpose. x86-64 has fewer general-purpose registers and may suffer more from
reserving one, so an AArch64 win cannot be generalized to x86-64.

## 10. Threading and Activation Safety

A physical CPU register is naturally part of each thread's machine state. The
OS saves and restores it during thread context switches. That property prevents
two OS threads from directly sharing one register value.

It does not make the runtime convention automatically thread-safe.

V1 installs the `EvalContext` belonging to each activation. That remains
necessary for any flow that can switch contexts within one thread.

V2 instead makes evaluation thread and context a lifetime pair:

```text
evaluation-thread lifetime <-> one stable EvalContext*
```

The context is bound once during evaluation-thread initialization and is not
replaced until teardown. Cross-thread Lambda calls use the message channel, so
no sender register value crosses threads. A receiving evaluation thread
processes the message under its own lifetime context. JavaScript asynchronous
jobs return to the owning evaluation thread rather than directly executing in
another JavaScript thread.

A native message receiver, event loop, interpreter, or callback dispatcher must
still use the C-to-MIR entry adapter. This is required because compiler-unaware
C may temporarily occupy the hard register, not because the logical
`EvalContext` changed.

V2 is invalid when:

- a worker thread runs jobs for different contexts sequentially;
- a task resumes on a different worker without starting a fresh evaluation
  lifetime on that worker;
- generated code reenters recursively under another context;
- a callback binds a different context on the same thread;
- a nested guest or cleanup scope swaps TLS while pinned MIR is reachable.

Thread-pool reuse can satisfy V2 only by ending the previous evaluation lifetime
and binding the next context before any new MIR entry. The entry adapter should
assert the binding in diagnostic builds.

The two correct statements are:

> Register storage is thread-local machine state, but logical context selection
> remains explicit under V1.

> Under V2, logical context selection occurs once per evaluation-thread
> lifetime, while compiler-unaware C-to-MIR edges reload the register from TLS.

TLS and the pinned register must agree whenever pinned MIR is executing. V1
maintains this by paired context switching. V2 maintains it through the stable
TLS invariant plus C-to-MIR adapters.

## 11. Production Correctness Flow Inventory

The following table is the conservative V1 inventory. The performance POC does
not need to implement all of these paths, but a production rollout must
explicitly resolve each one. Under V2, most rows collapse to either “remain
inside pinned MIR with no wrapper” or “enter from native code through the common
C-to-MIR adapter,” as detailed in Section 7.3. Any row that changes the
same-thread `EvalContext*` cannot use V2 without redesign or a V1 fallback.

| Flow | Required production handling | POC policy |
|---|---|---|
| Lambda top-level entry | boundary installs owner context | one controlled boundary |
| Lambda direct function call | pinned-to-pinned call omits hidden arg | eligible |
| Lambda indirect function call | stable wrapper or tagged entry kind | hidden fallback |
| Lambda closure invocation | owner context retained and installed | hidden fallback unless proven local |
| Lambda method/procedure wrapper | wrapper preserves entry convention | hidden fallback |
| cross-MIR-module call | every module reserves the same register | hidden fallback initially |
| imported Lambda module | module owner/context and register policy agree | hidden fallback |
| Lambda error propagation | restoration on every normal error return | limited measured paths |
| JavaScript program entry | realm's context installed at boundary | one controlled boundary |
| JS direct compiled call | pinned-to-pinned call omits hidden arg | eligible |
| JS native-call variant | C helper reads TLS; register survives call | eligible after audit |
| JS generic dispatcher | dispatch metadata selects correct entry | hidden fallback |
| `call`, `apply`, and bound functions | wrapper installs target owner context | hidden fallback |
| Proxy and exotic call | generic boundary required | hidden fallback |
| constructors/classes | constructor and method entries agree | hidden fallback |
| closures and lexical environments | callable owner context retained | hidden fallback unless proven local |
| generator creation | capture owner/context metadata | hidden fallback |
| generator resume/throw/return | resume boundary reinstalls context | hidden fallback |
| `yield` and `yield*` | no stale register assumed across suspension | hidden fallback |
| async function entry | normal boundary/direct-call rule | hidden fallback |
| `await` continuation | resume installs captured context | hidden fallback |
| promises and microtasks | queued job carries and installs context | hidden fallback |
| timers, animation, and DOM events | callback carries owner and enters wrapper | hidden fallback |
| libuv/event-loop callbacks | callback boundary installs context | hidden fallback |
| worker or task-pool jobs | per-job installation on execution thread | hidden fallback |
| task migration between threads | never depend on prior thread register | hidden fallback |
| `eval` and `Function` constructor | new compiled module reserves convention | hidden fallback |
| `require` and dynamic import | module boundary and owner context | hidden fallback |
| separate MIR contexts/modules | identical register reservation policy | hidden fallback |
| template/render callbacks | callback boundary reinstalls context | hidden fallback |
| Jube/guest-language callback | language boundary wrapper | hidden fallback |
| native C helper | helper reads TLS; standard ABI preserves register | eligible after ABI audit |
| C callback into MIR | generated boundary installs pinned value | one controlled path only |
| nested `EvalContextScope` | save/install/restore TLS and register together | hidden fallback |
| realm/document switch | explicit activation boundary | hidden fallback |
| inlining | inlined body uses caller's pinned register | eligible |
| tail call | must preserve entry convention and context | hidden fallback |
| ordinary return | restore only at boundary, not internal calls | eligible |
| exception/unwind | cleanup restores boundary state | hidden fallback |
| `setjmp`/`longjmp` | define whether and how pinned value is restored | unsupported in POC |
| signal handler | must not assume or mutate context convention | unsupported in POC |
| stack overflow recovery | boundary cleanup remains reliable | unsupported in POC |
| MIR interpreter | interpreter global-reg semantics are not native ABI | hidden fallback |
| pure interpreter execution | continues using explicit/TLS context | unchanged |
| interpreter/JIT transition | explicit wrapper; no implicit shared register | hidden fallback |
| C2MIR | frozen | unsupported |
| deferred MIR teardown | no callback can retain stale entry/context | hidden fallback |
| debugger/profiler/sanitizer | register and unwind behavior validated | release POC only; later audit |

The fallback is part of the design. A flow must be entirely pinned-aware or use
the existing hidden-context ABI; it must not partially omit context transport.

## 12. GC and Rooting

This proposal does not make the native stack a GC root source.

`EvalContext*` is runtime state, not an `Item` root range. Values that must
survive allocation or collection continue to use precise `RootFrame`/`Rooted`
ownership. Reserving a register for the context neither roots generated
arguments nor changes the liveness rules for heap objects reachable from local
values.

If argument-rooting optimization is pursued later, it must prove precise range
identity and lifetime independently. The context-register POC must not combine
that change with its performance comparison.

## 13. Performance POC

### 13.1 POC question

Compare:

- **A — hidden argument:** the current MIR Direct ABI;
- **B — V2 pinned register:** generated calls omit the context argument, read
  `EvalContext*` from the reserved register, and use adapters only at
  compiler-unaware C-to-MIR entries.

No other optimization should differ between A and B.

### 13.2 POC platform

Start with release builds on macOS AArch64 using the verified `r19` MIR hard
register name. Other targets remain on A until separately implemented and
measured.

### 13.3 Build-time selection

Use a build-time feature switch such as:

```text
LAMBDA_MIR_CONTEXT_REGISTER_POC=0
LAMBDA_MIR_CONTEXT_REGISTER_POC=1
```

The switch must select code generation, not add a runtime branch to each call.
It should be configured through the repository's supported build configuration,
not by manually editing generated `.lua` files.

Build both release binaries before timing begins and retain them under:

```text
temp/context-register-poc/hidden/lambda.exe
temp/context-register-poc/pinned/lambda.exe
```

Record the commit, build configuration, architecture, compiler version, MIR
revision, feature setting, and checksum for each binary.

### 13.4 Minimal implementation

The smallest useful POC is:

1. record the evaluation thread's invariant `EvalContext*` when its TLS binding
   is initialized;
2. reserve the selected hard register in every participating generated module;
3. make `em->frame.runtime` refer to the global register in pinned bodies;
4. emit context-free prototypes and operands for generated direct and eligible
   indirect calls;
5. route every native runtime, interpreter, callback, and public-API C-to-MIR
   edge used by the measured suites through the common entry-adapter policy;
6. load the adapter's context from TLS for native runtime calls and from the
   validated explicit parameter for public API calls;
7. leave native C/C++ helpers unchanged on TLS;
8. assert that active TLS rebinding never selects a different `EvalContext`
   during the evaluation-thread lifetime;
9. retain V1/hidden compatibility bridges only for mixed generated code that
   cannot join the V2 domain.

This POC is not a production migration. It may conservatively exclude functions
when entry provenance is uncertain. Participating pinned functions should form
a closed ABI domain. Every edge from compiler-unaware C or from a hidden/unpinned
generated domain must use an explicit adapter. No per-callback context-switch
wrapper is added when the V2 lifetime invariant holds.

### 13.5 Coverage counters

Timing without knowing how often the candidate path ran is ambiguous. Add
diagnostic counters, disabled by default, for:

- generated pinned internal functions;
- pinned direct calls;
- pinned indirect calls;
- hidden-context fallback calls;
- C-to-MIR adapter entries and restorations;
- attempted TLS bindings to a different `EvalContext`;
- V1/compatibility bridge calls;
- native helper calls made while pinned mode is active.

Counters must be enabled only in separate coverage runs, not performance runs,
unless their cost is proven identical in A and B.

### 13.6 MIR validation

Before performance measurement, validate representative MIR dumps:

Pinned mode must show:

- `_runtime_ctx` bound to the selected hard register;
- no hidden context operand on generated calls within the V2 domain;
- context-field loads based on the global register;
- reservation of the same hard register throughout the generated module.
- explicit register installation and restoration in C-visible entry adapters.

Hidden mode must show:

- the current first hidden context parameter;
- unchanged direct-call forwarding.

Neither mode may introduce a C global register declaration or native compiler
`-ffixed-*` option.

### 13.7 Correctness floor

Although the POC intentionally excludes corner-case implementation, it cannot
time a candidate that computes different results.

For every timed workload:

- process exit status must match;
- output, excluding timing text, must match;
- Test262 pass/fail/skip sets must match;
- benchmark checksums or expected results must match;
- GC stress behavior used by an existing workload must not be disabled;
- no conservative stack scanning may be introduced.

A mismatch rejects that sample and the affected POC path. It is not acceptable
to classify wrong output as a performance win.

## 14. A/B Test 1: Test262

This is the project Test262/js262 integration runner, not a new microbenchmark.
Use release binaries only.

The canonical command is:

```sh
./test/test_js_test262_gtest.exe \
  --baseline-only \
  --batch-only \
  --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --verbose
```

The runner currently invokes the project `lambda.exe`. The POC should add a
harness-only executable override, or use a controlled installation step, so A
and B can be selected without rebuilding. The override must not affect the
runtime under test.

Protocol:

1. build and archive A and B before any timing;
2. fix worker/job count for the complete run;
3. run on the same plugged-in, otherwise idle machine;
4. alternate the starting binary by pair, for example `AB`, `BA`, `AB`, `BA`,
   `AB`;
5. enable `LAMBDA_JS_PHASE_TIMING=1` for diagnostic phase attribution when its
   overhead is identical in A and B;
6. preserve the production default for `LAMBDA_JS_LARGE_INTERP`;
7. compare identical pass/fail/skip and retry behavior.

Primary timing metrics:

- batched Test262 execution wall time at the fixed worker count;
- aggregate per-test execute time where the runner reports it.

Secondary metrics:

- total runner wall time;
- preparation time;
- synchronous versus asynchronous batch time;
- retry and non-batched time.

An optional sensitivity run with `LAMBDA_JS_LARGE_INTERP=0` may determine
whether interpreter policy diluted the generated-code effect. It is diagnostic,
not a replacement acceptance test.

## 15. A/B Test 2: Lambda and JavaScript Benchmarks

Use the canonical benchmark registry and release runner:

```sh
python3 test/benchmark/run_benchmarks.py \
  -e mir,lambdajs \
  -n 5 \
  --fresh \
  --results-output temp/context-register-poc/results.json
```

The existing runner repeats one selected executable. For this POC, add a small
A/B orchestration mode or companion driver that:

- accepts explicit A and B executable paths;
- runs the same benchmark row with A and B adjacent in time;
- alternates which variant starts each pair;
- does not rebuild between samples;
- records raw samples rather than only summaries;
- preserves the runner's output validation.

Run the full registered matrix for both `mir` and `lambdajs`. Do not select only
expected winners.

Primary metric:

- benchmark self-reported `__TIMING__` execution time.

Secondary metrics:

- external wall time;
- compile/setup time where available;
- peak memory or spill diagnostics when already measurable without perturbing
  timing.

Report:

- every row's paired B/A ratio;
- geometric mean for the MIR engine;
- geometric mean for the LambdaJS engine;
- confidence interval for each aggregate;
- the slowest regressions as well as the largest wins;
- pinned-call coverage from a separate diagnostic run.

## 16. Statistical and Decision Rules

Use paired samples because thermal state and background load can dominate small
JIT changes. Calculate paired B/A ratios and a 95% bootstrap confidence interval
over pairs or benchmark rows as appropriate.

The three headline aggregates are:

1. Test262 execution time;
2. Lambda benchmark MIR geometric mean;
3. JavaScript benchmark LambdaJS geometric mean.

Recommended decision gate:

- **Proceed to production design:** at least two aggregates improve by 2% or
  more, their confidence intervals exclude no change, and the remaining
  aggregate does not regress by more than 1%.
- **Run one narrowed follow-up POC:** exactly one aggregate has a clear material
  win, the others are neutral, and coverage data identifies a plausible
  under-tested hot path.
- **Reject universal pinning:** results remain within roughly ±1%, confidence
  intervals cross no change, or any headline aggregate regresses by 2% or more.

A neutral result with very low pinned-call coverage may justify broadening the
eligible direct-call set once. It does not justify implementing all production
corner cases before another measurement.

Per-architecture decisions are allowed. For example, AArch64 may use the
convention while x86-64 retains the hidden argument if register pressure makes
the latter faster.

## 17. Production Work After a Successful POC

If the POC passes the decision gate, the production phase must:

1. define stable target-register mappings and unsupported targets;
2. reserve the register in every relevant MIR module;
3. prove and enforce the V2 one-context-per-evaluation-thread invariant;
4. centralize all compiler-unaware C-to-MIR calls through typed entry adapters;
5. retain the V1 wrapper design for real context switches and compatibility
   domains;
6. define generator, async, task, worker, and callback resumption behavior;
7. define interpreter/JIT transitions without assuming physical-register
   semantics in the interpreter;
8. validate unwind, sanitizer, debugger, signal, and non-local-jump behavior;
9. run the full Lambda, JavaScript, DOM/UI, and runtime regression suites;
10. repeat release A/B performance tests on every enabled architecture;
11. document the resulting MIR internal ABI.

Only after MIR-only production deployment is stable should a separate proposal
consider pinning selected native C/C++ code to the same register.

## 18. Risks

### 18.1 Hidden argument may already be optimal

The allocator may already keep `EvalContext*` in a favorable register and
eliminate forwarding moves. In that case, reserving a register merely reduces
allocation freedom. Rejecting the proposal after a neutral or negative POC is a
successful result.

### 18.2 Register pressure differs by workload

Small call-heavy functions may win while numeric or parser loops spill more.
Aggregate results must be accompanied by per-row regressions.

### 18.3 Boundary cost can hide internal wins

Frequent native callbacks can add C-to-MIR adapter save/install/restore work.
Coverage counters are necessary to understand this. Under V2, realm or callback
context switching is not an accepted cost; switching to a different pointer
violates the lifetime invariant and requires V1.

### 18.4 Mixed ABI mistakes are severe

Calling a context-free entry without the correct pinned value can silently use
another realm or stale context. Entry-kind metadata and conservative fallback
are required; an unchecked cast between prototypes is not acceptable.

### 18.5 TLS and register divergence

Native helpers read TLS while MIR reads the register. Under V1, any boundary
updating one without the other creates inconsistent runtime state. Under V2,
TLS remains fixed and every C-to-MIR adapter reloads the register from it.
Unaware native code may temporarily use the hard register, but it must never
call a raw pinned body during that interval.

### 18.6 Platform and toolchain dependence

Hard-register names, reserved registers, unwind rules, and compiler behavior
are target-specific. No result from one platform establishes a universal ABI.

## 19. Open Questions

- Which generated call categories account for most hidden-context forwarding?
- Does MIR already coalesce the current context parameter into a stable
  callee-saved register in hot code?
- Is `r19` the best AArch64 candidate after spill analysis, or would another
  nonvolatile register interfere less with MIR allocation heuristics?
- Can every participating MIR module reserve the register without forcing every
  function to reference it?
- What is the least invasive stable wrapper ABI for indirect callable objects?
- How should the pinned policy be represented in existing function metadata?
- Which existing activation helper should become the single TLS/register
  synchronization point?
- Can every current `eval_context_bind()`/restore flow prove that it either
  rebinds the same canonical pointer or cannot reach pinned MIR?
- Can the approximately 90 Phase 1 ABI arms be generated from the eight
  audited adapter families without duplicating entry logic?
- What are the dynamic C-to-MIR counts for each adapter family in Test262 and
  the Lambda/LambdaJS benchmarks?
- Does using an already-carried, TLS-validated explicit `Context*` materially
  outperform a literal TLS reload at hot Phase 1 entries?
- Does Test262 spend enough time in eligible generated direct calls for a useful
  signal under the production large-script interpreter policy?
- If MIR-only pinning wins, which native helpers have enough TLS-load cost to
  justify the much riskier future C-register experiment?

## 20. Proposed Decision

Approve only the release-build performance POC.

The POC uses V2: native C/C++ remains on TLS, one `EvalContext*` is invariant for
the evaluation-thread lifetime, participating generated MIR uses the pinned
register without per-call wrappers, and compiler-unaware C-to-MIR entries use
the common adapter policy. V1 remains documented as the fallback for genuine
same-thread context switches and mixed generated ABIs.

Measure A/B performance with Test262 plus the complete Lambda/JavaScript
benchmark matrix, and record the adapter-to-pinned-call ratio to verify the
expected wrapper elimination.

Do not commit to a production ABI or native C register pinning until the POC
demonstrates a repeatable material win.
