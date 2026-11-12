# LambdaJS — Central Issue Ledger

> **Working issue record for the JavaScript runtime under `lambda/js/`.**
> Design rulings are in `doc/Lambda_Formal_Design.md` (D8.4), and the design
> records are in `vibe/jube/`. This ledger is the home for all LambdaJS defects,
> including the per-area known issues imported on 2026-11-06, when the detailed
> design set was retired. The imported entries were not re-verified on import.
>
> **Audience:** engine developers. **Status:** working ledger (`vibe/`), not normative.
> Semantic and design rulings are cited by `S#` / `D#` per CLAUDE.md rule 17;
> where no formal ruling covers a point, the vibe ledger ID is given.

## ID convention

Sections are numbered by engine area. Imported issues keep their item number
(`JS08-3` is item 3 under `JS08 known issues`). Issues that originate **here**
take an `L` suffix (`JS05-L1`) so the two numbering spaces cannot collide.

| Mark | Meaning |
|---|---|
| **OPEN** | Reproduced in current source; `file:line` anchors resolved. |
| **PARTIAL** | Some sub-claims fixed, a real residue remains. |
| **RESOLVED** | Verified fixed; record retained with the fixing change. |

---

## 3. Value model, memory & GC interop (JS03)

### JS03-L2 — iframe load crashes under forced collection in current-source host — **OPEN**

Found 2026-11-12 during the online visual comparison gate. Precise ownership
must follow **D6.3.3 / D6.4.1**. Cause is **unconfirmed**; this does not block
the selected major visual repairs, but the aggregate Radiant gate remains red.

`test/test_ui_automation_gtest.exe
--gtest_filter=UIAutomation/UIAutomationTest.RunTest/dom_iframe_retire`
fails two of six assertions. The fixture forces collection on every allocation
and poisons freed memory. Its parent loads an embedded Markdown document and
then removes the iframe from a Lambda callback. The rebuilt host reports
`execute_document_scripts: recovered from crash in JS JIT code` before the
embedded document is available. No renderer assertion or test expectation was
weakened.

The saved older executable passes 3/3, but predates the current committed source.
A separate current-source control replaces every changed Radiant object with
its `git show HEAD` source while retaining the same runtime/adapter/input
objects. Both that control and the repaired host fail 3/3, so the issue predates
the native repairs in this round. Evidence: `temp/online-visual/pristine-build.py`,
`pristine-build.log`, `iframe-pristine-{0,1,2}.log`, `iframe-after-{0,1,2}.log`
and `iframe-direct-after.log`. The other aggregate outcomes were 8,163 passing,
299 partial and 20 skipped cases.

### JS03-L1 — durable JS native state retains raw wide-scalar `Item`s — **OPEN**

**Found:** 2026-09-14 during LR08-4's post-fix ownership audit.
**Reproduced against:** current MIR-default `./lambda.exe js` with and without
`LAMBDA_GC_FORCE_EVERY=1`. The AST execution backend passes the collection
probe; that does not make the default MIR path conformant.

**D2.4.2** rules that a raw wide-scalar `Item` pointing into a number frame
must never be retained. **D6.2.2** requires destination-owned scalar storage
at every ownership boundary. GC tracing the `Item` is insufficient: a scalar
home is not a GC allocation, and a trace does not copy its payload into the
durable destination.

Two native JS carriers still violate that rule.

1. **`Error` standard own fields.** `LambdaError` has raw one-word
   `js_name_item`, `js_message_item`, `js_cause_item`, and `js_stack_item`
   fields ([`lambda-error.h:220`](../../lambda/runtime/lambda-error.h)). Both
   ordinary property assignment and the completion setter write an incoming
   `Item` directly ([`js_runtime.cpp:7289`](../../lambda/js/js_runtime.cpp) and
   [`:7431`](../../lambda/js/js_runtime.cpp)). `err_gc_trace` marks the words
   ([`lambda-error.cpp:1250`](../../lambda/runtime/lambda-error.cpp)), but has no
   destination-owned scalar payload to preserve.

   ```js
   function makeError() {
     const error = new Error("tiny");
     error.cause = Number.MIN_VALUE;
     error.stack = -Number.MIN_VALUE;
     return error;
   }
   const error = makeError();
   for (let i = 0; i < 128; i++) {
     const churn = Number.MIN_VALUE * (i + 2);
     if (churn === 1) console.log("unreachable");
   }
   gc();
   console.log(error.cause === Number.MIN_VALUE);  // false
   console.log(error.stack === -Number.MIN_VALUE); // false
   ```

   `cause` and `stack` are reproduced; `name` and `message` take the same raw
   store path and need the same ownership repair. The separate
   `thrown_value_item` path was exercised through a throwing callee and passed;
   it is not part of this defect.

2. **JS `Map` / `Set` collection entries.** `JsCollectionEntry` is copied into
   the hash table and `JsCollectionOrderNode` duplicates its `key`/`value` in
   insertion-order storage ([`js_runtime.cpp:998`](../../lambda/js/js_runtime.cpp),
   [`:19490`](../../lambda/js/js_runtime.cpp)). `js_collection_method` builds the
   entry from raw arguments, calls `hashmap_set`, then calls
   `js_collection_order_upsert` ([`js_runtime.cpp:19708`](../../lambda/js/js_runtime.cpp)).
   Neither destination owns a scalar payload.

   ```js
   function makeMap() {
     return new Map([[Number.MIN_VALUE * 3, Number.MIN_VALUE * 5]]);
   }
   const map = makeMap();
   for (let i = 0; i < 128; i++) {
     const churn = Number.MIN_VALUE * (i + 11);
     if (churn === 1) console.log("unreachable");
   }
   gc();
   console.log(map.get(Number.MIN_VALUE * 3) === Number.MIN_VALUE * 5); // false
   ```

   The failure occurs even without forced collection after `makeMap` returns.
   The shared insertion route also covers `Set` wide-scalar keys and `WeakMap`
   wide-scalar values (WeakMap keys themselves are objects).

**Repair boundary.** Give each durable Error field and each collection-entry
copy storage that owns a scalar payload, and route stores/reads through the
shared rehome helpers. A GC mark alone must not be treated as scalar-lifetime
ownership. Add default-MIR and forced-GC regressions for both probes.

### JS03 known issues

1. **No small-BigInt fast path.** Every BigInt — including `0n`/`1n` and loop counters — is a full `mpd_t` heap allocation (`lambda-decimal.cpp:963`,`983`). An inline-int56 representation for small magnitudes (à la V8's SMI-BigInt) would cut allocation pressure in BigInt-heavy code; today the type is always boxed.
2. **Cached compiled wrappers remain module-lifetime.** The cacheable `js_new_function` path keeps pooled wrappers because the function cache embeds their addresses. Uncached method/`with` wrappers, closures, and bound functions are GC-owned, but repeatedly compiling distinct modules still retains cached wrappers until module teardown.
3. **Module-var ceiling is a hard 2048.** `JS_MAX_MODULE_VARS` (`js_runtime_state.hpp:21`) is fixed; `js_set_module_var` silently drops out-of-range indices (`cpp:124`). A module with >2048 top-level bindings would lose writes rather than grow.
4. **Sentinel values still exist.** `JS_DELETED_SENTINEL_VAL` no longer reuses the INT tag, but it remains a raw non-value `Item` in dense arrays; `ITEM_JS_TDZ` still reuses the UNDEFINED tag. Code that scans dense array items must preserve hole checks. The deleted-sentinel cleanup boundary is tracked in detail in JS06.
5. **Batch reset is a long manual fan-out.** `js_batch_reset` (`js_runtime_state.cpp:271`) hand-enumerates ~30 per-subsystem reset calls; a new stateful module that forgets to register a reset leaks across test262 cases. `js_assert_batch_runtime_state_clear` catches only the capsule fields, not module-private statics.

## 4. Expressions, references & operators (JS04)

### JS04-L1 — Optional deletion throws in the AST interpreter — **RESOLVED**

Found and fixed 2026-11-12. **D8.4.4** requires one semantic contract across
AST/AUTO/MIR; **D8.4.3** also routes dynamic source through the AST. Precise
receiver/key ownership remains **D6.3**. No design ruling changed.

RFC Editor's Nuxt/Vue application replaced its home page with Error 500 and
TypeError. LLDB isolates `delete i.props?.ref` in Vue's vnode handling
(member span 274284..274296). The AST delete path used ordinary reference
evaluation, coercing a nullish optional base and throwing before deletion.
The same script already succeeds in MIR and Node. A minimal reproduction is
`delete ({}).props?.ref`, which must return true. Earlier array-sort exceptions
in the application are caught library feature probes, not this root cause.

Delete now requests optional short-circuit information from reference
evaluation and returns true before evaluating a skipped key or delete trap.
Member reads, calls and optional deletion reuse one member-base helper,
preserving grouping, receiver identity and ordinary assignment behavior.
`optional_chain_delete.js` has 15 Node-checked golden results for null/undefined,
continuations and calls, skipped computed keys, one-time base evaluation,
real deletion, getter avoidance, Proxy traps, grouping, nonconfigurable and
strict deletion, and an unresolved base. All pass in AST and MIR; seven
short-circuit cases throw before repair in AST.

All 574 AST baseline tests and 14 MIR optional/deletion/super neighbors pass,
as do 630 JavaScript layout cases and both DOM UI integration fixtures.
The live RFC Editor home page now retains its heading, search and RFC cards.
Validation uses the compatible host with an exact copy of the current repaired
js_interp.cpp; the main checkout's unrelated paint API mismatch remains.
Evidence: `temp/online-visual-round6/{rfc-detail.log,delete-before-ast-output.log,
delete-after-ast-output.log,delete-after-mir-output.log,js-baseline-ast-final.log,
rfc-live.png}`. No framework code, expected failure, admission limit or capture
deadline was changed.

## 5. Functions, closures & scope (JS05)

### JS05-L1 — `with` scope of a suspended generator leaks to unrelated code — **RESOLVED**

**Found:** 2026-09-11. **Reproduced against:** a build of unmodified `master`
at `ef03733de`. **Fixed:** 2026-09-11 by JSCU44
([`Lambda_Design_Structs_JS.md`](../Lambda_Design_Structs_JS.md) §18), the
structural option below. Regression:
`test/js/regression_with_generator_scope.js`.

A generator that suspends inside a `with` block leaves its scope object on the
process-wide with-scope stack. Every name resolution that runs while the
generator is suspended — including unrelated top-level code — resolves against
that object.

```js
function* g() {
  const o = { zz: 1 };
  with (o) { yield 1; yield 2; }
}
const it = g();
it.next();                          // suspend INSIDE with (o)
console.log("outside:", typeof zz); // spec: "undefined"
it.next();
```

| engine | output |
|---|---|
| `./lambda.exe js` | `number` |
| `node` | `undefined` |

The leak is observable, not merely a lifetime hazard: `zz` is readable and
writable from outside the generator for as long as it stays suspended, and a
`with` whose scope object shadows a real global will shadow it globally.

**Root cause.** The with-scope chain is one runtime-wide stack
(`JsRuntimeState.with_scope.stack`, [`js_runtime_state.hpp:269`](../../lambda/js/js_runtime_state.hpp)),
pushed and popped by MIR-emitted calls to `js_with_push` / `js_with_pop`
([`js_mir_statement_lowering.cpp:3693`](../../lambda/js/js_mir_statement_lowering.cpp),
`:3702`). Generator suspension unwinds the native activation without unwinding
that stack: nothing spills the entries the suspended frame owns, and nothing
restores them on resume. The call kernel's swap
(`js_with_set_stack(js_fn_with(fn)->env, …)`,
[`js_runtime.cpp:14045`](../../lambda/js/js_runtime.cpp)) covers ordinary calls but
is not reached by a suspension, and it is additionally skipped whenever
`common_lane` is taken (`:14042`).

This is the same structural defect as the three with-scope use-after-frees
fixed 2026-07-24 (`Lambda_Design_Stack_API.md` Appendix A.2): the chain is
stored process-wide rather than in the activation that owns it. Those were
fixed with rooting guards; this one is not a rooting bug and a guard cannot
reach it.

**Related, not the same bug.** `common_lane` skipping the with save/restore
meant a callee on the fast lane also observed the caller's with-scope. That
path was never isolated with its own repro — the case that first surfaced this
had a suspended generator in scope — but JSCU44 closes it regardless: the
activation boundary now runs on every lane, guarded only by "neither side has a
with-scope". Case 3 of the regression test covers it.

**Fix taken.** JSCU44's per-activation chain. A scope open across a suspension
parks in a generator env slot and closes before the state machine returns, so
scopes are activation-bounded and live in root-stack slots reserved with the
pushing frame. The chain head is per-activation, which is what the fix needed.
A generator or async frame captures its lexical chain at creation, so a resume
never inherits the resuming turn's chain.

The call-boundary copy is gone with it: the callee's inherited chain is its
closure's existing `js_alloc_env` capture, borrowed by one frame rather than
copied into a process-wide stack. `js_with_set_stack`, `js_with_save_stack` and
`JsSavedWithScope` are retired.

**Gate for any fix.** A regression test alongside
`test/js/regression_with_stack_gc.js` (auto-discovered by the baseline and
wired into `make test-gc-rooting-core`), plus `make test262-baseline` at zero
regressions.

**Note on Appendix A.2.** Updated 2026-09-11: that table described
`js_with_stack` as a "16-slot array" and listed the `super_this_*` stacks as
`JsItemStack`, both long stale. Its rows now record the JSCU44 chain, the
`RootVector` clients, and super-this's move to
`js_call_activation_item(JS_CALL_ACTIVATION_SUPER_THIS)`.

### JS05-L2 — an abrupt jump closes every open `with`, not the ones it leaves — **RESOLVED**

**Found:** 2026-09-11 while auditing JSCU44 for further retirement.
**Reproduced against:** `master` lowering, untouched by JSCU44
(`js_mir_completion.cpp` last changed by `2fbc777ac`). **Fixed:** 2026-09-11.
Regression: `test/js/regression_with_abrupt_jump.js`.

`jm_emit_abrupt_jump_cleanup` emitted one `js_with_pop` per scope open anywhere
in the function, so a `break` that does **not** leave the `with` still closed
it and the rest of the body lost its bindings.

```js
const o = { a: 1 };
with (o) {
  for (let i = 0; i < 2; i++) { if (i) break; }
  console.log(typeof a);   // spec: "number"
}
```

| engine | output |
|---|---|
| `./lambda.exe js` (before) | `undefined` |
| `node` | `number` |

**Root cause.** The emitter knew how many scopes were open (`mt->with_depth`)
but not how many the jump crossed. Its sibling loop over try contexts already
gets this right by comparing `tc->loop_depth_at_push` against the jump target;
the `with` loop had no equivalent.

**Fix.** `JsLoopLabels` records `with_depth_at_push`, set by
`jm_push_loop_labels`, and the cleanup pops down to the target's floor. An
unresolved target (`-1`) still exits the function, so every open scope goes.
Labelled `with` works because the label entry is pushed before the body raises
`mt->with_depth`.

**Independent of JSCU44,** but easier to reason about after it: an over-pop used
to corrupt a process-wide stack shared by every activation, and now cannot
escape the activation that made the jump.

### JS05-L4 — `return`/`throw` did not close the `with` scopes they left — **RESOLVED**

**Found:** 2026-09-12 while auditing what still compensated for JSCU44.
**Fixed:** 2026-09-12 (`return` first, then `throw`). Regressions:
`test/js/regression_with_return_unwind.js`,
`test/js/regression_with_throw_unwind.js`.

The lowering unwound `with` only on the paths that stayed inside the function —
falling off the body, `break`/`continue` (JS05-L2). A `return` or `throw` that
left the function emitted no pop at all, so a callee handed its scopes to
whoever ran next. Two compensations hid it: the direct-call lane bracketed every
call to a `uses_with` callee with `js_with_save_depth`/`js_with_restore_depth`,
and `js_with_activation_leave` walked the chain freeing whatever the callee had
left. Neither is where the knowledge lives — only the callee's own lowering
knows which scopes a completion crosses — and neither covers a callee reached on
a lane that brackets nothing.

**Fix.** One emitter, `jm_emit_with_unwind_to(mt, floor)`, called at each
completion site with the floor that completion actually leaves; see
[`Lambda_Design_Structs_JS.md` §18](../Lambda_Design_Structs_JS.md) for the table.
Both compensations are then retired: `js_with_activation_leave` becomes a head
restore, and the direct-call lane emits nothing. `js_with_save_depth` /
`js_with_restore_depth` survive as the try/`using` brackets, which is the one
place a depth rather than a count is the right handle — a throw's landing point
is a label, not an emission site.

### JS05-L3 — a generator closure created inside `with` fails only under the batched suite — **OPEN**

**Found:** 2026-09-12 while extending the JSCU44 regressions. **Not caused by
JSCU44:** the same repro fails identically on `1146fec84`, the commit before
stage 1, built and run the same way.

Nine lines, correct standalone, wrong under `test_js_gtest`:

```js
function probeMakeGen() {
  const o = { g: 3 };
  with (o) {
    return function* () { yield typeof g; yield g; };
  }
}
const probeGen = probeMakeGen()();
console.log(probeGen.next().value === "number");   // both print true...
console.log(probeGen.next().value === 3);
```

| how it is run | result |
|---|---|
| `./lambda.exe js <file> --no-log` | exit 0, `true` / `true` |
| `./lambda.exe js-test-batch` with only this file | `BATCH_END 0`, `true` / `true` |
| `js-test-batch` with the 50-script chunk that contains it | `BATCH_END 0`, all 50 fine |
| full `test_js_gtest` run | **fails** — no `BATCH_START`/`BATCH_END` record for the script, and the standalone retry returns NULL |

So it is neither the script nor batch execution as such. The harness runs
sub-batches of 50 **in parallel** (`JS_BATCH_CHUNK_SIZE`,
`test/test_js_gtest.cpp:492`), and the failure only appears under that load.

**Not root-caused.** What is established is the boundary: `with` + *generator*
closure. The async-closure equivalent
(`with (o) { return async function () { … } }`) passes in the same full-suite
run, and is covered by case 5 of `regression_with_async_scope.js`. Whether this
is the parallel-load flakiness already recorded for heavy tests, or something
specific to a generator capturing a with-chain, is open.

**Why it is not in the suite.** Adding the repro as a test file makes
`test_js_gtest` fail, so it lives in the ledger rather than as a known-failing
test. The generator half of the JSCU44 async regression was trimmed for the
same reason; the case is recorded here instead.


### JS05-L5 — a closure created by direct eval loses projected caller locals — **RESOLVED**

**Found:** 2026-09-24 during the JSI35 move of dynamic code to the AST interpreter (**D8.4.3**). **Pre-existing:** the MIR eval unit behaved the same way. Both tiers fail it.

```js
function f(a) { return eval("() => a"); }
f(7)();                  // ReferenceError: a is not defined (Node: 7)
```

A direct eval sees its caller's bindings only through the `EvalContext` bridge. The bridge projects caller cells as temporary global properties and writes them back when the eval returns (`JsInterpEvalBridge` in `lambda/js/js_interp.cpp`; `jm_emit_intrinsic_direct_eval` for MIR callers). A closure created inside eval code resolves `a` as a free name. Once the bridge frame pops, that name is gone. Closures over variables the eval itself declares are unaffected; they keep the eval script's own slot.

**Retirement path:** link a T0 caller's live environment records to the eval activation (`vibe/Lambda_Design_JS_Interpreter.md` §5.6 residual and §16 item 13). MIR callers keep the bridge until P4 shared environment cells.

**Fixed:** 2026-09-24 for interpreted callers (**D8.4.3 / JSI36**, `vibe/Lambda_Design_JS_Interpreter.md` §16 item 13). Regression: `test/js/regression_js05_eval_env_linking.js`. A direct eval from AST code no longer installs the bridge. Its activation gets a boundary record (`JsInterpEnv::eval_script`) holding the eval's lexical declarations, and that record's `outer` is the caller frame's live environment. Free names in eval code — and in closures it creates — resolve by name through that chain (`js_interp_lookup_name`), so `f(7)()` above reads the caller's `a` after the eval returns. The `JsInterpEvalBridge` projection, its write-back, and the per-activation journal frames on T0 functions are deleted. A MIR caller still uses the bridge, so a closure made by its eval code still loses the local; that is the P4 residual of §16 item 13.

### JS05-L6 — `arguments` and `new.target` are unavailable inside direct eval — **RESOLVED**

**Found:** 2026-09-24 (JSI35). **Pre-existing** in both tiers.

- `function f() { return eval("arguments.length"); } f(1, 2)` → ReferenceError. `arguments` is not a bridged binding, and a MIR function does not materialize it for a direct eval.
- `function F() { this.t = eval("typeof new.target"); } new F().t` → `"undefined"` (Node: `"function"`). Direct eval code always runs with an undefined `new.target`. Field initializers rely on that; ordinary functions should see their own `new.target`.

Both fall out of the same environment-linking work as JS05-L5.

**Fixed:** 2026-09-24 for interpreted callers, with JS05-L5. The eval frame borrows the caller frame's `this`, new.target, and home-object homes, so `new F().t` is `"function"` and a `super()` in eval code initializes the caller's `this`. `arguments` resolves through the caller's function record. A function whose body or nested arrow contains a direct eval now materializes it (`JsAstFunctionFacts::observes_direct_eval` feeds `JsCallableCode::uses_arguments` and blocks environment elision). MIR callers are unchanged (P4 residual).

### JS05-L7 — `var x` in direct eval reads `undefined` over the caller's own `x` — **RESOLVED**

**Found:** 2026-09-24 (JSI35). **Pre-existing** (MIR eval unit parity).

```js
function f() { var x = 1; return eval("var x; x"); }   // undefined (spec: 1)
```

The bridge flattens every enclosing scope into one namespace. EvalDeclaration- Instantiation must not alias an *outer* function's `x` (test262 `S11.13.1_A6_T1` pins that down), and the bridge cannot tell the caller's own `x` from an outer one. So an eval `var` starts from the activation journal or `undefined`. Assignments still reach the caller: `var x = 2` updates the journal, which the caller reads first. Environment linking resolves it.

**Fixed:** 2026-09-24 for interpreted callers, with JS05-L5. EvalDeclarationInstantiation now targets the caller's nearest function variable environment (`js_interp_instantiate_linked_eval`). A name that record already binds is reused, so `var x; x` reads 1. A new name becomes a deletable eval binding on that record. An outer function's `x` is never aliased, because only the variable environment itself is consulted, and a Reference resolved before its right-hand side keeps its binding (`S11.13.1_A6_T1`). The same instantiation rejects a var that a caller block or parameter record already binds, except a catch parameter (Annex B.3.4). It skips an Annex-B block function whose name such a record binds (B.3.2.3). MIR callers keep the journal rule (P4 residual).

### JS05-L8 — a lexical binding declared inside a `with` body loses to the with object — **OPEN**

**Found:** 2026-09-24 while checking name-resolution order for JS05-L5. **Pre-existing** in the interpreter; MIR returns 2.

```js
function f() { var o = { x: 1 }; with (o) { let x = 2; return x; } }
f();                      // AST: 1 (MIR and Node: 2)
```

The block record holding `let x` is inside the object environment, so it answers first. The interpreter instead probes the `with` stack before any static binding. The only exception is a binding that precedes the function's *captured* `with` depth (`js_interp_with_minimum_depth`). Nothing records where a `with` statement sits relative to the block records opened inside its body, so the probe cannot stop at them.

---

### JS05 known issues

1. **for-of `let`-binding per-iteration capture.** Loop `let`/`const` bindings need a fresh binding per iteration, which is why `jm_create_func_or_closure` refuses the shared scope env when `iteration_depth > 0` and a capture is `let`/`const` (`js_mir_expression_lowering.cpp:11677`). The write-back/read-back path for the *copied* env then carries iteration-boundary hazards: the for-loop and block boundaries explicitly reset `last_closure_*` to stop a stale closure from capturing a later iteration's binding (`js_mir_statement_lowering.cpp:1473`, `:5426`). This is correctness-critical bookkeeping rather than a clean per-iteration-binding model, and is a likely source of subtle stale-capture bugs in for-of bodies.
2. **Fixed 512-capture / 16-name arrays.** The `last_closure_*` parallel arrays are fixed `[512][128]` (`js_mir_context.hpp:426`), but the registration loop only fills the first 16 entries (`js_mir_expression_lowering.cpp:11782`), so closures with >16 captures lose write-back/read-back for the overflow. Several capture-related loops are similarly capped at 16. *Improvement:* size the registration to the array, or grow dynamically.
3. **Arrow-`this` is capture-based, not a binding chain.** Arrows reify `_js_this`/`_js_new.target`/`_js_arguments` as synthetic captures resolved at closure-creation time. This works but means every arrow in a deep nest re-captures and re-stores the lexical receiver; there is no shared `this`-binding object. The runtime accessor `js_get_lexical_this_binding` exists precisely to paper over the TDZ-vs-resolved distinction across the save/restore boundary.
4. **Inliner / native-version type assumptions.** `jm_should_inline` and `jm_transpile_inline_native` assume `param_types`/`return_type` hold for native-versioned functions; a comment at the call site records that a broader call-site widening attempt "caused regressions … Object.defineProperty, Object.seal" and was reverted (`js_mir_expression_lowering.cpp:9621`). Widening inlining beyond native-versioned callees is a known hazard.
5. ~~**TCO is self-recursion only, with a magic iteration cap.**~~ *Resolved 2026-09-15 (JC24):* the loop rewrite was removed. It skipped `finally` for a self-tail call inside `try`, and its 1,000,000-iteration cap returned 0 instead of throwing `RangeError`. No backend eliminates tail calls, and deep recursion ends at the JC23 stack guard. How deep that is depends on frame sizes, which differ by backend and build (S7.11.4).
6. **`with` defeats the function cache and most fast paths.** An active `with` environment bypasses the `func_ptr` → `JsFunction*` cache (`js_runtime_function.cpp:158`) and forces dynamic dispatch (`js_mir_expression_lowering.cpp:9465`), so `with`-scoped functions get neither identity caching nor native/direct calls.

## 6. Objects, Properties & Prototypes (JS06)

### JS06 known issues

1. **Dense array hole sentinel remains.** `JS_DELETED_SENTINEL_VAL` (`js_runtime.h:26`) now uses unused tag `0x7E` and marks dense `Array::items` holes, not ordinary descriptor metadata. Ordinary map/FUNC/ARRAY companion-map deletes use `JSPD_DELETED`; readers that may inspect dense array items still preserve a raw-hole check, while ordinary property readers use `js_own_shape_slot_status`.
2. **Property dispatch decomposition status.** The post-Js59 cleanup split MAP built-in fallback out of `js_property_get`, ordinary MAP tombstone/configurable/frozen handling plus ARRAY/FUNC/string-exotic branches out of `js_delete_property`, ARRAY/MAP/FUNC branches out of `js_property_set`, and the validation/apply phases out of `ValidateAndApplyPropertyDescriptor`. Sparse array accessor hole-fill now lives in the descriptor write kernel (`js_define_own_property_from_descriptor`), so no Js59-specific dispatch cleanup remains.
3. **Catalog construction lookup.** The catalog builds fixed owner/name and ID indexes when the realm's intrinsic bindings are populated. Ordinary property reads never consult those indexes. Further indexing is justified only if release startup profiling shows catalog construction is material.
4. **TypeMap invariant — implemented.** Runtime JS Maps carry valid TypeMaps
  with immutable metadata before publication. Foreign/Input Maps remain at the
  explicit boundary and are not mutated merely by observation. Debug checks and
  the Tune6 census enforce **D3.4.1/D3.4.5/D3.4.7**.

## 7. Classes (JS07)

### JS07 known issues

1. **Class-expression inner-name scope leak.** The immutable inner class-name binding is tracked via `inner_module_var_index` and `alias_name` on `JsClassEntry`, but the lowering for named class *expressions* writes the class object into module-var slots driven by name (`js_mir_statement_lowering.cpp:4718`+) rather than a fully isolated lexical scope; an inner name can be observable beyond the strict class-body scope the spec mandates.
2. **Boolean/String subclass primitive-slot loss.** Subclasses route through the builtin `[[Construct]]` (`js_builtin_super_constructs_via_construct`, `js_runtime.cpp:11448`) to get `[[BooleanData]]`/`[[StringData]]`, but because the instance is then re-prototyped and merged via `js_object_assign`, edge cases where the primitive internal slot must survive `super()`-then-field-init are fragile.
3. **TypedArray subclass species/resize.** Construction now preserves explicit `newTarget` and stored element policy, but `@@species`-driven resize on subclassed typed arrays is not fully wired through every resizing operation.
4. **Private brand ownership.** Private names use NamePool private identity;
   public spelling prefixes cannot forge a class brand. Derived-class deferred
   initialization remains governed by `js_init_class_instance_fields`.
5. **Fixed method/field caps.** `methods[128]`, `static_fields[16]`, `instance_fields[32]`, `static_blocks[8]`, and `ctor_prop_*[16]` (`js_mir_context.hpp:317`+) are hard limits; a class exceeding any of them silently truncates (e.g. field-definition collection guards `static_field_count < 16` at `js_mir_function_collection_class_inference.cpp:707`). *Improvement:* grow these to dynamic arrays like `JsFuncCollected.captures`.
6. **Metadata boundary.** Class identity is resolved through immutable
   `TypeMap::js_meta` and `js_object_meta(Item)` under **D3.4.7**. Constructor
   and prototype relationships are ordinary properties/internal prototype
   state; spelling-based class probes and callable class Maps are retired.

## 8. Iterators, Generators & Destructuring (JS08)

### JS08 known issues

1. ~~**Max 64 yield/await states per generator.**~~ Resolved 2026-10-05 (Runtime_Async P4): there are no resume labels or state budget; a body parks at any number of `yield`/`await` points.

2. ~~**Yield-in-destructuring relies on conservative spill heuristics.**~~ Resolved 2026-10-05 (Runtime_Async P4): the expression spills (`jm_gen_spill_*`), the yield/await counting and the defensive unused labels are deleted; only the active-iterator env slot remains.

3. **for-of early-exit `.return()` gaps for fixed-layout iterators.** Built-in fixed-layout iterators have no `return` method, so `js_iterator_close` is a no-op for them — correct for built-ins, but a user iterator is honored through the generic property path. (The former depth-16 bound on the synthetic try-context stack is gone: `try_ctx_stack` is a growable `ArrayList`.)

4. ~~**Async-generator await modeled as yield.**~~ Resolved 2026-10-05: an await and a yield are two park kinds (`JS_GEN_PARK_AWAIT`/`JS_GEN_PARK_YIELD`) on one activation, with no shared state budget.

5. ~~**Fixed generator pool, no GC reclamation.**~~ Resolved: a generator's record lives inline in its GC-owned carrier (JSCU9), and an unfinished generator that is collected abandons its parked activation (`js_generator_map_heap_destroy`).

6. **Prototype override fallback.** If a built-in iterator prototype is overridden, construction falls back to a shape-backed iterator object so the user-visible `next` property and custom prototype are honored; the fixed-layout carrier remains the default path.

## 9. Async, Promises, Event Loop & Modules (JS09)

### JS09 known issues

Grounded in the current code; candidates for cleanup, not necessarily bugs.

1. ~~**TLA is "first-await-per-body only."**~~ Resolved 2026-10-05 (Runtime_Async P4): a module with a top-level await runs on a module carrier in both tiers and parks at every top-level `await`; the single-shot split is deleted.
2. **Named-import live binding is snapshot-only except self-import default.** Only `is_live_default_binding` self-imports route through `js_get_live_binding_default` (`js_mir_context.hpp:86`); general `import { x }` reads resolve to a module-state slot snapshot. Cross-module mutation of an exported `let` after import is therefore not observed live.
3. **Circular ESM relies on placeholder namespaces.** `jm_load_imports` registers an empty placeholder before recursing (`:4501`); a cycle that reads a not-yet-exported binding sees `undefined`/missing rather than a TDZ ReferenceError. Strongly connected components are recognized only for async evaluation (`js_module_mark_async_cycle`, which gives async parents the cycle root), not in a spec-style link phase.
4. ~~**Fixed pools with hard caps.**~~ Resolved: a `JsPromise` is a GC-owned carrier whose reactions are a growable GC array (`js_runtime_state.hpp:555`); async frames (JSCU10) and generators (JSCU9) are GC-owned carriers; module TLA state lives on registry descriptors with growable edge lists; nextTick, microtask and animation-frame jobs use growable `RuntimeJobQueue`s.
5. ~~**Vestigial `js_save_module_vars`/`js_restore_module_vars`.**~~ Resolved: deleted along with `js_alloc_module_vars`; module state is a per-context slab restored by scopes.
6. **`js_await_sync` cannot suspend.** Only an await with no activation to park on reaches it (a script's top level, the MIR entry module's top level, a host wait). If the awaited promise is still pending once the job queues are empty, it falls back to a bounded busy-drain (`js_event_loop.cpp:1599`) and reads as `undefined` if it cannot settle in-turn — a deliberate divergence from real suspension, documented against the Js55 1675 s regression (`:1581`). Anything there that depends on a cross-thread/cross-event settlement needing more than 64 turns silently observes `undefined`.
7. ~~**SIGSEGV guard around the drain is a band-aid.**~~ Resolved: the guard that survived "heap corruption in timer callbacks" was removed on 2026-07-31 (ER-S6), so a fault there now fails the process, as S7.11.4 requires, and timer re-entry became an owner check on 2026-07-29 (JO12). `test/js/timer_callback_gc_stress.js` and `dom_timer_callback_gc_stress.js` drive allocating callbacks, nested scheduling, clears and promise jobs through the drain in the forced-GC lane with freed memory poisoned (2026-09-26, central ledger §15.1).
8. ~~**Promise wrapper uses the bounded JR7 adapter.**~~ Resolved: the user-visible Promise object is the `JsPromise` carrier itself (a `VMap`, `js_promise_to_item`, `js_runtime.cpp:30508`); there is no `__promise_idx` expando or record pool.

## 10. Standard Built-in Library (JS10)

### JS10 known issues

1. **Catalog indexes are fixed-capacity construction structures.** Owner/property and ID lookups use bounded open-addressed indexes built from the catalog. They are absent from ordinary property reads, but realm construction should still profile collision and startup cost as the catalog grows.
2. **`globalThis` is a snapshot, not live.** Standard constructors/namespaces are materialized once as own properties (`:14329`+). Reassigning e.g. `globalThis.Array` does not retroactively change earlier-cached references, and freshly mutated prototypes after the snapshot are not re-mirrored except across a full `js_globals_batch_reset`.
3. **WeakMap/WeakSet/WeakRef have no real weak semantics.** `is_weak` only gates a key-eligibility check; entries are retained strongly by the same hashmap + order list as strong collections (`js_runtime.cpp:32476`, `:17201`), and `WeakRef.deref` never clears (`:32496`). This is a correctness gap for finalization-observing code, accepted because it requires GC-integrated ephemeron support.
4. **`js_globals.cpp` remains large.** Many lower algorithms (Date operations, JSON serialization, descriptor validation) remain hand-written switches or ladders. These are selected by typed target bodies rather than catalog/name dispatch, but coherent algorithm-family extraction would improve ownership and reviewability.
5. **Host functions can terminate the process.** `process.exit` calls `exit(code)` (`js_globals.cpp:2360`) and `process.abort` calls `abort()` (`:2809`) directly — there is no embedder hook to intercept, so a script can hard-kill the host. *Improvement:* route through an injectable termination callback.
6. **Date stores a visible engine value.** The `__time__` slot is implementation state for Date algorithms, while class identity is selected once by immutable TypeMap metadata; it is not a public brand marker.
7. **Fixed-size string-registry keys.** `JsSymbolEntry.key`/`JsSymbolDesc.desc` are `char[128]` (`js_globals.cpp:16518`, `:16528`); symbol descriptions longer than 127 bytes are truncated in the `Symbol.for`/description registries.

## 11. RegExp Engine (JS11)

### JS11 known issues

1. **RE2 leftmost-longest ≠ JS leftmost-greedy.** RE2's match semantics differ from ECMAScript's greedy/backtracking model. For most patterns the result is identical, but the wrapper's backreference two-pass (`js_regex_wrapper.cpp:2368`) exists precisely because the widened RE2 pattern can choose a shorter referenced capture than JS would; it papers over the mismatch for backrefs that share one referenced group but does not generalize to every greedy-vs-longest divergence.
2. **Lookbehind limits on the RE2 path.** Wrapper lookbehind compiles its body to an *unanchored* RE2 slice and checks a boundary offset (`compile_lookbehind_re`, `js_regex_wrapper.cpp:453`); bodies with captures, backreferences, `\b`/`\B`, alternation, or variable length are deliberately *not* handled here — `js_regex_needs_backtrack` routes those to the `bt` engine instead. A lookbehind that reaches the *direct* RE2 fallback is silently stripped (`js_runtime.cpp:15254`), yielding an approximate match rather than a SyntaxError.
3. **`/v` gaps.** The `/v` rewriter flattens nested classes and set operators into RE2 syntax (`rewrite_v_flag_classes`, `js_regex_wrapper.cpp:1115`), and the string-property tables only matter under `v`; multi-codepoint `\q{…}` alternation and full `/v` string-set semantics are an area where coverage against Test262 should be re-verified rather than assumed complete.
4. **Backtracking-engine coverage & routing.** The `bt` engine is a *fallback*, entered only when `js_regex_needs_backtrack` fires; the routing heuristic is conservative (e.g. it intentionally keeps unbounded-nullable shapes like `(.*\n?)*` on RE2 to avoid catastrophic backtracking, `js_runtime.cpp:13749`). A pattern the heuristic misroutes to RE2 can therefore produce a wrong capture; and a genuinely pathological pattern that reaches `bt` bails to "no match" once the 8M-step / 5000-depth budget is hit (`js_bt_regex.cpp:1018`), which is a silent non-match rather than a spec-mandated result.
5. **Property-escape memory.** The two generated `.inc` tables are large (~16K lines combined) and compiled into the binary; lookups are linear scans over the table array (`js_runtime.cpp:12714`). *Improvement:* index the tables by name (sorted + bsearch or a hash) and consider sharing range storage between aliases.
6. **Fixed capture ceilings.** Several buffers are fixed-size: `JS_REGEX_MAX_GROUPS == 256` (`js_regex_wrapper.h:25`), `JS_REGEX_MAX_FILTERS == 16` (`js_regex_wrapper.h:20`), `scan_assertions` accepts at most 32 assertions, and `js_bt_exec` falls back when groups exceed 256 (`js_bt_regex.cpp:1011`). Patterns past these limits silently lose captures or fail to route.

## 12. TypedArrays, Binary Data & Atomics (JS12)

### JS12 known issues

1. **TypedArray copy-method algorithms remain shared.** `toReversed`/`toSorted`/`with` now have distinct catalog targets and realm-local function identities, but their bodies reuse the common TypedArray algorithm layer. The result must still be created through TypedArray species construction and revalidation; detaching callbacks remain the important edge cases.
2. **The “stub” owner name is historical.** `set`/`subarray`/`toLocaleString` live under `JS_BUILTIN_OWNER_TYPED_ARRAY_STUB_METHOD`, but each row has a real direct catalog target and is installed as a normal property. The owner should be renamed in a mechanical cleanup; there is no placeholder or miss-time dispatch.
3. **Duplicated scalar-store semantics.** The scalar write path (`js_typed_array_set` `js_typed_array.cpp:2526`) narrows and clamps with an inline switch, while the bulk paths store through `js_typed_array_arraynum_store_number` → `array_num_set_*_value`; ToUint8Clamp's round-half-even in particular is implemented twice (`:2529` and `array_num_clamp_uint8_even` `lambda-data-runtime.cpp:29`). The two must be kept in agreement by hand. *Improvement:* route the scalar path through the ArrayNum store helper once the JIT-visible cost is measured.
4. **The refresh discipline is convention, not construction.** Every entry point must call `js_typed_array_refresh_arraynum_view` before touching `ta->view`; a new code path that forgets the call reads a stale `view->data` after a resizable-buffer `resize` reallocs. Nothing enforces this statically — the invariant lives in review.
5. **Subclass species-resize coverage.** `js_typed_array_species_create` now tests construct capability and invokes `js_construct_value`; concrete TypedArray constructors carry their immutable element policy on the function object, so renamed or shadowing constructors cannot change allocation selection (**D7.2.2**). Resizable-buffer corner cases still need broader species/resize conformance coverage.
6. **Freeze on a resizable-buffer-backed TypedArray is under-specified.** The
   metadata ops table still needs dedicated `Object.freeze` handling for a
   length-tracking TypedArray; a frozen-then-resized array's observable length
   and integer-index writability are not yet covered by the full spec matrix.
7. **TypedArray construction policy is split from active binding state.** Concrete constructors store `typed_array_element_type_plus_one` as immutable allocation policy, while `js_construct_value` passes an explicit `newTarget` and scopes the active binding. The construct body applies `newTarget.prototype` at the allocation boundary; there is no pending construction state. Source-class constructors are `JsFunction` values as described in JS07.
8. **Linear name dispatch in `js_dataview_method`.** Every DataView accessor call walks a chain of `strncmp` comparisons (`js_typed_array.cpp:3120`+) rather than a sorted/hashed table; on a hot serialization loop this is measurable. *Improvement:* dispatch on a small perfect hash of name length + first char.
9. **The NULL-buffer `.buffer` synthesis path is vestigial.** `js_typed_array_new` now always allocates and wraps a backing `JsArrayBuffer` eagerly (`:2100`), so the exotic-gate fallback that lazily synthesizes a buffer for a `ta->buffer == NULL` array (`js_runtime.cpp:3283`) should be unreachable for engine-constructed arrays; it survives as a guard for exotic construction paths and could be demoted to an assert.

## 13. Web Platform: DOM, CSSOM, Events & Fetch (JS13)

### JS13-L6 — Existing details elements lack their JavaScript interface — **FIXED**

Found 2026-11-12. Native/runtime ownership follows **D9.1.1v4 / D9.1.3v2**.
The native tag-interface registry omitted `details`, although native `open`
reflection, summary activation and exclusive groups already existed. Join's
company-page module checks `instanceof HTMLDetailsElement` in its language
switcher before installing section-reveal observers, and raised ReferenceError.

The registry now maps `details` to `HTMLDetailsElement`. The existing installer
creates a distinct illegal constructor and its prototype under `HTMLElement`;
parsed and created wrappers use that same registry. No generic-interface alias
or page-specific branch is added. `test/ui/dom/details_interface` checks global
identity, parsed/created branding, negative branding, prototype/constructor
identity, string tag, illegal construction, native reflection, continuation
pixels and summary activation: previous renderer 0/9, repaired renderer 9/9.
The existing resource-interface control passes 5/5.

Join's live startup exception disappears, but its main content remains hidden
at the selected virtual frame. This is not a major-page clearance. In an
archived read-only trace, its reveal attribute is set at virtual time zero;
the authored opacity transition is a separate observation. Capture must not
advance time merely to finish that transition (**D10.1.7**). Evidence:
`temp/online-visual-round11/{ui-before.log,ui-final.log,join-live-after-lambda.log,join-no-geometry-results.log}`.

### JS13-L7 — Initial geometry-observer sampling depends on a page geometry read — **FIXED**

Found 2026-11-12. The shared DOM checkpoint follows **D9.1.1v4 / D9.1.3v2**;
snapshot sampling retains the selected frame (**D10.1.7**).
`observer_sample_geometry` skipped targets whose document had no view tree,
before reaching the geometry operation that could establish the first layout.
A standalone export has no later interactive runtime turn to recover the
initial sample. A script observing a visible box styled with `opacity:0` never
received its first intersection/resize callback unless something else queried
geometry first.

Sampling now calls the existing `dom_ensure_geometry_snapshot` checkpoint.
Its host-driven loading/commit and reentry guards remain authoritative; the
observer's sampling guard prevents recursive sampling during layout. No timer,
threshold, admission limit or capture deadline changes. The standalone PNG
regressions cover initial visible intersection, initial settled resize,
initial offscreen notification and disconnect-before-delivery. With no page
geometry reads, the previous renderer fails the first three; all four pass
after the repair. The clip and CSSOM PNG suites reuse a common fixture runner
with these tests, and retain their original pixel expectations.

Qualification: 32 real CLI cases, seven DOM UI fixtures (50 assertions),
574 AST baseline cases, 3,851 recorded static layout cases (six skips), 630
JavaScript layout cases and five comparator tests pass. This repair is
independently reduced; it is not assigned as Join's remaining visual cause.
Evidence: `temp/online-visual-round11/{observers-before.log,cli-final.log,ui-final.log,js-baseline-ast.log,layout-static.log,layout-js.log}`.

### JS13-L4 — Missing serviceWorker.ready aborts application startup — **FIXED**

Found 2026-11-12. Native/runtime ownership follows **D9.1.1v4 / D9.1.3v2**;
the published promise uses precise **D6.3** roots. The existing headless
`navigator.serviceWorker` object installed register/getRegistrations but omitted
`ready`. Greenlist's admitted 83KiB main bundle reads
`navigator.serviceWorker.ready.then(...)` after ReactDOM.render and throws a
TypeError. LLDB isolates that final uncaught member read; earlier exceptions
are caught library feature probes. This source is identical in the current
checkout and the compatible capture host before repair.

The existing empty-registry profile now publishes one read-only pending promise,
rooted along with its property descriptor until the navigator retains it.
[Service Workers §3.4.2](https://w3c.github.io/ServiceWorker/#dom-serviceworkercontainer-ready)
requires readiness to wait for a matching active registration. No registration
or success value is fabricated. `test/ui/dom/service_worker_ready` checks the
Promise, identity, read-only value, empty registration array and pending state
after microtasks: all six assertions pass and the previous qualified host fails.
The isolated real-bundle bootstrap no longer reaches its exception handler.
Greenlist's live capture still lacks styling and hero content; that residual
cause is unconfirmed and is not declared repaired by removing this exception.
Evidence: `temp/online-visual-round5/{greenlist-lldb-python.log,greenlist-ready.json,ready-final-ui-result.json}`.

### JS13-L5 — Service-worker execution and registration lifecycle — **FEATURE GAP**

Observed 2026-11-12 while isolating JS13-L4. Ownership **D9.1.1v4 / D9.1.3v2**.
The existing headless container returns an empty getRegistrations result and
register resolves a placeholder object. It does not fetch/run worker scripts,
persist registrations, activate workers or dispatch fetch events. JS13-L4 only
completes empty-registry readiness; it does not qualify a full branded
ServiceWorkerContainer IDL surface or that existing register placeholder.
Implementing the worker subsystem is a major missing feature outside this
online bug-fix round, not a blocker to unrelated layout or snapshot work.
No remaining page omission is assigned to this gap without page-specific proof.

### JS13-L3 — Root theme mutation is dropped below a Document proxy — **FIXED**

Found 2026-11-12. DOM/host ownership follows **D9.1.1v4 / D9.1.3v2**;
snapshot state follows **D10.1.7**. This blocks correct theme updates on the
Supabase online pages, without requiring a new rendering feature.

Confirmed root cause: `dom_js_record_cascade_root` promotes an attribute edit to
its parent when structural selectors exist. The element-shaped Document proxy
is the HTML root's DOM parent, but is outside the rendered root. The subsequent
connection check drops the edit, while incremental reconciliation still reports
success. An unrelated `.shiki:has(> [data-wrapped=true]) .code-content` rule is
enough to trigger it. A 159-check CSS reduction leaves four rules: root color
variables, a light-theme attribute rule, the unrelated structural rule and the
body background. Static light attributes work; setting them from script fails.

`StyleEpochTest.DocumentElementMutationRetainsRootBelowDocumentProxy` reproduces
the lost inherited value (10px instead of 600px). The fix selects a parent only
if it belongs to the rendered tree; it preserves the Document proxy and DOM
parent relationships. The regression fails before and passes after repair;
all 38 StyleEpoch tests pass. Supabase's home capture now selects light and
its mean color error drops from 89% to 2.4%. The customer page also selects
light but still lacks its main content; clearing a coarse threshold does not
qualify that separate behavior. The aggregate Radiant gate retains only the
previously reproduced JS03-L2 iframe failure. Evidence:
`temp/online-visual-round2/theme-min-rules.json`, `supabase-full-output.log`,
`test-theme-before.log`, and the original/focused browser/native captures.

### JS13-L2 — ShadowRoot APIs do not provide full composed-tree rendering — **PARTIAL**

Observed 2026-11-12 in the online comparison. Native/runtime boundaries follow
**D9.1.1v4 / D9.1.3v2**. This is a major missing rendering feature outside the
user-authorized bug/incomplete-existing-feature milestone; it is not an online
capture blocker and is not implemented in this round.

`lambda/dom/dom.cpp::dom_core_attach_shadow` creates a stable fragment-backed
root and exposes host/mode/focus behavior. Its source explicitly states that
Radiant does not yet render a full shadow tree. WebAssembly.org's
`dark-mode-toggle.min.mjs` constructs its controls and stylesheet in
`this.attachShadow({mode:"open"})`. Supporting that API object does not qualify
its composed layout, shadow-scoped CSS or control rendering. The downloaded
module and page are retained under `temp/online-visual/`; the module is also in
the normal resource cache. This does not establish Shadow DOM as the cause of
every remaining blank page or of that site's remaining theme mismatch.

### JS13-L1 — snapshot script admission can leave the whole document hidden — **OPEN**

Found 2026-11-12. Browser execution belongs to **D8.4**; finite snapshot time
belongs to **D10.1.7**. This limits full application-page fidelity, but does not
block unrelated CSS/layout repairs. No admission limit was raised for matching.

`lambda.exe render https://payloadcms.com/ -o temp/online-visual/payload.png
-vw 1200 -vh 800` completes with a white image. Its authored CSS sets
`html { opacity:0 }` until application code installs `data-theme`. The log shows
three async bundles of 201,060, 241,382 and 248,319 bytes skipped by
`JS_PRELAYOUT_DEFER_BUDGET_BYTES` (128 KiB) in
`lambda/module/radiant/script_runner.cpp`. Skipped tasks are not later executed
for this one-shot snapshot. Source and total admission limits also exist;
large framework pages cannot be treated as fully executed merely because the
document loader and PNG encoder return success.

The immediate hidden-root condition and script admission decisions are confirmed.
Whether admitting the complete application would encounter other unsupported
APIs is unconfirmed. Forcing opacity/theme, removing application scripts, or
advancing arbitrary wall-clock animation would conceal the missing execution.
Evidence: `temp/online-visual/payloadcms.com.html`, downloaded CSS in that
directory, `payload-tree.json` (root opacity zero), and
`probes/saas_landing_313-lambda.log`. Many Framer/app pages also have large visual
differences, but a shared cause has not been established for every page.

Second-review qualification: after the root theme and media repairs, visual
inspection still finds major missing content on Supabase customers, Pitch,
Cofounder and Banky even though their mostly white or restored-background
captures fall below the coarse threshold. These cases remain in the audited
queue. Their individual render logs, authored sources and browser diagnostics
are preserved in `temp/online-visual-round2/` and
`test/online/snapshots/visual_comparison_review_round2.json`. This does not
establish script admission as the shared cause of all four omissions.

### JS13 known issues

1. **DOM API coverage is incomplete.** The declared element/document surface is broad but not complete; browser libraries continue to expose missing Node/Range/style behavior. A missing declared member is observable as `undefined`, so library probes remain part of the JS suite.
2. **No on-read layout flush.** Layout-metric getters read stale `DomElement` geometry; mutations only set `layout_dirty` (`dom.cpp:181`) without forcing a relayout, so a script that mutates then reads `offsetWidth` in the same turn sees pre-mutation pixels (or 0 before first layout). A spec-faithful engine would flush pending layout on metric access.
3. **Canvas remains intentionally narrow.** `OffscreenCanvas` now has ordinary replaceable global-constructor semantics, but the implementation supports text measurement rather than the general Canvas 2D, bitmap, or WebGL surfaces.
4. **Text segmentation remains narrow; Bidi is absent.** `Intl.Segmenter` is a construct-only callable with a realm-local prototype and a basic segment operation, sufficient for current editor libraries, but it is not a complete ICU-grade implementation. There is no bidirectional-text algorithm; FormData/clipboard direction remains hard-coded and canvas fallback width is a per-character heuristic.
5. **`dom.cpp` size & per-access logging.** The file is ~9,500 lines with ~50 `log_debug` call sites on hot get/method paths (e.g. every `dom_get_property` on a non-node logs, `:5340`); the `strcmp` ladder in `dom_get_property` re-tests every property name linearly per access. Both add avoidable per-access overhead in tight DOM loops — see JS15.
6. **O(n) listener and wrapper storage.** Event listeners live in a flat `_entries` array scanned linearly by target key (`dom_events.cpp:277`), and the DOM wrapper identity cache is a linked list of chunks scanned linearly per wrap (`dom.cpp:820`). Documents with many distinct event targets or many wrapped nodes degrade quadratically. *Improvement:* hash both keyed structures.

## 14. Node.js Compatibility Layer (JS14)

### JS14 known issues

1. **Event loop not fully integrated for `fs`.** libuv is wired for TCP/spawn/timers and drained by `js_event_loop_drain` (`js_event_loop.cpp:1197`), but `fs` "async" methods run the operation synchronously and invoke the callback inline (`js_fs_mkdir_async`, `js_fs.cpp:368`) — they never yield to the loop, so ordering relative to timers/microtasks diverges from Node.
2. **Stream internals are stubs.** Subclasses that don't override `_write`/`_transform` throw "The _write() method is not implemented" / "The _transform() method is not implemented" (`js_stream.cpp:447`, `:629`); backpressure, `highWaterMark`, and many state transitions are not modeled. Stream-heavy libraries will not behave correctly.
3. **Crypto coverage is hash/cipher only.** No asymmetric primitives (`createSign`/`createVerify`/`generateKeyPair`/Diffie-Hellman/ECDH); unsupported algorithms generally `log_error` and return `ItemNull` rather than throwing a proper Node `Error`, so callers see `null` instead of a thrown exception. WebCrypto `subtle` remains partial: digest/encrypt/decrypt plus branded `CryptoKey` raw HMAC import/export/sign/verify (SHA-1/256/384/512). HMAC material and usage permissions live in private, precisely rooted records (D6.3.3, D6.3.5); metadata getters return snapshots, and failures reject with the original exception payload (D7.4.3). Explicit HMAC import lengths, asymmetric keys and other key algorithms are unsupported. `CryptoKey` is a lazy browser global backed by node-crypto, with reviewed host ports only (D9.4.6).
4. **No `fs.watch`/`watchFile`.** The watch APIs are absent from `js_fs.cpp`; code that calls them gets `undefined` and will fail when it tries to use the (nonexistent) watcher.
5. **`ERR_*` error codes are incomplete.** A curated set is centralized in `js_error_codes.h` (e.g. `ERR_INVALID_ARG_TYPE`, `ERR_OUT_OF_RANGE`, `ERR_STREAM_DESTROYED`), but many Node code paths either omit the `code` property or surface a plain string/`log_error` instead of a coded `Error`, so `err.code` checks are unreliable.
6. **No `vm` module.** `vm` is not provided: `require('vm')` and `require('node:vm')` raise `MODULE_NOT_FOUND`. It was retired because its contexts were never isolated — code ran against the host's own global and lexical bindings, so the sandboxing guarantee callers rely on did not hold. Code needing dynamic evaluation must use `eval` / indirect `eval`, which are explicit about sharing the caller's realm.
7. **`child_process.fork()` is unimplemented.** Only `exec`/`execSync`/`spawn`/`spawnSync` exist (`js_child_process.cpp`); there is no IPC-channel child-process spawn.
8. **Hand-written `memcmp` dispatch chain.** `js_module_get` is a long linear `if`/`memcmp` ladder (`js_runtime.cpp:31550`+) with three spelling variants per module — adding a module means editing both this chain and `jm_resolve_module_path`'s `builtin_names[]` table, and the two lists can drift out of sync.
9. **External package support.** Installed external packages and `node_modules`
   resolution are outside the `lambda.exe` host build.
10. **No `zlib` module.** `zlib` is not provided: `require('zlib')` and
   `require('node:zlib')` raise `MODULE_NOT_FOUND`. The `node-zlib` Jube module
   was removed on 2026-10-07; it could no longer initialize because the Node
   `stream` Transform services it was built on were gone from the host.

## 15. Performance (JS15)

### JS15-L1 — load time is quadratic in the number of top-level functions — **OPEN**

**Found:** 2026-10-03 as a side finding of the promotion-cost table in
`vibe/impl/Lambda_Impl_JS_Interp_Tune.md` §2 (branch
`fix-continuation-param-inference`). **Reproduced against:** current
`./lambda.exe js` on 2026-10-03, with `JS_TRANSPILE_TIMING=1 --no-log`.
No `S#`/`D#` ruling covers load-time complexity.

**Repro:** [`test/js/slow/top_level_4000_functions.js`](../../test/js/slow/top_level_4000_functions.js)
(354 KB). It declares 4,000 identical functions
`pad0`…`pad3999` (`function padN(a,b){ var o={x:a,y:b}; for (...) ...; return o.x; }`),
then one `add` function and a run of `r=add(r,1);` calls. The file only
declares functions and calls `add`, so its load time should grow linearly.
Under the MIR backend it takes **4.2 s**.

Timings in ms, using the same function body with the count doubled each step:

| Functions | backend | `index_ms` | `mir_lower_ms` | `exec_ms` | `total_ms` |
|---:|---|---:|---:|---:|---:|
| 2,000 | ast | 79 | — | 327 | 448 |
| 4,000 | ast | 311 | — | 1,393 | 1,786 |
| 2,000 | mir | 75 | 701 | 301 | 1,497 |
| 4,000 | mir | 294 | 1,478 | 1,414 | 4,206 |

Two phases grow about 4× per doubling, and that holds for both backends.
`parse_build_ms`, `bind_ms` and `mir_lower_ms` grow about 2×.

1. **`exec_ms`: global declaration instantiation.** For each top-level
   function, `js_define_global_var_property`
   ([`js_globals.cpp:14770`](../../lambda/js/js_globals.cpp)) makes two
   O(n) checks:
   - `js_global_environment_find`
     ([`js_runtime_state.cpp:216`](../../lambda/js/js_runtime_state.cpp)) scans
     every binding backwards and compares keys with `memcmp`. The upsert that
     follows scans them again.
   - `js_has_own_property` on the global object goes to
     `js_map_shape_lookup` ([`js_runtime.cpp:4069`](../../lambda/js/js_runtime.cpp)).
     Its time is self time, which points to the shape-chain fallback, not the
     hash. `typemap_hash_lookup_by_hash` takes that fallback whenever the
     TypeMap has no `field_index` (`lambda-data.hpp:975`). *Unconfirmed:* whether
     the global object's TypeMap is ever given a hash table.

   A `sample` of a 12,000-function load puts almost all non-idle exec time in
   these two functions.
2. **`index_ms`: `AstIndex` pointer-hash probing.** At 12,000 functions, the
   self time during the parse phase is concentrated in
   `ast_index_find` ([`ast-core.cpp:1196`](../../lambda/runtime/ast-core.cpp)), the
   linear-probe loop. The load factor is kept at or below ½
   (`ast-core.cpp:303`), so long chains point to clustering in `ast_ptr_hash`
   (`ast-core.cpp:105`) for sequentially allocated nodes. *Unconfirmed:* the
   probe lengths have not been measured. `ast-core.cpp` is shared with Lambda's
   own front end, so a fix there also applies to `.ls` scripts.

**Fix direction:** give the global environment a hashed key → binding index
(`js_global_environment_find` and upsert), and make sure the global object's
TypeMap keeps its `field_index`. For the AST index, instrument probe lengths
first, then fix the hash or the probing. Re-measure with the doubling series
above. The success criterion is that `index_ms` and `exec_ms` grow about 2×
per doubling.

### JS15 known issues

Still-open performance work, distilled from the logs:

1. **Inline float fields without boxing** — the single biggest remaining gap. Keep shaped float fields in native registers across a loop iteration (scalar replacement of aggregates), or store doubles unboxed in shaped slots, to remove per-access boxing traffic on float-heavy loops (the allocation half is already retired by the side-stack architecture).
2. **Broaden the measured TypeMap family only where warranted.** Literal and
   eligible constructor recipes now serve exact ordinary fields with a guarded
   direct slot and one NameId miss. Do not revive a runtime shape cache;
   additional construction/field families need a profile, a fixed MIR contract,
   and the same immutable metadata rule (**D7.4.1**, **LC1v2**).
3. **Broaden native-index physical hits only where warranted.** A computed
   Number key now stays native to the selector and packed tagged literal reads
   use the shared address/load plan. Other receivers correctly use their
   existing native-key or generic semantic kernels. A new direct family must
   prove its receiver invariants and preserve the one-miss structure; the
   former bounded polymorphic call IC (JR8) remains retired by **D7.4.1**.
4. **Two-operand-non-string ADD inference + fixed-point return types** — recover native integer typing for additive/recursive numeric functions without resurrecting the string-concat unsoundness.
5. **Destination-passing lowering** — the structural fix for the 66–88% MOV volume; a scoped codegen-quality project, gated on full test262 + Radiant re-validation.
6. **De-pointered relocatable MIR + module cache** — unblock cross-compile/cross-realm artifact reuse for the repeated-vendor-JS workload.
7. **Profile large-shape and catalog construction indexes** (JS06) — ordinary reads use the TypeMap hash; intrinsic catalog indexes are construction-time only and should be optimized only from release startup evidence.

## 16. Testing & Conformance Infrastructure (JS16)

### JS16 known issues

1. **Batch-vs-single-script divergence.** Tests that pass in isolation but fail/flake in a 50-test batch are a recurring class — the entire Phase 4 retry + `t262_partial.txt` mechanism exists to detect and quarantine them. Root causes are residual cross-test state (next item) rather than the tested feature; the batch interaction is recorded, not fixed.
2. **Residual unreset statics.** `js_batch_reset` / `js_batch_reset_to` enumerate dozens of module/global reset calls by hand (`js_runtime_state.cpp:271`/`:388`); any process-global static not on that list silently leaks across tests. The two functions are also large near-duplicates that must be kept in sync manually — a missed reset surfaces only as a batch-order-dependent flake.
3. **MIR JIT code-page leak on crash recovery.** Each SIGSEGV/SIGBUS/timeout `longjmp` "leaks ~55MB (MIR code pages, AST, temporaries that skip cleanup)" (`main.cpp:3700`); the worker compensates by capping at 10 crashes / 4 GB RSS and exiting the batch (`:3721`), trading completeness for bounded memory. The leaked code pages are unreachable after the eventual `heap_destroy`, but accumulate within a batch.
4. **`_FIXTURE` files require special-casing.** test262 `_FIXTURE.js` helper modules are not standalone tests and are skipped by suffix match. Module includes remain in the module source; eligible classic AST tests retain helper Scripts.
5. **Config-dependent pass percentages.** Pass counts depend on build type, opt level (the timing TSV is per-`o<N>`), interpreter-vs-JIT mode, async/module admission flags, and host CPU count (which sets worker count and thus batch composition). The baseline header records the exact configuration; treat any absolute number as point-in-time.
6. **Large orchestrator.** The full phase pipeline still has separate Windows, POSIX, and persistent-worker readers. Numeric result and lifecycle parsing are shared; framing and process management remain platform-specific.
7. **Negative-test scoring is coarse.** `evaluate_batch_result` passes a negative test if the output merely contains "Error"/"error" (`:2936`) rather than matching the declared `negative_type`/`negative_phase`, so a test expecting `SyntaxError` would pass on any thrown error.
