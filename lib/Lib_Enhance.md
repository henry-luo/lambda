# `./lib` Enhancement Proposal

Analysis of `lambda/` and `radiant/` codebases for opportunities to centralize
common data structures / algorithms under `./lib`, and to extend existing
`./lib` types with the operations callers actually need.

The findings below are ordered by impact. Each item names concrete call sites
so the scope of the change is easy to verify.

---

## 1. Common data structures / algorithms to centralize

### 1.1 LRU cache — highest impact (4+ hand-rolled copies)

The same pattern (doubly-linked list + `lru_head`/`lru_tail` + size/entry
bookkeeping + `lru_remove`/`lru_insert_front`/`lru_touch`) is reimplemented
across the tree:

- [lambda/network/enhanced_file_cache.cpp:69](../lambda/network/enhanced_file_cache.cpp) — full LRU helpers (`lru_remove`, `lru_insert_front`, `lru_touch`)
- [lambda/input/input_pool.cpp:24](../lambda/input/input_pool.cpp) — `InputCacheEntry` with `prev`/`next`, `lru_head`/`lru_tail` (stubbed TODO)
- [lambda/input/input_file_cache.cpp:13](../lambda/input/input_file_cache.cpp) — `FileCacheEntry` (stubbed TODO)
- [lambda/input/input_sysinfo.cpp:34](../lambda/input/input_sysinfo.cpp) — `SysInfoCacheEntry`
- [lambda/js/js_dom.cpp:536](../lambda/js/js_dom.cpp) — `DomWrapperCacheEntry` (small fixed-array variant of the same pattern)
- [lambda/sysinfo.cpp:64](../lambda/sysinfo.cpp) — `SysinfoCache` (TTL-cached items, no LRU)

**Proposal:** add `lib/lru_cache.{h,c}` — a hash-keyed LRU supporting:

- max entries and/or max byte budget
- optional TTL per entry
- eviction callback (`void (*on_evict)(void* entry, void* udata)`)
- O(1) `get` / `put` / `touch` / `delete`

Composes cleanly on top of the existing `lib/hashmap.{h,c}` plus the inline
doubly-linked list pattern. The two TODO-stubbed caches in `lambda/input/`
become trivial wrappers around it.

### 1.2 Sorted-array + binary-search lookup

Reimplemented at minimum three times with the same shape:

- [lambda/format/html-defs.cpp:45](../lambda/format/html-defs.cpp) — `lookup(const char* table[], int count, ...)` using `str_icmp`
- [radiant/layout_text.cpp:177](../radiant/layout_text.cpp) — `lookup_full_case` over a sorted table
- [lambda/lambda-error.cpp:215](../lambda/lambda-error.cpp) — debug-info address lookup
- [lambda/js/js_runtime.cpp:12062](../lambda/js/js_runtime.cpp), [lambda/js/js_runtime.cpp:14252](../lambda/js/js_runtime.cpp) — open-coded `lo`/`hi` loops
- `qsort`/`bsearch` callers in [lambda/input/input-latex-tables.cpp:106](../lambda/input/input-latex-tables.cpp), [lambda/input/input-latex-ts.cpp:125](../lambda/input/input-latex-ts.cpp)

**Proposal:** small header-only `lib/binsearch.h` with:

- `binsearch_strtab(const char* const* table, int count, const char* key, size_t key_len, bool case_insensitive)`
- generic `void*` variant for arbitrary record arrays

### 1.3 Thread pool

Two real implementations plus several ad-hoc `pthread_create` sites:

- [lambda/network/network_thread_pool.h](../lambda/network/network_thread_pool.h) — libuv-backed, priority hint
- [radiant/tile_pool.cpp:202](../radiant/tile_pool.cpp) — pthread mutex/cond worker pool
- [lambda/runner.cpp:964](../lambda/runner.cpp), [lambda/js/js_mir_module_batch_lowering.cpp:5435](../lambda/js/js_mir_module_batch_lowering.cpp), [lambda/input/input_http.cpp:376](../lambda/input/input_http.cpp) — ad-hoc `pthread_create` arrays for one-off parallel work

**Proposal:** extract `lib/thread_pool.{h,c}` (pthread + condvar primary,
libuv backend optional). Keep the priority hint and `wait_all` semantics
from the network variant. Both existing pools become thin wrappers; ad-hoc
sites get a real API instead of bespoke `pthread_create` loops.

### 1.4 Compare-function boilerplate for `hashmap_new`

Every hashmap user writes its own `*_cmp` and `*_hash` — 25+ instances of
near-identical code that compares one `const char*` field at a fixed offset:

- 8 in [lambda/transpile-mir.cpp:252](../lambda/transpile-mir.cpp) (`import_cache`, `var_scope`, `local_func`, `native_func`, `global_var`, `infer_cache`, …)
- 4 in [lambda/py/transpile_py_mir.cpp:224](../lambda/py/transpile_py_mir.cpp)
- 3 in [lambda/bash/transpile_bash_mir.cpp:54](../lambda/bash/transpile_bash_mir.cpp)
- 3 in [lambda/rb/transpile_rb_mir.cpp:160](../lambda/rb/transpile_rb_mir.cpp)
- More in [lambda/ts/ts_type_builder.cpp:280](../lambda/ts/ts_type_builder.cpp), [lambda/js/js_early_errors.cpp:403](../lambda/js/js_early_errors.cpp), [lambda/bash/bash_runtime.cpp](../lambda/bash/bash_runtime.cpp), …

**Proposal:** ship `lib/hashmap_helpers.{h,c}`:

- `hashmap_cmp_cstr_at_offset(off)` / `hashmap_hash_cstr_at_offset(off)`
- `hashmap_cmp_ptr`, `hashmap_cmp_int_at_offset(off)`
- `HASHMAP_DEFINE_STRKEY(name, struct_type, key_field)` macro that emits
  both functions plus a `hashmap_new_<name>()` factory

Erases several hundred lines of repetition and reduces the risk of subtle
inconsistencies between sibling comparators.

### 1.5 String hashing — multiple djb2 / FNV / custom variants

`lib/hashmap.h` already exposes `hashmap_sip`, `hashmap_murmur`,
`hashmap_xxhash3`, but code keeps rolling its own:

- [lambda/input/css/css_properties.cpp:635](../lambda/input/css/css_properties.cpp) — djb2 (`hash * 33 + c`)
- [lambda/lambda-data.hpp:263](../lambda/lambda-data.hpp) — `typemap_fnv1a`
- [radiant/state_store.cpp:3291](../radiant/state_store.cpp) — `url_hash_func`
- [radiant/rdt_vector_tvg.cpp:162](../radiant/rdt_vector_tvg.cpp) — `rdt_picture_data_hash`

**Proposal:** small `lib/hash.h` with inline `hash_djb2`, `hash_fnv1a_32`,
`hash_fnv1a_64`, `hash_cstr`. Existing local copies become one-liners or get
deleted in favor of `hashmap_xxhash3` for bulk-data cases.

### 1.6 Object / free-list pools

Multiple pool variants exist, all with subtly different scopes:

- `lib/mempool.h` (arena), `lib/scratch_arena.h`, `lib/arena.h`
- [radiant/view_pool.cpp:349](../radiant/view_pool.cpp) — view tree pool
- [lambda/shape_pool.cpp](../lambda/shape_pool.cpp) — shape pool
- [radiant/tile_pool.h](../radiant/tile_pool.h) — render tile pool

Each wants the same "fixed-size object freelist over an arena" primitive.

**Proposal:** `lib/object_pool.h` (template/macro), providing
`pool_alloc<T>` / `pool_free<T>` over an existing `Pool*`. Not urgent —
domain-specific pools can stay — but bookkeeping is duplicated.

### 1.7 Sort utilities (including insertion sort for small N)

- 9 distinct `qsort` call sites with custom comparators ([lambda/mir.c:511](../lambda/mir.c), [lambda/input/input-latex-tables.cpp:106](../lambda/input/input-latex-tables.cpp), [lambda/validator/suggestions.cpp:151](../lambda/validator/suggestions.cpp), [lambda/js/js_globals.cpp:6836](../lambda/js/js_globals.cpp), [lambda/bash/bash_runtime.cpp:5347](../lambda/bash/bash_runtime.cpp), [radiant/graph_dagre.cpp:321](../radiant/graph_dagre.cpp), [radiant/cmd_layout.cpp:2152](../radiant/cmd_layout.cpp), [radiant/grid_sizing_algorithm.hpp:531](../radiant/grid_sizing_algorithm.hpp))
- 3 hand-written insertion sorts in [lambda/py/py_builtins.cpp:345](../lambda/py/py_builtins.cpp), [py_builtins.cpp:357](../lambda/py/py_builtins.cpp), [py_builtins.cpp:1535](../lambda/py/py_builtins.cpp)
- 2 bubble sorts in [lambda/lambda-vector.cpp:993](../lambda/lambda-vector.cpp) and [lambda-vector.cpp:1710](../lambda/lambda-vector.cpp) — marked "can optimize later"

**Proposal:** `lib/sort.h` with `insertion_sort`, `introsort`, and a
`sort_by_key` macro. `lambda-vector.cpp` drops the bubble sorts;
`py_builtins.cpp` drops the insertion-sort copies; `qsort` callers stay
valid but gain a faster non-recursive option.

### 1.8 Priority queue — currently unused, needs a reason to exist

`lib/priority_queue.h` exists but has **zero callers** anywhere in the
codebase. Open-coded heap-like patterns exist in
[radiant/grid_sizing_algorithm.hpp:531](../radiant/grid_sizing_algorithm.hpp) and the network scheduler.

**Proposal:** either delete the unused file, or extend it (key-keyed
lookup, `decrease_priority`, generic-typed elements) so it becomes
attractive enough for callers to actually pick it up.

---

## 2. Existing `./lib` types worth extending

### 2.1 `lib/arraylist.h` — most usage bypasses the API

70 files use `ArrayList`. Grepping for the documented accessors
(`arraylist_get`, `arraylist_size`, `arraylist_length`, `arraylist_set`,
`arraylist_at`, `arraylist_pop`) returns **zero hits**. Instead, callers
read `->data[i]` and `->length` directly — examples:

- [lambda/input/parse_error.cpp:19](../lambda/input/parse_error.cpp)
- [lambda/input/input-dir.cpp:92](../lambda/input/input-dir.cpp)
- [lambda/input/markup/block/block_table.cpp:259](../lambda/input/markup/block/block_table.cpp)
- [lambda/input/input.cpp:941](../lambda/input/input.cpp)
- [lambda/input/css/selector_matcher.cpp:398](../lambda/input/css/selector_matcher.cpp)

**Missing C API for the most-used operations:**

- `arraylist_get(list, i)` — replaces 25+ `->data[i]` casts
- `arraylist_size(list)` / `arraylist_length(list)`
- `arraylist_pop(list)` / `arraylist_pop_front(list)`
- `arraylist_back(list)` / `arraylist_front(list)`
- `arraylist_reserve(list, n)` (capacity is currently behind the private `_alloced`)
- `ARRAYLIST_FOREACH(list, type, var) { ... }` iteration macro

The C++ `arraylist.hpp` already has all of these (`begin`/`end`/`push_back`/etc.);
the C API just needs to catch up.

### 2.2 `lib/strview.h` — header out of sync with usage

- `strview_from_cstr` is declared/defined privately in
  [lambda/validator/ast_validate.cpp:35](../lambda/validator/ast_validate.cpp) and used in
  [lambda/validator/error_reporting.cpp](../lambda/validator/error_reporting.cpp),
  but **not exposed in `lib/strview.h`**. The header has
  `strview_from_str` as a macro — pick one name and lift the function to `lib/`.
- Naming inconsistency: `strview_start_with` / `strview_end_with` use
  singular form; conventional is `starts_with` / `ends_with`. Decide and
  apply consistently.
- Missing operations despite obvious sites:
  - `strview_contains`, `strview_split` (open-coded in markup parsers)
  - `strview_to_int64`, `strview_to_double` (only `strview_to_int` exists)
  - `strview_hash` (so it interops with `hashmap.h`)
  - `strview_dup_with_pool(Pool*)` — `strview_to_cstr` uses malloc, but
    most callers want pool allocation

### 2.3 `lib/strbuf.h` — saturated, but a few gaps

Usage is well-balanced. `strbuf_append_str` is hit 1720×, `append_char` 878×,
`append_format` 388×. Gaps that callers work around inline:

- `strbuf_starts_with` / `strbuf_ends_with`
- `strbuf_replace_all(needle, replacement)` — open-coded loops in formatters
- `strbuf_append_escaped_json` / `strbuf_append_escaped_html` — every
  formatter has its own escape loop

Also: `strbuf_append_long` / `strbuf_append_ulong` are marked deprecated
in the header but never removed. Either remove them or drop the
deprecated marker.

### 2.4 `lib/num_stack.h` — appears to be dead code

Grep for `num_stack_*` returns no external callers. Either delete it, or
document the use case (REPL? tests?) in the header so it stops looking
like an unused module.

### 2.5 `lib/hashmap.{h,hpp}` — needs friendlier wrapper

The raw `hashmap_new` signature requires writing custom
hash + compare + free callbacks every time, which is exactly what drives
the 25+ duplicated `*_cmp` / `*_hash` functions in §1.4. A
`hashmap_new_cstrkey()` convenience constructor (key = a `const char*` at
a fixed offset) would replace most of them. Pairs naturally with §1.4.

### 2.6 Geometry / math helpers worth lifting from `radiant/`

`Rect`, `Bound`, `Point2D`, `SizeF` are defined in
[radiant/view.hpp:334](../radiant/view.hpp) and
[radiant/graph_layout_types.hpp:53](../radiant/graph_layout_types.hpp).
The very useful `clamp` / `sign` / `lerp` / `abs` templates live at
[radiant/view.hpp:54](../radiant/view.hpp).

Many non-radiant places redefine clamp inline:

- [radiant/render_filter.cpp:27](../radiant/render_filter.cpp) — `clamp_byte`, `clamp_01`
- [radiant/render_composite.cpp:6](../radiant/render_composite.cpp) — `render_composite_clamp_u8`
- [radiant/text_edit.cpp:66](../radiant/text_edit.cpp) — `clamp_off`
- [radiant/layout_containing_block.cpp:8](../radiant/layout_containing_block.cpp) — `clamp_non_negative`
- [lambda/js/js_runtime.cpp:18773](../lambda/js/js_runtime.cpp) — `js_string_clamp_integer`

**Proposal:** lift `clamp`, `sign`, `lerp`, `abs` to `lib/math_utils.h`
(C-and-C++-compatible). Geometry structs can stay in `radiant/` unless
`lambda/` starts needing them.

### 2.7 `lib/log.h` — well-saturated

8205 `log_debug` / `log_info` / `log_warn` / `log_error` calls across the
tree. No extensions suggested.

---

## Suggested priority

1. **`lib/lru_cache.{h,c}`** — eliminates the most duplication and the
   bugs in the current scattered implementations (`input_pool.cpp` and
   `input_file_cache.cpp` are stubbed TODO files).
2. **`lib/binsearch.h`** + **`lib/hash.h`** — trivial wins, immediate dedup.
3. **Extend `lib/arraylist.h`** with the missing accessors and switch
   callers off `->data[i]` direct access.
4. **`lib/hashmap_helpers.h`** with `HASHMAP_DEFINE_STRKEY` macro — kills
   25+ near-identical comparators.
5. **`lib/thread_pool.{h,c}`** — unifies the two existing pools.
6. **`lib/math_utils.h`** for `clamp` / `sign` / `lerp`.
7. Header hygiene: fix `lib/strview.h` drift (add `strview_from_cstr` and
   friends); decide fate of `num_stack` and `priority_queue` (currently unused).
