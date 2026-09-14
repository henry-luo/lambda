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

### Closure verification pass — 2026-09-13

Four high-impact records were re-verified against the current tree and moved to
the fixed issue ledger: LR01-12 (`g_template_registry` is now owned by
`EvalContext`), LR07-7/LR08-3 (precise JIT-root classification and its collector
consumer now have fail-closed coverage), and LR12-9 (S9.1.2/S9.1.3/S9.3.1 COW
capture is unconditional; the escape hatch is retired). The root-witness
level-2 corpus sweep reports zero violations, and the COW fixtures pass under
forced GC and poison mode on JIT. LR13-8 (validator success-path work) and
LR12-6 (`g_dry_run`) remain active.

The same pass also moved the stale RESOLVED records LR09-30, LR08-12, and
LR08-11. The duplicate active notices for LR11-6 and LR12-3 were removed; both
were already archived as LR11-R6 and LR12-R3.

LR09-4 was also reclassified on 2026-09-13. **S7.1.1v3** and **S8.2.1v4**
define invalid member/index reads as `null`, while invalid writes take the hard
error channel. The former JIT float-OOB lane mismatch is resolved as LR07-10
in the fixed issue ledger.

### Numeric and native-ABI pass — 2026-09-14

LR03-3 and LR07-10 were reproduced and resolved without changing a ruling.
The retired JIT workaround helpers are gone; bitwise lowering now requires both
an integer semantic contract and an actual native integer carrier. Every native
double out-of-bounds path produces the nullable-float lane, as **S7.1.1v3**
requires. `bitwise_invalid_operands.ls`, the wide/sized bitwise fixtures, and
the nullable-float fixtures pass under eager JIT.

LR03-5 and LR04-5 remain PARTIAL. A fallible numeric conversion boundary now
protects float admission and typed-lane storage, and the float/decimal edges
are regression-tested; the legacy scalar ABI and general decimal spelling path
still need larger architectural work.

Counts:

| Source doc | Area | Open | Partial | Resolved | Total |
|---|---|---:|---:|---:|---:|
| LR_01 | Compilation pipeline, CLI & REPL | 7 | 2 | 0 | 9 |
| LR_02 | Parsing & AST construction | 2 | 4 | 0 | 6 |
| LR_03 | Value & type model | 3 | 1 | 0 | 4 |
| LR_04 | Numbers, decimal & datetime | 5 | 1 | 0 | 6 |
| LR_05 | Strings, symbols & vectors | 2 | 1 | 0 | 3 |
| LR_06 | C transpiler (legacy C2MIR) | 0 | 0 | 0 | 0 |
| LR_07 | MIR Direct transpiler & JIT | 9 | 0 | 0 | 9 |
| LR_08 | Memory management & GC | 6 | 0 | 0 | 6 |
| LR_09 | Runtime builtins | 3 | 0 | 0 | 3 |
| LR_10 | Error handling | 1 | 0 | 0 | 1 |
| LR_11 | Mark data API | 6 | 0 | 0 | 6 |
| LR_12 | Procedural runtime | 5 | 0 | 0 | 5 |
| LR_13 | Schema validator | 6 | 1 | 0 | 7 |
| TS / Issues8 / Lint / Issues0 | Sibling vibe ledgers | 5 | 1 | 0 | 6 |
| **Live total** | | **60** | **11** | **0** | **71** |

The active ledger now contains 71 live records, with the 64 previously counted
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
framing (LR07-3) now reads as a plain MIR Direct gap rather than a backend
divergence.
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

<a id="lr03-5"></a>**LR03-5 · `it2d` / `it2b` coercions · PARTIAL**
*Improved 2026-09-14:* `item_try_to_double` is now the fallible numeric boundary.
Float contract admission, typed-array construction/storage, interpreter typed
float literals, and `float(decimal)` use it, so a nonnumeric Item or failed
decimal conversion returns `ItemError` before it can become a lane value.
`it2b` remains deliberate: all numbers, including floats and NaN, are truthy
under **S3.1–S3.2**.
*Residue:* the legacy `it2d` scalar ABI has no error return and still maps an
unproven Item to NaN. It is retained for callers that have already established a
numeric source; migrating every such native/guest call to a fallible boundary is
separate work.

<a id="lr03-6"></a>**LR03-6 · JS accessor-pair tag overloading · OPEN**
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


<a id="lr04-5"></a>**LR04-5 · Float↔decimal round-trip via text is lossy and hot · PARTIAL**
*Improved 2026-09-14:* float spelling now probes at most `DBL_DECIMAL_DIG`
significant digits, the mathematically sufficient binary64 bound, rather than
21. Decimal→float accepts specials and in-range integral decimals directly and
has a fallible `decimal_try_to_double` boundary; it never silently substitutes
`0.0` on conversion failure. Subnormal and maximum-finite values round-trip in
the regression suite, satisfying **S4.7.1**.
*Residue:* mpdecimal exposes no direct binary64 import/export API. General
non-integral decimal conversion still needs its scientific spelling and `strtod`;
replacing that path requires a dedicated correctly-rounded converter or an
approved dependency.

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

<a id="lr07-5"></a>**LR07-5 · `get_effective_type` only narrows IDENTs to ANY · OPEN**
It does not catch every post-mutation type change, leaving a stale-type boxing
hazard for non-identifier expressions
(`transpile-mir.cpp:3879`, `mir_expr_carrier_type`).

<a id="lr07-6"></a>**LR07-6 · MATCH and vectorized-comparison results are forced boxed · OPEN**
To prevent callers re-boxing an already-boxed value and then dereferencing it as
a pointer.

<a id="lr07-8"></a>**LR07-8 · Bitwise ops are special-cased before generic dispatch · OPEN**
`band` / `bor` / `bxor` lower to a single MIR instruction and `shl` / `shr` are
guarded against out-of-range shift counts, hard-coded ahead of generic dispatch
because `SysFuncInfo` has no per-argument native-convention field. Paired with
[LR09-2](#9-runtime-builtins-lr_09).

<a id="lr07-9"></a>**LR07-9 · `uint8_t Bool` returns need masking · OPEN**
Runtime functions returning a `uint8_t` bool leave garbage in the upper 56 bits
of the MIR return register, so every bool box/unbox must call `emit_uext8`
(`transpile-mir.cpp:2796`, used `:3355`, `:8576`).

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

*Absorbed from [LR11-R6](<Lambda_Issue_Ledger(fixed).md#lr11-r6>) /
[LR12-R3](<Lambda_Issue_Ledger(fixed).md#lr12-r3>) on 2026-09-10:*
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

<a id="lr09-3"></a>**LR09-3 · Ordered comparison is deliberately partial · OPEN**
The scalar comparators enumerate numeric/datetime/string cases and return
`BOOL_ERROR` for other types, bool and null included
(`lambda/runtime/lambda-eval.cpp:1650`, `:1811`) — cross-family `<` is an error
while cross-family `==` is `false`, the Python-style split. The former
strict-equality and VMap key-domain residue is resolved in
[OI-1-R1](<Lambda_Issue_Ledger(fixed).md#oi1-r1>); the conversion-failure case
is retained as [LR04-4](<Lambda_Issue_Ledger(fixed).md#lr04-4>).

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

<a id="lr13-8"></a>**LR13-8 · Repeated successful validation of an unproven union member · PARTIAL (re-scoped 2026-09-14)**
*Resolved portion:* Tune27 removed the measured hot-path regression. A plain
unary `T[]` crossing now certifies its admitted carrier instead of re-walking
the whole array; a map whose trusted layout proves a union arm returns through
`runtime_union_map_rep_proves_cached` (`lambda-eval.cpp:10693`–`:10728`). The
current release evidence reports 6,615,034 cache hits from 6,615,040
`prettier_ast` union admissions, and the accepted-path validator cost is gone
from the former hot rows. `tune27_array_contract_union.ls` passes on interpreter,
auto, and eager JIT.

*Residue:* the memo records `MAP_CONTRACT_UNION_MEMBER_PROVEN` only. When a
dynamic/foreign map has no physical-layout proof but the schema validator still
accepts it, `runtime_union_map_rep_proves_cached` returns false without
recording that result; `runtime_type_admit_value` then reaches
`lambda_type_matches` (`:10842`), which invokes
`runtime_validate_value_against_type` (`:1665`). The first deep validation is
required by **D3.2.2**; subsequent crossings of that unchanged accepted value
still revalidate it. **S11.4.1v3** permits reuse, and **D3.2.4v3** defines the
physical-proof condition. The remaining work is a sound negative-proof or
post-validation certificate for stable untrusted shapes, with invalidation on
shape transition. [Tune27 M1/T27-1](impl/Lambda_Impl_Tune27 (done).md) records
why this residue was not measurable on the frozen benchmark rows.

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
- **OI-5 · MIR value-representation contract (MIR Direct) · RESOLVED 2026-09-14.**
  The canonical boundary is `MirValue`: it carries the full `Type*` contract and
  actual `ValueRep`; consumers request a carrier through `em_require_rep()`.
  Lambda expression lowering has no semantic `MIR_reg_type()` probe or raw-register
  expression shim. The historical truncation, boxed-result, and error-unboxing
  failures are resolved by the implementation records, including
  [LR07-1](Lambda_Issue_Ledger(fixed).md#lr07-1) and
  [LR07-4](Lambda_Issue_Ledger(fixed).md#lr07-4), under **D2.4.1–D2.4.3**.
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
| Unified AST Phases 0–5 | `Lambda_Design_Unified_AST.md` (U1–U26) | shared emitter/inference and guest ports; Lambda MIR Direct OI-5 is resolved |
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
| **TCO safety proof residue** | LR07-13 | The former root-classification faces LR07-7/LR08-3 are resolved and archived. The surviving TCO face is the unused `is_tco_function_safe` proof, now tracked independently under LR07-13. |
| **Representation ↔ semantics coupling** | LR07-5, LR07-14 | Remaining container and result-domain cases. Lambda expression lowering carries `MirValue`; see resolved [LR07-1](Lambda_Issue_Ledger(fixed).md#lr07-1). |
| **Silent-truncation caps** | LR01-5, LR01-6, LR03-2, LR05-6, LR07-11, LR08-6, LR08-10, LR11-4, LR13-4 | Every one of these fails by quietly dropping data rather than erroring. The truncate-vs-error inconsistency (LR11-4) is the clearest statement of the pattern. |
| **Surface syntax (S16) residue** | S16.9.5, i8-genafterlet, SO36, O3, §7.17 | S16.1–S16.6.7 are conformant on the harness (140/140 C, 135/135 Tree-sitter); S16.6.8/S16.6.9 (procedural blocks are not expressions; branch homogeneity) were ratified AND implemented 2026-08-24 in build_ast (E312); harness now 152/152 C, 135/135 Tree-sitter. SO36 (pn calls in expressions) is deliberately open. What remains is not the line-delimiter design but the type sublanguage and the paired `for`: forms that parse and then behave wrongly or inconsistently by position. See [Design_Syntax §6–§7](Lambda_Design_Syntax.md). |
| **Process globals** | LR12-6 | `g_template_registry` is now context-local; `g_dry_run` remains process-global and blocks per-run dry-run semantics. See RG1–RG14 in [Runtime globals audit], RC1–RC8 in [Radiant concurrency design]. |

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
