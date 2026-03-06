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

### Top Failure Categories (r8)

| # | Category | Fail | Total | Pass% | Root Cause |
|---|----------|------|-------|-------|------------|
| 1 | c-tests | 113 | 262 | 56.9% | Mixed: inline box model (~31), float (~23), font metrics (~13), vertical-align (5) |
| 2 | table-anonymous | 73 | 230 | 68.3% | Anonymous box generation edge cases (CSS 2.1 §17.2.1) |
| 3 | bidi | 56 | 113 | 50.4% | Incomplete bidi reordering implementation |
| 4 | first-letter-punct | 53 | 411 | 87.1% | Punctuation inclusion in ::first-letter selection |
| 5 | block-in-inline | 52 | 131 | 60.3% | Inline splitting around block children (CSS 2.1 §9.2.1.1) |
| 6 | first-letter-punct-before | 33 | 33 | 0.0% | Text nodes empty in output (text emission bug) |
| 7 | direction-bidi | 28 | 28 | 0.0% | Fundamental: RTL text measurement, line box width, vertical offset |
| 8 | run-in | 28 | 141 | 80.1% | Run-in box edge cases |
| 9 | text-transform | 24 | 54 | 55.6% | Bicameral text-transform (locale-aware casing) |
| 10 | floats | 22 | 86 | 74.4% | Float wrapping, clearance, shrink-to-fit edge cases |
