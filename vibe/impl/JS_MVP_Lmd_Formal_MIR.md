# JS MVP: activate the formal MIR engine

**Date:** 2026-10-16. **Status:** implementation in progress.
**Authority:** D8.1.3v25; [MVP design §36](../jube/JS_MVP_Lmd.md#36-formal-javascript-mir-engine).

## Contract

MVP-Lmd supplies normal JavaScript MIR execution. Dynamic source continues in
the retained AST interpreter. Unsupported features produce real failures;
there is no implicit legacy compiler retry. Reuse shared Lambda ownership,
precise roots, shapes and operation kernels (D1.3v3, D3.4, D5.2–D5.3).

## Work

1. Integrate MVP with the public runtime entry points. Retain compiled units
   through the existing Script extension destructor so escaping values and
   callbacks survive until the caller's heap is finalized. Avoid cross-heap
   copies and separate evaluator contexts.
2. Route normal CLI, MIR batch/harness, module, document/preamble and AUTO
   compilation edges to the same engine. Keep AST selection independent of
   MIR engine selection. Audit every legacy compiler caller.
3. Run the unchanged formal JS suites and Test262 baseline. Preserve raw
   failing/crashing/timed-out records and distinguish them from historical
   selected MVP tests. Diagnose and repair the activated runtime.
4. Complete the existing full-parity backlog: independent agents, child-realm
   module ownership, syntax/environment/exotic cases, and the pending Unicode
   target decision. The last intermediate whole MVP audit is not a score for
   this activation.
5. Require shared Lambda and Test262 gates, precise-GC checks, and pinned
   release MIR benchmark comparisons before claiming closure. Existing
   performance gaps remain open until measured and repaired.

## Ownership adapter

New helper announced before implementation: `mvp_lmd_execute_in_runtime`.
It extends the existing MVP executor with a caller-owned EvalContext. A small
Script extension retains its AST, MIR and per-unit metadata until normal
Runtime script teardown, after heap finalization. Standalone MVP execution
retains its previous own-context lifetime. No guest values are converted to
legacy callable or object layouts.

## Evidence

Entry is clean commit `f41d0259ee2beb1188ff5c66a968025e4dc60d71`.
Private builds, source fingerprints, commands and results are under
`temp/js_formal_mir_20261016/`. Each production pin records its source hashes;
the shared `lambda.exe` is not used as the identity of a release result.

### Implemented

- The normal public Script/Module entry, CLI, and formal MIR Test262 batch
  select MVP. The parser's existing module-goal scanner is shared with the
  public adapter. Explicit AST selection remains available; dynamic source
  continues through the retained interpreter (D8.1.3v25).
- Public results use the caller's heap. Runtime Script retirement retains
  generated code and metadata for escaped closures; shared host calls enter
  their MVP callable ABI without casting them to `JsFunction` (D5.3).
- Fixed public buffer/child-realm exposure, hosted `with` capture and
  `@@unscopables`, recursive stack recovery, large-class metadata rooting,
  closed-class fallback to Object.prototype, console aliases, disposal
  symbols, native callable source names, TDZ binding diagnostics, and live
  namespace results for the public module API.
- Shared RegExp lowering orders string alternatives by code-point length;
  the backtracking matcher indexes large literal-alternative sets while
  retaining source priority. Execution budgets are unchanged.

`mvp_lmd_execute_in_runtime` is the new public ownership adapter. The existing
module-goal scanner and native-function source formatter were promoted for
reuse. Other new local helpers are `js_with_property`,
`bt_index_literal_alternatives`, `v_append_ordered_string_set`, and
`host_console_label`. TDZ diagnostics extend the existing failure helper with
an optional message; normal function/object allocation layouts are unchanged.

### Verification checkpoints

| Pin / gate | Result | Scope |
|---|---|---|
| `host60`, release MVP units | 767/767 pass | Interleaved eval/with records, declaration order, escaped closures, saved receivers, unscopables and Annex B publication |
| `host60`, focused normal and forced/poisoned GC | 100/100 each | Existing formal-entry/full-parity checks plus eight new environment checks |
| `host59`, selected host fixtures | 51/51 pass | CLI is byte-identical to `host60`; previous 49 plus original eval/with and closure fixtures, unchanged goldens |
| `host59`, eval/with fixtures with forced/poisoned GC and explicit AST | 2/2 each | Both original fixtures pass in each configuration |
| `host59`, isolated Script integration | 198/205 pass; 7 fail; no timeouts | Same seven AUTO publication failures |
| `host59`, replay of previously failing small fixtures | 28/32 pass; 4 fail | Eval/with repaired; agent-state and CSS/JSON contract cases remain |
| `host60`, canonical eval/with slice on explicit AST | 650/650 pass | Original sources, metadata and runner; checks the shared interpreter lookup and publisher |
| `host60`, canonical integrated slice on formal MIR | 3,764/3,764 pass | Prior 3,114 property/Function/Date/Array iterator/module cases plus 650 eval/with cases; no unresolved records |
| `host56`, release MVP units | 759/759 pass | Lambda fn/pn imports, rooted export cells, foreign map reads and native construction of MVP classes |
| `host56`, focused normal and forced/poisoned GC | 92/92 each | Includes warm Lambda namespace admission and default class callable/construct capabilities |
| `host55`, selected host fixtures | 49/49 pass | CLI is byte-identical to `host56`; includes both original Lambda import cases and two custom-element fixtures |
| `host55`, import/custom-element fixtures with forced/poisoned GC | 4/4 pass | Original imports, original custom elements and frozen-constructor ownership |
| `host55`, isolated Script integration | 198/205 pass; 7 fail; no timeouts | Same seven AUTO publication cases |
| `host55`, replay of previously failing small fixtures | 27/32 pass; 5 fail | Both Lambda import failures and custom-element construction repaired; same selected population |
| `host56`, canonical property/Function/Date/Array iterator/module slice | 3,114/3,114 pass | Union of the prior 2,382-case slice and 732 module/import/export cases; unchanged corpus and runner, formal MIR, no stripped mode |
| `host45`, release MVP units | 756/756 pass | Includes retained loose-equality coercion and preparation of function enumeration storage |
| `host45`, focused normal and forced/poisoned GC | 89/89 each | Includes coercion reentry, original throw identity, and unread inherited accessors |
| `host45`, selected host fixtures | 45/45 pass | Same original fixtures and goldens as `host44` |
| `host45`, canonical property/Function/Date/Array iterator slice | 2,382/2,382 pass | All 17 formerly failing cases in this slice repaired; unchanged sources/metadata/harness, formal MIR, no stripped mode |
| `host45`, isolated Script integration | 198/205 pass; 7 fail; no timeouts | Seven known AUTO publication cases |
| `host45`, replay of previously failing small fixtures | 24/32 pass; 8 fail | Same selected population and failures as `host42`; separate from a complete recount |
| `host44`, release MVP units | 754/754 pass | Includes shared static-method source spans and native IdentifierName validation |
| `host44`, focused normal and forced/poisoned GC | 87/87 each | Includes private iterator caches, lazy host overrides and nested/escaped CommonJS require |
| `host44`, selected host fixtures | 45/45 pass | Includes the original dynamic-call and native-function source fixtures; unchanged goldens |
| `host44`, isolated Script integration | 198/205 pass; 7 fail; no timeouts | Only the seven known AUTO publication cases |
| `host42`, selected host fixtures | 44/44 pass | Native timers/strings, host families, host subclasses, virtual collections and source-relative CommonJS; unchanged goldens |
| `host42`, isolated Script integration | 198/205 pass; 7 fail; no timeouts | Seven known AUTO publication cases; AST CommonJS regression repaired |
| `host42`, canonical property/Function/Date/Array iterator slice | 2,365/2,382 pass; 17 fail; no timeouts/crashes | Same pre-existing property/Function failures as `host24`; later source-span repairs are recorded below |
| `host42`, replay of previously failing small fixtures | 24/32 pass; 8 fail | Selected replay, separate from the complete isolated fixture diagnostic |
| `host27`, existing DOM/Intl fixture gates | 25/25 pass | Date/FileList input round trips and both animation-clock fixtures; unchanged goldens |
| `host27`, release MVP units | 743/743 pass | Shared Date storage/clocks, native array callables, host array descriptors and home-realm dispatch |
| `host27`, focused normal and forced/poisoned GC | 76/76 each | Includes native Date mutation, virtual/frame clocks, callback ABI admission and child-realm Promise jobs |
| `host24`, shared Item/shape suite | 40/40 pass | Private pinned test executable |
| `host27`, isolated Script integration | 198/205 pass; 7 fail; no timeouts | Seven known AUTO publication cases remain |
| `host27`, canonical Date Test262 slice | 583/583 pass | Shared private DateValue storage and clock; formal MIR entry, unchanged metadata/source |
| `host24`, complete isolated formal fixture diagnostic | 461/507 pass; 41 fail; 5 outer timeouts | Unchanged runner, one selected case per process, two workers and a 180-second outer diagnostic limit; separate from whole-batch validation |
| `host19`, canonical Array keys/entries slice | 24/24 pass | Shared sequence iterator kernel; formal MIR entry |
| `host19`, existing DOM/Intl fixture gates | 22/22 pass | Includes `dom_module_props`, native constructor/prototype identity, Array iterators and attribute nodes; unchanged goldens |
| `host19`, release MVP units | 738/738 pass | Host-property bridge, generic-call expansion and sequence iterator installation |
| `host19`, focused normal and forced/poisoned GC | 71/71 each | Includes native callbacks, renamed errors, imported globals and iterator scalar ownership |
| `host19`, shared Item/shape suite | 40/40 pass | Private map clones preserve nominal identity and support forced snapshot detachment |
| `host19`, isolated Script integration | 198/205 pass; 7 fail; no timeouts | Seven known AUTO publication cases remain |
| `host24`, canonical property/Function Test262 slice | 1,758/1,775 pass; 17 fail; no timeouts/crashes | Every outcome matches `host19`; the 17 failures predate the host-boundary changes |
| `regexp2`, release MVP units | 727/727 pass | Three new RegExp state/realm/recompile checks |
| `regexp2`, focused normal and forced/poisoned GC | 61/61 each | Includes UTF-16 captures, child realms and retained dynamic source |
| `regexp2`, isolated Script integration | 198/205 pass; 7 fail; no timeouts | Wide-subject RegExp case fixed; seven AUTO publication cases remain |
| `regexp2`, existing named-capture fixture gates | 2/2 pass | Normal formal MIR fixture runner, unchanged golden |
| `regexp2`, shared Item/shape suite | 39/39 pass | Includes root-vector retirement after heap destruction |
| `regexp2`, canonical RegExp Test262 slice after individual replay | 1,650/1,721 pass; 70 fail; 1 timeout | All 151 initially nonpassing cases have identical replay outcomes on the previous `document3` pin |
| `document3`, release MVP units | 724/724 pass | Includes reusable module/preamble code and per-realm state |
| `document3`, focused normal and forced/poisoned GC | 58/58 each | Escaped code survives artifact retirement; fresh realms retain distinct state |
| `document3`, formal MIR Test262 module/import slice | 693/693 pass | Existing target set, unchanged metadata/source, normal MIR entry |
| `document3`, isolated Script integration | 197/205 pass; 7 fail; 1 timeout | Three module-cache cases fixed; AUTO publication and RegExp remain |
| `clone1`, release MVP units | 722/722 pass | Includes persistent public realms, shared cloning, host global identity and Lambda binary ingress |
| `clone1`, focused forced/poisoned GC | 56/56 pass | Includes heap replacement, native callbacks, child registries, TLA and queued nested imports |
| `clone1`, isolated Script integration cases | 194/205 pass; 10 fail; 1 timeout | Same 205-case population; default-engine and numeric representation assertions updated as explained below |
| `host_policy1`, six existing integration cases | 6/6 pass | Module resource admission, public host global reads, default engine and numeric-result checks |
| `module_goal1`, stored Unicode 16 RGI Emoji | 1/1 pass | Unchanged formal MIR Test262 harness; canonical Unicode 17 is separate |
| `module_goal1`, five existing module fixtures | 5/5 exact-output pass | Ordinary public `js` command, `JS_EXEC_BACKEND=mir` |
| `diagnostics1`, full formal JS fixture suite | 365/507 pass; 142 fail | Earlier pin; later module/interop fixes are not added to this aggregate |
| `diagnostics1`, standard Test262 baseline | 40,221 fully pass; 20 fail; 20 retry-only / 40,261 | Unchanged hybrid AST/MIR routing, **not** an all-MVP recount |
| `diagnostics1`, shared Lambda baseline | 7,234/7,471 pass; 237 fail | Earlier pin; legacy internal assertions, integration gaps, and environmental failures retained |

The later `diagnostics1` pin also passes exact-output replay of
`native_function_to_string`, `v20_tdz`, `console_log`,
`store_ic_ctor_reserved_slot`, and `regression_ast_self_tail_call`.
Its shared Lambda and Test262 gates are recorded below; its full formal JS fixture run is also complete. Individual replays are not added to an earlier aggregate score.

The first `diagnostics1` integrated unit run exposed 15 diagnostic-string
assertion failures: the new TDZ message omitted the type from the standalone
diagnostic API. `diagnostics2` keeps report text separate from guest
`Error.message` and restores that classification: 711/711 units and 45/45
focused forced-GC checks pass. The later namespace pin above adds the public
module namespace check.
The shared `test_js_script_gtest` run records 39/205 passing. Some later
failures cascade after an early fatal assertion skips runtime cleanup:
`RetainedHarnessRebuildsAfterRealmReplacement` and
`DirectEvalSharesInterpretedFunctionEnvironment` both pass isolated replay.
This does not clear the aggregate gate or the real host/promotion gaps.

### Remaining integration work

| Entry | Current implementation |
|---|---|
| Ordinary JavaScript CLI/public Script | MVP MIR |
| JavaScript ES modules | MVP MIR, or explicitly selected AST |
| Formal MIR Test262 source/harness batches | MVP body plus retained AST harness |
| `eval` / Function-family dynamic source | Retained AST, as required by D8.1.3v25 |
| Document/preamble compiled artifacts | MVP MIR; shared code images with fresh realm metadata |
| Explicit AUTO per-function promotion | Disabled pending MVP publication adapter |
| TypeScript-specific lowering | Existing path; separate from this JS activation |

1. **Cached metadata and document hosts.** Document/preamble and external
   classic-script compilation now use MVP code images. Fresh realms still
   parse and lower their own metadata before exact-image code reuse. Removing
   that repeated frontend work requires an immutable frontend image with
   execution overlays. DOM/Web host adapters remain incomplete, so activating
   the document compiler does not establish browser-script compatibility.
2. **AUTO promotion.** Legacy satellite publication is disabled, because its
   callable ABI cannot publish MVP entries. Explicit AUTO still interprets
   supported units and routes whole-unit capability misses to MVP. Per-function
   MVP promotion remains unimplemented.
3. **Host API integration.** Native callable/object properties, lazy host
   namespaces and prepared globals now use the shared services through the
   adapter described below. Additional DOM/Web integration, CommonJS and process events still need
   broader validation or adapters. Public timer handles and DOM string handlers
   now use the shared event loop, as described below. The full
   fixture failures are retained; no legacy retry or test-harness exclusion
   hides these gaps.
4. **Language/environment gaps.** Independent agents and the module direct-eval
   import-cell bridge remain open. MIR `with`/eval now shares interleaved records,
   as described below. RegExp legacy match reads and the wide-subject replacement
   integration failure are repaired in `regexp2` below; broader conformance is
   recorded separately.
   The `v9_number_json` fixture also expects the legacy JSON parser's i64-to-
   BigInt result for a large JSON integer, whereas MVP requests JS Number
   parsing. S4.9.1 defines actual host numeric crossings; the JSON contract
   needs resolution before altering either implementation or expectations.
5. **Unicode and acceptance.** Stored string-property tables are Unicode 16;
   canonical Test262 expects Unicode 17. That version decision remains pending.
   Benchmark timing remains deferred; neither no-regression acceptance nor
   complete formal-engine activation is claimed. Open public Runtime entries
   currently request the complete mutable realm. Their loss of closed-unit
   specialization, including ordinary CLI execution, requires measurement and
   a sound optimization policy before performance acceptance.

### Module ownership repair

The source-module callback owner is a `MvpLmdProgram`, not an execution pointer.
Child realms now receive separate module controllers/registries in the existing
EvalContext, while retaining the host resource policy. The shared dynamic-import
loader is promoted and queues a captured policy; `mvp_lmd_dynamic_import` is the
new thin realm adapter. The existing native-payload callable factory accepts an
optional precise closure environment (D5.3.3, D6.2.2v2). A two-child-realm module
identity/TLA/nested-import check passes normally and with forced/poisoned GC on
`module_bridge1`.

The 20 standard Test262 failures are the independent-agent `Atomics.waitAsync`
cases. The 20 retry-only outcomes remain non-fully-passing. Shared Lambda
failures include obsolete legacy-MIR internal assertions, public host/realm
and cross-language integration gaps, missing Python PIL, and timeouts. The
15 standalone diagnostic assertions are fixed on the later pins; those fixes
do not change the archived shared baseline count.

`realm_modules1` exposed a pending TLA completion in the new child-realm test:
the shared continuation looked up its module path in the parent registry.
The follow-up captures canonical descriptors and adds `js_tla_flush_for_runtime`
(the existing no-argument wrapper is preserved). The resulting tests pass on
`module_bridge1`, as recorded above.
A separately reproduced `tune4_call_construct` crash was a null builtin-library
dereference during public receiver-only setup, before entering guest code;
the guard and a minimal `this` check pass on `module_bridge1`; the original
fixture prints `4:4` without crashing.

### Cross-language namespace integration

Lambda's import bridge now enumerates map and virtual-map namespaces through
`item_keys`; MVP source namespaces publish the existing export lookup and
callable metadata operations. The subsequent native crash identified a real
storage assumption in `vmap_get_by_str`: non-hash virtual maps must dispatch
through their backend, rather than cast their payload to `HashMapData`. That
fix and direct Lambda-name lookup coverage pass on `module_bridge2` (714/714
units and 48/48 focused normal/forced-GC checks). Existing `import_js` and
`import_js_naming` fixtures match their goldens. No additional export-call
engine is introduced.

### Shared host realm and Lambda ingress

Public entries retain one execution owner per heap, with separate compiler
units sharing the realm's global and intrinsic slots. Declaration preflight
checks the whole Script before publishing bindings; global lexicals remain
off `globalThis`. Existing native functions cross through the retained
interpreter's callable adapter, including its identity cache (D5.3, D6.2.2v2).
The shared global getter/setter and binding lookup now observe this realm.
Module admission checks the existing resource policy before both warm registry
lookup and queued import creation; the queued loader checks again at entry.

New helpers announced before implementation: `mvp_lmd_new_unit`,
`mvp_lmd_prepare_script_bindings`, and `mvp_lmd_runtime_reset`. The last retires
heap-derived Promise/module owners while Script extensions retain compiler
records until heap finalization. Pins: `persistent3`
`6a7f172157617fe57fe8b0ed2889dc556c1dcf21fb34bb429b8993595c625fdd`,
`host_policy1`
`00cb1f0f9f75f0d5e4f60bf6803a399c6766f875454cc8cc51f5c483046d98f6`.

Lambda Promise/task interop reuses the existing public event-loop initialization.
Binary ingress extends the shared typed-array constructor policy through
`js_typed_array_from_binary_with_ops`; the clamped-byte case shares that kernel.
DataView conversion checks its physical carrier brand. Export calls normalize
native wide integers through the existing BigInt converter (S4.9.1), retaining
all arguments across allocation.

`interop3` passes `js_array_props_tail_bridge`; `interop1` passes
`conc/js_promise_wait` and `conc/to_promise`, all with exact golden output.
`binary_js_bridge` now has 11 of its 12 output fields correct; `Buffer.from`
still fails because its host binding is missing. These fixture improvements
do not revise the archived shared Lambda aggregate.

The earlier `host_global1` isolated run (188/205) had seven AUTO promotion cases, three
module-cache cases, missing `structuredClone`, module-policy admission, and
four assertions tied to the previous default/backend Number representation.
The last five issues pass the six-case `host_policy1` replay above; numeric
assertions now use JS equality, allowing the internal integer subtype specified
by D2.2.5, while the default-selection assertion requires MVP ownership.
At that checkpoint AUTO/cache failures remained; the module-cache failures are
fixed in the later code-image work below. The RegExp wide-subject case timed
out at 120 seconds and remains unresolved; this is not a performance result.
The final `clone1` isolated replay passes 194/205: seven AUTO publication cases
and three compiled-module cache cases fail, while the RegExp case records
legacy-static-state assertion failures and then times out. Each case runs in
its own process to prevent a fatal assertion's skipped cleanup from poisoning
later results; this is separate from the archived monolithic baseline.

### Shared structured cloning

`js_structured_clone_with_ops` adds optional realm allocation/property callbacks
to the existing clone traversal; `mvp_lmd_structured_clone` supplies MVP array,
object and typed-buffer policies. There is no second recursive clone engine.
The normal and forced-GC checks pass on `clone1`, including nested getters,
prototype identity, independent byte storage and empty typed-array identity.
Pin: `54f6596a1a2e5e260aae647dccf5ce052e0796449d6efc13979ddb8d38454a88`.
This does not claim broader structured-clone semantics beyond the existing
kernel (cycles/transfers remain separate work).

The existing `JsStructuredClone.RetainsRecursiveDestinationsAcrossEveryAllocation`
case now passes for both AST and formal MIR, alongside the callback-rooting,
module-policy and inline-dynamic-import checks (4/4, `clone1/script-host-fixed.json`).
The latest pin also repeats six Lambda interop fixtures with exact golden
output (`clone1/interop-replay/results.json`); `binary_js_bridge` retains only
the missing `Buffer.from` discrepancy.


### Relocatable code images and document activation

Continuation entry: `8b450390fc4f4d99eb2dc347b9c5c2043f250225`.
Under **D8.1.3v25, D8.5.1v2 and D5.3**, document preambles, cached external
classic scripts and their uncached consumer path now enter MVP. Shared module
artifacts use the same InputManager cache and dependency invalidation service.
Dynamic source still uses the retained interpreter.

A `MvpLmdCode` owns only finalized MIR/native entries and its serialized code
image. Every embedded source/metadata pointer in reusable code is loaded from
an instance-owned relocation table. Slots, literal storage, AST, classes,
shape/property caches, library state and imported bindings stay with the
receiving realm. Atomic code references keep entries alive through escaped
function retirement, independently of cache or document-preamble leases.
Ordinary uncached Script emission retains immediate constants.

Reuse requires byte-for-byte equal serialized MIR, function counts and
relocation counts. A mismatch compiles fresh MVP code. This is an intermediate
implementation: hits save native code generation, but still rebuild and lower
frontend metadata. It does not claim complete frontend-cache reuse or measured
performance acceptance. No guest heap is created by compile-only preambles.

New helpers announced before coding: `relocated_constant` / `relocated`,
`mvp_lmd_code_image`, `mvp_lmd_code_retain/release/bytes`, and
`mvp_lmd_compile_script_image` / `mvp_lmd_execute_script_image`. Existing compiler
and public execution adapters accept the cached image; source-reference
retention was folded into the existing unit compiler.

The module-cache integration tests retain their cache-hit/build/invalidation
assertions. Their six Number-result comparisons now use JS equality, allowing
MVP's internal integer representation (**D2.2.5**).

`document1` production SHA-256:
`eded95fe9902f9ca7802cae3fabf293759c54b9408ac825fbffa7af416501620`.
Its build records 2,941 unchanged production inputs. Focused normal checks:
58/58; full MVP units: 724/724. Further integration results are recorded below.
The new checks require identical native function addresses in successive fresh
realms, separate closure/class state, fresh property/RegExp metadata, and code
survival after releasing preamble artifacts. Earlier aggregate gate counts
above are historical and are not recalculated from these focused checks.


Final `document3` release pin:
`5788cf0b5deb8baac46fadd64b7f1085463897a1038518ba5ec048da6c3f1c83`.
All 2,941 production fingerprints still match the build. It passes 724/724
MVP units, 58/58 focused checks both normally and with forced/poisoned GC,
and the 693 module-code/dynamic-import Test262 cases in the existing target
set through `--mir-only` (no stripped harness). Artifacts are under
`temp/js_formal_mir_20261016/document3/`; this selected Test262 run is not a
whole-suite recount. The earlier `document1` isolated integration replay
passes 197/205, fixing all three cache failures; only the seven AUTO
publication cases and the RegExp timeout remain. The final `document3` replay
confirms the same 197/205 result.

The code owner reuses `lib/atomic.h`. Artifact publication now transfers
ownership only on success; failed publication frees the temporary wrapper,
leaving the caller's code/metadata alive for cleanup or execution. Shared
Lambda/full Test262/full JS aggregate gates have not been recounted in this
continuation. Benchmark timing remains deferred at the user's request.

### Shared RegExp match state and streaming replacement

`regexp2` keeps the normal formal MIR entry and reuses the existing runtime
kernels (D8.1.3v25). The earlier pinned CLI returned `undefined` for legacy
RegExp captures/context; its million-run wide replacement exceeded the
15-second reproducer deadline. The unchanged integration test had exceeded
120 seconds. Both cases now complete correctly; these are functional timeout
checks, not benchmark measurements.

The shared `js_regexp_update_last_match_spans` extends the existing rooted
match-state publisher with byte/UTF-16 coordinates. `js_regexp_legacy_get` and
`js_regexp_legacy_name` expose its readers/catalog. MVP's small accessor and
initialization adapters retain a separate `JsRegexpLastMatch` in each realm
capsule; dynamic AST calls use the same realm state. Successful native matches
publish captures; failed matches retain the previous state. Closed units whose
analysis excludes observation retain the unobserved path.

`js_regex_detect_simple_class` and `js_regex_match_simple_class` are promoted
from the existing character-class scanner. `js_regexp_replace_spans` extracts
the existing replacement loop behind matcher/state callbacks for both engines.
Its guard requires ordinary native protocol behavior, writable `lastIndex`,
a string replacement and no captures. Wide subjects additionally require
quantified built-in classes with safe byte spans. The loop copies directly to
the existing `StrBuf` and records only the final match, eliminating retained
per-hit arrays and repeated UTF-16 prefix scans. Overrides, callable
replacements and other patterns keep the existing JS protocol path
(D8.1.3v25).

`RegExp.compile()` now refreshes its Unicode matching mode as well as flags
and matcher metadata. Validation also exposed a shared teardown defect:
`heap_destroy()` freed the heap without clearing its owner's pointer. Later
root-vector retirement inspected that freed allocation. The shared destructor
now clears the pointer; roots remain precise (D5.3/D5.4), and a direct Item-suite
regression covers retirement after destruction.

Release SHA-256:
`6a76928c8c4a35780bfc7a44a78602662175bbfee3170fed589f3a6b6231e5d7`.
Artifacts, exact commands and source fingerprints are under
`temp/js_formal_mir_20261016/regexp2/`. All 727 MVP unit tests and 61 focused
checks pass, including all 61 with forced collection and poisoned freed memory.
The existing 205-case isolated Script replay improves from 197 pass / 7 fail /
1 timeout to 198 pass / 7 fail / 0 timeout. Its wide-replacement case covers
17 sources under AST and formal MIR, each with normal and forced collection.
The two named-capture fixture gates and all 39 shared Item/shape tests pass.
Full historical aggregates above are not recomputed from these focused gains.

The canonical RegExp slice covers 1,721 IDs through the unchanged formal MIR
runner (`--mir-only --no-stripped`). Its first chunked run reports 1,570 pass,
146 fail and 5 timeout; interrupted chunks leave some cases unreported.
Individual replay of all 151 nonpassing IDs gives 80 pass, 70 fail and 1
timeout. The previous `document3` pin produces **exactly the same 151 replay
outcomes**. Thus the candidate has 1,650 passing IDs after replay; 70 generated
Unicode property/string cases and `Unified_Ideograph` (timeout) remain open.
This is a selected-slice comparison, not a whole Test262 recount or a claim
that timing is stable. Raw runs and `regexp-comparison.json` preserve both
the initial outcomes and their replays. Benchmark timing remains deferred.

### Host property and global integration

The formal entry now imports already prepared host globals into the active MVP
realm and reuses the shared lazy namespace catalog for Intl, Buffer and other
host services (D8.1.3v25). Existing MVP intrinsics retain their identity. Window
aliases are rebound to the active global; lazy values are resolved once into
that global. Document/UI bindings are installed at the formal entry before the
first unit is instantiated.

Native wrappers retain their underlying callable in a precise closure slot.
`mvp_lmd_foreign_property` delegates descriptors, mutation, prototypes and
enumeration to the existing shared property/exotic operations. Callable ABI,
existing nominal metadata and host virtual-container identity select this
boundary; ordinary MVP containers gain no `is_normal` flag. Shared host reads
of MVP functions dispatch by ABI instead of interpreting the legacy function
tail (D6.2.2v2). `mvp_lmd_project_keys` extracts the existing proxy projection
loop for reuse. Other new adapters are `js_install_host_namespace_slots` and
`mvp_lmd_import_host_globals`.

Buffer initialization exposed an unrelated assumption at the same boundary:
the legacy descriptor clone dropped MVP nominal identity, and descriptor
promotion changed the map's storage kind. Descriptor mutation now reuses
Lambda's `map_clone_typemap_for_mutation`, with an optional forced detach for
snapshots, and preserves MVP nominal/storage identity. Attribute transitions
reuse `type_tree_reflag_field` (D3.4); no second clone implementation remains.

Native constructor reads now materialize lazy `name`/`length`/`prototype`
descriptors before interpreting a miss. Inherited property walks continue
through foreign storage with the original receiver. `branch_virtual_object`
admits host virtual carriers at generated reads, writes and sloppy receiver
entry. Shared errors retain their native class while preserving a separately
mutable public `name`. Prepared Window import also rebinds the document's
precise `defaultView` root; rebinding only global alias properties left this
readonly host getter pointing at the retired staging global (D5.3).

Array `keys` and `entries` now use the existing sequence iterator carrier,
projection and stepping kernels. Their installation reuses the shared builtin
catalog. Collection projections retain their branded admission; generic Array
methods apply ToObject, preserve live length and inherited reads, and retain
entry scalar values across allocation. No new iterator carrier/helper is added.

The `host14` whole fixture run was interrupted after a stalled DOM compilation
and oversized library compilation; its raw log and interruption record are
retained. It is not a completed aggregate. The earlier `diagnostics1`
365/507 result remains the last completed whole-batch fixture checkpoint.
The later `host24` isolated diagnostic completes the same 507-case population
with 461 passes, 41 failures and five outer timeouts; individual GTest selection
is checked to contain exactly one test. Focused gains are not added to either
aggregate. Additional host global installation,
CommonJS and AUTO publication remain open, along with
the broader conformance and performance gates.

### Bound generic call expansion at public entries

The stalled `dom_module_props` run was sampled in MIR register allocation,
before guest execution, with about 22 GiB of resident memory. Its finalized
entry contained 245,667 MIR instructions; root analysis visited 99,101 basic
blocks. Generic calls still duplicated callable admission, native/foreign
dispatch and intrinsic-token fallbacks after the existing compact-property
budget had been reached.

Large open entries now capture actual callable properties and reuse
`mvp_lmd_class_invoke` after that same emission budget, with the existing
argument-root span and pending scalar-result handling (D6.2.2v2, D5.2.1v3,
D5.3). Closed-unit lowering, small entries, function bodies, specialized calls
and dynamic-source interpretation keep their existing policies. No new runtime
helper or vendor change is involved. The finalized DOM entry falls to 162,584
instructions and 57,703 analysis blocks; the unchanged fixture completes and
passes its golden on `host19` after the `defaultView` repair.

Synthetic coverage checks getter/argument order, receiver identity, proxies,
native calls, thrown identity, wide scalar ownership and bounded emitted-code
growth. These are compile-growth and correctness results, not benchmark timing
or a no-regression performance claim. Benchmark timing remains deferred.

### Shared Date and native array boundaries

Native DOM Date construction and MVP Date methods now share the same private
DateValue shape key (D3.4). The existing `js_date_get_time_value` is promoted;
the announced `js_date_store_time_value` reuses ordinary map shape storage or
the existing native descriptor storage path. Both Date operation policies use
these kernels. Freezing the public object does not freeze its internal value;
public `__time__` fields, inherited Date prototypes and proxies cannot forge
the Date brand. Native Date construction roots its input before allocation and
uses destination-owned scalar storage instead of a static numeric ring
(D5.3/D5.4). The DOM `valueAsDate` setter validates through the shared kernel.

Host arrays can expose native functions directly in elements or own methods,
without going through a foreign property read. Generic invocation now checks
the published callable ABI before reading an MVP tail and reuses the existing
identity-cached native bridge (D6.2.2v2). Array companion metadata selects the
existing foreign descriptor/prototype service; this restores FileList methods,
branding and inherited accessors without a new container flag. Ordinary MVP
array storage and closed direct calls retain their dispatch policy.

The announced `mvp_lmd_program_for_global` resolves a native callable's existing
home global to a retained MVP realm. Native wrappers and Promise job policy use
that owner when available; prepared host globals that predate MVP retain the
existing caller policy. This fixes a native-callable admission failure that
otherwise poisoned child-realm Promise jobs (D8.1.3v25, D5.3). The focused and
forced-GC gates above include those child-realm cases. The real DOM Date setter
probe also checks frozen Dates, subclasses, invalid Dates, and rejected forged
or proxy receivers. These are correctness results; performance remains deferred.

The AUTO audit confirms that existing promotion admission permits calls, global
reads and top-level bindings. Publishing an isolated function into a fresh MVP
realm would lose those environments. The gate remains disabled until a shared
realm/environment publication adapter is implemented; enabling the old switch
would also reactivate the legacy loop compiler (D8.1.3v25).

### Shared host clocks

The broader formal fixture run exposed a separate clock domain in MVP:
`performance.now()` subtracted a private execution origin and Date read wall
time directly. Native animation frames advance the shared virtual clock, so
MVP callbacks observed neither that advance nor the native frame timestamp's
origin. MVP now reuses `js_performance_now`, `js_performance_time_origin_ms`
and `js_date_now`, including no-argument Date construction and the Date function
call. Its private host origin is removed. The existing Date numeric reader is
promoted for reuse; no new clock kernel or helper is introduced. This keeps
native callbacks and the formal engine on the same existing runtime state
(D5.3, D8.1.3v25). The focused check advances virtual and frame clocks explicitly,
collects between reads, and verifies Date construction and stable timeOrigin.

Release pin `host27` has SHA-256
`b6dfb1285ae3f54f40ac936dc4276e0345d9bc78a5b541b0e2a062f16acb100c`.
All 2,941 production input fingerprints stayed unchanged during its build and
the final source check. Its verification is recorded in the table above.
`host24` and the duplicate-include cleanup pin `host25` have identical CLI
bytes; their property/Function outcomes match `host19` case for case.

### Remaining activated fixture failures

The isolated `host24` diagnostic retains every raw failure under
`temp/js_formal_mir_20261016/host24/fixtures-isolated/`. The later clock pin
passes `dom_console_perf` and `dom_raf_clock`; those two results are not folded
into the earlier aggregate. Other failures include standalone Storage/media/
clipboard globals, host constructors and codecs, Lambda callable/module
interop, Promise/environment behavior, timer handles/string handlers, and
browser-library execution. Some expectations encode legacy behavior, including
CSS string conversion and large JSON integer representation; those goldens
remain unchanged pending semantic review.

Five library cases reach the outer diagnostic limit: `dom_jquery_lib`,
`lib_ajv`, `lib_bn`, `lib_lodash`, and `lib_ramda`. Their confirmed child process
groups were terminated explicitly after the harness timeout; cleanup records
are retained. A Ramda sample reports a `42.6G` physical footprint. Its stripped
addresses are not symbolicated, so the exact compilation bottleneck remains
unproven. The existing large-entry emission repair does not establish bounded
compilation for all libraries. This diagnostic is not benchmark timing or
whole-baseline closure; shared baseline and performance gates remain open.


### Lazy host families and native property ownership

The public MVP realm now exposes the shared host catalog lazily, including
Storage, media, codecs, streams, Headers/Response, clipboard, Window messaging
and catalog functions such as `fetch`. The existing eager AST bootstrap and
the lazy formal engine use the same family installers. Only an owned marker
may be replaced: deleted globals, user replacements and descriptor attributes
survive sibling materialization, including after `preventExtensions`
(D8.1.3v25, D6.2.2v2, D3.4.3v5).

Native property reads, Set, descriptor definitions, own-key reflection and
prototype walks derive ownership from the existing Function ABI and nominal
extension metadata. They enter the existing MVP kernels for MVP storage;
ordinary storage gains no `is_normal` flag. Shared reflection converts
canonical Symbol keys back to their observable identities. Native typed arrays
and host iterators use the active MVP realm's intrinsic prototype identities
(D3.4.4v4, D3.4.7, D6.2.2v2).

A lazy descriptor read can publish an entire host family and relocate Map
storage. `DefineOwnProperty` now refreshes its shape entry and rooted descriptor
operands after that publication, before deciding compatibility or writing.
Native iterator caching previously appended to the shared MVP iterator shape:
the first iterator succeeded, but a fresh iterator read an empty cached `next`
slot. Native writes now retain MVP shape ownership, and iterator caches use an
unforgeable private key through the existing private-slot operations. Caching
preserves the next-method snapshot without exposing keys or invoking guest
cache properties, proxy traps or extensibility checks (D3.4.3v5, D5.3/D5.4).

Public Runtime timers reuse native Timeout handles, coercion and cancellation;
standalone benchmark hosts retain numeric handles. DOM string handlers reuse
the shared scheduling/coercion policy and execute classic source through the
retained AST interpreter in the active realm. Timeout construction precisely
roots each unpublished handle and method. Window messaging/listeners use the
existing DOM EventTarget service; they no longer require an initialized Radiant
script API (D5.3/D5.4, D8.1.3v25).

Source classes can extend native host constructor wrappers through their
existing construct/newTarget protocol. Lambda nominal ancestry is retained
separately from mutable JS constructor/prototype links; native factories keep
their physical brands. Internal Blob creation materializes the shared private
prototype before the public binding is read and preserves user overrides
(D2.6.9v3, D3.4, D6.2.2v2).

New helpers announced before coding: `js_install_host_global_group`,
`js_materialize_host_global_group`, `mvp_lmd_import_host_property`,
`mvp_lmd_owns_properties`, `js_ensure_host_global_family`, and
`js_iterator_next_cache`. Existing `js_timer_normalize_handler` and
`js_module_create_require` were promoted for reuse. No vendor, Test262 runner,
metadata, golden or baseline changes hide integration failures.

The `host36` checkpoint passes 751/751 MVP units, 84/84 focused checks normally
and with forced/poisoned GC, and all 42 selected host fixtures. Its canonical
property/Function slice records 1,758/1,775 passing, with exactly the same 17
failures as `host24`. These selected results do not revise the earlier complete
fixture diagnostic or establish whole-baseline/performance acceptance.
The callable census reports 38 builtin semantic switch cases on both HEAD
and this candidate; the pre-existing ratchet failure is retained. The exception
catalog reports zero violations.


The host follow-up also routes `for…in` on native virtual collections through
the existing foreign own-key, descriptor and prototype operations. The ordinary
MVP no-callback proof excludes those objects; iteration and live-key checks
otherwise retain the same algorithm (D3.4.7, D6.2.2v2).

`createRequire` now retains its filename in the shared native closure layout;
`js_require_at` reuses the existing document/file resolver and loader policy.
CommonJS metadata publishes a per-module require function and the wrapper
captures it, so nested and escaped requires keep their source directory and
cache identity. `mvp_lmd_program_reference` exposes the retained unit reference
for the public lazy binding. These two helpers were announced before coding.
The original `dynamic_adapter_span` fixture then exposed a separate planning
bug: an early single-host intrinsic allocated the host span without assigning
its siblings' slot indices. Public realm planning now indexes the complete
span before global publication (D5.3, D6.2.2v2, D8.1.3v25).


The retained AST does not receive the new formal-only lazy require binding;
its existing unbound-identifier resolver remains active. The integrated
Script check caught that interaction and now returns to its prior 198/205
result on `host42`. The same pin passes 753/753 MVP units, 86/86 focused normal
and forced/poisoned-GC checks, all 44 selected host fixtures, and 24/32 previously
failing small fixtures. Its 2,382-case canonical property/Function/Date/Array
iterator slice records 2,365 passing and the same 17 pre-existing failures,
with no crashes or timeouts. Source hashes and exact commands are retained
under `temp/js_formal_mir_20261016/host42/`.

The remaining small replay failures are two agent-state entries, the two
Lambda-source import cases, custom-element construction, direct eval with
object environments, and the CSS/large-JSON legacy contract cases. This is a
selected replay, not a new complete fixture or Test262 recount. Large-library
compilation/execution, AUTO publication, frontend overlays, the Unicode target,
full baseline closure and performance acceptance remain open.

The static method Function-source follow-up reuses `js_function_source_span`,
the same semantic helper used by the retained interpreter. MVP previously
copied the class member's whole span, including `static` and leading comments.
Class constructors still expose their complete class source; other functions
share the existing method-span policy. No new helper is introduced
(D6.2.2v2, D8.1.3v25).


`host43` passes all 12 formerly failing static-method source cases; its complete
80-case canonical Function-toString slice records 79 passing and one native
source failure. That final failure reports `function $&() { [native code] }`
for the RegExp `$&` getter. The existing `js_regexp_name_is_identifier` helper
is promoted for shared NativeFunction source validation. Valid constructor
names retain their text; names that cannot form an IdentifierName use the
existing anonymous native-function spelling, without changing `.name` or
property identity (D6.2.2v2).


Final runtime pin `host44` has SHA-256
`2af5b05da20fd6d286f010c4882e7ff26df8ef6bc8dec3cb813f7097a591ea6b`.
Its 2,941 production input fingerprints and verification input hashes remain
unchanged. The latest runtime gates are recorded at the top of the table.
The earlier `host40` Script diagnostic briefly added an AST CommonJS failure;
`host42` and `host44` restore that case through the formal-only binding policy.
No benchmark timing was run.


`host44` records 2,378/2,382 passing in the same canonical slice: all 13
Function-source failures are repaired, leaving two boxed loose-equality and two
function-enumeration admission failures. Their follow-up exposes the existing
retained-interpreter binary operation as `mvp_lmd_dynamic_binary` and uses it
only after primitive, nullish and object-identity equality paths. The MIR
import declares reentry and precise Item operands. Function enumeration calls
the existing callable preparation helper before reading its own storage;
non-enumerable inherited accessors remain unread (D5.3/D5.4, D6.2.2v2,
D8.1.3v25). The helper promotion was announced before coding.


Latest release pin `host45` has SHA-256
`b1b1f255a852d0bc937318724d837bc2a1ace758829b94055d7cdd3086c48d6c`.
Its 2,941 production fingerprints and verification input hashes remain
unchanged. All 2,382 cases in the selected canonical slice pass, including
all four final admission failures; normal/forced-GC, runtime fixture and
isolated Script results are recorded in the table. Full Lambda/Test262/JS
baseline closure, the remaining host/environment/library work and performance
acceptance remain open. Benchmark timing remains deferred.

### Lambda imports and custom-element constructors

The next migration increment reuses `load_script_mir_direct`, the canonical
module registry and the existing Lambda procedure-to-Promise membrane
(D7.2.1, D8.1.3v25). New helpers announced before implementation:
`js_load_lambda_module`, extracted from the interpreter loader, and
`prepare_lambda_module`, which publishes foreign exports into the existing
precisely rooted module slabs. Lambda dependencies are not parsed as JS and
have no JS body to instantiate or evaluate. Named imports, namespace imports,
re-exports and dynamic imports share one source record and export-cell identity.

The original Lambda namespace remains on its canonical descriptor. JS uses
the existing module namespace exotic, with its own exact root and the same
readonly/sorted export policy as JS modules. The callable adapter accepts Core
exports only at module publication, preserving their compact ABI and invoking
them through `js_call`/`fn_call_into`; the module slab owns the adapter rather
than a new realm-wide cache (D5.3/D5.4.3, D6.2.2v2). Public `pn` exports keep
the existing Promise scheduler and ordering. This does not add support for
Lambda public variables beyond the shared namespace builder's current exports.

Packed Lambda maps reuse `map_shape_field_to_item` for reads, while writes
continue to require the existing owner/contract admission. Returned Lambda
VMaps reuse the native data-map read path, preserving null versus a missing
field. They do not change the representation of ordinary JS objects or Maps
(D3.4.3v5, D3.4.7). Resting native errors read their owned fields through the
existing `js_get_key_core` carrier path rather than losing their message in
ordinary map/prototype lookup (D6.2.2v2).

Custom-element registration previously cast MVP constructors to `JsFunction`
and crashed in `js_fn_class_ensure`. The announced `_custom_element_class_name`
helper selects the existing JS class association or MVP's private shape slots
by callable ABI. Frozen constructors retain this internal association without
an observable property, a new Container flag or a new native payload layout
(D3.4.7, D5.3, D6.2.2v2). Validation includes constructor identity, duplicate
registration, native `super()` construction, fields and GC.

Checkpoint `host52` passes 758/758 MVP units, 91/91 focused normal and
forced/poisoned-GC checks, 732/732 selected canonical module/import/export
cases, and 198/205 isolated Script cases. Both original Lambda import fixtures
pass. Custom-element construction no longer crashes, but `createElement` still
selects the ordinary HTMLElement prototype; that integration mismatch is
repaired by the follow-up below. These selected gates do not replace the
complete baseline recount or the deferred performance gate.

The DOM lookup called shared `js_is_callable`, whose MVP branch incorrectly
required a non-null body pointer. Default class constructors instead dispatch
through MVP class metadata. The capability query now reuses
`mvp_lmd_is_callable`; calling a class still throws, and construction retains
MVP's native-super/new-target protocol (D6.2.2v2). Shared
`js_construct_value` also routes MVP constructors through that protocol before
reading any JsFunction tail. This keeps native adapters and interpreter
callbacks on the formal callable ABI.

Checkpoint `host54` passes all 49 selected host fixtures (the previous 45,
both Lambda import fixtures, the original custom-element fixture and the new
frozen-constructor fixture). The four import/custom-element fixtures also pass
with forced-every-allocation and poisoned GC. The new direct native-construction
unit exposed the missing `js_construct_value` branch; its repair passes in
`host55` and is reverified in `host56`. No original fixture, golden, Test262
metadata or runner was changed to obtain these results.

Final release pin `host56` has SHA-256
`658edb6fe192a32157f22c95d4b6d54c9a4aca1f8cc05deaba37eac9135ca17c`.
It is byte-identical to `host55`, whose host fixtures and isolated Script
results are recorded above. The direct native-construction repair passes all
759 MVP units and both 92-case focused runs. The 2,941 production inputs and
1,045 verification inputs are unchanged. Its final canonical rerun passes all
3,114 cases with no unresolved records. Exact commands, source/corpus hashes
and the bit-identical CLI comparison are retained under the private pin.
The four import/custom-element fixtures also pass explicit AST execution,
checking the shared loader and the retained constructor-association path.
The exception catalog reports zero violations; the callable census still
fails its existing 38-case ratchet, unchanged from HEAD.

At `host56`, the five remaining small replay failures were the two agent-state entries,
direct eval with an object environment, and the CSS/large-JSON legacy contract
cases. Large-library compilation/execution, AUTO publication, frontend
overlays, the Unicode version decision, full baseline closure and performance
acceptance remain open. Benchmark timing remains deferred.

### Direct eval and interleaved object environments

The formal MIR eval bridge now retains Object Environment Records in the same
`JsInterpEnv` chain as parameter, block and variable records (D5.3, D8.1.3v25).
MIR allocates a record at a `with` statement only when its function needs the
eval environment bridge. Its object is a sixth contiguous traced metadata
Item; escaped eval closures and suspended MIR activations retain it through
the existing precise environment roots. Ordinary MIR functions without eval
keep their existing representation and code path.

Hosted interpreter `with` statements use the same interleaved records. Name
lookup stops at an inner parameter or lexical binding, checks objects in
order, and propagates throwing Has/unscopables operations. A resolved
reference keeps its original object through RHS evaluation and supplies that
object as the receiver of a call or tagged template. `typeof` uses the same
resolved reference without repeating HasBinding. Eval-created vars continue
to bind in the existing caller VariableEnvironment; object records do not
become declaration targets (D8.1.3v21, D8.1.3v25).

Helpers announced before coding: promote the shared
`js_with_binding_is_visible` helper and add `js_with_get_binding_value`, which
shares the existing property/Has kernel with interpreter reference reads and
the retained with-stack path. SetBinding reuses `js_set_with_binding_base`.
The shared Annex B publisher targets the predeclared variable record rather
than resolving through its inner block/object records. MIR's internal saved
object read uses its own statement site, avoiding redirection through the
guest declaration identifier. No Test262 runner, metadata, fixture source or
golden was changed. Unit expectations for the former admission rejection now
require successful eval or the actual runtime ReferenceError.

The final-source release pin `host60` has SHA-256
`39a35c0d3786b91b699d0ed595f372fa0ea6728031d8cc0b05c120a0177417a6`.
It is byte-identical to `host59`; the last source adjustment only lowercases
comments. All 767 units and both 100-case focused runs pass on `host60`.
The original eval/with fixture passes unchanged, normally, under forced GC,
and on explicit AST. The 650-case canonical AST slice passes completely.
The final MIR slice passes all 3,764 cases with no unresolved records: the
previous 3,114 cases plus all 650 eval/with cases. The 2,941 production inputs
and 9,246 verification inputs remain unchanged. Exact commands, corpus and
metadata hashes, binary identity and outcomes are retained under the pin.
The exception catalog has zero violations; the callable census still reports
the same 38 pre-existing cases as HEAD. The selected host, Script and small-
fixture results are recorded above. Full baseline closure, AUTO publication,
independent agents, module eval import cells, library coverage, the Unicode
version decision and performance acceptance remain open. Timing stays deferred.
