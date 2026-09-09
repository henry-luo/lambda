# LambdaJS AST Interpreter — P2/P3 Implementation Record

**Date:** 2026-08-26

**Status:** PARTIALLY IMPLEMENTED — the synchronous P2 core and the admitted
P3 breadth below are implemented; mixed-tier, suspension, and default-policy
phases remain open.

**Design authority:** `doc/Lambda_Formal_Design.md` **D1.3**, **D1.5**,
**D1.7**, **D5.3.2–D5.3.3**, **D6.2.2v2**, **D6.2.3v2**,
**D8.1.3v10**, **D8.2.4**, **D8.4.1v2**, and **D8.4.3v2**. The working
design is `vibe/Lambda_Design_JS_Interpreter.md` P2.

## Delivered boundary

`JS_EXECUTION_BACKEND=ast` parses, binds, indexes, and retains a `JsScript`
before executing its shared AST. `JsScript : Script` is catalogued by the
same `Runtime` that owns Lambda scripts. The interpreter obtains the
runtime's canonical `EvalContext`, allocates in its heap, and prepares the
script's own module-state slab. It therefore shares runtime, context, module
registry, event-loop owner, and GC heap with Lambda without importing Lambda
language semantics (**D1.3**, **D1.7**, **D8.1.3v10**).

AST functions are `JsFunction` objects with an explicit AST body kind. Their
normal calls and construction enter the established `fn->invoke` and
`fn->construct` kernels; AST evaluation is not a parallel call dispatcher
(**D6.2.2v2**). Function closures retain a traced `JsInterpEnv` chain with
mutable cells. This is JavaScript lexical capture by reference, while Lambda
snapshot capture remains confined to its own rule (**D6.2.3v2**).

The P2 walker implements literals, identifiers, unary/binary/logical/
conditional/sequence expressions, simple `var`/`let`/`const` declarations,
blocks, ordinary and arrow functions, calls/constructors, arrays/objects,
property references, assignment/update, `if`/`while`/`do`/`for`,
return/throw/break/continue, and `try`/`catch`/`finally`. Native callbacks
call back through the same dynamic JS function kernel. Function declarations
are instantiated once, `const` and TDZ writes are enforced, and loop-header
lexicals receive a fresh environment before the update so closures preserve
per-iteration identity.

## Admitted P3 surface

The same walker now also executes destructuring/default/rest patterns, array/
object/call spread, synchronous `for-in`/`for-of` with iterator closing,
switch and labels, optional chaining/logical assignment/delete, regex,
templates/tagged templates, and object methods/accessors. `with` uses the
existing object-environment APIs; AST closures created inside it capture the
same traced environment stack as compiled functions.

Classes use the existing class function and prototype kernels: public and
private methods/accessors are installed through the normal property descriptor
path, instance-field initializers are stored in runtime class metadata and
execute at construction time, and static fields/blocks execute with class
`this`. A traced class-private environment retains the evaluated class for
member bodies and nested closures; private keys, brands, private fields, and
private `in` use the shared runtime kernels. Direct eval projects those
retained private pairs through the existing eval-private bridge. Implicit and explicit derived
construction reuse the established class construct capability. `super()` uses
its derived-`this` and live-superclass helpers, then initializes the derived
fields; `super` property references use the same lexical-home-class property
helpers for class/object methods, accessors, statics, and arrows. This is all
within **D8.1.3v10**; it does not create a second JS object, class, iterator,
or call model.

Synchronous ES modules retain a `JsScript` import/export plan in the same
runtime registry used by Lambda and CommonJS. A loading module publishes its
registry-owned namespace placeholder, instantiates hoisted function exports
in its private module slab, then traverses static dependencies. This preserves
function identity and live import reads during a circular dependency. The
admitted path supports default/named/namespace imports, default/named/
namespace/non-ambiguous-star exports, named/star re-exports, `import.meta.url`,
and dynamic `import()` through the same resolver. A `.ls` dependency publishes
its existing Lambda namespace in that descriptor; JS calls its public function
through Lambda's boxed dynamic-call ABI after retaining the `TypeFunc` needed
for argument adaptation. No JS wrapper heap or separate context/heap/registry
is involved.

## Lifetime and rejection guarantees

`JsInterpEnv` is a GC-traced raw record: its outer edge and every `Item` cell
are marked. Active native frames hold exact object roots, and all operands
that cross a MAY_GC boundary occupy `RootFrame` or `RootSpan` slots
(**D1.5**, **D5.3.2–D5.3.3**). The focused suite forces collection after a
closure escapes and then calls it again.

Admission occurs after realm setup but before declarations or user code. The
forced AST backend rejects unsupported forms with a normal JavaScript error;
it never silently executes a second MIR copy after observable work
(**D8.1.3v10**, **D8.4.3v2**). The unset backend deliberately remains the
existing whole-script MIR policy.

Synchronous Test262 batches retain their parsed harness `JsScript`, not MIR
preamble state or heap objects. Each test receives a fresh realm, executes
that harness as a separate classic Script, then releases its Script/module
generation before the next test reuses those IDs. This preserves classic
global-lexical visibility while preventing mutable harness objects, callbacks,
or stale AST references from crossing the batch boundary (**D5.3.3**,
**D5.4.3**, **D8.1.3v10**).

## Validation

`test_js_script_gtest` covers:

- retained `JsScript` ownership and shared module-state identity;
- separate classic harness/test Scripts, fresh-realm harness rebuilding, and
  batch Script-generation reclamation;
- mutable closures surviving forced GC;
- explicit backend selection;
- control/property references and structured completions;
- arrow lexical `this`, ordinary construction, and native builtin callbacks;
- declaration identity, per-iteration closure cells, and `const` writes;
- direct/indirect eval with interpreted-cell writeback, eval-created function
  vars, global lexical synchronization, and shared runtime identity.
- ordinary and arrow-lexical `new.target` through the common construct state.
- explicit derived constructors and public `super` calls/references, including
  fields after `super()`, accessors, statics, arrows, and object-method homes.
- materialized runtime `arguments` objects, mapped sloppy parameter aliases,
  strict/non-simple unmapped objects, and escaped arrow lookup after nested
  calls.
- private fields, methods/accessors, static private elements, private `in`,
  direct eval, and nested functions retaining their class-private lexical
  identity.
- synchronous CommonJS `require()` resolution, cache identity, private wrapper
  bindings, module-registry publication, and restoration of the caller slab.
- synchronous ES static/dynamic imports, metadata, live named/star re-exports,
  namespace and anonymous-default exports, circular function imports, and a
  Lambda `.ls` import invoked through the shared registry and call boundary.

The expanded focused suite additionally covers patterns, iterator loops,
labels, `with` and escaped `with` closures, templates/tagged templates,
spread, optional/logical/delete operations, regex, object accessors, and
class methods/accessors/fields/statics/implicit inheritance and explicit
`super` dispatch.

## Suspension replay cursors (2026-09-09)

The interim suspension model (**JSI13**, **§10.1** — replay from the durable
activation until the §10.2 heapified continuation lands) originally had two
half-models: generators resumed structurally, async replayed. Async
skipped completed statements only at the async function body's *top-level*
list, and derived the ledger position of a skipped statement from a static
`js_interp_count_awaits` walk. Every nested list — a block, an `if`
consequent, a loop body — re-executed its already-completed statements on
each resume, and the static count was wrong for any statement whose await
count is dynamic. `for (let i=0;i<3;i++) o.push(await p(i))` produced
`0,0,1,0,1,2`; `{ push("x"); await p; push("y"); }` produced `x,x,y`.

Generators already solved this with `JsInterpGeneratorListContinuation`: one
statement cursor per list on the suspension path, each carrying the ledger
position at its resume point. That machinery is now shared rather than
duplicated (**D8.1.3v10**; the two walkers keep their own resume *signal*):

- `js_interp_list_head`/`js_interp_suspend_seen` select the generator record
  or the async context from the frame, so `js_interp_exec_list` owns the
  cursor logic once. `JsAsyncContextStateRecord::ast_list_continuation`
  replaces `ast_resume_statement`, and the async tracer marks its envs.
- Await cursors always set `replay_current_statement`: an await never owns
  its statement's completion the way a terminal `yield expr;` does.
- Loop continuations carry `resume_phase` (test/body/update) and
  `body_start_ledger`. A suspension in a loop *test* or *update* previously
  restarted the loop, re-running completed iterations
  (`while (await more())`); a body suspension in a bare-statement body had no
  cursor to restore the ledger. `js_interp_suspend_loop` is the single
  registration entry point for every loop form.
- Generators now use the same model. `ast_resumable_loop_active` is retired:
  it made loop resume *structural* and switched the yield ledger off for the
  whole activation, which is only sound for a terminal `yield expr;`. A yield
  whose value is consumed (`const v = yield x`) inside any loop re-suspended
  forever on the same value (`y0,y0,y0,…`), and a yield in a loop test or
  update replayed completed iterations (`t0,t1,t3`). Cursors now only *skip*
  completed work and hand the ledger its position; the ledger alone delivers
  the resume value, in both walkers. `ast_yield_skip` therefore advances on
  every yield, as `ast_await_skip` always did.
- A delegating `yield*` is never a terminal cursor. The shared generator
  runtime credits its ledger slot only once the delegate is exhausted
  (`gen->ast_yield_skip++` in `js_generator_next` /
  `js_generator_delegate_abrupt_call`), so the `yield*` node must be replayed
  to consume that slot; skipping past it let the next yield consume it and
  lose a loop iteration.
- `js_gen_return_signal` built its 2-element array on a bare native local and
  then called `js_get_gen_return_signal_marker()`, which allocates the marker
  on first use. A collection in that window freed the array before
  `items[1]` was written (SIGSEGV under `LAMBDA_GC_FORCE_EVERY`). It now roots
  value and array exactly as its `js_gen_throw_signal` twin already did
  (**D1.5**, **D5.3.2–D5.3.3**). Latent since the signal pair was introduced;
  the ledger change only shifted allocation counts enough to hit it.
- A concise-body async function (`async () => await p`) evaluated its body as
  an expression but discarded the normal completion value and returned
  `undefined`. The body-less arm now returns the completion value.

Verified against Node 22 on 29 async shapes (all loop forms, nested loops,
labels/break/continue, try/catch/finally, rejection, early return/throw,
switch, per-iteration closures, destructuring, async generators, timers,
concise bodies) and 15 generator shapes (consumed yield values in every loop
form, `yield*` in and out of loops, return/throw injection, labels, switch,
spread, per-iteration closures, early return); `test_js_gtest` (381),
`test_js_script_gtest` (107) and the 40,261-case Test262 baseline pass with
zero regressions; the 393-script `test/js` sweep and the 61-script DOM sweep
show no new AST-vs-MIR divergence.

## Suspended `finally` completions and suspension rooting (2026-09-09)

Two follow-ups from the same suspension model, one per walker plus the GC
lifetimes the second one exposed.

**A `finally` that suspends must not drop the completion it is holding.**
`it.return(v)` at a yield inside `try{…}finally{yield…}` finished the generator
with no value on both lanes, for different reasons:

- AST: the ledger stores the raw resume input, so an injected `return()`/
  `throw()` is still a signal when it is replayed — but only the live-resume
  and pending-injection paths decoded it. The replayed slot leaked the signal
  array as an ordinary yield value and the abrupt completion vanished. All four
  decode sites now share `js_interp_resume_completion`.
- MIR: `return_val_reg`/`has_return_reg` are plain registers that do not
  survive a state-machine return, so `jm_emit_try_state_reset` zeroed every
  enclosing try's delayed return on resume — including one legitimately waiting
  for a `finally` that yielded. They are now parked in generator-env spill
  slots by `jm_emit_try_state_save` (called from `jm_emit_suspend_env_save`, so
  no suspension site can forget it) and reloaded by
  `jm_emit_try_state_restore`. `jm_gen_spill_reserve` is a bounded reservation:
  when the fixed spill region is full the old zeroing still applies rather than
  writing past it.
- Preserving that value then exposed a second MIR defect, present for ordinary
  functions too: a try's `end_label` issued the delayed return directly, so
  `try{try{return v}finally{a}}finally{b}` never ran `b`. Its own context is
  already popped there, so it now offers the value to the enclosing context via
  `jm_emit_delayed_return_completion` first and only returns when none owns it.

**Rooting across a suspension.** With `LAMBDA_GC_FORCE_EVERY=1` every AST async
body silently stopped at its first `await` (exit 0, no output). Three bare
native locals held the only reference to a live object across an allocating
call (**D1.5**, **D5.3.2–D5.3.3**):

- `js_interp_async_state_result` allocated the `[value, next_state]` array
  before rooting `value` — the awaited promise — so the caller then registered
  its resume reactions on a collected promise. This was the root cause.
- `js_interp_start_async_function` held the frame carrier on a bare local
  across `js_async_get_promise` and `js_async_start`; nothing else references
  it until the reactions are installed.
- `js_async_drive` passed a freshly made native function into the allocating
  `js_bind_function`, so the bound callback could wrap a collected target.
- `js_gen_return_signal` (see above) belongs to the same family.

The 44-script async/generator probe set now matches Node 22 under
`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`, where previously every
async row produced no output at all.

## Try clause cursors (2026-09-09)

The cursor set was still one statement short. A statement list has a cursor and
a loop has one, but a try statement's block/catch/finalizer are three phases of
a single statement: a suspension in the catch or the finally replayed the whole
try, re-running the block's side effects — `try{L.push("t");throw 1}catch{yield}`
pushed twice, and `try{try{return v}finally{a}}finally{yield}` ran `a` twice.

`JsInterpTryContinuation` closes it, keyed by the try node like the loop cursor
and owned by whichever record the frame names:

- **FINALLY** carries the block/catch completion (kind, value and any
  break/continue label) that the finalizer was already holding, so re-entry
  runs the finalizer alone and then applies that completion.
- **CATCH** carries the thrown value *and* the handler's parameter record, so a
  closure created in the handler keeps its binding cell and the parameter is
  not re-bound over a value the body reassigned. `js_interp_exec_catch` takes
  that record instead of creating one.
- Both carry the ledger position of the clause, for the same reason the loop
  cursor does: the skipped block may have consumed yield/await slots.
- A suspension in the *block* still records nothing — replaying the try from
  the top re-enters the block, whose own statement cursor resumes it.

The pending-injection replay (`ast_pending_resume_yield`) used to drop every
nested statement cursor so an injected `throw()`/`return()` could be replayed
through its owning try. A try cursor now records which clause that injection
reached, so the drop is skipped whenever one exists; otherwise the reached
clause repeats its finished statements.

Verified on 17 further try/catch/finally shapes (await in catch/finally/both,
try inside an awaiting loop, return and rethrow through a suspended clause,
three-clause generators, break/continue/labelled-break through a yielding
finally, catch-parameter closure identity and catch-body `let` across a
suspension, nested catch, `for-of` with a try body, `throw()` into a suspended
catch, `return` overridden by a finally) — the AST lane matches Node 22 on all
of them, and on all 61 probes accumulated for this work, including under
`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`.

## MIR: finally inlining at break/continue (2026-09-09)

The AST work above surfaced two MIR defects in the same clause, both
pre-existing (verified identical on the base commit) and both silent.

**The resume-state budget did not count inlined finally bodies.**
`jm_emit_abrupt_jump_cleanup` inlines every unwound finally body at the
break/continue site, so one `yield`/`await` written inside a `finally` is
lowered once per such site *plus* once in the finally block itself. The budget
is fixed before the body is lowered, and `jm_next_resume_state` returns -1 once
it is exhausted — whereupon the yield lowering silently evaluates to its
operand and does not suspend. A generator dropped the yield (`y2` for
`y0,y1,y2`), and an async function's `await` produced the raw promise
(`[object Object]`). This is the third instance of the shape the two §9.3
comments already record, so `jm_count_finally_inline_yields` /
`..._awaits` now mirror the emitter's expansion — including re-inlining from a
break inside an inlined body — and are added to both budgets.

**A jump inlined finally clauses it does not leave.** The cleanup walked the
whole try-context stack regardless of where the jump landed, so `break` out of
a `switch` inside a try's block ran that try's finally and then ran it again
when the block completed normally, and `break` out of an inner loop ran every
enclosing try's finally. `JsTryContext::loop_depth_at_push` records the
break-target stack depth at entry, `jm_jump_target_index` resolves the target
before the cleanup runs, and only a try entered inside that target is unwound.

Verified on 14 further shapes (break/continue/labelled break out of loops,
switches and nested loops, three-deep nested try/finally, `for-of` and `while`
bodies, two suspensions per finalizer, async and async-generator variants).
Both lanes now match Node 22 on all 75 probes accumulated for this work.

## MIR await ordering (2026-09-09)

`await` on the MIR lane resumed synchronously: `js_async_must_suspend` answered
"no suspension needed" for an already-settled promise or a non-thenable, and
the lowering continued the body inline. The spec makes every `await` a
PromiseResolve plus a promise reaction job, so the rest of the async function
ran ahead of microtasks queued before it. `js_async_prepare_await` now always
PromiseResolves the operand and publishes it, and the lowering has no fast-path
branch. Verified against Node 22 on a 12-case microtask corpus: interleaved
await/`.then` chains, `await` in a loop, a suspending `finally`, two concurrent
async functions and awaiting a non-thenable all match now.

### What the fast path was hiding

The change broke `test/js/lib_floating_ui.js`, and isolating that is the
substance of this entry. The failing activation was the bundle's
`Fe = async function(t){ … return {reference: De(t.reference, await e(t.floating),
t.strategy), …} }` — an `await` in a call argument inside an object literal.
Reduced, the whole failure is:

```js
(async () => ({ r: await Promise.resolve("OP"), f: 1 }))()
// MIR: TypeError: Object.defineProperty called on non-object
```

The object under construction lives in a raw MIR register. `jm_emit_object_value`
decided whether to park it in an env slot by asking `jm_has_yield` — *does this
subtree contain a `yield`* — when the question a spill has to ask is *can this
subtree suspend*. Inside an async body an `await` returns from the state machine
exactly as a `yield` does. The two were interchangeable in practice only while
`await` had a fast path that usually did not suspend, so every such site was a
latent wrong-answer.

`jm_can_suspend` (`js_mir_analysis.cpp`) is now that question, and all 20 spill
decisions ask it. Three sites already spelled the disjunction out inline; they
collapse into it. Seven further defects of the same family surfaced with it:

- **`new C(1, await …)`** left the callee in a register across the arguments —
  the ordinary call path already spilled it (`jm_call_yield_blocks_direct`),
  `jm_emit_dynamic_new_expr` did not. Crashed on a garbage callee.
- **Tagged templates** spilled nothing at all: tag, receiver and argument buffer
  are plain registers held across the substitutions.
- **`for-in`** carries its key snapshot, length, cursor and source object in
  registers. `for-of` parks its iterator in the activation slot; `for-in` has no
  such home, so a suspending body resumed with a garbage cursor and stopped
  after the first key. The cursor is loop-carried, so it re-stores into one
  reserved slot per iteration (`jm_gen_spill_save_at`) rather than consuming a
  slot per back edge.
- **Array destructuring** gated its whole iterator-spill decision on
  `fc->node->is_generator`, which is false for an async function; `await` in a
  destructuring element got no spill whatsoever.
- **The resume-state prepass** gave the "an element of an array pattern resumes
  through both iterator-result branches" multiplier to `yield` only, so the
  `await` copies had no state left and silently did not suspend.

`lib_floating_ui` passes with the ordering fix in place, and a 35-case spill
corpus (object/array literals with computed keys, spreads, getters and
`__proto__`; call, `new` and spread arguments; templates and tagged templates;
member bases and assignment targets; destructuring; logical and nullish
operands; computed method, getter, setter, static and field keys with and
without `extends`; a `finally` lane save; nested combinations) matches Node on
both lanes.

Two further shapes were pre-existing — they fail identically with the fast path
restored once the awaited value is genuinely pending — and are now fixed too.
Neither turned out to be about `await` at all:

- **`const [a = 1] = []` in any state-machine function**, including a plain
  generator with no `await` anywhere, threw "Assignment to constant variable".
  An array element is lowered on *both* iterator branches (has-value and
  use-default); they are alternative runtime paths of one declaration, but
  `var->tdz_active` is compile-time state that the first emission consumed, so
  the second emitted an assignment — and `let` got a TDZ error on the path that
  never ran the initialization. `destructure_preserve_tdz` keeps the flag for
  the sibling branch and lets the last emission clear it, so const-reassignment
  and TDZ errors after the declaration still behave. The rest-element pair
  (collected-rest / `rest_skip`) had the same shape.
- **`class C { [await x](){} }`** exhausted the resume-state budget. A computed
  method key is an AST child of the method node, and `ast_index_node_is_function`
  counts that node as a function, so the owner filter attributed the key's
  suspension to the *method* — which never lowers it. The count now also accepts
  a suspension that structurally descends from the body with no function edge in
  between, and that walk passes through the single edge from an
  `AST_NODE_METHOD` to its own `key`. With the budget correct a derived class
  then crashed: `jm_emit_class_instance_setup_tail` holds the evaluated heritage
  in a raw register across method installation, where the class object and its
  prototype travel through the install policy and are restored but the
  superclass has no carrier.

### Promise tick accounting (2026-09-09)

Two defects made async results settle at the wrong microtask, both confirmed
against Node 22 and both now fixed.

**One promise per async call.** `js_invoke_fn_raw_or_async` minted a second
promise and joined it to the body's with a `.then`, costing every async call a
tick — `async function f(){ return "R" }` settled one job late. The wrapper now
returns the body's promise and creates one only on the synchronous-error path.
That works because every compiled async body publishes its own result promise:
a state machine returns its activation's, and an await-less body returns
`js_async_wrap_return`'s. That helper replaces the old `js_promise_resolve(v)`
wrap, which was wrong twice over — it handed back *the returned promise itself*
for `return somePromise` (so `f() === somePromise`), and it collapsed "settle
now" and "adopt later" into one already-settled promise. A fresh promise
resolved *with* the value keeps both timings. The native fast path returned the
body's value raw and relied on the wrapper to promisify it, so it wraps now too.

**Adoption follows PromiseResolveThenableJob.** `js_promise_adopt_native`
short-cut a returned native promise to a single enqueued handler, so an adopted
promise settled a tick early. A native promise is an object with a callable
`then`; ResolvePromise hands it to the thenable job like any other, which costs
one tick for the job and one for the reaction it registers. The shortcut is
gone (its cycle check moved to `js_promise_resolve_with_value`), and
`js_promise_async_function_finish` resolves rather than `.then`-joining.
`test/js/promise_jobs_j42_8.txt` moves one line: Node cannot run that fixture,
so its golden recorded our own timing — the line was re-verified against Node
in isolation before updating it.

34 of 36 microtask-ordering probes now match Node on both lanes; before this
work 8 rows were wrong on MIR.

### Still open

- **`Promise.all`/`allSettled`/`any` over an array containing a promise** is one
  tick late on both lanes. For array input the combinator allocates an
  `internal_result` promise and forwards it to the real capability, and that
  forwarding is the extra tick. It is deliberate — "without it aggregate
  reactions run before later combinators have published their own jobs" — so
  removing it needs the case that motivated it. `Promise.race`, and
  `Promise.all` over plain values on the MIR lane, are already correct.
- **`for await` over an async generator** is a tick out on MIR; the AST lane
  rejects `for await` outright.
(A third defect found here, a throw swallowed by a natively-typed body, is
fixed below.)

## The native ABI's throw channel (2026-09-09)

`function f(a){ if (a > 1) throw new Error("x"); return a * 2 }` returned `NaN`
for `f(2)` instead of throwing, and the same shape as an async function resolved
with `NaN` instead of rejecting. Only bodies that type inference compiles to a
**native** (unboxed) variant were affected, which is why an unconditional throw
or an arrow function behaved correctly.

The error ABI is entirely in-band: a fallible helper returns an ERROR-tagged
Item and the caller tests the tag. A native entry returns an unboxed `i64`/`d`,
so it has nowhere to put one. `jm_emit_throw_completion_impl` handled an
uncaught throw by *reinterpreting* the ERROR Item as the native return type —
`jm_native_return_reg` — which is literally where the `NaN` came from.

The native entry now has an explicit out-of-band channel, `js_native_throw_publish`
/ `js_native_throw_take` over a GC-rooted slot on `js_runtime_state`, registered
alongside the async scratch root. Taking clears it, so a stale lane cannot be
attributed to a later call. Three emission sites publish and two take:

- an uncaught `throw` in a native frame (`jm_emit_native_throw_exit`, replacing
  the reinterpret),
- a `finally` re-raising a saved lane with no enclosing handler — the generator
  case already returned the carrier, a native frame needs the same exit,
- the native function's own error landing pad, for implicit throws routed there;
- the boxed entry's native fast path, which returns the ERROR carrier or, for an
  async function, a rejected promise;
- a direct native call from another compiled body, which propagates it like any
  other lane.

The take is the only fallibility signal on those edges and the catalog cannot
infer that, so the lane state is declared `UNKNOWN` before the tag test —
otherwise `jm_emit_error_lane_test` folds to a constant "clean". `publish` is a
void helper and so must be cataloged `JIT_EXCEPTION_PRESERVES` (D8.4.3 tier B,
enforced by `make test-js-exception-catalog`): it parks a lane out of band and
never delivers a replacement carrier.

Verified on 16 shapes — int and float returns, nested native calls, a `try` that
catches inside the same native body, rethrow from a catch, `finally` on the
throwing path, implicit throws (`null.x`), a throw from inside a loop, repeated
calls, calls after a caught throw, and the async rejection variants — matching
Node on both lanes, including under `LAMBDA_GC_FORCE_EVERY=1
LAMBDA_GC_POISON_FREED=1`.
