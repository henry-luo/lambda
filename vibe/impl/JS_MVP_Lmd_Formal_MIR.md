# JS MVP: activate the formal MIR engine

**Date:** 2026-10-17. **Status:** functional migration complete; benchmark timing deferred.
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
   module ownership, syntax/environment/exotic cases, and Unicode 17
   (USER decision, 2026-10-16). The last intermediate whole MVP audit is not a score for
   this activation.
5. Require shared Lambda and Test262 gates and precise-GC checks for functional
   migration closure. The user explicitly deferred benchmark timing; pinned
   release MIR comparisons and any performance acceptance remain separate,
   open work until timing resumes.

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
| `host77`, release MVP units and focused normal/forced GC | 772/772; 105/105 each | Includes five module-eval lifetime, import and environment checks |
| `host77`, isolated Script integration and AUTO forced GC | 208/208; 10/10 | Existing AUTO publication and loop-handoff checks remain green |
| `host74`, release MVP units and focused normal/forced GC | 767/767; 100/100 each | AUTO adapter leaves the existing formal MIR units green |
| `host74`, isolated Script integration | 208/208 pass | Seven original AUTO failures repaired; three new ownership/identity/handoff cases |
| `host74`, AUTO normal and forced/poisoned GC | 10/10 each | Seven unchanged publication cases, three new cases; corrected one unrooted C++ result |
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

### Integration scope and qualification boundary

| Entry | Current implementation |
|---|---|
| Ordinary JavaScript CLI/public Script | MVP MIR |
| JavaScript ES modules | MVP MIR, or explicitly selected AST |
| Formal MIR Test262 source/harness batches | MVP body; shared native assertion helpers or retained AST harness |
| `eval` / Function-family dynamic source | Retained AST, as required by D8.1.3v25 |
| Document/preamble compiled artifacts | MVP MIR; shared code images with fresh realm metadata |
| Explicit AUTO function promotion and loop handoff | MVP satellite bodies through the native callable ABI |
| TypeScript-specific lowering | Existing path; separate from this JS activation |

Module/child-realm ownership, interleaved `with`/eval records, host properties,
CommonJS, timers, process events and cooperative agents are implemented and
covered by the later evidence below. Unicode 17 is the selected target and its
generated tables are checked for reproducibility (**S17.7.1, D8.1.3v25**).

Native worker threads and browser-wide DOM/Web compatibility remain separate
qualification scopes (**D6.3.1-D6.3.2**). Fresh document realms still reconstruct
frontend metadata before exact-image code reuse. Removing that frontend cost,
open-realm specialization and benchmark performance acceptance remain deferred;
none is represented by a functional-suite pass. The final shared Lambda,
standard Test262 and canonical formal MIR gates are recorded at the end.

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


### AUTO function publication and loop handoff

The existing definition-owned `FnPromotionCell` now publishes an MVP body
through the native JsBody ABI. The original callable remains `self`, so named
recursion, construction, descriptors and equality retain their identity.
Definition roots retain compiled bodies between closure instances; Runtime
Script retirement owns their metadata and code (D5.3, D6.2.2v2, D8.1.3v25).

All satellites in an AUTO runtime reuse MVP's existing shared-realm unit
mechanism. Their library borrows the native realm's intrinsic prototypes and
preserves native callable values throughout property reads, arrays and return
values. This avoids duplicate realm-capsule registration and adapter identity
leaks. Global variables continue through the original slab and global-property
mirror; lexical cells remain in the original slab. Native property operations
resolve the owning MVP program from existing callable/nominal metadata, so
escaped objects and implicit errors retain their realm without a Container
flag (D3.4.7, D5.3/D5.4, D6.2.2v2).

Helpers announced before coding: `mvp_lmd_compile_function_satellite`,
`mvp_lmd_call_function_satellite`, `mvp_lmd_compile_loop_satellite`,
`js_interp_mvp_body`, `mvp_lmd_retain_execution`,
`mvp_lmd_property_program`, and `mvp_lmd_dynamic_call_native`. The retention
and native-call helpers extract existing code; loop parsing and publication
reuse the existing frontend, live-in planner and invoke kernel. Async/generator,
captured, module and eval-sensitive definitions remain AST-pinned by admission.

Release pin `host74`, SHA-256
`a2db545b6c81f2b222a9e520fce8eee9b3481d351144e6d3478ccf4fa6947d84`,
passes all 767 MVP units, both 100-case focused runs, all 208 isolated Script
cases, and all ten AUTO checks normally and with forced/poisoned GC. One
original C++ test now roots its managed result across lookup-key allocation;
its source and expected JS behavior are unchanged. The 2,941 production
fingerprints and 9,246 verification-input snapshot are recorded under the pin.
The complete 40,261-case formal MIR recount was interrupted before completion. Broader
migration and performance acceptance remain open; timing stays deferred.


### Module direct eval over live source bindings

The module environment projects the existing source record into the retained
interpreter, with no copied import values or second export authority. New helper
announced before coding: `js_module_environment_binding`; it selects the existing
resolved import or local slab and reuses `js_module_binding_get` and Lambda's
owned module-slot writer (D5.3, D7.2.1, D8.1.3v25). Imports remain readonly,
local writes remain live through exports, and reads preserve the target TDZ.

MIR retains one precisely traced environment between module instantiation and
evaluation. Hoisted functions and eval-created closures retain that same record
across host calls, awaits, generators and GC. Module vars stay off the global
object; module eval receives undefined `this`. Ordinary units without direct
eval allocate no additional environment (D5.3/D5.4, D8.1.3v25).

Release pin `host77`, SHA-256
`73a4a0cee65b07a8ab29a578ca4e6fa51e80c53add9900c4796271ad0a394097`,
passes all 772 MVP units, both 105-case focused runs, all 208 isolated Script
cases and all ten AUTO checks under forced/poisoned GC. The old unit expecting
module-eval admission failure now requires successful execution of its unchanged
source. Original fixtures, Test262 metadata and runner are unchanged. All 2,941
production hashes and the 9,246 verification-input snapshot are retained with
the pin. The exception census has zero violations; the callable census reports
the same 38 existing cases as HEAD. Its selected canonical integration audit
subsequently passed all 3,764 cases with no unresolved results;
these focused results do not close the broader migration or deferred timing gate.


### Primitive and Lambda numeric ingress

Open-program property reads now resolve Boolean and BigInt values through
their realm prototypes even when a large call site cannot use the compact
callee path. Missing properties still produce undefined; detached methods
retain their existing receiver validation (D8.1.3v25).

The existing dynamic callable adapter is renamed
`mvp_lmd_dynamic_import_value` and also reuses
`js_native_bigint_to_bigint` for native i64/u64 values. The same ingress serves
native call results, packed Lambda field reads, descriptors, projections,
module exports, Promise callbacks and resumed await values. Packed foreign
maps are admitted only for reads; existing mutation admission remains the
authority. MVP class resolution is reused to keep ordinary class accesses
off this foreign path (S4.9.1, D3.4.7, D5.3/D5.4, D8.1.3v25).

Native JSON parsing now uses the existing strict parser's JS Number option,
including wide integer rounding and negative zero. JSON text does not cross
the Lambda numeric ABI. The numeric fixture now imports an actual Lambda i64
export for its FFI check and separately checks JSON Number behavior. The CSS
namespace uses the existing Object-prototype installer, making its existing
tag visible to String conversion; its golden reflects that namespace
contract. Both corrected fixture contracts execute through native AST and
formal MIR. These are explicit fixture corrections, not Test262 exclusions
([JSON.parse](https://tc39.es/ecma262/multipage/structured-data.html#sec-json.parse),
[Web IDL namespace objects](https://webidl.spec.whatwg.org/#namespace-object)).

Release pin `host82`, SHA-256
`9d7aa5fa4accfc330b4936471e82f1cf954804f1d00a5e0b96314a2fa5eb303d`,
passes all 774 MVP units, both 107-case focused runs, all 209 isolated Script
cases and 11 AUTO/interop checks under forced/poisoned GC. The earlier whole Test262 and full fixture
audits were interrupted and are not complete scores. A standalone MIR probe
reproduces the Acorn/Joi ARM64 address-optimization failure: validation uses
log2(size) instead of byte size, admitting unencodable i64 offsets 9 and 42.
The vendor repair was subsequently approved under AGENTS.md rule 16 and is
recorded in `patches/mir-aarch64-memory-validation.patch`; the standalone probe
passes after the fix, and `make verify-mir-patches` confirms exact upstream
plus patch equivalence. The ten-lane load/store regression test also covers
packed offsets and the immediate-range boundary. Independent
agents, the Unicode version decision, aggregate baseline closure and deferred
performance acceptance remain open.


### Shared-buffer carrier ownership

SharedArrayBuffer cloning now creates a distinct destination-realm carrier
over the same stable `ByteBufferHandle`. A JS-owned native allocation retains
that handle through Lambda's existing `RefCount`; ordinary buffer data access
and typed-array loops keep their existing layout and resolver. Growth updates
the one shared handle, including length-tracking TypedArray and DataView
clones. Fixed-length views retain their original offset and extent
(D2.4.1, D5.3/D5.4, D8.1.3v25).

Helpers announced before coding: `js_arraybuffer_allocate_handle`,
`js_arraybuffer_retain`, and `js_sharedarraybuffer_clone_with_ops`. All native
handle allocation paths reuse the first helper. Existing wrap operations
retain a borrowed handle; each GC carrier releases its own reference, so GC
must not deduplicate releases by handle address. Internal clone construction
selects intrinsic prototypes through the existing buffer adapters, without
reading guest constructor properties. The shared clone traversal reuses the
existing view constructors. No Container flag or second backing-store model
is added.

Release pin `host84`, SHA-256
`d312affe179ddac25b2095c4fcf7fd1304e1919125ea067aa598860ad1a3dc93`,
passes all 775 units, 108/108 focused checks, 210/210 isolated Script cases
and 13 AUTO/interop/clone checks under forced/poisoned GC. All three MIR
optimizer tests pass, including
80 packed/aligned load-store combinations in the new ARM64 regression. Acorn,
Moment (131 assertions) and Pretext (87 assertions) complete on formal MIR
with exact original golden output after the approved vendor repair. The
108-case forced-GC run also passes. Joi subsequently matches its original
golden output with 92/92 assertions passing. A fresh 40,261-case formal recount
is still running on that pin. Independent agent execution still needs runtime owners, queues,
blocking wait scheduling and teardown. Benchmark timing
remains deferred.

The stripped Test262 cache still contained 459 generated Unicode 16 property
test bodies. After the Unicode 17 decision, those files were regenerated from
the pinned canonical corpus through the existing comment stripper; the sibling
`lambda-test` checkout was clean in that scope. The refresh plan, original and
generated copies, corpus commit and before/after hashes are retained under
`temp/js_formal_mir_20261016/unicode17-cache-refresh`. Harness, metadata,
baseline population and original canonical sources are unchanged. The standard
baseline is running against that matching cache alongside the separate full
formal-MIR canonical recount.


### Canonical module allocation across borrowed Runtime controllers

A child realm's Lambda import exposed an allocator collision: its private
Runtime counter assigned module IDs already used by the shared EvalContext's
JS module slabs. The unchanged import path reported `sealed layout changed`
and left the import completion pending. The physical table and allocation
authority must have the same owner (D5.4.3, D7.2.1, D8.5.1v2).

New helper announced before coding: `runtime_module_state_owner`. The
EvalContext retains its canonical allocator Runtime once, while a borrowed
controller can still select its own module registry and execution metadata.
Script registration, direct slab reservation and generation retirement reuse
that existing owner's counter. No second counter or module table is added;
the new owner pointer is at the Context tail, preserving established hot
offsets (D8.1.3v10).

Release pin `host85`, SHA-256
`a6b4c87d88e265feee707f3376dee55f83c66be9e5f15339df250424ceb1d87f`,
now completes the original parent/two-child Lambda import reproducer with
values 42 in all three realms and the correct Array constructor identities.
A new integration unit also checks top-level await and independent exported
local cells. The pin passes 776/776 units and both 109-case focused runs, including forced/poisoned GC. The whole formal
recount remains pinned to `host84`; it must not be reported as a `host85`
whole-suite score. Legacy JS emission sidecars and size budgets still refer
to LJS function/ABI shapes and need migration review before aggregate baseline
closure. Independent agents, remaining library stalls and the Unicode target
remain open; benchmark timing is deferred.

### Bounded emission for oversized straight-line functions

The BN library exposed optimizer pressure from generic property/arithmetic
expansion, rather than a proven optimizer cycle. Existing compact dispatch now
also applies after the existing instruction budget in oversized straight-line
functions. Lexical loops and known numeric operations retain their established
emission. Generic arithmetic reuses `mvp_lmd_dynamic_binary` and its stable scalar
home; coercion order, BigInt errors and possible string results remain observable
(**D6.2.2v2, D5.3/D5.4, D8.1.3v25**).

Pin `host87` passes 778/778 units and both 110-case focused runs (normal and
forced/poisoned GC). BN matches its original golden; its largest MIR function
shrinks from about 314,000 to 108,000 instructions. Ramda (94 assertions) and
Lodash match their original goldens on the preceding dispatch pin `host86`.
These are functional/structural results, not benchmark timing evidence.

### Independent agent ownership and scheduling

Pin `host88` passes 778/778 units, both 110-case focused runs and 211/211 isolated
Script tests. Its new agent-state ownership test passes normally and with forced,
poisoned GC: globals, queues, timer callbacks and teardown stay with their owner.
The new `js_runtime_state_create_agent` / `js_runtime_state_destroy_agent` reuse
realm initialization, queues and exact roots while retaining the shared
EvalContext (**D5.4, D6.3.1**). Timer capture now retains the execution-state
selector; activation ambient state also preserves the borrowed Runtime controller.

The next candidate replaces the legacy synchronous agent simulation with strong
Lambda `Activation`s, independent AST source realms and FIFO reports. Dynamic
agent strings use the retained interpreter (**D8.1.3v25**). Broadcast clones the
existing SAB carrier and retains its shared handle; blocking waits park, and
notifications scan real waiters at the same backing handle and byte offset.
No report rewriting or substituted atomic values are retained. Announced helpers
are `js_agent_checkpoint`, `js_agent_pump`, `js_agent_state_at`,
`js_agent_group_shutdown`, and private group/member lifecycle routines. The
existing agent factory is shared by both frontends; `MvpLmdRealmScope` gains a
native-realm option. Functional, forced-GC and canonical agent qualification are
pending. This cooperative single-context implementation does not claim the
worker-thread isolation audit in **D6.3.2** is complete.


The first real-agent pin (`host89`) passes three integration checks normally and
under forced/poisoned GC, and the rewritten local agent-state fixture matches
its existing golden. One unit still asserted that agent access was unsupported;
it now checks the supported object's identity and callable method instead. The
canonical slice exposed a separate standalone-owner gap. The existing
`mvp_lmd_prepare_module_realm` factory is now shared with the lazy agent getter;
retained native Scripts join the program's existing post-heap metadata owner
(**D5.4, D8.1.3v25**), rather than being freed while unreachable GC functions can
still refer to their AST.

Release pin `host90`, SHA-256
`1ec9873caa8ffb9b648289399b7848529b60fb178a77ece18de37322f954f66d`,
passes **79/79 canonical agent cases** and **79/79 additional strict variants**
through the unchanged formal batch harness, with no unresolved cases. It also
passes **782/782 units** and **114/114 focused checks** normally; the integrated
forced-GC run is in progress. The final cleanup pin removes obsolete virtual
clock fields and binds the canonical slab allocator before the first agent.
A fresh full JS fixture audit has started on that pin. The full formal recount
remains independently pinned to `host84`; these selected results must not be
added to that pin's score.


#### Agent cleanup pin and complete fixture screen

Pin `host91` (`86f5c4472563176abf36043bf69bdaa534072047f8d6cc5bc20ef6f287a9b144`)
passes 782/782 MVP units, both 114-case focused runs, 211/211 isolated Script
cases and 14 AUTO/interop/clone/agent checks under forced/poisoned GC. The
complete 509-case fixture screen passes 508, with no timeouts. Its only failure
is `underscore_lib`: 113 of 114 assertions pass, and a minimal local alias of
`isFinite` reproduces the TypeError. `kind()` incorrectly assigned K_UNDEFINED
to an unresolved global name, although the runtime reads its live host/global
property. The next candidate uses K_ANY for this general unresolved-name case;
no builtin-name special case or new helper is added (**D8.2.4-D8.2.6,
D8.1.3v25**). Direct replay on `host92` restores Underscore's original 114/114
golden and passes the local/nested alias reproducer; integrated qualification
is in progress.

The pre-agent `host84` whole formal recount completed at 39,817/40,261 passes,
439 failures and five timeouts; 95 outcomes remain unresolved, including
missing child results. Replaying those 444 nonpasses on `host91` yields 121
passes, 310 failures and 13 timeouts (49 unresolved). These are distinct pins
and selections, not a combined current score. A new whole recount remains
running on `host91`. Remaining failures include generated Unicode RegExp
cases, legacy constructor/environment cases and inferred function names.

All 31 legacy JS MIR sidecars have been ported to formal MVP scope/ABI
contracts and pass (32/32 including corpus discovery) on `host91`. Review
preserves rooted argument spans, callable ABI/context/unit guards, source
throw versus catch conversion, packed-field and descriptor fallback,
BigInt result joins and scalar-home ownership (**D5.3/D5.4, D6.2.2v2,
D8.4.3v2**). Changed contracts are explicit: open Script globals require
mutable lookup; local hoisted functions use shared closure cells; global
declaration instantiation precedes the emitted entry; bounded loop counters
can use the integer lane; for-in yields String keys; nonescaping local
literals may fold. The size ratchet and aggregate baseline remain open.

### Reference coercion, names and observable iterator families

The agent integration's ambient restoration reverted a module controller
installed lazily by same-owner async work. Restoration now applies only when
switching execution owners (**D5.4, D6.3.1**). The paired diagnostic fails on
`host94` and passes on `host95`; all 101 affected dynamic-import cases and the
selected top-level-await case pass on `host95`. The complete `host91` recount
was 39,750/40,261, with 502 failures and nine timeouts (142 unresolved); selected
repairs are not added to that whole score.

Subsequent candidates repair nullish computed-reference ordering, deferred
simple-assignment keys, lazy configurable function names, object-to-primitive
selection before BigInt arithmetic, primitive writes with their original
receiver, strict unresolved references captured before RHS effects, constructor
spread, Script arrow receivers, primitive subclass nominal ancestry and
synchronous generator prototype selection after parameter initialization
(**D3.4.7, D5.3/D5.4, D6.2.2v2, D8.1.3v25**). The new private compiler helper
`primitive_operand` was announced before coding and extracts existing
conversion emission. Function-name definition uses the owning program's
canonical pool; a shape's `StrView` is never a runtime String.

Optional observable iterator metadata now distinguishes Map, Set and String
families, while the existing sequence family represents Array iterators.
Existing descriptor setters install nonenumerable iterator aliases and proper
tags. String iteration reuses the same carrier and stepping logic after
RequireObjectCoercible/ToString. No normal-container flag or replacement
storage is introduced (**D3.4, D5.3**).

Release `host102` (`4e5b3f7e99ab20aedfd27e9a325f03386249a40013ea8d0dafd58648f29ade40`)
passes **799/799 units**, **131/131 focused checks** normally and under
forced/poisoned GC, and **164/172 selected canonical cases**, with no unresolved
selected outcomes. One older unit expected computed key conversion before the
RHS; its trace now follows the unchanged checked-in Test262 assignment cases.
Those eight selected failures are repaired in the subsequent checkpoint below.
Script/fixture requalification, full recount and aggregate gates remain open.
Benchmark timing remains deferred.

### Generated declarations, Unicode 17 and emission qualification

The selected canonical screen passes **172/172** on `host106`. Template raw
and cooked values normalize source CR/CRLF in the shared frontend; function
hoisting replaces parameter bindings while preserving the implicit `arguments`
binding against Annex B block declarations. Computed anonymous class names
precede static initialization. Primitive deletion boxes only primitive
alternatives and retains Array, Function and typed-view carriers in the kind
proof (**D8.1.3v25, D8.2.4-D8.2.6**).

Unused private names use existing owned module slots instead of allocating a
closure cell for every spelling. Actual captures retain the shared Lambda
closure-cell ABI (**D6.2.2v2, D5.4**). Oversized passive module declarations
use one immutable initialization plan through the new helper
`mvp_lmd_private_names_initialize`, which calls the existing private-name
operation and owned-slot store. Private class metadata uses Lambda `array()`
and append-slot reservation; guest sparse-array policy must not turn an
internal dense slab into a carrier with a null item buffer (**D5.3**). The
original 8,326-name canonical class now completes within the unchanged
10-second deadline. A 9,000-name unit covers the allocation boundary.

**User decision (2026-10-16): upgrade JS Unicode support to 17 during the
migration.** Properties and string properties regenerate from the pinned
canonical Test262 Unicode 17 data, with source hashes. The new generator
`utils/generate_js_unicode_properties.py` replaces the previously unaudited
range-generation step; lookup functions stay in the shared runtime. Identifier
validation reads those same ID_Start/ID_Continue tables, removing the manual
version delta. Shared Lambda simple folding regenerates from official
[Unicode 17 CaseFolding.txt](https://www.unicode.org/Public/17.0.0/ucd/CaseFolding.txt)
(SHA-256 `ff8d8fefbf123574205085d6714c36149eb946d717a0c585c27f0f4ef58c4183`),
preserving **S17.7.1**. The shared case conversion already supplies the new
mappings. Annex B class ranges with a class-escape endpoint now form a union
instead of interpreting the escape letter as a range endpoint. No vendor
dependency or Test262 runner changes are needed.

Release `host107` passes **805/805 units**, **137/137 focused checks** normally
and with forced/poisoned GC. Its RegExp residual screen passes 144/150, with
six timeouts; all six pass in the serial residual replay on `host108`.
Release `host108`
(`0e4bc64214ee94d19f7619c0d471a7acb1550ac6d2ffcefc8b9b85b07f7b396f`)
passes **134/134** remaining `host91` nonpasses not covered by the preceding
successful selected screens. These separate screens do not constitute a new
whole-corpus score. Fresh integration and whole-corpus qualification are
running on `host108`.

All 31 JS emission sidecars pass (32 checks including discovery). Seven JS
size ratchets now select formal `mvp_lmd_program`/`mvp_lmd_fN` scopes, with
zero slack and reviewed precise frames (**D8.6.1, D5.3/D5.4**). The ERROR-tag
counter recognizes direct `beq`/`bne` comparisons as well as materialized `eq`,
preserving the gate across the emitter change (**D8.4.3v2**). Reviewed release
module instruction counts are:

| Probe | Retired LJS | Formal MIR | Main reason for the changed contract |
|---|---:|---:|---|
| numeric inference | 242 | 696 | mutable Script callee lookup and Item entry guards |
| hoisted closure | 507 | 1,016 | captured closure cells and owned module/activation slots |
| array methods | 3,727 | 16,622 | complete observable method/callback paths and precise homes |
| generators | 5,880 | 11,855 | guarded callable and suspension ownership paths |
| side-stack GC | 8,794 | 22,551 | formal public entry, precise roots and scalar lifetimes |
| exact collection | 21,474 | 15,762 | shared nominal class metadata and uncapped member plans |

The catch probe pins five physical tag comparisons across generic admission
and catch conversion. Lambda budgets remain unchanged. All seven JS ratchets
also pass with `host108` Darwin debug; other platforms are not measured.

### Large constant arrays and optimization-contract migration

The shared RegExp engine's resource failure crossed into a generic MVP
RangeError and lost its message. Both match and replacement paths now pass the
existing diagnostic through `mvp_lmd_fail`'s existing detail parameter. The
original router resource test passes unchanged (**D8.1.3v25, D8.4.3v2**).

Large passive Array literals previously emitted one initialization per element;
the 10,001-element optimization fixture produced a 99,939-line MIR artifact.
At the existing compact-emission threshold, primitive literals and elisions
now use an immutable frontend Item table and Lambda's existing
`array_copy_owned_items`. Allocation/reservation remains dense and fresh,
wide scalars copy into destination-owned homes, and hole sentinels retain
absence. Effectful elements, spread and BigInt allocation keep ordered
evaluation. No new runtime helper is added (**D5.3/D5.4, D8.2.4-D8.2.6**).
The same fixture emits a 1,575-line artifact on `host110`; a 7,000-element unit
checks fresh identity, holes, tiny floats, packed integer identity and GC lifetime.

The old optimization suite asserted retired LJS helper names, native peer
signatures and runtime trace hooks. Its 84 MIR fixtures now use the existing
emission checker and reviewed source-scope sidecars under `test/mir/js_opt`;
the trace-schema fixture has a sidecar too. The checks pin 1,323 arithmetic,
coercion, field, typed-lane, ownership and fallback counts across the formal
Item entries. JS operations, output assertions and the trace-on/off semantic
and MIR differential remain covered; retained AST kernel tests retain their
runtime counters (**D8.1.3v25, D8.2.4-D8.2.6, D8.6.1**).

Two obsolete expectations are corrected against ECMAScript:
[MakeConstructor](https://tc39.es/ecma262/multipage/ordinary-and-exotic-objects-behaviours.html#sec-makeconstructor)
creates a function's `prototype` before later user properties; numeric `-0`
becomes the property key `"0"` through
[ToPropertyKey](https://tc39.es/ecma262/multipage/abstract-operations.html#sec-topropertykey),
so the typed-array write changes element zero. The normalizer now retains
String/Symbol tags while removing relocation addresses, and searches for
stack-guard addresses within each line. Its previous suffix search was
quadratic in artifact size and stalled large-fixture verification.

Release `host110`
(`70275be9b03a1dfaaa68a66944e765b25aaac6f40583eed5ef8f46f6c6ad0b49`)
passes **808/808 units** and **140/140 migration checks** normally and under
forced/poisoned GC. The ported optimization suite passes **93/93** on its debug
companion (functional/emission qualification only). `host108` completes
**211/211 isolated Script cases**, **14/14 GC-stressed AUTO/interop/agent checks**
and **509/509 fixtures**, with no fixture timeouts. Its partially completed
whole recount was explicitly superseded after the two production fixes;
it is not an aggregate result. The complete 40,261-case formal recount, fresh
fixture replay and aggregate Lambda gate are running on `host110`. Timing
remains deferred.

### Retained interpreter integration and final recount

The aggregate gate exposed three integration defects after activation:

- The native prototype-reservation assertion observed retired LJS storage.
  Its original operation now runs in both tiers: the native suffix counter is
  zero for formal MIR and one for AST, with identical output and a formal MIR
  sidecar (**D8.1.3v25, D8.6.1**).
- String timers used indirect eval semantics and discarded top-level lexical
  declarations. They now execute classic Script source in the retained AST
  interpreter. Callback conversion precedes delay coercion; receiver arguments,
  source and converted scalars have precise homes across either conversion
  (**D5.3/D5.4, D8.1.3v25**). Dynamic classic scripts explicitly remain in AST
  even at the minimum AUTO function/loop thresholds. The original timer golden
  passes in both AUTO and AST. `host114` passes all **212 Script tests** and
  **15 forced/poisoned-GC interpreter/interop/agent checks**.
- The PDF checker selected Python 3.14 without Pillow. The installed Python
  3.13/Pillow 12.1.0 passes the unchanged two-tier check; the final aggregate
  uses that interpreter through a generic PATH prefix.

The generated test-host link rule also incorrectly treated the static runtime
archive as a module and linked every host to `node-core`. Debug hosts could
then bind private C++ module symbols that disappear in release modules. The
generator now uses the static host providers and retains the host export
allowlist needed by runtime-loaded modules (**D7.3.4-D7.3.6**). The Script and
Lambda-error test targets also drop their explicit redundant module link in
`build_lambda_config.json`; CLI/library target configurations are unchanged.
The generator selftest covers both static runtime entry configurations and
passes; generated Lua is not edited manually.

The canonical recount exposed repeated parsing of lone RegExp eval literals
through the MVP interpreter adapter. The existing `js_eval_source_shortcut`
now accepts the shared host operations: it constructs a fresh realm-local
RegExp through the existing constructor, with rooted source/pattern/flags.
Line terminators and other source shapes still receive parser validation;
other host early errors remain in their realm-local parser. Both direct and
indirect eval reuse this path without compiling dynamic source (**D8.1.3v25,
D5.3/D5.4**). No new production helper is introduced. Release `host115`
(`138f6d9fd20aed882137b70ee2afa914a42be9d37f6155ab810bd856c9a627c4`)
passes **198/198** original canonical RegExp literal/Annex B cases with the
unchanged deadline, including the 65,536-iteration eval cases. Its fresh
whole formal MIR recount is in progress. This pin also passes **809/809 MVP
units**, **141/141 migration checks** normally and under forced/poisoned GC,
and **212/212 Script cases** plus **15/15 additional GC checks**.

The preceding standard hybrid Test262 baseline on `host110` completed with
**40,251 fully passing cases, 10 isolated-retry-only slow classifications,
zero failed cases and zero regressions**. These categories are retained rather
than presented as a wholly clean formal MIR recount. The canonical recount
uses unstripped Unicode 17 source. The sibling generated cache was refreshed
for 459 Unicode files under the user's Unicode 17 decision; backups and source
hashes are recorded separately. Benchmark timing remains deferred.

`host115` also completes all **509 formal MIR fixtures**: 508 in the first
sweep and Joi on an unchanged-binary/harness/deadline replay. Fresh static
test hosts pass **212/212 Script cases** and **266/266 Lambda-error/PDF cases**;
their load dependencies contain no `node-core`. The aggregate activation gate
passes JS optimization **93/93**, Lambda optimization **54/54**, Lambda MIR
emission **249/249**, JS MIR emission **32/32**, size ratchets **20/20**, shared
GC stress **313/313**, and default JS fixtures **509/509**.

The complete Lambda aggregate records **5,564/5,565**, with only the LaTeX
phase-3 corpus reaching its existing 60-second timeout under load. The exact
same debug executable and unchanged golden harness pass that case alone in
41.860 seconds. The failed aggregate and timeout-cleanup crash are preserved
in `host115/shared-baseline`; this replay is not a green aggregate command.
After the Test262 recount, rerun the actual aggregate with its existing
`AST_TUNE_BATCH_WORKERS` setting to bound contention, retaining all cases and
deadlines. Final qualification remains open until that aggregate passes.

### Native assertion path and complete canonical coverage

The actual standard debug runner selects native assertion jobs that the older
private release runner executed with JavaScript assertion source. Its formal
MIR path omitted that preamble but had no real `assert` binding. An explicit
`MvpLmdOptions.native_harness` option now reuses
`js_test262_native_harness_install` and the existing host-global import
membrane, in an open Script realm. CLI selection is limited to inline
harnessless Test262 jobs; file-backed batch profiles retain ordinary globals.
Helpers are installed before Script declarations, then the existing
`mvp_lmd_prepare_script_bindings` performs declaration checks/publication, so
`var assert;` preserves the installed callable (**D8.1.3v25**).

Native assertion callbacks also exposed implicit MVP errors without a resting
JavaScript payload. Reading their constructor through the native carrier path
could treat an MVP callable as a legacy `JsFunction`. The existing
`MvpLmdRealmScope::complete` accepts an error-payload option at the shared
call/construct adapter. Materialization occurs only for failed returns, in the
callee's realm; ordinary call results and Lambda interop retain their existing
flow (**D6.2.2v2, D8.4.3v2, D5.3/D5.4**). An attempted broad native-realm reset
was rejected after it regressed Lambda integer interop, before qualification.

The earlier whole audit exposed eleven numeric-operator coercion-order failures
and one prototype-setter function-name failure. The shared scalar bridge
completes left `ToNumeric` before right object coercion. MIR performs the
throwing Symbol check at that same point; other primitive Number conversions
are nonthrowing. Addition and relational comparison retain their distinct
primitive-conversion order. The new shared AST predicate
`js_ast_property_is_proto_setter` is announced before addition and reused by
both front ends: anonymous functions/classes used as `__proto__` setter values
do not receive property-name inference (**D8.1.3v25**).

Release `host120`
(`4bed3d327f9b29af01b67b32ad37ee640d7c0fcb031ea56ae87be7981c203bc5`)
passes the complete **40,261/40,261** canonical formal MIR audit with the current
debug runner: **16,671 native-harness + 23,590 JavaScript-harness cases**, zero
failures, timeouts, crashes or lost results. The original metadata, includes,
negative expectations and deadlines are retained; `test_js_test262_gtest` is
unchanged. The subsequent declaration-ordering correction on `host121`
(`f295b66eaa2743fdb2bc80feeb5ea278deaadd39b998d6d999a02d99997baec3`)
passes **812/812 units**, **144/144 migration checks** normally and with
forced/poisoned GC, plus **100/100 canonical native assertion cases**. The actual standard Test262 target passes with **40,250 fully passing cases,
11 isolated-retry-only slow classifications, zero failed cases and zero
regressions**. Its incomplete follow-up canonical audit is retained and
superseded by a complete audit of the final guard refinement below. Final
shared Lambda qualification remains required; benchmark timing is deferred.

### Final coercion guard size review

The shared aggregate exposed four JS MIR size increases from the new early
Symbol check. Primitive right operands cannot run coercion hooks, so the guard
is retained only when the right operand can be an object. Its Symbol test uses
the existing high-byte tag directly: the shared value contract statically
asserts that Symbol is outside inline-double space (**D2.1.3-D2.1.4, D2.2.1, D8.4.3v2**). This avoids
generic tag dispatch and a possible container load.

Six of the seven JS size probes retain their exact prior budgets. The generic
array-methods probe needs **23 instructions** at three multiplication sites:
5 in an inline reduction and 9 in each of two generic callbacks, including
existing error/root publication. The reviewed module ceiling is **16,645**
(previously 16,622), with zero added slack; Lambda budgets are unchanged.
Typed-number arithmetic and known primitive right operands omit this guard.
The instruction review is in `host123/ratchets/symbol-guard-review.json`.

Final optimized `host123`
(`4e1ee20beaacdfbf437b3c76497545b72b7e4d31ff6c73ec25f215726c965ce3`)
was frozen for integrated qualification. Its complete audit recorded
**40,241 passing and 20 failing cases**, with zero unresolved results. A fresh
complete audit of the corrected `host124` below uses
the unchanged current debug runner, all 40,261 original cases, original
metadata/includes/negative expectations and the existing deadline. This guard
refinement adds no production helper. Benchmark timing remains deferred.

### Complete native declaration initialization

The `host123` whole audit found twenty native-harness failures after declaration
publication was deferred until the shared helpers were installed. The existing
`mvp_lmd_prepare_script_bindings` incorrectly skipped declared variables marked
`implicit_write` by destructuring/capture analysis, and skipped all Annex B
companions. Those analysis facts do not remove source declarations.

The existing initializer now prepares every declared name. Annex B companions
reuse `mvp_lmd_dynamic_global`'s existing availability probe, skip unavailable
bindings and declare available companions as variables rather than hoisted
functions. Helper-preserving `var assert;`, uninitialized destructuring targets,
catch/block companions and later Script lexical collisions have focused native
coverage. This changes only declaration setup, adds no helper and does not alter
the MIR emission budgets (**D8.1.3v25, D8.4.3v2**).

Corrected optimized `host124`
(`75a7220a9726cb5f350d32250c8fc14854dd124a303198af98a1a4636c7ca16f`)
passes **20/20** original failed canonical cases, with unchanged metadata,
expectations and deadlines, plus **812/812 units**, **144/144 migration checks**
normally and with forced/poisoned GC, and **509/509 formal MIR fixtures** without
timeouts or retries. Its complete current-runner canonical formal MIR audit
passes **40,261/40,261**, zero failures, timeouts, crashes or unresolved results.
The actual quiet shared Lambda baseline passes **5,568/5,568**, including all
**1,593 Lambda cases**, **20/20 size ratchets**, **93/93 JS optimization** and
**313/313 shared GC stress** checks. The former LaTeX timeout completes in
33.703 seconds under its unchanged 60-second deadline. Final Script GC passes **15/15**. The actual standard Test262 target exits zero
with **40,247 fully passing cases + 14 isolated-retry-only slow classifications**,
zero failures/regressions and 2,652 unchanged exclusions. The
failed `host123` audit remains preserved; the focused replay is not added to
its aggregate score.

## Functional closeout (2026-10-17)

The formal engine is activated and qualified under **D8.1.3v25**. The exact
optimized `host124` executable is published as the shared `lambda.exe`; its
SHA-256 is `75a7220a9726cb5f350d32250c8fc14854dd124a303198af98a1a4636c7ca16f`.
Dynamic source remains in the retained AST interpreter. Unicode property,
identifier, emoji and simple-fold data use Unicode 17; the existing normalization
library also reports 17.0.0. Generated tables reproduce from their recorded
sources, and raw/escaped Unicode 17 identifiers pass MIR/interpreter checks
(**S17.7.1, D8.1.3v25**).

| Final current-build gate | Result |
|---|---|
| Complete canonical formal MIR audit | 40,261/40,261; zero failures, timeouts, crashes or unresolved results |
| Actual `make test-lambda-baseline` | 5,568/5,568; exit 0 |
| Actual `make test262-baseline` | 40,247 fully passing + 14 isolated-retry-only slow; zero failures/regressions; exit 0 |
| Release MVP units | 812/812 |
| Focused migration checks, normal / forced-poisoned GC | 144/144 each |
| Script forced-poisoned GC | 15/15 |
| Formal MIR fixture sweep | 509/509; no timeouts or retries |
| JS / Lambda optimization checks | 93/93; 54/54 |
| Lambda / JS MIR emission checks | 249/249; 32/32 |
| Exact size ratchets / shared GC stress | 20/20; 313/313 |

The standard baseline's 2,652 pre-existing exclusions are not counted as passes.
The canonical audit retains original bodies, metadata, includes, negative
expectations and deadlines. The formal Test262 GTest source is unchanged. The
former LaTeX corpus timeout passes within its original 60-second deadline in
the complete aggregate, rather than only on isolated replay.

All four Node DSOs were forcibly rebuilt against the current headers and their
build-local manifest metadata refreshed. Fresh static Script/Lambda-error test
hosts have no `node-core` load dependency (**D7.3.4-D7.3.6**). No Node baseline
was run. The approved MIR address-validation delta remains recorded under
`patches/` and reverse-checks cleanly (**D8.6.2-D8.6.3**).

During validation, concurrent commit `bd2a8ed93` removed only retired Bash/Ruby
grammars. The original pin's source snapshot and discrepancy are preserved.
None of the removed paths occurs in the active makefiles or 656 checked release
dependency files; all **2,918 remaining recorded production inputs** still match
exactly. That cleanup is retained; no active migration source changed.

Final evidence is under `temp/js_formal_mir_20261016/host124/`:
`final-qualification.json`, `acceptance-audit.json`, `full-formal-canonical/`,
`shared-baseline-final/`, `standard-test262-result.json`,
`unicode17-verification.json`, and `source-audit-resolution.json`.

The earlier native-harness failures and superseded audits remain separate.
Functional migration is complete. Benchmark timing/performance acceptance,
native worker threads, broader browser compatibility and other-platform
qualification retain the boundaries defined above; none is silently inferred
from these functional results (**D6.3.1-D6.3.2, D8.6.3**).
