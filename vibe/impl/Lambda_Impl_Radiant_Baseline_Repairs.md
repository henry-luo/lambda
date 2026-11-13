# Radiant baseline repairs — 2026-11-13

Implementation record for the reported sizing, lists and iframe-retirement
failures. Ownership and precise roots follow D9.1.3v2, D5.5.1–D5.5.2 and
D6.3.3. No formal ruling changes.

## Completion record

- Goal: restore recorded layout and UI baselines without changing scores,
  tolerances, references or the test inventory.
- Checkout: Lambda `master` at `e5d5e848e`; Radiant at `ea6ec14`, initially clean.
- Updated: 2026-11-13.
- The user explicitly approved overriding the sibling-project boundary after
  automatic approval review rejected the initial source edits.
- Lambda changes root the frames array during construction and publication.
- The forced-GC/poison iframe test and six selected neighboring UI fixtures pass.
- Forced-JIT iframe retirement passes. The final host's selected Radiant UI
  baseline passes 329/329 tests. Owner dynamic layout tests pass 87/87; final
  layout cast lint checks 89 casts with zero unmarked exceptions.
- Layout regressions: `svg-intrinsic-size-001`, `keyword-sizes-on-abspos`,
  `counter-set-002`. All reproduce in isolation with the original host.
- ASan qualification is blocked by LR01-25, a separate host-link configuration
  defect. The normal debug build succeeds.
- RI-057 covers the native fixes. All temporary source diagnostics are removed.
- The final selected layout gate passes: sizing 776/776; lists 52 passing and
  eight recorded partial results; no regressions in any selected category.
- `make test-radiant-baseline` exits 0: **8,165 passed, 299 recorded partial,
  20 skipped, zero failures** out of 8,484 cases. This includes all requested
  layout categories and the 329/329 UI gate on the final debug host.
- The extra Lambda baseline was stopped after an isolated `proc_sync_rtc`
  replay confirmed E405 (optional WebRTC dependency unavailable). Its remaining
  golden sweep is incomplete; no full Lambda baseline pass is claimed.
- Both working-tree diffs pass whitespace checks. No source diagnostics, scored
  baseline updates, reference edits or tolerance changes remain. Requested work
  is complete; LR01-25 and the optional WebRTC setup are separate limitations.

## Iframe collection lifetime — LR12-54

`dom_collect_frame_windows_array` held its result only in a native local while
`dom_iframe_get_content_window` allocated child document wrappers. Forced GC
collected the array before it reached the global `frames` property. The crash
trace reaches `shape_entry_set_type` through `dom_install_window_frames_global`
and `js_set_key_cstr`. Precise `RootFrame` / `Rooted<Item>` ownership now covers
the producer and publication boundary, as required by D6.3.3.

The regression is the existing `test/ui/dom_iframe_retire.json`, which enables
`LAMBDA_GC_FORCE_EVERY=1` and `LAMBDA_GC_POISON_FREED=1`. Before the fix it
reported SIGSEGV and 4/6 assertions. After the fix it reports 5/5 assertions;
the extra failed assertion was caused by retrying the initial frame switch.
The retirement callback, DOM replacement and window shutdown now execute.

Verification with the rebuilt debug host at the commit above plus this patch:

- `make build`: passed.
- UI selector `dom_iframe_retire` in suite `dom`: passed.
- UI selector `dom_iframe_*,dom_fetch_host_turn_realm,dom_inline_event_attribute_realm`
  in suite `dom`: six fixtures passed, plus three scheduler tests.
- `LAMBDA_EXEC_BACKEND=jit` iframe retirement replay: passed under forced GC/poison.
- Exact selected UI gate (`node test/test_run.js --target=radiant
  --category=baseline --radiant-baseline --lambda-owned
  --include-test=test_ui_automation_gtest`): 329/329 passed with native access.
- An earlier expanded sandboxed UI replay selected 1,019 fixtures, reached
  unrelated media failures, and timed out on `template_task_reconcile`.
  It does not qualify the full UI inventory. The selected baseline above is
  the requested 326 fixtures plus three scheduler tests, without inventory edits.
- Evidence under `temp/failed-radiant-tests/`: `iframe-stack-before.log`,
  `iframe-after.log`, `iframe-neighbors.log`, `iframe-fix-build.log`,
  `iframe-jit.log`, `ui-selected-baseline.log`, `ui-expanded-output.log`.

## Native sizing and snapshot repairs — RI-057

- SVG: an explicit viewport height was measured correctly, then treated as an
  automatic cross size because its natural attribute had no numeric BlockProp
  slot. The shared automatic-size predicate now recognizes SVG dimension hints
  when CSS does not override them. Flex cross sizing uses the existing explicit
  size helper; column cross measurements publish their size before alignment.
  Late line stretching reuses the normal semantic stretch predicate instead of
  repeating a numeric-height check. A native regression covers the original
  40x40 viewport with a 4:1 viewBox, a taller peer, column flex, CSS auto and an
  attribute mutation during retained reflow.
- Positioned intrinsic sizing: the parent query recognized intrinsic preferred
  sizes but omitted intrinsic minimum/maximum keywords. Child percentages then
  resolved against a provisional 100px containing width. One predicate now
  covers preferred/min/max keywords in the contribution, cyclic-basis/cache and
  ratio-transfer paths. A data-driven native regression checks all six min/max
  canvas combinations against literal zero/20px contributions.
- Lists: geometry in `counter-set-002` already matched. The snapshot confused a
  suppressed layout role with computed `display:none`, shifting comparison child
  paths. Unrendered snapshots now use the existing computed-display serializer,
  preserving `noscript`'s computed display and zero geometry.

The final host is a debug-profile split build of Lambda `e5d5e848e` and Radiant
`ea6ec14` plus these uncommitted changes. Test-only fixture deduplication after
the host build does not alter the engine; the final owner test binary passes
87/87. Evidence: `native-dynamic-final.log`, `radiant-lint.log`,
`radiant-baseline.log` and the shared runner's `test_output/` artifacts.

## Qualification limitation — LR01-25

`make build-debug-asan` builds instrumented sibling archives but fails linking
the host with unresolved `___asan_*` symbols. Its generated compile/link flags
omit `-fsanitize=address`; the configuration/generator cause is unconfirmed.
The investigation used a temporary native crash trace in the normal debug host;
that instrumentation has been removed. Evidence: `asan-build.log` in the task
temporary directory. No sanitizer pass is claimed; this separate build defect
is not worked around.
