# Lambda Runtime Call Flow — How a `fn` Call Compiles and Executes

**Date:** 2026-07-27
**Scope:** MIR Direct backend only (`lambda/runtime/transpile-mir.cpp` + `lambda/runtime/mir_emitter_shared.hpp` + runtime support in `lambda/runtime/lambda-eval.cpp`). The C2MIR path is frozen and not covered.
**Line anchors** are as of 2026-07-27 and drift with the working tree; function names are the durable references.

This documents the end-to-end path for calling a normal Lambda function: how the transpiler picks a call shape, exactly what MIR is emitted at the call site, what the callee's generated prologue/body/epilogue do, and what the runtime does for dynamic (function-value) calls.

---

## 1. Overview: three call shapes

Every call expression is lowered by `transpile_call` (`transpile-mir.cpp:10136`), which wraps `transpile_call_raw` (`:8701`) (the wrapper only adds async suspend/resume splitting for procs that may await). `transpile_call_raw` resolves the callee in this order:

1. **Special forms** — type-coercion calls (`int64(x)`, sized-num casts), then system functions (`print`, `select`, vmap ops, …). These compile to dedicated runtime-helper calls and never touch the user-function machinery.
2. **Imported module functions** — the import-call arm (mangled name, import ref, same marshaling as direct calls).
3. **Direct call** — the callee identifier resolves to a known `AST_NODE_FUNC` / `FUNC_EXPR` / `PROC` definition **and** is not shadowed by a local variable. The shadow guard checks `find_var()`: if the name is a variable or parameter, it holds a *function value* at runtime and must go dynamic, even when AST type inference propagated the function type to it (prevents mis-treating `render_fn(args)` as a direct call).
4. **Dynamic call** (`:10039`) — everything else: closures in variables, function-typed params, member functions used as values. Goes through the `fn_call*_into` runtime family.

Within the direct arm there is a fourth shape that emits **no call at all**:

5. **TCO jump** (`:9637`) — a tail-recursive call to the function currently being compiled becomes argument moves + `MIR_JMP` back to the entry label.

```
call expression
 ├─ coercion/sysfunc? ──────────────► runtime helper call
 ├─ imported fn? ───────────────────► import call (direct-call marshaling)
 ├─ known fn def, not shadowed?
 │   ├─ tail self-call in TCO fn? ──► arg moves + MIR_JMP (no call)
 │   └─ otherwise ──────────────────► direct MIR_CALL to the func item
 └─ else (fn value / closure) ──────► fn_call0..3_into runtime dispatch
```

---

## 2. Direct call — caller-side steps

For `add(x, y)` where `add` is a known `fn` (direct arm, log line `mir: DIRECT call`, `:9630`):

1. **TCO interception first.** If `mt->tco_func && mt->in_tail_position` and the call targets the enclosing function: evaluate all args into temporaries (prevents aliasing when args read params being overwritten, e.g. `f(b, a)`), convert each to the parameter's *resolved MIR-local* representation (not the stale `TypeParam`), `MIR_MOV`/`MIR_DMOV` onto the param registers, refresh their GC root slots, `MIR_JMP` to `tco_label`. A type-shaped dummy register is returned for enclosing expressions.
2. **Argument resolution — entirely transpile-time.** Count args; if any are named, reorder into `resolved_args[16]` by matching param names against the static `AstFuncNode` definition; positional args fill remaining slots. Missing slots later take the declared default expression or a boxed-null pad. This is per-call-site AST work: the emitted call is purely positional, no runtime name matching exists. Runtime only *evaluates* what each slot got — defaults are evaluated in the **caller's** frame on every call, an empty no-default slot is a single `MOV` of a boxed-null immediate, and evaluation order follows parameter order, not source order (`f(y: g(), x: h())` runs `h()` first).
3. **Native vs boxed marshaling.** Look up `NativeFuncInfo` (registered when the callee got a native "dual version" — see §5). Per parameter:
   - *Native param* (`INT`/`FLOAT`/`BOOL`/…): `transpile_expr` the arg and pass the raw `i64`/`d` register. Same type → pass through; boxed `ANY`/`NULL` → `emit_unbox`; other mismatch → `emit_box` then `emit_unbox` (handles int→float etc.).
   - *Boxed param*: `transpile_box_item` → boxed Item register, then **`create_gc_root_slot`** for it. Rooting each boxed arg is mandatory: evaluating a later argument can allocate and trigger GC.
   - *Explicit `var` param*: the callee borrows the caller's writable root, so a COW-marked binding is detached (`mir_prepare_cow_root`) *before* the call — the callee's raw stores cannot write a replacement back into the caller.
4. **Variadic tail.** Extra args are collected into a rooted `List*` (`list()` + `list_push` per arg, with root-slot reload between pushes) and passed as one trailing pointer; zero extras pass NULL.
5. **Reload from roots.** Immediately before the call, every rooted arg is re-loaded from its root slot (`load_gc_root_slot(..., "call_arg")`). Register copies may be stale — the root slot holds the safepoint-current value.
6. **Emit the call.**
   - Callee has a `FnVariantAnalysis` → `em_call_direct` (`mir_emitter_shared.hpp:2893`). If the variant's `scalar_home_lane_mask` is set, it appends the hidden trailing argument: a pointer to a **caller-owned scalar result home** (a slot in the caller's number frame, or the caller's own incoming home for tail calls, or a discard home when the result is unobserved). Then it builds the proto and emits `MIR_CALL {proto, func-item ref, result reg, args…, home}` with full per-arg ABI metadata (value class, borrowed effects) and effect summary (may-GC, reentry, exception).
   - Fallback (no variant): build an ad-hoc proto (`<name>_cpN`) and emit through `em_emit_borrowed_call` (`:2791`) — args are treated as borrowed for the synchronous call; the emitter roots register args before the insn, bumps the may-GC call count, records the call site for root-liveness publication, and roots/classifies results after.
7. **Error lane** (callee has native return **and** `can_raise`): after the call, load `Context::mir_return_lane`; `MIR_BT` selects between the boxed success value and the error Item, merged into one register.
8. **Result handling.** Bind the scalar home to the result (`em_scalar_home_bind` — preserves caller-home identity across copies and lane joins), then box/unbox to the call expression's *effective* type (native return + caller wants boxed → box; boxed return + caller wants native scalar → unbox), and finally `root_gc_result_if_needed` roots heap-capable results.

---

## 3. MIR at the call site (annotated)

Boxed-ABI direct call `r = add(x, y)` (see the real output in `temp/mir_dump.txt` on a debug build):

```
; caller
mov   a0, <boxed x>                    ; transpile_box_item
mov   [root_base + k0*8], a0           ; publish arg to GC root slot
mov   a1, <boxed y>
mov   [root_base + k1*8], a1
mov   a0, [root_base + k0*8]           ; reload safepoint-current values
mov   a1, [root_base + k1*8]
add   home, number_base, <slot_off>    ; caller-owned scalar result home
call  proto_add, add, result, a0, a1, home
; native-return + can_raise callees additionally:
;   mov  err, [ctx + mir_return_lane]
;   bt   Luse_error, err
;   ...box success... / Luse_error: mov merged, err
```

Runtime-helper calls made through `em_call_with_args` follow the same home protocol: when the helper's normal result can be a scalar-home Item, the emitter allocates a home, snapshots `side_number_top` as the source base, and adopts the returned payload into the home after the call (`em_adopt_scalar_item`).

---

## 4. Callee anatomy (`transpile_func_def`, `transpile-mir.cpp:13408`)

### 4.1 Signature

```
[_env_ptr | _self]?   user params…   [_vargs]?   [_scalar_home]?   → 1 result
```

- `_env_ptr` (`MIR_T_P`) leads for closures (captures present, not a method); `_self` leads for methods. Mutually exclusive.
- User params are `MIR_T_I64` boxed Items, or native MIR types when the function got a **native dual version**.
- `_vargs` (`MIR_T_P`, a `List*`) trails for variadic functions.
- `_scalar_home` (`MIR_T_P`) trails for every generated body whose return may need a caller home: boxed-return bodies (`RETURN_LANE_SCALAR`) and native-return `can_raise` bodies (`RETURN_LANE_ERROR`, the home carries the error Item's payload). Pure native no-raise returns omit it.
- Result: one MIR value — boxed Item as `i64`, or native `i64`/`d`.

**Native dual version eligibility:** not a closure/method/variadic, no task context, no typed-array param, and at least one native-typed param *or* a native inferred return type. Registered as `NativeFuncInfo` so call sites marshal natively. Return lanes: native return + `can_raise` → `RETURN_LANE_ERROR`; boxed return → `RETURN_LANE_SCALAR`; native no-raise → `RETURN_LANE_NONE`.

### 4.2 Prologue — the side-stack frame

Emitted by `em_finalize_frame_prologue` (`mir_emitter_shared.hpp:1106`). Root/number slot counts are **lowering-time facts**, so the prologue is patched in *before the anchor* only at finalize time (`finalize_side_root_frame`, after the body has been lowered and slots counted). Shape:

```
add: func i64, i64:_x, i64:_y, p:_scalar_home
; ---- prologue ----
mov   root_base, [ctx + side_root_top]
ne    bound, root_base, 0
bt    Lbound, bound                     ; side stacks already bound?
call  lambda_side_stack_ensure(ctx, R, N) -> ok
bf    Loverflow, ok
mov   root_base, [ctx + side_root_top]
Lbound:
mov   number_base, [ctx + side_number_top]
add   ntop, number_base, N*8            ; reserve N number slots
mov   nlim, [ctx + side_number_limit]
ugt   ovf, ntop, nlim
bt    Loverflow, ovf
mov   [ctx + side_number_top], ntop
add   rtop, root_base, R*8              ; reserve R root slots
mov   rlim, [ctx + side_root_limit]
ugt   ovf, rtop, rlim
bt    Loverflow, ovf
mov   [root_base + 0*8], 0              ; zero every root slot
...
mov   [ctx + side_root_top], rtop       ; frame now visible to the GC

; … function body … single epilogue … ret …

Loverflow:                              ; shared overflow handler
call  lambda_stack_overflow_error("side-stack")
ret   ITEM_ERROR                        ; error-lane bodies instead:
                                        ;   mov [ctx+mir_return_lane], ITEM_ERROR
                                        ;   ret 0 (or 0.0)
```

(Windows adds a commit-limit check + `lambda_side_stack_ensure` re-call between the limit check and the zeroing.) The `MIR_ENTRY_CHECKED` ensure-call form is used for public entries; frames with zero root slots skip binding, and a compaction pass strips the dead watermark stores.

**Cost of the ensure call:** near-zero in steady state. The call sits behind the inline `side_root_top != 0` guard (1 load + 1 compare + 1 predictable branch per entry) and executes only when a context first runs on an unbound thread — where it does the real work: two anonymous mmaps (16 MB root + 64 MB number reserve, `side_stack.h`, per-thread `__thread` regions; demand-paged R/W on POSIX, reserve+commit-growth on Windows). Post-bind executions are a handful of compares/stores with no syscalls (POSIX). The body cannot be inlined into generated code — the regions are TLS, which MIR IR cannot address; `Context` caches `base/top/limit/commit_limit` precisely so generated code never needs them. If even the guard matters someday, the lever is `MIR_ENTRY_BOUND_INTERNAL` (skips guard + ensure entirely, binding as a caller-guaranteed invariant) for fns reachable only via direct calls; today `begin_function_epilogue` stamps every user fn `MIR_ENTRY_CHECKED`. Perf history: resolving TLS region metadata per argument frame once regressed call-heavy JS (`side_stack.c` comment in `lambda_side_root_alloc_n`), which is why the hot path stays entirely on `Context` fields.

**Overflow path:** every prologue failure branch (`bf` on the `lambda_side_stack_ensure` call, the two `bt` limit checks, the Windows commit check) targets the one shared `Loverflow` handler, emitted and owned by `finalize_side_root_frame` — see §4.5 for its emission and contract.

### 4.3 Body conventions

- Params are registered as MIR locals; heap/pointer/ANY params and locals get root slots. Assignment refreshes the slot (`update_gc_root_slot`); the collector scans only `[side_root_base, side_root_top)`.
- Before every may-GC call, live heap values are published to their root slots; values are reloaded from slots after (safepoint-current publication, driven by the recorded call sites + root-liveness pass).
- Full-width `INT64`/`UINT64` and out-of-band `FLOAT` temporaries live in `[number_base, ntop)` slots of the number stack.
- Every `return` lowers to `emit_function_return`: `MOV`/`DMOV` into the single `return_value` register (+ zero the error register on error-lane bodies) and `MIR_JMP` to the one shared `return_label`. Error raises use `emit_function_error_return` (zero value, error Item into the error register, same jump).

### 4.4 Epilogue — one exit for all paths (`finish_function_epilogue`, `transpile-mir.cpp:668`)

Order matters and is deliberate:

```
Lreturn:
; error-lane only: spill first result to a number scratch slot
; (preserves ARM64 nested-call first result across the home handoff)
mov   [ctx + side_root_top], root_base   ; restore root watermark FIRST…
; RETURN_LANE_SCALAR (boxed return):
;   em_adopt_scalar_item: if the Item's payload lives in this frame's
;   number extent, copy it into _scalar_home and retag, THEN
mov   [ctx + side_number_top], number_base
ret   return_value                        ; rehomed Item
; RETURN_LANE_ERROR (native + can_raise):
;   adopt error Item into _scalar_home, reload spilled first result,
mov   [ctx + side_number_top], number_base
mov   [ctx + mir_return_lane], adopted_error   ; 0 on success
ret   first_result
; RETURN_LANE_NONE:
mov   [ctx + side_number_top], number_base
ret   return_value
```

The root restore happens at the label (after any scoped cleanup emitted before each jump here) so the in-flight return value stays rooted across cleanup calls that may collect. The **scalar-home adoption is the key ABI subtlety**: a returned `int64`/`uint64`/out-of-band-`float` Item points at storage in the callee's dying number frame; the payload is copied into the caller-donated home *before* the number watermark restore. TCO/generator/async-suspend/handler paths all route through this same epilogue.

"One exit" means one exit for all **body** paths. The `Loverflow` handler (§4.5) is a separate second exit that deliberately bypasses `Lreturn`: it is reachable only from the prologue's reservation checks, before the frame exists, so there is nothing to adopt or restore — it returns `ITEM_ERROR` directly. In the emitted layout both blocks sit at the function tail, `Loverflow` after the epilogue's `ret`.

### 4.5 `finalize_side_root_frame` — deferred prologue finalize + the `Loverflow` exit

`transpile_func_def` calls this once per function, **after** the body and epilogue have been emitted, because root/number slot counts and scalar-home lifetimes are lowering-time facts that only exist post-body. Its steps (`transpile-mir.cpp:726`):

1. **`em_finalize_scalar_homes`** (`mir_emitter_shared.hpp:2328`) — packs the body's call-site scalar result homes into concrete number-frame slots. A liveness pass over the finished instruction stream colors the homes so non-overlapping ones share a slot, then the placeholder displacement in every `add home, number_base, <off>` materialization is patched, and the final number-slot count the prologue must reserve is settled (a discard-scratch slot is added when needed).
2. **`em_finalize_frame_prologue`** (`mir_emitter_shared.hpp:1106`) — with slot counts now final, *inserts* the §4.2 reserve/check sequence before the prologue anchor at function entry (frames that ended up with zero root slots get their dead watermark stores stripped instead), and emits the shared `Loverflow` label at the current append point — the function tail, right after the epilogue's `ret`.
3. **Handler body** — emitted under `Loverflow`: `call lambda_stack_overflow_error("side-stack")`, then the per-lane error return — scalar/boxed-lane bodies `ret ITEM_ERROR`; error-lane bodies store `ITEM_ERROR` to `ctx->mir_return_lane` and return a zero first result (`0` / `0.0`).
4. **`em_finalize_function_metadata`** (`mir_emitter_shared.hpp:2595`) — ABI verification and recording: public entries must be `MIR_ENTRY_CHECKED`; the scalar-home plan flag, the incoming `_scalar_home` register, and the trailing pointer param must all agree; home-displacement fixups must be consistent. Violations abort at transpile time.

**The `Loverflow` contract.** It is the function's *second* exit, and it never merges with `Lreturn`:

| | `Lreturn` (§4.4) | `Loverflow` (§4.5) |
|---|---|---|
| Emitted by | `finish_function_epilogue` | `finalize_side_root_frame` |
| Reached from | every body path: `return`, `raise`, TCO, suspend | prologue failures only: `bf` on `lambda_side_stack_ensure`, both `bt` limit checks, Windows commit check |
| Frame state on entry | fully established | not (fully) established |
| Teardown | root restore → scalar adoption → number restore | none |
| Result | rehomed `return_value` | `ITEM_ERROR` (error-lane: via `mir_return_lane`) |

The handler performs no teardown because there is no complete frame to tear down: it must not touch `_scalar_home` (nothing was computed), and skipping the watermark restores is safe even when the failure hits after a partial bump — the root-limit check fires after `side_number_top` was already reserved — because every caller's own epilogue restores both watermarks to *its* saved bases, clamping any callee-side drift as the error propagates.

---

## 5. Dynamic call — function values and closures

### 5.1 Compile side (`transpile-mir.cpp:10039`)

1. Evaluate and box the callee expression (`emit_box` → a `Function*` Item).
2. Allocate a caller-owned scalar home (`em_scalar_home_new` + materialize the frame ref); abort if the frame can't provide one.
3. Box each argument via `transpile_box_item`.
4. Call `fn_call0_into` / `fn_call1_into` / `fn_call2_into` / `fn_call3_into` with `(boxed_fn, args…, home)` through `em_emit_borrowed_call`.
5. Bind the home to the result; unbox if the call expression's effective type is a native scalar (matches direct-call behavior so callers re-box consistently).

**Known issue:** >3 args on this path logs `mir: calls with >3 args not yet fully supported` and returns the wrong value. Direct calls are unaffected.

**No named-arg/default resolution here:** parameter names aren't known at the call site and `Function` carries no name→position table, so a named arg just transpiles its value expression in the position it was written (`AST_NODE_NAMED_ARG` in `transpile_expr` returns `named->as`) — named args on a function-value call silently degrade to positional, and missing params are never default-filled.

### 5.2 Function values

When a `fn` is materialized as a value (`let f = add`, closures, member fns), the transpiler emits `to_fn_n` / `to_fn_named` / `to_closure(_named)` to build the heap `Function` object (`lambda.h:966`: `{type_id, arity, closure_field_count, flags, fn_type, ptr, closure_env, name}`). If the function has a *native* dual version, `ptr` points at its generated **`_b` all-boxed wrapper** (decided by `needs_fn_call_wrapper`), so dynamic callers can always pass boxed Items. The `fn_call_boxed_N(_into)` C trampolines in `lambda-eval.cpp` exist to call these wrappers from MIR through a platform-stable function-pointer ABI.

### 5.3 Runtime steps (`fn_call_into`, `lambda-eval.cpp:770`)

1. **Validate** the pointer is a real `Function*` (`is_valid_function`) — tagged error/null Items reach failed dynamic calls and are not dereferenceable; invalid callees fall through to the legacy `fn_callN` error paths.
2. **ABI gate:** `FN_FLAG_MIR_PUBLIC_ABI` set → a `result_home` is mandatory (`ERR_INVALID_CALL` otherwise). `FN_FLAG_SYS_REF` builtins are rejected as not dynamically callable. Non-public-ABI functions route to legacy `fn_call` (which handles `FN_FLAG_BOXED_RET` `RetItem` returns).
3. **Dispatch** by arity (0–8) and closure-ness, casting `fn->ptr`:
   - closure: `((Item(*)(void*, Item…, uint64_t*))ptr)(closure_env, args…, result_home)` — the env pointer maps to the callee's leading `_env_ptr` param;
   - plain: `((Item(*)(Item…, uint64_t*))ptr)(args…, result_home)`.
   The exact caller home crosses this forwarding boundary unchanged, so payload adoption in the callee epilogue lands directly in the original caller's frame.

---

## 6. Invariants worth remembering

- **Every boxed argument is rooted before the next argument is evaluated**, and reloaded from its root slot just before the call. Arg evaluation can GC.
- **The trailing `_scalar_home` is the wide-scalar return contract.** Callee number frames die at the epilogue; payloads that must outlive it are adopted into the caller-donated home. Dynamic calls always donate one; direct calls donate, forward (tail), or discard per observed lanes.
- **Native-return `can_raise` functions return the error out-of-band** via `ctx->mir_return_lane` (0 = success); the MIR result register carries only the success value.
- **One epilogue per function.** All returns, raises, TCO exits, and suspensions jump to `return_label`; both side-stack watermarks are restored there, root watermark first.
- **Shadowed names never direct-call.** A variable holding a function value goes through `fn_call*_into` even when the AST knows its type.
- **Prologue is finalized after the body** — slot counts are only known post-lowering, so the frame-reserve code is inserted before the anchor at `finalize_side_root_frame` time (§4.5), which also emits the `Loverflow` exit and runs the ABI verifier.

## 7. Debugging pointers

- Debug builds dump the JIT'd module to `temp/mir_dump.txt` — the fastest way to see the actual emitted sequence for a given script.
- `LAMBDA_MIR_LOG_FRAME_SLOTS` prints static frame telemetry (root/number slot counts per function).
- Grep `log.txt` for `mir: DIRECT call`, `mir: TCO tail call`, `mir: dynamic call - using fn_call` to see which shape a call site took.

## 8. Anchors

| What | Where (2026-07-27) |
|------|--------------------|
| Call dispatch + async split | `transpile_call` — `transpile-mir.cpp:10136` |
| Full call resolution | `transpile_call_raw` — `transpile-mir.cpp:8701` |
| Direct arm / TCO / dynamic arm | `:9629` / `:9637` / `:10039` |
| Direct-call emitter (+ home lane, ABI metadata) | `em_call_direct` — `mir_emitter_shared.hpp:2893` |
| Borrowed/unclassified call bookkeeping | `em_emit_borrowed_call` / `em_emit_unclassified_call` — `mir_emitter_shared.hpp:2791` |
| Helper calls with home adoption | `em_call_with_args` — `mir_emitter_shared.hpp` |
| Function definition build | `transpile_func_def` — `transpile-mir.cpp:13408` |
| Frame prologue | `em_finalize_frame_prologue` — `mir_emitter_shared.hpp:1106`; `finalize_side_root_frame` — `transpile-mir.cpp:726` |
| Finalize pass + `Loverflow` (§4.5) | `em_finalize_scalar_homes` — `mir_emitter_shared.hpp:2328`; `em_finalize_function_metadata` — `:2595` |
| Shared epilogue | `begin_/finish_function_epilogue` — `transpile-mir.cpp:606/668`; root exit `:595` |
| Scalar-home adoption | `em_adopt_scalar_item` — `mir_emitter_shared.hpp:878` |
| Dynamic runtime dispatch | `fn_call_into` — `lambda-eval.cpp:770`; `fn_call0..3_into` — `:828+`; `_b` trampolines `fn_call_boxed_N(_into)` |
| Function object | `struct Function` — `lambda.h:966` |
| Design doc | `doc/dev/lambda/LR_07_MIR_Transpiler_JIT.md` §5 (calls/inference), §6 (side frames) |
