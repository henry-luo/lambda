# LambdaJS LOC Reduction — Round 3 (Verified Re-scope + New Findings)

**Status:** proposal. **Date:** 2026-08-17. **Scope:** `lambda/js/` (~206.6K lines) plus the `lambda/module/node_core/` boilerplate it clones. **Target:** ≥5,000 net lines removed, reached without the two highest-risk workstreams; ~7,800 discounted identified in total.

**Relation to prior docs:** `vibe/Lambda_Impl_JS_LOC_Reduction.md` (round-2 analysis, WS1–WS12) and `vibe/Lambda_Impl_JS_Clean_Up.md` (execution plan C0–C6, §12–§17 record what landed). Round 2 landed **−1,487 net** across 41 `js-cleanup` commits (C0–C2 complete, C3 mostly complete). This document re-scopes every remaining C3–C6 item against the current tree — several round-2 estimates do not survive contact — and adds a fresh seven-way audit's new findings. Where this doc and round-2 docs disagree, **this doc wins**; the C4/C5 sections of the clean-up doc are superseded by §5 below.

Relation to the AST-interpreter decision (**D8.1.1v2**: T0 interpreter default + per-function MIR tier-up): unchanged from round 2 — MIR lowering remains the tier-up backend, so the MIR consolidations (§5.G) stay worthwhile.

**This document is self-contained.** Everything still outstanding from the two round-2 docs has been absorbed here, including the invariants, gates, non-goals, and the judgement calls recorded during round-2 execution. The round-2 docs are retained only as the historical estimate-vs-actual record; no round-3 work requires reading them.

---

## 0. Governing invariants and non-goals

Carried forward from round 2 — these constrain every change below and were load-bearing during round-2 execution.

1. **Behavior changes only where a §4 bug fix says so.** Every phase except R3.0 is intended to be observationally neutral; a diff that changes a test expectation outside R3.0 is a defect in the refactor, not a test to update.
2. **Bug fixes land before the refactors that absorb them**, each with a regression test committed in the same change and a root-cause comment at the fix point (CLAUDE.md rule 12).
3. **No `std::` anywhere new** (rule 3). This plan *removes* `std::` (regex wrapper deletion §5.C, plus the follow-up port of the kept `/v` rewriter); it must never add any. Use `lib/` `Str`/`StrBuf`/`ArrayList`/`HashMap`.
4. **Promote, never copy** (rule 13). Where a shared helper replaces per-file statics (abort binding, emitter adapters, uv plumbing, radiant SVG geometry, the "Received type" formatter), the shared version lives in a module header/TU and the statics are deleted in the same change. Note the recurring root cause: **a canonical implementation that is file-`static` with no header declaration invites re-cloning** — `js_format_invalid_arg_received` (§5.F2) got copied four times exactly this way.
5. **Interning is semantics, not style.** Property-key construction stays on `heap_create_name` (name pool). Never route converted sites through `js_get_key_cstr`/`make_string_item` — those heap-copy, break name identity, and add a `RootFrame` per call on hot paths.
6. **Precise rooting only** (rule 15) — `RootFrame`/`Rooted` ownership; never conservative native-stack scanning.
7. **Never mask test262 issues** (rule 18). Baselines stay at 100%; a failing test gets a root-cause fix, never a filter edit.
8. **Performance claims are measured, not asserted**, on release builds only (rule 10). Round 2's lesson: the DOM prop-id table was predicted to speed up property access and measurably did not (§3).
9. **Test placement:** Node-module regressions go in `test/node/*.js` + paired `*.txt` (enforced by `utils/check_js_node_test_separation.py`); core-JS/DOM regressions go in `test/js/` (+ `.html` for document tests). Every new `.js` test gets its `.txt` expectation in the same commit.
10. **Build config, not Lua** (rule 7): new TUs and flag defaults change in `build_lambda_config.json`, never in the generated `.lua`.

**Non-goals.** No behavior change outside §4. No `transpile.cpp`/C2MIR work (frozen, rule 14). No vendored-code edits (rule 16) — radiant helpers are *promoted*, never copied. No conversion of interned-name paths to heap-copying string helpers. No big-bang rewrite of the array lanes (now a no-go, §1) or the socket transport (re-scoped, §5.D2) — staged only.

---

## 1. Where round 2's remaining estimates were wrong (verdicts)

Each verdict below was verified by direct code reading (and for regex, by executing probe scripts); evidence in §5.

| Round-2 item | Was | Now | Why |
|---|---:|---:|---|
| C4.1 renderer merge | −1,000 | **−430** | The assert diff engine (LCS alignment, first-mismatch summarizers, ~1,140 lines) takes `(actual, expected)` pairs — it has no counterpart in the one-value `util.inspect` walker and cannot fold into it. Only the single-value walker twin (~154 lines), string/escape/date fragments, and message scaffolding merge. |
| C5.1 regex | −1,375 | **−1,925** | Larger than planned, but composed differently: the 796-line `/v` rewriter must **stay** (it runs before routing and both engines consume its output); the wrapper's two-pass backref lane (~250) is **already dead code**; runtime-side deletions are bigger than assumed. Five live spec bugs must be fixed first (§4.1). |
| C5.2 socket transport | −600 | **−130** | net/tls/http/child share vocabulary, not a state machine: net has the only full transport; tls coalesces plaintext gated on handshake with a borrowed handle; http is framing over the landed `js_node_uv` layer; child stdio has no drain contract at all. Re-scope: shared drain-accounting helpers + adopt `js_node_stream_write_owned` in child/tls. Never replace `js_node_uv.cpp` — compose on it (`js_net.cpp:883`, `js_http.cpp:2543` already prove the seam). |
| C5.3 array-lane vtable | −500 | **no-go → −80** | Array vs TypedArray method pairs differ observably at every spec-cited step (len-before-coercion, detached checks, HasProperty-skip, equality domain, SpeciesCreate forks — comments cite Js54/Js55). A `{length/get/set}` vtable boxes every element and forfeits both lanes' fast paths. Replace with an intra-TA-lane cleanup: the symbol→ToNumber→NaN→trunc→relative-clamp stanza repeats ~8–9× and the OOB 3-liner ~12× inside `js_indexed_intrinsic_algorithm`. |
| C5.4 TS builder merge | −200 | **−130** | 12 of the 22 `build_ts_*_u` builders are TsTypeNode builders with **no JS counterpart** — nothing to fold. Real pairs: variable_decl + function (~150 net), class_decl (~40). Skip class_body (TS synthesizes ctor param-property assignments; no JS seam) and all type builders. |
| C5.5 get/set-core | −800..1,500 | **−600** | The old range implicitly included the ~480 lines the landed `JsBuiltinProtoSpec` table already absorbed. Honest remainder scoped in §5.A. |
| C3.8 remainder | −700 | **closed, ~0** | Already realized: 217 X-macro forwarder bodies exist; the "~100 forwarder blocks" are one-liners now. A kernel table would need per-signature adapters that cost what the dispatch lines save, at hot-path risk. Meta/promise `JsPropertyOps` extension likewise <50 nominal — record both as done. |
| C3.1 remainder | (rest of −1,500) | **−600 plan-credible** (930 best-case) | Full walker inventory in §5.G classifies every remaining walker COMPLETE / COMPLETE-WITH-GAPS / CTX / PARTIAL. PARTIAL walkers (allow-lists) save nothing; gap-preserving explicit cases eat 10–20% in the rest. |
| C3.2 remainder | −575 total | **−135** | Failure lanes already share `js_mir_compile_unit_fail`; the five success paths still duplicate ~200 lines of parse→lower→link→activate spine. |
| C3.3 remainder | (rest of −475) | **−250** | Five modules left; per-module scoping in §5.D. Stream goes last and requires `captureRejections` in `node_events.cpp` first (+35 there). |

Two round-2 exclusions were re-checked and found stale: `constructor_prototype` was **not** promoted into `js_node_common.hpp` (4 clones remain, ~52 LOC — below threshold, folded into §5.D residue), and `js_runtime.cpp`/`js_globals.cpp` outside the property/regex/node blocks are **cleaner than round 2 assumed** — a normalized sliding-window clone scan found zero qualifying textual clones; their remaining wins are §5.A and relocation (§6).

---

## 2. Headline numbers

| | Nominal | Discounted |
|---|---:|---:|
| A. Property protocol (get/set core, own-keys, own-presence, descriptors) | ~1,720 | **−1,430** |
| B. assert/util (renderer, deep-equal PARTIAL mode, LCS, idioms) | ~1,400 | **−980** |
| C. Regex engine consolidation (staged, gated) | ~2,260 | **−1,925** |
| D. Node modules (emitter completion, transport re-scope, stream internals, cross-module families) | ~1,900 | **−1,260** |
| E. Runtime-core batched residue | ~320 | **−230** |
| F. Encodings, formatters, kits, verdict replacements (crypto, TA, TS, scanner) | ~1,400 | **−985** |
| G. MIR + DOM structure (walkers, compile unit, class-expression, tables, fetch) | ~1,850 | **−1,070** |
| H. Round-2 orphaned sub-items (re-verified remainder) | ~390 | **−263** |
| **Total** | **~11,250** | **≈ −8,140** |

Each workstream total is the sum of its own sub-items in §5, and §7's phase totals partition those same sub-items — the two tables reconcile to within rounding. Where a workstream is split across phases (D3 stream internals, the F batches, the H items) the split is named explicitly in §7.

The ≥5,000 target is reached by phases R3.1–R3.5 alone (**≈ −5,700**, excluding the regex flip and with the descriptor pipeline last); the regex workstream is ~−1,900 of gated upside on top. At round 2's observed realization rate (~55% of discounted for structural work, higher for mechanical), the floor is ~4,300–5,000 from the core path — the regex stage or the E/F mechanical batches close any shortfall.

---

## 3. What round 3 does NOT propose (verified clean — do not re-open)

- **Intrinsic dispatch**: `JS_*_INTRINSIC_BODIES` X-macros (217 bodies), `JS_OP_FLAG_*` bitmasks, `JsBuiltinProtoSpec`, builtin catalog/registry — done in rounds 1–2.
- **Promise machinery** (finely decomposed; combinators are 4-line wrappers over one iterable kernel), **Map/Set/Weak* collections** (one `js_collection_*` engine), **Math/console/namespace installs**, **error ctors**, **string HTML wrappers**, **parseInt/parseFloat**, **escape/unescape** (two essential legacy algorithms), **structuredClone vs JSON** (only the recursion skeleton overlaps — forced abstraction), **generator delegate-abrupt logic**, **timers**.
- **`js_typed_array.cpp`** (spec-table driven; single load/store switches), **`js_zlib.cpp`** (fully macro-tabled), **`js_buffer.cpp` read/write families** (`JS_BUFFER_READ_FIXED`/`WRITE_FIXED`/`FLOAT` macros), **fs promisify** (`JS_FS_PROMISE_*`), **dns wrappers** (`JS_DNS_RESOLVE_WRAPPERS`), **js_runtime_function/state/builtin_registry/event_loop** (clean), **js_props/js_property_attrs** internals (C4.4 territory only).
- **build_js_ast dispatch chains**: bsearch/`JS_AST_NEW` buy clarity, not lines (127 alloc sites are single-line; ~0–30 LOC) — skip.
- **`js_set_key_cstr` wiring**: ~1,754 call sites, ~1,722 of them single-line — a descriptor table trades one line for one row. Skip.
- **`js_object_meta.cpp`, `js_coerce.cpp`**: measured essentially optimal in round 2; no family or table opportunity.
- **DOM strcmp→switch follow-up**: the landed `JS_DOM_PROPS` if-chains convert 1:1 to switch — **~0 LOC**; do it for jump-table dispatch and case-exhaustiveness only, and do not book lines. Round 2 measured the prop-id table itself: it cost **+195 lines** and bought **no wall-clock** (the `test/js` DOM suite ran 20.88s vs 20.93s over 20 runs; a property-access microbenchmark gained ~2%, 0.964s → 0.948s). The JS execution loop dominates, not the dispatch. It was kept only because 201 scattered string literals became one list and a misspelled property name became a compile error instead of a silently dead branch.
- **Event-init stamping** (DOM): the three native event creators do not share a field set — mouse inserts `detail` between `composed` and `clientX`, pointer does not, and wheel omits `pageX`/`pageY`/`button` and puts its deltas before the modifiers. **Property insertion order is observable through `Object.keys`**, so a shared stamp must be parameterized per site — more lines than it saves. (The narrower prologue/geometry helpers *are* worth it — that is §5.G6, a different cut.)
- **`inherited_enum_attr`** (DOM): only two of the four getters walk ancestors (`spellcheck`, `writingSuggestions`); `autocapitalize`/`autocorrect` inherit from the **form owner**, a different rule. Sharing within each pair saves ~8 lines against a ~10-line helper.
- **`js_process_listener_op`**: the six ops have disjoint bodies — `on` flushes and re-refs IPC, `removeAllListeners` clears the fixed lists and re-inits roots, `once` wraps, `removeListener` unwraps once-wrappers. Only `listenerCount` and `listeners` share a three-line preamble, less than a helper would cost.
- **MIR forward-decl topological reordering** (round-2 D-2): ~124 forward-decl-only lines could be dropped by reordering, but the diff-blame churn outweighs the gain. Declined.

---

## 4. Bugs and hazards found by this audit (fix first, with tests — round-2 C0 discipline)

Behavioral fixes land before the refactors that absorb them, each with a regression test and a root-cause comment (CLAUDE.md rule 12).

### 4.1 Confirmed by execution (regex, blocking §5.C)
`bt_pattern` is captured (`js_runtime.cpp:17198`) **before** escape normalization, so patterns already routed to the backtracker mis-parse: (1) `\uHHHH` (bt `parse_atom` has no `u` case), (2) `\u{...}`, (3) `\p{...}` (parsed as literal `p`), (4) `\cX`, (5) non-ASCII `/i` folding (`bt_canon` is ASCII-only, `js_bt_regex.cpp:171-176`). Fix = split preprocessing into engine-neutral normalization **before** routing vs RE2-only rewrites after (§5.C stage 0). Independently justified; gates on new `test_js_bt_regex_gtest` cases.

### 4.1b `LMD_TYPE_ARRAY_NUM` is invisible to the JS layer (FOUND DURING R3.0 — not in the audit)

**The whole seven-agent audit missed this class.** A JS array whose elements are
all numeric — and a freshly built `[]` — is stored in the `LMD_TYPE_ARRAY_NUM`
lane, not `LMD_TYPE_ARRAY`; pushing a non-numeric element promotes it. The JS
layer classifies values by the bare tag in **~650 sites** and `lambda.h`'s
canonical `is_array_family_type_id()` had **zero adopters** in `lambda/js/`.
Most sites are benign (arrays built internally by `js_array_new` are always
`LMD_TYPE_ARRAY`), but every site that classifies a *user-supplied* value is
wrong for numeric arrays.

Confirmed broken and fixed in R3.0: `assert.deepStrictEqual([1,2],[1,2])` and
`util.isDeepStrictEqual([],[])` returned **false** (the object-like predicate
dropped the array to the primitive path and compared Item pointers);
`util.inspect([1,2])` printed `[1,2]` instead of Node's `[ 1, 2 ]` (the
dispatch missed the array renderer and fell through to `js_json_stringify`);
`util.types.isArray([])` was false; `markAsUntransferable([])` silently did
nothing. Nine sites in `js_util.cpp` and one in `js_globals.cpp` now use
`is_array_family_type_id`.

**Still outstanding — the remaining ~640 raw sites are unaudited.** The
per-file counts are `js_runtime.cpp` 177, `js_stream.cpp` 100,
`js_globals.cpp` 83, `js_assert.cpp` 44, `js_http.cpp` 34, `js_clipboard.cpp`
32, `js_dom.cpp` 26, `js_child_process.cpp` 22, then a long tail. A blanket
substitution is **wrong** — internal-construction sites legitimately mean
`LMD_TYPE_ARRAY`. Each site must be classified as *user-value classification*
(fix) vs *internal construction* (leave). `js_assert.cpp`'s 44 are the highest
priority: they decide assert message rendering, which is §5.B's golden
contract, so they must be settled **before** B starts or the goldens will
shift under the refactor. `js_stream.cpp`'s 100 are the next concern (12
stream tests still fail).

### 4.2 Property protocol
- String-wrapper presence drift: `js_property_ops_has_property` re-inlines the index probe bounds-checked against **byte** length (`js_globals.cpp:1042`) while `js_string_exotic_index_in_range` uses **UTF-16** length — `in`/HasProperty disagrees with `hasOwnProperty` on non-ASCII wrappers.
- `js_in`'s array lane lacks `js_has_own_property`'s numeric-array and identity-key companion paths (drift trio; §5.A4).
- Set-miss walks the prototype chain **three times** with inconsistent depth caps (16/32/64/100 across 28 bounded walks) — §5.A1(g) fixes by construction.

### 4.3 Node modules
- `js_http_server_address` hand-rolls sockaddr and handles only `AF_INET` — IPv6-bound HTTP server returns `{}` (net/tls already use the shared `js_node_tcp_server_address`).
- `child_install_abort_signal` (`js_child_process.cpp:421`) registers a listener without `is_callable`, unlike all six siblings; abort-error `cause` stamping differs per module (only fs/readline stamp it).
- tls is the only module with zero `js_node_uv_error` adoption: `js_tls.cpp:2003/:2364` build errors without `code`/`errno`.
- Buffer: `slice` **copies** instead of returning a view; `fill` ignores string values and offset/end (Node divergence — separate tickets).
- latin1 string→bytes: buffer memcpys raw UTF-8 (`js_buffer.cpp:1510-1514`) while crypto decodes+masks `&0xFF` (`js_crypto.cpp:1276-1287`) — different non-ASCII output; resolve against Node before merging the encoders (§5.F3).
- Stream: identical if/else branches at `js_stream.cpp:2056-2060`; duplex init drops `key_reading_sync` that readable init sets; `__writestream_error_cb__` is write-only (dead); three parallel listener stores (`__listeners__`, `_events` pipe-noop shadow, `__events__`).
- `js_https.cpp` dispatches listener keys nothing writes (`__on_connect__`/`__on_request__` — dead); `js_http_request`'s inline URL parse has dead `plen` computation and lacks userinfo/IPv6/query handling that `https_parse_url_string` has.

### 4.4 MIR walkers (systematic gap pattern — decide close-vs-preserve per walker, with test262 diff)
**Spread scan missing on one `super()` arm:** the member-expression-heritage arm (`js_mir_expression_lowering.cpp:5375`) calls `jm_build_args_array` with **no spread scan**, unlike the other four arms — so `class A extends obj.B { constructor(){ super(...xs) } }` takes the non-spread path. Fix with H2, or before it.

**Unreachable code + dead runtime function:** the second `decimalToPercentHexString` block (`js_mir_expression_lowering.cpp:5673-5677`) can never run — the block at `:5609` matches the same name with a weaker guard (`arg_count >= 1` vs `== 1`) and returns first. Its runtime target `js_decimal_to_percent_hex_string` (`js_globals.cpp:10085`) has no other caller (`js_test262_decimal_to_percent_hex_string` at `:9268` is the live one); both are registered in `js_test262_fast_paths.h:21,23`. Delete both with H1.

**Walker gap pattern.** Five walkers independently miss `DO_WHILE`/`FOR_OF`/`TRY`; three miss `NEW`/`CONDITIONAL`. Notable latent bugs: `jm_node_has_direct_eval_call` misses `cond ? eval(x) : y`; `jm_emit_evalscript_global_decl_prechecks` misses `var` inside `while` bodies; `jm_callsite_scan_node` misses callbacks in `new Foo(cb)`; `jm_mutable_native_var_needs_boxing_walk` misses assignments inside `try{}` (native-type miscompile lane). Also: the statement-path class lowering lacks the generator yield-spills the expression path has around static key/value.

### 4.5 Misc
- `cssom_camel_to_css_prop` is a strict subset of `js_camel_to_css_prop` (missing `cssFloat`/`cssText`) — `style.cssFloat` behaves differently through the CSSOM path.
- `js_crypto_getCiphers` re-hand-lists 14 names that `crypto_cipher_infos[]` also carries (drift hazard); resolve/validate helpers duplicate the same fact base 6×.
- bt silent failure modes: >255 capture groups and step-budget exhaustion both return no-match with only a log — add counters before the regex flip gates on them.
- **Precondition for §5.B:** three official-Node baseline tests are failing pre-existing (`test-assert-partial-deep-equal.js` — "is not a constructor", likely missing Float16Array; `test-assert-typedarray-deepequal.js` — Invalid typed array length; `test-util-inspect.js` — AsyncFunction label). Fix or formally re-baseline **before** touching assert/util, or merge regressions are unattributable. Also freeze the `util.inspect` Map/Set `{}` fidelity gap during the merge.
- **Open decision carried from round 2, still awaiting approval — fs errors carry no `err.path`.** The live `node_fs` error factory reaches the host through `JubeHostNodeErrorAPI::throw_system_error(session, syscall, error_number)` (`lambda/jube/jube.h`), which has **no `path` parameter**, so no fs error carries `err.path` today. Adding it means widening that versioned host ABI (`JUBE_HOST_API_VERSION`) and threading a path through ~23 `node_fs_sync_error` call sites. Not part of any phase below; needs a go-ahead first.
- **Clipboard/Blob interface oddities exposed by round-2's C3.6(f) interface probe** (pre-existing, unrelated to that refactor, never triaged): `Blob.prototype.text` is undefined; `File.prototype` is **not** chained to `Blob.prototype`; `DataTransfer.name` reads `"Clipboard"` because `Clipboard` reuses `js_data_transfer_new` and native constructors are cached by target. Worth a separate look — they are spec-conformance gaps, not LOC.

---

## 5. Workstreams (evidence-backed, current-tree line numbers)

### A. Property protocol — net ≈ −1,430

The protocol spans `js_runtime.cpp` (~4.4K of it), `js_globals.cpp` (~3.9K), `js_props.cpp`, `js_property_attrs.cpp` (~10.5K total). Hot/cold rule: the named-fast infra (`js_runtime.cpp:8055-8267`), MAP own-get sequence (`:4945-4983`), ARRAY dense read (`:5225-5236`), FUNC own-get (`:5351-5371`) stay inline; extraction targets only cold phases. Gate every step with `JS_EXEC_PROFILE=1` named-fast hit-rate delta = 0.

**A1. get/set-core decomposition (C5.5v2) — −600 as scoped, but see the two items re-measured in execution: (a) is ≈ −20, not −250, and (d) is ≈ −25, not −100. The honest remaining figure for A1 is closer to −250.** `js_get_key_core` is `:4760-5783` (1,024 lines); set cores `:6404-7468`; the real OrdinarySet is `js_set_completion_with_key` (`js_globals.cpp:6427-6755`). Items: (a) the lazy `.prototype` init block — **re-measured in execution: ≈ −20, not −250. Done; do not re-open.** The estimate assumed the block was repetitive. It is not: its 342 lines are 18 per-class blocks of *distinct ES spec conformance* (Array's @@unscopables object, Object's Annex-B `__proto__` accessor descriptor, the Error family's name/message/toString plus subclass proto chaining, Set's `keys === values` aliasing, Function's chain to %Object.prototype% plus ThrowTypeError accessors and @@hasInstance, the TypedArray base-proto link, DataView's own populator). The recurring *operations* — `js_intrinsic_set_to_string_tag`, `js_intrinsic_set_symbol_method`, `js_install_native_accessor` — are already one-line calls, and RegExp's symbol-method and accessor sets are already local tables. The only genuinely repeated shape was the data-property stanza (`heap_create_name` + `js_set_key_default` + up to three attribute marks), written 9×; those now use the existing `js_define_data_prop`, plus a new `js_define_data_prop_key` for the two symbol-keyed cases. Net −20. A spec table over the remainder would encode 18 one-off behaviours as 18 one-off callbacks and save nothing, while making property insertion order — which is observable through `Object.keys` — harder to reason about; (b) error-carrier std-field ladder ≥6 copies → one field enum + accessor pair (−60); (c) RegExp legacy statics table (−25); (d) primitive-key→string normalization — **re-measured during execution: −25, not −100; deprioritised.** The six sites are not copies of one algorithm. Two are full ladders that differ in policy (the get side wraps every result in `js_to_property_key`, roots it, and handles INT64; the set side interns raw, guards symbols, and returns errors through `JS_ASSIGN_OR_RETURN_INTO`), two normalise and immediately re-dispatch into `js_get_key_core`/`js_set_storage_mode` rather than falling through, and two are BOOL-only companion-map lookups (one already using `js_name_item`). The only safely shared core is "primitive scalar → interned name" (~25 lines), which leaves each site its own wrapping, rooting and error policy and nets ≈ −25 — not worth touching the hottest path in the engine for; (e) array own-index existence, 4 parallel impls → `js_array_own_index_status()` tri-state (−90); (f) String-wrapper `__primitiveValue__` probes (−45); (g) **one classified set-miss walk** `js_classify_set_obstacle()` replacing the three back-to-back proto walks (−50, plus the biggest perf win here — 3→1 walks on the hot-adjacent set slow path, and it unifies the inconsistent depth caps); (h) array named-get tail (−40); (i) ArraySetLength double-ToNumber ×2 → `js_array_set_length()` (−45); misc restricted-name/getter-only-throw helpers (−30). Landing pad for per-class logic is the existing `JsPropertyOps` table (`js_object_meta.h`), not a new mechanism.

**A2. own-keys unification (C4.3) — MEASURED: ≈ −50, not −430 (12% realization). Do NOT build `JsOwnKeysFilter`.** 14 walkers in `js_globals.cpp` (`:8091-9070` region + `js_reflect_own_keys:6299`, `js_error_own_property_names:6266`, `js_object_copy_enumerable_own:10116`), ~1,017 lines. ~~The skip-deleted stanza appears 29×; the String-wrapper index walk is written 3×~~ — **both counts disproven below: 11 in-family, and the wrapper walk is written once as a helper with two callers.** ~~Unified `js_own_keys(obj, JsOwnKeysFilter{...})`~~ — **costed at +84 to +124; do not build it.** Gates: test262 own-keys order sections, Proxy `ownKeys`.

**Measurement (2026-08-18, verified independently against running code).** The
LOC inventory is right (1,017 lines across 14 functions) but the duplication
premise is wrong on three counts.

*The proposed kernel already exists and is already flag-parameterized.*
`js_map_own_string_keys(Item, bool enumerable_only)` (`js_globals.cpp:8292`),
`js_array_append_companion_keys(..., bool enumerable_only, bool include_length)`
(`:7567`) and `js_append_string_wrapper_indices` (`:8083`) are exactly the
proposed dimensions, and gOPN and `Object.keys` already share them. Wrapping
them in a struct is a rename that deletes nothing.

*The five flags cannot express what the callers differ on.* At least eight
further dimensions are needed and several are non-orthogonal: three different
exotic-dispatch routes (gOPN routes everything through
`js_property_ops_own_property_names`, `Object.keys` re-implements proxy/VMAP/TA
inline, `Reflect.ownKeys` dispatches only proxy); a proxy recursion boundary
(`js_proxy_trap_own_keys` calls `js_reflect_own_keys(target)` three times inside
its invariant checks); a `.prototype` **materialization side effect** in gOPN
only; sparse-hash collection in `Object.keys` only; `skip_error_stack`
*conjoined* with `enumerable_only`; shape-name dedup; for-in's "skip but
remember" seen-set which `enumerable_only` cannot model; and two different
prototype accessors in one walk. Costed out, the specified merge is
**+84 to +124 lines** — it makes the file bigger.

*They do not share an ordering contract — they disagree today.* Verified by
running the engine: `Object.getOwnPropertyNames` on a sparse array returns
`["0","1","length","zz"]`, silently **dropping the sparse indices** that
`Object.keys` returns; and `Object.keys(new String("ab"))` with an extra `foo`
and index `7` returns `["0","1","foo","7"]` — a named key **before** an integer
index. Both are observable ES violations, and they are in opposite directions,
so no flag bridges them.

*Two supporting counts were wrong.* The "skip-deleted stanza ×29" is 11 inside
this family (48 repo-wide, but the rest are in `js_in`, `hasOwnProperty`, GOPD
and ArraySetLength — unrelated); and the "String-wrapper walk written 3×" is
written **once**, as a helper with two callers, with for-in delegating to
`Object.keys` outright.

**What to take instead (≈ −40 to −60):** merge the two near-identical passes
inside `js_map_own_string_keys` (19 identical lines, diff-verified) and promote
a `js_shape_key_is_public()` visibility predicate to `js_props.h` for the 7
remaining 4-line preambles (rule 13). **Risk is HIGH and asymmetric** — own-key
order is among the most-observed contracts in the language, and `js_for_in_keys`
bypassing the proxy `ownKeys` trap sits on an invariant path where a wrong
unification yields either a spurious `TypeError` or unbounded recursion.

**Separately, file as spec bugs with their own test262 deltas** (they will *add*
lines): gOPN dropping sparse-hash array indices, and `Object.keys` emitting
String-wrapper named keys before integer indices.

**A3. descriptor pipeline (C4.4) — MEASURED and SPLIT. LOC consolidation ≈ −55 to −115 (not −350), high risk, low value: defer. The perf defect underneath it is REAL and worth landing on its own (~30 lines).** Two live IRs confirmed. A Set that **misses the named fast path** allocates a throwaway descriptor Map object (even on the receiver==target branch), as does every for-in liveness check and every `Object.values`/`entries` key. ~~every slow-path Set … again per proto level~~ — **corrected below: monomorphic `o.x = v` on a stable shape never reaches it, and absent prototype levels do not allocate.** ~~Make POD `JsPropertyDescriptor` the only internal IR~~ — **the object form is irreducible at the two Proxy traps and the public GOPD return; it can only become a leaf adapter, and the consolidation is deferred.** Gate: full test262 property sections zero-delta.

**Measurement (2026-08-18, reproduced independently).**

*LOC: 16–33% of estimate.* The object form cannot be deleted — it is
irreducible at three ES boundaries: the Proxy `getOwnPropertyDescriptor` trap
(the invariant checks compare *the user's* object against the target), the
Proxy `defineProperty` trap (the user's object is passed through intact,
including extra own properties), and the public return of
`Object.getOwnPropertyDescriptor(s)`. The 362-line `js_object_get_own_property_descriptor`
— the biggest number in the claim — yields **−8**: it already calls the POD
kernel at 8 sites, and the ~270 remaining lines are per-class ES exotic rules
(`__proto__` suppression, RegExp virtual flags, Function `prototype` lazy
materialization, String primitive vs wrapper `length`, array dense/sparse/
companion, `Error.stack`) that **move** into `js_get_own_pd` rather than
vanishing. Same failure mode as A1(a). Realistic total: −54 order-preserving,
−116 only if the validator parse is hoisted — and that hoist **changes the
observable Has/Get trap sequence**, so it is a fork, not a free win. (Noted in
passing: the current probe order already deviates from §6.2.5.5, which
interleaves Has/Get per field starting at `enumerable`; Lambda probes
`get,set,value,writable` and throws early from
`js_define_property_validate_descriptor_object`.)

*Perf: CONFIRMED, and it is the real prize.* `js_make_data_descriptor`
(`js_globals.cpp:6973`) allocates a Map via `js_new_object()` plus four
`js_define_own_key_storage` writes, each preceded by a fresh `js_name_item` —
then `js_set_completion_with_key`'s receiver==target fast branch (`:6620`)
probes it with three `js_map_shape_lookup_ext` calls and discards it, to learn
three booleans that `js_props_desc_from_shape_slot` already had in a `uint8_t`.

Measured on this tree (1M iterations each), and independently reproduced:

| Path | ns/op |
|---|---:|
| `o.x = i`, existing slot, default flags (named fast path) | **42** |
| same, but property has `enumerable:false` (`entry->flags != 0`) | **1570** |
| `o[k] = i`, computed key (always kernel) | **1573** |

A **37× cliff** the moment the named fast path misses. It misses when the
property does not yet exist, when it has any non-default attribute, on a stored
type change, on a deleted sentinel, on a non-fast receiver — and **always** for
computed keys. Same defect hits `js_for_in_key_is_live` (per for-in key) and
`js_object_collect_enumerable_own` (`Object.values`/`entries`, per key), both
~1.6µs/key against `Object.keys`' 238ns.

Two honest corrections to the original claim: it is **not** "every Set"
(monomorphic `o.x = v` on a stable shape is already fast), and **not** once per
prototype level (absent levels return without allocating). Real-world share
sampled over bundled libraries is **1–20%**, not the 61% a microbenchmark
shows.

*Scoped fix, provably a no-op:* in the `:6614–6655` fast branch, consult the
POD `js_get_own_property_descriptor` first and read its flags directly, falling
back to the object path when it returns false. Safe because
`js_props_query_writable(m, se, …)` is literally `se ? jspd_is_writable(se) : true`
(`js_property_attrs.cpp:499`) — the same `ShapeEntry` bits the POD carries.
Four guards preserve current behaviour: skip the shortcut for `JS_CLASS_STRING`
wrappers, `JS_CLASS_ERROR` + `"stack"`, `JS_CLASS_REGEXP`, and the `__proto__`
key. ~30 lines, independent of `js_get_own_pd`, no descriptor-IR unification
required. The same 3-line treatment applies to `js_for_in_key_is_live` and
`js_object_collect_enumerable_own`.

**Recommendation: land the perf fix as its own change; treat the LOC
consolidation as deferred, low-value and high-risk.**

**A4. own-presence kernel — −50 (beyond A1e's shared helper).** `js_in` (`js_globals.cpp:5745-5913`) / `js_has_own_property` (`:10197-10399`) / `js_property_ops_has_property` (`:1002-1052`) triplicate own-membership; unify on a tri-state `js_own_presence()` built on A1(e), fixing the §4.2 byte-vs-UTF-16 bug and the `js_in` array-lane drift. Coordinate the seam with A2 (enumeration) — different kernels, same neighborhood.

Sequencing: A1(a–d) mechanical first → A1(e,f,h,i) → A4 → A2 → A1(g) → A3 last (reuses A1 helpers).

### B. assert/util unification — net ≈ −1,080

Inventory verified: 68 appender/message functions ≈ 2,176 lines in `js_assert.cpp`, in six families of very different mergeability (§1 verdict). The failure-message text is the contract — locked by the official Node suite (`test/test_node_gtest.cpp` over `ref/node/test/parallel/`; `test-assert.js` embeds 115 exact `message:` expectations; `make node-baseline`).

**B1. Fragment + predicate promotion — −230, low risk.** New `lambda/js/js_render.h` (lib types only): escaped-range/quoted-key/date-ISO/typed-array-header/chunked-string helpers shared with `js_util.cpp`'s inspect (twins verified, e.g. `append_escaped_string_range:765` ↔ `js_util_inspect_append_escaped_char:439`); plus the 8 duplicated deep-equal predicate pairs promoted (`is_real_regexp:3920`↔`:1629` etc. — assert's symbol-first key order kept as an explicit flag, it's a rendering contract).
**B2. LCS diff loop ×3 — −110, low risk (new find).** The 6-branch LCS emit loop exists verbatim-shaped at `append_array_diff_recursive:1377-1441`, inlined again in `append_object_diff_recursive:1599-1691` (with its own `build_lcs_score` copy), simplified at `:1479-1503` → one parameterized `append_array_lcs_diff()`, byte-identical output achievable.
**B3. `JsDeepMode` PARTIAL — −300 residual after B1.** util's walker already has STRICT/LOOSE (`js_util_isDeepEqual_impl:1841-2115`) and assert already delegates non-partial (`:2652`). Add PARTIAL at 6 branch points (subset objects/arrays/sets/maps, prefix buffers, TA length-≥, depth budget 32, no proxy dispatch, RegExp skips lastIndex); the subset engine moves beside the walker; delete `:3897-4433`.
**B4. `JsRenderStyle` walker unification — −120, med-high.** Merge Family C (signed multiline walker `:1152-1309`) with inspect's object/array/property walkers behind `JsRenderStyle{sign, indent_step, multiline, getter_labels, string_limit, diff_mode, seen}` + a `{JsClass, render_fn}` table replacing the if-chain at `js_util.cpp:867-901`. The sign-column indent quirk (`:1152-1163`) and excerpt caps (488/9488/508/512) are byte-exact contract.
**B5. Scaffolding — −90+.** 18 header stanzas + 46 `assert_make_string_n`+`strbuf_free` epilogues → two helpers; the 9 identical dispatch stanzas in `js_assert_deepStrictEqual:2691-2758` → table; fold the third mini-renderer (`throw_object_pattern_mismatch:3161-3282`) onto B4.

Families D (diff engine) and E (per-type first-mismatch summarizers) stay — they are two-value algorithms. Note `js_assert.cpp:5167-5859` is an unrelated node:test/mock module — split candidate for §6.

### C. Regex consolidation — MEASURED: the flip is a NO-GO. Net ≈ −150 LOC, plus 7 confirmed spec bugs worth fixing on their own merits (was: −1,925)

Current layout: RE2 wrapper + emulation (`js_regex_wrapper.cpp` 2,812), spec backtracker (`js_bt_regex.cpp` 1,046), frontend (`js_regexp_compile.cpp` 497), runtime block (`js_runtime.cpp` ~14.4K-19.5K region), routing at `js_regex_needs_backtrack` (`js_runtime.cpp:16251-16333`).

- **Stage 0 (bug fixes, +60..100):** split preprocessing — engine-neutral normalization (`\u`/`\c`/octal→`\x{}`, `[^]`, dot-class, `\p`→ranges) **before** routing; RE2-only rewrites (`\s`→`\p{Z}…`, `(?P<`, `(?m)` prefix) after. Fixes §4.1. Add bt gtest cases + failure counters.
- **Stage 1 (−250, ~~near-zero risk~~ BLOCKED — audit claim disproved):** the audit called this lane dead because backrefs route to bt before `js_regex_needs_wrapper` is consulted. **That is wrong.** `bt` is a *pointer* (`JsBtRegex* bt`); when `js_bt_compile` fails it is null, and both downstream guards are spelled `if (!bt ...)` — so a backref pattern whose bt compile fails falls straight through to the wrapper path, where `js_regex_needs_wrapper` returns true and the backref emulation runs. The lane is a **live fallback**, not dead code. Deleting it needs the same evidence as the routing flip: a test262 sweep proving `js_bt_compile` never returns NULL for a valid routed pattern (gate 5 below). Until that sweep exists, do not delete.
- **Stage 2 (bt gap closure, +205..325):** `\p{gc}/\p{Script}` property-class node backed by utf8proc + existing `JsRegexRange` tables; full simple case folding in `bt_canon` via utf8proc (honoring the non-Unicode /i U+017F/U+212A exception); then flip lookahead/easy-lookbehind routing **behind the bench gate**.
- **Stage 3 (−1,433):** delete assertion surgery (`:1307-1788`), scanner helpers (~269 of `:30-334`), `AssertionInfo` (`:335-443`), marker/filter runtime (`:2087-2240`), wrapper compile/exec (`:2242-2799`), header structs; **keep** the `/v` rewriter (`:495-1305`) and the /u//v validators (`:1790-2085`). Runtime side: wrapper predicates/branches/group-remap/strip-fallback (−260).
- **Stage 4 (−395):** shared `js_regex_scan.h` (UTF-8 fwd/bwd decode, hex/u escape, class-state tracker, capture-group walker — currently 4-6 copies each across engines/frontend/runtime) −170; kind-indexed `JsRegexRange` table + one alias table (currently triplicated) −110; named-groups object builder ×3 → one iterator −85; flag-parse ×4 −30.
- **Follow-up (LOC-neutral):** port the kept /v rewriter + validators off `std::` (56 sites remain after deletion; satisfies rule 3 where deletion alone cannot).

**MEASUREMENT (2026-08-18) — the no-go trigger fires.** The plan gated the
routing flip on "flipped lookaround categories ≤2× today's wrapper timings".
`test/js_runtime_bench/bench_regexp.js` now exists (it did not before) and the
gate fails decisively.

Isolating *engine* cost with identical matching semantics — the same pattern
prefixed with `()\1`, which always matches empty but forces backtracker
routing — on a 20K-char scan-to-miss, release build:

| Pattern | RE2 | backtracker | ratio |
|---|---:|---:|---:|
| literal scan | 92 µs | 372 µs | **4.0×** |
| class+quant scan | 35 µs | 825 µs | **23.6×** |

Confirmed by proxy pairs too: capture-free lookahead 70.5 µs (wrapper) vs 217 µs
(bt, 3.1×); fixed lookbehind 96.5 µs (wrapper) vs 623 µs (variable-length bt,
6.5×). RE2's linear-time engine and literal prefilters are exactly why the
emulation layer exists. **Stage 2 (flip) and Stage 3 (−1,433 emulation
deletion + −260 runtime plumbing) cannot proceed as designed.** The plan's
fallback ("retain only the ~60-LOC trim-group lane … still nets ≈ −1,700") does
not survive either: the wrapper's assertion surgery, marker/filter runtime and
two-pass retry are all *load-bearing* for patterns that must stay on RE2.

**What survives:**
- **Stage 0 — DONE (2026-08-18), for correctness not LOC.** Fixed in the backtracker itself rather than by restructuring the shared preprocessing loop: `js_bt_regex.cpp` now parses `\uHHHH` / `\u{...}` (with surrogate-pair combining gated on `/u`, since without it the pattern is a sequence of UTF-16 code units), `\cX`, and `\p{...}` / `\P{...}` via the same generated tables the RE2 side uses — in atom position and inside character classes, including as a range endpoint. `bt_canon` now implements the real ES §22.2.2.9 rule (uppercase, but leave a non-ASCII character alone when its uppercase is ASCII), which keeps U+017F and U+212A from folding — the property the old ASCII-only code satisfied only by never folding anything. All seven bugs verified fixed; locked by `test/js/regexp_backtracker_escapes.{js,txt}`. `test_js_bt_regex_gtest` needed the generated-properties TU added to its unity include to link. Gates: bt gtest 50/50, test262 40261/40261 zero regressions, MIR goldens 21/21. Cost: **+~90 lines** in the backtracker.
- ~~Stage 0 — DO IT (+60…100 lines)~~ Seven spec bugs
  verified live by execution on this tree, all on patterns that *already* route
  to the backtracker, because `bt_pattern` is captured before escape
  normalization: `\uHHHH`, `\u{...}`, `\p{L}` (top-level and with backref),
  `\cX`, and non-ASCII `/i` folding all silently return the wrong answer.
  Splitting preprocessing into engine-neutral normalization (before routing) vs
  RE2-only rewrites (after) fixes all seven and is independent of any flip.
- **Eighth regex bug — FIXED (2026-08-18).** Bare `\p{...}` threw
  `SyntaxError: invalid character class range` for 13 of the 20 most common
  binary properties (White_Space, Alphabetic, Math, Uppercase, Lowercase, Dash,
  Quotation_Mark, Radical, Bidi_Control, Join_Control, Emoji_Modifier, the two
  IDS operators, Pattern_White_Space), while the identical `[\p{...}]` spelling
  matched correctly. Root cause: RE2 does not know all binary-property names, so
  the `/v` class rewriter flattens them to explicit ranges — but only **inside**
  a class, leaving the bare form to reach RE2 unflattened. Fixed by wrapping
  bare property escapes in a one-element class *before* the rewriter runs, so
  both spellings take one path; an unrecognised name still raises SyntaxError.
  Covered by extending `test/js/regex_unicode_binary_property_class.{js,txt}`.

  This also corrected `regex_unicode_props` t7: `^\p{XID_Start}\p{XID_Continue}*$`
  no longer matches `"_test"`. U+005F is XID_Continue but **not** XID_Start — JS
  permits a leading underscore through a separate `IdentifierStart` production,
  not by `_` being ID_Start. The golden had recorded the unflattened-RE2
  behaviour; the in-class spelling always reported false, and the fix does not
  touch the in-class path, so this is the bare form being brought into line.
- **Stage 1 — BLOCKED** (see above: the backref lane is a live fallback, not
  dead code).
- **Stage 4 — IMPLEMENTED AND REVERTED for the measured item; ≈ −17, not −150.**
  The `JsRegexRange` consolidation was built: all 18 table-shaped branches in
  `js_regex_special_property_contains` were hoisted to file scope and driven
  from a kind-indexed table. It measured **−17 lines**, not the −90 estimated
  (hoisting preserves every data line; only the `if`/`return`/`}` scaffolding
  goes, and the table rows and header give most of it back). It also replaces an
  early-exiting if-chain with a linear 18-entry scan on a per-codepoint matching
  path. Reverted: the line saving does not justify a possible regression on
  property-heavy patterns. The remaining Stage 4 items (UTF-8 decoder sharing,
  named-groups builder, flag-parse) are unmeasured, and on this evidence should
  be measured before any are attempted.
- ~~Stage 4 — ≈ −150, flip-independent~~ Confirmed
  present: 48 `static const JsRegexRange` branches in `js_runtime.cpp` whose
  ~3-line dispatch each can collapse to a kind-indexed table (the range arrays
  themselves are data and stay, so ≈ −90, not −110); and five UTF-8 decoders
  (`bt_utf8_decode`, `bt_utf8_decode_prev`, `v_utf8_decode`,
  `js_regex_decode_utf8_permissive`, `js_regex_utf8_sequence_len`) whose
  contracts genuinely differ (forward/backward/permissive/length-only/
  `std::string`-based), so ≈ −60, not −170. Named-groups builder and flag-parse
  consolidation unmeasured.

**Net verdict: C is worth ≈ −150 LOC and seven bug fixes, not −1,925.** The
`std::` removal goal for `js_regex_wrapper.cpp` (114 sites) cannot be achieved
by deletion and needs a straight port instead.

**Gates (go/no-go):** test262 full baseline no regressions (40,261 passing; RegExp 1,703, lookBehind 17/17, property-escapes 611/613, unicodeSets ~150); extended `test_js_bt_regex_gtest` green; zero bt budget-exhaust/group-cap events across test262; **new `test/js_runtime_bench/bench_regexp.js`** (none exists today — must be created; release build per rule 10): plain lane ±2%, flipped lookaround ≤2× wrapper timings; a sweep proving `js_bt_compile` never fails a valid routed pattern before the strip-fallback becomes SyntaxError. **No-go fallback:** keep only a ~60-LOC trim-group lane for capture-free trailing lookaheads — still nets ≈ −1,700.

### D. Node modules — net ≈ −1,260

**D1. Emitter completion (C3.3) — −250.** The decision taken in round 2 stands: **no new emitter** — modules move onto the existing full implementation in `lambda/module/node_core/node_events.cpp` (`js_ee_on/once/off/emit/listeners/removeAllListeners/...`, `extern "C"`, `__events__` → array of `{listener, once}` records, ALS context captured per listener, snapshot-then-sweep emit). `node_events_state()` now attaches the module on demand — a landed prerequisite; without it a migrated module silently *drops* listeners rather than crashing.

**Per-module checklist — round 2 found these three the hard way; re-check each deliberately, they are not caught by the type system:**
1. `js_ee_listenerCount` returns a **double-backed JS number**, so testing it for `LMD_TYPE_INT` is always false — measure `js_ee_listeners` length instead.
2. Watch for **dual-shape dispatch**: a module may dispatch both an emitter snapshot *and* a bare constructor-supplied handler (http's `createServer()` callback). Splitting a `dispatch_one` out preserves the constructor-handler path.
3. `js_ee_emit` implements Node's **`'error'` rule — no listener means throw** (raw error if Error-like, else `ERR_UNHANDLED_ERROR`), while the per-module emitters silently do nothing. Every migration must decide this explicitly and test it; stream needs it routed differently (below).

Precondition: promote the `js_ee_*` adapter kit to `js_node_common.hpp` (extern block, `js_ee_emit_cargs`, `js_ee_has_listener` built on `js_ee_listeners`) −25 — three modules have already hand-cloned these decls plus the same two adapters, so the next migration would clone them a fourth time; add opt-in `captureRejections` to `node_events.cpp` (+35). Then per module: `js_https.cpp` −20 (its `__on_connect__`/`__on_request__` readers are dead); `js_readline.cpp` −15 (single-slot `__on_<event>__`, last-wins; keep the question-callback line-consumption priority and the `input.emit` override; add a multi-listener test — coverage is 2 tests); `js_fs.cpp` −30 (WriteStream 5 single-slot callbacks + replay hooks; ReadStream rides stream); `js_child_process.cpp` −45 (dual-shape emitter; the IPC/cluster replay queues are **registration hooks** that survive in the on-wrapper; emit keeps its microtask flush; unhandled child `'error'` will now throw — explicit decision + test); `js_stream.cpp` −170..200 **last** (replace registration/query/dispatch ~334 lines; survivors: the four on-hooks (readable/data/finish/drain replay), off-hooks, and an emit wrapper that routes no-listener `'error'` to autoDestroy+uncaughtException instead of js_ee's throw; live-array vs snapshot emit ordering and int→double `listenerCount` are the observable risks; collapse the `_events` pipe-noop shadow store into real registrations of `js_stream_pipe_data_noop`).
**D2. Transport re-scope (C5.2v2) — −130.** Shared drain-accounting pair (hwm dual-check + maybe-emit-drain) for net (`socket_maybe_emit_drain:790` etc.) and tls (scheduled drain-check); adopt `js_node_stream_write_owned` in child stdin/IPC (raw `uv_write` today, hard-coded 16KB backpressure) and tls ciphertext flush. http and child stay out of any queue abstraction (framing-dominated / no drain contract).
**D3. Stream internals — −300.** (i) Seven hand-rolled async promise-pump families (collect/map-filter/forEach/reduce/compose_result/readable_from/iter_pipe = 754 lines, 23 `_next/_step/_pump` functions, 19 `js_promise_then` sites) → one `js_stream_async_pump(iter, on_value, on_done, on_fail, env)` kernel, family-by-family (−150..200, med-high — completion/abort ordering per family); (ii) constructor init triplication `readable_new_internal:6126`/`writable_new:6540`/`init_duplex_like:6580` → `init_readable_side`/`init_writable_side` (−30, fixes the `key_reading_sync` drift); (iii) transform write-path stanza ×2 (−22); (iv) call-or-emit dual-dispatch helper ×8 sites (−25, do with D1); (v) compose bridge families (−50, lower confidence). Positive: `pipeline` already builds on `pipe()` — no duplicated forwarding.
**D4. AbortSignal unification — −185.** The five-part signal protocol (probe/abort-error/reason/bind/unbind/fire) implemented **7×**: `js_http.cpp:3892-3992`, `js_net.cpp:935-1030` + `:4350-4384` (second copy same file), `js_child_process.cpp:377-448` + `:1224-1279`, `js_fs.cpp:2864-2898`, `js_readline.cpp:275-384`; http↔net near-verbatim (10-line-window clone matches). Divergences are drift, not intent (§4.3). New `js_node_abort.{hpp,cpp}` beside `js_node_uv.*`: `js_node_is_abort_signal/signal_aborted/abort_error/abort_reason/abort_bind/abort_unbind` + per-module ~12-line fire callbacks. 36 baseline abort tests gate. Land net+http → cp → fs/readline.
**D5. Server-handle family — −150.** http/net/tls triplicate from_object/address/ref-unref/getConnections/listening-tick/close/listen-normalize (table in audit; tls↔net clone-verified). Extend `js_network_service.h` (home established): `JsNodeListenTarget` + `js_node_listen_args()` absorbing the argument-shape matrix once; first commit: point `js_http_server_address` at `js_node_tcp_server_address` (fixes IPv6, §4.3). The sockaddr→object switch exists in 4 spellings — one survives. 151 baseline listen/address tests gate.
**D6. Option/predicate/int64 helpers — −120.** 142 option-read stanzas (40 with type-guard, 26 nullish), 25 clamp/memcpy/NUL copies that `js_item_to_cstr` already implements, 10 per-module string-predicate clones, and two verbatim int64-coercion clones (`http_item_to_integral_int64:268` = `js_item_to_integral_int64`; `https_item_to_int64` a weaker third) → `js_node_opt*`/`js_node_str_eq`/`js_node_str_starts_with` in `js_node_common.hpp`; sites needing bespoke `ERR_INVALID_ARG_TYPE` text keep explicit guards (that is the discount).
**D7. https/http URL + agent-key — −85.** Promote `https_parse_url_string` (`js_https.cpp:375-468`, the correct one: userinfo/IPv6/query) to scheme-parametric `js_http_url_to_options()`; http's 27-line inline parser (with dead `plen`) deletes and **gains** those features; factor the shared agent-key prefix out of both `getName`s.
**D8. readline splice — −40.** Four hand-rolled `char buf[8192]` line-splice bodies (append/delete-word-fwd/delete-word-back/delete-range; backspace/delete-right already delegate) → one StrBuf `readline_splice()` (also removes the silent 8 KiB truncation); extract the twin CR/LF flush branches (`:1412-1437`).

### E. Runtime-core batched residue — net ≈ −230

No single item justifies its own change; batch opportunistically when touching the area (list with line ranges in the audit): proxy-trap prologue motif ×13 (~55), `js_get_global_this` host-ctor stanzas (~45), TypedArray name/enum mapping switches ×4 → X-macro (~35), delete-family identity-key motif ×3 (~25), set-op record-keys lambdas ×4 (~25), string-search prologue ×3 (~20), gen return/throw pair (~20), UTF-8 encode/decode re-inlined vs `lib/utf.h` ×4 (~30, rule-13 hygiene), radix digit-loop ×3 (~15). Snapshot-struct unification is NOT worth doing as dedup — instead relocate the whole ~670-line test262 snapshot block with §6.

### F. Encodings, formatters, kits, verdict replacements — net ≈ −800

**F1. node_core Jube boilerplate kit — −250.** `*_set_method` cloned 13–14× (`node_perf_hooks:44`, `node_path:943`, `node_string_decoder:112`, `node_querystring:893`, `node_v8:49`, `node_os:799`, `node_events:1060`, `node_url:1331` **and** `:1447`, `node_punycode:68`, `node_timers:89`, `node_tty:70`, `node_workers:44`; diff-verified identical modulo host-pointer spelling), plus `node_*_string` ×6, `*_root_value` ×9, `*_roots_begin` ×5, throw helpers ×8, two private `item_to_cstr`. One `node_core_common.hpp` (~60 lines); modules keep only their `*_state()` accessor.
**F2. "Received type" formatter — −140.** Canonical trio (`js_format_invalid_arg_received` `js_runtime.cpp:22998` + siblings, **static**, no header decl — the rule-13 root cause) re-cloned in buffer (`:215`, 13 callers), crypto (`:530`), tls (`:68`), dns (`:42-110`, near-verbatim port). Promote to `js_runtime.h`, delete 4 clones; clones drift in escaping/truncation, and messages are golden-locked — diff message output on the corpus first.
**F3. `js_encoding.{h,cpp}` + UTF-8 codec — −170.** Five copies of the codepoint decoder whose canonical already sits in `js_runtime.h:44` (`crypto:1194`, `buffer:175` + `:922` + inline `:1491`, `js_runtime_value:1650`); bytes→string arms duplicated crypto↔buffer (~115 vs ~78) and string→bytes (~70 vs ~47 + inline). Normalizer contracts stay per-module (they genuinely differ); kernels merge. Resolve the latin1 divergence (§4.3) deliberately first.
**F4. TA intra-lane cleanup (C5.3 replacement) — −80.** `js_typed_array_relative_arg()` for the ~8–9× coercion stanza + an OOB-validate helper for the ~12× 3-liner inside `js_indexed_intrinsic_algorithm` (`js_runtime.cpp:20009-20715`).
**F5. Crypto leftovers — −90.** Derive `resolve_cipher_type`/`is_known_cipher_name`/iv-key validators/`getCiphers` from an extended `crypto_cipher_infos[]` (fact base currently written 6×, drift hazard); `crypto_sign_verify_finalize_throw()` for the 8× finalize stanza + shared sign/verify prepare phase; context-kind table for the reset/destroy 4-arm loops.
**F6. TS builder merge (C5.4v2) — −130.** variable_decl + function first (~150 nominal; the TS function builder's first ~55 lines are a verbatim prologue copy), class_decl maybe; skip class_body and all 12 type builders. Gate per pair on the `test/ts` corpus + `test_ts_gtest`; abandon cheaply.
**F7. `js_source_scan` — −75.** Exactly two copies of the string/comment/regex state machine in `js_scope.cpp` (`:452-548` read-only vs `:750-1068` mutating, extra `ST_REGEX_CLASS`); shared scanner with mutating callback; caveat: scanner 1 inherits the better regex disambiguation (likely-fix behavior change) — gate on test262 u180e/template sections.
**F8. js/-side namespace installers — −50.** `assert_set_method:4905`, `crypto_set_method:7680`, `js_fs_set_method:363`, three in `js_stream.cpp` (`:218`, `:6109`, `:8673`) → one `js_ns_set_method()` in `js_runtime.h`.

### G. MIR + DOM structure — net ≈ −1,070

**G1. AST-walker migration (C3.1 remainder) — −600 plan-credible.** Full classification done (audit table). Migrate the top-6 first: `jm_collect_functions` (660 LOC, COMPLETE, ~33 generic case blocks whose visit order was verified to match the child table — −170), `jm_collect_func_assignments` (−100), `jm_callsite_scan_node` (−65), `jm_mutable_native_var_needs_boxing_walk` (−60), `jm_infer_walk` (−55), `jm_collect_body_locals` (−50); then the sub-45 tier or the alternative packaging: one `jm_for_each_statement(root, flags, cb, ctx)` replacing the 10-member statement-recursion family (do NOT double-count). PARTIAL walkers (allow-lists: tail-call, pattern shapes, float hints, P9 widen) stay hand-written.

**Correctness rule (from round-2 execution, non-negotiable):** a migrated walker must visit the same nodes in the same order. Where a hand-written walker deliberately *skips* a child — not descending into nested functions, for instance — that skip becomes an **explicit case**, never an accident of the table. Gap closures (§4.4) are separate commits with their own golden re-baseline.

**Delegate explicitly, never through `default:` (learned in execution).** The
obvious migration — keep the interesting cases and let `default:` call
`js_ast_visit_children` — is **wrong** for every walker measured so far. These
walkers use `default: break`, so a node kind with *no case* is a node kind they
never descend into; routing `default:` to the visitor silently starts
descending into all of them. Measured gaps: `jm_collect_functions` would newly
descend into 5 kinds (CLASS_EXPRESSION, FIELD/METHOD_DEFINITION, REST_PROPERTY,
STATIC_BLOCK), `jm_collect_func_assignments` into **18** (including PROGRAM,
class kinds, patterns, yield/await), `jm_callsite_scan_node` into **31**. The
correct shape lists the delegate-able kinds as bare `case` labels falling into
one `js_ast_visit_children` call and leaves `default: break` untouched. That
costs one line per kind and is provably behaviour-neutral. It also lowers the
yield: the three walkers above came in at −164/−96/−61 rather than the
−201/−119/−80 the fold-into-default shape produced.

**Verify order mechanically, not by eye.** Comparing each walker's recursion
sequence against the child table by script caught deliberate partial visits
that reading would have missed — `catch` body-but-not-param, `for-in/of`
left-and-body-but-not-right, declarator `init`-but-not-`id`, property
`value`-but-not-`key`. One apparent mismatch was a false positive:
`AstArrayNode` unions `item`/`elements`/`expressions`, so the table's
`elements` and the walkers' `expressions` are the same field.

**Classify before migrating.** Round 2's original −1,500 assumed these walkers are complete traversals whose `default:` can simply delegate; many are not. `jm_node_has_direct_eval_call`, for example, returns false for for/while/try/switch/object/array and every other unlisted kind, so delegating would newly descend into them — and under the correctness rule those kinds must become explicit skip cases, costing back most of what the table saves. The cheap signal: **does the walker's `default:` recurse generically, or return/break?** Complete walkers give ~75% reduction; deliberately partial ones give roughly nothing and should keep their allow-list shape.
**G2. `JsMirCompileUnit` (C3.2 remainder) — −135.** The five pipelines still duplicate: parse→build→early-errors ×4 (~90), jit-init→transpiler→module→lower→validate ×5 (~125, divergences are pure options: opt level, dims, is_module, prelink, preamble seeding), link→find `js_main` ×5 (~50), activate/link-names ×4–5 (~60), success cleanup (generalize `jm_finish_module_transpile:5488`). Unit struct = the failure primitive's field set + options; each pipeline keeps its unique middle inline. Spine emits no MIR → goldens must be byte-identical.
**G3. Class-expression lowering — −140.** The ~283-line CLASS case in `jm_transpile_expression` (`js_mir_expression_lowering.cpp:8267-8551`) hand-rolls what statement/module get from `jm_emit_class_setup`/`jm_emit_class_static_initializers` (mirrors verified line-for-line, e.g. `:8469-8548` ≈ statement `:2051-2084`). Extend `JsMirClassSetup` with a policy {private_home, gen_spills, name_conflict_check, inner_modvar}; freeze per-path behavior first (the `_simple` static-field emitter difference is deliberate), close the statement-side generator-spill gap separately.
**G4. Binary-op native lane table — −70.** `jm_transpile_binary` `:2268-2496`: ADD/SUB/MUL identical 6-line stanzas; DIV's both_int branch is byte-identical to its else (dead split); 6 comparison stanzas differ only in (D-op, I-op, name); shifts share a masking prelude. Static `{js_op, mir_d_op, mir_i_op, reg_name}` table — keeping reg-name strings as data preserves MIR dump bytes, so emission goldens stay stable. The boxed lane is already a table.
**G5. fetch → shared HTTP client — −70.** `js_fetch.cpp` hand-rolls libcurl (`curl_easy_*` in `fetch_work_cb:169-219`) beside the `FetchConfig`/`http_fetch` client XHR already uses (`js_xhr_send` ~`:643`). Rebuild on `http_fetch` inside the existing uv_queue_work harness; verify `http_fetch` exposes headers/status for `Response` first (unverified), preserve timeout/redirect deltas.
**G6. Native-event builders — −55.** Nine `js_create_native_*_event` builders share a 6–7-line prologue/epilogue; mouse↔pointer duplicate the 8-line geometry stamp verbatim (`js_dom_events.cpp:1837-1844` ≡ `:1866-1873`); drag already delegates to mouse — extend the proven pattern (`native_event_init/finish` + `stamp_mouse_geometry`).

### H. Round-2 orphaned sub-items — net ≈ −263

Round 2's WS2/WS4 "smaller items" bullet and the C6.3 dedupe were never given execution numbers, so they were re-verified individually against the current tree. **Four turned out to have been absorbed by work that landed in C1–C3** (`jm_emit_super_call`, `jm_emit_optional_method_call`, `jm_scope_env_mark_and_writeback`, `socket_update_state_properties` all exist and carry the duplication the round-2 audit described), and two were false positives. Realized fraction of the round-2 estimate: ~30%. What genuinely remains, ranked:

- **H1. test262 intrinsic-intercept descriptor table — −65, low.** The `#if JS_TEST262_FAST_PATHS` block (`js_mir_expression_lowering.cpp:5543-5686`) has a bare-identifier half with 7 hand-written blocks (`verifyProperty`, `compareArray`, `assert`, `$DONOTEVALUATE`, `isConstructor`, `buildString`, …) → `{name, len, min_args, argc, native_fn, flags}` descriptor + one generic emitter (box up to 4 args, pad with undefined); only `assert`'s local-binding guard stays bespoke. The `assert.*` member half is already half-tabled. Includes deleting the dead block in §4.4.
- **H2. `super()` construct kernel — −49, medium.** The constructor block (`:5217-5392`, plus the builtin-parent arm at `:5526`) has 5 arms each hand-writing scan-spread → build-args → apply-or-call → `jm_emit_super_bind_this_with_public_fields`. One `jm_emit_super_construct(mt, call, parent_reg, arg_count, use_class_helper)` (−35) plus a 3-line `jm_args_have_spread(args)` for the verbatim 2-line spread loop at 8 sites (`:5234`, `:5263`, `:5338`, `:5528`, `:5999`, `:6208`, `:6392`, `js_mir_statement_lowering.cpp:2278`) (−14). Note `jm_call_arg_flags` **cannot** absorb these — it also runs a `jm_expression_can_suspend` pass these sites don't want. The arms call three different runtime helpers (`js_super_call_class_into` / `js_super_apply_native` / `js_super_call_native`); goldens gate it.
- **H3. `JS_FS_ASYNC_*` extension — −35, low→medium.** 23 hand-written callback-style async fns remain (358 LOC) but only ~35% folds. Tier 1 (risk-free, bank first): `js_fs_chmod_async:2732`, `js_fs_rename_async:2786`, `js_fs_utimes_async:3227` are drop-in `JS_FS_ASYNC_SUCCESS_2/_3` instantiations (−23; the macros are `#undef`'d at `:3156`/`:3167`, so the three must move into that block). Tier 2: `symlink`/`copyFile`/`appendFile`/`truncate` behind 2 new macros (−16, discounted to ~12). Tier 3 (8 fns, 234 L) is **not** macro-able — `readFile`/`writeFile` do real threadpool dispatch; `mkdir`/`access`/`stat_like`/`unlink`/`rmdir` each hold a distinct permission gate + direct `uv_fs_*`. Exclude `js_fs_statfs_async:2690` (it alone dispatches with `this = make_js_undefined()` where everything else uses `ItemNull`, and throws on a non-callable callback).
- **H4. `JsMirFunctionStateSnapshot` widening — −30, MEDIUM-HIGH.** The struct exists (`js_mir_function_class_lowering.cpp:1327-1344`, 15 fields) with only 2 users, while ~83 lines of state are still hand-saved/restored at 5 sites. **Hazard:** each site saves a *different subset* — one wide snapshot would restore `in_generator`/`in_async`/`arguments_*` at the `:1454` and `:2097` sites, which don't restore them today. That is a codegen change, not a refactor. Safest shape: **two tiers** (core + error-lane quartet), never one wide struct. Hard-gate on emission goldens.
- **H5. `JM_BINOPS(X)` op-class predicates — −25, low.** A shared `jm_binop_result_type()` is **not** viable: the three result-type sites have genuinely different contracts (`jm_get_effective_type` at `js_mir_calls_boxing_types.cpp:1087` returns ANY unless both operands are proven numeric; the two inference-side arms return FLOAT unconditionally). What *is* shareable is the op-class lists — the comparison set is spelled out 3× — as `jm_op_is_cmp/arith/bitwise` predicates, plus the FLOAT-arith collapse. Fold in the near-identical MIR-opcode switches at `js_mir_expression_lowering.cpp:4336-4348` and `:4090-4099` (9 of 11 rows identical, ~−15).
- **H6. Scope-env store + arguments writeback residue — −22, low.** ~10 open-coded `jm_emit_store_i64(..., scope_env_slot * sizeof(uint64_t), ...)` stanzas and 2 verbatim 5-line arguments-writeback stanzas (`:4362-4367`, `:4393-4398`) remain outside the existing `jm_scope_env_mark_and_writeback` / `jm_emit_assignment_var_writeback` helpers (~40 call sites already).
- **H7. `parse_points_to_path` promotion — −21, low.** See §6; the only genuinely substitutable pair of the three.
- **H8. `jm_emit_nullish_skip` residual — −9** (3 sites × 4 lines; the two computed-member sites at `:5736`/`:5767` cannot use `jm_emit_optional_method_call` because the receiver guard must be emitted *before* the key is evaluated, or skipped side effects leak). **H9. `jm_unwrap_export_decl` — −7** (the export-unwrap idiom appears exactly 3× tree-wide).

**Closed as stale/absorbed — do not re-open:** INT/FLOAT compound-assign merge (the paths use different codegen strategies — native in-register MIR for INT vs box/call/unbox for FLOAT; merging means either a new FLOAT fast path or an INT perf regression, and the only shared code is H6's lines — do not double-count); the four "Third pass a/b/c/d" module-var hoist loops (**not** near-identical: pass (d) shares nothing; only H9's 5-line preamble is common); socket-facade property init ×4 (one builder per TU with 4 *call sites* — the round-2 audit counted call sites as copies; state mutation is already extracted into `socket_update_io_counters`/`socket_update_state_properties`/`net_set_endpoint_properties`, ~35 call sites).

---

## 6. Relocations (LOC-neutral, structure/size wins — after churn settles)

All splits move-only (no edits inside moved code, so blame survives), one commit each, TUs added to `build_lambda_config.json` (never the generated Lua).

- **`js_runtime.cpp`** → extract the self-contained trailing Node-module block (vm, diagnostics_channel, async_hooks, domain, cluster, repl, AsyncLocalStorage, trace_events, bindings, compile cache) into `js_node_modules.cpp`, and the regex block into `js_regex_runtime.cpp` (much smaller after §5.C).
- **`js_dom.cpp` SVG → `js_dom_svg.cpp`.** Re-measured: the block is **`:10742-12484` = 1,743 lines, ~87 functions** (85 `js_dom_svg_*` + 2 constructors, 5 type defs) with clean boundaries. (Round 2's "78 fns / `:10460-12226`" is stale.) Extraction needs a header covering 21 external references (`:164-168`, `:234-235`, `:1756`, `:8864`, `:8871`, `:12690`, `:12761`, `:14527-14545`); note `js_dom_svg_interface_name:1727` and `js_dom_element_is_svg:1711` sit *outside* the block.
- **SVG geometry sharing — much smaller than round 2 claimed (−21, not −300), and no new header is needed.** `radiant/render.hpp:228-233` already carries an SVG-geometry export section (`svg_parse_path_d`, `svg_parse_transform`, `svg_get_inline_style_property`), two of which `js_dom.cpp` already consumes — so promote into *that* section; do **not** create `radiant/svg_geometry.h`. Of the three round-2 candidates, only one is substitutable: `parse_points_to_path` (`radiant/render_svg_inline.cpp:1830-1866`) ↔ `js_dom_svg_path_add_points` (`js_dom.cpp:11811-11832`) are identical in type and differ only in argument order (**−21**). `parse_svg_viewbox` (`:604-635`) ↔ `js_dom_svg_parse_viewbox` (`:11282-11300`) have different output contracts *and* different validation (js_dom rejects `width/height <= 0`, radiant does not) — ~−8 with a behavior-change risk; skip unless that validation delta is resolved deliberately. `build_path_from_svg_shape` (`:4021-4075`) ↔ `js_dom_svg_basic_shape_path` (`:11843-11899`) are **not substitutable**: disjoint input types (`Element*` Mark node vs `DomElement*`), and radiant emits backend primitives (`rdt_path_add_rect`/`add_circle`) that js_dom's own hit-test visitor **explicitly rejects** (`js_dom.cpp:11970-11974`), which is why js_dom flattens to move/line/cubic. Net 0 — leave it.
- **Cheaper adjacent win the round-2 audit missed:** `js_dom_svg_bounds_from_points` (`js_dom.cpp:11158-11172`) is a *third* in-file copy of the same number-pair scanner. Deduping the two js_dom copies is ~−15 with no cross-module coupling — do this before, or instead of, the radiant promotion.
- **`js_globals.cpp`** → test262 fast-path asserts **plus the ~670-line snapshot machinery** (`:15500-16160`) into `js_test262_natives.cpp`; default `JS_TEST262_FAST_PATHS` to **0 for release** in `build_lambda_config.json` while the test262 gtest build keeps it 1 (today the harness accelerators ship in the production binary). Verify the release binary shrinks and `test262-baseline` still passes.
- **`js_assert.cpp`** → move the unrelated node:test runner + mock-timers module (`:5167-5859`, ~690 lines) out.
- **Remaining `std::` islands** in `js_runtime.cpp` (hex-property builder → `StrBuf`; fold-expansion map → `lib/hashmap`), smaller after §5.C.

---

## 7. Phased execution plan

Commit discipline: one task (or one file-batch of a mechanical task) per commit, message prefixed `js-r3 <phase>.<item>:`; phases land in order; each R3.6 stage gets an explicit go/no-go against its prerequisite.

| Gate | When |
|---|---|
| `make build` clean, zero new warnings | every commit |
| `make test-lambda-baseline` 100% | every commit |
| `./test/test_js_gtest.exe` | every commit |
| `./test/test_js_mir_emission_gtest.exe` **byte-stable goldens** | every commit touching `js_mir_*` (R3.5, and G4's table must preserve reg-name strings since they appear in MIR dumps) |
| `make test262-baseline` | every batch in R3.2–R3.5; every commit in R3.4 and R3.6 |
| `test/node` set (`./lambda.exe js test/node/X.js --no-log \| diff -u test/node/X.txt -`) | every commit touching Node modules (R3.2, R3.7) |
| `make node-baseline` (official Node suite, `ref/node/test/parallel/`) | every commit in R3.3; it is the real message-text lock |
| `utils/check_js_node_test_separation.py` | whenever tests are added |
| ASAN debug build over the http/net/tls node tests | R3.2 (D1 stream, D2 transport) |
| `test/js/props` invariant harness, before and after, zero delta | R3.4 A3 (descriptor pipeline) |
| `test_js_bt_regex_gtest` (extended per §5.C) + test262 RegExp sections | R3.6, every stage |
| `test/ts` corpus + `test_ts_gtest` | R3.7 F6 (per builder pair) |
| Release-build benchmark, `test/js_runtime_bench` (rule 10) | before/after A1(g), A3, and every R3.6 stage; `bench_regexp.js` must be created first (§4.5) |
| `JS_EXEC_PROFILE=1` named-fast hit-rate delta = 0 | every R3.4 commit |
| `grep` proving zero remaining references to each deleted static/alias | every deletion commit |
| `make test` + `test262-full` | end of every phase, before merge |

| Phase | Content | Net (disc.) | Risk |
|---|---|---:|---|
| Phase | Content | Net (disc.) | Risk |
|---|---|---:|---|
| **R3.0** | Bug preflight, tests first: regex Stage 0 normalization (§4.1), byte/UTF-16 presence fix, cp `is_callable`, tls error codes, http IPv6 `address()`, stream drift fixes, the `super()` spread-scan arm; triage the 3 failing node-baseline tests; create `bench_regexp.js`; bt failure counters | **~+50** (adds tests; the dead-code deletions it enables are banked in D1/H1) | behavioral, each test-locked |
| **R3.1** | Mechanical kits: F1 → F2 → F7 → F8 → D6 → D8 → G6 → G4 → E residue (opportunistic) | ~−1,030 | low |
| **R3.2** | Node consolidation: D4 → D5 → D7 → D1 (https→readline→fs→cp→stream) → D2 → D3(ii–iv); **+ H3** (fs async Tier 1 first) | ~−960 | low→med, stream last |
| **R3.3** | assert/util: B1 → B2 → B3 → B4 → B5 (after R3.0 baseline triage) | ~−980 | med, golden-gated |
| **R3.4** | Property protocol: A1(a–d) → A1(e,f,h,i) → A4 → A2 → A1(g) → A3 last | ~−1,430 | med→high, test262-gated |
| **R3.5** | MIR/DOM structure: **H1, H5, H6, H8, H9 first (mechanical)** → G1 top-6 → G2 → G3 → G5 → D3(i) stream pumps → **H2, H4 last** | ~−1,330 | med, emission-golden-gated |
| **R3.6** | Regex: C stages 1→2→3→4 with the §5.C go/no-go gates | ~−1,925 | med-high, benched |
| **R3.7** | F3 (after latin1 decision) → F5 → F4 → F6 | ~−470 | low-med |
| **R3.8** | Relocations (§6), incl. the js_dom points-scanner dedupe and **H7** | ~−35 | low |

Cumulative through R3.5 ≈ **−5,680 net** (−5,730 of deletions, less the ~50 lines of new regression tests R3.0 adds) — the ≥5,000 target is met before the regex work starts. R3.6–R3.8 add ~−2,430 of upside. If a phase underdelivers, the target still holds at ~75% realization of R3.1–R3.5, or at lower realization plus either R3.6 or R3.7.

## 7b. Execution status (2026-08-18)

Core-JS scope only — Node workstreams (D, F1, F3, F5, and the assert/util
workstream B) are deferred at the owner's direction.

**Net so far: −367 across `lambda/js`** (294 insertions, 661 deletions).

| Item | Result |
|---|---|
| R3.0 `ARRAY_NUM` bug class (§4.1b) | 10 sites fixed in `js_util.cpp` / `js_globals.cpp` |
| R3.0 `worker_threads` constructors | exports the real globals; identity matches Node |
| G1 `jm_collect_functions` | 661 → 497 |
| G1 `jm_collect_func_assignments` | 192 → 96 |
| G1 `jm_callsite_scan_node` | 143 → 82 |
| H1 test262 intercept table | 144-line chain → table + emitter; dead `js_decimal_to_percent_hex_string` deleted (decl, def, registration) |
| G4 binary-op native lanes | float-arith + comparison tables; DIV's byte-identical `both_int` split collapsed |

**Gates, green after every step:** `test262-baseline` 40261/40261 with zero
regressions (four separate runs), `test_js_mir_emission_gtest` 21/21
byte-stable, `test_js_gtest` clean apart from `lib_tabulator`, `test/node`
unchanged at 161/36.

`lib_tabulator` was verified failing at pristine HEAD (stash + rebuild), so it
is pre-existing and not attributable to this work.

**Deferred:** `jm_infer_walk` — five parameters, so the visitor adapter needs a
context struct, dropping it to roughly −35 for meaningful complexity. Next
best: `jm_mutable_native_var_needs_boxing_walk` (bool walker; needs
`js_ast_any_child`, not the void visitor), then workstream A.

---

## 8. Success criteria

1. ≥5,000 net LOC removed from the JS runtime (measured as `git diff --shortstat` over `lambda/js` + `lambda/module/node_core` across the `js-r3` commits).
2. Every §4 bug fixed with a committed regression test that survives all later phases; behavior changes only where §4 says so.
3. Zero regressions: `test-lambda-baseline`, `test_js_gtest`, `test_js_mir_emission_gtest`, `test262-baseline`, `test/node` set, `node-baseline` stay green throughout; `test262-full` delta-free at phase ends.
4. `std::` usage strictly decreases (regex wrapper deletion + follow-up port; no new sites — grep at phase gates).
5. No release-bench regressions; the A1(g) one-walk set-miss and A3 descriptor-POD changes are expected to *improve* set/for-in paths (measure, don't assume — round 2's DOM prop-id lesson).
6. Deleted duplicates are deleted, not orphaned: phase-end grep proves zero references to removed statics/aliases; every promoted helper lives in exactly one module header (rule 13).
