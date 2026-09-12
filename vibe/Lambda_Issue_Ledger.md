# Lambda Core Runtime — Central Issue Ledger

> **Consolidated from the per-area "Known Issues & Future Improvements" sections of
> [`doc/dev/lambda/LR_01`–`LR_13`](../doc/dev/lambda/LR_00_Overview.md)**, plus
> verified items from the former sibling `vibe/Lambda_Issue*.md` ledgers (§14)
> and the retired Outstanding rollup's design gaps (§15).
>
> **This is the only active issue ledger in `vibe/`.** Fixed and obsolete records
> are archived in the sibling [fixed issue ledger](<Lambda_Issue_Ledger(fixed).md>).
> Every former sibling was reviewed on 2026-08-25 and retired to `vibe/impl/`;
> each keeps its detail and evidence, while its live residue is indexed here.
> Add new issues here, not to the fixed archive or a retired archive. This is
> the single working list for core-runtime defects, caps, and structural
> hazards. The `LR_*` documents remain the *design* record; this ledger is the
> *issue* record.
>
> **Audience:** engine developers. **Status:** working ledger (`vibe/`), not normative.
> Semantic and design rulings are cited by `S#` / `D#` per CLAUDE.md rule 17;
> where no formal ruling covers a point, the vibe ledger ID is given.

## Verification pass — 2026-08-24

Every live issue below was re-checked against the tree at `c568f0f93`. Three outcomes:

| Mark | Meaning |
|---|---|
| **OPEN** | Reproduced in current source; `file:line` anchors re-resolved. |
| **PARTIAL** | Some sub-claims fixed, a real residue remains. The residue is stated. |
| **RESOLVED** | Verified fixed or removed; moved to the [fixed issue ledger](<Lambda_Issue_Ledger(fixed).md>). |

### Second pass — 2026-08-25 (sibling ledgers)

`vibe/Lambda_Issue*.md` were reviewed and their claims re-tested. Outcome:

| Doc | Result |
|---|---|
| `Lambda_Issues_Outstanding.md` | **Reviewed in full and RETIRED 2026-08-25** → archived as `vibe/impl/Lambda_Issues_Outstanding (retired).md`. Its §3 was verified subsumed by §1–§13; §2/§4 were pointer indexes into docs that still own them; its OI design gaps and hygiene themes moved to §15. One item was genuinely missing from this ledger — LR_12 #8, now resolved and moved to [LR12-R8](<Lambda_Issue_Ledger(fixed).md#lr12-r8>). |
| `impl/Lambda_Issue_Type_Support (retired).md` | TS-1, TS-2, TS-7 verified **FIXED**; TS-5's dead-code half fixed; TS-9's C16 implementation has landed. TS-6, TS-8 confirmed open; TS-3, TS-4 open pending measurement → §14. |
| `Lambda_Issues8 (retired).md` | **All 28 entries triaged; 22 re-tested.** 17 fixed/closed, 9 open or partial (§14), 4 not re-tested (Radiant-retained, Structurizr fixtures, and an incremental-release build issue — each needs a fixture outside the core runtime). Earlier note: **Fixed:** unbraced scalar `if` in a block body; map literal after `if` (S16.4.1v2); multiline iterator + `where`; `list` as a for-binding (now a clear diagnostic); the double-quoted-key error cascade. **Ruled not a defect:** double-quoted map keys — the doc was wrong and is corrected. **Does not reproduce:** recursive params overwritten after descent. **Still open → §14:** dynamic map spread, element attribute spread, one-line Mark comprehensions, and the weak double-quoted-key diagnostic. |
| `Lambda_Issues5 (retired).md` | 7 entries re-tested. **Fixed:** postfix `^` in `let` (#4), chained comparisons (#5), string slicing (#11), `if`-expression value in a `pn` (#15), and §23's inline-`if` attribute value. **By design, not defects:** element-wise list `+` (#1), `let` reassignment rejected in a `pn` (#10). **Still open:** §23's attribute spread → §14. |
| `Lambda_Issues6 (retired).md` | 5 open entries re-tested. **Fixed:** bare map as an `if` branch (#31, via S16.4.1v2), the paren-comma branch form (#32), multi-line `++` (#33), and non-fatal parse errors (#35 — a malformed file now exits 1). **Not reproduced:** the MIR float-param inference failure (#34); it was "real only" and a synthetic reconstruction runs clean on both tiers. |
| `Lambda_Issues4_Lint (retired).md` | cppcheck re-run (2.17.1). E1 still stands and is now **invisible to the analyser** after the `malloc`→`mem_alloc` migration → §14. W3 (`alloca`) fixed where reported. Counts elsewhere are historical. |
| `vibe/impl/*(fixed).md` | Archives spot-checked. `Lambda_Issue_GC_Native_Rooting` genuinely resolved (its 107/244 figure is historical discovery data — a stale memory note quoting it as live was corrected). **`Lambda_Issues0 (fixed).md` is mislabelled**: #9 was deferred there and is now resolved in the fixed issue ledger; #15 and five more items remain without resolution. |
| `Lambda_Issues3 (retired).md` | A test-enhancement proposal, not a defect ledger — and **substantially implemented**: it reported `test/std/core/` missing with 19 tests in `test/std/`; there are now 157, of which 104 sit under `core/` across all four proposed subdirectories (target was 57). |

### Direct implementation pass — 2026-08-26

Seven reproduced implementation defects were fixed after root-cause review and
confirmation against the formal rules: LR01-1 (`S2.4.1v2`, `D3.4.1`), LR01-3
(`S1.8`), LR01-4 (`S2.4.2v4`), LR02-13 (`S12.1.4`, `S12.3.4`), LR05-4
(`S10.1.2`), LR12-2 (`S7.10.6`), and LR12-8 (`S9.1.1`, `S9.1.6`). They are
moved to the fixed issue ledger with the `-R` suffix. The
`make test-lambda-baseline` gate passes **3914/3914** after the changes.

### Numeric implementation pass — 2026-08-28

LR04-1 was re-verified against **S4.6.1** and resolved: decimal source
coefficients are parsed exactly, and exact `+`, `-`, and `*` results use a
local maximum-precision context and promote storage when needed. The mixed
float/decimal paths retain the existing shortest-round-trip conversion required
by **S4.7.1**. Focused decimal and numeric tests pass; the full Lambda baseline
passes **3978/3978**.

### Direct implementation pass — 2026-09-05

Six independently reproduced items were resolved without changing a language
ruling: LR02-18 (**S12.3.3v2**, **D2.6.7**), LR05-3 (**S6.2.2v3**), LR07-12,
LR08-8, LR10-1, and LR10-4. Focused JIT/T0 fixtures and the error regression
suite pass; the resolution records are in the fixed issue ledger.

Counts:

| Source doc | Area | Open | Partial | Resolved | Total |
|---|---|---:|---:|---:|---:|
| LR_01 | Compilation pipeline, CLI & REPL | 8 | 2 | 0 | 10 |
| LR_02 | Parsing & AST construction | 2 | 4 | 0 | 6 |
| LR_03 | Value & type model | 4 | 1 | 0 | 5 |
| LR_04 | Numbers, decimal & datetime | 6 | 0 | 0 | 6 |
| LR_05 | Strings, symbols & vectors | 2 | 1 | 0 | 3 |
| LR_06 | C transpiler (legacy C2MIR) | 0 | 0 | 0 | 0 |
| LR_07 | MIR Direct transpiler & JIT | 12 | 1 | 0 | 13 |
| LR_08 | Memory management & GC | 9 | 0 | 0 | 9 |
| LR_09 | Runtime builtins | 5 | 0 | 0 | 5 |
| LR_10 | Error handling | 1 | 0 | 0 | 1 |
| LR_11 | Mark data API | 7 | 0 | 0 | 7 |
| LR_12 | Procedural runtime | 6 | 0 | 0 | 6 |
| LR_13 | Schema validator | 7 | 0 | 0 | 7 |
| TS / Issues8 / Lint / Issues0 | Sibling vibe ledgers | 6 | 1 | 0 | 7 |
| **Live total** | | **75** | **10** | **0** | **85** |

The active ledger now contains 87 live records, with the 61 previously counted
resolved records moved to the archive. Duplicate/split records and
verification-only findings remain represented there for provenance.
Two original entries each split into a resolved half and a surviving residue —
LR_03 #4 (sentinels → LR03-R4 + LR03-5) and LR_05 #3 (two string orderings →
LR05-R2 + LR05-R3). And two defects were **found during verification** rather
than extracted: LR02-8 through LR02-10, each marked as such in place (a fourth, LR02-11, was fixed the same day and is now LR02-R6).

The 2026-08-28 ledger cleanup removed seven non-live records: five source-marker
observations and two duplicate index entries. The fixed `LR05-2` classification
is archived alongside `LR05-R2`; both records are retained because they document
separate sides of the original split.

A follow-up syntax pass on 2026-08-24 added LR02-8 and LR02-9 and corrected three
stale markers outside this ledger: the `S16.1–S16.6` row in
`doc/Lambda_Formal_Semantics.md` Appendix A (said "wholly unimplemented"; the
harness passes 123/123 on the C parser and 118/118 on Tree-sitter), the
`§7.16` heading in `vibe/Lambda_Design_Syntax.md` (said OPEN, body says adopted
and implemented), and SO12's "does not parse" premise. The one genuinely
outstanding S16 task — O4's user-facing doc sweep — is now quantified in
Design_Syntax §6: **60 of 172** `lambda` code blocks in the four user docs no
longer parse.

The largest single change since the docs were written: **the C2MIR backend was
deleted from the tree** (`lambda/transpile.cpp`, `transpile-call.cpp`,
`lambda-embed.h`, `jit_compile_to_mir` all gone; no build defines
`LAMBDA_C2MIR`; the `--c2mir` CLI flag is not parsed). All nine LR_06 issues are
therefore archived as obsolete, and every cross-doc "diverges from C2MIR"
framing (LR07-3, LR03-3) now reads as a plain MIR Direct gap rather than a
backend divergence.
This is consistent with CLAUDE.md rule 14.

---


## 1. Compilation pipeline, CLI & REPL (LR_01)

<a id="lr01-2"></a>**LR01-2 · `serve` is a stub · OPEN**
The subcommand exists but does nothing: `// TODO: Phase 5 — instantiate Server,
configure, and run` (`lambda/main.cpp:3818`).

<a id="lr01-5"></a>**LR01-5 · Profiling has fixed caps · PARTIAL**
`PROFILE_MAX_SCRIPTS` 64 (`runner.cpp:213`) and `PROFILE_PATH_MAX` 512 (`:214`)
still silently drop rows and truncate paths (`:281`, `:291`).
*Residue only:* `PROFILE_MAX_IMPORT_LEVELS` is gone along with the parallel
import-level batching (see [LR01-R1](<Lambda_Issue_Ledger(fixed).md#lr01-r1>)).

<a id="lr01-6"></a>**LR01-6 · Fixed and non-reentrant static buffers · OPEN**
Module BSS name `char buf[256]` (`runner.cpp:565`); REPL synthetic path `char
script_path[64]` (`main.cpp:903`); the JS CLI thread stack is a 256 MB
`JS_CLI_STACK_SIZE` allocated per run (`main.cpp:264`, applied `:316`); and
non-reentrant `static char mir_error_msg[256]` (`main.cpp:1560`).

<a id="lr01-7"></a>**LR01-7 · Stateless REPL re-execution is O(n²) · OPEN**
The whole `repl_history` StrBuf (`main.cpp:785`) is re-transpiled and re-run
every turn, with error rollback implemented as a raw byte-truncate
(`main.cpp:882`–`893`). Any non-idempotent side effect repeats each turn.


<a id="lr01-9"></a>**LR01-9 · Namespace export gaps (pub vars) · OPEN**
`module_build_lambda_namespace` still skips **pub vars** entirely —
`// This will be addressed when we add live binding support.`
(`lambda/runtime/module_registry.cpp:445`). Cross-language importers see only
functions.

<a id="lr01-10"></a>**LR01-10 · Built-in module names are hardcoded · PARTIAL**
`resolve_module_path` is gone; resolution now runs through
`resolve_imported_module` (`lambda/runtime/build_ast.cpp:2346`), which still
hardcodes `"math"` and `"io"` by `strview_equal` plus two builtin aliases.
*Residue:* adding a built-in still means editing this function — but the Jube
path is now data-driven (`registered_jube_module_name`, `jube_module_imports`),
so the hardcoding no longer blocks third-party modules.

<a id="lr01-11"></a>**LR01-11 · Registry registration is entry-path-asymmetric · OPEN**
`load_script` registers a module for cross-language import only when
`context && context->heap` exists (`runner.cpp:1136`). During pure
Lambda→Lambda import the context is not yet set up, so those modules are not
registered; only the JS→Lambda path, which sets up context first, registers
them. The adjacent comment (`:1135`) documents the asymmetry rather than fixing
it.

<a id="lr01-12"></a>**LR01-12 · `g_template_registry` is a single process global · OPEN**
Created from two places (`runner.cpp:1516`, `transpile-mir.cpp:27369`) and read
unguarded across `interp.cpp:4342`ff. `template_registry_destroy` nulls it only
if it matches the destroyed registry, so multiple concurrent runtimes collide.
Cross-link: RC1–RC8 in [Radiant concurrency design].

<a id="lr01-13"></a>**LR01-13 · Teardown ordering is load-bearing · OPEN**
`runtime_reset_heap` (`runner.cpp:1752`) and `runtime_cleanup` (`:1872`) both
construct a temporary `EvalContext`, hand it the retained `heap` / `name_pool` /
`type_list`, and rely on releasing the name pool only *after* heap destruction
(`:1825`, `:1939`). The ordering and the temporary-context trick are required
and are not free to reorder.

---


## 2. Parsing & AST construction (LR_02)

<a id="lr02-1"></a>**LR02-1 · Relational result type is representation-sensitive · PARTIAL**
`< <= > >=` still yields `TYPE_BOOL` or `set_type_any(tp, ANY_COMPARE)`
depending on operand openness (`build_ast.cpp:7270`–`7274`), and must stay in
lockstep with the transpiler's vectorized-comparison codegen ([LR_07](#7-mir-direct-transpiler--jit-lr_07)).
*Residue:* the `ARRAY_NUM` third outcome the doc described is no longer produced
here, so the lockstep surface is narrower than documented but still real.

<a id="lr02-2"></a>**LR02-2 · No-`else` `if` still widens mixed joins to `ANY` · PARTIAL**
`infer_if_result_type` (`build_ast.cpp:4124`) now contributes `TYPE_NULL` for a
missing else arm, runs numeric joins through `lambda_numeric_classify`, and
builds a real `TYPE_KIND_BINARY` union when one arm diverges.
*Residue:* plain mixed non-numeric joins still fall to `set_type_any(tp,
ANY_JOIN)` (`:4146`), with an in-code comment saying they "remain open until
recursive return inference and boxed-carrier handling are resolved together."

<a id="lr02-3"></a>**LR02-3 · Undeclared global function returns stay `TYPE_ANY` · PARTIAL**
`function_type->returned = &TYPE_ANY` is still forced for undeclared returns
(`build_ast.cpp:8398`, `:8466`).
*Residue reframed:* this is now a deliberate stable forward-ABI carrier —
`function_type->inferred_return` *does* narrow from the completed body
(`:8393`–`:8395`) and MIR consumes it. The remaining gap is that the public
carrier stays `ANY`, so any consumer reading `returned` rather than
`inferred_return` loses the precision. Cross-link: TIG1 in
[Type-infer impl progress] — "consumers reading `node->type` instead of the
representation oracle" is the same defect class.


<a id="lr02-6"></a>**LR02-6 · Object literal routing · OPEN (note)**
Object construction goes through the element reduction path and resolves
object-typed tags before ordinary element construction. Retained as a structural
note, not a defect.

<a id="lr02-7"></a>**LR02-7 · Recursion / cycle guards remain load-bearing · PARTIAL**
*Fixed:* `MAX_BUILD_DEPTH` is gone; the `entry_count > 1000` cap in
`lookup_name` is replaced by a tortoise-hare cycle detector
(`build_ast.cpp:2645`–`2657`) after the fixed cap was found to fire on
legitimately large module scopes and cause a tier mismatch (SI3v2 — see
[Tier-mismatch fixes 2026-08-18]); the "skip invalid node and continue"
defensive-recovery arm is gone.
*Residue:* the cycle guard itself is still a safety net standing in for a
stronger structural invariant on scope entry lists.


<a id="lr02-17"></a>**LR02-17 · Bare `x.sum` on a map has never bound a builtin · OBSERVATION (verified 2026-09-03)**
Load-bearing evidence for **S12.3.3v2**, which makes the method-eligible
builtin tier a *call-site* rule: `x.name(...)` may reach a builtin, bare
`x.name` may not. The implementation has always behaved this way, by
construction rather than by intent — `get_sys_func_for_method`
(`build_ast.cpp:487`) is keyed on the parenthesized argument count, and its
only call site (`build_ast.cpp:8266`) sits inside the call-expression
builder, after `direct_lookup_object_method`. The member-expression builder
never consults the registry. Probe (release `lambda.exe`, commit `ababcb674`,
`temp/probe_bare_member2.ls`): on `let m = {a: 1, b: 2}`, `m.len()` → `2`
while bare `m.len` → `null` and `m.len == null` → `true`.

Why it matters: S8.2.1v4 now makes `obj["m"]` the dynamic form of `obj.m`,
reaching the type's methods. That is only safe because the builtin tier is
call-only — otherwise `m[key]` probing on a plain map would return a bound
builtin whenever `key` happened to spell one, instead of `null`. Any future
change that lets bare member access fall through to the registry silently
breaks that guarantee. Recorded as an observation, not a defect: nothing to
fix, but the property must not regress. [OB5, [Type_Object §16](Lambda_Type_Object.md)]

**LR02-14/15 outcome (2026-08-27).** Both landed; baseline **3966/3966**.
S16.10.1 was narrowed to **v2** (spec 18.0.0) twice during implementation:
first from the whole keyword table to *capture-real* words only (the full ban
cost 332 corpus files and broke public APIs), then again to release
`else case default on` — S16.2.2v2 already called the first three
continuation-only, so they were never capture-real. Final counts: 60 barred,
28 allowed; `state` and `lambda` are barred by **reservation**, not capture.
Migration was 52 `.ls` files plus one `.mir-check` sidecar. The lasting
lesson is recorded in §7.24: allowing a word takes **two** changes — the
lexer bar and `token_is_identifier_like` — or declarations are accepted while
every use fails to parse.

---


## 3. Value & type model (LR_03)

<a id="lr03-2"></a>**LR03-2 · Hard-coded capacity caps · OPEN**
`TYPEMAP_HASH_CAPACITY` 32 and `TYPEMAP_HASH_DYNAMIC_MAX_CAPACITY` 32768
(`lambda/lambda-data.hpp:346`–`347`) bound the per-map hash table; on saturation
lookups silently fall back to the O(n) shape chain. `NAME_POOL_SYMBOL_LIMIT` 32
(`lambda/lambda.h:77`) and `LAMBDA_TCO_MAX_ITERATIONS` 1000000 (`:83`) are
likewise fixed. `ArrayNumShape.ndim` is bounded 1..32 — see [LR05-1](<Lambda_Issue_Ledger(fixed).md#lr05-1>).

<a id="lr03-3"></a>**LR03-3 · MIR-JIT workarounds embedded in the value model · OPEN**
`_store_i64` / `_store_f64` prevent MIR SSA reordering in swap-pattern loops;
`push_d_safe` guards a representation ambiguity at float boxing boundaries;
`_barg` accepts tagged Items or raw integer values for bitwise ops. These
couplings should shrink as the common representation contract becomes
authoritative — see [LR07-1](#7-mir-direct-transpiler--jit-lr_07).
*Note:* the doc framed this partly as C2MIR/MIR-Direct divergence; with C2MIR
removed it is now purely a MIR Direct ↔ value-model coupling.
*Implementation note (2026-08-28, D2.4.1–D2.4.3):* the shared carrier vocabulary and
fail-closed conversion boundary now distinguish the int lane from machine/full-width
integers. Remaining workaround reduction is gated on expression-producer migration.

<a id="lr03-5"></a>**LR03-5 · `it2d` / `it2b` coercions · PARTIAL**
*Reframed as deliberate:* `it2d` poisons unrecognized types to `NaN`
(`lambda-data.cpp:353`) with an in-code note that the previous `0.0` was silent
data corruption; `it2b` returns `true` for all numbers including floats
(`:368`–`:372`) with a comment stating Lambda truthiness deliberately rejects
JS-style zero/NaN falsiness.
*Residue:* `it2d`'s NaN is still an unraised poison value rather than an error
Item, so a downstream consumer that does not check `isnan` silently produces a
wrong number instead of propagating.

<a id="lr03-6"></a>**LR03-6 · Overloaded tags · OPEN**
`BigInt` rides on `LMD_TYPE_DECIMAL`, distinguished only by
`Decimal.unlimited == DECIMAL_BIGINT` (`lambda/lambda.h:1361`–`1362`);
`JsAccessorPair` deliberately begins with `type_id == LMD_TYPE_FUNC`, so a slot
value mis-reads as a function unless callers check `JSPD_IS_ACCESSOR` first
(`lambda-data.hpp:281`, warned in the header at `:286`).

---


## 4. Numbers, decimal & datetime (LR_04)


<a id="lr04-2"></a>**LR04-2 · BigInt still has practical caps · OPEN**
`bigint_precision_context` caps precision at 100000 digits
(`lambda-decimal.cpp:1224`, `:1594`) and shift helpers reject counts above
100000 bits (`:1773`, `:1803`); string ingest rejects above 100000 (`:1310`).
Implementation guardrails, not mathematical limits in the surface model.


<a id="lr04-5"></a>**LR04-5 · Float↔decimal round-trip via text is lossy and hot · OPEN**
`decimal_mpd_to_double` reverses through `mpd_to_sci` + `strtod`
(`lambda-decimal.cpp:664`–`673`), and the forward direction goes through a
`snprintf`-formatted string into `mpd_qset_string`. Round-trip-safe for most
doubles but fragile at subnormals and edge magnitudes, and the string detour is
a hot-path cost.

<a id="lr04-6"></a>**LR04-6 · `error_code` / sentinel coupling · OPEN**
Division-by-zero and invalid decimal results can still collapse to a generic
`ItemError`; structured `LambdaError` codes are attached upstream — see
[§10](#10-error-handling-lr_10).

<a id="lr04-7"></a>**LR04-7 · DateTime range caps · OPEN**
`DATETIME_MAX_YEAR 4191` (`lib/datetime.h:95`) bounds years to −4000…+4191;
`tz_offset_biased : 11` (`:28`) bounds the offset to ±1023 minutes; milliseconds
are the finest precision. Out-of-range construction yields
`DATETIME_MAKE_ERROR()`.

---


## 5. Strings, symbols & vectors (LR_05)


<a id="lr05-6"></a>**LR05-6 · Fixed-size buffer truncation in stencils · OPEN**
`STENCIL_MEDIAN_CAP 4096` (`lambda-vector.cpp:4045`) rejects larger median
kernels (`:4070`) and backs a 4096-element stack `medbuf` (`:4086`); `fn_otsu`
hard-codes 256 bins in a stack `int64_t h[256]` (`:4501`).

<a id="lr05-7"></a>**LR05-7 · utf8proc allocation crosses the allocator boundary · OPEN**
Normalizers return raw utf8proc-allocated buffers that callers must `raw_free`
(`utf_string.cpp:64`–`65`, `:90`); the `RAWALLOC_OK` annotations acknowledge
this sits outside the pool/GC discipline.

---


## 6. C transpiler — legacy C2MIR (LR_06)

**All nine issues are archived in the [fixed issue ledger](<Lambda_Issue_Ledger(fixed).md#lr06-r1r9>).** The backend no longer exists in the tree.

## 7. MIR Direct transpiler & JIT (LR_07)

These cluster around three structural facts: MIR's immutable register types, the
dual native-or-boxed value representation, and GC rooting under a non-moving
collector.

<a id="lr07-1"></a>**LR07-1 · Numeric semantic result and physical representation are still coupled · OPEN**
`mir_expr_carrier_type` (aliased as `get_effective_type`,
`transpile-mir.cpp:2190`), `transpile_binary`, and `transpile_box_item` each
carry separate repairs for runtime helpers that return boxed Items even when the
AST names a concrete numeric type. All three sites must consume one shared
result-domain decision or a raw register can be mistaken for an Item.
Cross-link: this is the same "expression results carry no ValueRep" root cause
recorded in [Result32 lane-parity + Tune19] and [Compiling lane design].
*Implementation note (2026-08-28, D2.4.1–D2.4.3):* L0–L4's first slice landed: `MirValue`
carries the full contract, `ValueRep` separates `INT_LANE`/machine quantities, arithmetic,
branch, binding, index, call, and return consumers now use explicit carriers, and direct
identity/axis/fail-closed transition fixtures cover the router. Lambda expression lowering
has zero semantic `MIR_reg_type()` probes; ten remaining probes are physical-only. Raw
expression producers still cross the explicit `transpile_expr_reg_legacy` shim, so this issue
remains open.

<a id="lr07-2"></a>**LR07-2 · "undeclared reg 0" guard · OPEN**
Value-less statements would return the invalid register 0 and crash MIR;
`emit_null_item_reg` (`transpile-mir.cpp:1227`) synthesizes a boxed-null
register instead. The same hazard recurs at the `match` and let/var/break/
continue null-move sites (`:3724`, `:4050`).

<a id="lr07-3"></a>**LR07-3 · Typed-array construction gap · OPEN**
MIR Direct still never emits `array_int()` / `array_int64()` / `array_float()`
even though the registry exports them (`sys_func_registry.c:1306`, `:1308`); it
always builds a generic `Array*`. Element access and mutation have partial fast
paths gated on an `elem_type` proven through `fill()` narrowing or mutation
analysis, guarded by `safe_native_int` (`transpile-mir.cpp:15236`–`15260`), with
frequent `item_at` / `fn_array_set` fallbacks.
*Reframed:* the doc described this as "diverges from C2MIR, in C2MIR's favour,
port it into MIR Direct." With C2MIR deleted there is no reference
implementation left to port — this is now a from-scratch MIR Direct feature.

<a id="lr07-4"></a>**LR07-4 · Type widening is truncate-or-box · OPEN**
`transpile_assign_stam` assigns a FLOAT to an INT variable by truncating via
`MIR_D2I` inside loops (lossy, but required to keep the register type stable)
and by boxing to `ANY` outside loops. Related sharp edge: an error Item (e.g.
from division by zero) is silently coerced to `0` / `0.0` / `false` when a boxed
value is unboxed into a native variable. The range-checked conversion helper at
`transpile-mir.cpp:15435`–`15451` narrows this for indices only (out-of-range
yields the legitimate finite value `INT64_MAX`).

<a id="lr07-5"></a>**LR07-5 · `get_effective_type` only narrows IDENTs to ANY · OPEN**
It does not catch every post-mutation type change, leaving a stale-type boxing
hazard for non-identifier expressions
(`transpile-mir.cpp:3879`, `mir_expr_carrier_type`).

<a id="lr07-6"></a>**LR07-6 · MATCH and vectorized-comparison results are forced boxed · OPEN**
To prevent callers re-boxing an already-boxed value and then dereferencing it as
a pointer.

<a id="lr07-7"></a>**LR07-7 · Precise-root correctness is type-driven · OPEN (now instrumented)**
BUG-001's heap-frame growth hole is closed by static side-stack slots and
publish-before-call lowering. The remaining invariant: any register carrying a
heap-capable boxed value must retain a heap/ANY MIR type. `should_gc_root_var`
(`transpile-mir.cpp:1594`) derives that from `lambda_gc_value_class`, i.e. from
the static `TypeId` via `lambda_canonical_rep_for_type_id` (D2.4.1–D2.4.3), and
roots unaudited imports pessimistically (`JIT_VALUE_UNKNOWN` falls back to "root
any word-sized carrier", `mir_emitter_shared.hpp:918`).

*Instrumented 2026-09-10.* The invariant is no longer only trusted. Setting
`LAMBDA_ROOT_WITNESS=1` makes MIR Direct emit `lambda_jit_root_witness` at every
site that acts on a **negative** `should_gc_root_var` answer —
`mir_store_var_entry`, `update_gc_root_slot`, `root_gc_result_if_needed` — and
the probe checks the unrooted word against the GC zone (`gc_is_managed`) in both
carrier shapes: a bare container/descriptor pointer, and a tagged pointer-lane
scalar. Violations are logged and execution continues, so one suite run collects
every offending site; totals are reported at exit. The flag is off by default
and then emits nothing, so ordinary and release builds are unchanged.

*Coverage — two levels (`LAMBDA_ROOT_WITNESS`):*

| level | what is probed |
|---|---|
| `1` | named locals and rooted call results — a handful of probes per function, so whole-corpus sweeps stay cheap |
| `2` / `all` | adds **expression temporaries**: every register live across a may-GC call that the root machinery never made a candidate |

Level 2 exists because the root machinery's own liveness pass
(`em_finalize_semantic_root_write_back`) only ever considers **candidates** —
registers someone explicitly noted. A register that never became one is
invisible to it, and that is precisely where a temporary can carry a heap value
across a safepoint with nothing publishing it. `emit_root_witness_across_calls`
runs after root finalization, computes each register's first..last *mention*
span, and probes every non-candidate register whose span straddles a may-GC
call. A mention span rather than def..use: a loop-carried register is defined at
the bottom of the body and read at the top, so in linear order its last use
precedes its first definition and a def..use test would call it dead exactly
where it is live across every call in the loop. The span over-approximates, which
is the right direction for a diagnostic, and the runtime filter discards the noise.

Measured cost of level 2: `graph/structurizr/source_contract.ls` emits **53,937**
extra probes and runs 2.51 s vs 2.39 s at level 1 (~5%).

*The probe's own registry metadata is load-bearing.* Its first version left the
`lambda_jit_root_witness` row unannotated, which defaults to
`JIT_EFFECT_MAY_GC` with `JIT_VALUE_UNKNOWN` arguments — so the emitter
published **every probed register into a root slot at the probe's own call
site**. The instrumentation was rooting exactly the values it existed to catch
as unrooted, and at level 2 it was growing the frame after finalization. The row
now declares `JIT_EFFECT_NO_GC` / `JIT_REENTRY_NO` with non-GC argument classes,
and `lambda_jit_root_witness` is listed in
`jit_import_validate_no_gc_allowlist()` with its justification. That allowlist is
what caught the mistake: a NO_GC claim that is not audited aborts the build.
Any future probe added to this tool must be audited the same way, or it will
quietly falsify its own results.

*First sweep, 2026-09-10 (all 1052 `test/lambda/**/*.ls`, `LAMBDA_TIER=jit`):*
**4 violations in 3 scripts**, every one the same shape — `claimed_type=24`
(`LMD_TYPE_TYPE`) over a live `MAP` or `ARRAY_NUM`. Root cause: `LMD_TYPE_TYPE`
is overloaded. A type **value** (`let t = int`) really is a descriptor pointer,
but a value **contract** written as a type term — `Node?`, `int | null`, a
`TypeParam` carrier — also has `type_id == LMD_TYPE_TYPE`, with an extended
`kind`. `mir_unwrap_decl_type` only unwraps the SIMPLE kind, so
`mir_decl_type_id` reported the meta-type for a contract describing an ordinary
value; `lambda_gc_value_class` read that lone id, returned
`JIT_VALUE_RAW_NON_GC_POINTER`, and `should_gc_root_var` allocated no root slot
for a live container. This is the same overload that already cost Result37
`triangl2` on the indexing path (the reason `mir_type_is_type_value` exists) —
the GC classifier was simply never brought to the same discipline.

**Severity: latent, not live.** The MIR dump for the repro shows the binding
still reaching a root slot (`mov i64:72(%rf4), %rfa` before the next call): the
MOV-propagation candidate pass rescues it through the `JIT_VALUE_UNKNOWN`
compatibility fallback (`mir_emitter_shared.hpp:918`, "root any word-sized
carrier"). So the classifier lie is real but currently masked — **retiring that
fallback before fixing the classifier would convert these into live
use-after-free.**

*Fixed 2026-09-10 — all four sites.* Three producers read a contract's
`->type_id` raw and so published the meta-type for a value:
`mir_decl_type_id` (declaration/binding contracts), the `for`-clause element
binding (`val_tid = loop->type->type_id`), and `mir_expr_carrier_type`'s
fall-through (`tid = node->type->type_id`). The last already had a partial
repair beside it — a `LaneStorageDesc` refinement whose comment reads "an
occurrence node's compact TypeId is `type`, but its carrier is the payload
lane" — but it only covers INT/BOOL/FLOAT64/POINTER lanes, so a **container**
occurrence (`Node?`, `Variable?[]`) fell straight through.

Rather than repeat the test, one predicate now owns the distinction and all
three call it:

```c
// The TypeId a contract publishes for a VALUE.
static TypeId mir_value_type_id(Type* type) {
    if (!type) return LMD_TYPE_ANY;
    if (type->type_id == LMD_TYPE_TYPE && !mir_type_is_type_value(type)) {
        return LMD_TYPE_ANY;
    }
    return type->type_id;
}
```

`mir_decl_type_id` is now `mir_value_type_id(mir_unwrap_decl_type(type))`. Any
future value-side TypeId read should go through `mir_value_type_id` so the
overload cannot be forgotten at a fourth site.

*Verification:* `make test-lambda-baseline` **5259/5259**, and a full
`LAMBDA_ROOT_WITNESS` sweep over all 1052 `test/lambda/**/*.ls` under
`LAMBDA_TIER=jit` reports **0 violations** (pass 1: 4 in 3 scripts; pass 2 after
the first producer: 2 in 2 scripts). The binding half of the invariant is now
tested clean across the corpus.

*Classifier made fail-closed, 2026-09-10.* Fixing the producers left the
consumer still deciding GC safety from a bare `TypeId`, so a fourth producer
would have re-opened the hole. `lambda_gc_value_class` now takes the semantic
`Type*` when the caller has one and treats `LMD_TYPE_TYPE` as *unresolved*
rather than as a descriptor:

- with a contract, `lambda_canonical_rep(contract)` decides — it already
  unwraps occurrence kinds and refuses a raw carrier for a union, so it
  separates a real descriptor from a value wearing a type term;
- without one, the value is **rooted**. A needless root slot on a descriptor
  costs one store and the collector skips the word (`gc_mark_item` →
  `is_gc_object`); a missing slot on a live container is a use-after-free.

The contract is consulted **only** for that arm. `type_id` at these sites is the
reconciled carrier witness and is the more honest description of what the
register physically holds, so it still drives every other classification —
re-deriving the carrier from the raw AST contract would have been a regression.
`update_gc_root_slot` passes the binding's existing `VarEntry::full_type`; sites
without a contract simply fail closed.

*Verified by reverting the producers.* With `mir_value_type_id` temporarily
restored to the raw `->type_id` read — i.e. all three producers dishonest
again — the witness still reports **0 violations**: the classifier alone now
roots them. That is the defence-in-depth property the entry was missing.

*Temporaries covered 2026-09-10 (level 2).* See the coverage table above.
`make test-lambda-baseline` **5286/5286**, and a level-2 sweep over all 1054
scripts reports **0 violations** — so both halves of the invariant, bindings and
temporaries, now test clean across the corpus.

The notable finding while building it is how little was left: instrumenting the
candidate/non-candidate split showed that nearly every register live across a
may-GC call is **already a root candidate** (in one measured function, 13 of
17), so the root machinery's coverage was materially better than this entry
implied. It was simply never *checked*. What LR07-7 should now say is not "the
collector trusts the transpiler" but "the collector's trust is verified at
level 2, for bindings and temporaries, over the whole test corpus."

*Negative control.* Reverting both the producers and the classifier to their
pre-fix state makes the probe report the original violation again at level 1 and
level 2, so a clean sweep is evidence of correctness rather than of a probe that
stopped looking.

*Reproduce:*

```bash
LAMBDA_TIER=jit LAMBDA_ROOT_WITNESS=1 ./lambda.exe run temp/rw_repro/nullable_return_unrooted.ls
```

Cross-link: [LR08-3](#lr08-3) is the same defect seen from the collector's side.
The former LR11-6 / LR12-3 faces are archived — see
[LR11-R6](<Lambda_Issue_Ledger(fixed).md#lr11-r6>) — and the TCO face is now
[LR07-13](#lr07-13).

<a id="lr07-8"></a>**LR07-8 · Bitwise ops are special-cased before generic dispatch · OPEN**
`band` / `bor` / `bxor` lower to a single MIR instruction and `shl` / `shr` are
guarded against out-of-range shift counts, hard-coded ahead of generic dispatch
because `SysFuncInfo` has no per-argument native-convention field. Paired with
[LR09-2](#9-runtime-builtins-lr_09).

<a id="lr07-9"></a>**LR07-9 · `uint8_t Bool` returns need masking · OPEN**
Runtime functions returning a `uint8_t` bool leave garbage in the upper 56 bits
of the MIR return register, so every bool box/unbox must call `emit_uext8`
(`transpile-mir.cpp:2796`, used `:3355`, `:8576`).

<a id="lr07-10"></a>**LR07-10 · Out-of-bounds index semantics differ by type · PARTIAL**
*Improved:* OOB behaviour is now policy-driven — `MIR_INDEX_OOB_ITEM_NULL`,
`MIR_INDEX_OOB_FLOAT_ZERO`, `MIR_INDEX_OOB_FLOAT_NULL`
(`transpile-mir.cpp:14692`–`14694`), selected at `:15038`–`:15042`.
*Residue:* `MIR_INDEX_OOB_FLOAT_ZERO` still exists and still yields `0.0` rather
than null for a float-index OOB read whenever the result register is `MIR_T_D`,
so the type-dependent semantic split is real, just now explicit.

<a id="lr07-11"></a>**LR07-11 · Fixed-size structural caps · OPEN**
`var_scopes[64]` (`transpile-mir.cpp:153`, overflow errors at
`scope_depth >= 63`), `loop_stack[32]` (`:157`), hashmap key buffers
`char name[128]` that silently truncate long identifiers (`:1117`, `:3916`,
`:5796`, `:6250`, `:6682`, `:15109`), and `proto_name[140]`
(`mir_emitter_shared.hpp:1569`).

<a id="lr07-13"></a>**LR07-13 · TCO iteration ceiling, and the safety proof that would lift it · OPEN**
Tail-recursive loops emit a guard raising a stack-overflow error past
`LAMBDA_TCO_MAX_ITERATIONS` (`transpile-mir.cpp:27934`–`27941`); the interpreter
enforces its own `LAMBDA_INTERP_TCO_MAX_ITERATIONS` (`interp.cpp:5051`). A
correctly TCO'd loop consumes no native stack, so the ceiling is a proxy for a
proof the transpiler declines to use.

*Absorbed from [LR11-6](#lr11-6) / [LR12-3](#lr12-3) on 2026-09-10:*
`is_tco_function_safe` (`safety_analyzer.cpp:441`) computes exactly that proof —
"every recursive call in this function is in tail position, so after the goto
transform the frame cannot grow" — and is **declared, defined, and never
called**. Wiring it is what turns the iteration ceiling from a blanket cap into
a guard only unproven functions pay for.


<a id="lr07-14"></a>**LR07-14 · Cross-cutting gaps · OPEN (rollup)**
Numeric result-domain inference is duplicated across AST / MIR / runtime;
`SysFuncInfo` has no complete data-driven argument convention, so some return
conventions still use ad-hoc switches; and there is no debug-mode validation
that a boxed value carries the representation the transpiler believes it does.
The Stack API is the physical ownership authority; `Lambda_Impl_Numbers.md` owns
the semantic-promotion consolidation.

---


## 8. Memory management & GC (LR_08)


<a id="lr08-2"></a>**LR08-2 · Execution-side-stack capacity is reserved up front · OPEN**
Root and raw-number regions have fixed virtual limits. Checked prologues fail
deterministically instead of corrupting adjacent memory, but workloads that
genuinely exceed those reservations cannot grow them dynamically.

<a id="lr08-3"></a>**LR08-3 · JIT rooting still hinges on honest static types · OPEN (now instrumented)**
The collector trusts the transpiler's `should_gc_root_var` classification. A
heap Item mislabeled as a packed scalar could miss a precise slot; publishing
all heap-capable live locals before calls narrows but does not close the hazard.
This is the collector-side view of [LR07-7](#lr07-7), which now carries the
`LAMBDA_ROOT_WITNESS` probe that tests the classification against the runtime
bit pattern; run it together with `LAMBDA_GC_FORCE_EVERY=1` and
`LAMBDA_GC_POISON_FREED=1` to pair "unrooted heap reference" evidence with the
use-after-free it would cause.
Per CLAUDE.md rule 15, the fix is precise `RootFrame`/`Rooted` ownership — never
a return to conservative native-stack scanning.

<a id="lr08-4"></a>**LR08-4 · Wide scalar ownership must be explicit at every escaping store · OPEN**
Number-frame temporaries are reclaimed at return, so containers, JS
environments, exceptions, and other longer-lived stores must rehome payloads
into storage-owned lanes. The shared store/rehome helpers enforce the current
paths; a new raw Item store that bypasses them creates a dangling scalar
pointer.


<a id="lr08-6"></a>**LR08-6 · `SHAPE_POOL_MAX_CHAIN_LENGTH` = 64 silently returns NULL · OPEN**
Maps/elements with more than 64 fields get no pooled shape
(`lambda/core/shape_pool.cpp:182`–`183`, `:247`) — only a `log_warn`, with a
possible NULL-deref downstream depending on caller handling.

<a id="lr08-7"></a>**LR08-7 · Deep recursion consumes root and number watermarks as well as C stack · OPEN**
Frames no longer allocate heap root blocks, but recursion accumulates each
function's statically reserved slots until the epilogue restores them. The
side-stack bound check or the C-stack guard terminates pathological depth,
whichever fires first.

<a id="lr08-9"></a>**LR08-9 · Re-entrant allocation during GC silently skips collection · OPEN**
`gc_collect` guards with `gc->collecting` (`gc_heap.c:1106`) and the allocation
paths check it before triggering (`:628`, `:846`), so an allocation made *during*
tracing or a finalize callback simply skips collecting rather than asserting.
Acceptable, but unguarded against pathological growth inside a callback.

<a id="lr09-30"></a>**LR09-30 · Regex capture groups silently truncate at 256 · RESOLVED (2026-09-08)**
A regular expression with more than 255 capture groups reports the wrong result
and gives no diagnostic. Repro:

```js
const n = 300;
const re = new RegExp('(a)'.repeat(n));
const m = 'a'.repeat(n).match(re);
console.log(m.length - 1, m[n]);   // Lambda: 255 undefined   Node: 300 a
```

`$300` in a `replace` pattern likewise resolves to nothing. The cause is
`JS_REGEX_MAX_GROUPS` (256, `js_regex_wrapper.h:25`) with clamps of the form
`if (ngroups > JS_REGEX_MAX_GROUPS) ngroups = JS_REGEX_MAX_GROUPS;` at seven
sites across `js_runtime.cpp` and `js_regex_wrapper.cpp`. The constant sizes
about a dozen **stack** arrays (`re2::StringPiece matches[...]`,
`int starts[...]/ends[...]`, `RegexGroupInfo groups[...]`), so removing it means
either heap-allocating on the match path or sizing from the compiled pattern's
group count. ECMAScript sets no such limit and V8 allows 32,767.

**Fixed 2026-09-08.** `JS_REGEX_MAX_GROUPS` is gone. Match scratch is sized
from the compiled pattern's own group count through one `JsRegexScratch<T>`
helper (`js_regex_wrapper.h`) that keeps `JS_REGEX_INLINE_GROUPS` (32) slots
inline and heap-allocates only above that, so an ordinary pattern still
allocates nothing on the match path. Every clamp is deleted.

**There were three caps, not one, and the first fix only moved the boundary.**
After the match-scratch conversion a 300-group pattern matched correctly but a
*lookahead* over 128 groups still failed. Two more fixed limits stood behind it:

- `erased_original_group[256]` in the wrapper's assertion-rewrite pass, whose
  guards silently stopped the erased-group remap partway, producing a wrong
  rewritten pattern. Now sized from `original_group_count`.
- The backtracking matcher (`js_bt_regex.cpp`), which had `int cap_start[256]`,
  `cap_end[256]`, per-iteration `saved_s/saved_e[256]` and per-lookaround
  `sv_s/sv_e[256]`, plus an explicit `if (ng + 1 > 256) return 0; // fall back`.
  That return is reported to the caller as **no match**, so it was not a
  fallback at all — a large lookahead pattern silently failed. All four arrays
  are sized from the pattern and the refusal is deleted.

Verified against Node on match, `exec`, high-numbered `$n` replacement and
lookahead at 128/150/200/300 groups: byte-identical output. Regression test
`test/js/regex_many_capture_groups.{js,txt}` covers all six cases; JS gtest is
371 tests, up from 370.

No performance cost: the ordinary-pattern match path got *faster* in a
debug-build A/B (813 ms vs 1017 ms over 600k matches), which is consistent with
no longer placing 4 KB of `re2::StringPiece[256]` and 2 KB of `int[256]` on the
stack per match. Per rule 10 that debug figure is directional only; the point is
that it is not a regression.

Gates: test262 40261/40261 with 0 regressions, JS gtest 371/371, script gtest,
rooting core, MIR GC stress, lambda baseline 5078/5078 (the memtrack gate), node
slice identical to pristine.

<a id="lr08-12"></a>**LR08-12 · Generator/async suspension states capped at 64 · RESOLVED (2026-09-08)**
`JsMirTranspiler::gen_state_labels` was `MIR_label_t[64]`, and two clamps
matched it — `if (yield_count > 63) yield_count = 63;` and the identical line
for `await_count`. Both truncated silently: a 100-yield generator summed only
its first 62 values (1891 instead of 4950), and a 150-await async function was
wrong the same way. Fixed by exact-sizing the label array from the pre-counted
state count, checking that capacity in `jm_next_resume_state` instead of a
literal 64, and deleting both clamps. Same sweep produced LR09-30 above.

<a id="lr08-11"></a>**LR08-11 · Native realm construction is not GC-safe · RESOLVED (2026-09-08)**
The JS realm's native module builders were written against an implicit
"no collection happens here" assumption. Under
`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1` this fails in three distinct
ways, all violating **D5.4.2** (a value under construction is live and needs an
exact root); D2.1.7 pins the heap as non-moving, so these are liveness bugs, not
address-stability bugs.

1. **Root ranges registered one Item too high.** Five `JsNamespaceState`-derived
   caches (`stream`, `http`, `https`, `net`, `fs`) registered their precise root
   range at the first *derived* field rather than the inherited
   `namespace_object`, which the base lays out first. Each range therefore left
   its namespace object unrooted *and* scanned one Item past the end of the
   struct. `require("stream")` returned a namespace with **zero** properties
   under forced GC. Fixed 2026-09-08 in `js_runtime_state.cpp`; the catalog now
   starts every such range at `namespace_object`, and a one-time runtime check
   (`js-root-range:` in the log) pins the start/count pair for all seven
   namespace states. These states are not standard-layout, so `offsetof` on them
   is ill-formed and the guard cannot be a `static_assert`.

2. **Factories that build an object on a bare C local.** `js_new_object()`
   followed by a run of allocating property installs, with the only reference in
   a C automatic. The object is reachable from nowhere until the last store, so
   a collection mid-construction reclaims it and the finished object comes back
   missing methods, or a later store writes into reclaimed memory and crashes.
   Fixed: the four `node_crypto` factories, both `node_path` parse factories and
   `path.win32` (this one crashed `require("path")` outright), `node_os`
   `networkInterfaces`/`userInfo`, the `stream` base constructor and its
   prototype, and `http.STATUS_CODES`.

3. **Two allocating arguments in one store.** `set(obj, make_string(k),
   make_string(v))` — argument evaluation order is unspecified, so whichever
   operand is built first is an unrooted temporary while its sibling allocates.
   The canonical rooted publisher `js_install_native_*`
   (`js_runtime_function.cpp`) already carries this rule as a comment citing
   D5.2/D6.2.2v2, but hand-rolled `*_set_method` clones bypassed it. Fixed:
   `stream_set_method`, `assert_set_method`/`assert_set_fresh_method`/
   `assert_set_method_item`, `js_path_set_method`, `dns_set_constant`,
   `js_message_port_data_clone_error`, and the `js_net` address-property and
   `node_events` unhandled-error stores. The 15 Jube-module `*_set_method`
   clones were already correct.

**Second pass (2026-09-08) closed it.** All 26 built-in modules now report
byte-identical key sets with and without
`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`, and
`make test-jube-node-core-dynamic` — whose forced-GC arm produced 7 of 35
registry lines on pristine `HEAD` and 11 after the first pass — passes.

A fourth shape appeared in this pass, and it is the most dangerous of the four:

4. **A cache slot published before its root exists, or with no root at all.**
   `js_get_internal_stream_state_namespace` assigned the fresh object into a
   `JsStreamState` slot without first calling `stream_ensure_roots()`, so the
   range backing that slot was not yet registered. The object was reclaimed and
   its storage reused by the two native functions installed immediately after,
   which is why the module registry reported this namespace as a **function**
   rather than an object. `js_get_internal_stream_add_abort_signal_namespace`
   was worse: it cached into a function-local `static Item`, which a precise
   collector never scans at all. That namespace now has a real slot in
   `JsStreamState` (range 44 → 45, guard's last field updated accordingly), and
   both getters plus `js_get_internal_stream_end_of_stream_namespace` register
   the range before publishing. The other 13 function-local `static Item`
   namespace caches in `js_runtime.cpp` were audited and all call
   `heap_register_gc_root`, so this was the only unrooted one.

Also fixed in this pass: `tls` rootCertificates key (freed across the
certificate-bundle build) and the bundled-pem push; `http` `METHODS` array and
`globalAgent` (both unreachable across their own construction); the three `dns`
server-array builders (`dns_load_system_servers`, `dns_array_copy`,
`dns_validated_servers_copy`, whose arrays were bare locals across push loops —
this is why `dns.__dns_servers__` was absent while `getServers()` still worked);
and `zlib` `constants`, which was created and then left unrooted while the
sibling `codes` object allocated, so its root slot received a reclaimed pointer.

**Rule of thumb the four shapes reduce to:** publish into a registered root
*before* the next allocation, never between two of them. The ~194-site shape
scan of `lambda/{js,module,dom}` remains a starting point for future audits, not
a defect list — most entries are reachable through an already-rooted owner.

<a id="lr08-10"></a>**LR08-10 · Fixed compile-time sizes · OPEN**
Object size classes are now 16/32/48/64/96/128/256/384 B
(`gc_object_zone.h:16`, `GC_NUM_SIZE_CLASSES 8` at `:43`) with a `malloc`
large-object path above; data-zone blocks 4 MB; bump blocks 4 MB→64 MB;
root/number side-stack reservations and the adaptive-threshold cap are fixed
profiles rather than runtime configuration.

---


## 9. Runtime builtins (LR_09)

<a id="lr09-1"></a>**LR09-1 · Commented-out replace-in-file procedures (key collision) · OPEN**
`pn_replace_file3` / `pn_replace_file4` are still commented out in the registry
table (`lambda/runtime/sys_func_registry.c:1081`, `:1083`) because their
`("replace", 3)` key collides with the existing `SYSFUNC_REPLACE` row; the
composite `(name, arg_count)` map in `build_ast.cpp` cannot disambiguate them.
Enabling them requires `first_param_type`-based disambiguation (one is
`LMD_TYPE_PATH`) in `get_sys_func_info`, which does not exist.

<a id="lr09-2"></a>**LR09-2 · `SysFuncInfo` lacks a data-driven native-argument convention · OPEN**
`c_arg_conv` is still a coarse `C_ARG_ITEM` / `C_ARG_NATIVE` boolean
(`sys_func_registry.h:44`–`45`, field `:83`). With no per-argument convention,
the bitwise ops are special-cased inline in the transpiler ahead of generic
dispatch ([LR07-8](#lr07-8))
rather than driven from the table.

<a id="lr09-3"></a>**LR09-3 · Ordered comparison is deliberately partial · OPEN**
The scalar comparators enumerate numeric/datetime/string cases and return
`BOOL_ERROR` for other types, bool and null included
(`lambda/runtime/lambda-eval.cpp:1650`, `:1811`) — cross-family `<` is an error
while cross-family `==` is `false`, the Python-style split. The former
strict-equality and VMap key-domain residue is resolved in
[OI-1-R1](<Lambda_Issue_Ledger(fixed).md#oi1-r1>); the conversion-failure case
is retained as [LR04-4](<Lambda_Issue_Ledger(fixed).md#lr04-4>).

<a id="lr09-4"></a>**LR09-4 · `fn_index` swallows invalid indices · OPEN**
A non-integral `FLOAT` index, an out-of-range index, or an unrecognized index
type returns `ItemNull` with only a `log_debug`; the intended fix is still
marked `// todo: push error` at three sites
(`lambda-eval.cpp:4037`, `:4053`, `:4060`). OOB semantics also differ between
integer and float index fast paths at the JIT layer
([LR07-10](#lr07-10)).

<a id="lr09-5"></a>**LR09-5 · `NULL`-pointer and unimplemented registry rows · OPEN**
`number` is still marked `// unimplemented`
(`sys_func_registry.c:315`–`316`); the VMap rows are `NULL` by design because
they are lowered inline. A `NULL` that *should* have been a real pointer would
surface only as a JIT import-resolution miss (`mir.c` logs
`failed to resolve native fn`), not as a build error.

## 10. Error handling (LR_10)

## 11. Mark data API (LR_11)

<a id="lr11-1"></a>**LR11-1 · Reader traversal is stubbed · OPEN**
`MarkReader::ElementIterator`'s destructor leaks `state_`
(`lambda/core/mark_reader.cpp:50`, `// TODO: Free traversal state`), and
`next()` only linear-scans *direct* children (`:57`, `// TODO: Implement proper
tree traversal for nested elements`). There is no real descendant or CSS-like
matching, so any caller expecting deep selection gets silently wrong results.

<a id="lr11-2"></a>**LR11-2 · `render_map` iterates while it mutates · OPEN**
The retransform loop calls `fn()` inside the iteration, and that can reach
`render_map_record()` → `hashmap_set()`, resizing the very map being iterated.
The current code defends by snapshotting the entry before re-execution
(`lambda/runtime/render_map.cpp:323`–`326`) and by refreshing the root reverse
mapping (`:135`–`142`), but the iterate-while-mutate pattern remains and is easy
to break with any change to retransform ordering.

<a id="lr11-3"></a>**LR11-3 · The `ui_mode` arena-provenance landmine · OPEN**
Inline `map_rebuild_with_new_shape` must **not** `pool_free` the old data buffer
in `ui_mode_`, because in ui_mode that buffer was arena-allocated by the JIT
(`context->arena`), and freeing it through the editor's pool would corrupt
rpmalloc. The guard is present and correct at both sites
(`lambda/io/mark_editor.cpp:834`–`837`, `:1356`–`1359`), but any path that flips
`ui_mode_` incorrectly corrupts the heap with no diagnostic.

<a id="lr11-4"></a>**LR11-4 · Hard-coded caps with mixed failure modes · OPEN**
All four caps survive, and so does the inconsistency in how they fail:

| Cap | Where | Failure mode |
|---|---|---|
| `SHAPE_BUILDER_MAX_FIELDS` 64 | `lambda/core/shape_builder.hpp:6` | **silent truncation** in `shape_builder_import_shape` (`shape_builder.cpp:132`, `:141` — `log_warn` only), so maps/elements with >64 fields cannot be edited correctly |
| `MAX_BATCH_UPDATES` 64 | `lambda/io/mark_editor.cpp:13` | **errors out** above 64 (`:941`–`945`) |
| `MAX_DEPTH` 2000 / `MAX_FIELD_COUNT` 10000 | `lambda/core/print.cpp:12`–`13` | **clamps** deep/wide structures with a `[MAX_DEPTH_REACHED]` marker (`:139`, `:208`, `:650`) or a bail (`:142`) |
| `EDIT_SOURCE_PATH_MAX` 32 | `lambda/runtime/edit_bridge.h:31` | **fails** source paths deeper than 32 (`edit_bridge.cpp:72`–`75`, `"source path too deep"`) |

Truncate vs. error vs. clamp vs. fail, for four caps in one subsystem, is itself
the hazard.


<a id="lr11-6"></a>**LR11-6 · Conservative safety analysis (adjacent) · RESOLVED 2026-09-10**
Archived as [LR11-R6](<Lambda_Issue_Ledger(fixed).md#lr11-r6>). The record was
never live: the two hard-coded functions it named had no call site anywhere in
the tree even at the commit this ledger verified against, while the real gate
`should_use_tco` was already wired on both tiers. Surviving residue —
`is_tco_function_safe` is computed and discarded — moved to
[LR07-13](#lr07-13).

<a id="lr11-7"></a>**LR11-7 · `createSymbol` pooling-comment divergence · OPEN**
The header comment still claims symbols ≤32 chars are pooled
(`lambda/io/mark_builder.cpp:12`), but `createSymbol` (`:126`–`:134`) is
**unconditional `arena_alloc`** — no pooling branch exists. Anyone relying on
symbol pointer-identity for short symbols will be surprised.

<a id="lr11-8"></a>**LR11-8 · `push` / `splice` are not in the Mark editor · OPEN (note)**
They are runtime builtins in `lambda/runtime/collection_runtime.cpp` (registered
as `SYSPROC_PUSH` / `SYSPROC_SPLICE`) and belong to
[§12](#12-procedural-runtime-lr_12), not the editor surface.

---


## 12. Procedural runtime (LR_12)

<a id="lr12-1"></a>**LR12-1 · `fetch_response_to_item` returns a bare String · OPEN**
`// TODO: Implement proper map structure when the complex type system is
working` (`lambda/runtime/lambda-proc.cpp:513`–`514`) — `pn_fetch` hands back
only the response body as a String; status, headers, and metadata are dropped
(`:510`, consumed `:663`). A proper `{status, headers, body}` map is pending
type-system work.

<a id="lr12-3"></a>**LR12-3 · Safety gate hard-coded, TCO disabled despite being implemented · RESOLVED 2026-09-10**
Archived as [LR12-R3](<Lambda_Issue_Ledger(fixed).md#lr12-r3>). Same mistaken
reading as [LR11-6](#lr11-6): the named functions were dead code, `should_use_tco`
was the live gate all along, and deleting the vestige in `8d44a6ca3` changed no
behaviour.

<a id="lr12-4"></a>**LR12-4 · `push` is generic-`Array`-only · OPEN**
`pn_push` rejects `ArrayNum` (`collection_runtime.cpp:213`), so there is no
in-place append for typed numeric arrays; growing a typed array still requires a
rebuild.

<a id="lr12-5"></a>**LR12-5 · `splice` cannot touch views or N-D arrays · OPEN**
The `is_view` / `is_ndim` guard
(`collection_runtime.cpp:243`–`245`, "copy()/ravel() first") is correct but a
usability cap: in-place removal on a strided or shared typed buffer requires an
explicit copy.

<a id="lr12-6"></a>**LR12-6 · `g_dry_run` is a process-global · OPEN**
Declared `extern bool g_dry_run` (`lambda/lambda.h:63`), set once from the CLI
(`lambda/main.cpp:4920`), read from IO paths (`lambda/core/path.c:769`). A
single non-thread-local flag: concurrent compilation/execution that wants
per-run dry-run semantics has no per-context override. Cross-link: RG1–RG14 in
[Runtime globals audit].

<a id="lr12-9"></a>**LR12-9 · Construction/insertion aliases instead of capturing by value (`S9.3.1`) · IMPLEMENTED BEHIND A FLAG**

**Update 2026-08-28.** Insertion capture is implemented on both tiers behind
`LAMBDA_COW_CAPTURE` (default OFF). With the flag set, all four probes below
return the ruled value, the two-node cycle is no longer constructible, and
`awfy/richards3` still passes. Mechanism: capture is `cow_mark_shared` at the
insertion site — the copy stays deferred to `cow_prepare_write`, so nothing is
eagerly cloned. It is decided at COMPILE time and applied only to a *named*
value (`ast_expr_insertion_needs_capture`): a freshly produced container has no
second observer, and marking one would make `rows[i] = <fresh>` detach on the
owner's first write. MIR Direct additionally needed the static half — it picks
the store form from `MirVarEntry::cow_marked` at compile time, so an unflagged
binding keeps emitting raw field stores that never read the runtime bit
(`mir_note_value_captured` / `mir_emit_value_capture`).

**Why it is not yet the default.** Insertion capture is sound alone, but element
and field READS still borrow (the open C4.1 half). Once a slot holds a captured
value, the get-modify idiom `c = owner[i]` … `c[j] = v` writes to a detached
copy and the update is lost. Measured cost of flipping it: exactly **four**
corpus scripts, all that idiom — `proc/proc_fill_gc_nested`,
`awfy/{cd2_orig,deltablue,deltablue2}`. Three are benchmark sources (`cd2_orig`
is a perf *control*), so the rewrite is a scoping decision, not a mechanical
fix. The sanctioned rewrites are the path write (`owner[i][j] = v`, which
`cow_path_set` already propagates correctly), mutate-then-insert, or the
explicit read-modify-write handle store (`C4.2e`) that `richards3` uses.
`S9.1.3` plain-parameter snapshots remain unimplemented and are still expected
to land with this.

**Two Stage-2 rows closed by ruling, 2026-08-28 (designer), not by
implementation.** (a) The **JS↔Lambda ownership boundary is DEFERRED to
future** — explicitly out of the current COW programme; JS keeps reference
semantics and its raw setters, and the Lambda-side work does not wait on it.
(b) The **module-level half of `S9.2.4` is vacuous by design**: `var` is a
procedural binding and a module-level one is rejected with `error[E224]`, so
there is no module-level `var` to forbid passing as a `var` argument. Only the
**view-state** half survives, which is what `S9.2.4v2` now says (spec
18.1.0). Neither is outstanding work. The nested-mutation design that lets the flag become the
default is now written:
[`Lambda_Design_Nested_Mutation.md`](Lambda_Design_Nested_Mutation.md)
(CW22–CW28, PROPOSED, owner of `SO14`). Its scheduling result is that the flip
is gated on **CW24** — a compile error for a mutated place copy — which turns
silent wrong answers into located, mechanical fixes.

**CW24 implemented 2026-08-28** (worktree, not yet merged), gated on the same
`LAMBDA_COW_CAPTURE` switch: `error[E232]`, raised in `build_ast` so both tiers
share it. Two corrections fell out of building it, recorded in the design doc
§6.1: (a) the check must DEFER to end-of-function, because read-modify-**write-
back** (`p = w.pkts[i]` … `w.pkts[i] = p`) is the sanctioned C4.2e idiom and is
indistinguishable from the bug at the mutation site — a mutation-site check
rejects `awfy/richards3.ls`, the model's own worked example; (b) the migration
is **nine** scripts, not four. The extra five (`proc_markup_mutation`,
`proc_param_typed_container_write`, `proc_view_mutable`,
`typed_map_write_child_ownership`, `r7rs/mbrot2`) still work today only because
insertion capture marks named values only, so containers filled with fresh
values still hand back borrowable children. `proc_view_mutable` is the notable
one: it pins `var row = m[1]` as a write-through view *binding*, which S9.2.2
already forbids, so CW24 enforces part of Stage-2's CW16.3 confinement early —
and that family needs CW25 before it has a legal spelling.

**CW25 implemented 2026-08-28** (same worktree, same flag). Path borrows
(`f(var m.rows[i])`) now detach the whole spine before the call on BOTH tiers,
via one new runtime helper `cow_path_borrow` plus a hook at each tier's
argument site. Before this they aliased — a write through `m.rows` reached the
original binding, a standing violation of the ratified S9.2.2 ("a mutable
borrow over shared storage un-shares first"). Verified at depth 1 and 2 on both
tiers; no new test failures (still exactly the 9 E232 from CW24), and the view
family's migration is proven: `write_row(m[1])` produces the `99 5 88` that
`proc_view_mutable` expects.

The design's specified third step — install the leaf back on return — turned
out to be **unnecessary** and was dropped (design doc rev 4). Both tiers run
the borrow protocol as detach-then-mutate-in-place, and `var` parameters use
the in-place checked setters, so a detached leaf is already installed where it
belongs.

**`E207` closed 2026-08-28**: annotated path borrows (`pn f(var r: any[])`
called as `f(m.rows)`) now compile and borrow on both tiers. The exact-match
rule for `var` arguments was NOT relaxed — it exists because a callee writes
through the borrow and must not see a mismatched representation. The real
defect was that a place's node type is `any` (a member read does not propagate
its field's declared type, TIG1), so the check compared against a type nobody
had computed. It now resolves the declared type *through the path* via
`declared_compound_destination_type` — the walker the assignment side already
uses for annotated destinations — before reporting. A genuine mismatch
(`var r: int[]` against a declared `any[]` field) is still rejected. This
covers annotated roots only; general TIG1 carrier-read propagation stays open.

**Corpus migration 2026-08-28: all 9 done; the flag-on suite is 784/784.** Goldens
unchanged in every migrated case, each passing with the flag on and off:
`r7rs/mbrot2` + `proc_fill_gc_nested` → path writes; `proc_view_mutable` → a
`var`-parameter borrow (the CW25 spelling S9.2.2 requires of a write-through
view); `proc_param_typed_container_write`, `typed_map_write_child_ownership`,
`proc_markup_mutation` → read-modify-write-back (C4.2e).

The remaining three were stopped deliberately, as they are structural rather
than spelling problems (design doc §B.1). `awfy/cd2_orig` needs a cascading
`var`-signature migration through every caller — attempted and reverted, and it
is also the *comparable source* perf control for `cd2`. `awfy/deltablue` and
`deltablue2` are constraint graphs needing the C4.2e handle-store rewrite.
**`deltablue2.ls` has since been ported** (in place, golden unchanged): one `w`
world owns `w.vars`/`w.cons`, every Variable-valued field (`out`, `v1`, `v2`,
`sc`, `off`) holds a variable id, constraint lists and plans hold cids, planner
state moved onto the world, and `w` is the single `var` parameter. Passes with
the flag on, both tiers, zero `E232`. **`deltablue.ls` followed**, derived from
that port with its annotations stripped, so the typed/untyped pair still
differs only in signatures (138 lines, all annotations).

**`awfy/cd2_orig` completed once NM-O8's untyped arm was fixed** — trie path
writes plus `var` on the eight genuinely-mutating parameters; no cascade into
callers was needed after all. Correct on both tiers in both flag states, and it
runs within noise of the original (~40.3s vs ~39.7s debug), so its role as the
`cd2` perf control is intact. It is a heavy test that intermittently times out
under the suite's parallel load in a debug build (known flakiness — it passes
standalone in 38s); that is unrelated to this work.

Two engine findings fell out, both pre-existing: **NM-O8** — a nested path
write through a *plain* `pn` parameter was not published to the caller while a
flat one was (both tiers agreed) — now **fixed for the untyped arm** via
`cow_path_set_inplace`, selected on `is_var_param || is_proc_param`. The typed
arm was tried and reverted: its transactional publish *converts* (3.5 into an
`int` field becomes 2) and an in-place write has no candidate to convert, which
`proc_type_numeric_structural_admission` caught. Also a T0
scratch-planner under-budget for the nested-path assignment branch
(`interp: scratch overflow depth=5 cap=5`, write silently dropped), reproduced
on pristine master and **fixed** here.

Original record (behavior with the flag unset) follows.

Probed 2026-08-27 on `ba7ce817c`, interpreter and `LAMBDA_TIER=jit` alike.
`S9.3.1` rules that placing a value into a container captures it **by value** at
every constructor and insertion point; none of them do:

| Probe | Result | Ruled |
|---|---|---|
| `var t={n:1}; arr[0]=t; t.n=55; arr[0].n` | `55` | `1` |
| `var u={n:1}; var lit=[u]; u.n=66; lit[0].n` | `66` | `1` |
| `var b={n:1}; a.peer=b; b.n=99; a.peer.n` | `99` | `1` |
| `var c={n:1}; var d={peer:c}; c.n=77; d.peer.n` | `77` | `1` |

Binding copy (`S9.1.2`) *is* enforced — `var b = a; b.n=99` leaves `a.n==1` —
which is exactly what makes this hard to see: copy-on-bind works, so the model
looks live until a value goes into a container. The spec carried `S9.3.1`
**unmarked** (i.e. believed implemented) until this pass; now `*` with a
fixed-archive record.

Two consequences beyond the direct violation. Cycles are constructible today:
`var a={name:"a",peer:null}; var b={name:"b",peer:a}; a.peer=b` builds a real
cycle, proved by `a.peer.peer.name = "MUTATED"` changing `a.name` — the path
walks back to `a` itself. `print(a)` on that two-node graph emits 40,002 bytes,
terminating on a depth cap rather than on structure. So the totality `S9.1.5`
derives from "cycles are unconstructible" does not hold of reachable state. And
the benchmark corpus depends on the defect: `test/benchmark/{awfy,jetstream}/richards2.ls`
require `sched.tl` and `task_table[identity]` to observe one TCB, and compute
their expected `qpc=2322 / hc=928` only under aliasing. Fixing `S9.3.1` breaks
those scripts, which is the migration `C4.3` accepted; the sanctioned rewrite is
the handle store (`C4.2e`, [`doc/Lambda_Procedural.md`](../doc/Lambda_Procedural.md)
§"Sharing Mutable State"). `test/benchmark/awfy/richards3.ls` is that rewrite,
already landed and passing with identical counts on both tiers — so this fix has
a ready-made conformance fixture: `richards3.ls` must keep passing when `S9.3.1`
lands, and `richards2.ls` is expected to stop.

Sequence with COW Stage 2 (`S9.1.3` snapshot params, listed in the same
fixed-archive record) — the two share the insertion/argument copy path, and landing
one without the other leaves a half-aliasing model that is harder to reason
about than either endpoint.

<a id="lr12-7"></a>**LR12-7 · The procedural surface is thin and ad hoc · OPEN**
IO procedures are a hand-curated set in one file with bespoke validation per
procedure; there is no general effect/capability system, so adding a
network-write or process-spawn procedure means another bespoke `pn_*` plus a
registry row.

---


## 13. Schema validator (LR_13)


<a id="lr13-2"></a>**LR13-2 · Inconsistent `max_depth` defaults · OPEN**
`SchemaValidator::create()` sets 1024 (`doc_validator.cpp:139`),
`default_options()` sets 100 (`:779`), and the CLI sets 100
(`ast_validate.cpp:444`, `:464`, `:626`) — three ceilings for one bound.

<a id="lr13-3"></a>**LR13-3 · Fragile root-type selection · OPEN**
The schema root type is chosen by raw text-scanning of the schema source plus a
hard-coded filename map, with fixed `char cwd_path[1024]`-class path buffers
that truncate (`ast_validate.cpp:324`ff).

<a id="lr13-4"></a>**LR13-4 · Hard-coded caps with silent truncation · OPEN**
`MAX_UNION_TYPES = 32` silently drops members of larger unions
(`validate_pattern.cpp:478`–`482`); the reporting path array is `[100]`
(`error_reporting.cpp`); `type_info[]` is assumed size 32 so any `TypeId ≥ 32`
renders `"unknown"` (`validate_helpers.cpp`).

<a id="lr13-5"></a>**LR13-5 · Unenforced options · OPEN**
`strict_mode`, `allow_unknown_fields` / `--allow-unknown`, and
`allow_empty_elements` are parsed and printed
(`ast_validate.cpp:198`, `:201`, `:442`–`457`, `:593`) but largely not acted on;
the fixed-length array check is commented out; warning merging exists
(`doc_validator.cpp:474`) but no code path ever emits a warning.

<a id="lr13-6"></a>**LR13-6 · Placeholder helpers · OPEN**
`extract_type_from_ast_node` is "Phase 1, basic type extraction"
(`doc_validator.cpp:266`–`271`); `is_item_compatible_with_type` is a bare
`item.type_id() == type->type_id` (`:431`–`436`); `format_type_name` returns the
literal `"unknown"` (`error_reporting.cpp:341`–`344`).

<a id="lr13-8"></a>**LR13-8 · The validator runs on the typed boundary's *success* path · OPEN (measured 2026-09-12)**
`lambda_type_matches` (`lambda/runtime/lambda-eval.cpp:1611`) routes union
contracts (`TYPE_KIND_BINARY`/`UNARY`) and shaped map/element contracts into
`runtime_validate_value_against_type`, and it is called from the **accepting**
path of `runtime_type_admit_value` (`:10606`). `SchemaValidator::validate_type`
therefore runs per admitted value, not only to build a rejection diagnostic —
contrary to D3.2.2's "deep, on first crossing". Confirmed by `sample` stack
(`lambda_type_check` → `SchemaValidator::validate_type` →
`validate_against_type` → `validate_binary_type` → `validate_against_union_type`);
self time 18.7% prettier_ast, 19.7% splay, 13.3% three_way_merge, 11.2%
richards, 8.8% log_pipeline. `prettier_ast` reports 6,615,043
`union_admit_calls` per run. The existing memo
`runtime_union_map_rep_proves_cached` (`:10573`) is entered only for
`LMD_TYPE_MAP` values and never records a **disproof**, so an unprovable
candidate re-runs the relation and the validator on every crossing.
S11.4.1v3 grants the proof-reuse licence; D3.2.4v3 supplies the elision test.
Fix tracked in [Tune27 M1/T27-1](impl/Lambda_Impl_Tune27.md).

<a id="lr13-7"></a>**LR13-7 · `printf`/emoji output in production paths · OPEN**
Contrary to CLAUDE.md rule 4, `ast_validate.cpp` has 59 direct `printf` calls
and `error_reporting.cpp` 6, writing to stdout with emoji rather than through
`log_*`. Also `error->actual.item` truthiness treats a `0`/null actual as
"absent", which can misreport a legitimately-null value.


## 13.1 Ledger hygiene observations (not issues)

These records are retained for provenance but are excluded from the counts
above. The absence of source markers is not evidence that a structural defect
is absent; active rows must be found by behavior and ownership analysis.

<a id="lr12-1"></a>**LR12-1 · The validator test surface is not a baseline gate · PARTIALLY RESOLVED 2026-09-05 (found 2026-09-03)**

The hosted `lambda-runtime-full` DSO resolves `ItemNull` and `g_lambda_home`
from its executable host. Every test target that links that DSO now compiles
retained references to both globals, so static-archive extraction supplies them.
All ten current validator executables load and enumerate their GTests. The
post-startup SIGSEGV was a second link-closure defect: the direct AST reducer
calls `parse_type_pattern_text_span`, but `lambda-runtime-full` omitted
`runtime/parse_type_pattern.cpp`; macOS dynamic lookup therefore supplied a
null reduction target. Including that translation unit restores the call, and
the enabled `test_validator_gtest` cases complete without a crash.

The selected-root lookup is also repaired. The loader still expected a retired
`AST_NODE_ASSIGN`, while the direct parser canonically emits an
`AST_NODE_VARIABLE_DECLARATOR` marked `is_type_definition`; it now registers
that form (and named patterns) after shared `TypeType` unwrapping. The shipped
`validate` invocation reaches genuine validation rather than `REFERENCE_ERROR`.
It currently reports its independent `element`-versus-`map` mismatch. Root
*selection* remains the separate open LR13-3 CLI-contract issue: it still uses
the raw textual scan and filename map (`validator/ast_validate.cpp:270`–`312`).

**Why it went unnoticed:** the validator gate runs from `test-lambda-full` (`Makefile:1624`), not `test-lambda-baseline`, so both baselines stay green over this unexercised surface. That is the finding worth keeping — a gate outside the baseline is a gate nobody runs.

Not attributed. The `elmt code clean up` commit (2cdcc1ea1) touches none of the files involved — not `build_lambda_config.json`, not `lambda-data.cpp`, not `runner.cpp`, not any validator or test source. The two remaining candidates are the object-redesign commit (da7a97b13), which reshaped `lambda.h`/`lambda-data.cpp` heavily, and the merged upstream `DOM: seven linkage lies and a dead local` (dbe7b7dba). Confirming which needs a from-scratch build of an older tree; a worktree attempt stalled on re-fetching vendored `re2`.

**Consequence for the specs:** the D2.6.6v2 content-arity claim can only be read from the code, not run. `validate_against_element_type` does enforce `content_length`, and an element-kinded nominal type reaches that arm by tag, so the old "not implemented" note is wrong — but "conformant" cannot be asserted until this is fixed. Both conformance rows now say exactly that.


<a id="lr03-7"></a>**LR03-7 · Latent, not annotated · OBSERVATION**
The core value-model files carry no `TODO`/`FIXME`/`HACK`/`XXX` markers; the
issues above are structural and will not surface in a tag grep.

<a id="lr04-8"></a>**LR04-8 · No literal `TODO`/`FIXME` markers · OBSERVATION**
The number and datetime concerns are expressed only as "for now" /
"far more than needed" comments.

<a id="lr05-8"></a>**LR05-8 · No `TODO`/`FIXME` markers · OBSERVATION**
The string and vector concerns have no source-level marker.

<a id="lr09-7"></a>**LR09-7 · No tags in source · OBSERVATION**
The registry caveats carry no `TODO`/`FIXME`/`HACK`; they are discoverable only
by reading the commented-out block, the "transpiler special case" notes, and
the `NULL` pointers.

<a id="lr10-6"></a>**LR10-6 · No source-level markers · OBSERVATION**
The error-handling concerns have no source-level marker.

---


## 14. Sibling vibe ledgers (TS, Issues8)

> Issues raised in the sibling `vibe/Lambda_Issue*.md` docs rather than in an
> `LR_*` Known-Issues section. **IDs are kept as their owning doc assigns them**
> (rule 17: no new ID series); those docs stay the detail record and this section
> is the index. Verified 2026-08-25 unless noted.

<a id="ts-3"></a>**TS-3 · `int[]`/`float[]` on a *local* is a 3–5x regression · OPEN (needs re-measurement)**
`impl/Lambda_Issue_Type_Support (retired).md`. The cited cause has moved — the
`var_tid = LMD_TYPE_ANY` assignment and its *"treat as ANY"* comment are gone
from `transpile-mir.cpp` — but the regression itself was not re-measured, which
needs a release build and the typed benchmark column.

<a id="ts-4"></a>**TS-4 · A named map type on a *local* is a COW value root, not a borrow · OPEN (not re-verified)**
`impl/Lambda_Issue_Type_Support (retired).md`. Carries both a performance claim (raytrace3d2
120 s → 80 ms when the annotations are stripped) and a **correctness** one
(splay2 collapsing to 1 node instead of 8000 because rotations mutated copies).
The correctness half overlaps the map-aliasing-vs-reification rule.

<a id="ts-6"></a>**TS-6 · Binding a map literal to a local kills region allocation · OPEN**
`impl/Lambda_Issue_Type_Support (retired).md`. Structurally unchanged, only relocated:
`mir_region_producer_candidate` is now `transpile-mir.cpp:888` and delegates to
`mir_region_producer_node`, whose switch handles only CONTENT/LIST/BLOCK,
IF_EXPR, RETURN_STAM and MAP — with no `AST_NODE_VAR`/`AST_NODE_LET` case, a
`var`/`let` in the body still falls to `default:` and disqualifies the function.


<a id="s16-9-5-gap"></a>**S16.9.5 · `a?: T` optional-field marker · PARTIAL (parsing fixed 2026-08-25)**
Found while trying to write a schema for the Issues8 explicit-null attribute
entry. S16.9.5 says the marker "applies in every type-field position", and the
spec shipped it **unmarked** on a 2026-08-22 spot-check — but two of the three
named positions did not parse:

```
fn f(a: int, b?: int)     parameter        OK
type R = {a?: int}        map-type field   error[E103]  <- now fixed
type E = <e a?: int>      element attr     error[E103]  <- now fixed
```

**Fixed:** both field sites now read the marker through one shared helper in
`parse_type_pattern.cpp`, and the validator honours it — the fixture
`test/validator_test_data/maps.ls`, which contains `optional?: int`, no longer
reports an invalid-type-pattern error against itself. Covered by
`test/lambda/optional_field_marker.ls` on both tiers. Note the subtlety that
made the first attempt silently useless: `is_type_optional()` reads `type->op`
on the `TypeUnary`, not the AST node's `op`; a wrapper that sets only the AST
side parses but is invisible to every consumer.

**Residue (why this stays PARTIAL):**
1. The marker is carried by wrapping the field type in `OPERATOR_OPTIONAL` — the
   same representation `a: T?` produces — so the two spellings S16.9.5 calls
   *distinct* ("field may be absent" vs "field present, value nullable") are
   indistinguishable downstream. Separating them needs a field-level flag on
   `ShapeEntry`; that was not invented here.
2. The declaration binding checker treats an optional field as required:
   `type Rec = {name: string, opt?: int}` with `let v: Rec = {name: "a"}` gives
   `error[E205]: missing required field 'opt'`. This is **pre-existing and not
   specific to the new marker** — `opt: int?` behaves identically — so the
   binding path ignores field optionality for every spelling.

`Lambda_Formal_Semantics.md` is v15.1.2 with the footnote narrowed to this
residue. Fixing the parse also closed the Issues8 entry it was blocking
("Explicit null Mark attributes fail optional schema type checks"): with an
element schema using `fontname?: string`, explicitly-null attributes validate,
absent attributes validate, and a real type violation still fails.


<a id="i8-genafterlet"></a>**Issues8 · A comprehension generator may not follow a `let` clause · OPEN (design question, not a defect)**
Clause order is fixed: **all generators, then all `let`s**. Measured
2026-08-25:

| Form | |
|---|---|
| `for (v in vs, let k = v)` | accepted |
| `for (v in vs, w in vs, let k = v)` | accepted |
| `for (v in vs, let k = v, let j = k)` | accepted — lets chain, each sees the previous |
| `for (v in vs, let k = v, w in vs)` | `error[E100]: expected let clause` |
| `for (let k = 1, v in vs)` | `error[E100]: expected a for binding name` |

**This is documented and deliberate**, contrary to this entry's first draft,
which called it unruled after checking only S14.1 (group-by and joins).
`doc/Lambda_Expr_Stam.md:726` gives the grammar —
`for (<bindings> [, let <name> = <expr>, ...] [where <cond>] [order by <spec>]
[limit <n>] [offset <n>]) <body>` — and states the model: *"Clauses are
processed in logical order: bindings → let → where → order by → offset → limit
→ body."* The parser enforces exactly that pipeline.

**What remains open.** The fixed order cannot express one shape: compute a key
from the current item, then iterate what that key yields —
`[for (v in vs, let k = f(v), e in entries(v, k)) e]`. The workaround is a helper
(`entries_for(v)`) that exists only to satisfy clause order. Two answers are
coherent; the choice is a design call, not a bug fix.

---

**Option A — Keep it.** The fixed phases are the feature. `bindings → let →
where → order by → offset → limit` is a pipeline, and each stage having a single
well-defined input is what makes `where`, `order by` and `limit` compose
predictably: `where` filters *after* every binding exists, `order by` sorts a
settled row set, `limit` counts settled rows. Interleaving generators with `let`s
makes "what does `where` see?" depend on clause position rather than clause kind,
and the logical order stops being statable in one line. The cost is a helper
function in the one dependent-iteration case — real, but small and local.

*If chosen:* promote the reference-doc prose to an `S#` ruling so the order is
normative rather than descriptive, and extend the diagnostic from
`expected let clause` to name the rule and the repair — the shape the element
and map-key diagnostics now use (see [i8-semidiag](<Lambda_Issue_Ledger(fixed).md#i8-semidiag>),
[i8-dqdiag](<Lambda_Issue_Ledger(fixed).md#i8-dqdiag>)).

**Option B — Relax it.** Allow a generator to follow a `let`. The dependency
direction is already strictly left-to-right *within* the pipeline — `let j = k`
proves a clause may read an earlier one — so a generator reading an earlier `let`
introduces no new kind of dependency, only a new position for an existing one.
On that reading the restriction is a grammar artefact rather than a semantic
boundary, and the helper function it forces is pure ceremony. The logical order
would be restated per-clause ("each clause sees every clause to its left")
instead of per-phase, which is arguably simpler, not more complex.

*If chosen:* the `where`/`order by`/`limit` tail must stay phase-ordered — only
the `bindings`/`let` prefix interleaves — or the composability argument in
Option A genuinely breaks. Rule that boundary explicitly; do not let it be
inferred from the parser.

---

**Either way**, two things are owed: there is currently **no `S#` ruling** for
clause ordering (only `doc/Lambda_Expr_Stam.md` prose), and the diagnostic
reports *what* but not *why*. Option A makes both a small documentation and
message change; Option B makes them a grammar change plus the same ruling.

**Half already fixed:** the diagnostic used to point at the *first* generator
(`Unexpected syntax` at `value in values`); it now names the expected clause kind
and points at the generator that actually conflicts.

## 15. Design gaps inherited from the retired Outstanding rollup (OI)

> `vibe/Lambda_Issues_Outstanding.md` was **retired on 2026-08-25** and archived
> as `vibe/impl/Lambda_Issues_Outstanding (retired).md`. Its §3 (Lambda core,
> `LR_01–13`) was verified fully subsumed by §1–§13 above — every MAJOR item it
> bolded resolves to a ledger entry, and the ledger carries more per section than
> the rollup listed. Its §2 (JO1–JO13) and §4 (JS_01–16) were pointer indexes
> into docs that still own them: `vibe/Lambda_Design_Stack_Frame_JS.md` (18 JO
> references) and the `doc/dev/js/JS_*.md` Known-Issues sections (present in all
> 17). What had **no other home** were the OI design gaps and the cross-cutting
> themes, indexed below; the archived file remains the full argument for each.
>
> **IDs keep their `OI-n` spelling** (rule 17). These are design gaps needing an
> ADR before code, not point defects — none was re-verified in the 2026-08-25
> pass unless noted.

- **OI-2 · JS object model: internal metadata + GC lifetime.** (a) The
  marker→shape-flag migration is half-done — class identity, accessors,
  iterators and Promise branding still ride `__class_name__`/`__ctor__`/`__arr__`
  string keys beside the typed `JsClass`/`ShapeEntry` scheme; (b) pools are never
  GC-reclaimed (JsFunction wrappers, generator pool with index collision on
  churn, promise pool with reactions capped at 8), and WeakMap/WeakSet/WeakRef
  have no weak semantics. Rider: map-field tombstones and sparse-array holes are
  two conventions over one concept — unify when the representation work happens.
- **OI-3 · ESM correctness.** Named-import live bindings are snapshot-only;
  circular ESM sees placeholder `undefined` instead of a TDZ ReferenceError; TLA
  is first-await-only; `js_await_sync` busy-drains. The Lambda side has the same
  gap from the other direction — cross-language import skips `pub` vars
  ([LR01-11](#lr01-11)). One cross-language design.
- **OI-4 · RegExp semantics.** RE2 leftmost-longest ≠ JS leftmost-greedy;
  heuristic routing can silently yield wrong captures; the backtracking engine
  bails to "no match" at its 8M-step budget. Needs an explicit decision: own
  backtracking engine as primary, vs proven-equivalence routing.
- **OI-5 · MIR value-representation contract (MIR Direct).** No single canonical
  type↔representation contract per boundary. Casualties: INT64 arithmetic never
  native, FLOAT→INT widening truncating in loops, indirect/closure calls past
  three arguments returning wrong values, and errors silently coercing to
  `0`/`0.0`/`false` when unboxed.
  *Implementation note (2026-08-28, D2.4.1–D2.4.3):* the L0–L4 first slice is now
  present in the shared MIR metadata and Lambda adapter. Arithmetic, branch, binding,
  index, call, and return consumers use explicit carriers; direct identity/axis/fail-closed
  transition fixtures are landed; and semantic `MIR_reg_type()` probes are gone from Lambda
  expression lowering. Remaining raw producers and the final legacy-shim ratchet stay open.
- **OI-6 · Codegen quality cluster (JS).** Destination-passing lowering
  (66–88% of emitted MIR is MOVs); shape-based polymorphic inline caching;
  de-pointered relocatable MIR (~59 baked realm pointers) blocking artifact
  caching. The **PIC design record** is the substantive part and survives in the
  archive: the single-tier/no-patching/no-deopt constraints force a *data-driven*
  side-table cache (`{TypeMap* shape, void* target, uint32_t guard_version}` × 2
  ways, module-owned so it is realm-scoped by construction — the flaw in the
  reverted process-global prototype cache). Open decision: invalidation
  granularity, per-realm version (cheap, thrashes under test262 prototype
  mutation) vs per-shape counters (+8 B per TypeMap; recommended). Companion:
  re-key duplicate-class-name deopt by constructor/`TypeMap` identity rather than
  class-name strings.
- **OI-7 · Node compat majors.** Async `fs` runs synchronously inline; stream
  internals are stubs (K27 shared stream core is the settled fix); `vm` does not
  isolate (security-relevant); crypto lacks asymmetric primitives.
- **OI-8 · DOM fidelity.** No on-read layout flush, so mutate-then-read
  `offsetWidth` sees stale pixels; framework-blocking API gaps fail as silent
  `undefined`; O(n) listener/wrapper storage degrades quadratically; no text
  segmentation or Bidi.
- **OI-9 · Unboxed scalar storage in maps and arrays · DEFERRED by decision.**
  Shaped slots and array elements as a guaranteed, inline-addressable raw
  representation. Two rulings were **decided 2026-07-16 and must not be
  relitigated casually**: (1) **no in-band tombstones in unboxed scalar storage,
  for any type** — absence is always out-of-band, delete/uninitialized forces a
  transition back to boxed, holey arrays stay boxed; (2) **adopt the ArrayNum
  raw-storage discipline** (element-width-aware compaction, data buffers never
  scanned as Items) rather than inventing new rules. Open: transition policy on a
  non-conforming write, write-path blast radius, GC/shape coherence during
  transition, and whether scope is fields-only or fields + elements-kind.

### 15.1 Cross-cutting hygiene themes

One policy each, not per-site fixes.

- **Silent fixed caps with inconsistent failure modes** — closure captures 16,
  generator states 63, promise reactions 8, TypeMap hash 32, union types 32,
  module vars 2048/1024, regex groups 256, and more. One grow-or-error doctrine
  retires the class. Ledger instances: [LR01-5](#lr01-5), [LR11-4](#lr11-4),
  [LR13-5](#lr13-5).
- **Layout-coupled raw offsets** — resolved for module binding by removing the
  unreachable `init_module_import` walk ([LR01-8](<Lambda_Issue_Ledger(fixed).md#lr01-8>)); GC
  trace/compaction is resolved by [LR08-5](<Lambda_Issue_Ledger(fixed).md#lr08-5>).
- **One masked memory-safety bug** — the event-loop SIGSEGV band-aid remains;
  the `sys://` map-walk segfault workaround was replaced by the shape-aware
  traversal in [LR01-R3](<Lambda_Issue_Ledger(fixed).md#lr01-r3>).
- **`SysFuncInfo` registry expressiveness** — data-driven argument/return
  conventions would delete inline special-casing ([LR09-1](#lr09-1),
  [LR09-2](#lr09-2)).

### 15.2 Settled designs awaiting implementation

No new decisions needed; each has an owning design doc.

| Work | Design doc | Unblocks |
|---|---|---|
| Unified AST Phases 0–5 | `Lambda_Design_Unified_AST.md` (U1–U26) | shared emitter/inference, guest ports, OI-5 partially |
| K27 shared stream core | `Lambda_Design_Concurrency.md` §11 | OI-7 streams, fs/event-loop integration |
| De-pointered MIR P1–P5 | `Lambda_Design_MIR_Cache.md` (MC1–MC8) | OI-6 artifact caching |
| JS threading P1–P3 | `Lambda_Js_Thread.md` (JT1–JT7) | worker isolation/watchdog; feeds `vm` realm isolation |
| Concurrency Stage A/B | concurrency v3 (K11–K18) | real suspension; actor/mailbox K20 |
| Stack-frame Python port | `Lambda_Design_Stack_Frame_Python.md` (PS1–PS10) | PO1–PO6 |

---


# Appendix B — Cross-cutting clusters

Several ledger entries are one defect wearing different masks. Fix them
together, not individually.

| Cluster | Entries | Root |
|---|---|---|
| **Honest static types** | LR07-7, LR08-3, LR07-13 | The collector trusts the transpiler's type classification: `should_gc_root_var` roots a JIT local only where the static type says heap-capable, and nothing proved that matches the runtime bit pattern. Verified 2026-09-10 — the former LR11-6/LR12-3 faces were a misreading of dead code and are archived; the surviving TCO face is the unused `is_tco_function_safe` proof, now under LR07-13. A `LAMBDA_ROOT_WITNESS` probe (see LR07-7) now tests the root half at run time. Fix per CLAUDE.md rule 15 with precise `RootFrame`/`Rooted` ownership. |
| **Representation ↔ semantics coupling** | LR03-3, LR07-1, LR07-5, LR07-14 | Expression results carry no `ValueRep`; each consumer re-derives it. See [Result32 lane-parity + Tune19], [Compiling lane design]. |
| **`INT64_MAX` sentinel residue** | LR07-4 | [LR03-R4](Lambda_Issue_Ledger(fixed).md#lr03-r4) removed `INT64_ERROR`; index OOB still lands on the legitimate finite value `INT64_MAX` and must get an explicit failure channel. |
| **Silent-truncation caps** | LR01-5, LR01-6, LR03-2, LR05-6, LR07-11, LR08-6, LR08-10, LR11-4, LR13-4 | Every one of these fails by quietly dropping data rather than erroring. The truncate-vs-error inconsistency (LR11-4) is the clearest statement of the pattern. |
| **Surface syntax (S16) residue** | S16.9.5, i8-genafterlet, SO36, O3, §7.17 | S16.1–S16.6.7 are conformant on the harness (140/140 C, 135/135 Tree-sitter); S16.6.8/S16.6.9 (procedural blocks are not expressions; branch homogeneity) were ratified AND implemented 2026-08-24 in build_ast (E312); harness now 152/152 C, 135/135 Tree-sitter. SO36 (pn calls in expressions) is deliberately open. What remains is not the line-delimiter design but the type sublanguage and the paired `for`: forms that parse and then behave wrongly or inconsistently by position. See [Design_Syntax §6–§7](Lambda_Design_Syntax.md). |
| **Process globals** | LR01-12, LR12-6 | `g_template_registry` and `g_dry_run` block concurrent runtimes. See RG1–RG14 in [Runtime globals audit], RC1–RC8 in [Radiant concurrency design]. |

---

# Appendix C — Maintaining this ledger

1. **This file is the working list; `LR_*` sections stay as design record.**
   When an `LR_*` "Known Issues" section changes, mirror the change here with
   the same `LRnn-k` ID. IDs are stable — a resolved issue keeps its number and
   moves to the fixed archive with an `-R` suffix rather than being renumbered.
2. **Re-verify before acting.** Every `file:line` in this document was resolved
   against the dated verification passes above and will drift. Grep the quoted identifier,
   not the line number.
3. **Cite rulings by formal-spec ID** (CLAUDE.md rule 17): `S#` from
   `doc/Lambda_Formal_Semantics.md`, `D#` from `doc/Lambda_Formal_Design.md`;
   vibe ledger IDs (OI-#, TE-#, RG-#, RC-#, TIG#) only where no formal ruling
   covers the point.
4. **Do not close an issue from a doc edit alone.** A resolution needs a
   verified source anchor, as every fixed-archive entry has.
