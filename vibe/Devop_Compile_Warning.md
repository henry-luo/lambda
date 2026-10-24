# Compile Warning Triage & Cleanup Proposal

## Background

A clean debug build of Lambda currently emits **~82,000 compiler warnings** (Apple clang 17, debug_native config). The volume is high enough that real, actionable warnings are buried in noise — meaning new bugs of the same class can be introduced without anyone noticing.

This document categorizes the warnings, recommends which to silence and which to fix, and proposes a staged cleanup plan.

## Warning Inventory

Output of `make 2>&1 | grep -oE '\[-W[a-zA-Z0-9-]+\]' | sort | uniq -c | sort -rn`:

| Count | Flag |
|------:|------|
| 62,137 | `-Wreturn-type-c-linkage` |
| 5,972  | `-Wc99-extensions` |
| 4,694  | `-Wgnu-anonymous-struct` |
| 4,406  | `-Wnested-anon-types` |
| 984    | `-Wcast-function-type-mismatch` |
| 916    | `-Wmissing-field-initializers` |
| 329    | `-Wgnu-include-next` |
| 328    | `-Winclude-next-absolute-path` |
| 317    | `-Wmissing-braces` |
| 317    | `-Wdeprecated-copy` |
| 251    | `-Wwritable-strings` |
| 250    | `-Wreorder-ctor` |
| 242    | `-Wunused-parameter` |
| 218    | `-Wnon-c-typedef-for-linkage` |
| 176    | `-Wunused-variable` |
| 166    | `-Wpointer-bool-conversion` |
| 143    | `-Wunused-function` |
| 139    | `-Wcomment` |
| 121    | `-Wsign-compare` |
| 72     | `-Wstrict-prototypes` |
| 52     | `-Wignored-qualifiers` |
| 47     | `-Wsingle-bit-bitfield-constant-conversion` |
| 43     | `-Wdeprecated-declarations` |
| 42     | `-Wunused-but-set-variable` |
| 24     | `-Wc11-extensions` |
| 17     | `-Wconstant-conversion` |
| 15     | `-Wtautological-constant-out-of-range-compare` |
| 13     | `-Wenum-compare` |
| 11     | `-Wnewline-eof` |
| 11     | `-Wmismatched-tags` |
| 7      | `-Wundefined-bool-conversion` |
| 7      | `-Wextra-semi` |
| 5      | `-Wswitch` |
| 5      | `-Wformat` |
| 3      | `-Wvarargs` |
| 3      | `-Wunused-const-variable` |
| 3      | `-Wsometimes-uninitialized` |
| 3      | `-Wmacro-redefined` |
| 3      | `-Winvalid-offsetof` |
| 2      | `-Wparentheses` |
| 2      | `-Wmicrosoft-redeclare-static` |
| 1 each | `-Wzero-length-array`, `-Wunused-label`, `-Wtrigraphs`, `-Wreturn-type`, `-Wincompatible-pointer-types-discards-qualifiers`, `-Wgnu-conditional-omitted-operand` |

## Category A — Silence (intentional patterns, ~78k warnings)

These reflect deliberate codebase conventions or come from third-party headers we don't control. Fixing them would be massive churn for no correctness gain.

| Flag | Count | Rationale |
|------|------:|-----------|
| `-Wreturn-type-c-linkage` | 62,137 | `Item` is a tagged 64-bit value returned through `extern "C"` for the MIR JIT ABI (see `lambda/lambda.h`). The pattern is core to the architecture — clang can't verify C layout-compatibility, but it is. |
| `-Wc99-extensions` | 5,972 | Compound literals & designated initializers used in C++ headers per the C+ convention documented in `doc/dev/C_Plus_Convention.md`. |
| `-Wgnu-anonymous-struct` | 4,694 | Anonymous structs in `lambda.hpp`, `view.hpp`, `clip_shape.h`, `event.hpp`, `dom_node.hpp`, etc. — pervasive deliberate pattern. |
| `-Wnested-anon-types` | 4,406 | Same root cause. |
| `-Wnon-c-typedef-for-linkage` | 218 | `typedef struct { … } Name;` with C++ members in `view.hpp`. Cosmetic. |
| `-Wgnu-include-next`, `-Winclude-next-absolute-path` | 657 | From third-party headers (thorvg/mpdecimal etc.). Not our code. |
| `-Wstrict-prototypes` | 72 | Mostly third-party C headers. |
| `-Wignored-qualifiers` | 52 | `const` on return-by-value in `thorvg_capi.h`. Third-party. |
| `-Wnewline-eof`, `-Wmismatched-tags`, `-Wextra-semi` | 29 | Pure style. |
| `-Wmissing-field-initializers`, `-Wmissing-braces` | 1,233 | Aggregate-init style; C++ zero-inits the rest. Safe. |
| `-Wunused-parameter` | 242 | High volume from callback/interface conformance (signal handlers, hashmap compare fns, etc.). Per-site `(void)param` suppression is more churn than value. |

## Category B — Fix (real correctness issues, ~270 warnings)

These flag actual bugs or undefined behavior. Each occurrence deserves inspection.

| Flag | Count | Why fix |
|------|------:|---------|
| `-Wreturn-type` | 1 | Missing return → undefined behavior. |
| `-Wsometimes-uninitialized` | 3 | Uninitialized read on some code paths. |
| `-Wundefined-bool-conversion` | 7 | Comparing `this` or array-decay against null — always true/false. |
| `-Wpointer-bool-conversion` | 166 | Same class. Example: `radiant/script_runner.cpp:174` tests `s->chars` where `chars` is an inline array — the check is dead. Each site is a likely-broken null guard. |
| `-Wtautological-constant-out-of-range-compare` | 15 | Comparison that can never succeed. |
| `-Wconstant-conversion` | 17 | Silent value truncation. |
| `-Wsingle-bit-bitfield-constant-conversion` | 47 | Storing values that don't fit (commonly signed-1-bit). Often a logic bug. |
| `-Wenum-compare` | 13 | Comparing values from two unrelated enums — usually the wrong constant. |
| `-Wswitch` | 5 | Missing enum cases — easy to fix, valuable. |
| `-Wvarargs` | 3 | `va_start` misuse → UB. |
| `-Winvalid-offsetof` | 3 | `offsetof` on non-standard-layout type; can silently break. |
| `-Wformat` | 5 | printf/format-arg mismatch. |
| `-Wparentheses` | 2 | Suspect precedence. |
| `-Wmacro-redefined` | 3 | Two headers disagree on macro definition. |
| `-Wdeprecated-declarations` | 43 | Calling APIs scheduled for removal. Plan migrations. |
| `-Wincompatible-pointer-types-discards-qualifiers` | 1 | Drops `const`. |
| `-Wmicrosoft-redeclare-static` | 2 | Static/non-static mismatch. |

## Category C — Clean up opportunistically (code health, ~2,300 warnings)

Low-risk improvements that can be batched per subsystem.

| Flag | Count | Notes |
|------|------:|-------|
| `-Wunused-function` | 143 | **Dead-code candidates** — either delete or mark `static`. Best signal we have for dead code without running coverage. |
| `-Wunused-variable` | 176 | Trivial deletions. |
| `-Wunused-but-set-variable` | 42 | Variable written but never read — often a leftover from refactoring. |
| `-Wunused-const-variable`, `-Wunused-label` | 4 | Trivial. |
| `-Wreorder-ctor` | 250 | Member-init list out of declaration order. Harmless but indicates the ctor diverged from the struct layout — fix in the ~3 affected files (`DomElement`, etc.). |
| `-Wdeprecated-copy` | 317 | User-declared `operator=` makes implicit copy-ctor deprecated. Add `= default;` copy-ctor in one place per type (e.g. `ConstItem` in `lambda.hpp`) — kills hundreds at once. |
| `-Wwritable-strings` | 251 | `char* s = "literal"` should be `const char*`. Real const-correctness; fix in waves per subsystem. |
| `-Wcast-function-type-mismatch` | 984 | Function-pointer casts between incompatible types, typically around callback registration. Often intentional but UB on strict ABIs — audit when convenient. |
| `-Wsign-compare` | 121 | Most benign; a few hide bugs. Fix per file. |
| `-Wcomment` | 139 | Stray `/*` inside block comments. Trivial. |

## Progress

| Phase | Status | Result |
|-------|--------|--------|
| Phase 1 — Silence Category A | ✅ Done | Warnings: ~82,000 → 2,837 |
| Phase 2 — Promote Category B to errors | ✅ Done (with one caveat — see below) | Warnings: 2,837 → **2,764**, 0 errors |
| Phase 3 — `-Wpointer-bool-conversion` audit | ✅ Done | Warnings: **0**; now promoted to error |
| Phase 4 — Dead code audit | ✅ Done | 167 → **0** `-Wunused-function`; now promoted to error |
| Phase 5 — Code-health waves | ⏳ Ongoing | `-Wdeprecated-copy`, tiny unused classes, `-Wreorder-ctor`, `-Wcomment`, `-Wwritable-strings`, and `-Wunused-but-set-variable` fixed and promoted |

**Caveat (Phase 2):** `-Werror=undefined-bool-conversion` was **not** added. The 7 sites (all in `lambda/lambda-data.cpp`) use the `if (!this) return null_result;` defensive null-receiver pattern; fixing them properly requires moving null checks to every caller — a wider refactor than Phase 2 scope. The 7 warnings remain; treat as a separate task.

**Flag-ordering gotcha discovered during Phase 2:** clang's `-Werror=foo` is a **prefix match** that re-enables warnings — `-Werror=return-type` also promotes `-Wreturn-type-c-linkage`. The `-Wno-*` flags must appear **after** the `-Werror=*` flags in the build flags list to take effect; the config in `build_lambda_config.json` is ordered accordingly.

## Recommended Plan

### Phase 1 — Silence Category A (1 PR) ✅ DONE

Add `-Wno-*` flags to the project compile flags in `build_lambda_config.json` for the patterns we've ratified as intentional:

```
-Wno-return-type-c-linkage
-Wno-c99-extensions
-Wno-gnu-anonymous-struct
-Wno-nested-anon-types
-Wno-non-c-typedef-for-linkage
-Wno-gnu-include-next
-Wno-include-next-absolute-path
-Wno-strict-prototypes
-Wno-ignored-qualifiers
-Wno-newline-eof
-Wno-mismatched-tags
-Wno-extra-semi
-Wno-missing-field-initializers
-Wno-missing-braces
-Wno-unused-parameter
```

**Actual result:** 82,000 → 2,837 warnings.

### Phase 2 — Promote real bugs to errors (1 PR) ✅ DONE (except `-Werror=undefined-bool-conversion`)

The following are now `-Werror=*` and cannot regress:

```
-Werror=return-type
-Werror=sometimes-uninitialized
-Werror=varargs
-Werror=format
-Werror=switch
-Werror=tautological-constant-out-of-range-compare
-Werror=invalid-offsetof
-Werror=enum-compare
```

**Fixed sites** (~55):
- **`-Wreturn-type` (1):** `lambda/js/js_runtime_value.cpp:630` — `break` in `LMD_TYPE_MAP` case exited the switch with no return; replaced with the actual default return.
- **`-Wvarargs` (3):** `lambda/lambda-data-runtime.cpp:711,951,1025` — `va_start` was passing `type->length` / a local var instead of the actual last named parameter (`map`/`obj`/`elmt`).
- **`-Wformat` (5):** `lambda/lambda-eval.cpp:1865`, `lambda/main.cpp:3160,3207`, `lambda/validator/validate.cpp:237,245` — `%ld` vs `int64_t` / `int` mismatches.
- **`-Wswitch` (5):** `lambda/validator/error_reporting.cpp` (missing `PATH_UNION`), `lambda/js/js_globals.cpp`, `lambda/js/js_runtime.cpp`, `lambda/js/js_typed_array.cpp` (missing `JS_TYPED_UINT8_CLAMPED`, `JS_TYPED_BIGINT64`, `JS_TYPED_BIGUINT64` cases).
- **`-Wsometimes-uninitialized` (3):** `lambda/js/js_mir_expression_lowering.cpp:12943`, `lambda/js/js_mir_statement_lowering.cpp:4855` — initialized `MIR_reg_t` locals to `0`.
- **`-Winvalid-offsetof` (3):** `lambda/transpile-mir.cpp:73,10152,11774` — wrapped each `offsetof(EvalContext, …)` in `#pragma clang diagnostic push/pop`. `EvalContext` inherits from `Context` via single, non-virtual public inheritance with POD members, so the offset is well-defined; the existing `static_assert` continues to verify the invariant.
- **`-Wtautological-constant-out-of-range-compare` (15):** `lambda/js/js_mir_*.cpp` and `lambda/lambda-error.cpp:198` — added `(int)` casts on the LHS of `node_type ==/!= (int)TS_AST_NODE_*` comparisons (the field is typed `JsAstNodeType` but legitimately holds `TsAstNodeType` values ≥ 1000) and on `ERR_IS_INTERNAL((int)code)`.
- **`-Wenum-compare` (13):** `radiant/layout_flex.cpp` and `radiant/layout_flex_multipass.cpp` — `(int)item->fi->align_self ==/!= ALIGN_*`. `FlexItemProp::align_self` is declared `CssEnum` but stores `AlignType` values which alias `CSS_VALUE_*` — same values, different declared enums.

**Deferred — `-Werror=undefined-bool-conversion` (7 sites):**
All 7 are in `lambda/lambda-data.cpp`: `Map::get`, `Element::get_attr`, `Element::has_attr`, `Map::has_field`, `List::get`. Each begins `if (!this || …) return null_result;` — a defensive null-receiver pattern that is UB in C++ but used as a deliberate convention in this codebase. Fixing them properly requires auditing every caller (e.g. `mark_reader.cpp:604`, `validate.cpp:408`, `html5_parser.cpp:502`, `dom_element.cpp:453`, …) and moving the null check to the call site. This is wider than Phase 2 scope and needs a design decision on the receiver-null convention before proceeding.

### Phase 3 — Audit `-Wpointer-bool-conversion` (separate task)

✅ **Done.** Audited and removed the stale null checks against inline array fields such as `String::chars`, `Symbol::chars`, `Binary::chars`, URL component strings, JS AST identifier names, and fixed MIR collection arrays. These checks now test the owning pointer and, where emptiness matters, `len > 0` / count fields.

`-Werror=pointer-bool-conversion` is now enabled in `build_lambda_config.json`; a clean `make build` reports **0** `-Wpointer-bool-conversion` warnings.

### Phase 4 — Dead code audit (separate task)

✅ **Done.** Cleaned dead local helpers across markup parsing, formatters, CSS helpers, URL/path/input utilities, JS runtime modules, Lambda vector/equality/proc helpers, logging, packaging, npm, target handling, vmap, TS preprocessing, MIR transpilation, font database, and Radiant layout/event/state/table/text code. Optional hashmap macro factory helpers are now marked intentionally unused at the macro source rather than at every expansion.

Current clean-build result: `-Wunused-function` is **0** in `temp/compile_warning_deadcode_werror_clean_build.log` (from **167** at Phase 4 start). `-Werror=unused-function` is now enabled in `build_lambda_config.json`, and `-Wgcc-compat` remains **0** after moving the hashmap unused attribute placement.

### Phase 5 — Code-health waves (ongoing)

`-Wwritable-strings`, `-Wsign-compare`, `-Wcast-function-type-mismatch` — clean up per subsystem when touching the relevant code. Don't block on a single sweep.

✅ **`-Wdeprecated-copy` done.** Added an explicit defaulted copy constructor for `ConstItem` in `lambda/lambda.hpp`, matching its existing defaulted copy assignment operator. Clean build result: `-Wdeprecated-copy` is **0** in `temp/compile_warning_deprecated_copy_werror_clean_build.log`; `-Werror=deprecated-copy` is now enabled in `build_lambda_config.json`.

✅ **Tiny unused classes done.** Removed the last `-Wunused-const-variable` and `-Wunused-label` sites: an unused async-state-machine label, one stale sysinfo TTL constant, and two unused WOFF2 decoder constants. Clean build result: both warning classes are **0** in `temp/compile_warning_tiny_unused_werror_clean_build.log`; `-Werror=unused-const-variable` and `-Werror=unused-label` are now enabled in `build_lambda_config.json`.

✅ **`-Wreorder-ctor` done.** Reordered initializer lists to match declaration order in `InputContext` and `DomElement`. Clean build result: `-Wreorder-ctor` is **0** in `temp/compile_warning_reorder_werror_clean_build.log`; `-Werror=reorder-ctor` is now enabled in `build_lambda_config.json`.

✅ **`-Wcomment` done.** Fixed nested block-comment markers in repeated header comments and file docs (`radiant/view.hpp`, `lambda/js/js_class.h`, `lambda/js/js_dom.cpp`, `radiant/layout_table.cpp`, markup/PDF/router docs). Clean build result: `-Wcomment` is **0** in `temp/compile_warning_comment_werror_clean_build.log`; `-Werror=comment` is now enabled in `build_lambda_config.json`.

✅ **`-Wwritable-strings` done.** Tightened const-correctness for literal-only APIs (`heap_strcpy`, `_map_get`, `jit_gen_func`, print indentation/type names) and gave Radiant default font-family literals writable static storage where the existing ownership field requires `char*`. Clean build result: `-Wwritable-strings` is **0** in `temp/compile_warning_writable_strings_werror_clean_build.log`; `-Werror=writable-strings` is now enabled in `build_lambda_config.json`.

✅ **`-Wunused-but-set-variable` done.** Removed stale state/counter variables and assignment-only leftovers across markup parsers, CSS parsing, YAML/TOML/XML/RDB/D2 inputs, JS regex/path/buffer helpers, TS preprocessing/type parsing, logging/memtrack, and Radiant layout/table/intrinsic sizing. One warning exposed a real TypeScript preprocessing bug: `angle_depth` was tracked but not included in the nested-depth condition, so generic type expressions now stay inside the skipped type span. Clean build result: `-Wunused-but-set-variable` is **0** in `temp/compile_warning_unused_set_werror_clean_build.log`; `-Werror=unused-but-set-variable` is now enabled in `build_lambda_config.json`.

### Phase 6 — Defect wave (2026-06-06)

A full clean rebuild (`make build`, debug_native) showed **1,420** warnings. This wave fixed every category that represents an actual defect, plus the bulk dead-variable cleanup. Result: **1,420 → 1,198**, 0 errors; Lambda baseline **2942/2942** and Radiant baseline **5712/5712** both green.

| Flag | Fixed | Root cause & fix |
|------|------:|------------------|
| `-Wparentheses` | 2 → **0** | **Real bug.** `ITEM_TRUE`/`ITEM_FALSE` in `lambda/lambda.h` lacked outer parens → `x != ITEM_FALSE` parsed as `(x != …<<56) \| 0`. Added parentheses; removes a latent correctness bug wherever these macros appear in a larger expression. |
| `-Wconstant-conversion` | 17 → **0** | **Real bug.** U+FFFD (`0xFFFD`) was truncated to a single invalid `char` (`-3`) in `lambda/input/html5/html5_tokenizer.cpp`. Added `HTML5_REPLACEMENT_CHAR_UTF8` + byte-string append helpers so the replacement char is emitted as proper 3-byte UTF-8 per the HTML5 spec. |
| `-Wsingle-bit-bitfield-constant-conversion` | 47 → **0** | Boolean flags declared signed `int : 1` (truncating `1`→`-1`) in `radiant/view.hpp` (`FlexItemProp`, background struct). Changed to `unsigned : 1`. |
| `-Wmacro-redefined` | 3 → **0** | `_GNU_SOURCE` defined on the command line *and* in `lib/cmdedit.c`, `lib/file_utils.c`, `lib/mime-detect.c`. Guarded the local defines with `#ifndef`. |
| `-Wtrigraphs` | 1 → **0** | `"??="` is a trigraph sequence in `lambda/js/build_js_ast.cpp`. Escaped to `"?\?="`. |
| `-Wzero-length-array` | 1 → **0** | `Item check_args[0]` in `lambda/js/js_dom.cpp` passed with count 0 → replaced with `nullptr`. |
| `-Wunused-variable` | 154 → **3** | Dead-code cleanup across ~70 files (incl. cascade-orphans the removals exposed: `row_count`/`current_row_index` in `layout_table.cpp`, `left_int64`/`right_int64` in `transpile-mir.cpp`, `ep`/`line_is_ordered`/`range_off_pos`). `runner.cpp` `p3`–`p7` moved inside `#ifdef LAMBDA_C2MIR`; `cmdedit.c` `default_bindings` removed (tab completion wired into `enhanced_bindings`). The 3 remaining are platform/config-gated — see below. |

Side-effect-bearing initializers were preserved (the call kept as a bare statement); only the unused binding was dropped.

### Deliberately not fixed (and why)

Remaining **1,199** warnings, none of which represent a defect to fix in this pass:

| Flag | Count | Why left |
|------|------:|----------|
| `-Wcast-function-type-mismatch` | 996 | Intentional. All from the `FPTR()` type-erasure macro in `lambda/sys_func_registry.c`, which stores heterogeneous function pointers in one registry table and calls them through the correct signature at dispatch. This is the design, not a bug. |
| `-Wsign-compare` | 119 | Mostly benign `size_t`/`int` loop comparisons; a broad sweep is risky for little gain. Fix per-file when touching the code (Phase 5 stance). |
| `-Wdeprecated-declarations` | 44 | Deprecated macOS platform APIs (e.g. `NSBitmapImageRep`); needs API migration, out of scope. |
| `-Wc11-extensions` | 26 | Benign C11-in-C++ usage. |
| `-Wundefined-bool-conversion` | 7 | The `if (!this) return null_result;` defensive null-receiver pattern in `lambda/lambda-data.cpp`. UB in C++ but a deliberate convention; a proper fix is a call-site-wide refactor (already deferred in the Phase 2 caveat). |
| `-Wunused-variable` | 3 | Platform/config-gated, needed in other builds: `g_old_sigwinch` (`#if defined(SIGWINCH)`), `linux_font_dirs` (`#ifdef __linux__`), `dst_offset` (`#ifdef FONT_COMPRESSION_BIN`). |
| `-Wmicrosoft-redeclare-static` | 2 | Minor static/non-static redeclaration. |
| `-Wgnu-conditional-omitted-operand` | 1 | Benign `x ?: y` GNU idiom in `radiant/webview_layer_mac.mm`. |

**`cmdedit.c` tab completion (resolved):** the live dispatch table is `enhanced_bindings` (via `find_key_handler`), and it previously did **not** map `KEY_TAB` — so `handle_tab_completion` was unreachable and the duplicate `default_bindings` table was dead. Fixed by adding `{KEY_TAB, handle_tab_completion}` to `enhanced_bindings` and deleting the dead `default_bindings` table (its other handlers all already live in `enhanced_bindings`). TAB now reaches the handler; full completion still requires the REPL to register a provider (`rl_attempted_completion_function`), otherwise TAB inserts a literal tab. REPL tests (`test_lambda_repl_gtest.exe`) pass 38/38.

### Phase 7 — Targeted defect/portability wave (2026-06-06)

A clean rebuild (`make build`, debug_native) showed **1,112** warnings (the prior 1,199 had already dropped via unrelated code churn — notably `-Wdeprecated-declarations` 44→12, `-Wc11-extensions` 26→1, `-Wsign-compare` 119→91). This wave fixed the remaining small categories that are genuine defects or portability issues. Result: **1,112 → 1,104**, 0 errors; Lambda baseline **2943/2943** green.

| Flag | Fixed | Root cause & fix |
|------|------:|------------------|
| `-Wdeprecated-declarations` (`sprintf`) | 5 → **0** (of 12) | `lambda/js/js_runtime.cpp` JS `Function()` constructor built its wrapper string with deprecated `sprintf`. Switched to `snprintf` with a tracked `bufsz`/remaining-size; behavior identical for the bounded constant fragments it writes. The other 7 are `CVDisplayLink*` (macOS 15 deprecation, needs Swift-style `NSView.displayLink` migration — left on, see below). |
| `-Wmicrosoft-redeclare-static` | 2 → **0** | `content_type_to_extension` (`lambda/input/input_http.cpp`) and `js_module_cache_reset` (`lambda/js/js_runtime.cpp`) were defined `static` but declared non-static in their headers and called from other TUs (`main.cpp`, `js_runtime_state.cpp`). clang's MS extension silently gave them external linkage to match. Removed `static` so the definition matches the header. |
| `-Wc11-extensions` | 1 → **0** | `lambda/serve/serve_utils.cpp` (a C++ TU) used the C11 keyword `_Thread_local`. Switched to the C++ `thread_local` keyword — identical semantics. |

### Deliberately left on (Phase 7 stance)

Per the "keep the warning visible rather than mask it" preference, the following were **not** silenced:

| Flag | Count | Why left on |
|------|------:|-------------|
| `-Wcast-function-type-mismatch` | 996 | Intentional type-erasure: `FPTR()` registry in `sys_func_registry.c` (952) plus the JIT call-site casts from `fn_ptr` (`void *(*)()`) to concrete `Item`-returning signatures in `lambda-eval.cpp`/`render_map.cpp`/`event.cpp`/`template_registry.cpp`. Same design, not a bug. |
| `-Wsign-compare` | 91 | Phase 5 stance — mostly benign `size_t`/`int` comparisons; a broad sweep is risky for little gain. Fix per-file when touching the code. *(Superseded — swept in Phase 8.)* |
| `-Wdeprecated-declarations` (`CVDisplayLink*`) | 7 | macOS 15 deprecation in `radiant/frame_clock.cpp`; needs migration to `NSView/NSWindow/NSScreen.displayLink`. Left visible rather than `-Wno-`'d so the migration stays on the radar. |
| `-Wundefined-bool-conversion` | 7 | `if (!this) return …;` defensive null-receiver convention in `lambda/lambda-data.cpp` (deferred since Phase 2; call-site-wide refactor). |
| `-Wunused-variable` | 3 | Platform/config-gated (`g_old_sigwinch`, `linux_font_dirs`, `dst_offset`). |

### Phase 8 — `-Wsign-compare` full sweep (2026-06-06)

The whole `-Wsign-compare` class (the largest non-intentional remainder) was swept in one pass, file by file. Result: **1,104 → 1,013**, 0 errors; Lambda baseline **2943/2943** green and the Radiant **layout** baseline (baseline/wpt-css-text/form/markdown/page-suite/view) all 0-failed. All 91 sites were signed loop-indices/lengths compared against unsigned counts (`size_t`/`uint32_t`/`uint64_t`) or vice-versa; every fix is a behavior-preserving cast that makes the existing implicit integer conversion explicit (no edge-case semantics changed).

| File(s) | Sites | Fix pattern |
|---------|------:|-------------|
| `lambda/lambda-eval-num.cpp` | 16 | `size_t i < arr->length` / `list->length` (`int64_t`) → cast length to `(size_t)`. |
| `lambda/js/js_runtime.cpp` | 19 | 16 namespace-cache `static int *_epoch = -1` caches compared against `uint64_t js_heap_epoch` → **changed the caches to `uint64_t` (sentinel `(uint64_t)-1`)** — the proper fix, since the old `int` truncated the epoch on assignment. Plus 2 `int i < name->len` loops (cast `(int)`) and one `sb->length == (int)s->len` (→ `(size_t)`). |
| `lambda/input/html5/html5_parser.cpp` | 12 | `size_t` loop index vs `int64_t ->length` → cast length to `(size_t)`. |
| `lambda/input/html5/html5_tree_builder.cpp` | 8 | Same as above. |
| `lambda/input/markup/block/*` (link_def, paragraph, quote, list, document) | 12 | `size_t` line indices vs `int line_count`/`current_line`; plus `current_line` vs `size_t lazy_lines_count`, and a `size_t i < int64_t length` loop → casts (`line_count` is always ≥0). |
| `lib/log.c` | 6 | `int len` (guarded `> 0`) vs `size_t remaining` → `(size_t)len`. |
| `lambda/lambda-proc.cpp` | 3 | `int i < arg->len` (`uint32_t`) → `(int)arg->len`. |
| `lambda/input/css/*` (engine, formatter ×2, parser, value_parser) | 5 | `int`/`size_t` index vs the other-signed `count`/`property_count`/`rule_count` field → cast to match. |
| `lambda/build_ast.cpp` | 2 | `(int)name->length == ->len` (`uint32_t`) → `(int)` on the RHS. |
| `lambda/js/js_globals.cpp` | 2 | `int on_len == rn->len` (`uint32_t`) → `(int)rn->len`. |
| `lambda/input/input-jsx.cpp` | 2 | `int i < text->len` (`uint32_t`) → `(int)text->len`. |
| `lambda/emit_sexpr.cpp` | 1 | `size_t length == (int64_t)f->name->len` → `(size_t)`. |
| `lambda/input/input-csv.cpp` | 1 | `size_t i < headers->length` (`int64_t`) → `(size_t)`. |
| `lambda/js/js_clipboard.cpp` | 1 | `int i < s->len` (`uint32_t`) → `(int)s->len`. |
| `lib/url.c` | 1 | `int slen = strlen(...)` → `size_t slen` (matches the `size_t` loop var). |

`-Werror=sign-compare` was **not** added: a few of these sites (and future ones) are genuinely benign and the cast convention is project-by-project; promoting would force a cast on every incidental `size_t`/`int` loop. Left as plain warnings so they stay visible without blocking builds.

**Pre-existing Radiant UI-Automation failures (not caused by this work):** `test_ui_automation_gtest.exe` reports 187 failing (e.g. `test_state_store_refactor`, 19/20 assertions). Verified by `git stash` + clean rebuild that these fail **identically on `master` without any of these changes** — they are environment/GUI-driven and orthogonal to the warning cleanup. The layout baseline (what the CSS/HTML/markup edits actually touch) passes 100%.

### Phase 9 — Scope-silence the registry type-erasure (2026-06-06)

The 952 `-Wcast-function-type-mismatch` in `lambda/sys_func_registry.c` are the `FPTR()`/`NPTR()`
macro casting every system function to the uniform `fn_ptr` (`void*(*)()`) for the MIR JIT import
registry — the matching ABI is stored per-row and MIR calls each through its real prototype at
dispatch (sound by construction; see the `fn_ptr` round-trip note above). They were 94% of all
remaining warnings, drowning out the rest.

Rather than a global `-Wno-` (which would hide *new* mismatches anywhere) or a per-file build flag
(which would touch `generate_premake.py` and be invisible to a reader of the file), the two registry
tables (`sys_func_defs[]` + `jit_runtime_imports[]`) are now wrapped in:

```c
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
... tables ...
#pragma clang diagnostic pop
```

with an explanatory comment at the push. The flag stays **on globally**, so the 44 call-back casts
elsewhere (`lambda-eval.cpp` ×38, `event.cpp`/`render_map.cpp` ×2, `template_registry.cpp`,
`lambda-data-runtime.cpp`) and any future mismatch still warn. A diagnostic pragma is codegen-neutral
(object code byte-identical), so no baseline re-run was needed.

Result: **1,013 → 61** warnings, 0 errors.

### Remaining after Phase 9 — 61 warnings, all intentional or out-of-scope

| Flag | Count | Why left on |
|------|------:|-------------|
| `-Wcast-function-type-mismatch` | 44 | Intentional `fn_ptr` **call-back** casts (JIT arity dispatch in `lambda-eval.cpp`, `template_body_fn` in `render_map.cpp`/`template_registry.cpp`, etc.). Left visible by choice; the 952 *registry* casts are now scope-silenced (Phase 9). |
| `-Wundefined-bool-conversion` | 7 | Deliberate null-receiver convention in `lambda/lambda-data.cpp` (needs call-site refactor). |
| `-Wdeprecated-declarations` | 7 | `CVDisplayLink*` macOS 15 deprecation in `radiant/frame_clock.cpp` (needs `NSView.displayLink` migration). |
| `-Wunused-variable` | 3 | Platform/config-gated (`g_old_sigwinch`, `linux_font_dirs`, `dst_offset`). |

## Success Criteria

- Clean debug build emits **< 100 warnings** after Phase 1. ⚠️ Actual: 2,837 (the remaining ~2,700 are Category C, deferred to Phase 5).
- Category B regressions are caught at build time after Phase 2. ✅ Achieved for 8 of 9 classes (see `-Werror=undefined-bool-conversion` caveat above).
- Each phase is independently shippable. ✅
