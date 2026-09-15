# LambdaJS Call Flow — Flatten the Dynamic Call Chain to ≤4 Hops

**Date**: 2026-09-15

**Status**: PROPOSAL — not ratified. **P1–P3 implemented 2026-09-15
(uncommitted working tree)**: span entry + finalized body entry (Appendix C),
one kernel entry `js_call` (Appendix D), rooting dedup + kernel trims + O1–O5
(Appendix E). P4 open (evidence-gated). **P5–P6 (AST interpreter, §3.5,
JC20–JC22) implemented 2026-09-15 (uncommitted, Appendix F).** Measured against the working tree at `772ef7625`
(debug configuration = `-O3` + frame pointers, `premake5.mac.lua:21`).

**Scope**: the runtime path a JavaScript call takes from an emitted MIR call
site to the callee's compiled body — the C++ adapter/dispatch layers and the
two MIR functions emitted per JS function. Construction (`new`) shares the
tail of this path and is covered where it differs. **Out of scope**: what a
call site does *before* dispatch (property `Get`, argument evaluation, frame
slots — see `doc/dev/js/JS_03_Value_Model.md` §7), native/direct call
lowering, inlining, TCO, and per-site specialization of property access.

**Formal anchors**: D6.2.2v2 (per-callee `invoke`/`construct` capabilities;
`Item* + argc` is the JS dynamic boundary; rooted spans and scalar homes are
ownership-qualified adapters, not dispatch mechanisms), D8.4.2v2 (direct
calls pass individual operands; `Item* + argc` is dynamic-boundary only),
D5.2.1v3 (companion-lane returns; C-reachable wrappers use
`Context::mir_companion_slot`), D5.3.1/D5.3.3 (safepoint-current root
slots; native helpers root on the same side-root stack), D8.4.1v2 (no inline
caches; specialization = inline guard + shared kernel on miss), D8.4.3v2
(explicit completions, no frame skipped), CLAUDE rule 15 (precise rooting
only).

**Ledger series**: extends the callable area's `JC#` from
`vibe/jube/JS_Runtime_Callable.md` §4 — **JC13–JC19** below, **JC20–JC22** for the
AST interpreter (§3.5); no new series.
Prior design in this area: `vibe/impl/Lambda_Impl_Tune7_JS_Plain_Call
(done).md` (call lanes, C1–C4) and `vibe/impl/Lambda_Impl_Tune_JS_Dynamic_Call
(done).md` (DC1–DC7, per-callee entries). Both are inputs here; §2.4 records
which of their conclusions this proposal keeps and which it reverses.

---

## 1. The problem

A dynamic JS call today passes through **ten named functions** between the
emitted call site and the callee's body. Five of them own a machine frame;
three are tail jumps; two are inlined. Measured per recursion level on a
2-argument function (`temp/jscall/dyn.js`, lldb stack-pointer probe):

| Hop | Function | Frame | Role |
|---:|---|---:|---|
| 1 | `js_call_function_prerooted_args_into` | 0 (tail) | ownership-qualified adapter: reject NULL home, pass `prerooted=true` |
| 2 | `js_call_value` (inlined into 1) | 0 (tail `br`) | callee-type split: Lambda `Function` → `fn_call_into`; `JsFunction` → `fn->invoke`; else fall through |
| 3 | `js_call_entry_generic` (`fn->invoke`) | 0 (tail) | one-line forward adding `new_target = 0` |
| 4 | `js_call_function_impl_mode` | **704 B** | the semantic kernel (depth guard, rooting, activation, `this`, `new.target`, module/realm/`with` switching, result adoption) |
| 5 | `js_invoke_fn_with_source` (inlined into 4) | — | `arguments` source span; a second `RootSpan` (0 slots when prerooted) |
| 6 | `js_invoke_fn_raw_or_async` (inlined into 4) | — | legacy async: wrap an early ERROR into a rejected promise |
| 7 | `js_invoke_fn_raw` | 208 B | body-kind / native-policy / ABI dispatch; pad/rest adapter (`JsCallAdapterSpan`, a third `RootSpan`); corrupt-`func_ptr` check; context-owner check |
| 8 | `js_invoke_mir_context_by_count<HasEnv>` | 224 B | `switch (argc)` over 0–32 arities; each case a template spreading `args[i]` into register operands; `lambda_item_resolve_pending_slot` on return |
| 9 | MIR public wrapper `_js_f` | ~96 B | re-roots every parameter, calls the body, stores lane 2 into `Context::mir_companion_slot` |
| 10 | MIR body `_js_f_body` | 96 B | the function |
| | **Total** | **1,328 B** | vs **96 B** for a direct call (site → body) |

Only hop 4 and hop 10 carry semantics. Hops 1–3 are three names for "enter
the callee's `invoke`". Hops 5–8 exist to turn `Item* + argc` back into
register operands and to pick which of several body ABIs the callee has.
Hop 9 exists because a C caller cannot receive MIR's two-result return and
because the wrapper ABI wants register operands. About 85% of the stack
(1,136 B) and every branch between hops 4 and 10 is adapter, not semantics.

The chain is also the source of three standing problems:

- **Triple rooting.** The kernel roots the argument span; `with_source` roots
  it again (a 0-slot `RootSpan` once the kernel has already marked it
  prerooted); the adapter roots a third time when padding; then the public
  wrapper publishes each parameter into its own root slots and the body
  publishes them again. Two of these are dead work on the hot path.
- **Two arity limits at the dynamic boundary.** `JS_MIR_CONTEXT_CALL_MAX_ARITY
  = 32` and `LAMBDA_MAX_FUNCTION_ARGS` exist only because C must spread a
  span into registers. A generated span-consuming entry has no such limit.
- **Three compiled-body ABIs** (`MIR_CONTEXT_ABI`, legacy `MIR_PUBLIC_ABI`,
  hosted `lambda_hosted_item_invoke_by_count`) selected by flags per call,
  contrary to the spirit of JC1/JC4 (adapter chosen once at creation, no
  per-call mega-switch).

## 2. Analysis — what each layer is for, and whether it must be a hop

### 2.1 Hops 1–3: adapters that are already free, but still hops

D6.2.2v2 says caller-donated scalar homes and precise rooted spans are
*ownership-qualified adapters to the same entries*. The implementation
honors that (all three are tail jumps, 0 bytes), but the adapters are
separate symbols with separate forwards (`JS_FORWARD_LOCAL_RETURN`,
`JS_FORWARD_STATIC_ITEM`, `JS_FORWARD_ITEM`), and the callee-type split sits
in its own function. Nothing here needs a symbol boundary: the type split is
the kernel's first step, and the ownership facts are two operands.

### 2.2 Hop 4: the one kernel — keep it, and keep it singular

Tune7 (§8.2) and the Tune_JS_Dynamic_Call record (§0) established, with
release A/B, that a *thinner copy* of the kernel for the "ordinary" 83%
population measured **slower** than the generic kernel: its mostly-false
branches are predictable, and the lane classification cost more than the
skipped no-ops. The DC2 template family of ~40 entries was subsequently
collapsed back to one generic entry (`js_call_value` comment: "Tune7's
specialized templates duplicated its rooting, state switching, and restore
protocol while the release path did not benefit"). **This proposal does not
reopen that.** The kernel stays one function and one semantic authority;
its per-shape branches stay branches. What is removable in the kernel is
*structure*, not semantics: duplicated roots (callee is rooted twice —
`func_root` and `activation_callee_root`), a `super_this` root that is
`ItemNull` for every non-derived call, two dead `RootSpan`s, a local scalar
home used only by the retired public-ABI fallback, and the `common_lane`
classifier whose only surviving effect is the private-home-class install.

### 2.3 Hops 5–9: the adapter tail exists because the compiled ABI is wrong for C

Every layer from `with_source` to the public wrapper is a consequence of one
decision: the C-reachable MIR wrapper takes **register operands**
(`Item(Context*, [env,] Item p0 … pn)`). Given that ABI, C must (a) know the
arity to spread a span (hop 8's switch), (b) pad or pack the span first when
`argc ≠ formals` (hop 7's adapter), (c) pick the spreading template by ABI
and env presence (hop 7's flag tests), and (d) accept a one-result return
and fetch lane 2 from a Context slot (hop 9 + `resolve_pending_slot`).

MIR can consume a span directly: a generated function that takes
`Item* args, int64 argc` and loads `args[i]` into its own registers is a
`mov` per parameter, resolved at **compile time per function** — the
"spreading" that the arity switch does at run time per call. That single
move retires hops 7, 8 and 9 and both arity caps, and it is consistent with
D8.4.2v2: register operands remain the *direct-call* ABI (`_body`), and the
span entry *is* the `Item* + argc` dynamic boundary, now implemented in
generated code rather than in 66 C++ template instantiations.

### 2.4 Relation to prior rulings and designs

| Prior | Kept / reversed | Why |
|---|---|---|
| JC1, JC2, JC4, JC8 | kept | entries fixed at finalization; two capabilities; adapters chosen once; Get-then-Call |
| D6.2.2v2 `Item* + argc` boundary | kept | the span entry is that boundary |
| D8.4.2v2 register-operand direct calls | kept | `_body` unchanged; direct sites unchanged |
| D5.2.1v3 companion slot for C-reachable entries | kept | the span entry stores lane 2 into `Context::mir_companion_slot` exactly as the public wrapper does today |
| DC1 `fn->invoke` per-callee entry | kept | still the capability pointer; still bound vs ordinary |
| DC2 entry template family | **stays retired** | measured slower; one kernel (JC19) |
| DC6 monomorphic call-target cells | **retired by D8.4.1v2** | mutable per-site cache; replaced by JC18's inline guard + shared kernel |
| Tune7 C2.3 "fully pre-rooted entry" (never implemented) | subsumed | JC17 — the span is rooted once, by the kernel invariant |
| Tune7 C4.2/C4.3 exact-arity thunks (never implemented) | subsumed | JC15 — adaptation is generated per function; no thunk family |

## 3. Design

### 3.1 Target chain

```
                       today (10 named, 5 frames)             proposed (≤4 hops, 3 frames)
JIT call site ──▶ prerooted_into ─▶ call_value ─▶ entry_generic   JIT call site ──▶ js_call (JC13)
                  ─▶ impl_mode ─▶ with_source ─▶ raw_or_async      │  type split, then tail-enter fn->invoke
                  ─▶ invoke_fn_raw ─▶ by_count<N> ─▶ _js_f         ▼
                  ─▶ _js_f_body                                    kernel body  (== js_call for ordinary fns)
                                                                   │  depth · roots · activation · this/new.target
                                                                   │  module/realm/with · code->body_entry(...)
                                                                   ▼
                                                                   _js_f_span   (MIR, JC14/JC15/JC16)
                                                                   │  p_i = i<argc ? args[i] : undefined ; rest pack
                                                                   │  call _js_f_body ; lane2 → mir_companion_slot
                                                                   ▼
                                                                   _js_f_body   (unchanged)
```

Hop count: **JIT → `js_call` → `_js_f_span` → `_js_f_body` = 3 calls** when
`fn->invoke` is the ordinary entry (the tail-enter inside `js_call` is a
jump, not a frame). Bound functions add one hop (`js_call_entry_bound`
merges arguments and re-enters `js_call` on the target). Construction:
`js_construct_value → fn->construct → kernel(new_target) → span → body`, 4.
With JC18 (stage 2) a JIT site may call `fn->invoke` directly and the
ordinary case becomes 3 hops with no trampoline at all.

### 3.2 Rulings

| ID | Decision |
|---|---|
| **JC13** | **One kernel symbol.** `js_call(fn, this, args, argc, result_home, args_prerooted)` is the sole exported dynamic-call entry and the sole semantic authority for `[[Call]]`. Its first step is the callee-type split (Lambda `Function` → `fn_call_into`; Proxy → `apply` trap; non-callable → TypeError; `JsFunction` → `fn->invoke`). For an ordinary `JsFunction`, `fn->invoke` **is** the kernel body, so the split falls through without a call. `js_call_function`, `_into`, and `_prerooted_args_into` become inline forwards that only fix the two ownership operands; `js_call_value`, `js_call_entry_generic`, `js_call_function_impl_mode`, `js_invoke_fn_with_source`, and `js_invoke_fn_raw_or_async` cease to exist as separate functions. (The construct kernel keeps the same shape: `js_construct_value` → `fn->construct` → the same kernel body with `new_target` set.) |
| **JC14** | **One body entry per callable, one signature.** `JsCallableCode` gains `JsBodyEntry body_entry` with the signature `Item (Context* ctx, Item callee, Item this, Item* args, int64 argc, uint64_t* result_home)`. Compiled functions store their MIR-emitted **span entry** `<f>_span`; AST-interpreted functions store `js_interp_call_function` (signature-adapted once); native functions store their already-selected `JsNativeCode::call` adapter (signature gains the leading `Context*`; the typed factories set it once — JC4). The kernel invokes `code->body_entry` **once, unconditionally**: no body-kind, native-policy, ABI, env, or arity switch at call time. `js_invoke_fn_raw`, `js_invoke_mir_context_by_count`, `js_invoke_public_by_count`, `js_invoke_*_wrapper_impl`, `JS_MIR_CALL_ARITIES_0_15/0_32`, `JS_MIR_CONTEXT_CALL_MAX_ARITY`, the `MIR_CONTEXT_ABI`/`MIR_PUBLIC_ABI` flag tests, and the hosted-callback `lambda_hosted_item_invoke_by_count` path at this boundary are retired. |
| **JC15** | **Argument adaptation is generated, per function, in the span entry.** For formal `i`: `p_i = (i < argc) ? args[i] : undefined` — two instructions, emitted only for formals that can be missing. A rest formal packs `args[k..argc)` via `js_pack_args_span` (existing). Extra actuals are ignored by the body and remain visible to `arguments` through the activation record's source span, which the kernel still publishes (`js_pending_call_args/argc`). `JsCallAdapterSpan`, `js_invoke_needs_adapter`, and the C pad/rest loops are retired. |
| **JC16** | **The register-operand public wrapper `<f>` is retired.** The span entry is the only C-reachable compiled entry; `<f>_body` remains the only direct-call target (D8.4.2v2). The span entry is `MIR_ENTRY_CHECKED` (side-stack overflow guard) like the wrapper it replaces, and stores lane 2 into `Context::mir_companion_slot` before returning lane 1 (D5.2.1v3) — the kernel resolves it with `lambda_item_resolve_pending_slot` exactly as hop 8 does today. `JsCallableCode::func_ptr` continues to name the span entry for the existing `func_ptr → JsFunction*` cache and debug tooling. |
| **JC17** | **Root once.** The kernel owns exactly one owned-span `RootSpan` (allocated only when `!args_prerooted`) and one activation `RootFrame`; after the kernel marks the span prerooted, **no inner layer re-roots it**. The span entry publishes **no parameter root slots**: parameters live in the prerooted span for the entry's whole extent, and the body publishes its own slots on entry as it does today. The kernel's `RootFrame(9)` shrinks to the distinct homes it actually needs (callee, `this`, `new.target`, generator proto, private home class, super-`this` only on the derived-constructor branch). Forced-GC sweeps (`LAMBDA_GC_FORCE_EVERY=1`) gate every step; no conservative scanning (rule 15). |
| **JC18** | **Stage-2 site lowering (measured, optional).** A JIT dynamic call site may emit `type == FUNC && js_layout(fn) ? call [fn->invoke] : call js_call` — a compile-predicted specialization with an inline guard and the shared kernel on the miss, which D8.4.1v2 sanctions because the guard is immutable and there is no per-site state. This removes the `js_call` trampoline for ordinary calls (3 hops, no jump). It is taken only if a release A/B on call-dense rows shows the trampoline's `br` is visible after JC13–JC17. DC6-style callee cells remain retired. |
| **JC19** | **One authority, no entry family.** Per-shape entry templates (DC2) stay retired. Shape variation (arrow, strict, generator, async, derived ctor, `with`, foreign module/realm, eval-initializer, vm-source) is expressed as flag branches inside the single kernel; the span entry is *generated per function from its formals*, never templated per shape. Adding call-protocol state means editing one kernel and, if the callee must see it, one emitter — the "requires an entry-template review" contract from DC2 collapses to "requires a kernel review". |

### 3.3 What the kernel keeps (unchanged semantics)

Depth guard and its RangeError; callee/`this` rooting; `JsCallActivationScope`
(`this`, `new.target`, generator proto, private home class, callee, super-`this`,
`arguments` source span, source text, strictness); sloppy-`this` coercion for
non-arrow non-native callees; `new.target` scoping (D6.2.2v2/JC3); module-state
activation; `globalThis` swap for foreign realms; `with`-chain relink; derived
constructor TDZ `this` and `super` finish; legacy-async rejected-promise wrap
(moved from hop 6 into the kernel epilogue as one flag branch — or, better,
retired once the MIR async body is verified to create its promise before any
throwing statement, open item O2); `lambda_item_adopt_scalar_home` into a
caller-donated home. None of this moves to the span entry; the span entry is
pure ABI adaptation and must stay so (it is per-function code and would
otherwise duplicate semantics N times).

### 3.4 Expected effect (estimate, to be measured)

| | today | after JC13–JC17 | after JC18 |
|---|---:|---:|---:|
| named functions traversed | 10 | 4 (`js_call` → invoke → span → body) | 3 |
| machine frames | 5 | 3 | 3 |
| C stack / call (2-arg) | 1,328 B | ≈ 550–650 B *(kernel ≈ 450–550 after §2.2 trims; span ≈ 32; body 96)* | same |
| runtime `switch`/flag dispatch between kernel and body | body-kind, policy, ABI×env, arity | none | none |
| `RootSpan`/`RootFrame` constructions per call | 4 | 2 | 2 |
| parameter root stores per call | 2× formals | 1× formals | 1× formals |
| dynamic-boundary arity cap | 32 / 16 | none | none |

The frame figures are estimates from prologue sizes, not measurements; P0
records the release baseline first. The Tune_JS_Dynamic_Call record measured
the dispatcher+invoke protocol at ≈57% of a plain 2-arg call loop; JC13–JC17
removes most of the *invoke* half of that and none of the *dispatcher* half,
so the honest expectation is a call-cost reduction in the 1.3–1.6× range on
plain rows, not the ≥2× that per-shape templates once promised and failed to
deliver.

### 3.5 AST interpreter call path (JC20–JC22)

*Added 2026-09-15, after P1–P3. Design first; implementation record in
Appendix F.*

P1–P3 flattened the call path from a call site into the kernel and out through
the body entry. An AST-interpreted callee takes that shared middle
(`js_call` → kernel → `fn->body`), but the interpreter's own layers on either
side were never flattened. Measured on the P3 debug build for a non-tail
dynamic recursion under `JS_EXECUTION_BACKEND=ast`, one AST call costs
**4,128 B**:

| Frame per call level | Stack | Role |
|---|---:|---|
| `js_interp_eval_call_chain` | 928 B | evaluates callee, receiver and arguments into one rooted span |
| `js_call_function_prerooted_args_into` → `js_call` | 0 (tail) | ownership adapter, callee split |
| `js_call_entry_generic` [kernel] | ~544 B | depth guard, activation, `this`/`new.target`, module/realm/`with` checks, result adoption |
| `js_body_entry_ast` | 0 (tail) | body-entry adapter |
| `js_interp_call_function` | 816 B | AST activation: env, `arguments`, parameter binding, body walk |
| `exec_list` → `exec` → `eval` | 1,840 B | interpreter recursion back into the next call |

Two defects of shape remain, the same two JC17 and JC13 removed from the MIR
path:

1. **Inner re-rooting.** The kernel roots the callee, `this` and `new.target`
   and publishes them in the call activation. `js_interp_call_function` then
   opens its own `RootFrame(7)` and re-roots the callee, `this` (read back
   through `js_get_lexical_this_binding`), `new.target` (read back through
   `js_get_new_target`) and the home class, which the callee's payload already
   roots.
2. **No direct call.** A MIR caller with a proven callee calls
   `<fname>_body` without the kernel. An AST caller reaches an AST callee only
   through the full generic kernel: argument-span preparation, callee-layout
   split, body-entry dispatch, `with`/module/realm/generator/derived/vm-source
   branches, and the pending-scalar resolve that an AST body can never need.

#### Rulings

| ID | Decision |
|---|---|
| **JC20** | **The AST activation borrows the kernel activation; no inner re-roots.** `js_interp_call_function` runs only under a call activation for its callee, pushed by a kernel instance (JC13 generic or JC21 direct). It opens no root for anything that activation or the callee already roots. **Callee:** rooted by the activation's callee home. **Non-arrow `this`:** the environment's `lexical_this` is seeded from the activation's `this` binding after the environment exists. A self-tail call publishes its next receiver into the activation's `this` home, because a reused activation *is* that call's activation. **Arrow `this`:** the lexical environment home, or else the callee payload's `lexical_this`. **`new.target`:** non-arrow → the activation's `new.target` home; arrow → the payload's `lexical_new_target`. **Home class:** the payload's `home_class` slot, or a shared null home when the callee has no payload. The only roots it keeps hold values it creates: the `arguments` object and the two self-tail argument arrays. `RootFrame(7)` → `RootFrame(3)`. Payload slots are stable (`malloc`/pool, not data-zone) and traced through the rooted callee, so they are valid homes for the activation's extent. |
| **JC21** | **AST direct call = the kernel with proven facts, entered by a guard.** An AST call site with plain arguments (the existing rooted argument span, not construct, not `super`, not an intrinsic `eval`/`require`/`import`) calls `js_call_from_ast(callee, this, args, argc, result_home)` instead of `js_call_function_prerooted_args_into`. That entry evaluates one guard and, on a hit, runs `js_call_ast_direct`, a third `always_inline` instance of the *same* kernel source with a constant mode that folds away the steps the guard proves irrelevant. It then calls `js_interp_call_function` directly instead of `fn->body`. On a miss it tail-calls `js_call`, which is observably identical, so there is no per-site state and no cache (D8.4.1v2). **Guard:** callee is a JS-layout `JsFunction` with `body_kind == AST` and `invoke == js_call_entry_generic` (ordinary capability, not bound); flags exclude `GENERATOR`, `ASYNC`, `ASYNC_GEN`, `DERIVED_CTOR`, `USES_WITH`, `HAS_BOUND_THIS`, `TYPED_ARRAY_METHOD`; no eval-initializer context and no vm-stack source; no active `with` chain; callee module state is none or equals the active module state; callee home global equals the current global. **Kept on the hit path** (the kernel steps the guard does not prove): depth guard, call activation with its homes, sloppy `this` coercion, `new.target` install, private home-class install, `arguments` source span, and result adoption into the caller's home. **Folded away:** argument-span copy (the span is pre-rooted), callee-layout split, null-body check, module-state activation, realm swap, `with` relink, generator prototype, derived-constructor TDZ, vm-source push, body-entry dispatch, owner check, and pending-scalar resolve. |
| **JC22** | **One kernel source, three instances; not an entry family.** The generic `[[Call]]` instance (`js_call_entry_generic`), the `[[Construct]]` body instance (`js_call_constructor_body`) and the AST direct instance (`js_call_ast_direct`) are compile-time instantiations of `js_call_kernel`, differing only in constant operands. JC19 stands: shape variation stays flag branches in one source, and adding call-protocol state still means editing one kernel. The mode operand may only *remove* a step whose precondition a guard proved, never add semantics. The self-tail-call reuse inside `js_interp_call_function` is unchanged. |

#### Expected effect

Per AST call level: the `js_call_function_prerooted_args_into` → `js_call` →
generic-kernel → `js_body_entry_ast` hops collapse into
`js_call_from_ast` → direct kernel. That is two named hops instead of five, with
the same two machine frames: the direct instance and `js_interp_call_function`.
Both frames shrink, the kernel instance by its folded branches and the AST
activation by four root slots. The interpreter recursion
(`exec_list` → `exec` → `eval`, ~45% of the level) is tree-walking structure,
not call protocol, and is out of scope. Gates: the `JS_EXECUTION_BACKEND=ast`
runs of `test_js_gtest` and the test262 baseline must show no new failures
against the pre-change tree on that backend, plus a forced-GC run on it. The
MIR backend gates are unchanged.

## 4. Open items

- **O1** — `JS_FUNC_FLAG_MIR_PUBLIC_ABI` without `MIR_CONTEXT_ABI`
  (`js_runtime_function.cpp:1423`, `JS_FUNC_INIT_MIR_PUBLIC_ABI`): census who
  creates these (hosted/Jube callbacks?) and give them a span entry or a
  native adapter; the legacy spreading path must not survive as a third ABI.
- **O2** — Legacy async wrap (hop 6): verify every compiled async body reaches
  `js_promise_async_function_start` before any statement that can return an
  ERROR; if so the kernel-epilogue branch is unnecessary.
- **O3** — `js_mir_owner_is_current(code->runtime_context)` per call: the
  kernel already runs on the current `context`; decide whether the check is a
  debug assertion or a release guard (it is a TLS compare + thread compare per
  call today).
- **O4** — `lambda_side_root_contains_span` on every prerooted call
  (Tune7 §8.3 item 2): emission-scope ownership is the proof; move behind a
  diagnostic gate.
- **O5** — The JIT-inline depth guard (`jm_enter_source_invocation`) and the
  kernel's RAII guard are two implementations of one semantic; they share the
  counter, which is what matters, but the message and limit constants should
  be one definition.

## 5. Risks

- **Rooting regressions** are the correctness cliff: retiring wrapper param
  slots relies on the "span is prerooted for the entry's extent" invariant
  (JC17). Gate: `LAMBDA_GC_FORCE_EVERY=1` + `POISON_FREED=1` sweep over the
  forced-GC fixture set and test262 baseline, per phase.
- **Companion-slot ordering**: the span entry must store lane 2 before its
  own epilogue restores `side_number_top`, exactly as the wrapper does
  (`jm_finish_function_frame`'s companion-slot branch); MIR emission fixtures
  (`test/mir/js/*.mir-check`) ratchet the shape.
- **Native signature migration** (JC14 adds `Context*`): mechanical, but wide
  (typed factories, `JS_INTRINSIC_*_BODY` macros, hand-written bodies). Do it
  as one sweep with the old signature deleted in the same phase (JC11 — no
  phase-local dual ABI).
- **Generator/async/derived-ctor** flows are the shapes most likely to depend
  on hop-7/8 side effects (`js_pending_call_args` timing, callee-proto
  install). They stay in the kernel; the differential oracle is the current
  tree (`JS_CALL_FORCE_GENERIC`-style forcing is unnecessary once there is one
  kernel, but the pre-change binary remains the reference).

---

## Appendix A — Phasing (brief)

| Phase | Lands | Retires | Gate |
|---|---|---|---|
| **P0** | release-build baseline: ns/call microbench (`temp/tune8_call_bench.js` rows), stack-bytes probe, `sample` attribution | — | numbers recorded here |
| **P1** | `<f>_span` emitter (JC15/JC16 shape), `code->body_entry` (JC14), kernel calls `body_entry` | hops 7, 8, 9 and both arity caps | test262 40,261 baseline; MIR fixtures; forced-GC sweep |
| **P2** | `js_call` = kernel with inline type split (JC13); forwards for the three adapters | hops 1–3 as symbols; hops 5, 6 | same + ns/call A/B |
| **P3** | rooting dedup (JC17); kernel structural trims (§2.2); O1–O5 | dead `RootSpan`s, duplicate roots, `common_lane` | forced-GC sweep; A/B |
| **P4** | JC18 site lowering, only on P3 evidence | `js_call` trampoline on ordinary sites | A/B on call-dense rows; MIR fixtures |
| **P5** | JC20 AST activation borrows the kernel activation | `js_interp_call_function` re-roots (RootFrame 7 → 3) | AST-backend `test_js_gtest` + test262 vs pre-change; forced GC |
| **P6** | JC21/JC22 AST direct call (`js_call_from_ast`, `js_call_ast_direct`) | adapter/generic-kernel/body-entry hops on AST→AST calls | same + MIR-backend gates |

Each phase is net-negative in mechanisms and source (JC12).

## Appendix B — Evidence anchors (tree `772ef7625`)

`js_runtime.cpp`: `js_call_value` 14243, `js_call_entry_generic` 14232,
`js_call_function_impl_mode` 14028, `js_invoke_fn_with_source` 10050,
`js_invoke_fn_raw_or_async` 10022, `js_invoke_fn_raw` 9880,
`js_invoke_mir_context_by_count` 9733, `js_invoke_mir_context_wrapper_impl`
9690, `JsCallAdapterSpan` 9759, `js_prepare_owned_argument_span` 13804,
`JsCallActivationScope` 13984, `js_call_use_common_lane` 13741.
`js_runtime_function.cpp`: `js_function_finalize_capabilities` 162.
`js_mir_function_class_lowering.cpp`: `jm_emit_public_function_wrapper` 822.
`js_mir_calls_boxing_types.cpp`: `jm_call_direct_boxed` 136,
`jm_enter_source_invocation` 114, `jm_call_function_into` 172.
`js_function.hpp`: `JsCallEntry` 31, `JsNativeCode` 97, `JsCallableCode` 139.
Measurement scripts: `temp/jscall/{direct,dyn}.js`, `spprobe.py`, `prolog.py`.

## Appendix C — P1 implementation record (2026-09-15)

**Landed.**

- `JsBodyEntry` (`js_function.hpp`) is `JsNativeCallBody` —
  `Item (callee, this, args, argc, result_home)` — so a span-policy native is its
  own body entry with no adapter.
- `JsFunction::body` is written by `js_function_finalize_capabilities` next to
  `invoke`/`construct`, via `js_function_select_body_entry` (`js_runtime.cpp`):
  AST → `js_body_entry_ast` / `_ast_generator` / `_ast_async`; native
  BODY/SPAN/THIS_SPAN → `native->call`; other native policies →
  `js_body_entry_native_formals`; compiled (`MIR_CONTEXT_ABI`) → the MIR span
  entry itself; other code pointers → `js_body_entry_hosted`; none →
  `js_body_entry_absent`.
- `jm_emit_span_entry` (`js_mir_function_class_lowering.cpp`) replaces
  `jm_emit_public_function_wrapper` under the same `<fname>` symbol, so every
  `js_new_*_mir` site and the `func_ptr` cache are unchanged. It loads `Context`
  from `callee->code->runtime_context`, the env from `callee->env`, pads
  missing formals with `undefined`, packs a rest formal through the new
  `js_args_rest_array` import, calls `<fname>_body` with individual operands,
  and forwards a pending lane 1 with its payload in `Context::mir_companion_slot`.
- The kernel (`js_invoke_fn_with_source`) calls `fn->body` once and resolves a
  pending result with `lambda_item_resolve_pending_slot`.
- Retired: `js_invoke_fn_raw`, `js_invoke_fn_raw_or_async` (its legacy-async
  wrap moved into the kernel as one flag branch), `js_invoke_mir_context_by_count`,
  `js_invoke_public_by_count`, both `*_wrapper_impl` templates,
  `JS_MIR_CALL_ARITIES_0_15/0_32`, `JS_MIR_CONTEXT_CALL_MAX_ARITY`, and
  `JsCallAdapterSpan` (replaced by the one `JsFormalArgs` helper shared by the
  native-formals and hosted adapters).

**Deviations from the JC14/JC16 text** (to fold into the rulings on
ratification):

1. *Body entry lives on `JsFunction`, not `JsCallableCode`.* Selection depends
   on per-value facts (native policy is a per-value payload; `flags` gain
   `MIR_CONTEXT_ABI` only at `js_finalize_function`), and finalization is the
   one writer of executable capabilities (D6.2.2v2). Appended after the latch
   byte so no existing offset moves; `sizeof(JsFunction)` 88 → 96, still inside
   the 128 B class.
2. *No leading `Context*` on the body-entry signature.* The span entry reads
   the owning Context from the callee, which avoided the native-signature sweep
   JC14 anticipated. The kernel keeps the per-call owner check
   (`js_mir_owner_is_current`) for compiled bodies — open item O3.
3. *The span entry is frameless, not `MIR_ENTRY_CHECKED`.* It owns no roots and
   no scalar homes: the kernel roots `args` for the whole call,
   `js_args_rest_array` roots its own array, and the body
   (`MIR_ENTRY_BOUND_INTERNAL`) publishes its parameter roots and checks
   side-stack capacity on entry. Its instructions are appended outside the
   shared frame bookkeeping; the rest-array call is a raw import call for the
   same reason.
4. *Native and hosted adaptation stay in C* (`JsFormalArgs`), selected once.
   JC15's generated adaptation applies to compiled functions only; the C
   adapters are per-shape, not a per-call switch.
5. A debug-build check in the kernel re-derives the selection on every call and
   logs `js-body-entry: stale body entry` if a metadata writer skipped
   re-finalization.

**Pre-existing defect found and fixed.** `JsMir.AwaitsNestedForAwaitCloseBeforeReturning`
crashed deterministically with P1 and passed on the pristine tree — by luck.
`jm_transpile_return` evaluated the return value into a register and then
emitted the enclosing for-await's `AsyncIteratorClose`, which awaits and so
suspends the state machine; the register did not survive the resume. Pristine
returned the close result or `null` depending on memory state (`for await (…)
{ return 'ret'; }` completed with `null`); P1 only changed which stale value
appeared, which sent the nested case into a runaway loop. The value is now
spilled to a generator env slot across the closes, mirroring
`jm_emit_async_iterator_close_preserving_throw`. Regression:
`test/js/regression_for_await_return_close.js` (output matches Node 22).

**Measured** (same lldb stack-pointer probe as §1, debug `-O3` build):
dynamic 2-arg call **1,328 → 832 B** per level; direct call unchanged at 96 B.
Physical frames per dynamic level: kernel (`impl_mode` + inlined
`with_source`) 704, span entry 16, body 96, plus the frame record. Release
ns/call A/B not yet taken (P0).

**Gates** (debug build unless noted): test262 baseline 40,261/40,261, 0 regressions (release); JS MIR emission fixtures 27/27;
forced-GC MIR sweep 184/184; `test_js_gtest` 488/488 (includes the new
regression); `test_js_script_gtest` 136/136; JS exception and callable catalog
censuses clean; forced-GC (`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`)
outputs identical for the regression and a span-adaptation smoke script
(missing/extra/rest/default args, `arguments`, closures, wide and out-of-band
double returns, async, generators, classes, `call`/`apply`/`bind`).

## Appendix D — P2 implementation record (2026-09-15)

**Landed.**

- `js_call(fn, this, args, argc, result_home, args_prerooted)` (`js_runtime.cpp`,
  declared in `js_runtime.h`) is the sole dynamic-call entry. It does the
  callee-layout split (Core Lambda `Function` → `fn_call_into`; `JsFunction` →
  tail call through `fn->invoke`; anything else → the kernel's Proxy-trap /
  TypeError step) and replaces `js_call_value` and `js_call_function_impl`.
- The kernel is `js_call_kernel`, `always_inline`, instantiated in exactly two
  entries: `js_call_entry_generic` (every ordinary function's stored `invoke`,
  newTarget = 0) and `js_call_constructor_body` (explicit newTarget). An
  ordinary callee's capability is therefore the kernel itself, not a forward.
- `js_invoke_fn_with_source` is folded into the kernel. Its source-span
  `RootSpan` is gone: the kernel prologue already guarantees a rooted span.
  Early failures (missing body, owner mismatch) now fall through the kernel's
  restores instead of returning early from a helper.
- `js_call_function`, `js_call_function_into`, `js_call_function_prerooted_args_into`
  remain exported for C callers and hosted modules (`lambda/module/node_*`
  call `js_call_function` by symbol) but each only fixes the two ownership
  operands and tail-calls `js_call`.
- The JIT emits `js_call` with the rooted-span fact as a literal operand, as
  `js_construct_value` already did; the `js_call_function_prerooted_args_into`
  import row is replaced by one `js_call` row. Five `.mir-check` fixtures now
  pin `call js_call_p_r6_n1_a6, js_call`; `tune4_call_construct` also pins the
  receiver operand and the prerooted flag, and `native_completion_ownership`
  forbids `js_call` from native bodies.

**Resulting chain** for a JIT dynamic call to an ordinary function:
call site → `js_call` (tail `br`) → `js_call_entry_generic` [kernel] → span
entry → body. Four hops, three frames; bound functions add
`js_call_entry_bound`, which re-enters `js_call` on the target.

**Measured** (lldb stack-pointer probe, debug `-O3`): dynamic 2-arg call
832 → **768 B** per level (1,328 B before P1); direct call unchanged at 96 B.

**Gates:** test262 baseline 40,261/40,261, 0 regressions (release); JS MIR
emission fixtures 27/27; forced-GC MIR sweep 184/184; `test_js_gtest` 488/488; `test_js_script_gtest` 136/136; JS exception and
callable catalog censuses clean; forced-GC outputs identical for the
for-await regression and the span-adaptation smoke script.

## Appendix E — P3 implementation record (2026-09-15)

**JC17 — rooting.** The kernel's `RootFrame(9)` is now `RootFrame(7)`:
- The separate callee root is gone; the activation's callee home roots the
  callee. The redundant `js_pending_args_callee = func_item` store went with it.
- The separate newTarget root is gone. An explicit newTarget is published into
  the activation's rooted new.target home before `this`-binding coercion, the
  kernel's first possible allocation. Nothing in between observes new.target.
- The super-`this` home is **kept** for every activation, contrary to the JC17
  text: `js_super_bind_this` publishes through the *running* activation as
  well as the derived constructor's, and that running activation is an arrow
  when `super()` is called from one.

With P2 having already removed the source-span `RootSpan`, the kernel holds one
owned-argument `RootSpan` (only when not pre-rooted) and one `RootFrame`.

**§2.2 trims.**
- `js_call_use_common_lane` is deleted. All three of its uses (generator
  prototype, derived-constructor TDZ, vm-stack source) were no-ops for every
  function it could accept, because the classifier already excluded those
  shapes.
- The local scalar-home fallback for public-ABI-only callees
  (`js_finish_borrowed_scalar_result`) is deleted.

**Open items.**
- **O1 — closed.** Nothing creates a public-ABI function without the context
  flag. `JS_FUNC_FLAG_MIR_PUBLIC_ABI` (512) and `JS_FUNC_INIT_MIR_PUBLIC_ABI`
  (1<<5) are retired, and both bits are left unassigned.
- **O2 — closed.** The kernel's async rejection fallback is removed, together
  with its now-unused `js_promise_async_function_start/finish`.
  - Evidence: test262 baseline 40,261/40,261 with no regressions. That covers
    the async-function abrupt default-parameter and destructuring suites.
  - Evidence: `test_js_gtest` and `test_js_script_gtest`, plus targeted scripts
    (destructuring, defaults, expression bodies, methods, rest, class methods,
    recursion, stack overflow), all reject as before.
  - An abort probe inserted in the branch never fired in `test_js_gtest` or
    `test_js_script_gtest`.
- **O3 — decided: keep the owner check as a release guard, but inline its fast
  path.** Two thread-local compares (callee's `runtime_context` against the
  current `context`, and the capsule against `js_active_runtime_state`). The
  out-of-line `js_mir_owner_is_current` runs only to confirm and log a mismatch.
- **O4 — done.** `lambda_side_root_contains_span` in
  `js_prepare_owned_argument_span` and `js_call_entry_bound` is now `#ifndef
  NDEBUG`. The emitter's arg-frame scope (`jm_args_are_prerooted`) is the
  production proof.
- **O5 — done.** `JS_CALL_STACK_EXCEEDED_MESSAGE` (`js_runtime.h`) is the one
  message for both the kernel guard and the JIT's inline source-invocation
  guard. They already shared the counter and the limit.

**Measured** (lldb stack-pointer probe, debug `-O3`): dynamic 2-arg call
768 → **672 B** per level (1,328 B before P1); direct call unchanged at 96 B.

**Gates:** test262 baseline 40,261/40,261, 0 regressions (release, with the O2
removal in); JS MIR emission fixtures 27/27; forced-GC MIR sweep 184/184;
`test_js_gtest` 488/488; `test_js_script_gtest` 136/136; JS exception and
callable censuses clean; forced-GC outputs identical for the for-await
regression and the span smoke script. After the dead-helper deletion,
`test_js_gtest` and the MIR fixtures were re-run.

**Found, not fixed (pre-existing):**
1. *Async functions with abrupt parameters* — **fixed 2026-09-15** (three MIR
   lowering defects, independent of the call flow; see
   `test/js/regression_async_abrupt_params.js`): (a) function-level error
   landing pads read `jm_emit_error_lane_return`, which ignores the
   function-level carrier while a try context is still open, so an
   expression-bodied async arrow built its rejection from a stale register —
   landing pads now use `jm_emit_function_error_lane_carrier`; (b) the async
   state-machine wrapper routed parameter-destructuring throws to an error
   label it never emitted, crashing MIR linking — it now emits a rejecting
   landing pad; (c) the async state machine pushed its implicit try after
   parameter instantiation although both share one catch label, so a
   default-parameter throw rejected with a stale value — the try is now pushed
   first. Test262 40,261/40,261 unchanged.
2. *A codegen-sensitive failure.* Adding an unreachable `abort()` inside the
   kernel's async branch (the throwaway O2 probe) made `lib_fast_diff.js` fail
   deterministically with a raw ERROR Item escaping as a JS value, although no
   `js_throw*` helper and no probe ever ran. P2 and P3 builds without the probe
   pass, including under forced GC. This points to a latent layout- or
   UB-sensitive defect somewhere on that path; not root-caused.

## Appendix F — P5/P6 implementation record: AST interpreter (2026-09-15)

**JC20 — no inner re-roots (`js_interp_call_function`).**
- `RootFrame(7)` → `RootFrame(3)`. The remaining slots hold only the
  `arguments` object and the two self-tail argument arrays.
- The callee is rooted by the activation's callee home. A debug build logs
  `js-interp-call: entered without its callee's call activation` if a caller
  ever enters without one.
- A non-arrow environment's `lexical_this` is seeded from
  `js_get_lexical_this_binding()` after the environment exists. The kernel has
  already bound and coerced the receiver.
- A self-tail call writes its next receiver into the activation's `this` home
  (`js_current_this`) instead of a private root.
- An arrow's frame `this` home is its lexical environment home, or else
  `&payload->ast->lexical_this`.
- `new.target`: non-arrow → the activation's `new.target` home; arrow →
  `&payload->ast->lexical_new_target`.
- Home class: `&payload->home_class`, or the file-static
  `js_interp_no_home_class` (null) when the callee has no payload.

**JC21/JC22 — AST direct call.**
- `js_call_kernel` gains a constant `ast_direct` operand, instantiated by
  `js_call_ast_direct`. On that instance the callee-layout split, null-body
  check, module-state activation, realm swap, `with` relink, generator
  prototype, derived-constructor TDZ, vm-source push and body-entry dispatch
  (with its stale-entry check, owner check and pending resolve) fold away.
- The body call is `js_interp_call_function` directly.
- `js_call_ast_direct_eligible` is the guard (§3.5 JC21).
- `js_call_from_ast` (declared in `js_runtime.h`) is the AST call site's single
  entry; a miss tail-calls `js_call`. `js_interp_eval_call_chain` calls it in
  place of `js_call_function_prerooted_args_into` for plain-argument,
  non-construct calls.

**Measured** (debug `-O3`, lldb, `JS_EXECUTION_BACKEND=ast`, non-tail dynamic
recursion `temp/jscall/dyn_nontail.js`):
- Breakpoint counts: all 7 recursive calls take `js_call_ast_direct`. Only the
  native `console.log` takes `js_call` and the generic kernel.
- Per call level: **4,128 → 4,048 B**. The kernel frame is 544 → 512 B, since
  the direct instance is inlined into `js_call_from_ast`. The AST activation is
  816 → 768 B.
- Named hops between evaluator and AST activation: 5 → 2 (`js_call_from_ast`
  [direct kernel] → `js_interp_call_function`).
- Folding branches reduces frame size only slightly; the gain is in hops and
  per-call work, not stack.

**Gate facts established while planning.**
- The standard test262 baseline is predominantly an AST run: hybrid routing
  sends 34,334 of 40,261 tests to AST batches, and the runner sets
  `JS_EXECUTION_BACKEND` per batch.
- `test_js_gtest` defaults to mixed mode: every test not in the MIR list runs
  on AST.
- Both suites were therefore already gating the AST path in P1–P3.
- The pre-change AST baselines are test262 40,261/40,261 and `test_js_gtest`
  489/489.

**Release A/B** (interleaved ×3, `JS_EXECUTION_BACKEND=ast`,
`temp/aap/callloop_big.js`, 4 million dynamic AST calls): pre-change 1.69–1.71 s,
after 1.62–1.68 s, about 3% faster. Interpreter tree walking dominates an AST
call, so call-protocol flattening moves the total only a little.

**Gates:**

| Gate | Result |
|---|---|
| test262 baseline (release, 34,334 tests in AST batches) | 40,261/40,261, 0 regressions |
| `test_js_gtest` (mixed, AST-dominant) | 489/489 |
| `test_js_script_gtest` | 136/136 |
| JS MIR emission fixtures | 27/27 |
| Forced-GC MIR sweep | 184/184 |

Forced GC with poisoned frees on **both** backends (`ast`, `mir`) produces
identical output for the span smoke script, the for-await and async
abrupt-parameter regressions, and `lib_fast_diff.js`.
