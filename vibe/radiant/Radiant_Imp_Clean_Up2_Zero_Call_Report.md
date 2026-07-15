# Radiant Clean-Up 2 — Reviewed Zero-Call Report

**Review date:** 2026-07-15  
**Scanner:** `utils/lint/dead-code/run_unused_function.sh`

## Gate result

The repaired scanner uses a C/C++-only ast-grep configuration, treats worker
failure and an empty definition inventory as errors, counts declarations as API
sites, and includes `test/` when counting references. Its fixture verifies that
a declared-and-defined dead external function is reported while a declared,
defined, and called external function is not.

After the Phase 1 deletions, the only reported Radiant functions were three
members of the dormant WebDriver subsystem: `element_registry_is_stale`,
`webdriver_element_send_keys`, and `webdriver_perform_actions`. Phase 3 retired
that subsystem completely. The final repository-wide scan reports no Radiant
candidate.

## Deleted domains

- layout and intrinsic measurement compatibility APIs, including flex
  measurement scaffolding, obsolete axis wrappers, float positioning, grid
  helpers, and form intrinsic wrappers;
- event, state, editing, and selection compatibility APIs, including the two
  projection-to-DOM reverse-sync functions;
- render, shell, ownership, font diagnostic wrappers, legacy layout entry
  points, and superseded webview/vector helpers; and
- unused DOM Selection aliases whose JavaScript-facing implementations already
  use the canonical range primitives directly.

The two event resolver registration functions remain intentionally. Each is
marked `UNUSED_FUNCTION_OK` because a process constructor registers it without
an ordinary C++ call site.

## Final verification

- dead-code fixture and repository-wide scanner: pass; no Radiant candidate;
- debug and release builds: pass;
- release WebDriver and human-dumper symbols/representative strings: absent;
- CoreGraphics source: retained; both build-config exclusions present; no
  generated makefile reference;
- layout baseline: 4,351 exact passes and 11 accepted tolerance cases;
- editor suite: 1,931/1,931 pass; DOM Range suite: 70/70 pass;
- Selection WPT: 83 pass, 62 skipped, and 14 known/unsupported failures;
- aggregate Radiant run: 6,096 pass, 358 skipped, and 9 pre-existing UI TODO
  failures (the aggregate target exits nonzero for those known failures);
- Radiant integer-cast lint: pass;
- full lint reaches the pre-existing structural-header ratchet failure for
  `resource_resolver.hpp`; warning inventories are unrelated to this campaign;
- `git diff --check` and modified JSON validation: pass; and
- final production delta under `lambda/` and `radiant/`: 623 additions and
  4,768 deletions, net -4,145 LOC.
