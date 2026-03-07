---
description: "Analyze CSS 2.1 layout test failures, categorize by root cause, and identify next improvement areas"
agent: "agent"
---

# Analyze CSS 2.1 Test Suite Results

Run and analyze the CSS 2.1 conformance test suite for the Radiant layout engine.

## Steps

1. **Build & Run**: Build the project (`make build`) and run `make layout suite=css2.1 2>&1 | tee temp/css21_results_rN.txt` (increment N from previous runs in temp/).

2. **Categorize Failures**: Run `python3 temp/analyze_css21_v2.py` (update its `RESULTS_FILE` to the new results file). This produces:
   - Mega category breakdown (TABLE, INLINE/VALIGN, BIDI, etc.)
   - Detailed category breakdown (table-anonymous, c-tests, bidi, etc.)
   - Ranked improvement areas by failure count

3. **Compare with Previous Run**: If a previous result file exists, compare to identify regressions and improvements. Look for newly-failing tests (regressions) first.

4. **Deep-Dive Top Failures**: For the top 5 failure categories by count, examine:
   - 2-3 representative test HTML files in `test/layout/data/css2.1/`
   - Their reference JSON in `test/layout/reference/`
   - Their output JSON in `temp/`
   - Identify: what CSS feature is tested, what differs (elements/spans/text), root cause hypothesis

5. **Report**: Produce a prioritized list of improvement areas with:
   - Failure count and pass rate
   - Root cause (fundamental missing feature vs edge case bug)
   - CSS spec section reference
   - Estimated fix scope (localized vs systemic)

## Principles

- **Do not fix without root cause** — it's OK to leave tests as failed
- **No hard coding or workarounds** — conform to CSS spec
- **Structured design** — understand the spec algorithm before implementing
- Refer to [Radiant_Layout_Design.md](../../doc/Radiant_Layout_Design.md) for engine architecture
- Key source files: `radiant/layout_table.cpp`, `radiant/layout_inline.cpp`, `radiant/layout_block.cpp`, `radiant/dom_build.cpp`

## Current Baseline (r8/r9, March 2026)

- **Total: 9888 | Pass: 8811 | Fail: 1077 | Rate: 89.1%**
