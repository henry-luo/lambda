# JS interpreter alignment with formal MVP

**Date:** 2026-10-18  
**Authority:** **S1.11**, **D1.3v3**, **D5.2–D5.3**, **D6.2.2v2**, **D8.1.3v26**, **D8.2.3**.  
**Status:** direct-eval and receiver-ownership migration implemented and behavior-qualified. The aggregate Test262 run retains eight timing partials. Full AST/native realm replacement is not claimed.

## Audit

The retained walker is partly aligned already. It is neither the retired MIR
compiler nor a separate Lambda heap. The operation profiles remain distinct.

| Area | Live implementation | Finding |
|---|---|---|
| Values and lexical storage | `js_interp_env.h`, `runtime/gc_environment.h`, `owned_item_slot_*` | Lambda `Item`, durable slots, numeric ownership and GC tracing are shared. JS lexical metadata remains language-specific under S1.11. |
| Runtime ownership | `js_interp_execute_script`, `RuntimeExecutionScope`, `RuntimeModuleStateScope`, `RootFrame` | Same Runtime, EvalContext, heap, module-state and event-loop infrastructure as Lambda. |
| Formal MVP dynamic source | `mvp_lmd_dynamic.cpp`, `JsInterpHostOps`, `js_interp_execute_host_eval` | MVP supplies its property, call, class, array and error operations to the existing walker. Direct eval already receives live `JsInterpEnv` cells. |
| Explicit AST/AUTO | `js_interp.cpp`, native `js_*` kernels | Still uses native JS globals, object metadata, arguments and callable capabilities. These have active DOM/host/module consumers and cannot be deleted as unreachable legacy code. |
| Promotion | `mvp_lmd_compile_function_satellite`, `mvp_lmd_compile_loop_satellite` | Already targets formal MVP. Ordinary dynamic source remains interpreted. Native prototype/callable compatibility is still used by AUTO satellites. |
| Retired direct-eval bridge | Former `JsEvalBindingJournal`, `JsEvalLocalState`, bridged interpreter branches | Consumers survived after all frame/binding producers were removed with the old compiler. No live caller supplies the old direct-eval flags. Removed in this migration. |

The native profile does not justify copying Lambda's language operations:
JavaScript coercion, equality, reference mutation, TDZ and completion semantics
remain JavaScript's (**S1.11**, **D1.3v3**). The appropriate reuse boundary is
physical storage/ownership and shared frontend facts, with explicit JS semantic
operations.

## Changes

1. Retire the old MIR binding projection, lexical projection and activation-var
   journals, their eight root-vector lanes, their compatibility accessors and
   interpreter branches, the bridged direct-eval source entry, the private-name
   snapshot needed only by that entry, the constant-write text scanner, and
   five unused generated-import entries.
   `js_builtin_eval(Item)` is now explicitly indirect. Direct callers enter
   with live environment cells, including MVP's `JsInterpHostCaller`.
2. Share eval context validation through `js_ast_collect_function_facts`.
   Native direct/indirect eval now uses the same facts as MVP-hosted eval;
   eval always parses with Script grammar. Remove the old class-initializer
   text scanner. The retained private-name parse scope remains necessary for
   early errors; it stores identities, not copies of lexical values.
3. Give `JsInterpEnv` an owned scalar home for `this`, using
   `lambda_item_adopt_scalar_home`. Ordinary calls, suspended calls and
   per-iteration environment clones use that same storage. This replaces
   the hosted path's per-call pool allocation and the native path's raw
   receiver copy. The existing six retained Item fields remain contiguous
   for common GC tracing; the scalar home is outside that traced span.
   Advance executable module-cache compiler ABI 3 to 4 because generated
   environment slot offsets change.
4. Update the formal design and interpreter design together. No ECMAScript
   coercion or completion rule changes; this implements the established
   guest-semantics and precise-ownership contracts.

## Reproduction and verification

The archived pre-change executable is
`temp/js_interp_mvp_alignment/before_lambda.exe`. The source mismatch is:

```js
class C { value = eval('({arguments: 42}).arguments'); }
console.log(new C().value);
```

Forced AST reports `SyntaxError: Invalid eval in class field initializer`;
formal MVP prints `42`. The text scanner incorrectly treats a property name as
an `arguments` reference. The shared AST fact visitor already excludes property
names and nested ordinary-function bodies while visiting template substitutions
and escaped identifiers.

The pre-change native runtime/script suite passed **210/210**. New tests exercise
AST, AUTO and formal MVP for eval context, escaped eval-created bindings and
constant writes, and numeric receivers retained by closures across collection.
Qualification on 2026-10-19:

| Check | Result | Evidence under `temp/js_interp_mvp_alignment/` |
|---|---|---|
| `make build-test` | passed | `build.log` |
| `make build-release-compile` | passed | `release_build.log` |
| Original mismatch on the pinned release executable | AST, AUTO and MIR all print `42` | `reproduction_after.json` |
| Three new regression tests | 3/3; 33 backend/source executions | `focused.log` |
| Native JS runtime/script suite | 213/213 | `script.log` |
| Formal MVP suite | 825/825 | `mvp.log` |
| Eval/receiver/private-closure tests with forced GC and freed-memory poisoning | 18/18 | `forced_gc.log` |
| `make test-lambda-baseline` | 7,892/7,892: 2,112 input + 5,780 runtime | `lambda_baseline_python313.log` |
| `make test262-baseline` | exit 0; 40,253/40,261 fully passing, eight timing partials, zero semantic failures/regressions | `test262_baseline.log` |
| Eight timing-partial RegExp cases, isolated paired batch | 8/8 before and after; 18.8 s before, 18.7 s after | `regexp_before.log`, `regexp_after.log` |
| Retired APIs | all 12 absent from production sources and linked exports | `retirement_check.json` |

The first aggregate run passed 7,891/7,892; its sole failure was the PDF output
validator importing Pillow from a Python 3.14 environment without that package.
The installed Python 3.13/Pillow environment passed the error-system suite
269/269 and then the entire aggregate gate. The command-local environment was
`PATH=/opt/homebrew/opt/python@3.13/bin:$PATH`; no source, golden, timeout or
baseline allowance was changed for this failure.

Test262 uses `LAMBDA_TEST262_EXE` to select the private
`validated_release.exe`; `release_provenance.json` records its SHA-256 and the
production diff hash. Release executables are stripped, so source absence is
the proof that the old APIs were retired, rather than a symbol-count comparison.

The standard Test262 gate is mixed AST/MVP: 34,334 AST and 5,895 MIR assignments
plus 32 MIR-native cases. Its eight Unicode property-escape RegExp cases passed
semantically but took 3.23–4.74 s against the existing 3 s threshold in the
seven-worker run. All eight passed the runner's individual retries. A subsequent
single-worker batch of the same cases passed on both archived pre-change and
new release executables with essentially equal elapsed time. This diagnostic
does not establish a fully clean aggregate timing gate; the eight partials
remain recorded. No Test262 harness, allowlist, timeout or timing threshold was
modified. Original diagnostics are preserved beside the logs.

`make formal-index` regenerated the ruling index, with the existing duplicate
DO25 warning. `git diff --check` passes. Production C/C++ changes remove a net
655 lines. This is correctness/ownership qualification, not a performance or
cross-platform result.

## Remaining migration boundary

Explicit AST/AUTO still has a native JS realm and callable profile. This pass
retires the defunct compiler-specific eval protocol; it does not remove all
native `js_*` support or claim a single object representation across both
profiles. Live entry points include `js_execution.cpp`'s classic, CommonJS and
ES-module dispatch; `js_event_loop.cpp`'s string timers;
`lambda/dom/realm/dom_realm.cpp`'s callback adapter; and the function/loop
satellites in `js_interp.cpp`. A complete profile migration must jointly cover:

- retained classic Scripts, harness reuse and global lexical declarations;
- static/dynamic modules, CommonJS, live exports and Lambda imports;
- direct native and DOM callbacks, function identity and prototype mutation;
- arguments aliasing, class/private state, async/generator reentry;
- AUTO call promotion and loop handoff using the same realm identities.

Changing only the root executor to the MVP dynamic-source profile would be
incorrect: its admission excludes static imports/exports and CommonJS, it
wraps AST callables, and it disables ordinary AUTO promotion. Those differences
need migration of their actual owners and call sites, followed by the same
behavioral gates. Removing the native profile before that work would drop live
features rather than retire obsolete compiler support.
