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

### JS03-L3 — Nominal Error instances omit captured stacks — **FIXED FOR LAZY STACK ACCESS**

**2026-11-20.** Nominal objects retain their representation under **D3.1.5**;
shared JavaScript behavior follows **D8.4.4**, with precise owners under
**D5.5.1 / D6.3.3**. MVP Error constructors created nominal maps without stack
storage, while native Error carriers already captured raw frame data. React's
component-stack probe on Hera therefore failed at `error.stack.trim()` while
trying to report an earlier error.

The existing private Error-data slot now retains a native carrier for its raw
trace. A configurable, non-enumerable stack accessor delegates to the shared
materializer using the nominal instance's display properties. Construction
does not read subclass name/message accessors. Assignment replaces the stack
accessor with an own data property. Nominal ancestry, cause and AggregateError
entries remain in their existing representation. No parallel formatter or
fabricated frame text is introduced.

Five native controls pass in AUTO, AST and MIR, and the five shared Chromium
controls pass. They cover constructor/call forms, all seven ordinary error
types, AggregateError, implicit and asynchronous exceptions, subclasses,
descriptor mutation, component probes and collection during a lazy name getter.
The renderer control confirms that getter actually invokes GC. Four initial
controls fail on the prior qualified renderer. Exact frame formatting and
broader prepareStackTrace/realm conformance are outside this qualification.

Hera's five traced nullish stack-member failures disappear, exposing the
independent strict title write addressed by JS13-L27. Evidence:
`temp/online-visual-round118/` and `test/online/render_error_stack.test.cjs`.

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

### JS07-L1 — Native superclass property writes lose the public class receiver — **RESOLVED**

Found 2026-11-17 while diagnosing Archive.org's blank online capture. Under
**D8.4.4 / D7.2.2**, a class shell owns its observable static properties;
its captured AST constructor function is executable payload. The foreign
property bridge and native-call receiver conversion unwrapped explicit
constructors into that payload. Static assignment across `HTMLElement`
reported success but stored on the wrong function; native inherited methods
also observed that wrong receiver. Implicit constructors did not reproduce.

`mvp_lmd_dynamic.cpp` now shares a public-receiver conversion that retains
class shells for foreign properties, native calls and prototype arguments.
Body invocation keeps the existing executable unwrapping. The focused
`JsFormalMir.DynamicClassStaticWritesAcrossNativeSuperclassKeepPublicReceiver`
regression covers explicit/implicit native inheritance, string/computed/Symbol
keys, `Reflect.set`, inherited setters and receiver/prototype identity. It and
four adjacent runtime checks pass; Chromium independently agrees with the
small class and Lit property-decorator controls. Archive.org's navigation
module imports successfully after the fix. Its subsequent template-commit
failure is a separate DOM surface omission; module success alone does not
clear the page's high-severity classification. Evidence is retained under
`temp/online-visual-round60/`.

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

### JS10-L1 — Locale-aware currency formatting is unavailable — **FEATURE GAP**

**2026-11-20.** Shared JavaScript semantics follow **D8.4.4**. Native NumberFormat
currently implements an English decimal profile and rejects other style values.
Getsafe's current 1040-byte inline number-formatting initializer uses
`new Intl.NumberFormat(locale, {style:'currency', currency:code, ...})`. Its
currency branch throws before the initializer finishes. The exact EUR/de-DE
reduction reproduces the RangeError. Both the prior qualified renderer and the
intermediate CSS repair candidate log that exception and produce identical
blank live PNGs. Its relation to every later startup step is not established.

A general locale/currency implementation exceeds this round's native layout
repairs. No currency symbols are hard-coded, valid options ignored, or formatter
results fabricated to let the page proceed. The broad Intl qualification remains
partial and Getsafe stays high. Evidence: `temp/online-visual-round116/`, especially
`getsafe-number-format-32.js`, `currency-gap.js/.log` and the matched-policy live
before/current captures. RI-166/RI-167/RI-168 separately repair native CSS defects
and restore the cookie panel in its frozen source.


### JS10-L2 — Missing PluralRules aborts Shopify localization — **FEATURE GAP**

**2026-11-22.** Shared JavaScript behavior follows **D8.4.4**. The Intl
namespace in js_runtime.cpp has no PluralRules constructor. Shopify's admitted
shell-vendor module checks Intl.PluralRules during i18next initialization.
Its configured logger in shell-CEw6rNTy.js throws Error for the compatibility
warning, so hydration reaches the application error boundary. The first fatal
construction is traced to that logger and the plural resolver check; later
React stack probes produce many unrelated, caught TypeErrors.

The exact reduced check reports pluralRules=undefined and threw=true in the
qualified release, versus function and false in Chromium. A diagnostic with
author scripts skipped renders the full SSR storefront; normal admission
reproduces the error screen. That control is evidence, never a product fix.
Implementing locale-aware plural selection is outside this existing-bug round.
No fake constructor, site-specific fallback, disabled script or budget change
is introduced. Shopify remains high. Evidence: temp/online-visual-round159/
shopify-source-debug2-output.log and shopify-traces.json; round160/
plural-rules.html, plural-rules-native-output.log and plural-browser-result.json.

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

<a id="js13-l64"></a>

### JS13-L64 — CSSOM notification borrows replaced style storage — **FIXED for CSSOM commits**

**2026-11-23.** Native attribute borrows obey **D5.5.1**; shared CSSOM
notification remains Lambda-owned under **D8.4.4 / D9.5.6v2**. The CSSOM
commit path retained the pointer returned by get_attribute("style"), replaced
that attribute, then passed the pointer to selector invalidation and observer
record creation. Replacing a synthetic attribute releases its old pool block
in dom_element_set_synthetic_attribute. The later read therefore exceeded
the borrow's lifetime even when the allocator left the old bytes intact.

CSSOM now uses the existing DomAttributeOldValue snapshot from the class and
attribute setters. Its declaration moves to the module header and its
implementation remains shared in dom.cpp; no duplicate copy helper is added.
The copy survives both inline-declaration replacement and notification.

Three focused property/cssText/remove controls check exact observer old values
and ancestor :has() updates on a programmatically created element. They also
pass on the prior release: this is a source-proven lifetime repair, not a
claim of a deterministic visible failure. Those controls, seven style-value
dependency controls and 22 relational/class controls all pass after repair.
No live-page clearance is attributed to this ownership change. Evidence:
`test/online/render_style_old_values.test.cjs` and
`temp/online-visual-round192/`. Baselines are not repeated for this small fix.

<a id="js13-l63"></a>

### JS13-L63 — Static-view script limit also discards normal-load startup — **FIXED for profile selection**

**2026-11-23.** The Lambda-owned scripting adapter follows **D8.4.4 /
D9.5.6v2**. The 128 KiB pre-layout async/defer guard permanently discarded
startup scripts under the normal document-load profile, although that profile
already admits 5 MiB per external script and 20 MiB per document. There was no
later turn that executed the discarded tasks. Identical blocking scripts ran.

The default pre-layout guard now follows the existing static-headless profile
selection, as the aggregate budget already does. Static viewers retain their
128 KiB pre-layout and 512 KiB aggregate limits. Normal loads use the existing
external and aggregate limits. Explicit pre-layout overrides still apply to
both profiles. This changes the normal admission default; it does not change
the selected 1000ms animation frame (**D10.1.7**), force visibility, or add a
site condition. Admitted scripts can expose further compatibility failures.

The original normal-load defer/async reductions fail on the previous release.
Twelve profile controls pass, including module scheduling, explicit declines,
the other budgets, static viewer behavior and its override. Forty dynamic
script controls pass, including large admitted classic/module sources. Existing
budget-decline fixtures now select their limit explicitly and retain all
behavior assertions. Thirty-seven load-boundary, image and preparation
controls pass. At the milestone, the dynamic layout baseline passes 630/630.

Payload's hidden document and WeTransfer's missing main content are restored
under the normal profile. WeTransfer's small top navigation remains missing.
Aircall's consent panel now appears, but the application root is empty after
startup, so a manual major-omission review keeps it high. Dayos executes its
main module but retains its loading cover. Neither is cleared by a low metric
or mere task execution. Evidence: `temp/online-visual-round190/`,
`test/online/render_script_admission.test.cjs`, and
[comparison round167](../../test/online/comparison/repairs.html#round-167).

Round191 extends the same qualified release to Overflow and Agiloft. Overflow's
695,008-byte startup bundle now executes and restores its brand, navigation
and header actions, resolving the retained manual omission. Agiloft removes
its startup cover and renders its navigation, large heading, actions and
consent panel close to Chromium. Its capture succeeds in 100.8 seconds, so
slow initialization remains recorded separately from visual correctness.
Current Chromium checks retain small consent/layout differences. No additional
production change or capture-clock adjustment is made; the prior focused and
dynamic-baseline qualification applies to the identical release. Evidence:
`temp/online-visual-round191/selected/` and
[comparison round168](../../test/online/comparison/repairs.html#round-168).

Aircall's follow-up native trace confirms the application root has no actual
DOM children, rather than only being omitted by the view serializer. Caught
URL errors and other feature probes are observed, but their causal role is
unconfirmed; they do not justify a URL parser patch. A local-origin document
probe changes request context and is diagnostic only. All temporary traces
are removed, original production files match their qualified snapshots, and
clean runtime/adapter archives are rebuilt. Aircall remains high.

<a id="js13-l62"></a>

### JS13-L62 — Class writes lose observer old values and borrow replaced storage — **FIXED for qualified setters**

**2026-11-23.** Shared DOM notification follows **D8.4.4 / D9.5.6v2**;
old-value bytes obey **D5.5.1**. HTML/SVG class setters and token-list rewrites
omitted old values from mutation notification. Reading the old pointer before
replacement is insufficient for synthetic/recorded attributes: their storage
can release those bytes while setting the new value.

A shared scoped snapshot retains the old bytes through notification. The
className/classList, SVG baseVal, ordinary setAttribute and namespaced
setAttribute paths use it. The synthetic-element control exercises className,
classList.add and setAttributeNS and checks every observer old value alongside
ancestor selector updates. It fails twice on the old release and passes after
repair. This qualification does not cover every DOM attribute setter.
Evidence: `test/online/relational_mutation_fixtures.cjs`,
`temp/online-visual-round188/combined-before.log` and `qualified-controls.log`.

<a id="js13-l61"></a>

### JS13-L61 — Unrelated class writes force global relational cascade — **FIXED for qualified value projections**

**2026-11-23.** Shared host behavior follows **D8.4.4 / D9.5.6v2**; runtime-free
input/native helpers retain their owners under **D9.1.1v4 / D9.1.3v2**.
Every class write was classified as class-sensitive, even when no class or
attribute leaf in a :has() argument changed membership. Connected setup also
disabled the inserted-subtree proof in the same batch. The reduced className
and classList controls each force repeated global cascade before repair.

Mutation begin now accepts the retained old value through an overload that
preserves the existing symbol. The shared recursive selector walk compares
actual leaf results under both case policies. Raw class matching shares the
input DOM cache's token iterator; attribute selectors reuse the input matcher.
Only proved-stable class writes in documents without shadow roots become
local. The child-list history check can retain its subtree proof for records
already classified local. Changed/unknown dependencies, removal history,
scope, column and shadow handling retain their conservative paths. No runtime
or provider-state struct change is required.

All 29 focused render controls pass, including stable class writes followed by
insertion, entering/leaving witnesses, attribute selectors, case flags,
synthetic old values and existing relational/style controls. An input unit
checks ten raw values against cached class-selector matching under both case
policies, plus two null controls. The live Swag retry on the first class
candidate still reaches the unchanged 150-second limit; no whole-page
clearance is claimed. The final token-sharing candidate is qualified locally;
the same live capture and baselines were not repeated. The previous milestone
has 630 dynamic-layout passes. Evidence: `temp/online-visual-round188/`.

<a id="js13-l60"></a>

### JS13-L60 — Detached setup disables inserted-subtree dependency checks — **FIXED for qualified insertion batches**

**2026-11-23.** Shared mutation behavior follows **D8.4.4 / D9.5.6v2**;
retained state follows **D5.5.1**. The child-list dependency check discarded
its subtree proof whenever any attribute record appeared in the batch.
Preparing a detached subtree before insertion therefore forced broad
relational-selector cascade. Swag's dynamic script setup exposes this path.

Insertion checks now pass their mutation sequence. Earlier detached setup
keeps the existing proof; connected writes, same/later writes and removals
retain the history guard. The existing static-selector/subtree algorithm is
reused for all element types. The prepared-unrelated regression fails before
repair and passes afterward. Matching insertion and clearing a removed
witness's class still update the ancestor-dependent style correctly.
Evidence: `test/online/relational_mutation_fixtures.cjs` and
`temp/online-visual-round187/{prepared-insert-before-final,prepared-after}.log`.
These cases are included in the final 39 passing render controls. Swag still
times out at the unchanged 150-second limit; no page clearance is claimed.
The milestone dynamic-layout baseline passes all 630 cases once.

<a id="js13-l59"></a>

### JS13-L59 — Unchanged style-attribute tests force global cascade — **FIXED for qualified retained changes**

**2026-11-23.** Shared CSSOM behavior follows **D8.4.4 / D9.5.6v2**;
helpers retain their input/native owners under **D9.1.1v4 / D9.1.3v2**.
Any stylesheet reference to the style attribute, and every container query,
made inline opacity/transform writes unrestricted attribute mutations.
Unchanged predicates such as `[style*="writing-mode"]` forced global cascade.

Projection now compares old/new attribute-leaf results using the existing
matcher under both case policies. Unknown values and ID projections remain
conservative; subsequent class-value projection is qualified in JS13-L61. Size-query discovery is shared with the input evaluator;
only flat opacity and an existing nonempty transform can preserve those
inputs. Scope, attr(), unknown queries and layout changes retain broad
behavior. The containing-block predicate is shared with paint admission.

Seven render controls pass: stable opacity/transform, changing predicates,
case flags, empty style, quirks serialization and container-width changes.
The two stable cases fail before repair. Quirks coverage caught an intermediate
regression that was never installed. Fifteen classification inputs and five
existing input query tests pass; the complete focused render batch has 39
passes, including relational, inline-style and SVG controls. No runtime-state
or provider-struct ABI change. Evidence:
`test/online/render_style_attribute_dependencies.test.cjs` and
`temp/online-visual-round187/`. Reduced controls do not establish full CSS
invalidation conformance or whole-page clearance.
The milestone dynamic-layout baseline also passes 630/630 once; no aggregate
Lambda/Radiant baseline or catalog sweep is repeated.

<a id="js13-l58"></a>

### JS13-L58 — Nested browser timers omit the minimum delay — **FIXED**

**2026-11-23.** Shared timer behavior follows **D8.4.4 / D9.5.6v2**;
headless selection follows **D10.1.7**. Browser timers tracked deadlines but
not their task nesting level. A zero-delay timeout chain could keep firing
while the loading clock was frozen, repeatedly mutating and relaying out the
document. Intervals instead used an unconditional 1ms minimum, without the
shared nesting rule.

The callback scope now carries owner-checked task nesting. HTML timeouts,
intervals and mixed chains apply the 4ms minimum when parent nesting exceeds
five, as required by [HTML 8.7](https://html.spec.whatwg.org/multipage/timers-and-user-prompts.html#timer-initialisation-steps).
Intervals reinitialize after each callback and honor cancellation. Microtasks
and animation-frame callbacks start a separate nesting scope; Node timer
objects retain their previous scheduling. No runtime-state/provider ABI,
capture clock, source budget or navigation deadline changes.

The focused regression fails before the repair in all three browser timer
variants. Afterward the seventh and eighth callbacks occur at virtual 4ms
and 8ms. Its Node and microtask-boundary controls also pass, along with the
existing runtime-isolation test. Evidence:
`test/test_js_script_gtest.cpp` (`JsTimers.*`, `JsAgentState.*`) and
`temp/online-visual-round184/{timer-before,timer-after}.log`.
Online-page impact is recorded separately after capture; this unit result
alone does not clear a high-severity review.

<a id="js13-l57"></a>

### JS13-L57 — Pages without scripts skip required image completion — **FIXED**

**2026-11-23.** Shared host behavior follows **D8.4.4 / D9.5.6v2**; export
settles the selected frame under **D10.1.7**. The script runner returned before
the ordinary load boundary when it found no executable scripts or declined
the document's source budget. Required images then received only the optional
snapshot grace. A 1600ms CSS-image response rendered white, while the same
document with a no-op script rendered blue. Eager images also failed.

Those branches now share the existing load wait without creating a JS realm.
Native image-request records initialize independently of JS event dispatch,
so the native blocker query can distinguish eager and lazy requests. The wait
polls all completions but tests the existing native load-blocker predicate and
pending dynamic scripts, rather than aggregate network/document work that also
includes speculative, lazy and overridden resources. Navigation deadlines,
source budgets and virtual-frame selection are unchanged.

Seven focused PNG controls pass: no script, a scripted control, data-only
scripts, a skipped module, an explicitly declined document budget, eager and
lazy images. The first, third, fifth and sixth fail before the repair. The
scripted control confirms the load wait leaves virtual time at zero. All 17
existing load-boundary controls pass, including detached images, chained
sources, lazy siblings, hidden/unused/overridden CSS and source replacement.
Evidence: `test/online/render_static_load.test.cjs` and
`temp/online-visual-round183/{static-load-final,existing-load-final}.log`.

Azure's fresh matched 1000ms diagnostic has 2.35% mean error, with its body
and backgrounds present. It remains high: the original HTML has empty
navigation containers and its 1907515-byte `index-docs.js` is skipped. That
startup gap remains **JS13-L1**; this repair does not claim complete client
execution or attribute the full historical score change to the source fix.

<a id="js13-l56"></a>

### JS13-L56 — Computed custom-property reads retain serialization scratch — **FIXED**

**2026-11-23.** Caller-owned serialization follows **D5.5.1**, shared DOM
behavior follows **D8.4.4 / D9.5.6v2**. The custom-property branch of
`dom_computed_style_get_property` passed the document pool to the native
serializer, then copied its output into a GC string. Each read retained the
formatter, buffer and output until document destruction. Ordinary properties
already used a temporary pool.

Both branches now share that temporary pool and release it after copying the
result. Literal custom names, lengths and case, ordinary property conversion,
and the method-dispatch marker keep their existing behavior. No provider ABI
or computed-value rule changes.

The real runtime/native-provider regression fails before the fix: 32 warmed
reads retain 3,584 bytes and 96 allocations in the document pool. It passes
afterward with no growth. A second focused test covers custom-name case and
embedded NUL, ordinary values and the method marker; copied strings survive
forced, poisoned GC. Evidence: `temp/online-visual-round183/` and
`test/test_js_script_gtest.cpp` (`JsComputedStyleTest.*`, 2/2 passing).
This fixes allocation lifetime; no online page is cleared by this test alone.

### JS13-L55 — Webflow immediate initialization fails at a zero clock — **EXTERNAL EDGE CASE**

**Observed 2026-11-22; no engine repair.** Metronome's desktop hero is blurred
by its fixed `.nav-menu-fade` overlay. The authored stylesheet sets opacity 0;
script-free replay preserves that value, and six focused opacity/transition/
restyle controls pass. The Webflow script instead writes inline opacity 1.
Its immediate opacity-to-zero and display-none instances start at timestamp 0.
At performance.now() == 0, zero elapsed time divided by zero duration becomes
NaN; its reducer normalizes progress to zero and leaves both instances
incomplete. Viewport width 1200 and the `main` media query are correct.

The identical instrumented document reproduces this behavior in Chromium 148
when its initialization performance clock is zero: origin 1, destination 0,
progress 0, incomplete, with the overlay left visible. The normal Chromium
clock completes both instances and hides the overlay. The diagnostic's SRI
hash matches its instrumented script bytes; earlier SRI-rejected attempts are
unqualified. No site style, framework code or arbitrary clock offset is added
to production. Shared JS ownership and snapshot frames follow **D8.4.4 /
D9.5.6v2 / D10.1.7**. The page remains high severity.

Evidence: `temp/online-visual-round177/metronome-state.log`,
`metronome-mq.log`, `overlay-probe.log`, and
`browser-{normal,zero}-clock-sri/result.json`. The full-page capture and native
view tree are retained beside the diagnostic; canonical pairs are unchanged.

<a id="js13-l54"></a>

### JS13-L54 — Adjacent HTML insertion bypasses stylesheet and mutation publication — **FIXED FOR QUALIFIED HTML INSERTION**

**2026-11-22.** Shared DOM mutation follows **D8.4.4 / D9.5.6v2** and precise
ownership follows **D6.3.3**. Empathy679 and Mode173 execute CookieYes, which
sanitizes CSS through DOMParser and inserts it with insertAdjacentHTML.
The native implementation only rewired DOM sibling links. It bypassed Mark
backing edits, stylesheet registration, observers and custom-element reactions.
Consent content therefore appeared as unstyled page columns or filled the
viewport instead of forming the authored overlay. A local style roundtrip
reproduces the missing stylesheet and unchanged pixels.

The method now uses the existing backed insertion transaction and shared
fragment context selection. DOMString conversion, exception propagation,
undefined return, and detached/Document-parent errors have focused controls.
Legacy inserted scripts stay inert. Parent, reference, markup and fragment
remain precisely rooted across parsing and callbacks. The fragment transaction
also reported its parent as the added node; it now snapshots the actual
inserted sequence for the aggregate child-list record.

The existing observer snapshot helper is promoted and shared with fragment
insertion and replaceChildren. Its native walk never requires a JavaScript
iterator. An intermediate Array.from implementation passed JavaScript checks
but regressed the Lambda-only fragment-insertion golden; it was never installed.
Frozen167 passes that golden and the final native snapshot restores it.

Six AST, six MIR and six Chromium groups qualify insertion positions,
stylesheet activation, context/ownership, inert scripts, coercion, observers,
custom-element lifecycle and GC reentry. The prior renderer fails four groups;
the corrected stylesheet reduction independently fails before repair. Ten
Range/stylesheet regressions and eight existing/new DOM and Lambda goldens
pass. The 630 baseline-js layout cases pass. The broader layout suite reports
3844 passes and eight failures; all eight reproduce on frozen167 with identical
element pass rates. No references or expected results were changed.

Fresh native Empathy and Mode captures restore consent layout and main page
regions. Both retain major media omissions, and Mode's navigation remains
unstyled, so neither receives a whole-page clearance. Empathy's source contains
a GIF placeholder with the real primary image in noscript; client hydration
is deferred by the current admission policy. No noscript image activation,
source/transport budget, capture clock or vendor workaround is introduced.
XML parsing, Trusted Types, source-fragment removal record details and complete
DOM/IDL conformance remain unqualified.

Evidence: temp/online-visual-round168/html-insertion-before.log,
insertion-stylesheet-before-corrected.log, focused-native-snapshot.log,
baseline-js-qualified.json, baseline-layout.json, baseline-old-structured.json
and fresh browser/final capture directories. Rules follow
[HTML adjacent insertion](https://html.spec.whatwg.org/multipage/dynamic-markup-insertion.html#the-insertadjacenthtml()-method).

<a id="js13-l53"></a>

### JS13-L53 — Range contextual fragment parsing is absent — **FIXED FOR HTML AND SVG CONTEXTS**

**2026-11-22.** Shared DOM behavior follows **D8.4.4 / D9.5.6v2**;
temporary ownership follows **D6.3.3**. OneTrust's admitted diagnostic source
calls createRange().createContextualFragment(), which had no native binding.
The relationship to Aircall's missing main application is unconfirmed.

The Range method now validates its receiver before DOMString conversion,
then reads the live start boundary. Element contexts, Text/Comment parents,
body fallback, SVG integration and foreign HTML document ownership use the
existing fragment parser. The returned fragment and source stay precisely
rooted across construction. A shared script mode permits execution on later
insertion while preserving innerHTML's inert-script behavior. The DOM catalog
row appends without moving existing slots (**D9.5.4**); Lambda and JavaScript
reach the same body. XML parsing explicitly reports NotSupportedError;
Trusted Types, MathML and complete Range WPT conformance remain unqualified.

Six native AST, six MIR and six Chromium groups pass, covering context,
ownership, range preservation, required arguments, coercion exceptions and
script insertion/reinsertion. All six fail on frozen166. The Range-only
candidate exposed a shared HTML parser defect, fixed separately as LI-101.
Five nearby DOM golden scripts and the new Lambda-only contextual-fragment
golden pass. Evidence: temp/online-visual-round167/range-before.log,
range-ast-final.log, range-mir.log, range-browser.log, nearby-results.json and
input-html.log. Rules follow [HTML contextual fragments](https://html.spec.whatwg.org/multipage/dynamic-markup-insertion.html#dom-range-createcontextualfragment).

No capture timing, source/transport budget or vendor change. No whole-page
visual clearance is claimed. Fresh InfluxData capture still hides its server
markup until the 277181-byte main module runs; that module exceeds the current
admission policy. NVD's 2691323-byte application module is likewise deferred.
These observations do not establish additional native API bugs.

<a id="js13-l52"></a>

### JS13-L52 — Message delivery loses undefined payloads — **FIXED FOR QUALIFIED DELIVERY**

**2026-11-22.** Shared JavaScript behavior follows **D8.4.4 / D9.5.6v2**;
precise temporary ownership follows **D6.3.3**. The MessagePort dequeue helper
used undefined for both an empty queue and a valid queued payload. Async
message delivery and Node receiveMessageOnPort therefore discarded valid
messages. Owner, queue, removed payload and replacement queue also remained
unrooted across queue-rebuilding allocations.

The shared helper now reports presence separately from data and roots all
live values, including the current copied entry. Both callers honor that
presence result and propagate errors. A second cause affected transport
MessageEvents: the generated data passed through MessageEventInit conversion,
which changed undefined to the constructor's default null. The shared
construction path now accepts the transport's rooted raw data; public
constructor and initMessageEvent dictionary/argument defaults are retained.
This follows [HTML MessageEvent](https://html.spec.whatwg.org/multipage/comms.html#the-messageevent-interface).

Three MessagePort controls fail on the prior renderer (property handler,
EventTarget listener and reentrant GC/FIFO delivery), as does a Window message
control. The queue-only candidate exposes the second cause by delivering null
instead of undefined and was never installed. All four final controls pass in
native AST, MIR and Chromium, including unchanged public constructor defaults,
false/zero/empty/null values, nested object data after collection and Window
source identity. Four existing Node golden scripts pass in AST; the lifecycle
script also passes in MIR. Its new own-message-property envelope check fails
before repair. Evidence: temp/online-visual-round166/message-before.log,
window-before.log, property-diagnostic.log, node-lifecycle-before.log,
ast-message-final.log, mir-message-final.log, message-browser-final.log and
focused-results-final.json.

No source budget, capture clock, timer policy, private ABI or vendor change.
Aircall's diagnostic page failure is not established as caused by this bug;
no whole-page visual clearance is claimed. Full MessagePort/structured-clone
and MessageEvent IDL conformance remain unqualified.

<a id="js13-l51"></a>

### JS13-L51 — ShadowRoot stylesheet adoption is absent — **FEATURE GAP**

**2026-11-22.** CSSOM and native ownership meet through **D9.5.6v2 / D6.3.3**;
input and rendering retain their owners under **D9.1.1v4 / D9.1.3v2**. Ebury's
current admitted Webflow bundle executes a custom-element reaction that reads
`this.shadowRoot.adoptedStyleSheets.filter(...)`. The native ShadowRoot surface
has no adoptedStyleSheets property or stylesheet-adopter ownership path. The
reaction throws TypeError. CSSStyleSheet construction and ShadowRoot.styleSheets
exist, but neither supplies adoption or shared-sheet cascade invalidation.

A bounded release trace identifies the call at bytes 448833..448884 in the
page's ebury-staging1 bundle. A separate module 108.js (380005 bytes) and the
consent/personalization bundles are skipped by the current source policy, so
this exception is not established as the sole cause of the entirely white
page. No empty-array shim, injected style element or source-limit increase is
used. General stylesheet adoption needs its list mutation contract, ownership,
multiple adopters and cascade invalidation; it is deferred under the online
review's instruction to avoid major new features. Ebury remains high.
Evidence: temp/online-visual-round165/ebury-traces.json,
live/saas_landing_522/after/ and ebury-debug-output.log.

<a id="js13-l50"></a>

### JS13-L50 — Document URL fields are mistaken for shadow hosts — **FIXED FOR COMPONENT LOOKUP**

**2026-11-22.** Interface ownership follows **D8.4.4 / D9.5.6v2**. Archive's
router walks `getRootNode()` and recurses through `root.host` until reaching a
Document. The declared Document interface and native public getter exposed
Location's nine URL fields, so Document.host returned a string. The router
then called getRootNode on that string. A native trace and a source-root
reduction confirm this cause; it does not require a new Shadow DOM feature.

The nine fields are removed from Document's declared interface and public
getter. Location reads its private owning document's native URL through the
existing component table. Its stringifier uses the same private read. Main,
parsed and detached documents no longer expose those fields, and a Document
host expando has normal assignment, descriptor and deletion behavior. Existing
legacy Document navigation-method and stringifier aliases, component writes,
and complete Document/Location conformance remain unqualified.

All 26 focused native EventTarget/interface and URL groups pass, including
three new ownership, expando and shadow-host traversal groups. Those three
also pass in MIR and Chromium 148 and fail on the prior frozen renderer.
Archive's custom-element reaction error disappears. Its current image remains
missing application regions, so no visual clearance or improvement is claimed.
Evidence: temp/online-visual-round163/document-ownership-before.log,
root-host-before.log, archive-root3-output.log; temp/online-visual-round164/
native-focused.log, mir-focused.log, document-browser.log and
live/archive_org/after/. The full inventory remains 109 high.

<a id="js13-l49"></a>

### JS13-L49 — URL pathname writes leave serialized private state stale — **FIXED FOR PATHNAME WRITES**

**2026-11-22.** Shared JS behavior follows **D8.4.4 / D9.5.6v2** and precise
ownership follows **D6.3.3**. The global URL setter updated only its pathname
array entry. href and stringification retained the old path. Archive's Web
Components feature probe therefore rejected the native URL and selected a
failing fallback constructor.

Pathname writes now use the same rooted private-record replacement path as
href/search. Input's coherent pathname setter supplies encoding and shared
path normalization (LI-100). Query/fragment values and the live searchParams
object survive. Conversion reentry, collection, throwing conversion, frozen
owners, public shadows and opaque-path no-ops have focused coverage. Other
URL component setters and full URL/IDL conformance remain unqualified.

Five new native groups fail before repair. All 16 URL render groups and five
additional MIR groups pass. Chromium 148 passes four complete new groups and
all 16 HTTP encoding/authority assertions in the fifth. Its Windows file-URL
control differs: /D|/a/../b becomes file:///D%7C/b, while native Input and Node
produce file:///D:/b. That reference control stays red and is not treated as
qualified browser interoperability. No assertion or browser API is weakened.
The 630-case JS layout baseline passes once at this milestone. Archive's old
URL fallback failure is gone, but the current frame loses regions visible in
the earlier partial-startup frame. The page stays high; captures are retained
as diagnostic evidence, not imported as a clearance. Evidence:
temp/online-visual-round163/pathname-before-qualified.log,
input-pathname-before.log, final-url-focused.log, final-pathname-mir.log,
pathname-browser-final-trace.log, baseline-js.log and archive-final-traces.json.
[URL pathname setter](https://url.spec.whatwg.org/#dom-url-pathname) ·
[Path encoding](https://url.spec.whatwg.org/#path-percent-encode-set)

<a id="js13-l48"></a>

### JS13-L48 — DOM interface constructors lose their inheritance chain — **FIXED**

**2026-11-22.** Shared behavior follows **D8.4.4 / D9.5.6v2**, with precise
temporary ownership under **D6.3.3**. The shared interface-linking helper
connected instance prototypes but omitted constructor objects. Node had a
separate special case, while Text, Comment, Document, HTML and SVG subclasses
failed constructor-parent checks and could not inherit Node.ELEMENT_NODE.

The helper now roots both constructors and prototypes and links both chains.
Node uses that same path instead of its duplicate special case. Ten
representative constructor pairs and their inherited constants fail before
repair. All seven focused EventTarget/interface groups, three additional MIR
groups and three Chromium groups pass afterward. Evidence:
temp/online-visual-round162/constructor-chain-before.log, final-focused.log,
final-mir.log and constructor-chain-browser.log. No runtime-state layout or
capture-policy change.

<a id="js13-l47"></a>

### JS13-L47 — Character-data interfaces are absent from Window — **FIXED FOR INTERFACE LOOKUP**

**2026-11-22.** Interface exposure belongs to **D9.5.6v2** and the shared
JavaScript contract **D8.4.4**. Archive's admitted Web Components polyfill
traverses Text, Comment, CDATASection and ProcessingInstruction prototypes.
The last two globals were absent, so setup threw before installing its DOM
patches. The existing interface table now publishes CDATASection under Text
and ProcessingInstruction under CharacterData, using the existing distinct
interface constructors. These parent links and Window exposure follow
[DOM §4.12–4.13](https://dom.spec.whatwg.org/#interface-cdatasection).

The qualified Chromium 148 profile rejects direct construction even with
arguments; the shared illegal-constructor body retains that behavior. This
repair does not qualify XML node factories or the newer ProcessingInstruction
construction/attribute API in the current DOM standard. It introduces no
Shadow DOM implementation or vendor patch.

Both interface-lookup and exact prototype-traversal controls fail before
repair and pass in native, MIR and Chromium. Archive's polyfill exception
disappears, and its current native frame restores the gray search region and
main body content. Header branding and other startup differences remain;
the page stays high and its canonical pair is preserved. Evidence:
temp/online-visual-round162/character-data-before.log, browser controls,
archive-final-traces.json and live/archive_org/after/.

<a id="js13-l46"></a>

### JS13-L46 — Collection bindings mask ordinary HTML element fields — **FIXED**

**2026-11-22.** Native interface ownership follows **D9.5.6v2** and the
shared JavaScript property contract **D8.4.4**. No ruling changes.

Maple's small, admitted collapsible module saves a button in this.toggle.
Its initialization then receives a native function instead of that button
and throws while adding the click listener. The same failure reproduces with
the original component body in isolation. The generic html_element interface
incorrectly declares item, toggle and replace as native methods. These
immutable virtual members mask author fields, descriptors and deletion.
The token operations already belong to the separate
[DOMTokenList interface](https://dom.spec.whatwg.org/#interface-domtokenlist).
HTMLSelectElement also needs its own item method, scoped to selects.

The three misplaced declarations and bindings are removed together. The
existing token-list implementations, collection item methods and element
expando machinery are retained. Select.item now delegates to its live options
collection; the former generic binding returned the wrong result even on
selects. A focused control covers both select.item(0) and select.options.item(0). No per-page exception, prototype fork,
resource-limit or capture-timing change is introduced.

Three shared cases cover assignment, explicit descriptors and custom-element
class fields, including stored-button use, null assignment and deletion.
All fail before repair and pass afterward on AST, MIR and Chromium. Their
token-list and collection controls retain their behavior. Five related
EventTarget controls also pass. The original component changes from init=false
with an exception to init=true. The current full page has zero custom-element
errors, but its large consent panel remains absent; the fresh matched pair
stays high at 26.31% mean error / 41.14% changed area. No page clearance is
claimed. The previous round's 630-case baseline is not repeated for this
incremental interface correction.

Evidence: `test/online/dom_expando_collisions_fixtures.cjs`,
`temp/online-visual-round158/{expando-before.log,final-focused.log,
final-mir.log,browser-preparation-results.json}`, the original-body
`component-{before,after}-output.log`, and `live/saas_landing_082/`.

<a id="js13-l45"></a>

### JS13-L45 — Module source entry drains its enclosing callback's timers — **FIXED**

**2026-11-22.** Callback ownership follows **D6.4.1 / D6.4.4** and the
shared JavaScript contract **D8.4.4**. No ruling changes.

A dynamically appended external module entered the standalone source-turn
drain while its dispatch timer was still running. A reduced after-load module
fired its 100ms timer before its script load event. A second reduction appends
another module; the nested drain selects the still-active first script task
again and frees it twice. Both that reduction and the live Wine Investment
page abort in dynamic_script_dispatch on the previous executable. The earlier
round156 release also faults while closing the active timer.

The shared source-entry boundary now leaves timer/frame progression to the
enclosing callback's host task and retains its microtask checkpoint. It uses
the existing context-owned callback_running state, common to AST and MIR;
independent script tasks remain asynchronous. No source cap, settling window,
vendor code or task-queue serialization is changed. This preserves the
[HTML task processing model](https://html.spec.whatwg.org/multipage/webappapis.html#event-loop-processing-model).

The focused suite passes 38 dynamic-script controls, including the three new
timer/chain cases; those three also pass on MIR and Chromium. Twenty related
request-timing and parser-preparation controls pass. The JavaScript layout
baseline passes 630/630 once at this milestone. Wine now captures without
a crash and executes its admitted application modules. Its missing regional
dialog remains a major difference; no page clearance is claimed.
Evidence: `test/online/dynamic_script_fixtures.cjs`,
`temp/online-visual-round157/{late-timer-before.log,late-chain-before.log,
dynamic-focused.log,module-mir.log,module-browser.log,related-focused.log}`
and `wine-{before,after}-output.log`.

<a id="js13-l44"></a>

### JS13-L44 — Web keyframe eligibility skips supported background shorthand — **FIXED**

**2026-11-21.** Host bindings follow **D8.4.4 / D9.5.6v2**; native
animation sampling follows **D9.1.3v2 / D10.1.7**. No ruling changes.

The existing background-shorthand explicit-seek regression paints red rather
than lime on both the round153 renderer and the first round155 candidate.
The native parser already normalizes background to its supported color track,
but the host's property-name filter checks the shorthand's non-animatable
metadata before invoking that parser. Longhand seeks still pass.

Property eligibility now calls the existing css_animation_property_target
normalizer before reading metadata. Parsing, keyframe conversion order and
native sampling remain shared. The shorthand and longhand controls pass in
the final focused suite; the shorthand also passes against Chromium. This
does not add automatic Web Animations playback or new shorthand tracks.
Evidence: `temp/online-visual-round155/shorthand-before.log`,
`focused-qualified.log`, `test/online/render_capture_settle.test.cjs`.

<a id="js13-l43"></a>

### JS13-L43 — Network-created fades begin at the already selected virtual frame — **FIXED FOR FINITE REQUEST SETTLEMENT**

**2026-11-21.** Native requests belong to **D6.4.4**, owner turns to
**D6.4.1**, and frozen-frame export to **D10.1.7**. No ruling changes.

**Round155 repair.** The snapshot drain now services context-owned finite
requests during its existing virtual-frame slices. The shared capped native
wait gives pending I/O the elapsed slice before selecting a later frame;
completion callbacks, microtasks and zero-delay continuations commit at that
frame. Timer-only virtual advances retain their existing behavior. The
existing five-second drain watchdog bounds settlement and subsequent native
completion waits together; source admission limits are unchanged.

Requests that finish after the selected interval still settle without moving
its clock, and an explicit zero window remains frozen. Seven reduced native,
MIR and Chromium controls cover fetch, XHR, timer-created fetch, chained
fetch, delayed continuation, after-window delivery and zero-window delivery.
The final focused suite passes 44 checks, including retained-opacity and
intrinsic-motion controls. Kotlin's full sidebar is visible in the fresh
matched pair. Its smaller missing announcement banner remains separate.
Native image-manager requests retain their separate RI-187 completion path;
this repair does not qualify arbitrary streaming or wall-clock timelines.

Evidence: `temp/online-visual-round155/focused-qualified.log`,
`focused-mir.log`, `resource-timing-browser-results.json`,
`resource-timing-browser-xhr.log`, `live/kotlin_docs/`, and
`test/online/render_resource_timing.test.cjs`.

**Original round154 diagnosis.**

After the round153 repairs, Kotlin's article and cards render, but the sidebar
is transparent. A focused native view tree contains the correct sixteen TOC
roots and link geometry; the containing ul.toc has opacity0. The real stylesheet
applies a supported300ms fadein animation. HelpTOC.json returnsHTTP200(81232B).
This is not missing TOC data, missing DOM construction or a geometry failure.

Two small native/Chromium controls distinguish the cause. A timer inserts the
same fade at virtual100ms; both captures are green after the1000ms window. An
80ms fetch inserts it at native virtual1016.67ms because the snapshot drain
advances its virtual window before waiting for finite I/O. The native capture
correctly retains the new fade's first transparent frame; Chromium's elapsed
post-load window has already finished that fade. The loader's finite requests
finish, but their arrival times are not coordinated with virtual progression.

The required integration was to interleave native completion delivery and
virtual timers before selecting the frame. Advancing the selected frame during
export, overriding opacity or treating all I/O as instantaneous would hide the
timing difference. Round154 left that integration open and kept Kotlin high;
round155 implements the bounded finite-request path described above.

Evidence: `temp/online-visual-round154/kotlin-view.json`, `kotlin-app.css`,
`fade-controls.test.cjs`, `fade-results.json` and paired browser diagnostics.

<a id="js13-l42"></a>

### JS13-L42 — Virtual immediates invent a delay after completed I/O — **FIXED**

**2026-11-21.** Shared JavaScript execution follows **D8.4.4 / D6.4.1**;
the selected export frame follows **D10.1.7**. No formal ruling changes.

Kotlin's four JSON requests return HTTP200 after the finite-request drain
repair, but React's state updates remain queued. Its scheduler selects the
available setImmediate implementation. `js_setImmediate_impl` supplied a1ms
timeout to the timer backend, including the virtual clock. A request completed
at the selected frozen timestamp therefore scheduled rendering beyond that
frame. Configuration, article and TOC requests succeeded without an app error.

Immediate creation now supplies zero elapsed delay. The existing virtual timer
ID boundary still moves callbacks to the next host turn; the native libuv
backend retains its existing zero-delay normalization. This changes neither
the capture window nor the ordinary timeout/interval policies. No browser API
is hidden and no page-specific ready/reveal action is added.

A delayed-fetch control schedules its final mutation through setImmediate,
with MessageChannel as the normal browser fallback. It fails before repair
and passes afterward in the native host and MIR; Chromium passes the same
source through the fallback. Kotlin now renders its article, cards and current
version. Its sidebar navigation remains absent, so it stays high severity.

Evidence: `test/online/dynamic_script_fixtures.cjs` (`late-fetch-immediate`),
`temp/online-visual-round153/{immediate-before.log,immediate-after.log,late-fetch-final-mir.log,browser-immediate.log}`,
`kotlin-fetch-completion.log` and `live/kotlin_docs/final/`.

<a id="js13-l41"></a>

### JS13-L41 — Snapshot exits after a checkpoint starts another request — **FIXED**

**2026-11-21.** Finite native work belongs to its resource table under
**D6.4.4**, runtime turns follow **D6.4.1**, and the selected capture frame
follows **D10.1.7**. No formal ruling changes.

After the dynamic classic and Location repairs, Kotlin renders its header and
footer but has no article or navigation. A narrow live trace shows all four
configuration/content/TOC fetches starting without a rendering exception.
The snapshot loop obtains libuv's active result, then runs render checkpoints
and same-time virtual callbacks. A dynamic script evaluated there can start
another fetch after libuv returned zero. The early exit checked that stale
result and empty timer/rAF queues, ignoring the new finite native request.
The next loop-condition request check was therefore never reached.

The early exit now also checks the existing pending-request predicate after
all checkpoints. The same resource table, drain watchdog and frozen virtual
time are retained; persistent sockets/timers do not gain a new wait policy.
There is no added polling loop, deadline increase or page-specific readiness.

Two delayed local controls start fetch from a classic script appended during
window.load, including a second dependent JSON request. Both fail before the
fix and pass afterward in the native host, MIR and Chromium. The fixture
permits its file-to-loopback JSON request through a normal CORS response
header. Existing script/URL controls are qualified separately.

Evidence: `test/online/dynamic_script_fixtures.cjs` (`late-fetch`,
`late-fetch-chain`), `temp/online-visual-round153/late-fetch-{before,after,mir}.log`,
`browser-late-fetch.log` and `kotlin-fetch-trace.log`.

<a id="js13-l40"></a>

### JS13-L40 — Location copies omit the base URL components — **FIXED FOR LIVE ENUMERABLE COMPONENTS**

**2026-11-21.** Shared browser values follow **D8.4.4**, the scripting adapter
contract **D9.5.6v2**, and precise ownership **D5.5.1 / D6.3.3**. No formal
ruling changes.

After Kotlin's translation chunk starts its React application, the bundled URL
parser throws at `w.pathname.charAt(0)`. The unmodified parser works with an
absolute URL and fails with empty, hash-only and query-only relative inputs.
Its base is copied with `for…in` from Location. Native Location was an alias
of Document, whose URL members are non-enumerable; the copy omitted pathname
and protocol. The empty relative path therefore returned undefined.

The JS adapter now publishes a separate, cached Location with live, enumerable,
non-configurable own URL accessors. Two private edges retain the owning
Document and cache the Location without exposing either through own keys.
Captured accessors validate their receiver. The existing component table,
declared Document reads, navigation methods and history writes are reused;
there is no URL-parser patch or page-specific result. Document/window share
Location identity, and detached parsed documents expose null Location.
No public runtime-state layout or owner archive dependency changes.

Six new controls fail before repair and pass in the native host, MIR and
Chromium. They cover copying, own descriptors, identity/stringification,
illegal construction and receivers, foreign documents, history/hash liveness,
empty relative paths and collection. The five existing URL controls also pass.
Kotlin gets past the React error fallback, but its subsequent JSON requests
still outlive an event-loop checkpoint; no page clearance is claimed here.
Full Location navigation/component-setter conformance, ancestorOrigins and
cross-origin exotic behavior remain unqualified.

Evidence: `test/online/url_live_fixtures.cjs`,
`temp/online-visual-round153/{location-before.log,location-final.log,location-final-mir.log}`,
`browser-location/results.json`, `url-relative-output.log`, the narrow live
`kotlin-url-locals.log` trace, and
[HTML Location](https://html.spec.whatwg.org/multipage/nav-history-apis.html#the-location-interface).


<a id="js13-l39"></a>

### JS13-L39 — Node event methods mask JavaScript overrides — **FIXED**

**2026-11-21.** Shared JavaScript behavior follows **D8.4.4** and declared
host interfaces follow **D9.5.4 / D9.5.6v2**. No formal ruling changes.

An Agiloft listener probe exposed a shared object-model defect: assigning
document.addEventListener had no effect. Node's constructor and prototype
did not inherit EventTarget, and the native Node/Document event-method
records projected methods ahead of JavaScript overrides. Window's standalone
lazy method slots also masked its inherited surface. Document's descriptor,
own-key and deletion hooks were stubs, so an override could not be reflected
or removed correctly even after its lookup was repaired.

Node now inherits the existing EventTarget surface, and the method records
use prototype dispatch. Window initialization retires only untouched lazy
markers; later family materialization preserves deleted or authored slots.
Document reflection and deletion reuse the existing node expando helpers,
including presence when an own value is undefined. The native event dispatcher
and Lambda's declared method projection are retained.

All five new shared controls fail before repair and agree with Chromium
afterward. They cover ten node receivers, Window and standalone EventTarget,
method identity, instance/prototype wrappers, own descriptors and keys,
undefined overrides, deletion, inherited readonly writes and native delivery.
All 31 focused online checks pass; the five new cases also pass separately in
AST and MIR. No broad baseline or full online sweep is repeated. Agiloft's
remaining callback exception is still unisolated; no page clearance is claimed.

Evidence: `temp/online-visual-round148/`,
`test/online/render_event_target_prototype.test.cjs`,
[DOM Node inheritance](https://dom.spec.whatwg.org/#interface-node) and
[Web IDL operations](https://webidl.spec.whatwg.org/#es-operations).

<a id="js13-l38"></a>

### JS13-L38 — Programmatic click reenters the same element's activation — **FIXED**

**2026-11-21.** DOM behavior follows **D8.4.4 / D9.5.6v2**; native node
state and precise wrapper ownership follow **D5.5.1 / D6.3.3**, with the
input/host boundary under **D9.1.1v4 / D9.1.3v2**. No formal ruling changes.

Teachable's tab click handler calls goToTab(), which calls the same tab's
click() again. The current native capture reports two maximum-call-stack
errors. The HTML click algorithm requires a per-element activation guard;
the native bridge dispatched a new event on every call without that guard.

A native rare-state bit now covers event construction, dispatch and default
activation. The rooted original element retains the guard across removal and
forced layout. It clears after ordinary completion and handler errors; other
elements, clones, later microtasks and direct dispatchEvent calls retain their
own behavior. The bit fits the existing packed state; no runtime root-layout
or native module ABI change is introduced.

Seven focused controls agree with Chromium. Six fail before repair, while
the later-microtask control already passes. All seven pass afterward, as do
six existing form/label activation tests. The current Teachable capture loads
without either recursion error. Its separate initial hidden state remains,
so no whole-page clearance is claimed. No baseline sweep, page override,
admission limit or capture-clock change. All1067 comparison rows remain
unchanged at111 high, zero critical,956 low.

Evidence: `temp/online-visual-round147/`,
`test/online/render_click_reentrancy.test.cjs`,
[HTML activation behavior](https://html.spec.whatwg.org/multipage/interaction.html#dom-click).

### JS13-L37 — Missing Node.lookupNamespaceURI breaks captured consent-wrapper calls — **FIXED**

**2026-11-21.** Shared DOM publication follows **D8.4.4 / D9.5.4 /
D9.5.6v2**; prefix conversion retains its receiver and input through precise
roots under **D6.3.3 / D5.5.1**. Coda's admitted external-cms script throws
TypeError during DOM mutation. Optimized current-source debugger traces
separate expected intrinsic probes from the fatal Transcend wrapper call:
`e=>e?s(Ry,e,null):null`, where Ry captures Node.prototype.lookupNamespaceURI.
That method is absent, so Function.call receives an undefined callable.

One DOM catalog operation now delegates node-kind dispatch to the shared
Input namespace lookup. The Node interface declaration and prototype publish
that operation through the ordinary binding system. Documents use their root,
attributes their owner, and character data its parent element; fragments do
not cross to a host. Input LI-093 supplies expanded-name precedence and
actual namespace declarations, including live shadowing and empty resets.
No page check, consent bypass, invented API result or source/timer change.

Four focused fixtures fail before the combined repair. All17 focused native
cases and four matching Chromium fixtures pass, covering captured calls,
HTML/SVG/XML, live declarations, attributes, fragments, descriptors, receiver
branding and conversion. The unchanged baseline-js category passes630/630;
no full baseline repeats. Coda's external-cms/load-listener TypeErrors vanish.
Its page now displays the real missing-IndexedDB failure rather than blank,
so JS13-L36 remains a major blocker and Coda remains high. Other namespace
methods, complete WebIDL edge cases and broader XML conformance are not claimed.

Evidence: `temp/online-visual-round139/`, `coda-errors-function.log`,
`namespace-before.log`, `namespace-focused-after.log`, `browser-namespace.json`,
`baseline-js-results.txt` and current live pairs. The algorithm is specified by
[DOM namespace lookup](https://dom.spec.whatwg.org/#locate-a-namespace).


### JS13-L36 — IndexedDB is absent during Coda startup — **OPEN feature gap**

**2026-11-21.** Browser API ownership follows **D8.4.4 / D9.5.6v2**.
Current source has no indexedDB/IDBFactory publication or transaction/request
implementation. Coda's live startup logs `Expected value to be defined and
non-null: idbFactory missing`, an external-cms TypeError and a load-listener
TypeError. Lambda's1000ms post-load frame is blank while Chromium loads the
page. The missing API is observed, but its causal relation to the blank frame
and the earlier TypeErrors is **unconfirmed**. No fake factory or successful
request is installed. Persistent asynchronous database transactions are a
substantial missing feature outside this round's native bug repairs.

Round139 isolates and fixes the earlier consent-wrapper TypeErrors as
JS13-L37. They disappear, and Coda now visibly renders its own error page
reporting idbFactory missing. The absent IndexedDB API is therefore confirmed
as the remaining startup blocker after that repair; it is not assigned as
the cause of the earlier native TypeErrors. No database stub is installed.
Current evidence: `temp/online-visual-round139/coda-fixed/`.

The independent saved-source font defects are repaired as RI-178/179 and
LI-092. Coda remains high; those fixes do not establish live startup recovery.
Evidence: `temp/online-visual-round137/coda-live/`, current paired captures,
`saas_landing_266-lambda-output.log` and [report125](../../test/online/comparison/repairs.html#round-125).


### JS13-L35 — Missing inherited Node constants crash ancestor walks — **FIXED**

**2026-11-21.** Interface publication follows **D8.4.4 / D9.5.6v2**;
constant-definition temporaries use precise roots under **D6.3.3**. Node
kind constants existed only on the constructor. Consequently
`document.DOCUMENT_NODE` was undefined. Ramp's scroll-parent helper walked
past Document and read `document.style`, throwing after React cleared the
server-rendered content and leaving an empty page.

The existing interface constant table now publishes both constructor and
prototype properties, including document-position masks. The shared Node /
CSSRule installer defines enumerable, non-writable, non-configurable
properties. No Document style property, exception suppression or page rule
is introduced. Existing opt-in script-task diagnostics now work in release
captures and report collected/settled execution state; source admission,
limits and timing remain unchanged.

Two reduced native tests reproduce the original failure and qualify ancestor
walking, inherited node kinds, comparison masks, readonly descriptors and
CSSRule reuse. Both pass and agree with Chromium headless shell. Five
existing request/base fixtures and the real React fixture's exact golden
also pass. No broad baseline is repeated for this interface-only repair.

A current, matched 1000ms post-load pair restores Ramp's full interface:
0.49% coarse mean error and 0.30% changed area. The prior native image was
blank. The old pair's animation origins were unrecorded, so both references
are refreshed with the original pair retained in the append-only frame
audit. Reduced tests independently establish causality; historical score
movement includes the live reference refresh. Canonical inventory is now
**113 high / zero critical / 954 low**. Evidence:
`temp/online-visual-round134/`, frozen pin/source, native/Chromium controls,
React golden and [report123](../../test/online/comparison/repairs.html#round-123).

### JS13-L34 — Fetch ignores HTML base and XHR retains stale base URLs — **FIXED**

**2026-11-21.** Shared request URL resolution follows **D8.4.4 /
D9.5.6v2**; Input owns document-base selection under **D9.1.3v2**.
Fetch resolved against `document->url`, ignoring the active HTML base. In a
saved Dribbble document with a network base, `/client_app/users` consequently
became a local-file request. XHR used the base captured at document binding,
so later base changes and removal continued to target the old directory.

Both APIs now share `dom_resolve_document_request_url`, which reuses the
existing `dom_document_resolve_url` selection policy at fetch creation or
XHR open. The first active HTML base wins; inert template bases do not.
URL bytes are owned before asynchronous transport. Standalone XHR's fallback
and fetch's local WPT root mapping remain available. No resource permission,
transport, source limit, timer or host-state layout changes.

Four reduced native cases fail before repair: fetch and XHR in both HTTP
and file documents. They cover directory/root-relative requests, base href
mutation, first-base removal and document/absolute fallback. All four pass
after repair and agree with Chromium headless shell. The existing HTML-base
dependency fixture also passes; four existing local JS goldens cover XHR URL
coercion/ordering, local fetch/JSON and header/error conversion. No broad
baseline or live catalog capture is repeated for this request-only repair.

This fixes a confirmed shared defect exposed by the page investigation; it
does not establish the cause of Dribbble's live blank state or claim a major
page clearance. No canonical comparison row or reference is replaced by the
instrumented saved document. Evidence: `temp/online-visual-round133/`,
including `fetch-before.log`, `xhr-before.log`, `focused-after.log`,
`browser-base.json`, `local-js-controls.json` and the frozen source/pin.

### JS13-L33 — Font-loading promises lack native resource completion — **OPEN feature gap**

**2026-11-21.** The browser preamble exposes `FontFace` and
`document.fonts.load`, but their promises deliberately remain pending: there
is no native resource-completion bridge. This is separate from
`document.fonts.ready`, which currently resolves during document setup.
Shared DOM ownership follows **D8.4.4 / D9.5.6v2**; native resources follow
**D9.1.1v4**.

Stellar's WebFont 1.6.26 detects `window.FontFace` and calls
`document.fonts.load`. In the current network-enabled diagnostic, all four
Inter weights and the root remain `wf-loading` at the selected frame; the
authored loading selector leaves the page transparent. The library races
the pending promise against a 3000ms rejection timer, beyond the unchanged
1000ms post-load capture window (**D10.1.7**). Its unrelated callback error
was already reproduced in Chromium in round27. No success promise, forced
visibility, longer settling interval or capture replacement is introduced.

Completion requires an actual FontFaceSet/resource lifecycle, rather than
an unconditional resolved promise. This feature gap is outside the current
existing-bug repair. Evidence: `temp/online-visual-round132/webfont.js`,
`stellar-probe.html`, `stellar-network/result.json` and its `PAGE_PROBE` log.
The sandboxed `stellar-current` attempt lacks network resources and is not
qualification evidence. Canonical comparison data remains unchanged.

**2026-11-23: Homerun customers (`saas_landing_696`) has the same gap.**
Its main Webflow chunks execute, including the 662598-byte shared chunk.
The saved-source diagnostic retains `wf-loading` and all requested font
weights' loading classes; authored `.wf-loading * { opacity: 0; }` makes
the body transparent. The script-free control has body opacity 1 and the
same 3460.78px content height. Chromium clears the loading state. This is
not attributed to the separately skipped Wistia or GTM scripts. No visibility
or capture-time workaround is applied; the page remains high. Evidence:
`temp/online-visual-round185/homerun-probes.log`, the two probe HTML files,
and the original/current Chromium DOM captures.

### JS13-L32 — Null-namespace metadata and reflected inert writes force layout — **FIXED**

**2026-11-21.** Shared DOM mutations follow **D8.4.4 / D9.5.6v2**;
Input owns attribute storage under **D9.1.3v2**. `setAttributeNS(null, ...)`
and the `inert` reflected setter bypassed the existing metadata path. Dock's
actual menu handler repeatedly assigns `inert=true`; each stable assignment
was eligible to trigger another full layout.

The namespace writer now reuses ordinary metadata handling for null-namespace
value writes, excluding Attr replacement/prefix changes. Input shares stable
inert-presence eligibility (LI-087). Existing case/namespace, CSS dependency,
transaction, after-set, observer and custom-element guards remain in charge.
Same-valued writes still deliver attribute notifications. First inert enable,
removal and CSS-sensitive lexical-value changes retain normal invalidation.

Four before controls fail with eleven layouts instead of one. All 38 focused
metadata/inline-style/SVG controls pass after repair, including namespace Attr
identity collisions, six custom-element routes, focus and value-selector
guards. Integer lint passes. This proves the shared mutation repair; Dock's
whole-document outcome is recorded separately under RI-031. No broader
baseline is repeated immediately after round125's 17-category milestone.
Evidence: `temp/online-visual-round126/`.

### JS13-L31 — Unrelated keyframes force attribute cascade and layout — **FIXED**

**2026-11-21.** Shared attribute dependency checks follow **D8.4.4 /
D9.5.6v2**. The query treated every `@keyframes` rule as an attribute
reference, including an unused opacity animation. This defeats retained
metadata and CSSOM property updates across a document. Two focused controls
fail before repair with eleven layouts instead of one; genuine keyframe
attribute references already preserve invalidation.

The query now uses the existing CSS tokenizer and function-name decoder to
recognize actual `attr()` calls in raw keyframe content. Quoted/commented text
is ignored; escaped names are decoded. Allocation/tokenization failure and
scope/container dependencies remain conservative. Five transform controls
pass in the query-only build, including ordinary and escaped attribute calls.
The combined release passes all 23 focused controls (10 inline-style and
13 metadata), including direct style assignment and selector/callback guards.
Integer lint passes. A layout baseline checkpoint and live-page diagnostic are
recorded separately under `temp/online-visual-round122/`; these focused checks
alone do not establish page clearance.

### JS13-L30 — CSSOM writes discard authored-attribute notification and property identity — **FIXED**

**2026-11-21.** Shared DOM behavior follows **D8.4.4 / D9.5.6v2**;
attribute storage remains document-owned under **D9.1.3v2**. The inline
CSSOM commit knew the changed property but sent only a generic rendering
mutation. Mutation observers and custom-element callbacks received no style
attribute record, selectors depending on `[style]` stayed stale, and the
retained paint path could not identify the changed property.

The commit now passes the property, style attribute name and old value through
shared native mutation dispatch. Direct `element.style = ...` assignments use
the same notification helper; their prior selector control also fails before
repair. Existing CSS dependency checks keep selector-
dependent writes on the attribute cascade path; other writes retain ordinary
attribute observations alongside their rendering classification. The existing
paint admission guards still decide eligibility. All six inline-style controls pass, including observer old values, selector
dependencies, callback-driven geometry, cssText, and transform containing-block
establishment. The layout-count control exposed and now covers the separate
publication defect RI-170: ten established-transform writes retain one layout
instead of eleven, while geometry and final pixels update. Thirteen metadata
controls also pass. Live-page evidence is recorded separately under
`temp/online-visual-round121/`; focused checks do not establish page clearance.

### JS13-L29 — Non-stylesheet link metadata forces full cascade and layout — **FIXED**

**2026-11-21.** Shared DOM/host invalidation follows **D8.4.4 / D9.5.6v2**;
associated stylesheet ownership follows **D9.1.3v2**. The host classified every
`link` element as stylesheet-related. Unrelated data/ARIA writes on preload
and icon links therefore bypassed the metadata path and forced full cascade
and layout when scripts read geometry. The focused prior-renderer controls
produce ten layouts for ten writes, versus one in the no-write control.

The predicate now recognizes the existing stylesheet relation token or an
associated stylesheet. The latter preserves invalidation when a previously
active link changes relation or is removed. The CSSOM sheet lookup is promoted
through its module header and reused, not copied. CSS-dependent metadata still
uses ordinary invalidation; mutation observers retain all ten records and old
values through both setAttribute and dataset.

**23 focused native controls and 11 stylesheet lifecycle controls pass**, plus
the existing form-user-validity SVG render control. Two unrelated-link cases
fail before the fix; their two selector-dependent controls already pass.
The final controls also cover prior metadata, Error-stack and document-title
repairs, dynamic linked sheets, media activation, empty/error completion and
load ordering. Full baselines are not repeated. A live-page diagnostic is
recorded separately; these layout-count controls alone do not establish a
page clearance or live performance improvement.

Evidence: `temp/online-visual-round120/` and
`test/online/render_dataset_metadata.test.cjs`.

### JS13-L28 — createDocument discards the root namespace — **FIXED FOR ROOT IDENTITY**

**2026-11-20.** Namespace and document ownership use **D9.1.1v4 / D9.1.3v2**.
The existing DOMImplementation operation ignored namespaceURI and built the
root directly. A requested SVG document therefore exposed a null root
namespace, preventing its SVG title operation. The shared foreign-XML factory
now uses the existing namespace-aware element factory, and createDocument
passes its namespace argument. The previous public helper remains an
unchanged wrapper for namespace-less callers. The SVG title control now passes
on native and Chromium. Doctype adoption and complete QName validation are
not qualified by this repair. Evidence: `temp/online-visual-round118/`.

### JS13-L27 — Document.title is published without a setter — **FIXED**

**2026-11-20.** The shared DOM/host boundary follows **D8.4.4 / D9.5.6v2**;
native documents retain ownership under **D9.1.3v2**. The Document catalog
published title as getter-only. Hera's strict assignment therefore returned
false from Set and threw, causing the client root to fail. The older backend
also stored a realm-wide cached value that the document getter did not read.

The catalog now publishes its setter. The backend edits the document's real
title node through existing mutation helpers, creates one under an available
HTML head or SVG root when necessary, and uses the first applicable title.
The getter shares that lookup and the existing ASCII whitespace normalizer.
Foreign-document writes remain scoped to their own document. Readonly URL and
receiver-brand checks remain effective; no page condition or capture-policy
change is introduced.

All five strict title controls fail before repair and pass afterward, alongside
the same five Chromium groups. The shared fixture helper was extracted for
title/error/stream controls; all seven stream controls still pass. In total,
17 final native controls pass, plus ten additional AST/MIR stack checks and
ten Chromium groups. Broad baselines are not repeated.

Hera now loads without its fatal TypeError and restores the blue background
and logo. Its matching 1000ms browser/native pair has 1.53% mean error versus
the retained 66.07% historical comparison. The primary animated headline
remains absent, so this is an actual repair with a remaining major omission,
not a page clearance. Evidence: `temp/online-visual-round118/`.

Pitch News also restores its main heading and article layout. A current prior
renderer control at the same 1000ms post-load origin remains blank and reports
the fatal TypeError. The repaired matching pair has 2.48% mean error and 0.85%
changed area; its former manual major omission is resolved. Comand restores
its headline and foreground card but retains a major video omission. A related
FramerAuth check reaches the unchanged 150-second outer deadline while repeating
layout work; it is not imported as a successful capture. Historical selected
captures lacked origin metadata, so the refreshed origins are audited separately
from the current prior-renderer controls.

### JS13-L26 — Removing absent attributes publishes phantom mutations — **FIXED**

**2026-11-20.** The shared DOM operation follows **D8.4.4 / D9.5.6v2**.
`removeAttribute` notified observers, custom-element reactions and the host's
layout invalidation path even when the attribute did not exist. Ten removals
of one attribute produced ten mutation records; `removeAttributeNS` already
correctly produced one. Primer's current startup also repeatedly removed
absent id/srcset/sizes attributes while reading geometry.

The ordinary operation now checks the existing native presence predicate
before removal and notification. This preserves HTML name normalization and
distinguishes a present empty attribute from an absent one. No callback or
invalidation policy changes for successful removals.

**13 focused renderer checks pass**, covering metadata writes, real and absent
removals, empty attributes, custom-element callbacks and observer startup.
Repeated absent removals retain the single-removal control's layout count.
The ordinary-removal regression fails on the preceding qualified renderer;
the namespace-aware control passes there.

Primer's absent-removal warnings fall from 14 to zero, but both current live
runs still hit the unchanged 120-second script watchdog and produce identical
orange PNGs. This fix does not clear Primer or establish a live performance
improvement. Dock's preceding diagnostic reaches the outer 150-second limit;
its prior canonical pair is retained. Further profiling targets the remaining
full-document reconciliations. Evidence: `temp/online-visual-round117/` and
`test/online/render_dataset_metadata.test.cjs`. Broad baselines are not repeated.

### JS13-L25 — Asynchronous readable pulls report EOF before producing data — **FIXED FOR PENDING DEFAULT READS**

**2026-11-20.** Shared semantics follow **D8.4.4**, and pending values follow
**D5.5.1 / D6.3.3**. The [Streams read contract](https://streams.spec.whatwg.org/#readable-stream-default-reader-read)
requires an empty readable queue to retain a pending request, not signal EOF.

The reader invoked pull and immediately returned done:true when the queue was
still empty. Dropbox's gRPC decoder wraps Response.body in an asynchronous pull;
its first await therefore caused a false premature EOF before frame decoding.
Two focused controls fail before repair, including that framed-decoder shape.

Reads now retain promises in a traced private queue. Enqueue, close and error
settle pending requests; startup completion gates pull, and pull promises
serialize subsequent requests. Original rejection values reach pending and
future reads. Consumed queue entries release their references. The existing
controller and Response byte path are reused, with an appended private-name ID
and no public runtime-state or host-API layout change. Full byte-stream/BYOB,
locking, cancellation, backpressure and piping conformance remain unqualified.

**34 focused native checks, seven MIR stream cases and seven Chromium fixture
groups pass.** They cover async/concurrent pulls, delayed push/close, async
startup, errors, callback GC and binary frame decoding. The actual Dropbox
capture now has zero gRPC request errors and zero premature-EOF messages, versus
eight gRPC failures in the prior pair. Its native image remains pixel-identical:
the pale-versus-blue hero composition is a separate, unresolved difference.
No capture duration, transfer limits or page conditions change. Broad baselines
are not repeated. Existing Node-module golden probes could not resolve assert
or stream on either the prior or current host, so they are not counted as
passing verification. Evidence: `temp/online-visual-round115/` and
`test/online/comparison/repairs.html#round-111`.

### JS13-L24 — URL query and search-parameter records diverge — **FIXED FOR QUERY/HREF BINDING**

**2026-11-20.** Shared semantics follow **D8.4.4**; precise ownership follows
**D5.5.1 / D6.3.3**, and the appended host API slot follows **D9.5.4**.
The [URL query update contract](https://url.spec.whatwg.org/#urlsearchparams-update)
requires parameter mutations to update the owning URL.

The global URL stored an independent URLSearchParams snapshot. Coframe's
`url.searchParams.set('config', ...)` therefore fetched a queryless URL, which
returned HTTP 400 / 53 bytes instead of the project's configuration. Mutators
now use the existing serializer and notify their owner through a traced private
edge. URL.search and URL.href replacements refresh the same parameter object;
record parsing and serialization share the constructor path. Both arguments are
rooted before coercion, and private owner state survives GC and public shadows.
Standalone parameter objects remain unbound. Input LI-086 separately normalizes
default ports in parsed records.

Five native/Chromium fixture groups pass: mutations and form encoding, query/href
replacement and invalid-URL atomicity, no-op normalization, private state and
frozen URL owners, and collection during string conversion. The wire-observed
configuration round trip passes in AST and MIR. The exact SDK request now
returns HTTP 200 / 692816 bytes, and the actual page runs Coframe watchers.
Other URL component setters and full URL/IDL/WPT conformance remain unqualified.
No page condition, capture budget or timer change is introduced. Dropbox remains
high because its large hero composition still differs. Evidence:
`temp/online-visual-round114/` and `test/online/comparison/repairs.html#round-110`.

### JS13-L23 — Fetch rejects coercible URLs and truncates long addresses — **FIXED FOR STRING INPUT**

**2026-11-20.** Shared semantics follow **D8.4.4**, network ownership
**D9.1.3v2**, and retained values **D5.5.1 / D6.3.3**.

Fetch used the string-only `js_item_to_cstr` helper instead of converting the
string arm of RequestInfo, and copied input, resolved and effective URLs through
2048-byte buffers. A URL object failed before HTTP; long paths and redirects
lost their tails. Four local controls fail before repair.

Fetch now converts once, preserves the original conversion exception as a
promise rejection, and snapshots the full URL into worker-owned memory before
option getters run. Resolution retains existing admission checks, and curl's
final effective address is copied before handle cleanup. Missing input rejects
asynchronously. The separate Request-valued arm remains unsupported rather than
being coerced to a fake URL.

All 22 HTTP session controls and five live-URL groups pass with the final release
host; all 12 changed-input and URL groups pass through MIR. Coverage includes
4KB URLs and redirects, original exception identity, missing input, and the
live-query wire request. Five URL groups also pass Chromium; 37 Input URL tests
pass for LI-086. Broad baselines were not repeated. The native page now obtains
Coframe configuration, but its gRPC errors and hero difference remain unresolved.
Evidence: `temp/online-visual-round114/`.

### JS13-L22 — Script HTTP requests lose the shared client profile — **RESOLVED**

**2026-11-20.** Shared browser semantics follow **D8.4.4** and network/host
ownership **D9.1.3v2**. No formal ruling changes.

The document and resource downloaders use Input's shared HTTP client profile,
but the fetch worker omitted `CURLOPT_USERAGENT`; asynchronous XHR uses that
same worker. Synchronous XHR independently supplied a different literal profile.
A local server control observes all five request kinds in one document: document
and image agree, fetch and asynchronous XHR have no user agent, and synchronous
XHR differs. The control fails before repair.

Fetch now sets the existing `RADIANT_HTTP_CLIENT_USER_AGENT`; synchronous XHR
uses the same definition. No new identity string is introduced. All **15 HTTP
session controls pass**, including the new wire-observed identity comparison,
cookies, redirects, independent sessions, image readiness and layout exports.
No public state layout, deadline or capture policy changes; broad baselines
were not repeated for this focused transport repair.

Aircall's current native image is pixel-identical before and after this fix,
and remains high severity. Its small consent loader executes and receives its
configuration and geolocation through successful XHRs, then appends an external
SDK. That remaining dynamic-script preparation gap is tracked separately in
`online-dynamic-script-preparation`. Independent endpoint requests with either
profile receive the same bot flag, so that flag does not prove the overlay's
cause. Evidence: `temp/online-visual-round111/` (before/after local controls,
request traces, matching-frame native/browser pairs and installed release pin).

### JS13-L21 — NodeIterator-dependent sanitization fails — **RESOLVED**

**2026-11-20.** Shared traversal semantics follow **D8.4.4**, host ownership
**D9.1.3v2**, and precise state ownership **D6.3.3 / D5.5.1**. No formal ruling
changes. The [DOM traversal contract](https://dom.spec.whatwg.org/#interface-nodeiterator)
defines the cursor and removal adjustment; TreeWalker is not an alias.

Dropbox's DOMPurify failed at `es.call` (bundle bytes 4,207,252–4,207,259),
where `es` was the parsed document's missing `createNodeIterator`. Prior native
main/parsed-document probes and an AST-only render confirmed the gap independently
of MIR promotion. React's stack and missing ErrorEvent failures obscured it.

The realm now installs NodeIterator and Document.createNodeIterator for main
and parsed documents. It reuses the existing traversal filter, with distinct
bidirectional reference and in-flight candidate pointers. Pre-removal adjusts
both pointers and preserves iterator roots inside removed subtrees. Private
cursor state uses the existing WeakMap ephemeron machinery; a precisely rooted
weak registry tracks live iterators without keeping abandoned ones alive.
Collected registry entries are pruned. No author-visible cursor fields,
TreeWalker alias, sanitizer bypass, page conditions or capture-budget changes
are introduced. The appended realm root is included in the shared trace/reset
span; all four native Node modules were rebuilt for the state layout.

Seven focused fixture groups cover root inclusion, direction changes, masks,
callback receivers and coercion, readonly properties, reentrancy and exceptions,
reference/candidate removal, collection during callbacks, detached roots and
parsed-document sanitization. The final fixtures pass Chromium headless shell;
all **36 selected native CLI tests pass**, including existing globals, cloning,
observers and interruption controls. The earlier renderer fails the original valid missing-interface controls.
Full WPT/interface conformance is not claimed.

Dropbox now renders its heading, supporting copy, primary action and product
image. Its current Chromium pair shows a different, blue centered hero, while
native shows a pale split hero. Coframe reports missing project configuration
and native gRPC requests report premature EOF; the remaining composition's cause
is unconfirmed. Dropbox remains high; the repaired omission is not described
as a visual parity clearance. Evidence: `temp/online-visual-round113/`, qualified
release `29e40dcacead3b6a87bba2872664387e09dcf0e5403050b0a7cce389ce8cd7cf`.

### JS13-L20 — Resource load events incorrectly reach Window — **RESOLVED**

**2026-11-20.** Shared event semantics follow **D8.4.4** and the host boundary
**D9.1.3v2**. The DOM Standard's [Document parent algorithm](https://dom.spec.whatwg.org/#interface-document)
returns no parent for `load`, including capture and synthetic bubbling.
No formal ruling changes.

The propagation builder appended Window after Document for every event type.
Dropbox's Web Vitals callback waits for the document to become complete and
registers another capturing window load listener when it is still loading.
Resource load events incorrectly reached that callback, doubling registrations
until the diagnostic recorded **131,072 load listeners on Window**. A longer
run reached SIGBUS after heavy listener growth; its exact crash site was not
independently isolated.

The shared path builder now stops the Document-to-Window edge for `load`.
Document listeners, direct Window dispatch and other event types keep their
existing paths. The new control fails with the prior renderer and passes after
repair for both bubbling settings; the identical fixtures pass Chromium.
Existing ordinary-exception and one-second interruption controls also pass.
Their stale `1s` log expectation was updated to the exact `1000ms` emitted by
both before and after renderers; the timeout itself is unchanged.

Dropbox now exits successfully in **8.28s**, without the former watchdog or
SIGBUS, using the unchanged 1000ms settling window. Its current image is blank:
application errors remain and the page stays high severity. This is an execution
repair, not a visual clearance. The installed release includes this repair and
JS13-L19. Evidence: `temp/online-visual-round109/` (callback source/span,
listener-growth notices, before/after controls and the current diagnostic pair).

### JS13-L19 — Class initializer metadata repeatedly republishes whole bundles — **RESOLVED**

**2026-11-20.** Stable AST identity and derived facts follow **D8.2.4**;
shared JavaScript semantics follow **D8.4.4**. No formal ruling changes.

Two release profiles of Dropbox startup show class evaluation repeatedly
entering `js_script_field_initializer_ensure`, appending one synthetic initializer
definition, and rebuilding the complete AST query tables. Each field rescans
the bundle although its existing node IDs remain unchanged.

The execution overlay now prepares the script's initializer definitions together
and publishes their query tables once. The existing single-fragment API delegates
to the shared batch operation. Initializer expressions still execute when the
class or instance is evaluated, with that evaluation's captured environment.
All **276 script/ownership tests pass**, including AST/MIR capture and static-block
ordering controls. A local release fixture with 12,000 unused functions and 768
fields completes correctly in both versions (one paired measurement: 14.0s before,
8.5s after). The subsequent native page profile no longer has this indexing hot
path. Broad baselines have not been run for this candidate.

Dropbox remains high severity. The initial candidate reached a later SIGBUS
after heavy listener registration/dispatch; JS13-L20 subsequently removed that
growth and the current native run completes, but its image remains blank. Both
repairs are installed; no canonical pair or page clearance is published.
Evidence: `temp/online-visual-round109/`,
including normalized link-map qualification, before/after samples, focused test
logs, the local fixture, and the failed native/current browser pair.

### JS13-L18 — Loader rewrites mutate author globals — **RESOLVED**

**2026-11-20.** Shared realm semantics follow **D8.4.4**, host ownership
**D9.1.3v2**, retained source ownership **D5.5.1**. No formal ruling changes.

The document loader ran a library-specific global synchronization snippet after
each scheduled script. It read and wrote jQuery/$ properties, manufactured an
alias when the author defined only one export, and copied a lexical binding onto
window. A separate text scan appended window assignments for function names,
including named lexical function expressions. Those assignments invoked unrelated
global setters. The realm's existing binding and declaration machinery already
handles ordinary global properties, vars and function declarations.

Both rewrites and their now-unused generated-source owner are removed. Author
source reaches declaration instantiation unchanged; ordinary globals and authored
aliases continue through the existing realm. Five focused controls fail with the
prior renderer. All eleven controls pass in **both AST and MIR**, and the same
fixtures pass in Chromium. All **37 existing inline-script/HTTP resource/session
checks** pass; two lifecycle UI replays pass five assertions. Admission limits,
resource deadlines and selected capture times are unchanged. No public state
layout or vendor code changes. Broad baselines are not repeated for this repair.

Dropbox's historical global-sync exception motivated the investigation, but is
not by itself proof that these rewrites caused its main content omission. The
current native diagnostic still omits the hero and reaches the existing 120s
JavaScript watchdog after AST construction. Its remaining execution stall is
unconfirmed; no page clearance or canonical capture replacement is attributed. Evidence: Lambda
`temp/online-visual-round108/` (before controls, shared browser fixtures, tier
logs, HTTP checks and lifecycle replays). Early sandbox diagnostics stuck before
JavaScript in dyld are excluded from the failure evidence.


### JS13-L17 — Canvas gradient paint objects are absent — **FEATURE GAP**

**2026-11-20.** Host profile **D8.4.4**; graphics boundary
**D9.5.1 / D9.5.6v2**, ownership **D5.5.1**. No formal ruling changes.

The bounded HTML Canvas 2D surface in `lambda/dom/dom_canvas.cpp` installs
solid paints, paths, transforms, images and text, but neither
`createLinearGradient` nor `createRadialGradient`. A local HTML canvas probe
reports both as undefined; calling the radial constructor raises TypeError.
This is a missing paint-object API, not a failed implementation of an
advertised method. A complete repair needs gradient objects, stops and
lifetime/state handling through the native graphics boundary; substituting
solid fills or dropping the call would not implement the authored drawing.

Gladia's public `/js/hero-canvas.js` requires radial gradients for its large
purple hero background. Its matched-animation-origin live capture still lacks
the background and headline. The headline separately waits for a feature-flag
callback or a 1500ms JavaScript fallback, beyond native's selected 1000ms
frame; Chromium's JS timers still include loading time. Do not attribute all
live omissions solely to gradients. No timer advance, forced visibility or
new Canvas subsystem is introduced in this major-difference round. Gladia
remains high severity. Evidence: `temp/online-visual-round103/`, including
`canvas-gradient-control.{html,log}`, downloaded scripts and the loaded pair.

### JS13-L16 — Image request state, decode promises and initial completion — **FIXED for the native image profile**

**2026-11-20.** Shared JS contract **D8.4.4**, native boundary
**D9.5.6v2**, precise ownership **D6.3.3 / D5.5.2**. No formal ruling changes.

HTMLImageElement lacked loading, currentSrc, complete and decode(). The
native decoder and scripted load events already worked, but a standard
capability check could bypass image reveal handlers. The branded prototype
now exposes loading reflection (eager/lazy, unknown values read as eager),
the selected absolute request URL, terminal request state, and a decode promise.
The loading attribute remains a hint; this does not add viewport-based lazy
admission. Picture/srcset selection uses the existing native selector.

Decode begins at a microtask checkpoint, waits for real request completion and
rejects EncodingError for failed decoding or a changed pending request.
A metadata probe is insufficient: the backend materializes raster pixels with
the existing decoder, or checks the parsed native SVG picture. Event and timer
callbacks hold a rooted bound record; completion removes listeners, cancels the
poll timer and breaks the record/callback edge before promise reactions run.
No GC pointer enters Radiant. The shared DOMException helper now roots both
string arguments during construction.

Parser-created images also lacked the completion path used by scripted src
writes. The load boundary now prepares their native requests, including local
and data images; native request completion owns both synchronous and asynchronous
notification. Removing the second DOM-side notification avoids duplicate load
events. This is also recorded under native RI-155.

Validation: 8 local image checks and 3 HTTP image checks pass. The HTTP checks
cover a delayed image at the zero-time load boundary, replacement while waiting,
and a valid PNG header with corrupt pixels. The corrupt-pixel check incorrectly
fulfilled before the full-decode repair. All 14 HTTP session controls pass; the
16 local image/script controls have 15 passes and the pre-existing
ratio_content_growth_control failure (95px instead of 432px), independently
reproduced with the previous renderer. The exact Gatsby reveal-script reduction
is green in both Chromium and Lambda. Layout integer lint passes. No broad
baseline rerun is claimed. Evidence: temp/online-visual-round99/.

**Live qualification limit:** Mode's current capture is byte-identical to the
previous renderer, with 19.53% coarse error. Its custom hero is authored with
zero-sized/hidden containers and uses a separate client startup path. The
generic Gatsby reveal script has no matching data-main-image elements in the
live SSR document. The initial attribution to that generic script was incorrect;
these verified platform repairs do not clear Mode or establish its complete
startup root cause. Its current async framework measures 142,207 bytes, above the
unchanged 131,072-byte pre-layout async/defer budget. No cap, clock, visibility
override or vendor code was changed. Orum's current hero is present, but the prior
renderer produces identical pixels on the current source, so its high-severity
clearance is capture requalification only.

### JS13-L15 — Button commands and command events — **PARTIAL**

Button `command` and `commandForElement` resolve named and direct references in
their permitted tree scopes. Click activation honors submit and reset before
commands. Custom commands dispatch a trusted, cancelable `CommandEvent`.
Built-in commands reuse the popover and dialog transitions and pass the
invoker through to toggle events. Source retargeting shares the ToggleEvent
algorithm. Authored `oncommand` handlers use the normal inline-handler path.
IDL numeric input writes convert to a string before the native sanitizer
(**D6.3.3 / D7.2.2 / D9.1.3v2 / D9.5.6v2**).

All 326 subtests in the 13 pinned fixtures pass, including the formerly
failing implicit-anchor case and four declarative-root retargeting cases.
Button invokers participate in the same default anchor lookup as sources
supplied to the popover IDL. The earlier closed imperative-root control and
trusted pointer replay also passed under forced GC
(**D9.1.1v4 / D9.1.3v2 / D9.5.6v2**).

### JS13-L14 — Progress event interface and delivery — **PARTIAL**

Scripted `ProgressEvent` and FileReader dispatch share the traced native
payload. Web IDL conversion preserves fractional and signed finite values,
applies defaults and rejects nonfinite values. The shared payload-event
constructor setup publishes prototype inheritance before first use. Both
pinned interface and constructor fixtures pass all 17 subtests on the live
clock. The native FileReader and subclass regression passes under forced GC.
XHR load and progress dispatch remain open (**D6.3.3 / D7.2.2 / D9.1.3v2**).

### JS13-L13 — Dialog and popover toggle events — **PARTIAL**

The `ToggleEvent` interface uses traced, readonly state and source fields.
`source` is retargeted against the listener's current target. Dialog and
popover state transitions deliver synchronous `beforetoggle` and queue one
`toggle` for pending transitions with the first old state and latest new
state. Separate trackers allow a dialog with a popover attribute to deliver
both events. Dialog opening hides automatic popovers above its flat-tree
ancestor in reverse opening order and preserves manual popovers
(**D6.3.3 / D7.2.2 / D9.1.3v2 / D9.5.6v2**).

All 38 pinned interface tests, all 12 dialog event cases and the popover
opening case pass. The focused test covers cancellation, task order,
native invoker source and synthetic shadow retargeting. Popover IDL
reflection, effective automatic and hint stacks, mode changes during
closing callbacks and removal now use the same document state. The pinned
stacking file passes 6/6. The 249 attribute cases, 11 pointer-order cases, three
cross-document adoption cases and removal during dismissal all pass. Trusted
pointer dismiss closes eligible stack entries before `pointerup`. The broader
light-dismiss fixture passes 19/26 with `ProgressEvent`. It still exercises
unsupported `elementsFromPoint()`, older nested-open semantics, and
native click targeting after a cross-target press. Authored autofocus and
explicit close now use the shared native focus transition. Dialogs choose a
sequential control or the dialog itself as a fallback. Popovers save focus
for the first automatic or hint entry. Dialogs and popovers now share the
native document top layer (Radiant RI-029). Modal focus and pointer hit
testing follow flat-tree inertness, and nested modals restore focus to the
previous modal on close. The pinned inert, showModal, removal and requestClose
fixtures pass all 87 subtests. Native close watchers, complete keyboard
focus order and overlay transition lifetimes remain open
(**D9.1.1v4 / D9.1.3v2 / D9.5.6v2**).

The pinned dialog focus matrix passes 42/50. Its eight remaining cases
exercise delegatesFocus ordering across a shadow host and sibling controls.
The previous-focus fixture passes 4/5. Its remaining case expects restoration
for a light-tree button slotted into a non-modal dialog. The current close
algorithm uses shadow-including ancestry for non-modal restoration
(**D9.1.3v2 / D9.5.6v2**).
[HTML dialog close algorithm](https://html.spec.whatwg.org/multipage/interactive-elements.html#close-the-dialog)

### JS13-L12 — Dialog form submission and submitter values — **PARTIAL**

Native activation, `requestSubmit()` and direct `submit()` share dialog
method and close handling. The form's nearest source-tree dialog gets the
submitter's value or the image button's selected point. Closing clears
descendant focus that is no longer rendered and queues the close event.
The focused controls cover cancellation, validation, external submitters,
shadow boundaries and the native pointer path (**D9.1.3v2 / D9.5.6v2 / D6.3.3**).

The three pinned files cover dialog form submission, unusual actions and the
close task. The latter files pass 2/2 and 1/1. The main file passes 6/8 when
native WebDriver pointer actions supply the clicks. The public click helper
requires `elementsFromPoint()`. An image subtest compares the submitted integer
with a fractional native event offset. The no-submitter case expects the
stored value to be cleared, as in Chromium 152. The HTML close algorithm
preserves that value when submission supplies a null result.

`SubmitEvent` and `FormDataEvent` now share the Event record's traced
payload. Activation and `requestSubmit()` block reentry during validation
and submit listeners. Submission and the FormData constructor share
entry construction and use the listener-edited snapshot after dispatch.
The pinned event constructors and submitter-entry fixtures pass all
18 subtests. The construction algorithm fixture passes 11 of 13.
Legacy `accept-charset` conversion and a control named `submit` that
should hide the form method account for the two remaining failures.
The expanded native image replay checks actual `x` and `y` entries.
The focused submission and iteration controls pass under forced GC
(**D6.3.3 / D7.2.2 / D9.1.3v2 / D9.5.6v2**).

GET activation, `requestSubmit()` and direct `submit()` share URL construction.
It resolves the action against the document base, replaces the query before
the fragment, preserves an empty query and encodes file names and CRLF only
for serialization. The current navigation waist updates the selected
document's URL. Legacy form encodings, named controls that shadow form
methods, and full POST navigation are deferred (**D9.1.3v2 / D9.5.6v2**).

### JS13-L11 — Until-found reflection, rendering and fragment reveal — **PARTIAL**

`HTMLElement.hidden` now reflects the HTML union setter. Until-found uses
the UA content-visibility rule, and computed reads use the same cascade as
layout. Same-document hash writes and anchor activation share the fragment
reveal path. It snapshots flat-tree ancestors, opens closed details around
their non-summary content, dispatches trusted `beforematch` and scrolls to
the resulting box. A disconnected or boxless target does not cause scrolling
(**D9.1.1v4 / D9.1.3v2 / D9.5.6v2**).

The pinned hidden reflection and cascade suites pass all 26 cases. The
fragment-navigation suite passes 10 and four more ancestor-mutation controls
pass. Pinned `beforematch-element-removal-002.html` times out because it
expects the next ancestor's event after the handler clears the current
ancestor's hidden state. Chromium 152 continues in this case. The current
HTML ancestor-revealing algorithm returns when that state changes. Text
fragment and find-in-page reveal remain open.

### JS13-L10 — Initial script collection executes inert HTML template contents — **RESOLVED**

Fixed 2026-11-17 under **D8.4.4 / D9.5.6v2**. The raw source walker collected
inline and external scripts beneath HTML templates. It now uses the shared
namespace-aware Radiant predicate, retaining the template element's own event
attributes while excluding its contents. Foreign SVG template elements remain
active. Initial and document.write collection share this path and the current
document resource base (RI-109/RI-110). This does not implement dynamic script
preparation or full template insertion-mode support.

Nine focused native document tests and two persistent HTTP regressions pass.
The inert HTTP fixture asserts both non-execution and no dependency downloads;
native/Chromium controls agree on six inert HTML and active SVG states.
Evidence: `temp/online-visual-round62/`; owner records are in
`../Radiant_Issue_Ledger (fixed).md#ri-110`.

### JS13-L8 — Missing Element.hasAttributes rejects dynamic Lit templates — **RESOLVED**

Found 2026-11-17 during Archive.org review. **D8.4.4 / D9.5.4 /
D9.5.6v2** require the existing declared DOM operation boundary. Lit calls
`Element.hasAttributes()` while walking its parsed template; the absent
method rejected even a one-interpolation template, while a static template
worked. Added the derived `has_attributes` catalog row, its declared interface
member and Element prototype exposure. The native fast path reuses the live
namespace-aware attribute iterator and excludes internal bookkeeping.

The focused document test checks 16 states: empty elements, expando exclusion,
HTML mutation, empty attribute values, SVG namespace add/remove, template
TreeWalker traversal, arity and invalid receivers. It and two adjacent document
tests pass after correcting an initially missed interface declaration.
Chromium agrees with all 16 states. The small dynamic Lit component now
commits successfully in both renderers. This fixes the method omission;
Archive.org also exposes a distinct imported-descendant upgrade omission.
Evidence: `temp/online-visual-round60/`.

### JS13-L9 — Imported fragments leave custom descendants uninitialized — **RESOLVED**

Found 2026-11-17 after removing Archive.org's earlier Lit failures.
**D8.4.4 / D9.5.6v2** keep DOM lifecycle operations behind their existing
boundary. `dom_import_node_bridge` cloned a fragment's descendants but
upgraded only its root. Lit then reached a custom property setter before the
child constructor had initialized its reactive state. The existing tree walk
now shares upgrade/connect/disconnect actions; imports upgrade descendants
without sending connected callbacks for detached nodes. Template contents
remain inert. The document adapter also admits text/comment sources already
handled by its clone owner, rather than returning null at an element-only guard.

`ImportedFragmentUpgradesNestedCustomElementsBeforePropertyBinding` passes
18 checks for detached construction, pre-insertion property binding, shallow
fragments, text/comments, connection/disconnection and no duplicate construction.
Chromium agrees with all 18. The final four selected document tests pass;
Archive.org's root template update completes. This does not clear the separate
shadow-tree painting limitation below. Evidence: `temp/online-visual-round60/`.

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

The 2026-11-18 HTML support goal reproduced zero-height shadow descendants
through both `innerHTML` and `appendChild()`. Reconciliation now follows the
shadow-including ancestor chain for mutation connection and dirty layout.
Raster and vector paint enter the shadow child list already used by layout,
and stacking replay crosses the host boundary. Controls cover open and closed
roots, document stylesheet isolation, empty-root suppression of light children,
and a positive z-index inside a shadow tree. PNG, SVG and PDF paint the supplied
text and fill. The original Lit applications remain unqualified
(**D9.1.1v4 / D9.1.3v2**).
The same goal added composed root lookup plus live named-slot queries for
elements and text. Closed roots expose neither `host.shadowRoot` nor a light
node's `assignedSlot`. Sequence reads cover nested fallback flattening and
reflect slot renames and reordering (**D9.1.3v2 / D6.3.3**).

Named distribution and fallback changes now queue one `slotchange` per slot
for delivery after mutation observers. Dispatch follows assigned slots,
retargets both targets across shadow boundaries, and filters closed-tree
entries from the frozen path. The legacy global event stays scoped outside
shadow listeners. Document, element, and fragment queries share source-tree
traversal, which keeps retained HTML template contents out of their enclosing
query scope. Touch-target retargeting remains open
(**D9.1.3v2 / D6.3.3 / D5.5.1**).

Named slot projection now shares normal-flow margin adjacency, paint order
and hit testing with the flattened tree. Assignment changes reconcile both
branches from the host, and client rect reads exclude unassigned light nodes
and hidden fallback. Focused PNG, flat SVG and flat PDF cases verify live
reassignment. Broader layout modes and slot inheritance remain open
(**D9.1.1v4 / D9.1.3v2**).

Manual roots now expose `slotAssignment` and accept variadic Element and
Text arguments to `HTMLSlotElement.assign()`. The lifecycle registry keeps
ordered weak relations through detach and adoption. Manual and named
assignments share the DOM, directionality, layout and paint traversal.
Nested projection preserves each slot's source root. The pinned imperative
API and slotchange suites pass (**D9.1.1v4 / D9.1.3v2 / D5.5.1**).
Document teardown now releases slot links before adopted-document resources
destroy the nodes' backing storage. The standalone document-probe cases
cover plain adoption, fragments and replacement (**D5.5.1**).

The HTML default for a slot is `display:contents`. Inline and scoped
`display:none` suppress projected and fallback boxes while preserving the
assignment APIs. Scoped stylesheet and `hidden` changes also update
geometry and hit testing (**D9.1.1v4 / D9.1.3v2**).

Inline shadow stylesheets now participate in the owning root's author cascade
in source order. `ShadowRoot.styleSheets` is a live, same-object list using the
same traversal. These sheets stay out of `document.styleSheets`. CSSOM edits,
text changes, media changes, moves between roots and host insertion update the
computed style and PNG paint. Selector parent relations stop at the shadow
fragment boundary. Initial scoped link styles also enter the root cascade.
Live linked-sheet lifecycle, adopted sheets, shadow name registries, `:host`
and `::slotted` remain open (**D9.1.1v4 / D9.1.3v2 / D5.5.1**).

Declarative templates attach in navigation and explicit unsafe HTML parsing
through the shared DOM root provider (Input LI-081). Root flags, closed
access, matching-mode imperative hydration, cloning and opt-in HTML
serialization have focused checks. The pinned attachment, basic and repeat
fixtures pass all 674 subtests. Closed manual declarative assignment through
`ElementInternals` and streaming parser behavior remain open
(**D9.1.1v4 / D9.1.3v2 / D6.3.3**). HTML/SVG contextual fragments now have
focused qualification under JS13-L53; full parser conformance remains open.

Focus survives native reflow through connected shadow hosts. The document's
`activeElement` reports the outer host, and each root's `activeElement` reports
its own adjusted target. This includes nested closed roots. The focus-within
path uses the flat tree (**D9.1.3v2 / D9.5.6v2**).

Round sixty (2026-11-17) separates this residue from confirmed runtime bugs:
after JS07-L1, JS13-L8 and JS13-L9, Archive.org's module and root Lit template
update complete, and its shadow DOM contains the header/main/footer nodes.
The capture still omits them. A local `shadow-hosts.html` control also produces
an empty image for shadow content on span, div and custom hosts, including
explicit block hosts; `lit-nested.html` commits its nested template but paints
empty. Radiant has partial shadow traversal code, so these observations do not
prove that every composed-tree path is absent. They do show that successful
DOM construction is insufficient to claim painted support. Full shadow-tree
rendering remains outside this milestone's scope. Controls and logs are under
`temp/online-visual-round60/`.

Observed 2026-11-12 in the online comparison. Native/runtime boundaries follow
**D9.1.1v4 / D9.1.3v2**. This is a major missing rendering feature outside the
user-authorized bug/incomplete-existing-feature milestone; it is not an online
capture blocker and is not implemented in this round.

`lambda/dom/dom.cpp::dom_core_attach_shadow` creates a stable fragment-backed
root and exposes host/mode/focus behavior. WebAssembly.org's
`dark-mode-toggle.min.mjs` constructs its controls and stylesheet in
`this.attachShadow({mode:"open"})`. Supporting that API object does not qualify
its composed layout, shadow-scoped CSS or control rendering. The downloaded
module and page are retained under `temp/online-visual/`; the module is also in
the normal resource cache. This does not establish Shadow DOM as the cause of
every remaining blank page or of that site's remaining theme mismatch.

### JS13-L1 — snapshot script admission can leave the whole document hidden — **PARTIAL**

**2026-11-23 update:** [JS13-L63](#js13-l63) fixes the misplaced static-view
default in normal document loads. Payload and WeTransfer's main content now
render. The per-script/aggregate admission limits and incomplete application
APIs remain, so this umbrella issue stays open. Earlier observations below
describe the profile in use at those review dates; they are not a requirement
to retain the incorrect normal-load limit.

**2026-11-23 source checks:** Amperos565 changes header `text-black` to
`text-white` only after client startup. Overflow160 starts with root
`is-loading`; Chromium replaces it with `has-scroll-init is-loaded`, while
the 695008-byte site bootstrap is skipped natively. Popcorn473's client
animation moves the device group from `translateY(72px)` to about 1px and
rotates the phones; its two main Next chunks are skipped. Original and
post-script Chromium DOM captures confirm these differences. No native
color, fixed-position or static-layout defect is established, and no authored
state is overridden. All three remain high. Evidence:
`temp/online-visual-round186/{diagnosis.json,current-metrics.json,pairs.png}`
and its browser source/DOM captures.


Round177 (2026-11-22) refines two media/startup diagnoses. Monarch's actual
hero is a mux-player component; its only ordinary light-DOM videos are hidden
or below the first viewport. The current native run skips six async chunks
(131397–1058840 bytes). An ordinary video-frame or loadeddata patch is therefore
not established as a repair for this hero. The component's remaining shadow,
scoped-style and stream path is unqualified; current partial shadow rendering
under JS13-L2 must not be described as wholly absent. Mux documents its shadow
encapsulation in the [player API](https://www.mux.com/docs/guides/player-api-reference/html).

A separate Setpoint diagnostic uses the existing process-local
RADIANT_JS_PRELAYOUT_DEFER_BYTES=0 override. Its 239880-byte module executes;
load completes in 14.62 seconds and the heading/navigation appear, while the
large product panel remains blank. This is admission evidence, not a shipping
policy change, performance comparison or full-page clearance. All normal
source limits remain unchanged. Evidence: temp/online-visual-round177/
diagnostic-qualification.json, browser/saas_landing_394/{dom.html,media.json},
current native task logs, and setpoint-admission-diagnostic/ (**D8.4.4 /
D9.5.6v2 / D10.1.7**).

Round176 (2026-11-22) isolates Agiloft's white viewport. Its wrapper and
ancestors reach opacity 1 with visible, correctly positioned native boxes;
the final view tree instead contains an opaque fixed div._vis_hide_layer at
z-index 2147483647. The authored VWO bootstrap removes that cover in finish(),
with a 2000ms settings fallback. Its 716230-byte tag is skipped by native source
admission. The separate core-animation callback exception does not establish
the blank-page cause. No cover is removed and no timer/source limit changes.
Evidence: `temp/online-visual-round176/agiloft-view.json`, current navigation
HTML, native trace and instrumented snapshots (**D8.4.4 / D10.1.7**).

Round172 (2026-11-22) rechecks two older media omissions with the qualified
RI-204 renderer and the existing 1000ms frame. FigJam still lacks its large
product media; eight settled tasks are skipped as large, including the
1,094,814-byte chunk. Frame's customers page still lacks its image montage;
ten settled tasks are skipped as large, including a 502,945-byte chunk.
Neither attempt reports a task exception. The improved FigJam brand alone
does not clear the page. NVD also retains its already recorded 2,691,323-byte
main-module admission barrier. These are current admission observations,
not proof that admitting the full apps would qualify their remaining behavior.
No cap, visibility, canonical pair or browser reference changes. Evidence:
`temp/online-visual-round172/admission-summary.json` and `review/live/`
(**D8.4.4 / D10.1.7**).

Round165 (2026-11-22) checks Aircall with only the pre-layout source limit
disabled as a diagnostic. The release AST executes the 629523-byte GTM,
585747-byte OneTrust and 514534-byte gtag sources; the consent panel appears,
but the normal header/hero disappears. The native layout contains the consent
panel and no normal application content; its backdrop is display:none. This
is not a qualified policy change. All shipping admission limits remain intact.
The separate MessagePort/transport-data defects found during this investigation
are fixed under JS13-L52 without claiming they caused that page failure.
Evidence: temp/online-visual-round165/admission-diagnostic/,
aircall-admission-contact.png and aircall-diagnostic-layout-summary.json.


Round146 (2026-11-21), using the qualified RI-184 renderer and the unchanged
1000ms post-load frame, adds three focused observations. Figma pricing skips
eight async chunks, including the1,094,814-byte `6986-096f061ed4aeb73f.js`.
Its current initial HTML includes the pricing content and a fixed, full-screen
`#exp-overlay` with an opaque white fallback controlled by the Statsig client.
The native frame remains white. WeTransfer skips its133,035-byte
`428m3c1fnans3.js` and132,636-byte `1kd31x42pyvte.js`; its initial main contains
an empty opacity-zero container, with the hero content in JSON until client
rendering. Handhold skips six startup chunks, including354,055-byte and
161,282-byte sources; its
server-rendered headline appears but the complete interface remains absent.
No task exception is reported in these focused attempts. These are confirmed
admission barriers, not proof that admitting every chunk would qualify the
rest of the runtime or resolve every visual difference. No limit, visibility
override or canonical capture is changed (**D8.4.4 / D10.1.7**).
Evidence: `temp/online-visual-round146/figma-pricing.html`,
`handhold.html`, `saas_landing_582/`, `saas_landing_571/` and
`handhold-current/`. The WeTransfer initial-shell source is retained in
`temp/online-visual-round2/documents/saas_landing_571.html` and its listed
chunk URLs agree with the current task trace.

Round142 (2026-11-21) confirms the same admission limit on Dayos. Its current
`/_nuxt/DkFIkpF9.js` is2,383,199 bytes. Opt-in task diagnostics show the module
ready but executed0 with status `skipped-large-defer`; inline configuration
scripts execute. The qualified1000ms post-load native frame retains the yellow
startup overlay, while Chromium shows the main interface. This establishes
that the main startup module never runs; further runtime compatibility after
admission remains unqualified. No source limit is raised and no overlay is
removed. The existing static-paint repair RI-112 remains valid. Evidence:
`temp/online-visual-round142/dayos-entry.mjs`, `dayos-diagnostic.log` and
`dayos-engine.log`; the current pair is in report128. Boundaries remain
**D8.4 / D10.1.7 / D9.1.1v4 / D9.1.3v2**.


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
