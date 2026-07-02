# Lambda Bug — LambdaJS: class field-initializer arrow loses `this` inside a nested function scope

**Date:** 2026-07-01 · **Component:** LambdaJS JS→MIR transpiler (class field initializers / `this` scope-env capture) · **Status:** FIXED
**Discovered by:** Stage 4C (editor test suite under Radiant) — blocks `test/editor-js/test/view/editor-view-dom.test.ts` (~9) + `full-editor-dom.test.ts` (~14). See `vibe/editing/Radiant_Editor_Stage4C.md`.

---

## 1. The issue (what we're trying to fix)

A **class field-initializer arrow function** — e.g. `handler = (ev) => { … this.state … }` — must capture `this` **lexically as the class instance** (per JS spec, a field initializer runs with `this` bound to the instance being constructed). Under LambdaJS this works **only when the class is defined at global scope**. When the class is defined **inside any nested function scope** (a plain function, an arrow, or an IIFE), the field-initializer arrow captures the **enclosing function's `this`** instead of the instance.

This matters because **esbuild wraps every bundle in an IIFE** `(() => { … })()`. So in any bundled editor build, *every* class whose instance methods are stored as field-initializer arrows (the standard pattern for DOM event handlers: `onClick = (e) => { this.… }`) gets a broken `this`.

---

## 2. Symptom

Inside the field-arrow, `this` is **not the instance** — it is the enclosing function's `this` (which, in an IIFE under `"use strict"`, is `undefined`/global). Concretely:

- Direct/`.call` use: `this` reads as undefined → `this.foo` is `undefined` ("NO-THIS").
- As a DOM event listener dispatched via native `fire_listeners` → `js_call_function`: `this.state`/`this.dispatch` dereference garbage → **`AddressSanitizer: BUS` in `js_debug_check_callee` → `get_type_id`** (`lambda/js/js_runtime.cpp:8943`, called from `js_dom_events.cpp:1722`). Non-ASan builds SIGSEGV/SIGABRT.

Observed end-to-end: the plain-DOM editor mounts, then the first `beforeinput` dispatch crashes the process.

---

## 3. Minimal reproduce

### 3a. Smallest (no DOM) — `temp/4c-spikes/min_this.js`
```js
"use strict";
(() => {
  var E = class { f = () => this; };
  var e = new E();
  console.log('result: ' + (e.f() === e ? 'INSTANCE' : 'WRONG'));
})();
```
Run: `./lambda.exe js min_this.js --no-log`
- **Expected:** `result: INSTANCE`
- **Actual:** `result: WRONG`

### 3b. Declaration-vs-expression / global-vs-nested matrix
```js
"use strict";
class Decl { tag = 'inst'; f = () => (this ? this.tag : 'NOTHIS'); }
console.log('GLOBAL decl:   ' + new Decl().f());            // -> inst   (OK)
(() => {
  class D { tag = 'inst'; f = () => (this ? this.tag : 'NOTHIS'); }
  console.log('NESTED decl:  ' + new D().f());              // -> NOTHIS (BUG)
})();
(function(){
  var E = class { tag = 'inst'; f = () => (this ? this.tag : 'NOTHIS'); };
  console.log('NESTED expr:  ' + new E().f());              // -> NOTHIS (BUG)
})();
```
The single variable is **being nested inside a function scope**. Class declaration vs expression, arrow-IIFE vs function-IIFE, bare fields, explicit constructor — none of those matter; only the nesting does.

### 3c. The real trigger (DOM event listener) — crashes
```js
class C {
  tag = 'INSTANCE'; d;
  constructor(){ this.d = document.createElement('div'); document.body.appendChild(this.d);
                 this.d.addEventListener('keydown', this.onK); }
  onK = (_ev) => { this.tag; };   // this === undefined → BUS on dispatch in a full bundle
}
new C().d.dispatchEvent(new KeyboardEvent('keydown', {}));
```
(esbuild-bundle to an IIFE, run under `lambda.exe js … --document page.html`.)

---

## 4. Root cause diagnosis

Confirmed via MIR dump (`JS_MIR_DUMP=1 ./lambda.exe js temp/4c-spikes/min_this.js --no-log` → `temp/js_mir_dump.txt`), debug build.

**The arrow reads `this` from its closure env, and that env slot holds a stale value.**

The field-arrow compiles to (function `_js_anon0_200`):
```
_js_anon0_200: func i64, i64:_js.env
    mov  _js_this_1, i64:(_js.env)                 # load _js_this from closure env slot 0
    call js_resolve_lexical_this, …, _js_this_1    # js_runtime_state.cpp:910 — returns it as-is (unless 0/TDZ)
    ret  …
```

The enclosing function (`_js_anon1_170`) populates the **child scope-env** (`mt->scope_env_reg`, shown as `js_alloc_env_4`) `_js_this` slot **once at prologue**, from the enclosing lexical `this`:
```
call js_get_lexical_this_binding, …_5             # = enclosing this (undefined in the IIFE)
mov  i64:(js_alloc_env_4), …_5                     # write child scope-env this-slot (slot 0)
```
Every child closure created in this function (including the field-arrow, via `js_new_closure`) reused **this scope-env pointer**. Since it was set to the enclosing `this` at prologue and never updated to a stable field-initializer cell, the arrow observed the wrong value.

**Why global scope works:** at global scope there is no enclosing function `_js_this` var, so `jm_emit_current_this` (`js_mir_expression_lowering.cpp:561`) falls back to `js_get_this()` at call time — which for a direct method call `x.f()` is the instance. Inside a function, the `_js_this` var exists and is captured instead, so the fallback is never taken.

**Why the base-class construction path doesn't fix it (but `_for_super` does):**
- The super-class field-init path `jm_emit_public_instance_fields_for_super` (`js_mir_expression_lowering.cpp:628`) does `js_set_this(obj)` + `jm_emit_update_lexical_this_binding(mt, obj)` before transpiling initializers, and it works — because in the *constructor* context the `_js_this` var carries proper `scope_env_reg`/`scope_env_slot`, so the helper updates the very slot arrows capture from.
- The base-class paths (inline `new`) — `jm_emit_own_instance_fields_on_object` (`js_mir_statement_lowering.cpp:2153`) and the inheritance-chain path (~`js_mir_statement_lowering.cpp:3242`) — update only the `_js_this` **reg** (and the reg is not what arrows capture). At those points the `_js_this` var has `scope_env_reg == 0`, so `jm_emit_update_lexical_this_binding` is a no-op. The **child scope-env this-slot (`mt->scope_env_reg`) is never repointed at `obj`.**

### Layer
This is in **JS→MIR lowering**, not JIT codegen — it reproduces identically under `JS_MIR_INTERP=1` (MIR interpreter) and the default JIT.

### Key files / lines
- `lambda/js/js_mir_expression_lowering.cpp:561` `jm_emit_current_this` (arrow → captured `_js_this` → `js_resolve_lexical_this`)
- `lambda/js/js_mir_expression_lowering.cpp:607` `jm_emit_update_lexical_this_binding` (updates reg + `js_this_var` scope-env slot; no-op when `scope_env_reg==0`)
- `lambda/js/js_mir_expression_lowering.cpp:628` `jm_emit_public_instance_fields_for_super` (super path — WORKS)
- `lambda/js/js_mir_statement_lowering.cpp:2153` `jm_emit_own_instance_fields_on_object` (base path — missing scope-env this-slot update)
- `lambda/js/js_mir_statement_lowering.cpp:3242`+ inheritance-chain inline-`new` field init (updates `_js_this` reg only)
- `lambda/js/js_mir_statement_lowering.cpp:3364` "own class instance fields" loop
- `lambda/js/js_runtime_state.cpp:910` `js_resolve_lexical_this` (returns captured value as-is unless 0/TDZ)
- `lambda/js/js_mir_context.hpp:462` `mt->scope_env_reg` (current func's child scope env — the reg arrows capture from), `:463` `scope_env_slot_count`

---

## 5. Fix landed

Implemented in the JS→MIR lowering:

1. Added `mt->force_closure_env_copy` plus a capture guard so field-initializer arrows whose captures are lexical meta bindings (`_js_this`, `_js_new.target`, `_js_arguments`) do **not** reuse the enclosing function's shared scope env.
2. Added `jm_emit_begin_lexical_this_rebind()` / `jm_emit_end_lexical_this_rebind()` to temporarily write the instance/class object into both the `_js_this` register and the resolved current scope-env slot while initializer closures are built.
3. Applied the helper to base-class instance field initialization, `jm_emit_own_instance_fields_on_object()`, and the class static-field declaration/expression/batch lowering paths. The `super()` public-field path now also forces copied closure envs while field initializers run.

The key correction from the initial proposed fix: `js_new_closure()` stores the env pointer, it does not copy the env array. Therefore writing the parent `_js_this` slot and then restoring it is not sufficient by itself. For the field-handler shape that captures lexical `this`, the field initializer must force a copied closure env while the slot is rebound; the arrow then keeps the instance/class value after the enclosing scope env is restored.

### Regression

Added `test/js/class_field_arrow_nested_this.{js,txt}` covering:

- nested class declaration instance field arrow `this`
- nested class expression instance field arrow `this`
- two separate instances, to guard against a shared mutable `this` cell
- nested static field arrow `this`

### Validation

- `./lambda.exe js temp/4c-spikes/min_this.js --no-log` → `result: INSTANCE`
- `JS_MIR_INTERP=1 ./lambda.exe js temp/4c-spikes/min_this.js --no-log` → `result: INSTANCE`
- `./lambda.exe js temp/4c-spikes/field_this_matrix.js --no-log` → nested declaration/expression/static cases pass
- `./lambda.exe js test/js/class_field_arrow_nested_this.js --no-log` → matches expected output
- `./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/*class_field_arrow_nested_this*'` → passed
- `make build` → passed

---
---

# Bug 2 — residual: class field-arrow with MIXED captures (`this` + a called module binding) still loses `this`

**Date:** 2026-07-01 · **Component:** same (JS→MIR class field-init `this` capture) · **Status:** FIXED
**Discovered by:** Stage 4C — Bug 1's fix unblocked the simple cases, but `test/editor-js/test/view/editor-view-dom.test.ts` (~9) + `full-editor-dom.test.ts` (~14) still crash: the editor **mounts** (constructor `this` now correct), then the first `beforeinput` dispatch fails.

## 1. The issue (what remains)

Bug 1's fix forces a private (copied) closure env for a field-initializer arrow **only when the arrow's captures are *exclusively* lexical meta bindings** (`_js_this` / `_js_new.target` / `_js_arguments`). But a real event handler almost always **also captures an ordinary binding** — e.g. it calls an imported/module function. `EditorViewDom.handleBeforeInput = (ev) => { const intent = intentFromInputEvent(ev); … this.state … }` captures **both** `_js_this` **and** `intentFromInputEvent`. For such a **mixed-capture** arrow the guard is bypassed, the arrow reverts to the enclosing function's shared scope-env `_js_this`, and `this` is lost again — reintroducing Bug 1 for the dominant real-world shape.

## 2. Symptom

`this` is `undefined` at the very start of the dispatched field-arrow handler:
`TypeError: Cannot read properties of undefined (reading 'state')` (from `handleBeforeInput`'s `const cur = this.state`), swallowed by `fire_listeners` as `event listener for 'beforeinput' threw; continuing dispatch`; in a full bundle it surfaces as the same `AddressSanitizer: BUS` in `js_debug_check_callee` → `get_type_id` (`lambda/js/js_runtime.cpp:8943`). The editor mounts fine (Bug 1 fixed constructor-time `this`), then the first `beforeinput` dispatch throws/crashes.

## 3. Minimal reproduce

Single class, one imported function called from the handler — `temp/4c-spikes/min2crash.js`:
```js
import { intentFromInputEvent } from './src/view/intent-from-input-event.js'  // any cross-module fn
class A {
  state = { n: 1 }; d;
  constructor(){ this.d = document.createElement('div'); document.body.appendChild(this.d);
                 this.d.addEventListener('beforeinput', this.h); }
  h = (ev) => { intentFromInputEvent(ev);                 // <-- captures a module binding
                globalThis.__A = (this && this.state) ? 'OK' : 'THIS-UNDEF'; }
}
new A().d.dispatchEvent(new InputEvent('beforeinput', { bubbles:true }));
console.log(globalThis.__A);
```
esbuild → IIFE, run `./lambda.exe js min2crash.js --document page.html`.
- **Expected:** `OK` · **Actual:** `THIS-UNDEF`

**Control matrix** (single variable isolated — `temp/4c-spikes/call.js`, `mixed.js`):

| Field-arrow handler body | `this` |
|---|---|
| calls an **imported (cross-module)** function + reads `this` | **THIS-UNDEF (bug)** |
| same class, imports present, but handler does **not** call it | OK |
| calls a **local (same-file)** function + reads `this` | OK |
| captures an outer local **var** + reads `this` | OK |
| reads `this` only (the Bug-1 shape) | OK (fixed) |

The single trigger: **the field-arrow captures `this` *and* a cross-module binding (calls an imported function).**

## 4. Root cause diagnosis

MIR dump of `min2crash.js` (`JS_MIR_DUMP=1 ./lambda.exe js temp/4c-spikes/min2crash.js --document page.html` → `temp/js_mir_dump.txt`): the handler arrow `_js_anon0_1251` has **both** `_js_intentFromInputEvent_171` and `_js_this_172` in its locals, and reads `_js_this` from its **closure env slot**:
```
_js_anon0_1251: func i64, i64:_js.env, i64:_js_ev
    …
    mov  _js_this_172, i64:16(_js.env)                 # _js_this from closure env (slot 2)
    call js_resolve_lexical_this, …, _js_this_172
```
That env slot was populated at closure creation from the **enclosing function's shared scope-env `_js_this`** (undefined in the IIFE) — because Bug 1's `force_closure_env_copy` guard **did not fire** for this arrow: its capture set is **not exclusively** lexical meta bindings (it also captures `intentFromInputEvent`).

**Residual root cause:** the guard condition is **too narrow**. It forces the private-closure-env copy + `_js_this` rebind only for arrows whose captures are *purely* `_js_this`/meta, not for the (dominant) case of an arrow capturing `_js_this` **plus** one or more ordinary bindings. Same layer (JS→MIR lowering); reproduces identically under `JS_MIR_INTERP=1`.

## 5. Fix landed

Widened Bug 1's capture guard — `jm_force_copied_env_for_field_initializer` (`lambda/js/js_mir_expression_lowering.cpp` ~12460). The old test forced the private closure-env copy only when **every** capture was a lexical meta binding (`for (…) if (!is_meta(cap)) return false; return true;`). It now forces the copy when the arrow captures a meta binding **at all**, regardless of other captures:
```c
for (int ci = 0; ci < fc->capture_count; ci++)
    if (jm_capture_is_lexical_meta_binding(fc->captures[ci].name)) return true;
return false;
```
`mt->force_closure_env_copy` is set only during field initialization, so this only affects field-initializer arrows. (Rare edge case knowingly accepted: a field-init arrow that captures `this` **and mutates** an ordinary outer binding now gets a copied env, so that mutation would not persist to the outer scope — but such an arrow's `this` was already broken, and the real-world captures are read-only function references.)

### Validation
- `./lambda.exe js temp/4c-spikes/min2crash.js --document page.html` → `OK` (was `THIS-UNDEF`)
- `./lambda.exe js temp/4c-spikes/call.js --document page.html` → A and B both correct; `mixed.js` D/E correct
- Bug 1 unchanged: `min_this.js` → `INSTANCE`; `test_js_gtest` `*class_field_arrow_nested_this*` passes
- No regression: Stage-4C core **207/209**, tier_e **520/520**; `test_js_gtest` **299 passed** (only pre-existing `vm_runincontext_cross_unit` fails); `make build` clean
- Stage-4C view bundle `editor-view-dom`: **crash → runs (3/9)** — the this-capture crash is gone. *(Remaining `editor-view-dom` failures ("…reading 'doc'": the edit pipeline yields no transaction) and the `full-editor-dom` crash are SEPARATE follow-ups, not this bug.)*
- Recommended before merge: `make node-baseline` (class `this` is load-bearing).
- `git diff --check` → clean

---
---

# Bug 3 — LambdaJS headless (`lambda.exe js --document`): post-dispatch `process.exitCode` lookup interns with `context == NULL`

**Date:** 2026-07-01 · **Component:** LambdaJS runtime — `process.exitCode` bridge across JS `EvalContext` lifetime · **Status:** FIXED — and confirmed to be a RED HERRING for the editor (the real editor blocker is a separate `onChange is not a function` / "Bug 4")
**Discovered by:** Stage 4C — while chasing the editor's post-Bug-2 `editor-view-dom` failures.

> **READ §5–§6 FIRST.** §1–§4 preserve the *initial* diagnosis and are partly superseded. The live repro showed the failing intern happens **after** the dispatched handler and after `transpile_js_to_mir` restores `context` to the caller's old value (`NULL` on CLI `js --document`), when `main.cpp` asks `js_process_current_exit_code()` one more time. It is a genuine runtime boundary bug, but it is NOT what breaks the Stage-4C view tests.

## 1. The issue

`heap_create_name()` (`lambda/lambda-mem.cpp:557`) interns structural identifiers (map keys, element tags, attribute/mark names) via `context->name_pool`, where `context` is the thread-local `EvalContext*`:
```c
String* heap_create_name(const char* name, size_t len) {
    if (!context || !context->name_pool) { log_error("heap_create_name called with invalid context or name_pool"); return nullptr; }
    return name_pool_create_len(context->name_pool, name, len);
}
```
On the `lambda.exe js --document` path, the original suspicion was that `context`/`context->name_pool` became invalid inside `js_dom_dispatch_event` → `fire_listeners` → `js_call_function`. That framing was wrong. The handler's own dynamic object/Map interning succeeds; the invalid intern happens later, after JS execution has completed and the CLI queries process exit state without an active JS `EvalContext`.

## 2. Symptom

Log (even with most logging off):
```
[ERR!] heap_create_name called with invalid context or name_pool
[ERR!] map_get: key must be string or symbol, got type null
```
For trivial handler code the JS-visible result still looks right (the value semantics survive a failed intern). ~~For the editor, the null names corrupt the produced transaction…~~ **Correction (see §5):** this causal link was WRONG. The editor's transaction is produced correctly (`P5 tx=ok`); the failing interns observed (`'exitCode'`) are post-dispatch noise, and the `editor-view-dom` "reading 'doc'" failures come from a *separate* bug (`onChange is not a function`, Bug 4), not from Bug 3.

## 3. Minimal reproduce

`temp/4c-spikes/interncmp.js` — intern in a plain call (OK) vs a dispatched handler (fails):
```js
"use strict";
function doIntern(t){ var o = {}; o['k'+t] = 1; var m = new Map(); m.set('key'+t, 2); return o['k'+t] + m.get('key'+t); }
console.log('A plain-call=' + doIntern('A'));                       // 0 heap_create_name errors
var d = document.createElement('div'); document.body.appendChild(d);
d.addEventListener('keydown', function(){ globalThis.__B = doIntern('B'); });
d.dispatchEvent(new KeyboardEvent('keydown', { key:'x', bubbles:true }));  // 2 heap_create_name errors in log.txt
console.log('B dispatched=' + globalThis.__B);
```
Run: `./lambda.exe js interncmp.js --document page.html` (WITHOUT `--no-log`), then inspect `log.txt`:
- **Expected:** 0 `heap_create_name … invalid context` errors
- **Actual before fix:** 2 errors, but log ordering proves they occur after `js-mir: transpilation completed`, not inside the handler body.

End-to-end editor probe (`temp/4c-spikes/pipe2.js`): a real `EditorViewDom`-shaped handler runs and prints `P5 tx=ok steps=1` (the tx IS produced). The `heap_create_name` errors were post-dispatch noise, not the editor blocker.

## 4. Root cause diagnosis

`context` (thread-local `EvalContext*`) is the source of `name_pool` for `heap_create_name`. It is established for the top-level script run and remains valid through the synchronous DOM dispatch. The actual failing path is the CLI/process bridge after the JS MIR entrypoint returns:

1. `transpile_js_to_mir_core_len()` calls `js_process_current_exit_code()` while the JS context is still active, which synchronizes the C-side `js_process_exit_code_value` cache from `process.exitCode`.
2. The entrypoint then restores `context = old_context`; on standalone `lambda.exe js --document`, `old_context` is `NULL`.
3. `lambda/main.cpp` computes the final exit code by calling `js_process_current_exit_code()` again.
4. The old implementation tried to read `process.exitCode` from the JS object every time, so it called `heap_create_name("exitCode")` with `context == NULL`, producing the two `heap_create_name` errors and the null-key `map_get` error.

Confirmed by `log.txt`: before the fix, the invalid-context errors appear after `js-mir: transpilation completed`, not between the dispatch-site and handler-call logs. The first failing intern is `"exitCode"`.

**Correction (see §5–§6):** the initial framing above — "the dispatch path does not re-establish `context`" — is incorrect. `js_call_function` does not touch `context`, and `context` is valid at the dispatch site. The pointer is restored to `NULL` only after JS execution completes.

### Key files
- `lambda/lambda-mem.cpp:557` `heap_create_name` (the `context->name_pool` guard)
- `lambda/js/js_globals.cpp:2641` `js_process_current_exit_code` (fixed boundary)
- `lambda/js/js_mir_entrypoints_require.cpp:995`/`:1009` syncs process exit code while JS context is active
- `lambda/js/js_mir_entrypoints_require.cpp:1052` restores `context = old_context`
- `lambda/main.cpp:2387` asks for final JS exit code after the JS entrypoint has returned

## 5. Refined findings + attempt log (2026-07-01)

Deeper diagnosis with a pointer-level probe (log `context` / `context->name_pool` / `_lambda_rt` at the dispatch site vs at the `heap_create_name` failure), plus final log ordering:
- **At the dispatch site** (`fire_listeners`, before `js_call_function`): `context`, `context->name_pool`, and `_lambda_rt` are ALL valid and consistent.
- **At the failure point**: **`context == NULL`** (not merely a null `name_pool`) and first failing intern is `"exitCode"`.
- **Ordering proof**: `log.txt` shows the invalid-context errors after `js-mir: transpilation completed`, so the failure is outside the dispatched handler body.

So the loss is not at the dispatch boundary, and not inside the handler's own interning. The correct invariant is: `js_process_current_exit_code()` may read the JS `process` object only while an `EvalContext` is active; after the JS entrypoint returns, it must use the scalar exit-code cache that was already synchronized while the context was live.

### Attempts (both reverted)
1. **Restore `context` from `_lambda_rt` at the `fire_listeners` dispatch site** — no effect: `context` is valid there and gets nulled later. Reverted.
2. **Fallback in `heap_create_name`: use `_lambda_rt`'s `name_pool` when `context`/`name_pool` is null** — eliminated the interning errors (`interncmp.js` 2→0), **but** (a) regressed the JS unit suite by ~13 (299→286: typed-array/DataView/child_process/fuzz) because `heap_create_name` is core and `_lambda_rt` is stale in contexts where `context` is *legitimately* null, and (b) **did NOT fix the editor**. Reverted.

### IMPORTANT — `heap_create_name` was a RED HERRING for the editor
Fixing the interning errors left `editor-view-dom` at **3/9** unchanged. The `'exitCode'` interning failures are post-dispatch noise, not the edit-pipeline blocker. The transaction IS produced (`P5 tx=ok`). **The editor's real blocker is a SEPARATE bug:** `editor-view-dom` fails with **`onChange is not a function`** — inside `EditorViewDom.dispatch = (action) => { … this.onChange?.(this.state) }`, `this.onChange` holds a non-function (the empty `seenStates` → `Cannot read properties of undefined (reading 'doc')` cascades from that). This looks like *another* instance-field/`this`-binding corruption in the dispatched path (`this.onChange` was set from `opts.onChange`, a function) and should be filed/diagnosed as **Bug 4** — it, not Bug 3, is what blocks the Stage-4C view tests.

## 6. Fix landed

`js_process_current_exit_code()` now reads `process.exitCode` from the JS object only when `context && context->name_pool` is valid. After the JS entrypoint has restored `context` to the CLI caller's old value, it returns the already-synchronized `js_process_exit_code_value` scalar instead. This keeps `heap_create_name()` strict and avoids the reverted `_lambda_rt` fallback.

Regression added:
- `test/js/js_document_exit_context.{js,html,txt}` covers the visible `js --document` behavior.
- `JavaScriptRegression.DocumentExitCodeAfterContextRestoreDoesNotInternWithNullContext` runs the same script without `--no-log`, then asserts `log.txt` has no `heap_create_name called with invalid context or name_pool` or null-key `map_get` errors.

Validation:
- `./lambda.exe js temp/4c-spikes/interncmp.js --document temp/4c-spikes/page.html` → `A plain-call=3`, `B dispatched=3`, 0 invalid-context errors.
- `./lambda.exe js test/js/js_document_exit_context.js --document test/js/js_document_exit_context.html` → same output, 0 invalid-context errors.
- `make build` → passed.
- `make build-test` → passed (pre-existing warning noise only).
- `./test/test_js_gtest.exe --gtest_filter=JavaScriptRegression.DocumentExitCodeAfterContextRestoreDoesNotInternWithNullContext` → passed.
- `./test/test_js_gtest.exe --gtest_filter=JavaScriptTests/JsFileTest.Run/js_document_exit_context` → passed.

---
---

# Bug 4 — hoisted IIFE-scope function declaration not written to its module-var slot → nested inline-`new` field-arrow captures garbage ("X is not a function")

**Date:** 2026-07-01 · **Component:** LambdaJS JS→MIR lowering — function-declaration hoisting / module-var write-through · **Status:** FIXED
**Discovered by:** Stage 4C — the real editor blocker behind `editor-view-dom` (`onChange is not a function` / `Cannot read properties of undefined (reading 'doc')`). This is the "Bug 4" flagged as a red herring away from Bug 3. See `vibe/editing/Radiant_Editor_Stage4C.md`.

## 1. The issue

A **class field-initializer arrow** that captures an **outer-scope function declaration** reads garbage for that function **when the class is inline-`new`'d inside a nested function**. After esbuild bundles the editor into an IIFE, every module import becomes an IIFE-scope function/const; the editor's `EditorViewDom.handleBeforeInput = (ev) => { intentFromInputEvent(ev); … this.dispatch(…) }` captures such a binding, and the view is constructed inside a nested `mount()`/`it()` closure — so the capture resolves to an undefined slot and calling it throws `intentFromInputEvent is not a function`, aborting the handler before `dispatch` runs (→ `onChange` never fires → downstream `reading 'doc'` on the empty `seenStates`).

## 2. Symptom

Inside the dispatched field-arrow, `typeof <captured outer fn>` is `"object"` (garbage), and calling it throws `is not a function`. In a full editor bundle the wild value can also be a stack/heap pointer → `EXC_BAD_ACCESS` in `Item::type_id` via `js_debug_check_callee`. `editor-view-dom` sat at 3/9; the whole typing pipeline failed.

## 3. Minimal reproduce — `temp/4c-spikes/t3.js`
```js
"use strict";
(() => {
  function helper(){ return 42; }
  class A { m = () => helper(); }
  function mount(){ return new A(); }        // inline-new INSIDE a nested function
  console.log(mount().m());                  // expected 42; actual: throws "is not a function"
})();
```
Reproduces identically under `JS_MIR_INTERP=1` (lowering bug, not JIT). Variants that WORK isolate the trigger: (a) top-level `new A()` — OK; (b) a nested plain closure reading `helper` — OK. Only **nested inline-`new` + field-arrow capture of an IIFE-scope decl** fails.

## 4. Root cause

`helper` is a function declaration local to the IIFE. Its binding is promoted to a **module-var slot** (`module_consts` entry `MCONST_MODVAR`, `int_val = 2`), and a nested closure's field-arrow capture resolves it via `js_get_module_var(2)` (`jm_create_func_or_closure`, `lambda/js/js_mir_expression_lowering.cpp` ~12612 — the fallback taken because `jm_find_var("_js_helper")` misses inside `mount`). But the **inner-function-declaration hoist** (`jm_function_class_lowering.cpp` ~542 and ~3046) wrote the closure only to its local reg + the shared scope env via `jm_scope_env_mark_and_writeback` — it **never wrote the module-var slot**. So `js_get_module_var(2)` returned an undefined/garbage slot. (At top level the capture instead reads the closure from the scope-env slot directly via `jm_find_var`, so it was fine — hence top-level `new` worked and only nested `new` failed.) `is_iife_var` is `false` on this entry; the missing write-through is unconditional, matching the non-direct function-declaration statement path in `js_mir_statement_lowering.cpp` which already writes the module var for `MCONST_MODVAR`/`var_kind==0`.

### Key files
- `lambda/js/js_mir_function_class_lowering.cpp` — the two "Hoist inner function declarations" loops (~542, ~3046) that created the closure without a module-var write.
- `lambda/js/js_mir_expression_lowering.cpp:~12612` — the capture-fill fallback that reads `js_get_module_var(int_val)`.
- `lambda/js/js_mir_statement_lowering.cpp:~5175` — the existing (non-direct) write-through this mirrors.

## 5. Fix landed

Added `jm_hoisted_func_modvar_write_through(mt, vname, val_reg)` and called it right after both inner-function-declaration hoist writes. It looks up `vname` in `module_consts` and, for `MCONST_MODVAR` / `var_kind==0` / not `annexb_suppressed`, emits `js_set_module_var(int_val, closure)` — so every resolution path (module-var readers + scope-env readers) sees the same closure. Same condition the non-direct statement path already uses (no `is_iife_var` gate).

### Validation
- `./lambda.exe js temp/4c-spikes/t3.js` → `42` (was: throws). Same under `JS_MIR_INTERP=1`.
- Stage-4C `view/editor-view-dom` bundle: **3/9 → 9/9** (added the missing `toHaveBeenCalled`/`toHaveBeenCalledTimes`/`toHaveBeenCalledWith` mock matchers to the in-engine harness for the last case). `render-vnode` 9/9, `dom-bridge` 7/7, `html-parser` 12/12, `use-editor-state` 5/5, `reconcile` 4/6 (pre-existing DOM-fidelity gaps).
- Regression: `test/js/class_field_decl_capture_nested_new.{js,txt}` (top-level + nested inline-new + capture-with-`this` + sibling arrows) — passes in `test_js_gtest`.
- No regression: `test_js_gtest` **301/302** (only pre-existing `vm_runincontext_cross_unit`). `make build` clean.

### Remaining (separate, NOT this bug)
- `view/full-editor-dom` still crashes — a **stack overflow** (`Item::type_id` at a stack address), a distinct deeper issue (render/reconcile loop or DOM-fidelity gap), matching Stage-4C's "Bug 4 + possibly more".
- Hand-repro `temp/4c-spikes/nest3c.js` exposes an adjacent **env-sizing** bug: a mixed-capture field arrow that reuses `mount`'s size-1 scope env reads a captured fn from an out-of-bounds slot. Pre-existing, does not affect the real editor tests; filed as a follow-up.
