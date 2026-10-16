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
3. **Host API integration.** DOM/Web, Intl, CommonJS, process events and object
   timer handles need shared service adapters. The full fixture failures are
   retained; no legacy retry or test-harness exclusion hides these gaps.
4. **Language/environment gaps.** Direct eval inside MIR `with` and independent
   agents remain open. RegExp legacy match reads and the wide-subject replacement
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
