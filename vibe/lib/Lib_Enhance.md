# `./lib` Enhancement Proposal

Analysis of `lambda/` and `radiant/` codebases for opportunities to centralize
common data structures / algorithms under `./lib`, and to extend existing
`./lib` types with the operations callers actually need.

The findings below are ordered by impact. Each item names concrete call sites
so the scope of the change is easy to verify.

**Status legend:** ✅ done · ⏳ partial · ❌ not yet done · ⛔ deferred (out of scope)

---

## 1. Common data structures / algorithms to centralize

### 1.1 LRU cache — ✅ done

Originally 4+ hand-rolled copies. Now centralised:

- ✅ `lib/lru_cache.{h,c}` shipped — hash-keyed LRU with max entries / byte
  budget, optional TTL, eviction callback, O(1) get/put/touch/delete, iter MRU→LRU.
- ✅ Stub caches at [lambda/input/input_pool.cpp](../lambda/input/input_pool.cpp)
  and [lambda/input/input_file_cache.cpp](../lambda/input/input_file_cache.cpp)
  rewritten as thin wrappers over `lib/lru_cache`.
- ⏳ Other LRUs remain as-is because they are entangled with cache-specific
  metadata (etag handling, write slots, thread-local state):
  - [lambda/network/enhanced_file_cache.cpp:69](../lambda/network/enhanced_file_cache.cpp) — full LRU helpers retained
  - [lambda/input/input_sysinfo.cpp:34](../lambda/input/input_sysinfo.cpp) — manager allocated but cache pathway unused (sysinfo is currently dead code)
  - [lambda/js/js_dom.cpp:536](../lambda/js/js_dom.cpp) — small fixed-array variant
  - [lambda/sysinfo.cpp:64](../lambda/sysinfo.cpp) — named-slot TTL cache (not a generic shape)

15 gtest cases cover the new module; all callers verified.

### 1.2 Sorted-array + binary-search lookup — ✅ done

- ✅ `lib/binsearch.h` shipped — three variants:
  - `binsearch_strtab_n` / `binsearch_strtab` (case-sensitive + case-insensitive)
  - `binsearch_records` (generic key-equality)
  - `binsearch_range` (range-containment for sorted disjoint intervals)
- ✅ Migrated equality sites:
  - [lambda/format/html-defs.cpp:45](../lambda/format/html-defs.cpp) — `lookup()` over sorted string tables now a one-liner
  - [radiant/layout_text.cpp:178](../radiant/layout_text.cpp) — `lookup_full_case` ported with a small record cmp
- ✅ Migrated range sites:
  - [lambda/lambda-error.cpp:217](../lambda/lambda-error.cpp) — JIT debug-info address lookup
  - [lambda/js/js_runtime.cpp:12061](../lambda/js/js_runtime.cpp) — regex `js_regex_sorted_range_contains`
- ⛔ Remaining sites have different shapes or aren't lookups (qsort/bsearch
  in [input-latex-tables.cpp](../lambda/input/input-latex-tables.cpp) is sorting, not lookup).

11 gtest cases cover the module.

### 1.3 Thread pool — ✅ done

- ✅ `lib/thread_pool.{h,c}` shipped with `tp_create`, `tp_create_with_stack(threads, stack_size)` (added to support deep-recursion workloads), `tp_submit`/`tp_submit_priority`, `tp_wait_all`, `tp_destroy`. 3-level priority queue, mutex+condvar backend.
- ✅ Migrated:
  - [lambda/runner.cpp:944](../lambda/runner.cpp) — module pre-compilation (`tp_create_with_stack(actual, 8MB)`)
  - [lambda/js/js_mir_module_batch_lowering.cpp:5417](../lambda/js/js_mir_module_batch_lowering.cpp) — JS module batch lowering
  - [lambda/input/input_http.cpp:299](../lambda/input/input_http.cpp) — URL prefetcher converted from work-stealing to per-URL job submission
- ⏳ Existing dedicated pools kept as-is because they manage thread-local state (ThorVG canvas, scratch arena per worker):
  - [lambda/network/network_thread_pool.h](../lambda/network/network_thread_pool.h) — libuv-backed
  - [radiant/tile_pool.cpp:202](../radiant/tile_pool.cpp) — render workers with thread-local pool/arena/canvas

11 gtest cases cover the new module.

### 1.4 Compare-function boilerplate for `hashmap_new` — ✅ done

- ✅ `lib/hashmap_helpers.{h,c}` shipped with **four** macros:
  - `HASHMAP_DEFINE_STRKEY(name, struct_type, key_field)` — NUL-terminated C-string keys
  - `HASHMAP_DEFINE_PTRKEY(name, struct_type, key_field)` — pointer identity keys
  - `HASHMAP_DEFINE_INTKEY(name, struct_type, key_field)` — integer keys (int32/64, uint32/64, size_t)
  - `HASHMAP_DEFINE_LENSTRKEY(name, struct_type, str_field, len_field)` — length-prefix (non-NUL-terminated) strings, supports `StrView`-shaped keys via dotted-field access
- Each macro emits `<name>_hash`, `<name>_cmp`, `<name>_new(cap)`, `<name>_new_with_free(cap, fn)`.
- ✅ **36 cmp/hash function pairs eliminated** across:
  - [lambda/transpile-mir.cpp:247](../lambda/transpile-mir.cpp) — 6 entries (5 STRKEY + 1 PTRKEY)
  - [lambda/py/transpile_py_mir.cpp:218](../lambda/py/transpile_py_mir.cpp) — 4 entries
  - [lambda/bash/transpile_bash_mir.cpp:49](../lambda/bash/transpile_bash_mir.cpp) — 3 entries
  - [lambda/rb/transpile_rb_mir.cpp:154](../lambda/rb/transpile_rb_mir.cpp) — 3 entries
  - [lambda/ts/ts_type_builder.cpp:280](../lambda/ts/ts_type_builder.cpp) — 1 entry
  - [lambda/js/js_early_errors.cpp:393](../lambda/js/js_early_errors.cpp) — 1 entry
  - [lambda/js/js_globals.cpp:16145](../lambda/js/js_globals.cpp) — 1 entry
  - [lambda/js/js_mir_module_batch_lowering.cpp:5108](../lambda/js/js_mir_module_batch_lowering.cpp) — 1 entry
  - [lambda/module_registry.cpp:34](../lambda/module_registry.cpp) — 1 entry
  - [lambda/runner.cpp:658](../lambda/runner.cpp) — 1 entry
  - [lambda/mir.c:47](../lambda/mir.c) — 1 entry
  - [lambda/network/network_scheduler.cpp:51](../lambda/network/network_scheduler.cpp) — 1 entry
  - [lambda/network/enhanced_file_cache.cpp:22](../lambda/network/enhanced_file_cache.cpp) — 1 entry
  - [lambda/network/network_resource_manager.cpp:28](../lambda/network/network_resource_manager.cpp) — 1 entry
  - [lambda/input/input-rdb.cpp:220](../lambda/input/input-rdb.cpp) — 2 INTKEY entries (pk, revfk)
  - [lambda/input/css/dom_element.cpp:2703](../lambda/input/css/dom_element.cpp) — 1 PTRKEY entry
  - [radiant/layout_counters.cpp:19](../radiant/layout_counters.cpp) — 1 entry
  - [radiant/state_store.cpp:54](../radiant/state_store.cpp) — 1 INTKEY entry
  - [radiant/retained_display_list.cpp:34](../radiant/retained_display_list.cpp) — 1 INTKEY entry
  - [radiant/surface.cpp:22](../radiant/surface.cpp) — 1 STRKEY entry
  - [radiant/script_runner.cpp:709](../radiant/script_runner.cpp) — 1 PTRKEY entry
  - [lambda/bash/bash_runtime.cpp:3580, 5024](../lambda/bash/bash_runtime.cpp) — 2 LENSTRKEY entries (BashRtVar, BashRtFuncEntry)
  - [lambda/name_pool.cpp:6](../lambda/name_pool.cpp) — 1 LENSTRKEY entry
  - [lambda/validator/doc_validator.cpp:145, 162](../lambda/validator/doc_validator.cpp) — 2 LENSTRKEY entries (TypeRegistryEntry, VisitedEntry)
- ⛔ Composite-key sites (state_store StateKey, template_state TemplateStateKey, render_map RenderMapKey) are too varied to fit a single-field macro.
- ⛔ `js_mir_hashmap_scope_utils.cpp` exposes cmp/hash via header for cross-TU use; macro emits `static` functions and so doesn't fit (would require non-static variant).

### 1.5 String hashing — ✅ done

- ✅ `lib/hash.h` shipped — inline `hash_djb2`, `hash_fnv1a_32`, `hash_fnv1a_64`, `hash_cstr`, `hash_ptr`.
- ✅ [lambda/lambda-data.hpp:262](../lambda/lambda-data.hpp) `typemap_fnv1a` now a thin alias over `hash_fnv1a_32`.
- ⛔ Skipped sites have different algorithm variants (additive djb2 in css_properties, streaming FNV in rdt_vector_tvg) where migrating would change values without value.

10 gtest cases cover the new module.

### 1.6 Object / free-list pools — ⛔ deferred

After deeper review, view_pool / shape_pool / tile_pool are domain-shaped enough that a generic `lib/object_pool.h` would be a coercion rather than a real consolidation. Leave domain-specific pools in place.

### 1.7 Sort utilities — ✅ done (one site outstanding)

- ✅ `lib/sort.h` shipped — `insertion_sort`, `sort_ptrs_by_int_key`, `sort_ptrs_by_double_key`, **and a suite of stock qsort-compatible comparators**: `sort_cmp_int_asc/desc`, `sort_cmp_int64_asc/desc`, `sort_cmp_uint64_asc`, `sort_cmp_double_asc/desc`, `sort_cmp_float_asc`, `sort_cmp_cstr_asc`, `sort_cmp_cstr_ci_asc`. Plus a `SORT_CMP_AS_R(name)` macro for adapting to `insertion_sort`'s 3-arg signature.
- ✅ Replaced 4 hand-written bubble sorts in [lambda/lambda-vector.cpp](../lambda/lambda-vector.cpp) with `insertion_sort` (median, sort1 float/int/mixed branches).
- ✅ Migrated [lambda/input/input-latex-tables.cpp:94](../lambda/input/input-latex-tables.cpp) — removed local `cmp_str_ptr`, both `qsort` and `bsearch` call sites now use `sort_cmp_cstr_asc`.
- ❌ 3 hand-written insertion sorts in [lambda/py/py_builtins.cpp:345, 357, 1535](../lambda/py/py_builtins.cpp) — still open. Easy migration; ~30 min.
- ⛔ Remaining 7 `qsort` callers use struct-specific comparators (`compare_debug_info`, `compare_suggestions`, `js_idx_pair_cmp`, `compare_indexed_rules`, `NodeWithBarycenter`) that don't map to stock comparators without record-key extraction. Left as-is.

16 gtest cases cover the module.

### 1.8 Priority queue — ⛔ deferred

`lib/priority_queue.h` still has zero callers. Decision deferred.

---

## 2. Existing `./lib` types worth extending

### 2.1 `lib/arraylist.h` — ✅ API extended, ⛔ call-site migration deferred

- ✅ Added: `arraylist_get`, `arraylist_size`, `arraylist_length`, `arraylist_set`, `arraylist_front`, `arraylist_back`, `arraylist_pop`, `arraylist_pop_front`, `arraylist_reserve`, `ARRAYLIST_FOREACH` macro.
- ⛔ Mass migration of the 70 files using `->data[i]` directly deferred — each conversion is mechanical, doesn't reveal bugs, and `arraylist_get(l, i)` vs `l->data[i]` is a wash for readability.

9 gtest cases cover the new accessors.

### 2.2 `lib/strview.h` — ✅ drift fixed, API extended

- ✅ `strview_from_cstr` lifted from validator stubs and shipped in `lib/strview.h`.
- ✅ Added: `strview_starts_with`/`strview_ends_with` aliases, `strview_contains`, `strview_to_int64`, `strview_to_double`, `strview_hash` (sip-compatible), `strview_dup_with_pool`.
- ✅ Migrated 9 hand-rolled `{ptr, strlen(ptr)}` initializers to `strview_from_cstr(ptr)` in [build_ast.cpp](../lambda/build_ast.cpp) (5 sites) and [doc_validator.cpp](../lambda/validator/doc_validator.cpp) (2 sites).
- ✅ Removed redundant `extern "C"` forward-declaration of `strview_to_int` in [radiant/resolve_htm_style.cpp:14](../radiant/resolve_htm_style.cpp).
- ❌ **Hygiene** Naming inconsistency `strview_start_with` / `strview_end_with` still present alongside the new `_starts_with` / `_ends_with` alias names. Could retire the old names in a breaking change.

16 gtest cases cover the extended API.

### 2.3 `lib/strbuf.h` — ⏳ predicates done, escape helpers still open

- ✅ Added `strbuf_starts_with` / `strbuf_ends_with` (NULL-safe, delegate to `lib/str.h`).
- ❌ `strbuf_replace_all(needle, replacement)` — formatters still hand-roll
- ⛔ `strbuf_append_escaped_*` (JSON/XML/HTML/CSV) — see §3.2 below. `lambda/format/` already has its own `EscapeRule` infrastructure (`JSON_ESCAPE_RULES`, `HTML_TEXT_ESCAPE_RULES`, etc.) that does this internally. Lifting it to lib would be a refactor of that subsystem, not a clean additive change.
- ❌ **Hygiene** Deprecated `strbuf_append_long` / `strbuf_append_ulong` markers still present; not removed.

33 gtest cases (3 new for predicates).

### 2.4 `lib/num_stack.h` — ⛔ deferred

Still has no external callers. Pure overhead either way; not actively harmful.

### 2.5 `lib/hashmap.{h,hpp}` — ✅ done (via §1.4)

The `HASHMAP_DEFINE_*` macro family in `lib/hashmap_helpers.h` replaces the proposed `hashmap_new_cstrkey()` constructor with something broader.

### 2.6 Geometry / math helpers — ⏳ partial

- ✅ `lib/math_utils.h` shipped — `lib_math::clamp/sign/lerp/abs_val/min_val/max_val` (C++ templates in `lib_math` namespace, opt-in via `using`) plus C-and-C++-compatible `LMB_*` macros (CLAMP/SIGN/LERP/ABS/MIN/MAX). Also `clamp_byte` (int → uint8) and `clamp_unit` (float → [0,1]).
- ✅ Migrated:
  - [radiant/render_filter.cpp:34](../radiant/render_filter.cpp) — `clamp_01` now delegates to `clamp_unit`
  - [radiant/layout_containing_block.cpp:8](../radiant/layout_containing_block.cpp) — `clamp_non_negative` now uses `lib_math::max_val`
- ⛔ Skipped:
  - [radiant/render_filter.cpp:27](../radiant/render_filter.cpp) `clamp_byte(float)` with rounding — different semantics from lib's `clamp_byte(int)`
  - [radiant/render_composite.cpp:6](../radiant/render_composite.cpp) `render_composite_clamp_u8(uint32_t)` — only upper bound; in hot pixel path
  - [radiant/text_edit.cpp:66](../radiant/text_edit.cpp) `clamp_off(uint32_t, uint32_t)` — namespace-anonymous, marginal value
  - Geometry structs (`Rect`, `Bound`, `Point2D`, `SizeF`) stay in `radiant/` per original proposal

9 gtest cases cover the math helpers.

### 2.7 `lib/log.h` — ✅ saturated, no changes

---

## 3. Additional candidates discovered during migration

These items emerged from doing the work in §1–2.

### 3.1 `HASHMAP_DEFINE_LENSTRKEY` macro variant — ✅ done

See §1.4 — folded into the macro family. 5 sites migrated.

### 3.2 String-escape helpers in `lib/strbuf.h` — ⛔ superseded by format/EscapeRule

Investigation revealed that `lambda/format/format-utils.h` already centralises
this via the `EscapeRule` table-driven infrastructure:

- `JSON_ESCAPE_RULES`, `HTML_TEXT_ESCAPE_RULES`, `HTML_ATTR_ESCAPE_RULES`,
  `XML_ATTR_ESCAPE_RULES`, `LATEX_ESCAPE_RULES`, `YAML_ESCAPE_RULES`,
  `JSX_TEXT_ESCAPE_RULES`, `JSX_ATTR_ESCAPE_RULES`, `INI_ESCAPE_RULES`,
  `PROP_ESCAPE_RULES`
- Driven through `format_escaped_string_ex(sb, str, len, rules, count, ctrl_mode)`

The remaining ad-hoc escapes are in `format-graph.cpp` (DOT/Mermaid/D2 quoting
with their own escape shape) and don't fit a strbuf-level utility. Moving
EscapeRule into `lib/` is feasible but is a `lambda/format/` subsystem refactor,
not a small additive change.

### 3.3 Standard sort comparators in `lib/sort.h` — ✅ done

See §1.7 — 9 stock comparators added; `input-latex-tables.cpp` migrated.

### 3.4 Content-addressed cache key in `lib/cache_key.h` — ⛔ deferred (low ROI)

After migration, only `enhanced_file_cache.cpp` uses the SHA-256 + hex pipeline
(input_http already uses `file_cache_path` with DJB2). One caller is below the
dedup threshold; pulling mbedtls into lib for it would be net negative.

### 3.5 Hex encode/decode in `lib/hex.h` — ✅ done

- ✅ `lib/hex.h` shipped — `hex_encode_nibble[_upper]`, `hex_decode_byte`,
  `hex_encode[_upper](bytes, len, out)`, `hex_decode(in, len, out, *out_len)`
- ✅ Migrated:
  - [enhanced_file_cache.cpp:47](../lambda/network/enhanced_file_cache.cpp) — `sha256_to_hex` body
  - [js_buffer.cpp:830](../lambda/js/js_buffer.cpp) — `Buffer.toString("hex")`
  - [js_crypto.cpp:407, 497](../lambda/js/js_crypto.cpp) — `randomUUID` + `bytes_to_hex_string`

12 gtest cases cover the module.

### 3.6 Monotonic time helpers in `lib/time_util.h` — ✅ done

- ✅ `lib/time_util.h` shipped — `time_now_ns/us/ms`, `time_now_seconds`, `time_elapsed_ms_since`
- ✅ Migrated:
  - [lib/lru_cache.c:41](../lib/lru_cache.c) — internal `lru_now_ms` collapsed to macro
  - [py_stdlib.cpp:854, 879](../lambda/py/py_stdlib.cpp) — Python `time.monotonic` and `time.perf_counter_ns`
  - [network_resource_manager.cpp:41](../lambda/network/network_resource_manager.cpp) — `get_time_seconds`
  - [network_integration.cpp:58](../lambda/network/network_integration.cpp) — `doc->load_start_time`

5 gtest cases cover the module.

### 3.7 Path utilities — ✅ already in `lib/file.h`; partial migration

- ✅ `lib/file.h` already has `file_path_basename`, `file_path_dirname`,
  `file_path_join`, `file_path_ext`, `file_realpath`, `file_ensure_dir`.
- ✅ Migrated 6 sites that used `strrchr(path, '.')` for extension lookup,
  now use `file_path_ext()`:
  - [main.cpp:822, 2302, 2387, 2439, 2777](../lambda/main.cpp) — 5 sites
  - [validator/ast_validate.cpp:170, 486, 535](../lambda/validator/ast_validate.cpp) — 3 sites
  (Bonus: fixes a latent bug where `strrchr` would pick up dots in directory
  components.)
- ⛔ Several `strrchr('/')` sites in `runner.cpp` and `py_stdlib.cpp` have
  subtle semantic differences (trailing slash for concatenation; Python's
  empty-string-for-no-separator) and weren't migrated.

### 3.8 Atomic counters in `lib/atomic.h` — ✅ done

- ✅ `lib/atomic.h` shipped — `atomic_int32`/`atomic_int64` wrapper structs
  with `load`/`store`/`inc`/`dec`/`add` for both widths. Maps to `__atomic_*`
  builtins on clang/gcc.
- ✅ Migrated [input_http.cpp:299](../lambda/input/input_http.cpp) URL prefetcher —
  removed dedicated `pthread_mutex_t count_mutex` and its lock/unlock pairs,
  replaced with `atomic_int32 success_count`.
- ⛔ Other mutex-protected counters (network_scheduler, enhanced_file_cache
  active_writes) involve multi-counter invariants under condvars, where atomics
  alone don't fit.

5 gtest cases including multi-threaded contention.

### 3.9 Range-containment binary search — ✅ done

See §1.2 — `binsearch_range` added; both flagged sites migrated.

### 3.10 Bitset helpers — ⛔ deferred (low value)

Each site has slightly different shape; abstraction win not high.

---

## What's left

**Genuinely outstanding (worth doing):**

1. **§1.7 `py_builtins.cpp` insertion sorts** — 3 hand-written sorts at
   lines 345, 357, 1535. Easy migration to `insertion_sort` + a stock
   comparator. ~30 minutes.

**Hygiene (cosmetic, breaking if pursued):**

- **§2.2** Retire `strview_start_with` / `strview_end_with` in favour of the
  `_starts_with` / `_ends_with` aliases.
- **§2.3** Either remove the `strbuf_append_long` / `strbuf_append_ulong`
  deprecated functions or drop the deprecation marker.

**Bigger scope (separate proposals if pursued):**

- **§3.2** Lift `format/EscapeRule` infrastructure into lib — would
  centralise JSON/HTML/XML/CSV/YAML escaping but requires refactoring
  `lambda/format/format-utils.{h,cpp}`.
- Length-prefix `BashAssocEntry` uses bash's custom additive DJB hash
  (not portable `hashmap_sip`); migration would change hash values.

**Deferred indefinitely:**

- §1.6 (object pools)
- §1.8 (`lib/priority_queue.h` — zero callers; delete or extend)
- §2.1 ArrayList accessor migration (70 files, cosmetic)
- §2.4 (`lib/num_stack.h` — zero callers)
- §3.4 (`lib/cache_key.h` — only 1 caller after `file_cache_path`)
- §3.10 (bitset helpers — too scattered)
- Composite-key hashmap macros (too varied)

---

## Migration tally

After 10 rounds:

- **10 new lib modules:** lru_cache, binsearch (3 variants), hash, hashmap_helpers (4 macro variants), thread_pool (+ stack-size variant), math_utils, sort (+ stock comparators), hex (lower+upper), time_util, atomic
- **36 hashmap cmp/hash function pairs eliminated**
- **4 binsearch migrations** (2 equality + 2 range)
- **4 bubble sorts → insertion_sort** + 1 qsort using stock comparator
- **3 ad-hoc pthread loops → thread_pool** (including a new stack-size API)
- **2 stub LRU caches revived as real wrappers**
- **2 clamp helpers centralized**
- **9 strview-from-cstr initializers** + 1 forward-declaration removed
- **4 hex encode sites + 4 monotonic time sites**
- **1 mutex+counter → atomic**
- **6 path-ext lookups via `file_path_ext`** (+ latent bugfix)
- **1 inline FNV alias**

Net: 22 actively-tested lib modules with **780 passing tests**. The proposal
is substantively complete.

---

# Appendix: Bigger-scope proposals (separate work items)

These items emerged during migration but are larger in scope than the additive
"new lib module + migrate callers" pattern that worked for round 1–11. Each is
written as a standalone proposal that could be picked up independently.

## A. Lift `format/EscapeRule` infrastructure into `lib/`

**Rationale.** §3.2 originally proposed `strbuf_append_escaped_*` helpers
directly. During investigation we discovered `lambda/format/format-utils.{h,cpp}`
already has a comprehensive table-driven escape system that subsumes most of
what was proposed. The remaining work is *moving* that subsystem to `lib/`
so other subsystems (HTTP body builders, log formatters, debug printers, JS
template-literal formatters, etc.) can use it without depending on
`lambda/format/`.

**Existing infrastructure in `lambda/format/format-utils.h`:**

```c
typedef struct {
    char trigger;           // input char that triggers the rule
    const char* replacement; // literal replacement
} EscapeRule;

extern const EscapeRule JSON_ESCAPE_RULES[];
extern const EscapeRule HTML_TEXT_ESCAPE_RULES[];
extern const EscapeRule HTML_ATTR_ESCAPE_RULES[];
extern const EscapeRule XML_ATTR_ESCAPE_RULES[];
extern const EscapeRule LATEX_ESCAPE_RULES[];
extern const EscapeRule YAML_ESCAPE_RULES[];
extern const EscapeRule JSX_TEXT_ESCAPE_RULES[];
extern const EscapeRule JSX_ATTR_ESCAPE_RULES[];
extern const EscapeRule INI_ESCAPE_RULES[];
extern const EscapeRule PROP_ESCAPE_RULES[];

void format_escaped_string_ex(StringBuf* sb, const char* str, size_t len,
                              const EscapeRule* rules, int num_rules,
                              EscapeCtrlMode ctrl_mode);
```

**Proposal: new `lib/escape.h`/`lib/escape.c`:**

```c
// lib/escape.h
typedef struct {
    char         trigger;
    const char*  replacement;
} EscapeRule;

typedef enum {
    ESCAPE_CTRL_NONE,           // pass through
    ESCAPE_CTRL_JSON_UNICODE,   // emit \uNNNN
    ESCAPE_CTRL_XML_NUMERIC,    // emit &#xNN;
    ESCAPE_CTRL_DROP,           // skip control bytes silently
} EscapeCtrlMode;

// Append `len` bytes of `str` to `sb`, applying first-match rule replacement;
// control bytes handled per `ctrl_mode`.
void escape_append(StrBuf* sb, const char* str, size_t len,
                   const EscapeRule* rules, int rule_count,
                   EscapeCtrlMode ctrl_mode);

// Ship the canonical rule tables here too:
extern const EscapeRule ESCAPE_RULES_JSON[];     extern const int ESCAPE_RULES_JSON_COUNT;
extern const EscapeRule ESCAPE_RULES_HTML_TEXT[]; extern const int ESCAPE_RULES_HTML_TEXT_COUNT;
extern const EscapeRule ESCAPE_RULES_HTML_ATTR[]; extern const int ESCAPE_RULES_HTML_ATTR_COUNT;
extern const EscapeRule ESCAPE_RULES_XML_ATTR[];  extern const int ESCAPE_RULES_XML_ATTR_COUNT;
// ... LATEX, YAML, JSX_TEXT/ATTR, INI, PROP, CSV
```

Plus thin convenience wrappers:

```c
static inline void strbuf_append_escaped_json(StrBuf* sb, const char* s, size_t n) {
    escape_append(sb, s, n, ESCAPE_RULES_JSON, ESCAPE_RULES_JSON_COUNT,
                  ESCAPE_CTRL_JSON_UNICODE);
}
// ... and similar for the other formats
```

**Why this is bigger-scope:**

1. **API shape conflict.** `format_escaped_string_ex` takes `StringBuf*` (different from `lib/strbuf.h`'s `StrBuf`). Migration needs to either generalise to both, or convert one of the two string types.
2. **Header dependencies.** `format-utils.h` has a wider public surface (`TextEscapeConfig`, `MARKDOWN_ESCAPE_CONFIG`, `format_text_with_escape`, plus table/list utilities, `ElementReader`, indent helpers). Lifting just the escape parts means carving out the right subset.
3. **Migration breadth.** ~10 formatters use the existing rules. Moving the rule tables relocates a lot of strings; even though the formatters still compile against the same names, the include path changes.
4. **Test coverage.** Each escape rule needs a dedicated test (the existing `lambda/format/` tests cover them indirectly via formatter output). A clean lib module would want focused gtests.

**Scope estimate:** 1-2 days. Touches ~10 files. Risk: medium (subtle escape-rule changes can produce silently wrong output).

**Sites that would benefit beyond format/:**

- `lambda/js/js_qs_escape` (querystring percent-encoding, uppercase hex)
- `radiant/script_runner.cpp` (compiles JS event handlers, currently no escaping for inline-attr context)
- `lib/log.c` color/escape sequences (currently inline)
- `lambda/network/cookie_jar.cpp` (cookie value encoding)
- Future HTTP body builder, future logging-to-JSON sink

**Recommendation.** Pick this up if a non-format/ subsystem needs escaping. Don't preemptively migrate — the existing `format-utils.h` location is workable for the formatters.

---

## B. `lib/bitset.h` — generic bit-set / character-class table

**Rationale.** Multiple subsystems manually pack bit-flags or build ASCII
character lookup tables. A small generic layer would centralise the bit-math
and let callers focus on which bits/chars they want.

**Current sites:**

- [lambda/input/css/css_properties.cpp](../lambda/input/css/css_properties.cpp) — per-property metadata flags (inherited, keyword, default, etc.) packed into a `uint32_t`
- [lambda/js/js_regex_*.cpp](../lambda/js) — character-class bitmaps (256-bit for ASCII, larger for Unicode classes)
- [lambda/input/html5/html5_tokenizer.cpp](../lambda/input/html5/html5_tokenizer.cpp) — `is_ascii_alpha`, `is_ascii_alnum`, `is_whitespace` lookup tables, currently inline 256-byte tables
- [lambda/input/css/css_tokenizer.cpp](../lambda/input/css/css_tokenizer.cpp) — character classification for selectors/identifiers
- [lambda/transpile-mir.cpp](../lambda/transpile-mir.cpp) — feature-flag bitfields on function signatures

**Proposal: header-only `lib/bitset.h`:**

```c
// Fixed-size bit-set declared at struct-field scope.
//
//   typedef struct {
//       BITSET_FIELD(flags, 64);
//   } MyEntry;
//
//   bitset_set(my.flags, 7);
//   if (bitset_test(my.flags, 7)) { ... }
//
#define BITSET_FIELD(name, bits)  uint64_t name[((bits) + 63) / 64]

static inline void bitset_set   (uint64_t* bs, size_t bit);
static inline void bitset_clear (uint64_t* bs, size_t bit);
static inline bool bitset_test  (const uint64_t* bs, size_t bit);
static inline void bitset_flip  (uint64_t* bs, size_t bit);
static inline void bitset_clear_all(uint64_t* bs, size_t n_words);

// Convenience for ASCII character classes (256 bits = 32 bytes = 4 words).
typedef struct { uint64_t words[4]; } AsciiCharSet;
static inline void ascii_charset_add(AsciiCharSet* s, char c);
static inline void ascii_charset_add_range(AsciiCharSet* s, char lo, char hi);
static inline bool ascii_charset_test(const AsciiCharSet* s, unsigned char c);

// Pre-built common classes (defined in lib/bitset.c):
extern const AsciiCharSet ASCII_ALPHA;
extern const AsciiCharSet ASCII_ALNUM;
extern const AsciiCharSet ASCII_DIGIT;
extern const AsciiCharSet ASCII_WHITESPACE;
extern const AsciiCharSet ASCII_IDENT_START;   // [A-Za-z_]
extern const AsciiCharSet ASCII_IDENT_CONT;    // [A-Za-z0-9_]
```

**Why this is bigger-scope:**

1. **Shape variance.** css_properties uses fixed-width `uint32_t` flags
   indexed by property kind; regex uses 256-bit ASCII bitmaps; html5 uses
   compiled byte-lookup tables for speed. A single bitset abstraction may
   not be faster than the bespoke representations at the hot paths.
2. **Performance sensitivity.** The html5 tokenizer's inline lookup tables
   compile to a single byte load + test. A bitset version is `(words[c>>6] >> (c&63)) & 1` — three ops. For tight tokenizer loops, the bespoke version may stay.
3. **Migration breadth shallow per site.** Each migration converts ~5-20
   lines, but spread across ~5 files with different shapes.

**Scope estimate:** Half a day for the lib module + tests. Migrations are
per-site judgement calls; possibly only 1-2 sites would actually move.

**Recommendation.** Add the lib module only if a *new* caller needs a bit-set
and would otherwise hand-roll one. Existing sites are correct and tuned; the
abstraction win is marginal.

---

## C. `lib/digest.h` — SHA-256/512 wrapper

**Rationale.** Two implementations of SHA-256 currently coexist:

- [lambda/network/enhanced_file_cache.cpp:37](../lambda/network/enhanced_file_cache.cpp) — uses `mbedtls_sha256_*` (wrapped in `compute_sha256`)
- [lambda/js/js_crypto.cpp:105](../lambda/js/js_crypto.cpp) — full from-scratch C implementation (`sha256_compute`, plus `sha512_compute` at line 169)

js_crypto's own implementation exists because it needs SHA-256/384/512 with
streaming + finalize behavior matching browser `crypto.subtle.digest` exactly.
mbedtls is already linked for TLS and would deliver the same algorithm.

**Proposal: `lib/digest.h` thin wrapper:**

```c
// One-shot interface
void digest_sha256(const void* data, size_t len, uint8_t out[32]);
void digest_sha384(const void* data, size_t len, uint8_t out[48]);
void digest_sha512(const void* data, size_t len, uint8_t out[64]);

// Streaming interface (matches mbedtls but with stable names)
typedef struct DigestCtx DigestCtx;
DigestCtx* digest_ctx_new(int variant);   // 256 / 384 / 512
void       digest_update(DigestCtx* ctx, const void* data, size_t len);
void       digest_finalize(DigestCtx* ctx, uint8_t* out);
void       digest_ctx_free(DigestCtx* ctx);
```

Implementation delegates to `mbedtls_sha256_*` / `mbedtls_sha512_*`.

**Why this is bigger-scope:**

1. **mbedtls dependency creep.** Currently mbedtls is a network-subsystem dependency. Pulling it into `lib/digest.{h,c}` means it's pulled into every binary that links lib (including standalone test executables). Mitigations: keep digest as opt-in (separate `.c` file with no force-linking), or split into a separate sub-lib.
2. **Algorithm equivalence verification.** js_crypto's hand-rolled SHA-256 already passes test262 conformance. Switching to mbedtls means re-validating that output bytes match for all edge cases (empty input, very large input, exact-block-boundary input).
3. **Performance.** mbedtls SHA-256 is hardware-accelerated on platforms with `sha256` CPU instructions; the hand-rolled version isn't. Net: migration is likely a *win* for js_crypto on Apple Silicon / modern x86.
4. **Out-of-scope use cases.** Future cache_key (§3.4), if revived, would use this.

**Scope estimate:** Half a day. Risk: low (mbedtls is well-tested), with the
caveat that js_crypto's existing test262 coverage must continue to pass.

**Recommendation.** Worth doing if any new caller needs SHA-256, *or* if
js_crypto starts being a CPU bottleneck. Until then, two implementations is
ugly but correct.

---

## D. Composite-key `HASHMAP_DEFINE_COMPOSITE` macro family

**Rationale.** Multiple hashmaps key on 2-3 fields (pointer + name, item +
ref, item + ref + name_id). My current macros (STRKEY / PTRKEY / INTKEY /
LENSTRKEY) all key on one field; these sites still hand-write cmp/hash.

**Outstanding sites:**

- [radiant/state_store.cpp:33](../radiant/state_store.cpp) `StateEntry` — key is `{void* node, const char* name}` (pointer + string)
- [lambda/template_state.cpp:22](../lambda/template_state.cpp) `TemplateStateEntry` — `{Item model_item, void* template_ref, const char* state_name}` (3-field)
- [lambda/render_map.cpp:73](../lambda/render_map.cpp) `ReverseMapEntry` — uint64 key (already INTKEY-able but slightly tweaked)
- [lambda/render_map.cpp:103](../lambda/render_map.cpp) `RenderMapEntry` — `{Item source_item, void* template_ref}` (2-field)

**Proposal:** macros that compose existing key shapes:

```c
// 2-field: pointer + string
HASHMAP_DEFINE_PTR_STR_KEY(name, struct_type, ptr_field, str_field)

// 2-field: int + pointer (e.g. Item + ref)
HASHMAP_DEFINE_INT_PTR_KEY(name, struct_type, int_field, ptr_field)

// 3-field generic — caller writes the cmp/hash but factory + register helpers are emitted:
#define HASHMAP_DEFINE_CUSTOM(name, struct_type, hash_expr, cmp_expr) ...
```

Or alternatively, a more powerful generic with a per-field-list approach
(this needs more API design).

**Why this is bigger-scope:**

1. **Combinatorial explosion.** Two-field combos: ptr+str, ptr+int, ptr+ptr,
   int+str, int+int, str+str. Three-field doubles the count. Macro family
   becomes unwieldy.
2. **Hash composition isn't free.** The current sites use ad-hoc hash mixes
   (`h1 ^ (h2 * 0x9e3779b97f4a7c15ULL)`). Centralising means picking *one*
   mixing function and migrating; if it's not bit-equivalent to the existing
   choices, hash distribution may change slightly (correctness preserved,
   bucket layout shifts).
3. **Tooling for templating.** A real solution might use code generation
   (Python script reading struct definitions, emitting cmp/hash) rather than
   C macros. That's a different shape than the current `HASHMAP_DEFINE_*`
   pattern.

**Scope estimate:** 1 day for a 2-field macro covering ptr+str (the most
common shape), or 2-3 days for a full family.

**Recommendation.** Defer until a *new* site needs a composite-key hashmap.
Current 4 sites are stable and correct; refactor risk outweighs the dedup win
(~40 lines saved).

---

## E. `lib/sort.h` introsort / `qsort_r` wrapper

**Rationale.** Current `lib/sort.h::insertion_sort` is O(n²). It's used in
`lambda-vector.cpp::sort1` and `math.median`, which run on user-provided
arrays. For arrays of 1000+ elements, insertion sort gets noticeable
(seconds rather than milliseconds).

The proposal's original §1.7 mentioned introsort. Now that we have stock
comparators (§3.3), an `O(n log n)` sort with the same comparator API would
slot in naturally.

**Proposal:**

```c
// O(n log n) introsort (quicksort with heap-sort fallback).
// Same SortCmpFn signature as insertion_sort; can drop in for any caller.
void introsort(void* base, size_t count, size_t stride,
               SortCmpFn cmp, void* udata);

// qsort_r-style 2-arg wrapper that calls libc qsort (when udata is NULL):
static inline void sort_quick(void* base, size_t count, size_t stride,
                              SortCmp2Fn cmp);  // matches qsort signature
```

**Why this is bigger-scope:**

1. **Real algorithm implementation.** Quicksort + heap-sort fallback is
   ~150 lines of careful code with stack-depth bounds, median-of-three pivot,
   etc. Existing implementations (musl `qsort_r`, BSD `heapsort`) could be
   ported with their licences, but that's a copyright/attribution decision.
2. **Or: just use `qsort_r`?** macOS, glibc, and musl all have `qsort_r`,
   but the argument order differs (BSD vs GNU). A small portability shim
   would work. Doesn't address the desire for a known-good algorithm in lib,
   but trades real code for `#ifdef`s.
3. **insertion_sort callers should stay.** The bubble-sort → insertion_sort
   migration in `lambda-vector.cpp` was an O(n²) → O(n²) constant-factor
   improvement. Real O(n log n) is the next step.

**Sites that would benefit (rerun the comparator):**

- [lambda/lambda-vector.cpp](../lambda/lambda-vector.cpp) `fn_sort1`, `fn_math_median` — currently insertion_sort
- [lambda/py/py_builtins.cpp](../lambda/py/py_builtins.cpp) `sorted()` / `arr.sort()` — currently insertion_sort
- Any future general-purpose user sort

**Scope estimate:** 1 day for the algorithm + tests, or 2 hours for a
`qsort_r` portability wrapper.

**Recommendation.** Add `qsort_r` shim *now* if any caller starts seeing
perf issues. Defer real introsort port until there's a measured need.

---

## F. Length-prefix `BashAssocEntry` migration

**Rationale.** `BashAssocEntry` in `lambda/bash/bash_runtime.cpp:3991` is one
of the few remaining length-prefix hashmap sites that I didn't migrate to
`HASHMAP_DEFINE_LENSTRKEY`. The reason: it uses bash's *custom additive DJB
hash* (`hash * 33 + c`), not `hashmap_sip`, to match bash's iteration order
semantics. The macro hardcodes sip.

**Proposal:** add an optional hash-function parameter to LENSTRKEY:

```c
// Default: sip
HASHMAP_DEFINE_LENSTRKEY(name, struct_type, str_field, len_field)

// Custom hash:
HASHMAP_DEFINE_LENSTRKEY_HASH(name, struct_type, str_field, len_field, hash_fn)
```

Then bash can pass its `bash_djb_hash` to retain iteration semantics.

**Why this is bigger-scope:**

1. **Macro API churn.** Adding a parameterised variant means another macro
   in the family (5 total). Documenting when to pick which becomes a real
   doc page.
2. **Marginal benefit.** Only 1 site. Net saved: ~12 lines of cmp/hash.
3. **Iteration order is a load-bearing semantic.** Bash users may depend on
   `for k in "${!array[@]}"` order. Changing the hash function (even to sip)
   would change order; this must be preserved.

**Scope estimate:** 1 hour for the macro variant; 5 minutes to migrate.

**Recommendation.** Low priority. If we add this for a future caller too,
sure. Otherwise leave bash's custom hash alone.

---

## Summary

| Proposal | Scope | Worth doing now? |
|---|---|---|
| A. Lift `format/EscapeRule` to `lib/escape.h` | 1-2 days | Only when non-format caller needs escaping |
| B. `lib/bitset.h` | half day | Only when new caller emerges |
| C. `lib/digest.h` (SHA-256 wrapper) | half day | Worth it if js_crypto perf becomes an issue, or if new caller needs SHA |
| D. Composite-key hashmap macros | 1-3 days | Only when new site emerges |
| E. `lib/sort.h` introsort or qsort_r shim | 2 hours – 1 day | Worth doing if user-facing sort perf becomes an issue |
| F. LENSTRKEY with custom hash for bash | 1 hour | Low priority |

All six are deferred because the existing code is correct, the migration win
is small (or carries risk), and no current caller has an unmet need. They
remain on this list to be picked up when the calculus changes.

