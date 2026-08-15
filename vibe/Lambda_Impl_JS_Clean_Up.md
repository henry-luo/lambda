# LambdaJS Clean-Up — Implementation Plan

**Status:** proposed

**Revision date:** 2026-08-15

**Scope:** `lambda/js/` — bug fixes, hazard removal, idiom consolidation, table-driven restructuring, semantic unifications, and file splits. Execution companion to `vibe/Lambda_Impl_JS_LOC_Reduction.md` (the analysis: evidence, occurrence counts, per-workstream estimates). This document defines *what to change, in what order, and how each change is verified*. Workstream references (`WS1`–`WS12`) point into the analysis doc.

**Target:** ≥5,000 net LOC removed by the end of Stage C4, with all found bugs fixed under regression tests and zero baseline regressions. Nominal identified total is ~16,000; this plan sequences the ~11,000 of low/medium-risk work first and leaves the high-risk remainder opt-in (C5).

---

## 1. Governing invariants

1. **Behavior changes only where a bug fix says so.** Every stage except C0 is intended to be observationally neutral; any diff that changes a test expectation outside C0 is a defect in the refactor, not a test to update.
2. **Bug fixes land before the refactors that would absorb them** (C0 before C3/C4). Each C0 fix carries a regression test committed in the same change, so the later consolidation must keep it green. Each fix point gets a root-cause comment (CLAUDE.md rule 12).
3. **No `std::` anywhere new** (rule 3). This plan *removes* `std::` usage (C3.6 DOM lambdas, C5.1 regex wrapper); it must never add any. Use `lib/` `Str`/`StrBuf`/`ArrayList`/`HashMap`.
4. **Promote, never copy** (rule 13). Where a shared helper replaces per-file statics (emitter, uv plumbing, radiant SVG geometry), the shared version lives in a module header/TU and the statics are deleted in the same change.
5. **Interning is semantics, not style.** Property-key construction stays on `heap_create_name` (name pool). Do not route converted sites through `js_get_key_cstr`/`make_string_item` — that heap-copies and breaks name identity, and adds a `RootFrame` per call on hot paths.
6. **Precise rooting only** (rule 15). The `JS_ROOTS` macro (C2.2) must expand to exactly the `RootFrame`/`Rooted` structure that exists today, with the frame size derived from the argument count.
7. **Never mask test262 issues** (rule 18); baselines stay 100%; performance checks use release builds only (rule 10).
8. **Test placement:** Node-module regressions go in `test/node/*.js` + paired `*.txt` (checked by `utils/check_js_node_test_separation.py`); core-JS/DOM regressions go in `test/js/` (+ `.html` for document tests). Every new `.js` test gets its `.txt` expectation in the same commit.
9. **Build config, not Lua** (rule 7): the `JS_TEST262_FAST_PATHS` release default (C6.4) changes in `build_lambda_config.json`.

---

## 2. Stage C0 — Bug fixes (behavioral; land first, one commit each)

### C0.1 `TLSSocket.once()` never removes its listener

- **Root cause:** `js_tls_socket_once` (`lambda/js/js_tls.cpp:1056`) is `JS_FORWARD_ITEM(..., js_tls_socket_on, ...)` — a straight alias for `on()`, so the listener fires on every emit. The tls listener storage (layout B: `__on_<event>__` callable-or-array) has no once-flag concept.
- **Fix (minimal, pre-emitter):** wrap the listener in a self-removing shim closure at registration (env: target object, event name, inner listener), mirroring how `js_net.cpp`'s layout-A `{listener, once}` records behave. Do not restructure tls storage now — C3.3 replaces it wholesale; this fix only has to make `once` observable-correct and give the regression test something to lock.
- **Test:** `test/node/tls_socket_once.js` + `.txt` — register `once` + `on` handlers for the same event, cause the event to fire twice (prefer the public `emit()` surface if exposed on TLSSocket; otherwise drive two `'data'` deliveries over a loopback pair following the existing `test/node/tls_*.js` server patterns), assert the once-handler ran exactly once and ordering vs the `on` handler.
- **LOC:** +~25 now (net negative later when C3.3 deletes tls storage entirely).

### C0.2 HTTP emitter silently keeps only the last listener

- **Root cause:** `js_http.cpp`'s listener layout (C: single callable per `__on_<event>__` key) — `js_http_*_on` does a bare `js_set_key_default` (`lambda/js/js_http.cpp:2094` and the four sibling `on()` variants at `:2687/:3418/:3853/:4828`), overwriting any prior listener instead of promoting to an array like layout B does.
- **Fix (minimal):** on second registration for an event, promote the stored value to an array and append; teach the corresponding emit paths (`:763/:2599/:2860/:3663`) to iterate callable-or-array. ~15 lines shared between the five `on()` sites via one static helper *inside* `js_http.cpp` (it gets deleted by C3.3).
- **Test:** `test/node/http_multiple_listeners.js` + `.txt` — two listeners on one event on a server/request object, both fire, in registration order.
- **LOC:** +~20 now, absorbed by C3.3.

### C0.3 Every fs error reports `syscall: "access"`

- **Root cause:** `make_fs_error` (`lambda/js/js_fs.cpp:2579`) hardcodes `syscall="access"` at `:2600` for all callers.
- **Fix:** add a `const char* syscall` parameter and thread the correct name from each caller (`open`, `read`, `write`, `stat`, `unlink`, …, matching Node's mapping — `readFile` reports `open`). This is also the seed of the shared `js_node_uv_error()` (C2.8); implement the shared signature now (`status, syscall, path`) and let C2.8 migrate the other modules onto it.
- **Test:** `test/node/fs_error_syscall.js` + `.txt` — `readFile` on a nonexistent path → `err.syscall === "open"`, `err.code === "ENOENT"`, `err.path` set; `access` still reports `"access"`.
- **LOC:** ~0 net.

### C0.4 HTTP write-error path double/triple-free

- **Root cause:** the `uv_write` failure branch at `lambda/js/js_http.cpp:2551-2557` frees the request wrapper and buffers that the write callback also frees when libuv still invokes it — the other seven `uv_write` sites in the tree don't replicate this.
- **Fix:** single-owner rule — on synchronous `uv_write` failure, free only what the callback provably will not see (per libuv contract, a failed submission never fires the callback: then the *callback-owned* frees must be done here, once). Align with the cleanup pattern of `js_net.cpp:946`; add the ownership comment at the site.
- **Verification:** no deterministic JS repro; verify by (a) code-path audit against the other seven sites, and (b) an ASAN debug build (`make build` with sanitizers per the build config) running the `test/node/http_*.js` set and `test_http_gtest`. C2.8 then makes the pattern impossible by construction.
- **LOC:** ~0.

### C0.5 DOM string-reflection gating asymmetry (writes gated, reads not)

- **Root cause:** the setter path consults `_is_string_reflected` (`lambda/js/js_dom.cpp:8203`) before reflecting, but the getter block (`:9099-9515`) open-codes its own per-property logic and skips the predicate, so unsupported element/property pairs read as reflected but refuse writes.
- **Fix (minimal):** route the getter's mechanical cases through the same predicates. Do not build the full reflection table yet — that is C3.6, which replaces both sides; this fix just makes get/set agree and locks it with a test.
- **Test:** `test/js/dom_reflect_gating.js` + `.html` + `.txt` — a reflected property on a supported element round-trips; the same property name on an unsupported element behaves consistently between read and write.
- **LOC:** +~10 now, absorbed by C3.6.

---

## 3. Stage C1 — Hazards and dead code (no behavior change)

- **C1.1** Rename one of the two `static jm_emit_class_static_field_value` definitions — `lambda/js/js_mir_statement_lowering.cpp:1970` (4-arg, private-home enter/leave) vs `lambda/js/js_mir_expression_lowering.cpp:8364` (2-arg, trivial). Rename the expression-side one to `jm_emit_class_static_field_value_simple`. Pure rename; do this *before* any WS2 work in those files.
- **C1.2** Delete the verbatim duplicate compound-assign op table at `js_mir_expression_lowering.cpp:4575` — `jm_compound_assign_fn` (`:1491`) already exists; call it. (−14)
- **C1.3** Delete verified-unreachable DOM code: the formdata `Blob`/`File` shim (`lambda/js/js_formdata.cpp:756-830` plus its two install guards) — unreachable because `js_register_clipboard_globals` installs full-spec `Blob`/`File` during global bootstrap before formdata's guarded install runs from `js_dom_set_document`; re-verify the install order once more in-tree, then delete with a comment naming the order dependency. Also `js_is_document_proxy` (`js_dom.cpp:2422` + `js_dom.h:167`) and `js_dom_collection_has_live_property_state` (`js_dom.cpp:2104` + `js_dom.h:58`) — zero callers repo-wide. (−~110)
- **C1.4** `js_globals.cpp` hygiene: remove 5 orphan forward decls (`:48`, `:51`, `:313`, `:984`, `:6219`) and 4 duplicate `#include`s (`js_host_hooks.h`, `log.h`, `base64.h`). (−9)
- **C1.5** Add `static inline bool jm_strict_put(JsMirTranspiler* mt)` and replace the 20 copies of the 2-line strictness disjunction. (−~20)
- **Gate:** build + `make test-lambda-baseline` + `test_js_gtest` + `test_js_mir_emission_gtest`; for each deletion, a final `grep` proving zero remaining references.

---

## 4. Stage C2 — Idiom helpers (mechanical, pattern-diff reviewable)

Each task: introduce the helper in one commit, then convert sites in per-file batches. All are behavior-neutral; converted batches must produce byte-identical test output.

- **C2.1 Interned-name key helpers** (WS7): add to `js_runtime.h`: `js_name_item(const char*, int)` returning `(Item){.item = s2it(heap_create_name(...))}`, plus `js_get_name_key(obj, cstr, len)` / `js_set_name_key(obj, cstr, len, v)` wrappers that preserve interning (invariant 5). Convert the 314 standalone `Item x_key = …` declaration lines first (`js_runtime.cpp` 168, `js_globals.cpp` 136, siblings 10; e.g. the eight `len_key` decls in `js_runtime.cpp:25084-25474`), then fused get/set sites within 3 lines of the decl (176 measured). DOM variant in `js_dom.h`: `js_str(cstr)`, `js_str_or_empty(cstr)`, `js_dom_attr_str(elem, attr)` for the ~40 two-line attribute-return sites (241 `s2it(heap_create_name(...))` occurrences across DOM files). Keep raw inline forms inside `js_get_key_core` hot paths. (−~450)
- **C2.2 `JS_ROOTS` variadic macro** (WS7): in `lambda/runtime/lambda-root-frame.hpp` next to `Rooted<Item>` (`:89`): `JS_ROOTS(frame, a,va, b,vb, ...)` → `RootFrame frame((PP_NARG(...))/2); Rooted<Item> a(frame,va), b(frame,vb), …;` with a standard PP_NARG pair counter. Deriving the size kills the hand-counted `RootFrame roots(N)` mismatch class (144 hand-counted sites). Convert the 129 runs of ≥2 consecutive `Rooted<Item>` decls in `js_runtime.cpp` (453 lines; largest at `:30949`, `:24677`, `:29559`) and the 48 runs in `js_globals.cpp` (175). (−~400)
- **C2.3 MIR register-call macros** (WS2): `jm_callr_N` / `jm_callr_void_N` beside `jm_call_N` (`js_mir_internal.hpp:333-350`), wrapping bare regs as `MIR_T_I64, MIR_new_reg_op(...)` with `JM_R(x)`/`JM_I(x)` escapes for mixed sites. Convert the 645 sites file-by-file (arity census in the analysis doc). (−~750)
- **C2.4 Branch/jump/ret emitters** (WS2): `jm_emit_branch(mt, code, label, reg)`, `jm_emit_jmp(mt, label)`, `jm_emit_ret(mt, reg)` beside `jm_emit_mov` (`js_mir_internal.hpp:200-250`); convert 95 + 43 + 25 sites. (−~220)
- **C2.5 Module-const + var-name helpers** (WS2): `jm_find_module_const(mt, name)` for the 58 hashmap-lookup stanzas; `jm_var_name(mt, id)` for the 118 `jm_format_name("_js_%.*s", …)` sites. (−~185)
- **C2.6 Printf-style throws** (WS7): `js_throw_type_errorf(fmt, ...)` / `js_throw_range_errorf(fmt, ...)` beside `js_throw_named_error_text`; convert the 17 `char msg[N]; snprintf; throw` sites in `js_runtime.cpp` and the matching subset of the 61 `snprintf` sites in `js_globals.cpp` (includes the ×3 copy-pasted strict-delete block, `js_globals.cpp:12342/12371/12446`, via `js_throw_delete_rejected(key, owner, strict)`). (−~140)
- **C2.7 Node micro-clone removal** (WS4): new `lambda/js/js_node_common.hpp` (header-only): one `js_node_alloc_cb` (replaces 6 byte-identical clones — `js_tls.cpp:1899/:2432`, `js_http.cpp:3041/:3763`, `js_child_process.cpp:241/:1054`); one `js_node_is_object_like` (replaces 6 static clones + 17 inline three-way checks); one `js_node_constructor_prototype` (4 clones). Convert `socket_get_write_bytes`/`tls_get_write_bytes`/`cp_sync_input_bytes` to the existing `js_item_bytes` (base the unified converter on the `cp_sync` variant, the richest). Drop the `copy_string_item`/`item_to_cstr` `#define` aliases for `js_item_to_cstr` and the 8 inline string→buf sites in http/https. Replace `js_https.cpp`'s private `append_*` fixed-buffer builder with `lib/` `StrBuf`. (−~200)
- **C2.8 uv plumbing** (WS4): `js_node_stream_write(uv_stream_t*, data, len, done_cb, ud)` in a new `lambda/js/js_node_uv.cpp` replacing the 8 hand-rolled `uv_write` sites (kills the C0.4 pattern by construction); `js_node_uv_error(status, syscall, path, host, port)` replacing `make_uv_error`/`http_error_from_uv`/`make_dns_error_with_syscall`/`make_uv_spawn_error` and the C0.3-fixed `make_fs_error` (base it on `js_dns.cpp`'s `make_node_error:114`, the best of the five). (−~265)
- **C2.9 Shape-lookup helpers** (WS7): `js_map_own_or(obj, name, len, fallback)` and `js_map_own_string(...)` for the 78 two-line `bool own = false; js_map_shape_lookup(...)` preambles. (−~70)
- **C2.10 SpeciesConstructor helper** (WS7): `js_species_constructor(obj, default_ctor, what)`; converge the 4 implementations (`js_runtime.cpp:23104`, `:30108`, `:3652`, `:12836`). Spec-visible order (Get `constructor` → Get `@@species` → IsConstructor) is identical in all four; gate on test262 species tests. (−~100)
- **C2.11 Small kernels** (WS7): merge `js_array_like_join`/`js_array_like_to_locale_string` (`js_runtime.cpp:19763-19845`, ~70% verbatim) behind a per-element stringify pointer. (−~40)
- **Gate per batch:** build, `test_js_gtest`, `test_js_mir_emission_gtest` (C2.3–C2.5 touch codegen — MIR emission goldens must be byte-stable), `test262-baseline` after each file-level batch; full `make test` at stage end.

Stage C2 net: **≈ −2,800**.

---

## 5. Stage C3 — Structural consolidation (tables and shared kernels)

- **C3.1 Generic AST child-visitor** (WS1, the single biggest item, −~1,500): add `JS_AST_CHILDREN(X)` to `js_ast.hpp` — one row per `JS_AST_NODE_*` kind (~62) declaring its child fields (single-child, `->next` list, optional). Generate `js_ast_visit_children(node, cb, ctx)` and a short-circuiting `bool` variant (~90 lines once). Migration order, one file per commit, walker-by-walker: `js_mir_analysis.cpp` (11 walkers / 1,229 lines) → `js_mir_function_collection_class_inference.cpp` (10 / 831) → `js_mir_module_batch_lowering.cpp` (8 / 971) → `js_mir_statement_lowering.cpp` + `js_mir_function_class_lowering.cpp` → `js_early_errors.cpp` (149) → `js_mir_eval_lowering.cpp`. Each walker keeps only its interesting cases; `default:` delegates to generic recursion (worked example: `jm_collect_enclosing_lexicals_for_target`, 236 → ~25 lines). **Correctness rule:** a migrated walker must visit the same nodes in the same order — where a hand-written walker deliberately *skips* a child (e.g. not descending into nested functions), that skip becomes an explicit case, never an accident of the table.
- **C3.2 One compile pipeline** (WS3, −~575): `JsMirCompileUnit` + `jm_compile_unit_begin/parse/lower/link/fail` (options: `is_module`, opt level, transpiler capacities, module-name policy, preamble mode). Migrate the five pipelines (`js_mir_entrypoints_require.cpp:337`, `:585`, `js_mir_module_batch_lowering.cpp:5725`, `js_mir_eval_lowering.cpp:599`, `:1745`ff); the 4-line teardown cascade (6× verbatim) becomes `jm_compile_unit_fail`. Gate: eval/Function/require tests + module tests + `test262-baseline`.
- **C3.3 Unified Node EventEmitter** (WS4, −~475 plus makes C0.1/C0.2 structural): new `lambda/js/js_node_emitter.{hpp,cpp}` (~280 lines): layout-A storage (map of arrays of `{listener, once}`), full surface `on/once/off/removeAll/listenerCount/emit/emit_or_queue`, plus `JsEmitterHooks { on_listener_added; before_emit; }` for the three real special cases (stream flowing-mode transition `js_stream.cpp:2032-2073`, child_process IPC/cluster replay `js_child_process.cpp:1741-1748`, net `allowHalfOpen` sync `js_net.cpp:774`). Check whether `js_readline.cpp`'s `js_ee_emit` declaration (`:27`) is the intended seed and subsume it. Migration order (lowest-risk first, one module per commit): `js_net.cpp` (delete both intra-file clones, `socket_*:229-790` and `server_*:3930-4939`) → `js_readline.cpp`/`js_fs.cpp` → `js_child_process.cpp` (replay via `emit_or_queue`) → `js_tls.cpp` (deletes the C0.1 shim; test stays green) → `js_https.cpp` → `js_http.cpp` (deletes the C0.2 promotion helper) → `js_stream.cpp` last (canonical, most hooks). Storage keys are internal, but grep `test/` for any test peeking at `__on_*__`/`__socket_listeners__` before assuming.
- **C3.4 Async-closure macros** (WS4, −~375): `JS_TICK_N(fn, ...)` / `JS_TIMEOUT_N(fn, ms, ...)` for the 109 alloc-env→fill→closure→enqueue stanzas; `JS_ENV_UNPACK(env, env_item)` for the 93 unpack prologues. **Decision point D-1:** `js_tls.cpp` schedules via `js_setTimeout(tick, 1)` where net/cp use `js_next_tick_enqueue` — keep per-module scheduling in this stage (macro takes the scheduler), reconcile only with an explicit decision + tests, since it changes event ordering.
- **C3.5 Globals family merges, mechanical half** (WS8, −~640): Date `JsDateFieldSpec` table (~52 rows; one `__time__` preamble; dedupe the ×3 `wday[]/mon[]` arrays); URI/escape `js_uri_transcode(str, profile)` + 4 profiles (one hex-value helper instead of 3); `Array.from` kernel (one iterate+map loop, flags for ctor/mapFn/array-like); MessagePort event-spec table extending the existing `JS_INSTALL_MESSAGE_PORT_METHOD` idea; `js_process_listener_op(event, fn, op)` for on/once/off/removeAll/count/listeners; symbol-description lookup + `groupBy` kernels. Gate: `test262-baseline` (Date and URI sections), `test_js_gtest`.
- **C3.6 DOM tables** (WS9, −~1,700 net + removes `std::function`): (a) IDL reflection X-macro `X(idl, attr, KIND, dflt, TAGMASK)` + `js_dom_tag_bit()` bitmask + generic `reflect_get/set` — replaces the 4 predicate chains (`js_dom.cpp:8083-8254`) *and* the getter's open-coded block (`:9099-9515`); C0.5's test locks the semantics. (b) Property-ID dispatch: `js_dom_prop_id(name)` (sorted static table + bsearch, or NameId comparison via `js_well_known_names` — pick one, measure both on `test/js` DOM suite) and convert `js_dom_get_property_impl` / `js_dom_set_property_impl` / `js_dom_element_operation_impl` / `js_document_get_property` to `switch`. (c) `dom_walk_descendants(root, flags, visit, ud)` replacing 12 recursive collectors and the 4 recursive `std::function` lambdas (`:7703-7847`, `:9561`) — drops `<functional>`. (d) Route `_register_live_form_collection`/`_register_live_lookup_collection` and the three `_refresh_live_*` copies through the existing template + macro. (e) Event-init stamping: `static const EventField` tables + `stamp_fields()`; shared pointer-geometry stamp. (f) `js_install_interface()` + 8-row table for `js_register_clipboard_globals`. (g) Shared `k_url_props[9]` accessor table for document/location (and reuse from `js_history.cpp`). (h) `inherited_enum_attr()` for the 4 mapper+ancestor-walk pairs. (i) Promote one DOMRect builder + one set of object-prop micro-helpers into `js_props.h`; export one camel→css-prop converter.
- **C3.7 Misc stdlib** (WS10, −~520): one `js_source_scan(source, len, cb, ctx)` tokenizer in `js_scope.cpp` with the 6 scanners as callbacks; `js_encoding.{h,cpp}` (`js_encoding_from_item`, `js_bytes_to_string`, `js_string_to_bytes`) consuming the crypto/buffer duplicates (hex/base64 primitives stay in `lib/`); OpenSSL dlsym X-macro `X(field, ret, params, "SYMBOL", required)` generating struct + loader + availability check for the DSA (44 fields) and Ed (9) backends; `build_js_ast.cpp` sorted-table + `bsearch` dispatch for the 34-branch expression / 23-branch statement `strcmp` chains + `JS_AST_NEW(Type, KIND, node)` macro (~55 unwrapped sites).
- **C3.8 Runtime tables** (WS7, −~700): op-indexed kernel tables + `JS_OP_HAS(op, FLAG)` bitmasks for `js_array_intrinsic_algorithm_impl` / `js_indexed_intrinsic_algorithm` / `js_string_intrinsic_algorithm` (collapses the 100 ≤5-line forwarder blocks and the 11 membership disjunctions); `JsBuiltinProtoSpec` table + one materialization walker + `js_define_data_prop(obj, name, v, flags)` for `js_get_key_core:5241-5720`; extend the `JS_HOST_META_*_OP` macros over the meta/promise `JsPropertyOpResult` families. **Optional (D-2):** topological reordering to drop ~124 forward-decl lines — high diff-blame churn for small gain; do only if the files are already being heavily edited.

Stage C3 net: **≈ −6,000**. Gates: per-commit suite runs as in C2; DOM work additionally gates on the `test/js` DOM/document set; C3.3 gates on the full `test/node` set.

---

## 6. Stage C4 — Semantic unifications (spec/golden-visible, heavier gates)

- **C4.1 One value renderer** (WS5, −~1,000): fold `js_assert.cpp`'s 68 appender/message functions (1,933 lines) into the `js_util` inspect walker parameterized by `JsRenderStyle { sign; indent_step; force_multiline; diff_mode; depth; }` + a `{JsClass, render_fn}` table. Migrate appender-by-appender, running the assert corpus after each; **failure-message text is the contract** (golden-tested against Node output) — any diff is a bug in the port.
- **C4.2 Deep-equal modes** (WS5, −~400): add `JsDeepMode { STRICT, LOOSE, PARTIAL }` to `JsDeepEqualContext`; delete `js_assert.cpp:3756-4550`'s parallel partial matcher; assert already routes non-partial through `js_util_isDeepStrictEqual` (`:2658`). Promote the 8 duplicated predicates to a shared header as part of the move.
- **C4.3 Own-keys unification** (WS8, −~430): `js_own_keys(obj, JsOwnKeyFilter{enumerable_only, include_symbols, include_length, proto_chain, skip_deleted})` replacing the 13-function family; the String-wrapper shape walk (written 3×) becomes one function. Gate: test262 own-keys/for-in order sections + proxy `ownKeys` traps.
- **C4.4 One property-descriptor pipeline** (WS8, −~350, **highest spec risk — land last**): make `JsPropertyDescriptor` the only IR; `js_object_define_property` parses Item→POD once via `js_descriptor_from_object` and delegates to the `js_props.cpp` engine; descriptor read-back only through `js_property_descriptor_from_pd`. Gate: full test262 property sections + `test/js/props` invariant harness, before and after, zero delta.

Stage C4 net: **≈ −2,200**.

---

## 7. Stage C5 — High-risk consolidations (opt-in, each needs a go/no-go)

- **C5.1 Regex strategy** (WS6, −~1,375): **prerequisite (D-3):** benchmark lookaround/backref-heavy patterns (RE2+filter emulation vs `js_bt_regex`) using `test/js_runtime_bench` on a release build; flip `js_regex_needs_backtrack()` (`js_runtime.cpp:16045`) to route all lookaround + backrefs + `/v` set-ops to the backtracker only if within an agreed budget. Then delete the emulation layer (`js_regex_wrapper.cpp` assertion surgery, marker/filter runtime, two-pass backref retry, the 796-line `/v` rewriter) — this also removes the file's 114 `std::` sites. Share one class parser + UTF-8 decoder between engines (−~250). Runtime side: kind-indexed `JsRegexRange` table + alias table (−~125). Gate: test262 RegExp sections + `test_js_bt_regex_gtest`, full.
- **C5.2 Shared socket transport** (WS4, −~600): `JsNodeTransport` (handle, write queue, drain/EOF/close flags) + `js_node_transport_write/flush/fail/shutdown/close`, composed into `JsSocket` → `JsTlsSocket` → http conn/client in that order, one per commit, `test/node` green between each.
- **C5.3 Array-lane unification** (WS11, −~500): one kernel over a 4-function accessor vtable (`length/get/set/create_result`); converge **method-by-method** (start: `at`, `includes`, `indexOf`, `fill`, `slice`), each behind the relevant test262 sections; the lanes differ in observable details (detached-buffer checks, `ArraySpeciesCreate` vs `TypedArrayCreate`, live length) — any method whose semantics don't factor cleanly stays unmerged.
- **C5.4 TS/JS AST builder merge** (WS10, −~200, low confidence): fold the 22 `build_ts_*_u` builders into JS counterparts behind `ts_mode`; verify against the TS corpus first; abandon cheaply if similarity is worse than measured.
- **C5.5 `js_get_key_core` / `js_set_*_core` decomposition** (−800–1,500, unscoped): run a dedicated scoping pass over `js_runtime.cpp:4692-5731` and `:6357-7248` after C3.8 lands (the builtin-proto table removes 480 lines of it first); produce a follow-up plan before touching.

---

## 8. Stage C6 — File splits and placement (structure only; after churn settles)

- **C6.1** `js_runtime.cpp` → extract `:31850-36759` (vm, diagnostics_channel, async_hooks, domain, cluster, repl, AsyncLocalStorage, trace_events, bindings, compile cache) into `js_node_modules.cpp` (or 2–3 files along those seams).
- **C6.2** `js_runtime.cpp` → extract the regex block (`:14370-19133`) into `js_regex_runtime.cpp` (smaller if C5.1 ran first).
- **C6.3** `js_dom.cpp` → extract SVG geometry to `js_dom_svg.cpp`; first de-`static` radiant's `parse_svg_viewbox` / `build_path_from_svg_shape` / `parse_points_to_path` behind a new `radiant/svg_geometry.h` (promote, don't copy — rule 13) and delete js_dom's reimplementations (−~300 net; `svg_parse_transform` is already shared correctly).
- **C6.4** Move the test262 native asserts (`js_globals.cpp:9417-10242`, `:4978-5044`; catalog in `js_test262_fast_paths.h`) to `js_test262_natives.cpp`; default `JS_TEST262_FAST_PATHS` to **0 for release** in `build_lambda_config.json` while the test262 gtest build keeps it 1. Verify `make release` binary size drops and `test262-baseline` still passes (it builds with the flag on).
- **C6.5** Replace the `std::string`/`std::unordered_map` islands remaining in `js_runtime.cpp` (`:15090` hex-property builder → `StrBuf`; `:16645` fold-expansion map → `lib/hashmap`) — smaller after C5.1/C6.2.
- All splits: `build_lambda_config.json` gains the new TUs (never edit the Lua); each split commit is move-only (no edits in the moved code) so blame survives.

---

## 9. Gates and test matrix

| Gate | When |
|---|---|
| `make build` clean, zero new warnings | every commit |
| `make test-lambda-baseline` 100% | every commit |
| `./test/test_js_gtest.exe` | every commit |
| `./test/test_js_mir_emission_gtest.exe` | every commit touching `js_mir_*` (C1, C2.3–C2.5, C3.1, C3.2) |
| `make test262-baseline` | every batch in C2–C4; every commit in C4/C5 |
| `test/node` set (`./lambda.exe js test/node/X.js --no-log \| diff -u test/node/X.txt -`) | every commit touching Node modules (C0, C2.7–C2.8, C3.3–C3.4, C5.2) |
| `utils/check_js_node_test_separation.py` | when adding tests |
| `test_js_bt_regex_gtest` + test262 RegExp | C5.1 |
| `test/js/props` invariant harness | C4.4 |
| ASAN run of http/net/tls node tests | C0.4, C3.3, C5.2 |
| `make test` + `test262-full` | end of every stage, before merge |
| Release-build benchmark (`test/js_runtime_bench`, rule 10) | before/after C3.6(b), C3.8, C5.1 |

Commit discipline: one task (or one file-batch of a mechanical task) per commit, message prefixed `js-cleanup CN.M:`; stages land in order; C5 tasks each get an explicit go/no-go against their prerequisite.

---

## 10. Tracking checklist

- [x] C0.1 TLS `once()` fix + `test/node/tls_socket_once.{js,txt}`
- [x] C0.2 HTTP multi-listener fix + `test/node/http_multiple_listeners.{js,txt}`
- [x] C0.3 fs `syscall` fix + `test/node/fs_error_syscall.{js,txt}`
- [x] C0.4 HTTP write-error ownership fix + ASAN verification
- [x] C0.5 DOM reflection gating fix + `test/js/dom_reflect_gating.{js,html,txt}`
- [x] C1.1 rename `jm_emit_class_static_field_value_simple`
- [x] C1.2 delete duplicate op table (`expression_lowering:4575`)
- [x] C1.3 DOM dead code (formdata shim, 2 zero-caller fns)
- [x] C1.4 globals orphan decls + duplicate includes
- [x] C1.5 `jm_strict_put`
- [x] C2.1 name-key helpers (runtime → globals → DOM batches)
- [x] C2.2 `JS_ROOTS` (runtime → globals batches)
- [x] C2.3 `jm_callr_N` (per MIR file)
- [x] C2.4 `jm_emit_branch/jmp/ret`
- [x] C2.5 `jm_find_module_const` + `jm_var_name`
- [x] C2.6 `js_throw_*_errorf` + `js_throw_delete_rejected`
- [x] C2.7 `js_node_common.hpp` micro-clones
- [x] C2.8 `js_node_uv.cpp` write + error factory
- [x] C2.9 `js_map_own_or/_string`
- [x] C2.10 `js_species_constructor`
- [x] C2.11 join/toLocaleString kernel
- [~] C3.1 `JS_AST_CHILDREN` visitor — table + visitor landed; 1 of ~40 walkers migrated
- [ ] C3.2 `JsMirCompileUnit` (5 pipeline migrations)
- [ ] C3.3 `js_node_emitter` (8 module migrations)
- [ ] C3.4 `JS_TICK_N` / `JS_ENV_UNPACK` (D-1 decided)
- [ ] C3.5 globals tables (Date, URI, Array.from, MessagePort, process, symbol/groupBy)
- [ ] C3.6 DOM tables (reflection, prop-ID, walker, collections, events, interfaces, URL, enum-attrs, micro-helpers)
- [ ] C3.7 misc (scanner, encoding, dlsym, bsearch dispatch)
- [ ] C3.8 runtime tables (op dispatch, builtin-proto, PropertyOps; D-2 decided)
- [ ] C4.1 unified renderer (golden-gated)
- [ ] C4.2 deep-equal modes
- [ ] C4.3 `js_own_keys`
- [ ] C4.4 descriptor pipeline (test262-gated)
- [ ] C5.1 regex routing flip (D-3 benchmark) + emulation deletion
- [ ] C5.2 `JsNodeTransport` (net → tls → http)
- [ ] C5.3 array-lane merge (method list, per-method)
- [ ] C5.4 TS builder merge (corpus-verified)
- [ ] C5.5 get/set-core scoping pass → follow-up plan
- [ ] C6.1–C6.5 file splits, radiant promotions, test262 release gating, `std::` island removal

## 11. Success criteria

1. All five C0 bugs fixed, each locked by a committed regression test that survives every later stage.
2. Net LOC: ≥5,000 removed by end of C4 (nominal budget: C1 ≈ 150, C2 ≈ 2,800, C3 ≈ 6,000, C4 ≈ 2,200 — target holds at ~45% realization); C5 upside ≈ 2,700; C6 relocates ~9,600 out of `js_runtime.cpp` and 892 out of `js_globals.cpp`.
3. Zero regressions: `test-lambda-baseline`, `test_js_gtest`, `test_js_mir_emission_gtest`, `test262-baseline` stay 100% throughout; `test262-full` delta-free at stage boundaries.
4. `std::` usage in `lambda/js/` strictly decreases (regex wrapper, DOM lambdas, runtime islands); zero new occurrences (enforceable by grep at stage gates).
5. No perf regressions on `test/js_runtime_bench` (release build); DOM property access and AST build dispatch are expected to *improve* (strcmp chains → id switch / bsearch).
6. Every deleted "duplicate" is deleted, not orphaned: stage-end grep confirms no remaining references to removed statics/aliases.

---

## 12. Implementation findings (recorded during execution)

Three root causes in this plan did not match the tree and were re-derived
against it before fixing:

- **C0.3** `make_fs_error` (`js_fs.cpp`) is shadowed: `require('fs')` resolves
  through the Jube `node_fs` native module, whose error factory already names
  most syscalls correctly, so the hardcoded `"access"` was not observable.
  `make_fs_error` was still fixed (it is live wherever `node_fs` is not
  loaded, and its `(status, syscall, path)` shape seeds C2.8). The two wrong
  names in the live path — `readdir` and `realpath` — were fixed to Node's
  `scandir`/`lstat`.
- **C0.4** The named site (`http_conn_write_bytes`) already obeyed the
  single-owner rule, as do `js_net.cpp` and `js_child_process.cpp`. The real
  defect is the opposite: the two http *client* `uv_write` sites ignored the
  return value, leaking the wrapper and dropping the caller's write callback.
- **C0.5** The asymmetry runs the other way: the *setter* applied the
  `_idl_to_attr_name` mapping ungated on any element while the getter gates
  every pair by tag, so unsupported pairs wrote an attribute that could not be
  read back. The gate was added on the setter side.
- **C1.3** `js_dom_collection_has_live_property_state` is not dead — it has a
  live caller in `js_runtime.cpp`. Only `js_is_document_proxy` was deleted.

**Open, not yet done (needs a decision):** C0.3's stated acceptance test also
asserts `err.path`. The live `node_fs` error factory reaches the host through
`JubeHostNodeErrorAPI::throw_system_error(session, syscall, error_number)`
(`lambda/jube/jube.h`), which has no `path` parameter, so no fs error carries
`err.path` today. Adding it means widening that versioned host ABI
(`JUBE_HOST_API_VERSION`) and threading a path through ~23 `node_fs_sync_error`
call sites. Deferred pending approval.

---

## 13. Status at end of the first implementation pass

Landed and gated (18 commits, `test-lambda-baseline` 3870/3870 and the
`test/node` set delta-free at every commit):

- **C0** — all five bug fixes, four with committed regression tests.
- **C1** — all five items.
- **C2** — all eleven items (C2.4 landed before C2.1–C2.3).
- **C3.1** — the shared child table (`lambda/js/js_ast_children.cpp`),
  `js_ast_visit_children` / `js_ast_any_child`, and the first walker migration
  (`jm_collect_enclosing_lexicals_for_target`, 146 → 34 lines).

Net so far: **−1,400 lines** across `lambda/`.

### Revised expectation for the rest of C3.1

The 1,500-line estimate assumed the walkers are *complete* traversals whose
`default:` can simply delegate. Many are not: `jm_node_has_direct_eval_call`,
for example, returns false for for/while/try/switch/object/array and every
other unlisted kind, so delegating would newly descend into them. Under the
correctness rule those kinds must become explicit skip cases, which costs back
most of what the table saves.

The split to expect:

- **Complete walkers** (the one migrated is representative): ~75% reduction.
- **Deliberately partial walkers**: little or no reduction; they should keep
  their allow-list shape, and the table is not the right tool for them.

Before migrating each remaining walker, classify it first. A useful signal is
whether its `default:` recurses generically or returns/breaks.

### Not started

C3.2–C3.8, C4, C5, C6.
