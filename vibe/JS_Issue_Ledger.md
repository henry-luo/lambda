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

## 5. Functions, closures & scope (JS_05)

### JS05-L1 — `with` scope of a suspended generator leaks to unrelated code — **RESOLVED**

**Found:** 2026-09-11. **Reproduced against:** a build of unmodified `master`
at `ef03733de`. **Fixed:** 2026-09-11 by JSCU44
([`Lambda_Design_Structs_JS2.md`](Lambda_Design_Structs_JS2.md) §13), the
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

**Fix taken.** JSCU44's per-activation chain, not either option as first
sketched. Locally pushed scopes live in a free-listed `RootVector` rather than
in the activation's side-root frame: the side root stack is watermark-LIFO, and
a suspended generator holds its scope while other activations push and pop, so
LIFO storage would have required a spill on every suspension regardless. The
chain head *is* per-activation, which is what the fix needed. A generator's
chain is captured at creation and re-entered on every resume
(`JsSuspendedActivation::with_env`), so the contained option is subsumed.

The call-boundary copy is gone with it: the callee's inherited chain is its
closure's existing `js_alloc_env` capture, borrowed by one frame rather than
copied into a process-wide stack. `js_with_set_stack`, `js_with_save_stack` and
`JsSavedWithScope` are retired.

**Gate for any fix.** A regression test alongside
`test/js/regression_with_stack_gc.js` (auto-discovered by the baseline and
wired into `make test-gc-rooting-core`), plus `make test262-baseline` at zero
regressions.

**Note on Appendix A.2.** That table still describes `js_with_stack` as a
"16-slot array" and lists the `super_this_*` stacks as `JsItemStack`. Both are
stale: JSCU14(b) made the with-scope stack a growable `RootVector`, and
super-this moved to `js_call_activation_item(JS_CALL_ACTIVATION_SUPER_THIS)`.
