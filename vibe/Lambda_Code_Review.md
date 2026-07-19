# Lambda / Radiant C/C++ Code Review

**Date:** 2026-07-18
**Commit:** `d76b90f5e`
**Scope:** ~747k lines of first-party C/C++ across `lambda/` (core runtime, input parsers, formatters, validator, network, JS/Python/Bash engines), `radiant/` (CSS layout & rendering engine), and `lib/` (foundation allocators, containers, GC, string libraries). Vendored code (`lib/sqlite/`, tree-sitter, WOFF2, tidwall hashmap, c-algorithms arraylist, mbedTLS) and generated files (`parser.c`, `lambda-embed.h`) excluded.

**Method:** Six parallel audit agents, one per subsystem, each instructed to verify every finding by reading the cited source rather than inferring from grep. The highest-severity findings in this report were then independently re-verified against the current tree. Line numbers are current as of the commit above.

**What this review is *not*:** It does not re-litigate two workstreams already tracked in `vibe/`:
- **`Compile_Warning.md`** — the compiler-warning cleanup (82k → 61 warnings) is essentially done; this review does not re-derive it.
- **`Lambda_Code_Clean_Up.md`** — the `./lib`-reuse / duplication-migration workstream is in active progress and independently tracked. Where this review names duplication, it flags *safety* or *maintainability* consequences not already covered there, and cross-references rather than duplicating the migration plan.

The project's coding conventions are respected as constraints, not flagged as defects: the deliberate "C+" subset (C-compatible C++17), the ban on `std::` containers in favor of `lib/` equivalents, pool/arena/GC allocation, `log_*` logging, and the `float`-only rule in radiant. Findings concern *unsafe or inconsistent application* of these chosen paradigms, not the paradigms themselves.

---

## Executive Summary

The codebase is, in most respects, **more disciplined than its size would predict**. It ships a purpose-built safety library (`checked_math.hpp`, `checked_alloc.hpp`, `mem_grow.hpp`) that is genuinely adopted; an audited-exception comment convention (`UNSAFE_LIBC_OK`, `RAWALLOC_OK`, `INT_CAST_OK`, `RAW_ITEM_EQ_OK`) tied to custom ast-grep lint rules; a `.clang-tidy` with a broad check set; thoughtful stack-overflow engineering (signal-based recovery, RAII recursion guards, TCO iteration caps); and near-zero dead/commented-out code with best-in-class TODO density. Naming, header guards, and the tagged-pointer cast discipline are followed almost universally.

The problems cluster into **five themes**, in rough priority order:

1. **Memory-safety gaps on the OOM and untrusted-input paths.** The most serious findings are not everyday bugs — they are latent failures that surface under memory pressure or on adversarial input. The GC collector can free live objects if a `realloc` fails mid-mark; the memtrack layer can corrupt the heap on a cross-mode `realloc`; several untrusted parsers (PDF, YAML) recurse without a working depth limit; and the HTTP server has a stack-buffer overflow and a header-injection hole. These are individually fixable and mostly localized.

2. **Structure: mega-functions and mega-files.** This is the most *pervasive* issue by volume. `resolve_css_property` is a single **5,978-line function**; `js_runtime.cpp` is **39,481 lines / 1,125 functions** with a 2,545-line dispatcher; `main()` is ~2,987 lines. Across the tree, ~90 functions exceed 300 lines and a dozen exceed 1,000. This is a maintainability and reviewability tax, and it is where subtle state-flow bugs hide.

3. **Fallible-but-silent APIs.** A recurring shape: functions that can fail (OOM, short write, overflow) return `void` or an always-true value, or log-and-continue. String-builder appends, GC root registration, file writers, and ~66 allocation sites in the core runtime swallow failure. Output silently truncates; the atomic-file writer can install a truncated file; roots silently go unregistered.

4. **Global mutable state vs. a multi-threaded future.** ~1,015 mutable file-scope statics (620 in `lambda/js` alone), plus process-global singletons (`_lambda_rt`, `js_runtime_state`, `s_in_batch`) that coexist uneasily with a `__thread`-local context design and real threads (parallel module compilation, frame clock, network). Single-runtime-per-process is an *implicit, unenforced* invariant.

5. **Inconsistent error conventions and integer-width handling.** Within a single module, failures are variously `ItemNull`, `ItemError`, `NULL`, `&STR_ERROR`, or `set_runtime_error`; callers can't handle them uniformly. `size_t` computations truncate through `int` allocation parameters; untrusted numbers are parsed with `atoi`/`strtol` without `ERANGE` checks.

**Bottom line:** No architectural rewrite is warranted. The recommended program is (a) a focused memory-safety sprint on the ~15 high-severity findings, (b) a sustained structural-decomposition effort on the worst dozen functions, and (c) two or three new lint rules plus a UBSan CI job to prevent regression. Detailed roadmap in [§7](#7-prioritized-remediation-roadmap).

---

## 1. Severity-Ranked Master Findings

The table lists every **high-severity** finding plus the most consequential mediums, across all subsystems. IDs map to the per-subsystem sections that follow.

| # | Severity | Finding | Location | Class |
|---|----------|---------|----------|-------|
| 1 | **High** | GC mark-stack `realloc` self-assign: on OOM mid-mark, old stack leaks, pointer becomes NULL, gray object dropped → **live objects swept (use-after-free)** | `lib/gc/gc_heap.c:816` | mem-safety |
| 2 | **High** | Slab-range registration on `realloc` failure: OOB write in the `gc_heap.c` copy, live-object sweep in the `gc_object_zone.c` copy | `lib/gc/gc_heap.c:165`, `gc_object_zone.c:43` | mem-safety |
| 3 | **High** | memtrack `mem_realloc` passes `ptr - 16` to `realloc` even when the header magic doesn't match → **heap corruption** (the `free` path guards this; realloc doesn't) | `lib/memtrack.c:686` | mem-safety |
| 4 | **High** | PDF recursion-depth guard **completely defeated** — depth hard-coded to literal `1` on every recursive call; `> 50` / `<= 20` gates never fire → stack overflow on nested `[[[[` | `lambda/input/input-pdf.cpp:443,482` | untrusted-input |
| 5 | **High** | YAML parser has **no recursion depth limit at all** (every other parser has one) → stack overflow on nested flow input | `lambda/input/input-yaml.cpp:1186,1370` | untrusted-input |
| 6 | **High** | PDF stream decompressors have **no output-size cap** (decompression bomb) + `compressed_len * 4` / `buffer_size *= 2` overflow | `lambda/input/pdf_decompress.cpp:32,70` | untrusted-input |
| 7 | **High** | HTTP response serializer: 64KB **stack buffer** with unclamped `snprintf(buf+pos, sizeof-pos, …)` — headers >64KB wrap `size_t-int` → OOB stack write | `lambda/js/js_http.cpp:1660` | mem-safety |
| 8 | **High** | HTTP **header-value injection**: only `undefined` rejected, no CRLF/control-char check → response splitting via `res.setHeader(name, userValue)` | `lambda/js/js_http.cpp:1055` | security |
| 9 | **High** | Python function with **>16 params**: fill loop capped at 16 but `mir_param_count` / `varargs_param_offset` uncapped → uninitialized read, then OOB write past `params[20]` (stack corruption) | `lambda/py/transpile_py_mir.cpp:6784` | mem-safety |
| 10 | **High** | Thread-local `context` left **dangling to a dead stack frame** after every script run; REPL/Radiant do work in that window | `lambda/runner.cpp:1609,1636` | mem-safety |
| 11 | **High** | `LAMBDA_ALLOCA` bound is **assert-only** (vanishes in release) + 2 raw bash `alloca` on user-sized input → stack-clash on large scripts | `lib/lambda_alloca.h:43`, `bash/transpile_bash_mir.cpp:1634` | mem-safety |
| 12 | **High** | `resolve_css_property` is a **single 5,978-line function** (262 property cases, inlined shorthand parsers) | `radiant/resolve_css_style.cpp:5777` | structure |
| 13 | **High** | `js_runtime.cpp`: 39,481 lines, 1,125 functions, **12 functions >300 lines** (`js_dispatch_builtin` = 2,545) | `lambda/js/js_runtime.cpp` | structure |
| 14 | **High** | Radiant mega-functions: **11 functions ≥1,000 lines**, 52 ≥300; top three each run to their file's EOF | `radiant/` (see §5) | structure |
| 15 | **High** | ~**1,015 mutable file-scope statics** (620 in `lambda/js`); single-runtime-per-process is unenforced | repo-wide (see §6) | global-state |
| 16 | Med | GCM authenticated-decrypt failure returns JS `null` instead of throwing → auth bypass if caller ignores return | `lambda/js/js_crypto.cpp:7245` | security |
| 17 | Med | `input_http` honors a full TLS-verification bypass flag; `js_xhr.cpp` sets it **unconditionally** → all XHR/fetch MITM-able | `lambda/input/input_http.cpp:116`, `js_xhr.cpp:493` | security |
| 18 | Med | ~66 of 130 allocation sites in the core runtime dereference the result **without a NULL check** | `lambda/lambda-eval.cpp`, `lambda-mem.cpp:534` | mem-safety |
| 19 | Med | `heap_alloc(int size)` — `size_t` computations truncate to `int`; `fn_strcat`/`heap_create_symbol` unguarded → small alloc + giant memcpy | `lambda/lambda-mem.cpp:351`, `lambda-eval.cpp:221` | integer |
| 20 | Med | `arena_alloc_aligned` breaks its alignment contract on fresh chunks; `alignas(256)` on a FAM is a no-op wasting 232B/chunk | `lib/arena.c:42,320` | allocator |
| 21 | Med | Broken double-checked locking in rpmalloc lazy-init; `next_pool_id++` races | `lib/mempool.c:94` | thread-safety |
| 22 | Med | `log.c` not thread-safe: `localtime()`, unlocked category registry, interleaving chunked writes | `lib/log.c:98,820` | thread-safety |
| 23 | Med | ~35 fallible-but-`void` APIs (all strbuf/stringbuf appends, GC root registration) silently swallow OOM | `lib/strbuf.c`, `gc_heap.c` | api-design |
| 24 | Med | Signed `cap * 2` growth guards rely on **UB overflow wrap** the compiler may delete; GC growth sites unguarded | `lib/mem_grow.hpp:17`, `gc_heap.c:100` | UB |
| 25 | Med | GC traces Lambda objects via **hard-coded byte offsets** with no `static_assert` tying them to the real structs | `lib/gc/gc_heap.c:1067` | UB |
| 26 | Med | py/bash AST builders & transpilers recurse with **no depth guard** (Lambda's frontend has one) → Jube DoS | `lambda/py/build_py_ast.cpp:438` | untrusted-input |
| 27 | Med | Tagged-downcast checks (`view_require*`, `dom_require*`) are `assert` — **compile out in release** → silent type confusion | `lib/tagged.hpp:45` | error-handling |
| 28 | Med | HTML output formatter recursion ignores its own `depth` counter → stack overflow at format time | `lambda/format/format-html.cpp:323` | untrusted-input |
| 29 | Med | State-name intern table silently breaks pointer-identity keys after 64 names; overflow path stores a caller-owned (dangling) pointer | `radiant/state_store.cpp:124` | mem-safety |
| 30 | Med | `resolve_target` failure leaves out-params uninitialized; all 3 callers drop the return → UB read + event to garbage coords | `radiant/event_sim.cpp:918,3638` | UB |
| 31 | Med | ~33 static `Item` prototype globals never registered as GC roots — survive only via implicit reachability | `lambda/js/js_http.cpp:52`, `js_stream.cpp:136` | lifetime |
| 32 | Med | `_lambda_rt` process-global contradicts the `__thread context` design; two runtimes on two threads stomp each other | `lambda/mir.c:21` | global-state |
| 33 | Med | Content-Length / Range HTTP headers parsed with `atoi`/`atol` (UB on overflow, accepts negatives) | `lambda/js/js_http.cpp:563`, `serve/static_handler.cpp:133` | integer |
| 34 | Med | Inconsistent error conventions within one module (`ItemNull` vs `ItemError` vs `NULL` vs `&STR_ERROR`); one function returns two different failure values | `lambda/lambda-eval.cpp:221` | error-handling |
| 35 | Med | `fclose`/`write` results ignored on write paths, **including atomic-rename flows** → silent truncated-file install | `lambda/lambda-proc.cpp:228`, `lib/file.c:298` | error-handling |
| 36 | Med | Lowercase `min`/`max` **macros in the core header** (`lambda-data.hpp`, included by 105 TUs); 5 rival MIN/MAX definitions | `lambda/lambda-data.hpp:33` | macro |

---

## 2. Cross-Cutting Themes

### 2.1 Memory safety concentrates on two paths: OOM and untrusted input

The everyday happy path is well-managed (GC-rooted stack scanning, pool/arena discipline, no unbounded libc `strcpy`/`sprintf` in scope). The danger lives in two places the happy path doesn't exercise:

- **The OOM path.** `realloc` failure is mishandled in the worst possible module — the garbage collector (#1, #2) — where dropping a gray object silently promotes a live object to garbage. memtrack (#3), file readers (#35), and ~66 core alloc sites (#18) share the shape: failure is either ignored or turned into a crash/corruption rather than a clean error return. The irony is that the project *has* the right tools (`checked_realloc` preserves the old pointer on failure; `heap_strcpy`/`fn_repeat` model the correct guard) — they're just not applied uniformly.

- **The untrusted-input path.** Parsers that consume attacker-controllable bytes — PDF (#4, #6), YAML (#5), the HTTP server (#7, #8), py/bash frontends (#26) — are exactly where the depth guards, size caps, and injection checks are missing or defeated. Meanwhile the *text* parsers that were clearly hardened first (HTML5 tokenizer, CSS tokenizer, JSON/XML/Mark/TOML with depth limits) are rigorous. The hardening was real but uneven; the newer/less-central formats didn't get the same treatment.

### 2.2 Files and functions grew by appending, not by decomposing

The size distribution is bimodal: most files are reasonable, but a dozen have grown into monoliths where new cases were appended into an existing switch rather than split out. The tell is that the three largest radiant functions each **end at their file's EOF** — the file *is* the function. This produces 5–8 levels of nesting, mutable locals threaded across 2,000 lines, and functions no single reviewer can hold in working memory. It is also where the subtle bugs in §2.1 hide: an early-return that skips a NULL check, a state flag cleared on one path but not another.

The good news: the seams are visible. `js_dispatch_builtin`'s 2,545 lines are a switch whose cases are natural files; `resolve_css_property`'s 262 cases each want to be a `resolve_prop_<name>()`; `main()`'s js262 batch worker is a self-contained protocol. Decomposition here is mechanical, not creative.

### 2.3 The "fallible but silent" API shape

Across `lib/` and the runtime, a large family of functions that *can* fail don't *say* they failed:

- All `strbuf_append_*` / `stringbuf_append_*` return `void` or an always-true `size_t >= 0`; OOM mid-document yields silently truncated but structurally-valid-looking JSON/HTML/PDF (#23).
- `gc_register_root` / `_weak` / `_root_range` on OOM log and return — the root is silently not registered, so a referenced object becomes collectable.
- `write_text_file` returns `void`; the *atomic* writer doesn't check `fclose`, defeating its own purpose (#35).
- ~66 core alloc sites (#18) and the `_safe` boxing shims (#17 in core) turn failure into a sentinel a caller reads as an ordinary value.

The fix pattern is consistent: add a sticky error bit (the vendored tidwall hashmap models this with `hashmap_oom()`), or return a checkable status, and audit terminal consumers to check once.

### 2.4 Global state vs. the threading the code already does

The runtime made `EvalContext* context` `__thread`-local *specifically* so each thread can host a runtime, and parallel JS module compilation already runs on multiple threads. Yet the execution path funnels through process-global singletons — `_lambda_rt` (#32), `js_runtime_state` with a single exception slot, `s_in_batch` shared across documents (#29-adjacent) — and ~1,015 unannotated mutable statics (#15) make the safe/unsafe boundary invisible. None of this is a bug *today* (execution is effectively process-serial), but it is the single biggest obstacle to the Radiant/webview embedding's plausible next step of running two documents or moving work off-thread, and nothing enforces or documents the invariant.

### 2.5 Error-convention and integer-width drift

Two lower-grade but pervasive consistency problems:

- **Error conventions** (#34): `lambda-eval.cpp` alone mixes 26 `set_runtime_error`, 59 bare `return ItemError` (log-only, no user message), and 63 `return ItemNull`. One function (`fn_strcat`) returns `&STR_ERROR` for bad input but `NULL` for OOM, and a caller dereferences the NULL. Callers cannot handle failure uniformly.
- **Integer width** (#19, #33): `size_t` sizes truncate through `int` allocation parameters; untrusted numbers use `atoi`/`atol`/`strtol` without `ERANGE`. The safe idiom (`strto*` + `errno`, `checked_add`) already outnumbers the unsafe one 3:1 — the remaining sites are drift, not house style.

---

## 3. Lambda Core Runtime (`lambda/` top-level, py/, bash/)

**Scale:** ~105k lines; `transpile-mir.cpp` 14.8k, `build_ast.cpp` 10.3k, `transpile.cpp` 8.3k, `lambda-eval.cpp` 6.6k.

### High severity

- **C-F1 — Dangling thread-local `context` (#10).** `__thread EvalContext* context` points into `Runner`'s stack frame (`transpiler.hpp:47`), which is embedded by value in `run_script` and never nulled on return (`runner.cpp:1609,1636`). The REPL and Radiant embedding call `print_root_item` and other GC-touching work *after* return; `persistent_last_error` exists only to paper over this. The js262 path nulls it (`main.cpp:246`); the primary path doesn't. **Fix:** null `context`/`input_context` at the end of each `run_script*`, or move `EvalContext` into the `Runtime` that already owns the heap and name pool it wraps.

- **C-F2 — >16-param Python function stack corruption (#9).** Verified: `MIR_var_t params[20]` / `char* param_name_bufs[20]`, fill loop `for (i=0; i<param_count && i<16; …)`, but `mir_param_count` and `varargs_param_offset` are computed from the uncapped `param_count`. `def f(p1..p21)` reads uninitialized `params[16..]`; with `*args`/`**kwargs` after 17+ params the offset writes past `params[20]`. Root cause at `transpile_py_mir.cpp:5638` (`param_count = pc`, never capped). **Fix:** reject >16 params with a diagnostic at AST collection, or size the arrays from `param_count`.

- **C-F3 — Unbounded `alloca` in release (#11).** `LAMBDA_ALLOCA` (74 sites) wraps its 256KB bound in `assert(...)`, which the header itself admits "decays to a plain alloca" under NDEBUG — the build config that runs untrusted scripts. Two raw bash `alloca(len+1)` sites (`transpile_bash_mir.cpp:1634`, `build_bash_ast.cpp:2217`) are sized by source-word length with no bound even in debug. A multi-MB `alloca` can jump the stack guard page, defeating the SIGSEGV-based overflow recovery. **Fix:** make the bound a real branch that falls back to `mem_alloc`; convert the bash sites to scratch-arena.

### Medium severity

- **C-F4 — ~66/130 unchecked allocations (#18).** `heap_alloc`, `pool_calloc`, `arena_alloc`, `mem_alloc` all return NULL on exhaustion; densest in `lambda-eval.cpp`. Neighboring newer functions check meticulously — inconsistent application of the house standard. **Fix:** an `heap_alloc_or_err()` one-liner + a sweep.
- **C-F5 — `heap_alloc(int)` truncation (#19).** `fn_strcat` narrows `uint32_t` lengths to `int`, adds them (overflow), passes to an `int` size parameter; concatenating two ~1GB strings → small alloc + giant memcpy. `heap_strcpy` guards this exact case; `fn_strcat` and `heap_create_symbol` don't. **Fix:** change `heap_alloc` to `size_t` (single choke point).
- **C-F6 — `_lambda_rt` process-global (#32).** Every JIT'd module funnels its context through one non-`__thread` pointer, contradicting the thread-local `context` design.
- **C-F7 — Ignored `fclose`/`write` (#35).** All five `fclose` in `lambda-proc.cpp` unchecked, including the `atomic` mode that then renames a possibly-truncated file over a good one.
- **C-F8 — Split error conventions (#34).** See §2.5.
- **C-F9 — py/bash recursion unguarded (#26).** `build_py_expression`/`bm_transpile_node` self-recurse with no `RecursionGuard`; `lambda-stack.h:60` documents that SIGSEGV recovery is *unarmed* during AST build, so nested `((((…))))` hard-crashes the Jube runtime. The Lambda frontend solved this (`build_ast.cpp:8963`, `MAX_BUILD_DEPTH 1000`); the fix wasn't propagated.
- **C-F10 — `main()` is ~2,987 lines (#10-struct).** Inlines the entire js262 batch worker protocol alongside a dozen subcommand dispatchers sharing mutable locals.
- **C-F11 — Two full transpilers (#11-struct).** `transpile.cpp` (181 `AST_NODE_*` cases) and `transpile-mir.cpp` (231) are two implementations of the whole language; every semantic fix must land in both, and several have drifted. Plus in-function triplicated blocks (py param-name ×3 with `alloca(128)` in a loop; bash digit-extraction ×3; empty-string creation ×3). Note `transpile.cpp` is **excluded from the build** (legacy C2MIR) yet still listed as a key entry point in `CLAUDE.md` — see §6.

### Low severity

- **C-F12 — `i2it` triple-evaluates its argument** (`lambda.h:1186`); `i2it(getpid())` makes three syscalls, and the macro is emitted into generated C. Dead `stack_alloc` macro with a trailing semicolon at `lambda-eval.cpp:83`. **Fix:** make `i2it`/`d2it` `static inline`.
- **C-F13 — `func_map` lazy-init is check-then-act** (`mir.c:46`); safe only because callers pre-init before `pthread_create`. **Fix:** `pthread_once`.
- **C-F14 — `vmap.cpp:20` declares `context` with the wrong type and without `__thread`** — latent ODR/TLS-model mismatch (currently unused).
- **C-F15 — Silent argument truncation** at `transpile-mir.cpp:8231` (`while (arg && ai < 16)` drops args 17+ with no diagnostic).
- **C-F16 — Known crash worked around** (`runner.cpp:1505`): Map/Element traversal skipped because "map->data access crashes for some maps (csv_test)" — an un-diagnosed memory error, and a violation of the project's own "fix root cause" rule.

### Positives

`fn_repeat` and `heap_strcpy` are model implementations (checked multiply + INT_MAX guard + null check). The stack-overflow engineering is genuinely thoughtful (armed/unarmed signal recovery, RAII `RecursionGuard`, `EQ_MAX_DEPTH 256`, TCO iteration cap). No unbounded libc string functions, no VLAs. The C transpiler has no code-injection surface (constants emitted as const-pool indices, never spliced into source). GC integration is disciplined (mailbox Items registered as root ranges; `is_heap` ownership flag prevents double-free).

---

## 4. Input Parsers, Formatters, Validator, Network (`lambda/input/`, `format/`, `validator/`, `network/`)

**Scale:** ~40k lines. These consume untrusted input and are the primary security surface.

### High severity

- **I-F1 — PDF depth guard defeated (#4).** Verified: `parse_pdf_array`/`parse_pdf_dictionary` take no depth parameter and call `parse_pdf_object(ctx, pdf, 1)` with depth hard-coded. The `depth > 50` and `depth <= 20` gates are unreachable dead code; the "skip if too deep" branches never execute. Nested `[[[[…` (O(N) bytes) recurses N frames → stack overflow. **Fix:** thread real depth through both functions.
- **I-F2 — YAML has no depth limit (#5).** `struct YamlParser` has no depth field; `parse_flow_node`/`parse_flow_sequence`/`parse_flow_mapping`/`parse_block_node` mutually recurse unbounded. Unique among the parsers — JSON (`MAX_PARSING_DEPTH 64`), XML, TOML, Mark (512), JSX, RTF all guard. **Fix:** add a depth field + cap.
- **I-F3 — PDF decompression bomb (#6).** FlateDecode/LZW/ASCII85 grow the output buffer unbounded (`buffer_size *= 2`) with no maximum; `compressed_len * 4` and `(output_pos + len) * 2` can overflow `size_t`, and a wrapped-small realloc followed by an `avail_out` underflow lets zlib write past the buffer *before* OOM. **Fix:** cap decompressed size; overflow-checked growth.

### Medium severity

- **I-F4 — HTML formatter recursion ignores `depth` (#28).** The shared formatter framework (`format-utils.hpp`, `max_recursion_depth=50`) protects JSON/XML/YAML, but `format-html.cpp` rolls its own recursion that threads `depth+1` and never checks it; the `max_depth` override handler is dead. The HTML5 tree builder also imposes no `open_elements` nesting cap. **Fix:** honor the existing depth handler.
- **I-F5 — TLS bypass exercised in-repo (#17).** `input_http.cpp:116` honors a `verify_ssl=false` flag that disables `CURLOPT_SSL_VERIFYPEER` *and* `VERIFYHOST`; `js_xhr.cpp:493` sets it unconditionally, so all XHR/fetch traffic is MITM-able. The default and the other backends are secure. **Fix:** remove the full-bypass path or gate it behind a loud debug-only flag; keep XHR verifying.

### Low severity

- **I-F6 — `cookie_jar.cpp:724`** uses `mem_calloc` result without a NULL check; `atoi(fields[7])` cast straight to a `SameSitePolicy` enum with no range check.
- **I-F7 — Download-cache writes ignore `fwrite`/`fclose`** (`network_downloader.cpp:378`, `enhanced_file_cache.cpp:271`, `curl_multi_backend.cpp:221`) → truncated cache file parsed as complete.
- **I-F8 — ~85 `printf`/`fprintf` debug dumps** in `css_style_node.cpp`, `dom_node.cpp`, `ast_validate.cpp` bypass `log.txt` and can corrupt formatter stdout.
- **I-F9 — CSS numeric validators skip `ERANGE`** (`css_properties.cpp:1073`); `long→int` narrowing wraps huge values.
- **I-F10 — ICS/RTF/VCF/EML parsers silently swallow malformed input** (return partial results, no `addError`), unlike the JSON/XML/Mark/TOML convention.

### Duplication (safety-relevant subset)

`parse_number` scan/extent logic copied across JSON/TOML/Mark/YAML/PDF (the exact-conversion core *is* shared — `parse_scanned_decimal_number`); `skip_whitespace` reimplemented in XML/TOML/YAML/graph despite a shared `input-utils` version; balanced-delimiter scanning hand-rolled ~7 times. The model to follow is `parse_hex_codepoint` (properly shared by TOML/Mark/YAML). See `Lambda_Code_Clean_Up.md` §2 for the migration plan.

### Positives

The formatter output path is uniformly depth-guarded via `FormatterContextCpp::RecursionGuard`. The **HTML5 tokenizer is rigorously bounds-checked** — every access goes through `html5_peek_char`/`consume_next_char` with `pos >= length` guards, and every multi-byte lookahead is preceded by an explicit `pos + N <= length` check. The CSS tokenizer is equally careful. `InputContext` centralizes source ownership, guaranteed NUL-termination, and a structured `addError` list with a stop-on-too-many cap. The validator uses RAII `DepthScope` with a configurable `max_depth` — the model I-F1/I-F2 should emulate.

---

## 5. Radiant Layout & Rendering Engine (`radiant/`)

**Scale:** ~188k lines, 161 files. Structure is the dominant theme; 10 files exceed 5,000 lines.

### High severity (all structural)

- **R-F1 — `resolve_css_property` = 5,978 lines (#12).** Verified: one function, 262 `case CSS_PROPERTY_*` labels, shorthand parsers (the ~150-line `font` shorthand) inlined. The entire CSS cascade application in one unreviewable unit. **Fix:** one `resolve_prop_<name>()` per case, dispatched via a function-pointer table indexed by `CssPropertyId` — the pattern `css_prop_table.cpp` already proves.
- **R-F2 — Mega-functions are the norm (#14).** 11 functions ≥1,000 lines, 52 ≥300. Worst: `measure_element_intrinsic_widths` (2,717), `handle_event` (2,661, one switch inlining hover/drag/dropdown/scrollbar/IME), `process_sim_event` (2,122), `layout_block_content` (1,947), `table_auto_layout` (1,568). **Fix:** extract one function per event type / per phase; the switches become 50-line dispatchers.
- **R-F3 — Property→behavior knowledge duplicated across 4 subsystems (#13-adjacent).** `resolve_css_style.cpp` (262 cases), `css_animation.cpp` (36-case animatable list), `css_prop_table.cpp` (accessor table, used by production getComputedStyle), and `event_sim.cpp:577` (a hand-rolled `get_computed_style` with hardcoded defaults used by the *test harness*). Adding a property touches up to four chains, and the test serializer can diverge from the real API — tests may assert values production never returns. **Fix:** route `event_sim` through `css_prop_serialize_computed`; derive the animatable set from the accessor table.

### Medium severity

- **R-F4 — State-name intern table (#29).** `state_store.cpp:124`: the state hashmap compares `key.name` by *pointer identity* (`HASHMAP_DEFINE_FIELD2_KEY`), but the intern table caps at 64 (`if (s_interned_count < 64)`); past that it returns the caller's own pointer. Result: after 64 distinct names, two lookups of the same logical name miss each other (silent stale state), and the fallback persists a possibly-temporary pointer that dump/assert code later `strcmp`s (use-after-free). Only 2× headroom over the 32 pre-interned names. **Fix:** make `state_key_cmp` do `strcmp` so correctness never depends on interning; always `mem_strdup`.
- **R-F5 — `resolve_target` uninitialized out-params (#30).** Verified: returns `false` without writing `*out_x`/`*out_y`; all three `event_sim.cpp` callers drop the bool and pass the uninitialized ints to `sim_mouse_move` (and log them). Replay tests fail nondeterministically instead of at the resolution step. **Fix:** check the return at all three sites.
- **R-F6 — Exact float equality as semantic signal (#30-adjacent).** `layout_block.cpp:4781` infers "first in-flow child" from `block->y == margin.top`; `layout_flex.cpp:2161` uses `target_width + pb_w > target_width` as a stand-in for `pb_w > 0`. Sub-pixel arithmetic or coincidence flips the branch. **Fix:** carry the fact as an explicit `bool`; write `pb_w > 0.0f`.
- **R-F7 — Tagged-downcast checks compile out in release (#27).** `view_require*`/`dom_require*` (`lib/tagged.hpp:45`) are `assert` + `static_cast`; in NDEBUG a mis-tagged node becomes an unchecked cast reading the wrong union arm, far from the root cause. Checked `view_as` variants exist but the `require` sites were chosen by habit. **Fix:** make `dom_require_element`/`view_require_element` fail loudly in release on event/JS-driven paths.
- **R-F8 — Border/padding metrics hand-rolled ~380× (#7-radiant).** 384 raw `border->width.` accesses (167 in `layout_*.cpp`), each with a hand-rolled null-guard ternary — sometimes twice within 8 lines — despite `layout_boundary_metrics()`/`layout_box_metrics()` existing for exactly this (used by 6 files). Each copy is a chance to miss the guard. Violates Rule 13. **Fix:** sweep + an ast-grep lint for the raw pattern.
- **R-F9 — Per-side border cases unrolled ~550 lines (#8-radiant).** `resolve_css_style.cpp:8627`: width/style/color × 4 sides × (value + specificity), fully unrolled. Margin/padding already got helpers; border didn't, so a specificity-comparison fix can miss 11 of 12 copies. **Fix:** store sides as indexed arrays or add `resolve_border_side()`.
- **R-F10 — Process-wide mutable flags across documents (#15-radiant).** `s_in_batch` (`state_store.cpp:3680`) is per-process but `DocState` is per-document — with iframes/webviews one document's batch suppresses another's invariant checks. `g_css_document_charset` is an implicit parameter that dangles if the doc is freed on an early return. Real threads exist (`frame_clock.cpp` pthread). **Fix:** move `s_in_batch` into `DocState`; thread the charset as a parameter.
- **R-F11 — UA stylesheet = 1,189-line C++ switch (`resolve_htm_style.cpp:714`)** re-implementing property application (including the `-1` specificity convention) by hand per element; a forgotten `_specificity` makes an author rule lose to a UA default. **Fix:** express defaults as a data table fed through `resolve_css_property`.

### Low severity

- **R-F12** — Document loaders (`cmd_layout.cpp`) duplicate the parse→build→cascade→layout pipeline with copy-pasted `std::chrono` timing scaffolding (and `std::chrono` sits oddly against the C+ convention; `event_sim` rolls its own `get_monotonic_time` — three timing idioms).
- **R-F13** — `InlineProp` hash/equal field lists (`view_reuse.cpp:30`) can silently drift from the struct; no `static_assert(sizeof(InlineProp)==…)` tripwire though the project uses that pattern elsewhere.
- **R-F14** — CSS unit conversion implemented twice with unnamed `96.0f`/`16.0f`/`1.2f` constants (`surface.cpp:264` vs `resolve_css_style.cpp:3497`).
- **R-F15** — In-band signaling: a negative property id smuggles "raw mode" through a `uintptr_t` parameter (`resolve_css_style.cpp:3472`) — invisible at call sites, signed-overflow trap if a future id uses the high bit.
- **R-F16** — `view_pool.cpp` is two-thirds JSON debug serializer (lifecycle logic ends ~line 947; :949–3230 is tree printing) — the most safety-critical file buried in a serializer that churns with every test tweak.

### Positives

Table-driven teardown (`ViewPropTeardownEntry`) with root-cause lifecycle comments (Rule 12 genuinely followed). Every storage-aliasing `reinterpret_cast` is pinned by `static_assert(sizeof(...))`; DomNode/DomElement enforce a calloc-only zeroing contract. Null-safe read accessors return shared **const** defaults (accidental writes are a compile error). No raw libc malloc/strdup in radiant. `resolve_length_value` guards var()/calc() recursion with a single-decrement exit. Vector caches are mutex-protected + LRU-capped. The `no-int-cast-radiant` lint passes clean.

---

## 6. LambdaJS Engine (`lambda/js/`) & lib/ Foundation

### LambdaJS (`lambda/js/`, ~225k lines — the largest subsystem)

**High:**
- **J-F1 — `js_runtime.cpp` monolith (#13).** 39,481 lines, 1,125 functions, 12 over 300 lines: `js_dispatch_builtin` (2,545), `js_map_method` (1,685), `js_array_method` (1,398), `js_string_method` (1,350), `js_property_get` (1,213), `js_create_regex` (1,019). One file holds property access, prototype-method dispatch for 5 types, a regex engine, UTF-16 codecs, function invocation, and module resolution. **Fix:** split along the dispatch-group seams (`js_property_access.cpp`, `js_map_set_methods.cpp`, `js_string_methods.cpp`, `js_regex_engine.cpp`, `js_call.cpp`). Same medicine for `js_globals.cpp` (674 funcs) and `js_dom.cpp` (three 500–1,400-line get/set/method impls).
- **J-F2 — HTTP 64KB stack-buffer overflow (#7).** Verified above.
- **J-F3 — HTTP header CRLF injection (#8).** Verified above.

**Medium:**
- **J-F4 — GCM auth-decrypt returns `null`, not throw (#16).** `js_crypto.cpp:7245`: a tampered ciphertext / wrong auth tag returns JS `null` instead of throwing "unable to authenticate data"; a caller that ignores the return proceeds as if authentication passed — defeating GCM. ~9 security-relevant `log_error; return ItemNull` sites share the shape. **Fix:** throw on auth failure.
- **J-F5 — Linear `strcmp`/`strncmp` method dispatch** (607 in `js_dom.cpp`, 199 in `js_runtime.cpp`) coexists with the clean table-driven `js_builtin_catalog` — two dispatch mechanisms, O(n) per method call, ordering-dependent bugs.
- **J-F6 — `js_runtime_state` single global** (not `thread_local`), single exception slot, fixed 128-deep super-this stacks that silently no-op on overflow.
- **J-F7 — `ToIntegerOrInfinity` duplicated** per-namespace plus ~40 inline copies that drift on the Symbol-before-coerce check (spec-conformance bugs fixed N times).
- **J-F8 — `jm_*` MIR emit primitives shadow `transpile-mir.cpp`'s** `emit_*` set — two JIT backends that must stay bit-compatible on value representation.
- **J-F9 — ~33 static `Item` prototype globals never GC-rooted (#31).** The conservative GC scans the stack + registered root ranges, not BSS; these survive only via transitive reachability from `globalThis`. `js_fs`/`js_net`/`js_dns` root correctly — the pattern exists, it's just not applied here.

**Low:** unchecked `mem_alloc` feeding fixed-size mbedTLS writes (`js_crypto.cpp`, 47 sites); fixed stack buffers silently truncating header names/hosts to 255/4096 (correctness bug, wrong header keyed); `ItemNull`-vs-exception error-channel split; a hand-rolled `strptime` shadowing the POSIX symbol but handling one format; native pointers stored as 56-bit tagged ints via `i2it` (works only because user addresses fit in 47–48 bits today).

**Positives:** builtin identity is genuinely table-driven (`js_builtin_catalog.def`, one source of truth). Crypto uses vetted mbedTLS, and `timingSafeEqual` is correctly constant-time (`volatile` accumulator, length-independent loop). UTF-16 conversion buffers are sized right. Async subsystems root their callbacks explicitly. Conservative stack-scanning keeps ordinary local `Item`s rooted automatically.

### lib/ Foundation (~37k lines — highest leverage; everything depends on it)

**High:**
- **L-F1 — GC mark-stack `realloc` frees live objects (#1).** Verified above.
- **L-F2 — Slab-range registration OOB write / live-object sweep (#2).** Verified: the `gc_heap.c` copy has no `else return` when the grow-realloc fails and `memmove`s past the array end; the `gc_object_zone.c` original returns early but then the slab is invisible to `gc_object_zone_owns()` so its objects are never marked. Copy-pasted (the comment even says "For now, register directly"), and the two copies rot differently. **Fix:** one `gc_object_zone_register_range()` that fails the whole allocation on grow failure.
- **L-F3 — memtrack `mem_realloc(ptr-16)` on magic mismatch (#3).** Verified: tracking is per-thread-toggleable, so a raw-`malloc`'d pointer can reach `mem_realloc` with tracking on; `mem_free_loc` anticipates this ("we don't know the real base pointer") but `mem_realloc_loc` passes `ptr-16` to `realloc` unconditionally → heap corruption, plus a 16-byte underflow read to probe the magic. **Fix:** mirror the free path.

**Medium:**
- **L-F4 — `arena_alloc_aligned` breaks alignment on fresh chunks (#20).** `alignas(256)` on a flexible array member only pads `offsetof(data)`; the runtime base comes from 16-byte-aligned `pool_alloc`, so `arena_alloc_aligned(a, n, 64)` returns 16-aligned memory from a fresh chunk (formal UB: accessing an overaligned type through an under-aligned pointer). Wastes 232B/chunk for a guarantee it doesn't deliver. Latent (only caller uses alignment 16). **Fix:** drop `alignas`, align the fresh-chunk base like the fast path already does.
- **L-F5 — rpmalloc broken double-checked locking (#21).** Non-atomic flag read outside the mutex (UB); on ARM the flag store can be visible before rpmalloc's init writes → a second thread allocates from a half-init allocator. `next_pool_id++` races. **Fix:** atomic flag with acquire/release (the project's own `lib/atomic.h` fits).
- **L-F6 — `log.c` not thread-safe (#22).** `localtime()` (static internal buffer), unlocked `categories[categories_count++]`, and multi-`fprintf` chunked writes that interleave mid-record. The codebase is multi-threaded and logging is *the* debug channel. **Fix:** `localtime_r`, mutex the registry, single `fwrite` per record.
- **L-F7 — Custom printf mis-reads varargs with `%t` present (`log.c:579`).** When a format contains the custom `%t`, the whole string is re-parsed and `%ld`/`%zu` pull an `int` from the va_list → misaligned va_list, garbage, crash on a following `%t`. Latent (current `%t` sites use only `%d/%f/%s/%x`). **Fix:** dispatch `va_arg` by length modifier.
- **L-F8 — Fallible string-builders return `void` (#23).** All `strbuf_append_*`/`stringbuf_append_*` silently drop on OOM; these assemble JSON/HTML/YAML/PDF output → silent truncation with no error signal. NULL-handling is also inconsistent across the two families. **Fix:** sticky `oom` bit.
- **L-F9 — StrBuf vs StringBuf are a duplicated builder family** (~15 function pairs) that have diverged in *safety* (overflow cap and NULL checks only in one; `strbuf_full_reset` leaks where `stringbuf_full_reset` frees). Forces every sink-generic util (`escape.c`) to be written twice.
- **L-F10 — `gc_object_zone_alloc` and `_alloc_class` are byte-identical 50-line bodies** in the GC hot path.
- **L-F11 — `file.c` unchecked `fseek`/`ftell`** (`read_text_file`) → on `ftell` failure (files ≥2GB on Windows) allocates 0 bytes then `fread((size_t)-1)` → heap overflow; unchecked `fclose` in the atomic writer (#35).
- **L-F12 — Signed `cap*2` growth guards are themselves UB (#24).** `int doubled = new_capacity * 2; if (doubled <= new_capacity) return false;` — the overflow the guard checks for is UB the compiler may assume away at -O2. GC growth sites have no guard at all. `arraylist_reserve` does it correctly (`> INT_MAX/2`). **Fix:** guard before multiplying, or use `size_t` + `checked_mul`.
- **L-F13 — GC traces via hard-coded byte offsets (#25).** Dozens of magic offsets (`p+8`, `p+16`, `tp+32`) mirror `lambda-data.hpp` layouts by comment only; any field reorder silently turns the GC into a memory corrupter, with no `static_assert(offsetof(...))` — even though `gc_heap.c` already includes `lambda.h`. Separately, `lib/gc` and `item_tagged.hpp` include *upward* into `lambda/` and `radiant/`, so "lib" isn't a standalone layer. **Fix:** a `gc_layout_check.inc` of `_Static_assert(offsetof(...)==N)` for every offset.

**Low:** `hashmap.hpp` is a 1,000-line C++23 wrapper (`std::expected`) that can't compile under the project's C++17 and violates the std-ban inside lib itself — dead, referenced only by an unbuilt test; `mime-detect.c` calls `tolower()` on raw signed `char` (UB on UTF-8 filenames — lib/str.h documents this exact hazard and provides the fix); `strbuf_append_file` returns `read >= 0` (always true); `gc_heap` large-object path truncates size into a `uint32_t` field with no cap; `mem_context.c` has a dangling `mem_doc_url` result and a fixed-64 context stack that silently skips deeper trees.

**Positives:** `checked_math.hpp`/`checked_alloc.hpp`/`mem_grow.hpp` are well-designed (`[[nodiscard]]`, slot-preserving `checked_realloc`) and genuinely adopted across 15+ files. **`lib/str.h` is an exemplary C string API** — documented NULL/length contracts on every function, a "scanner tier" built around the NUL-in-set and signed-char-ctype UB classes, SWAR loads via `memcpy` (no aliasing UB). `byte_storage.c` refcounting is textbook (CAS loops, release-callback capture before free). Arena scope tracking actively prevents reset-under-borrow use-after-free. memtrack's layered OFF/STATS/DEBUG design (guard bytes, fill patterns, poisoned raw allocators + `make check-raw-alloc`) is a serious safety net — L-F3 is its one gap.

---

## 7. Tooling & Process Recommendations

The project already has strong guardrails: `.clang-tidy` (broad check set), ~30 custom ast-grep rules under `utils/lint/rules/`, CodeQL + nightly CI, ASan in debug builds, and the audited-exception comment conventions. The gaps below would have *caught* several findings in this report at commit time.

| Gap | Recommendation | Would have caught |
|-----|----------------|-------------------|
| No UBSan in CI | Add a `-fsanitize=undefined` build to nightly (alongside the existing ASan). Signed-overflow, misaligned access, unreachable-return. | L-F4, L-F12, C-F5, R-F6 |
| `make lint`/`lint-full` not wired into CI | Run the fast `make lint` on every PR (it's ~10s); run `lint-full` nightly. | R-F8, future int-cast/dup regressions |
| No lint for `realloc(p, …)` self-assign | The rule exists in spirit (`no-open-realloc.yml`) — extend it to flag `x = realloc(x, …)`. | L-F1, L-F3, L-F11 |
| No lint for raw `->border->width.` outside helpers | New ast-grep rule (radiant). | R-F8 (~380 sites) |
| No recursion-guard checklist for new parsers | A `RecursionGuard`/depth-field lint or a parser-authoring checklist. | I-F1, I-F2, I-F4, C-F9 |
| `assert`-only downcast/alloca bounds vanish in release | Introduce a project `RDT_CHECK`/`LAMBDA_CHECK` that aborts (or logs+returns) in release for security-relevant paths; reserve `assert` for pure invariants. | R-F7, C-F3 |
| No `static_assert(offsetof)` on GC-traced structs | Add a layout-check header. | L-F13 |
| `atoi`/`atol` on untrusted input | ast-grep rule flagging `ato*` in `input/`, `js/`, `serve/`, `network/`. | I-F6, I-F9, #33 |
| Stale `CLAUDE.md` entry points | `transpile.cpp` is excluded from the build yet listed as a key entry point; ~13k lines of unbuilt sources + a `.broken` file sit in-tree. Delete/attic them and fix the doc. | C-F11, navigation |

---

## 8. Prioritized Remediation Roadmap

Ordered by (risk reduction ÷ effort). Phases are independently shippable.

### Phase 1 — Memory-safety sprint (highest risk, mostly localized)
The ~15 high-severity findings. Roughly one to three days each; none is a rewrite.
1. **GC OOM handling** — L-F1, L-F2 (mark stack + slab registration). *Highest priority: silent live-object frees are the hardest bugs to diagnose.*
2. **memtrack cross-mode realloc** — L-F3.
3. **Untrusted-parser depth/size caps** — I-F1 (PDF depth), I-F2 (YAML depth), I-F3 (decompression bomb), I-F4 (HTML formatter), C-F9 (py/bash frontends).
4. **HTTP server** — J-F2 (stack overflow), J-F3 (CRLF injection), J-F4 (GCM auth), I-F5 (TLS bypass).
5. **Stack corruption / dangling** — C-F1 (dangling context), C-F2 (>16 params), C-F3 (release alloca).

### Phase 2 — Fallible-but-silent APIs (correctness under stress)
6. Sticky-error bits on StrBuf/StringBuf (L-F8) and the ~66 unchecked core allocations (C-F4).
7. File-writer / atomic-rename error checking (C-F7, L-F11, I-F7).
8. GC-root the static prototype globals (J-F9); make security-relevant downcasts fail loud in release (R-F7).

### Phase 3 — Integer & error-convention hygiene
9. `heap_alloc` → `size_t` (C-F5); `atoi`→`strtoll`+`errno` on network paths (#33, I-F9); UB overflow guards → `checked_mul`/`size_t` (L-F12).
10. Converge error conventions in `lambda-eval.cpp` (C-F8/#34); one sentinel per return type.

### Phase 4 — Structural decomposition (sustained; do opportunistically alongside feature work)
11. Table-drive `resolve_css_property` (R-F1) and unify the property→behavior chains (R-F3) — the single biggest maintainability win in radiant.
12. Split `js_runtime.cpp` along dispatch seams (J-F1); split `main()` (C-F10), `handle_event`, `layout_block_content` (R-F2).
13. Extract the border-side/UA-default helpers (R-F8, R-F9, R-F11) and the intern-table fix (R-F4).

### Phase 5 — Thread-safety & global-state (enabler for multi-document/off-thread)
14. `log.c` thread-safety (L-F6), rpmalloc init (L-F5), `func_map` (`pthread_once`, C-F13).
15. Document-and-enforce the single-runtime invariant, or move `_lambda_rt`/`s_in_batch`/`js_runtime_state` per-context (C-F6, R-F10, J-F6).

### Phase 6 — Tooling (prevents regression; do in parallel with Phase 1)
16. The §7 table: UBSan CI, `make lint` on PRs, the new ast-grep rules, the GC layout-check header, `CLAUDE.md` cleanup.

---

## 9. What the Codebase Does Well

This is not a troubled codebase, and the review would be misleading without saying so plainly:

- **Purpose-built safety infrastructure that is actually used.** `checked_math.hpp`/`checked_alloc.hpp`, the audited-exception comment conventions tied to lint rules, `make check-raw-alloc`/`check-int-cast`, and the layered memtrack allocator are a serious, coherent safety program — well beyond what most C/C++ projects this size have.
- **Exemplary foundation pieces.** `lib/str.h` (documented contracts, UB-aware scanner tier), `byte_storage.c` (correct lock-free refcounting), the arena scope-tracking (prevents a whole use-after-free class), and the conservative stack-scanning GC design.
- **Discipline at scale.** Near-zero `#if 0`/commented-out code across ~835 files, best-in-class TODO density (66 TODO / 0 FIXME / 0 HACK), near-universal snake_case, universal header guards, textbook `goto` usage, and a tagged-pointer cast pattern applied consistently with adjacent tag checks.
- **The hard parts are done carefully.** The HTML5/CSS tokenizers are rigorously bounds-checked; crypto uses vetted mbedTLS with correct constant-time comparison; UTF-16 codecs are sized right; the stack-overflow recovery (armed/unarmed signal handling, RAII guards, TCO caps) is genuinely thoughtful; builtin dispatch is table-driven from one source of truth.
- **Self-awareness.** The `vibe/` docs (`Compile_Warning.md`, `Lambda_Code_Clean_Up.md`) show the team already tracks warning and duplication debt honestly and is actively paying it down.

The findings above are the exceptions to an otherwise disciplined codebase — which is exactly why they're worth fixing: they're isolated enough to address without disturbing the parts that work.

---

## Appendix: Metrics

| Metric | Value |
|--------|-------|
| First-party C/C++ reviewed (excl. vendored/generated) | ~747k lines |
| Largest subsystem | `lambda/js/` (~225k, 101 files) |
| Functions ≥1,000 lines (radiant + core) | ~15 |
| Functions ≥300 lines | ~90+ |
| Largest single function | `resolve_css_property`, 5,978 lines |
| Largest single file | `js_runtime.cpp`, 39,481 lines |
| Mutable file-scope statics | ~1,015 (620 in `lambda/js`) |
| Unchecked allocation sites (core runtime sample) | 66 of 130 |
| `atoi`/`atol` vs `strto*` | 75 vs 219 (safe idiom is the majority) |
| Distinct MIN/MAX macro definitions | 5 |
| `#if 0` blocks / commented-out code blocks ≥15 lines | 0 / 0 |
| TODO / FIXME / HACK | 66 / 0 / 0 |
| Recursive parsers with a working depth guard | JSON, XML, TOML, Mark, JSX, RTF (✓); YAML, PDF, HTML-formatter, py/bash (✗) |

*Report generated from six parallel subsystem audits; all high-severity findings independently re-verified against the tree at `d76b90f5e`.*
