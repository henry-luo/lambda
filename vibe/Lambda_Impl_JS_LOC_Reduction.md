# LambdaJS LOC Reduction — Round 2 (Structural)

> **SUPERSEDED — historical record only.** Every outstanding item from this analysis has been re-verified against the tree and absorbed into **`vibe/Lambda_Impl_JS_LOC_Reduction3.md`** (2026-08-17), which is self-contained and is the document to execute from. Do not plan work from the estimates below: several were materially wrong (WS5 −1,000 → −430, WS11 −500 → no-go, WS4's transport −600 → −130), and the WS2/WS4 "smaller items" bullet realized only ~30% because four of its items were absorbed by work that landed in stages C1–C3. This file is retained for the estimate-vs-actual calibration record — the ~55% structural realization rate observed here is what sets round 3's floor.

**Status:** superseded (was: proposal). **Date:** 2026-08-15. **Scope:** `lambda/js/` (~208K lines, 105 files). **Target:** ≥5,000 net lines removed, with the codebase left *more* structured, not just smaller.

## 1. Goal and context

Prior "JS LOC reduction" rounds (git: `88257969f` … `804ab676f`, ~31K deleted / ~13K added) already removed dead code, exec-profiling scaffolding, and unused MIR-lowering paths. Verified consequences for this round: there is essentially **no remaining dead code** (symbol-reference scans over `js_runtime.cpp`, `js_globals.cpp`, and the MIR files found zero unreferenced functions), **no cross-file copy-paste** of static helpers (only 2 real cases repo-wide), and exact-textual clone detection comes back nearly empty. What remains is **structural duplication**: the same algorithm hand-written N times as near-identical families, hand-rolled dispatch chains, and 2–6 line idioms repeated 100–500×.

This round therefore proposes *consolidations*: shared kernels, static tables, X-macros, and idiom helpers. Each workstream below carries evidence (occurrence counts, `file:line`) and a net estimate (savings minus new helper/table cost). Estimates come from a 6-way parallel audit of the runtime core, globals, DOM, Node modules, MIR lowering, and misc stdlib.

Relation to the AST-interpreter decision (**D8.1.1v2**: T0 interpreter default + per-function MIR tier-up): the MIR lowering layer remains the tier-up backend, so consolidating it (WS1–WS3) stays worthwhile; none of the workstreams below conflict with the interpreter plan.

## 2. What is already clean (do NOT spend effort here)

The audit confirmed these areas are already table-driven or optimal — proposals that "table-ize registration" would churn with ~0 net gain:

- **Builtin registration**: `js_builtin_catalog.def` already holds 441 method / 92 global / 459 id rows; installation runs through `js_runtime_builtin_registry.cpp`. Intrinsic bodies are macro-generated (`JS_ARRAY_INTRINSIC_BODIES` etc.); only ~56 of 516 bodies are hand-written. `JS_FORWARD_*` has 830 uses.
- **`js_set_key_cstr` wiring**: 1,754 call sites, 1,722 single-line → a descriptor table trades 1 line for 1 row. Skip.
- **`js_crypto.cpp` algorithms**: one HashCtx/HmacCtx/CipherCtx each, mbedtls name dispatch, cipher metadata already a static table. Only the dlsym'd OpenSSL backends repeat (WS10).
- **`js_typed_array.cpp`**: `js_typed_array_specs[]` is already the descriptor table; zero switches over the type enum; DataView funnels through one `js_dataview_operation`.
- **`js_zlib.cpp` / `js_buffer.cpp`**: cleanest files in the Node set; use as the reference style.
- **`js_props.cpp` / `js_property_attrs.cpp` / `js_object_meta.cpp` / `js_coerce.cpp`**: essentially optimal.

## 3. Workstreams

### WS1 — One generic AST child-visitor (X-macro child table) — net ≈ 1,500

The single biggest item. 27+ hand-written recursive AST walkers across `js_mir_analysis.cpp` (11 walkers, 1,229 lines), `js_mir_module_batch_lowering.cpp` (8, 971), `js_mir_function_collection_class_inference.cpp` (10, 831), `js_mir_statement_lowering.cpp`, `js_mir_function_class_lowering.cpp`, `js_mir_eval_lowering.cpp`, plus `js_early_errors.cpp` (149 lines of pure traversal). Mechanical measurement: ~70% of walker lines are `case JS_AST_NODE_*` + downcast + recurse + `->next` loops + `break;` — ~721 case labels total, ~1,820 lines of pure traversal.

Refactor: one `JS_AST_CHILDREN(X)` X-macro in `js_mir_internal.hpp` (or `js_ast.hpp`) listing each of the ~62 node kinds' child fields, generating `js_ast_visit_children(node, cb, ctx)` plus a short-circuiting bool variant (~90 lines once). Each walker keeps only its interesting cases and delegates `default:` to generic recursion. Worked example: `jm_collect_enclosing_lexicals_for_target` (`js_mir_module_batch_lowering.cpp:1487`, 236 lines) keeps only its `BLOCK_STATEMENT` case → ~25 lines. No generic walker exists today (grep confirms).

### WS2 — MIR emission idioms — net ≈ 2,000–2,300

All compile-time code, zero runtime cost (macros/inlines), building on the already-accepted `jm_call_N` / `JM_EMIT_ITERATOR_CALL` patterns:

- **`jm_callr_N` register-arg call macros** (~750): 645 `jm_call_*` sites carry 777 lines of `MIR_T_I64, MIR_new_reg_op(...)` and 239 of `MIR_new_int_op(...)` operand boilerplate. Wrap plain regs/ints as I64 ops by default, `JM_R(x)`/`JM_I(x)` escapes for mixed sites. Example `js_mir_expression_lowering.cpp:1509`.
- **Branch/jump/ret emit helpers** (~220): 95 `MIR_BT/BF` + 43 `MIR_JMP` + 25 ret sites, each 2–3 lines → `jm_emit_branch/jmp/ret` beside the existing `jm_emit_mov` inlines (`js_mir_internal.hpp:200-250`).
- **`jm_find_module_const` + `jm_var_name`** (~185): 58 copies of the 3–4 line module-const hashmap lookup stanza; 118 copies of the `jm_format_name("_js_%.*s", ...)` idiom.
- **Op-family X-macro `JM_BINOPS(X)`** (~200): four separate op→helper/op→opcode switches in `js_mir_expression_lowering.cpp` (`:1491`, `:4264`, `:4530`, `:4575`); `:4575` is a **verbatim 14-line duplicate** of `:1491` — delete on sight. Also collapses the native-arith switch at `:2430-2560` (~82 → ~25 lines) and can feed a shared `jm_binop_result_type()` replacing repeated result-type reasoning in `jm_get_effective_type` / inference walkers (~100 more).
- **Smaller** (~550 combined): super-call dispatch consolidation (5 sites ×20–35 lines, `:5486-5806`; the spread-scan loop appears 7× verbatim though `jm_call_arg_flags` at `:5222` already computes it); widening `JsMirFunctionStateSnapshot` to kill hand-saved state in the generator/async builders (`js_mir_function_class_lowering.cpp:1499/:1936/:2146`); test262 intrinsic-intercept table (`js_mir_expression_lowering.cpp:5823-5981`, 8 hand-written `strncmp` blocks → descriptor table); nullish-guard + optional-call merge (`:5113/:5170`); `jm_strict_put()` inline (20 sites); scope-env/arguments writeback helper (11+5 sites); INT/FLOAT compound-assign merge (`:4513-4612`); the four near-identical "Third pass a/b/c/d" module-var hoist loops (`js_mir_module_batch_lowering.cpp:2411-2745`).

Hazard to fix first: `jm_emit_class_static_field_value` is defined `static` **twice with different signatures** (`js_mir_statement_lowering.cpp:1970` 4-arg with private-home handling vs `js_mir_expression_lowering.cpp:8364` 2-arg trivial). Rename one before any refactor in the area.

### WS3 — One compile pipeline — net ≈ 575

Five near-identical `parse → build_ast → early_errors → jit_init → create transpiler → MIR module → lower → link → find js_main` implementations, each with a hand-rolled unwind cascade: `js_mir_entrypoints_require.cpp:337` (137 lines) and `:585` (653), `js_mir_module_batch_lowering.cpp:5725` (319), `js_mir_eval_lowering.cpp:599` (314) and `:1745`ff (489). The exact 4-line teardown cascade appears 6× verbatim; 49 `jm_clear_active_js_transpile` / 44 `js_transpiler_destroy` / 29 `MIR_finish` sites overall. Refactor: `JsMirCompileUnit` + `jm_compile_unit_begin/parse/lower/link/fail` in one TU, options struct for `is_module`, opt level, capacities, preamble mode.

### WS4 — Node layer: one EventEmitter, one async idiom, one transport — net ≈ 2,300 (of which ~600 high-risk)

- **Unified emitter `js_node_emitter.{cpp,hpp}`** (~475): there are **6–9 independent emitter implementations** in 3 mutually incompatible listener layouts (~690 lines outside `js_stream.cpp` + the 338-line canonical one in it). `js_net.cpp` alone contains two byte-level clones (`socket_*` at `:229-790` vs `server_*` at `:3930-4939`). This also fixes two real bugs: `js_tls_socket_once` (`js_tls.cpp:1056`) is a plain alias for `on` (listener never removed), and `js_http.cpp`'s layout silently drops all but the last listener (`:2094`). Per-module special cases (stream flowing-mode, child_process IPC replay, net `allowHalfOpen`) become hooks. The queued-event replay machinery (3 implementations, ~80 lines) falls out as `emit_or_queue`.
- **`JS_TICK_N` / `JS_ENV_UNPACK` macros** (~375): 109 sites of the 4–6 line alloc-env → fill → `js_new_native_closure` → enqueue stanza (e.g. `js_net.cpp:826-842` has two adjacent copies differing by one identifier) and 93 copies of the 2-line env-unpack prologue (53+93 in js_stream alone). Also reconcile `js_tls.cpp`'s `js_setTimeout(tick,1)` vs `js_next_tick_enqueue`.
- **uv plumbing** (~265): shared `js_node_stream_write()` for the 8 hand-rolled `uv_write` sites (one of which, `js_http.cpp:2551-2557`, triple-frees on failure); shared `js_node_uv_error(status, syscall, path, host, port)` replacing ~5 per-module error factories (fixes `make_fs_error` hardcoding `syscall="access"` for every fs error, `js_fs.cpp:2600`).
- **Micro-clones + small items** (~585): 6 identical `uv_alloc_cb` clones; 3 Item→bytes converters where `js_item_bytes` already exists; 6 `*_is_object_like` clones (+17 inline 3-way checks); 4 `*_constructor_prototype` clones; `js_https.cpp`'s from-scratch URL parser + private string builder vs `lib/` StrBuf (~120); extending `js_fs.cpp`'s existing `JS_FS_ASYNC_*` macro family over the hand-written half (`:2574-3053`, ~100); stream forEach/reduce/compose `_next/_step/_continue` trios → one pump driver (~120); socket-facade property init block ×4 (~40).
- **Shared socket transport** (~600, HIGH RISK, own phase): `JsSocket` (60 fields), `JsTlsSocket` (48), `JsHttpConn` each reimplement write-queue/drain/EOF/close (~640 + ~330 + ~250 lines); six hand-rolled pending-write queue structs. Compose a `JsNodeTransport` struct; stage net → tls → http.

### WS5 — assert/util unification — net ≈ 1,500

- **One value renderer** (~1,000): `js_assert.cpp` carries 68 functions / 1,933 lines of bespoke diff rendering; `js_util.cpp:295-1045` has ~750 lines of `inspect` doing the same walk. 31 vs 30 near-parallel appenders differing only in sign prefix / forced multiline / indent / diff-marking → one walker + `JsRenderStyle` descriptor + `{JsClass, render_fn}` table. Risk: failure-message text is golden-tested against Node — gate on the assert test corpus.
- **Deep-equal modes** (~400): `js_assert.cpp:3756-4550` (29 functions, 520 lines of partial/subset matching) duplicates `js_util.cpp:1656-2119` deep equality with the same per-type arms. Add `JsDeepMode {STRICT, LOOSE, PARTIAL}` to the existing `JsDeepEqualContext`; assert already calls `js_util_isDeepStrictEqual` for non-partial paths (`:2658`).
- **Shared predicates + producer table** (~100): 8 identical predicate pairs (`is_real_regexp`, `is_boxed_primitive`, `enumerable_own_keys`, …) → shared header; the 10 consecutive `X_msg` producer blocks at `js_assert.cpp:2656-2771` → function-pointer array loop.

### WS6 — One regex strategy — net ≈ 1,375 (MEDIUM-HIGH risk)

Two engines exist: the RE2 wrapper plus a complete spec backtracker (`js_bt_regex.cpp`). Because `js_regex_needs_backtrack()` (`js_runtime.cpp:16045`) routes only "hard" cases to the backtracker, `js_regex_wrapper.cpp` carries a large **lookaround/backref emulation layer** (assertion span surgery, marker insertion, post-match filter runtime, two-pass backref retry — ~1,150 gross lines). Widening the routing predicate to all lookaround + backrefs deletes the emulation (~1,000 net). Bonus: this file is the main `std::string`/`std::vector` violator (114 sites) — deletion beats porting. Also: share one character-class parser + UTF-8 decoder between the engines (~250); the 796-line `/v`-flag set-operation rewriter exists only because RE2 can't express class set ops — the backtracker's `RxClass` can. Runtime side: 18 identical 4-line `JsRegexRange` lookup shapes + the ~90-line property-name alias chain in `js_runtime.cpp:14817-15495` → kind-indexed static tables (~125).

Risk/mitigation: backtracker is slower than RE2 on common patterns; benchmark representative lookahead-heavy regexes before flipping routing, keep the predicate data-driven so it can be tuned. Profiling to date has not flagged regex as hot (helpers dominate CPU).

### WS7 — Runtime-core idioms and tables (`js_runtime.cpp` + siblings) — net ≈ 1,900

- **Interned-name key helpers** (~450 across runtime/globals/DOM): 530 occurrences of `(Item){.item = s2it(heap_create_name(...))}` in `js_runtime.cpp` (241 more in DOM files); 314 are standalone `Item x_key = …` declaration lines that vanish (e.g. eight separate `len_key` decls inside one function, `:25084-25474`). IMPORTANT: do NOT convert to `js_get_key_cstr` — it heap-copies via `js_make_string_len` while the idiom interns via the name pool, and it builds a `RootFrame(2)` per call. Add `js_name_item(cstr,len)` + `js_get_name_key`/`js_set_name_key` that preserve interning exactly; keep inline forms on `js_get_key_core` hot paths.
- **`JS_ROOTS` variadic macro** (~400): 495 `Rooted<Item>` lines in `js_runtime.cpp` (129 runs of ≥2 consecutive decls totalling 453 lines; +175 in globals). `JS_ROOTS(roots, a,x, b,y)` expands to `RootFrame roots(N); Rooted<Item> a(roots,x), b(roots,y);` — valid C+, and auto-deriving N eliminates the hand-counted `RootFrame roots(N)` mismatch bug class (144 hand-counted sites). Precise rooting only, per the retired-conservative-GC rule.
- **Dispatch tables for the intrinsic mega-dispatchers** (~300): 178 `if (operation == …)` blocks totalling 2,661 lines across `js_array_intrinsic_algorithm_impl` / `js_indexed_intrinsic_algorithm` / `js_string_intrinsic_algorithm`; 100 blocks are ≤5-line pure forwarders; 11 multi-line `op == A || op == B || …` membership chains (one is 23 lines, `:25009-25031`) → op-indexed kernel + flags-bitmask tables, same shape as the accepted `JS_ARRAY_INTRINSIC_BODIES`.
- **Builtin-prototype materialization table** (~300): `js_get_key_core:5241-5720` (480 lines) hand-writes 21 `strncmp` name guards + per-ctor setup; three near-identical `__primitiveValue__` blocks, field-by-field `__proto__` descriptor building → `JsBuiltinProtoSpec` table + one walker + `js_define_data_prop(obj, name, v, flags)` collapsing the 28 set-then-mark clusters.
- **Smaller** (~450): one `js_species_constructor()` replacing 4 implementations (`:23104`, `:30108`, `:3652`, `:12836`); extend the existing `JS_HOST_META_*_OP` macros over the meta/promise `JsPropertyOpResult` families (18 six-arg functions, 17 `(void)lane;` lines); `js_map_own_or()` for the 78 two-line shape-lookup preambles; `js_throw_type_errorf/range_errorf` for the 17+ `char msg[N]; snprintf; throw` sites (61 more snprintf sites in globals); merge `js_array_like_join`/`to_locale_string` (~70% verbatim, `:19763-19845`); topological reordering to drop ~124 forward-decl-only lines.
- **Follow-up scoping pass** (unbudgeted upside ~800–1,500): the two 1,000-line monoliths `js_get_key_core` (`:4692-5731`) and the `js_set_*_core` trio (`:6357-7248`) hold the densest per-type dispatch; decompose only after the above land.

### WS8 — Globals family merges (`js_globals.cpp`) — net ≈ 1,675

- **Own-property-key enumeration** (~430): 13 functions / 917 lines walk the same 9-branch type ladder with different filters (`js_for_in_keys` 239, `js_object_get_own_property_names` 161, `js_object_keys` 154, …; 0.60 shingle containment; the String-wrapper shape walk is written out 3×) → one `js_own_keys(obj, JsOwnKeyFilter)` + thin adapters.
- **One property-descriptor pipeline** (~350, HIGHER RISK — spec-visible ordering; gate on test262): the Item-based path in globals (~773 lines: `js_object_define_property` + validators) and the `JsPropertyDescriptor`-struct path in `js_props.cpp` (~349) + three descriptor read-back producers → make the existing POD the only IR; globals parses Item→POD once and delegates.
- **Date dispatch table** (~195): `js_date_method` (105) and `js_date_setter` (233) are 0.71-similar magic-id chains duplicating the `__time__` guard, NaN branch, and tm setup; `wday[]/mon[]` arrays appear 3× → ~52-row `JsDateFieldSpec` table + one preamble + kind switch.
- **URI/escape transcode profiles** (~165): fast/slow encode + decode + escape/unescape re-implement the hex/UTF-8 walk 4× with 3 hex-value helpers → one `js_uri_transcode(str, profile)` + 4 static profiles.
- **Smaller** (~535): Array.from family (10 fns / 343 lines, pairs at 0.80/0.64/0.61 similarity → one kernel); MessagePort per-event plumbing → event-spec table; process listener family (on/off/removeAll at 0.67/0.54 → one `js_process_listener_op`); four per-type `delete` handlers share a copy-pasted 7-line strict-throw block ×3 + configurability probe; symbol description lookup pair (0.88); `Object.groupBy`/`Map.groupBy` (0.77); 5 orphan forward decls + 4 duplicate `#include`s.

### WS9 — DOM tables (`js_dom*.cpp`) — net ≈ 1,800

- **IDL attribute reflection table** (~350): the setter delegates to 4 predicate functions (176 lines of strcmp chains, `js_dom.cpp:8083-8254`) while the getter open-codes the same knowledge again (~417 lines, 59 strcmp sites, `:9099-9515`; e.g. `inputMode` vs `enterKeyHint` byte-identical apart from keyword list). One `X(idl, attr, KIND, dflt, TAGMASK)` table + `js_dom_tag_bit()` bitmask (replaces 33 `_is_tag OR`-chains) + generic reflect_get/set. Also fixes the current get/set asymmetry where `_is_string_reflected` gates writes but not reads.
- **Property-ID switch for the mega-dispatchers** (~250 + big runtime win): `js_dom_get_property_impl` (1,199 lines, 28% dispatch scaffolding, up to 127 sequential strcmp per property access), `js_dom_set_property_impl` (592), `js_dom_element_operation_impl` (842), `js_document_get_property` (322) → `switch (js_dom_prop_id(prop))`.
- **SVG dedupe against radiant** (~300 net + 1,750 relocated): `js_dom.cpp:10460-12226` is 78 `js_dom_svg_*` functions of renderer geometry; radiant already exports `svg_parse_transform` (which js_dom correctly calls) but keeps `parse_svg_viewbox`, `build_path_from_svg_shape`, `parse_points_to_path` static — promote them behind a small `radiant/svg_geometry.h` (per the promote-don't-copy rule), delete js_dom's reimplementations, move the rest to `js_dom_svg.cpp`.
- **Descendant-walker unification** (~185): 12+ recursive collectors + **4 `std::function` recursive lambdas** (`:7703-7847`, `:9561` — convention violation, pulls `<functional>`) → one `dom_walk_descendants(root, flags, visit, ud)`.
- **Smaller** (~715): live-collection registries — 2 hand-copied variants of the existing template + 3 identical refresh functions (~150); event-init stamping tables — 238 `event_set_*` calls, 62 in exact table-row shape, mouse/pointer factories share 13 identical lines (~120); dead code verified unreachable — the formdata Blob/File shim can never install because clipboard registers full-spec Blob/File first (`js_formdata.cpp:756-830`), plus 2 zero-caller functions (~120); clipboard's 8 near-identical interface-registration blocks → shared `js_install_interface()` + table (~90); URL accessors duplicated verbatim between document and location (~70); inherited/enumerated global attrs — 4 copies of mapper+ancestor-walk (~70); 3 identical DOMRect builders + 5 sets of object-prop micro-helpers + duplicated camel→css-prop converters (~95).

### WS10 — Misc stdlib small items — net ≈ 700

One source-scanner in `js_scope.cpp` (6 hand-written full-source state machines sharing the same ST_STRING/TEMPLATE/COMMENT/REGEX skeleton → one tokenizer + callbacks, ~200); shared encoding layer for crypto/buffer (encoding-name dispatch + bytes↔string implemented twice, plus a within-file duplicate in buffer, ~180 — hex/base64 primitives are already centralized in `lib/`); OpenSSL dlsym X-macro (44+9 function-pointer fields each declared + loaded + availability-checked by hand, `js_crypto.cpp:4440-4635`, ~155); `build_js_ast.cpp` dispatch — the 527-line/34-branch strcmp chain has 31 pure-forward branches → sorted table + bsearch (~35 net, big parse-time win) + `JS_AST_NEW` alloc macro (~50); optional/lower-confidence: merge the 22 `build_ts_*_u` builders (955 lines, 0.35–0.53 similarity) into JS counterparts behind `ts_mode` (~200 — verify against the TS corpus first).

### WS11 — Array-lane unification — net ≈ 500 (HIGH RISK, optional)

The same ~25 array methods exist in three lanes: generic-object (`js_array_generic_*`, 1,166 lines), real-Array (`js_array_intrinsic_algorithm_impl`, 858), TypedArray (`js_indexed_intrinsic_algorithm`, 974). One kernel over a 4-function element-accessor vtable. The lanes differ in observable spec details (detached-buffer checks, ArraySpeciesCreate vs TypedArrayCreate, live length) — converge **one method at a time** behind test262, never big-bang.

### WS12 — File splits (structure only, no net LOC)

For maintainability and build parallelism, after the net-LOC work: `js_runtime.cpp` 37,272 → ~27,600 by extracting the self-contained Node-module block (`:31850-36759` → `js_node_modules.cpp`) and the regex block (`:14370-19133` → `js_regex_runtime.cpp`); `js_dom.cpp` −1,750 via `js_dom_svg.cpp` (WS9); move the test262 native asserts out of `js_globals.cpp` (892 lines, `:9417-10242` + `:4978-5044`) into `js_test262_natives.cpp` and default `JS_TEST262_FAST_PATHS` to **0 in release** — today the harness accelerators ship in the production binary.

## 4. Consolidated ledger

| WS | Theme | Net (nominal) | Risk |
|---|---|---:|---|
| 1 | Generic AST child-visitor | 1,500 | Low |
| 2 | MIR emission idioms | 2,150 | Low |
| 3 | One compile pipeline | 575 | Low-Med |
| 4 | Node emitter/async/transport | 2,300 | Low→High |
| 5 | assert/util unification | 1,500 | Med (golden msgs) |
| 6 | One regex strategy | 1,375 | Med-High (perf) |
| 7 | Runtime-core idioms/tables | 1,900 | Low-Med |
| 8 | Globals family merges | 1,675 | Med (test262) |
| 9 | DOM tables | 1,800 | Low-Med |
| 10 | Misc stdlib | 700 | Low |
| 11 | Array-lane unification | 500 | High |
| | **Total nominal** | **≈ 16,000** | |

Estimates are audit-derived, not measured diffs; applying a 50–60% realization factor still yields **~8,000–9,500 net**, so the ≥5,000 target holds even if half the items underdeliver or are cut. Phases 1–2 alone are ~5,600 nominal of low-risk work.

## 5. Phased execution plan

Gates for every phase: `make build` clean, `make test-lambda-baseline` 100%, `test_js_gtest`, `test_js_mir_emission_gtest`, `test262-baseline` (never mask failures — CLAUDE.md rule 18); `make test` + `test262-full` before merging a phase. Phases land as separate commits/PRs; within a phase, land per-workstream.

- **Phase 0 — free wins (≤1 day, ~350)**: delete the verbatim duplicate op table (`js_mir_expression_lowering.cpp:4575`); rename the colliding `jm_emit_class_static_field_value`; DOM dead code + zero-caller functions; orphan decls + duplicate includes; `jm_strict_put`.
- **Phase 1 — mechanical idioms (~2,900 nominal)**: WS2 macros/helpers, WS7 name-key helpers + `JS_ROOTS` + throwf, WS4 micro-clones + uv write/error helpers. Zero intended behavior change; review = pattern diff.
- **Phase 2 — table-driven structure (~2,700 nominal)**: WS1 visitor, WS3 pipeline, WS8 Date/URI/Array.from/MessagePort/process/delete tables, WS9 reflection + prop-ID + walkers + stamping, WS10 scanner/encoding/dlsym/bsearch.
- **Phase 3 — semantic unifications (~2,800 nominal)**: WS5 renderer + deep-equal modes (gate: assert golden messages), WS4 unified emitter + tick/env macros (gate: Node-compat tests; fixes the `once`/single-listener bugs), WS8 own-keys then descriptor pipeline (gate: test262 property sections).
- **Phase 4 — high-risk consolidations (opt-in, ~2,600 nominal)**: WS6 regex routing flip (benchmark first), WS4 socket transport (net→tls→http), WS11 array lanes (method-by-method), WS10 TS-builder merge.
- **Phase 5 — file splits (WS12)**: relocation only, after the churn above settles so blame/diffs stay readable.

## 6. Bugs and convention violations found in passing (fix with root-cause comments)

1. `js_tls_socket_once` is `JS_FORWARD_ITEM(..., js_tls_socket_on, ...)` — `once()` never removes the listener (`js_tls.cpp:1056`). Fixed by WS4 emitter.
2. `js_http.cpp` listener storage keeps only the last listener (`:2094` bare `js_set_key_default`, no array promotion). Fixed by WS4 emitter.
3. `make_fs_error` hardcodes `syscall="access"` for every fs error (`js_fs.cpp:2600`). Fixed by WS4 uv-error factory.
4. `js_http.cpp:2551-2557` write-error path triple-frees. Fixed by WS4 uv-write helper.
5. DOM reflection get/set asymmetry: `_is_string_reflected` gates writes but not reads. Fixed by WS9 table.
6. `std::` violations (CLAUDE.md rule 3): `js_runtime.cpp` (`std::string` in regex hex-property builder `:15090`, `std::unordered_map` fold table `:16645`), `js_regex_wrapper.cpp` (114 sites — mostly deleted by WS6), `js_dom.cpp` (4 recursive `std::function` lambdas — removed by WS9). `js_https.cpp` private string builder → `lib/` StrBuf.
7. Double-defined `jm_emit_class_static_field_value` (WS2 hazard, Phase 0).
8. Formdata Blob/File shim unreachable by install-order (WS9 dead code).

## 7. Non-goals

No behavior changes outside the listed bug fixes; no `transpile.cpp`/C2MIR work (frozen); no vendored-code edits (radiant helpers are *promoted*, not copied, per the module-header rule); no conversion of interned-name paths to heap-copying string helpers (perf + identity semantics); no big-bang rewrites of the three array lanes or the socket transport — staged only.
