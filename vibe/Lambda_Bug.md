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
