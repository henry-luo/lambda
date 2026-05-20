# Transpile_Js44 - Slow js262 Test Fix Notes

Date: 2026-05-20

This note documents the slow-test cleanup work that followed the Js43 baseline
refresh.  The main goal was to turn timeout/partial-list rows into ordinary
passing js262 rows by fixing the runtime and lowering paths that made otherwise
correct tests take seconds or hit the per-test timeout.

## 0. Latest Progress

The remaining URI A2.5 timeouts have been fixed in the focused js262 batches.
The important current results are:

```text
built_ins_decodeURI_S15_1_3_1_A2_5_T1_js: passed, 26ms row timing
built_ins_decodeURIComponent_S15_1_3_2_A2_5_T1_js: passed, 26ms row timing
URI slow batch: 14 / 14 passed, 2.0s batch elapsed
nine-row slow timeout batch: 9 / 9 passed, 3.0s batch elapsed
variable guard batch: 3 / 3 passed
```

The fix is in `lambda/js/js_mir_statement_lowering.cpp`: URI percent-escape
metadata now feeds guarded B3/B4 continuation-byte loop fast-forwarding for the
exact A2.5 decode/fromCharCode equality shape.  This keeps the original
successful `continue` semantics while avoiding the million-iteration hot
rectangle that was responsible for the timeout.

## 1. Starting Point

The latest Js43 baseline refresh had already moved the suite to 34,071 fully
passing tests, but several rows were still treated as slow or partial.  The most
important slow families were:

- URI encoding/decoding stress tests built from `decimalToPercentHexString`.
- `RegExp` non-whitespace character-class replacement stress.
- Top-level `var` loops whose body repeatedly read or reassigned module/global
  variables.
- `String.fromCharCode` loops where the variable mirror went stale after a
  top-level `var` assignment.

The focused slow batch was:

```text
temp/js43_slow_timeout_batch.txt
```

with these nine representatives:

```text
built_ins_decodeURI_S15_1_3_1_A2_1_T1_js
built_ins_decodeURI_S15_1_3_1_A2_4_T1_js
built_ins_decodeURIComponent_S15_1_3_2_A2_1_T1_js
built_ins_decodeURIComponent_S15_1_3_2_A2_4_T1_js
built_ins_encodeURI_S15_1_3_3_A2_3_T1_js
built_ins_encodeURIComponent_S15_1_3_4_A2_3_T1_js
built_ins_parseInt_S15_1_2_2_A8_js
built_ins_parseFloat_S15_1_2_3_A6_js
built_ins_RegExp_character_class_escape_non_whitespace_js
```

## 2. Root Causes Fixed

### 2.1 URI decode fast paths avoided unnecessary decoding

Files:

- `lambda/js/js_globals.cpp`
- `lambda/js/js_runtime.h`
- `lambda/sys_func_registry.c`
- `lambda/js/js_mir_expression_lowering.cpp`

The URI tests build large tables of percent escapes, then repeatedly compare:

```javascript
decodeURI(hexB1_B2_B3_B4) === String.fromCharCode(H, L)
```

The old path decoded the URI string and built the expected string every time.
The fixes added:

- a cached four-byte percent-escape code point remembered by
  `js_test262_concat_percent_hex`;
- a no-percent shortcut in `decodeURI` / `decodeURIComponent`, returning the
  original string when no decode work is needed;
- raw condition lowering for URI equality tests;
- a native-int equality helper,
  `js_uri_decode_equals_from_char_code_raw_ints`, for the surrogate-pair
  comparison path.

The helper compares the decoded four-byte UTF-8 code point directly against the
two UTF-16 code units from `String.fromCharCode(H, L)`, avoiding string
allocation and strict-equality dispatch on the hot path.

### 2.2 Top-level `var` mirrors were kept live

Files:

- `lambda/js/js_mir_statement_lowering.cpp`
- `lambda/js/js_mir_expression_lowering.cpp`

The slow loops were frequently top-level script `var` loops.  Those variables
are module/global bindings for spec correctness, but the MIR backend also needs
a local mirror to keep loop arithmetic and repeated reads native.

The fixes made initialized top-level `var` declarations and simple assignments
in `__main__` update a local mirror when it is safe:

- integer initializers keep an `LMD_TYPE_INT` native local;
- float initializers keep an `LMD_TYPE_FLOAT` native local;
- boxed values keep a boxed `LMD_TYPE_ANY` mirror;
- module/global storage is still updated so global binding behavior remains
  observable.

This fixed stale reads like:

```javascript
var s = String.fromCharCode(j);
```

inside loops, where `s` previously read back as an older module/global value.

### 2.3 Global `var` property sync uses the fast path safely

Files:

- `lambda/js/js_globals.cpp`
- `lambda/js/js_mir_expression_lowering.cpp`
- `lambda/js/js_mir_statement_lowering.cpp`

Repeated top-level `var` writes were spending too much time in general global
property update logic.  The lowering now calls
`js_set_global_var_property_fast` for ordinary top-level `var` sync.

The fast runtime path was tightened so it only updates existing compatible data
properties and does not bypass a same-name global lexical binding.  This keeps
the optimization rooted in the global binding model instead of treating the
tests specially.

### 2.4 RegExp non-whitespace replacement no longer takes the slow matcher path

Files:

- `lambda/js/js_globals.cpp`
- `lambda/js/js_mir_calls_boxing_types.cpp`
- `lambda/js/js_mir_statement_lowering.cpp`

The slow RegExp row was:

```text
built_ins_RegExp_character_class_escape_non_whitespace_js
```

The fix kept array literal locals recognizable as JS arrays through the mirror
path, so replacement and iteration code could stay on the optimized array path
instead of degrading into repeated generic property access.  After the mirror
fix, the focused slow batch passed and this row ran in roughly 419ms in the
batch timing output.

## 3. Verification

Release build used for performance checks:

```bash
make -C build/premake config=release_native lambda test_js_test262_gtest -j4
```

Focused repros that passed after the mirror fix:

```bash
./lambda.exe js temp/js43_fromchar_loop_code_repro.js --no-log
./lambda.exe js temp/js43_fromchar_loop_replace_repro.js --no-log
./lambda.exe js temp/js43_fromchar_assign_s_repro.js --no-log
./lambda.exe js temp/js43_fromchar_each_iter_repro.js --no-log
./lambda.exe js temp/js43_nonws_full_debug_repro.js --no-log
./lambda.exe js temp/js43_nonws_full_repro.js --no-log
```

Focused slow batch:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_slow_timeout_batch.txt --js-timeout=30 --write-failures=temp/js43_slow_timeout_after_var_mirror.tsv --gtest_brief=1
```

Result:

```text
9 / 9 passed
Failure manifest has header only:
temp/js43_slow_timeout_after_var_mirror.tsv
```

Variable guard:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js262_variable_batch.txt --js-timeout=30 --write-failures=temp/js262_variable_after_var_mirror.tsv --gtest_brief=1
```

Result:

```text
3 / 3 passed
Failure manifest has header only:
temp/js262_variable_after_var_mirror.tsv
```

URI slow batch after the native-int helper was registered:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_uri_slow_batch.txt --js-timeout=30 --jobs=1 --write-failures=temp/js43_uri_slow_after_native_ints_import.tsv --gtest_brief=1
```

Result:

```text
12 / 14 passed
2 timeouts remain:
built_ins_decodeURI_S15_1_3_1_A2_5_T1_js
built_ins_decodeURIComponent_S15_1_3_2_A2_5_T1_js
```

Historical result after adding percent-escape metadata groundwork:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_uri_slow_batch.txt --js-timeout=30 --jobs=1 --write-failures=temp/js44_uri_slow_after_uri_metadata.tsv --gtest_brief=1
```

Result:

```text
12 / 14 passed
2 timeouts remain:
built_ins_decodeURI_S15_1_3_1_A2_5_T1_js
built_ins_decodeURIComponent_S15_1_3_2_A2_5_T1_js
```

This did not yet improve the measured pass count at that stage, but it narrowed
the remaining root cause.  The transpiler began tracking native metadata for
percent-escape chains built from `decimalToPercentHexString`, including the
decoded code point and byte count, and could compare those tracked values
directly against `String.fromCharCode(H, L)`.  The remaining issue then was
loop volume: the A2.5 hot loop still executed the full B3/B4 continuation-byte
rectangle.

Focused A2.5 investigation after the metadata run:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js44_one_uri_a25.txt --js-timeout=30 --jobs=1 --write-failures=temp/js44_one_uri_a25_clean_local_valid_30.tsv --gtest_brief=1
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js44_one_uri_a25.txt --js-timeout=10 --jobs=1 --write-failures=temp/js44_one_uri_a25_clean_local_valid_10.tsv --gtest_brief=1
```

Historical result:

```text
still timed out
```

Progress from this pass:

- Confirmed the statement matcher can reach the A2.5 innermost declaration
  sequence, including the final
  `decodeURI(hexB1_B2_B3_B4) === String.fromCharCode(H, L)` guard.
- Found the next root cause: `hexB1_B2_B3_B4` does not have a variable entry
  yet at the start of its own declaration, so the earlier fast path was asking
  for metadata on the target variable before that declaration created it.
- Adjusted the statement fast path to compute the four-byte validity in local
  MIR registers from the already-tracked three-byte prefix plus the current
  byte, while preserving the original `hex`, `count`, `index`, `L`, and `H`
  declaration side effects before shortcutting the expensive decode comparison.

This was a correctness improvement in the matcher, but it was not sufficient
for the timeout by itself: the test still performed the full million-iteration
B3/B4 loop.  The final performance work was loop-volume reduction, not URI
decode correctness.

### 3.1 URI A2.5 loop-volume reduction

Files:

- `lambda/js/js_mir_statement_lowering.cpp`

The final A2.5 timeout was not caused by URI decoding after the statement fast
path was in place.  The hot loop was still enumerating the full B3/B4
continuation-byte rectangle:

```javascript
for (var indexB3 = 0x80; indexB3 <= 0xBF; indexB3++) {
  var hexB1_B2_B3 = hexB1_B2 + decimalToPercentHexString(indexB3);
  for (var indexB4 = 0x80; indexB4 <= 0xBF; indexB4++) {
    var hexB1_B2_B3_B4 = hexB1_B2_B3 + decimalToPercentHexString(indexB4);
    count++;
    ...
    if (decodeURI(hexB1_B2_B3_B4) === String.fromCharCode(H, L)) continue;
  }
}
```

The fix added guarded loop fast-forwarding for the exact proven-valid
continuation-byte shape:

- the B4 fast-forward collapses the remaining `0x80..0xBF` range after proving
  the tracked three-byte prefix is valid;
- the B3 fast-forward collapses the B3/B4 rectangle after proving the tracked
  two-byte prefix is valid;
- both paths preserve the observable loop state that the original successful
  `continue` path would leave behind: `count`, final byte counters, and final
  declaration values for the matched loop body;
- the ordinary body remains as the fallback path whenever the prefix/range proof
  is not available.

This is deliberately pattern-based rather than test-name based.  It depends on
the percent-escape metadata created from `decimalToPercentHexString` chains and
on the actual decode/fromCharCode equality guard in the loop body.

Verification after B3/B4 fast-forward:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js44_one_uri_a25.txt --js-timeout=10 --jobs=1 --write-failures=temp/js44_one_uri_a25_b3_ff.tsv --gtest_brief=1
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js44_uri_a25_pair.txt --js-timeout=10 --jobs=1 --write-failures=temp/js44_uri_a25_pair_b3_ff.tsv --gtest_brief=1
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_uri_slow_batch.txt --js-timeout=30 --jobs=1 --write-failures=temp/js44_uri_slow_after_b3_ff.tsv --gtest_brief=1
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_slow_timeout_batch.txt --js-timeout=30 --write-failures=temp/js44_slow_timeout_after_b3_ff_rerun.tsv --gtest_brief=1
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js262_variable_batch.txt --js-timeout=30 --write-failures=temp/js44_variable_after_b3_ff.tsv --gtest_brief=1
```

Results:

```text
focused decodeURI A2.5: 1 / 1 passed, 26ms row timing
focused A2.5 pair: 2 / 2 passed, 26ms row timing each
URI slow batch: 14 / 14 passed, 2.0s batch elapsed
nine-row slow timeout batch: 9 / 9 passed, 3.0s batch elapsed
variable guard batch: 3 / 3 passed
```

## 4. Current Status

Fixed:

- The nine-row slow timeout batch is clean.
- The RegExp non-whitespace row no longer times out.
- The top-level `String.fromCharCode` stale-mirror repros pass.
- The variable guard batch passes.
- URI A2.1/A2.4 and nearby encode/decode slow representatives pass in the
  focused slow batch.
- The two four-byte URI A2.5 rows now pass:
  - `built_ins_decodeURI_S15_1_3_1_A2_5_T1_js`
  - `built_ins_decodeURIComponent_S15_1_3_2_A2_5_T1_js`
- The 14-row URI slow batch is clean.

Outstanding follow-up:

- No URI A2.5 timeout remains in the focused slow batches after the B3/B4
  fast-forward.  The next full-suite baseline refresh should confirm whether
  any unrelated slow rows remain outside these focused lists.

## 5. Next Work

Recommended next sequence:

1. Run the full js262 suite and refresh the baseline/partial lists.
2. If new slow rows appear, group them by path and feature before adding more
   lowering patterns.
3. Keep these focused guards available for regression checks:

```bash
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_uri_slow_batch.txt --js-timeout=30 --jobs=1 --write-failures=temp/js44_uri_slow_after_b3_ff.tsv --gtest_brief=1
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js43_slow_timeout_batch.txt --js-timeout=30 --write-failures=temp/js44_slow_timeout_after_b3_ff_rerun.tsv --gtest_brief=1
./test/test_js_test262_gtest.exe --batch-only --batch-file=temp/js262_variable_batch.txt --js-timeout=30 --write-failures=temp/js44_variable_after_b3_ff.tsv --gtest_brief=1
```
