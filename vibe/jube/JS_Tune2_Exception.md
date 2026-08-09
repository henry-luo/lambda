# JS Tune2 Exception — Implementation Record

**Date**: 2026-08-09  
**Status**: IMPLEMENTED — final validation passed  
**Tree anchor**: master `0ed462fe3` plus this uncommitted Tune2 change set  
**Design authority**: **D8.4.3** (merged `Item` error ABI), **S7.4.4**
(errors are first-class and deliberate), and **D6.1.3** (unknown effects are
fail-closed). This is conformance work against those rulings; no formal-spec
revision is required.

Related records: `JS_Tune1_Runtime.md`, `JS_Tune1_Helpers.md`, and
`JS_Runtime_Redesign.md` (JR3).

## 1. Outcome

Tune1 removed the ambient exception channel, but it left one representation
mistake: `jm_publish_call_result` treated every `MIR_T_I64` as a boxed `Item`.
An I64 also represents raw `bool` and `int64_t`, so comparisons and predicates
were needlessly published into D8.4.3's error lane and followed by a
provably-dead ERROR-tag branch.

Tune2 fixes the root cause at that publication gate. The gate now consults the
callee's catalog return class: only `JIT_VALUE_BOXED_ITEM` is admitted to the
lane. Unknown calls retain the old I64 fallback, deliberately fail-closed.
The fix-point comment in `js_mir_internal.hpp` records that invariant.

The catalog is now explicit for every affected class:

| class | before | after | action |
|---|---:|---:|---|
| raw scalar imports without `PRESERVES` | 37 registry entries / 36 distinct C targets | 0 | declared scalar and `PRESERVES`; do not publish result |
| void imports claiming `MAY_SET` | 85 | 0 | declared `PRESERVES`; a void import has no `Item` carrier |
| remaining `MAY_SET` imports | 372 | 372 | retained fail-closed because they return an `Item` carrier or lack proof of infallibility |

The entry-count/target-count distinction is intentional: the static checker
resolves `FPTR(...)` aliases before classifying a row, so an alias cannot create
a spurious scalar violation.

## 2. Decisions and completed phases

### X0 — instrumentation and baseline hygiene — complete

- `utils/js_exception_catalog_census.py` now resolves the actual `FPTR`
  target, understands the shared scalar/void contracts, and fails on all three
  D8.4.3 violation classes.
- `utils/analyze_js_mir.py` now reports emitted ERROR-tag tests instead of the
  retired ambient-poll metric.
- `make test-js-exception-catalog` is a standing gate and is a prerequisite of
  `make test262-baseline`.

### X1 — scalar publication conformance — complete

`jm_publish_call_result` accepts the helper name from every `jm_call_*` macro
and checks its catalog `ret_class`. A raw scalar therefore cannot overwrite the
last boxed error carrier. This directly implements **D8.4.3** rather than
trying to peephole-delete the resulting branch.

`PRESERVES` is now also implemented as a carrier-lifetime contract, not merely
as a publication classification. A collecting preserving helper roots the
existing in-band boxed carrier in its exact side-root slot before the call. A
non-collecting scalar-result helper copies that carrier into a distinct MIR
register before the call, because the native call destination may otherwise
alias the register later tested for `ERROR`. Void helpers have no destination
to copy, and preserving no-GC void calls keep the existing carrier directly.
Only asynchronous resume joins clear the remembered carrier. An ordinary
label can be the explicit target of an iterator/destructuring cleanup rethrow,
so it must retain that carrier; clearing it would turn a real `ERROR` Item
into clean control flow. These narrow cases retain the true catalog effect and
protect **D8.4.3** without inventing a generic transparent-call classification.

The final Test262 pass caught and closed this control-flow boundary. A trial
that invalidated every ordinary label caused 715 iterator/destructuring cases
to report that an expected exception was not thrown. The focused failing
repro and the complete 40,261-case gate both pass with invalidation limited to
the asynchronous state-machine join, where the prior carrier truly does not
dominate.

`test/mir/js/error_lane_raw_scalar.js` and its `.mir-check` prove that raw
equality/logical calls remain emitted while their `eq ..., 27` ERROR-tag test
does not. The test is intentionally an emission assertion, not a behaviour
golden: the deleted condition is unreachable for a raw result.

### X2 — pass-through transparency — measured null, no change

This phase was investigated rather than guessed. `lambda_item_adopt_scalar_home`
is inserted by the shared MIR emitter after a fallible call; its ERROR-tag test
tests the prior returned `Item`, not the adoption helper. `js_debug_check_callee`
has its result ignored. A generic transparent effect would either preserve no
useful state or risk detaching a real carrier.

No transparent contract landed, no D8.4.3 revision is needed, and no artificial
optimization was retained. This is the planned null-result path, not an
unfinished implementation item.

### X3 — catalog tightening — complete at the safe boundary

The full `js_*` catalog was classified by result transport rather than by an
unsafe bulk assertion. All 85 void imports are now explicitly `PRESERVES`;
they have no replacement `Item` error carrier, so the emitter retains the
preceding carrier according to X1. The scalar imports receive the same
explicit `PRESERVES` contract and are kept outside the lane by X1.

All remaining value-carrying/import-unknown rows deliberately remain
`MAY_SET`. **D6.1.3** requires this conservative outcome until an individual
body and its reachable calls prove otherwise. Tune2 makes the two mechanically
detectable D8.4.3 contract violations impossible without claiming that a
fallible `Item` helper is clean.

### X4 — standing regression gates — complete

- The catalog lint is part of the Test262 baseline prerequisite chain.
- `test_mir_ratchet_gtest` gained `error_lane_tag_tests`; the fixed dynamic
  catch probe has an exact budget of one ERROR-tag test.
- A focused emission fixture guards raw-scalar publication independently of
  the ratchet.

### X5 — historical record corrections — complete

The Tune1 runtime/helper notes and the runtime redesign document now state
that P5 did not land in Tune1, distinguish eliminated poll calls from emitted
ERROR-tag tests, and identify Tune2 as the owner of the catalog gate.

## 3. Reproducible checks

```bash
python3 utils/js_exception_catalog_census.py --prefix js_ --violations
./test/test_js_mir_emission_gtest.exe
./test/test_mir_ratchet_gtest.exe
make test-jube-node-error-lane
LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 \
  ./lambda.exe js test/node/js_tune1_error_lane.js --no-log
make test-lambda-baseline
make test262-baseline
```

The lint's required final output is:

```text
D8.4.3 tier A -- raw-scalar helper lacking a PRESERVES contract: 0
D8.4.3 tier B -- void helper whose row still claims MAY_SET: 0
D8.4.3 tier C -- boxed-Item helper cataloged as a raw scalar: 0
```

## 4. Validation record

| check | result |
|---|---|
| Python tool compilation + catalog lint | pass; tier A 0, tier B 0, tier C 0; focused lint rerun also passed |
| focused raw-scalar MIR emission fixture | pass, 1/1; `js_eq_raw` and `js_logical_not` remain emitted while the raw-scalar function has no `eq ..., 27` ERROR-tag test |
| focused dynamic-catch ERROR-tag ratchet | pass, 1/1; guards the known fallible path's recorded ERROR-tag-test upper bound |
| full JS MIR emission suite | pass, 14/14 |
| full MIR ratchet suite | pass, 16/16; includes the error-lane ratchet |
| Moment regression suite | pass, 131/131 |
| error-lane target + explicit forced-GC/poison run | pass |
| focused Test262 exception repro | pass, 4/4 across typed-array, arrow, iterator, and try/destructuring cases |
| focused DOM golden | pass, 1/1 for `JavaScriptTests/JsFileTest.Run/dom_module_props`; template content is correctly a detached `DocumentFragment` |
| `make test-lambda-baseline` | pass, 3,668/3,668; includes the corrected `dom_module_props` template-content golden |
| `make test262-baseline` | pass, 40,261/40,261; 0 non-fully-passing, 0 failed, 0 regressions, retry 0.0s |
| `git diff --check` | pass after the DOM-golden and Tune2-ledger updates |

The Lambda baseline is clean. `dom_module_props` now records that a template's
`content` is its detached `DocumentFragment`, not the template wrapper itself.
The focused emission fixture demonstrates the reduction precisely: it retains
the raw helper calls but rejects their unreachable ERROR-tag comparison. The
dynamic-catch ratchet and the complete Test262 gate ensure this does not erase
a genuine exception path. The JavaScript exception, MIR-ratchet, and
forced-GC stress coverage is clean. The emitted scalar branch removed by X1
was constant-false; the new checks additionally cover preservation of a real
pre-existing carrier across preserving calls.

## 5. Release A/B limitation

An exact pre-Tune2 detached worktree at `0ed462fe3` was prepared for the
required release emission/performance comparison. Its clean release rebuild
reaches an unrelated host ThorVG C API incompatibility in
`radiant/lottie_player.cpp` (the installed `thorvg_capi.h` exposes opaque
handles where that source expects complete types). The current worktree can
use its existing release build, but an equivalent clean pre-change binary
cannot be produced on this host; therefore no misleading release A/B number is
claimed.

This does not affect the implementation claim: X1 removes only an impossible
ERROR-tag test after raw scalar results, and the focused emission, ratchet,
runtime, and GC checks above cover the changed path. A release A/B can be run
unchanged on a host with the project-compatible ThorVG headers using
`utils/js_error_lane_census.py emit --bin ...`.

## 6. Closed scope

Tune2 is complete. A future optimization of genuinely fallible helpers such as
`js_call_function_prerooted_args_into` would require reducing real call sites,
not weakening their D8.4.3 error-lane checks; that is separate runtime work.
