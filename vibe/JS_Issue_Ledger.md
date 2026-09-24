# LambdaJS — Central Issue Ledger

> **Working issue record for the JavaScript runtime under `lambda/js/`.**
> The design record stays in [`doc/dev/js/JS_00_Overview.md`](../doc/dev/js/JS_00_Overview.md)
> and its `JS_01`–`JS_16` set; the per-area "Known Issues & Future Improvements"
> sections there remain valid and are not yet consolidated into this file.
> This ledger is the home for defects found outside those sections.
>
> **Audience:** engine developers. **Status:** working ledger (`vibe/`), not normative.
> Semantic and design rulings are cited by `S#` / `D#` per CLAUDE.md rule 17;
> where no formal ruling covers a point, the vibe ledger ID is given.

## ID convention

Sections mirror the `doc/dev/js/JS_<area>` split. Issues consolidated from a
doc's own numbered list keep that number (`JS08-3` = `JS_08` known issue 3).
Issues that originate **here** take an `L` suffix (`JS05-L1`) so the two
numbering spaces cannot collide when the doc sections are folded in later.

| Mark | Meaning |
|---|---|
| **OPEN** | Reproduced in current source; `file:line` anchors resolved. |
| **PARTIAL** | Some sub-claims fixed, a real residue remains. |
| **RESOLVED** | Verified fixed; record retained with the fixing change. |

---

## 3. Value model, memory & GC interop (JS_03)

### JS03-L1 — durable JS native state retains raw wide-scalar `Item`s — **OPEN**

**Found:** 2026-09-14 during LR08-4's post-fix ownership audit.
**Reproduced against:** current MIR-default `./lambda.exe js` with and without
`LAMBDA_GC_FORCE_EVERY=1`. The AST execution backend passes the collection
probe; that does not make the default MIR path conformant.

**D2.5.2v3** rules that a raw wide-scalar `Item` pointing into a number frame
must never be retained. **D5.2.2v3** requires destination-owned scalar storage
at every ownership boundary. GC tracing the `Item` is insufficient: a scalar
home is not a GC allocation, and a trace does not copy its payload into the
durable destination.

Two native JS carriers still violate that rule.

1. **`Error` standard own fields.** `LambdaError` has raw one-word
   `js_name_item`, `js_message_item`, `js_cause_item`, and `js_stack_item`
   fields ([`lambda-error.h:220`](../lambda/runtime/lambda-error.h)). Both
   ordinary property assignment and the completion setter write an incoming
   `Item` directly ([`js_runtime.cpp:7289`](../lambda/js/js_runtime.cpp) and
   [`:7431`](../lambda/js/js_runtime.cpp)). `err_gc_trace` marks the words
   ([`lambda-error.cpp:1250`](../lambda/runtime/lambda-error.cpp)), but has no
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
   insertion-order storage ([`js_runtime.cpp:998`](../lambda/js/js_runtime.cpp),
   [`:19490`](../lambda/js/js_runtime.cpp)). `js_collection_method` builds the
   entry from raw arguments, calls `hashmap_set`, then calls
   `js_collection_order_upsert` ([`js_runtime.cpp:19708`](../lambda/js/js_runtime.cpp)).
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

## 5. Functions, closures & scope (JS_05)

### JS05-L1 — `with` scope of a suspended generator leaks to unrelated code — **RESOLVED**

**Found:** 2026-09-11. **Reproduced against:** a build of unmodified `master`
at `ef03733de`. **Fixed:** 2026-09-11 by JSCU44
([`Lambda_Design_Structs_JS.md`](Lambda_Design_Structs_JS.md) §18), the
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
(`JsRuntimeState.with_scope.stack`, [`js_runtime_state.hpp:269`](../lambda/js/js_runtime_state.hpp)),
pushed and popped by MIR-emitted calls to `js_with_push` / `js_with_pop`
([`js_mir_statement_lowering.cpp:3693`](../lambda/js/js_mir_statement_lowering.cpp),
`:3702`). Generator suspension unwinds the native activation without unwinding
that stack: nothing spills the entries the suspended frame owns, and nothing
restores them on resume. The call kernel's swap
(`js_with_set_stack(js_fn_with(fn)->env, …)`,
[`js_runtime.cpp:14045`](../lambda/js/js_runtime.cpp)) covers ordinary calls but
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
[`Lambda_Design_Structs_JS.md` §18](Lambda_Design_Structs_JS.md) for the table.
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

**Found:** 2026-09-24 during the JSI35 move of dynamic code to the AST interpreter (**D8.1.3v20**). **Pre-existing:** the MIR eval unit behaved the same way. Both tiers fail it.

```js
function f(a) { return eval("() => a"); }
f(7)();                  // ReferenceError: a is not defined (Node: 7)
```

A direct eval sees its caller's bindings only through the `EvalContext` bridge. The bridge projects caller cells as temporary global properties and writes them back when the eval returns (`JsInterpEvalBridge` in `lambda/js/js_interp.cpp`; `jm_emit_intrinsic_direct_eval` for MIR callers). A closure created inside eval code resolves `a` as a free name. Once the bridge frame pops, that name is gone. Closures over variables the eval itself declares are unaffected; they keep the eval script's own slot.

**Retirement path:** link a T0 caller's live environment records to the eval activation (`vibe/Lambda_Design_JS_Interpreter.md` §5.6 residual and §16 item 13). MIR callers keep the bridge until P4 shared environment cells.

**Fixed:** 2026-09-24 for interpreted callers (**D8.1.3v21 / JSI36**, `vibe/Lambda_Design_JS_Interpreter.md` §16 item 13). Regression: `test/js/regression_js05_eval_env_linking.js`. A direct eval from AST code no longer installs the bridge. Its activation gets a boundary record (`JsInterpEnv::eval_script`) holding the eval's lexical declarations, and that record's `outer` is the caller frame's live environment. Free names in eval code — and in closures it creates — resolve by name through that chain (`js_interp_lookup_name`), so `f(7)()` above reads the caller's `a` after the eval returns. The `JsInterpEvalBridge` projection, its write-back, and the per-activation journal frames on T0 functions are deleted. A MIR caller still uses the bridge, so a closure made by its eval code still loses the local; that is the P4 residual of §16 item 13.

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
