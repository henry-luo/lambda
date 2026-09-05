# Lambda Core Runtime — Central Issue Ledger

> **Consolidated from the per-area "Known Issues & Future Improvements" sections of
> [`doc/dev/lambda/LR_01`–`LR_13`](../doc/dev/lambda/LR_00_Overview.md)**, plus
> verified items from the former sibling `vibe/Lambda_Issue*.md` ledgers (§14)
> and the retired Outstanding rollup's design gaps (§15).
>
> **This is now the only issue ledger in `vibe/`.** Every sibling was reviewed on
> 2026-08-25 and retired to `vibe/impl/`; each keeps its detail and evidence,
> while its live residue is indexed here. Add new issues here, not to an archive.
> This is the single working list for core-runtime defects, caps, and structural
> hazards. The `LR_*` documents remain the *design* record; this ledger is the
> *issue* record.
>
> **Audience:** engine developers. **Status:** working ledger (`vibe/`), not normative.
> Semantic and design rulings are cited by `S#` / `D#` per CLAUDE.md rule 17;
> where no formal ruling covers a point, the vibe ledger ID is given.

## Verification pass — 2026-08-24

Every issue below was re-checked against the tree at `c568f0f93`. Three outcomes:

| Mark | Meaning |
|---|---|
| **OPEN** | Reproduced in current source; `file:line` anchors re-resolved. |
| **PARTIAL** | Some sub-claims fixed, a real residue remains. The residue is stated. |
| **RESOLVED** | Verified fixed or removed; moved to [Appendix A](#appendix-a--resolved-and-obsolete-issues). |

### Second pass — 2026-08-25 (sibling ledgers)

`vibe/Lambda_Issue*.md` were reviewed and their claims re-tested. Outcome:

| Doc | Result |
|---|---|
| `Lambda_Issues_Outstanding.md` | **Reviewed in full and RETIRED 2026-08-25** → archived as `vibe/impl/Lambda_Issues_Outstanding (retired).md`. Its §3 was verified subsumed by §1–§13; §2/§4 were pointer indexes into docs that still own them; its OI design gaps and hygiene themes moved to §15. One item was genuinely missing from this ledger — LR_12 #8, now resolved and moved to [LR12-R8](#lr12-r8). |
| `impl/Lambda_Issue_Type_Support (retired).md` | TS-1, TS-2, TS-7 verified **FIXED**; TS-5's dead-code half fixed; TS-9's C16 implementation has landed. TS-6, TS-8 confirmed open; TS-3, TS-4 open pending measurement → §14. |
| `Lambda_Issues8 (retired).md` | **All 28 entries triaged; 22 re-tested.** 17 fixed/closed, 9 open or partial (§14), 4 not re-tested (Radiant-retained, Structurizr fixtures, and an incremental-release build issue — each needs a fixture outside the core runtime). Earlier note: **Fixed:** unbraced scalar `if` in a block body; map literal after `if` (S16.4.1v2); multiline iterator + `where`; `list` as a for-binding (now a clear diagnostic); the double-quoted-key error cascade. **Ruled not a defect:** double-quoted map keys — the doc was wrong and is corrected. **Does not reproduce:** recursive params overwritten after descent. **Still open → §14:** dynamic map spread, element attribute spread, one-line Mark comprehensions, and the weak double-quoted-key diagnostic. |
| `Lambda_Issues5 (retired).md` | 7 entries re-tested. **Fixed:** postfix `^` in `let` (#4), chained comparisons (#5), string slicing (#11), `if`-expression value in a `pn` (#15), and §23's inline-`if` attribute value. **By design, not defects:** element-wise list `+` (#1), `let` reassignment rejected in a `pn` (#10). **Still open:** §23's attribute spread → §14. |
| `Lambda_Issues6 (retired).md` | 5 open entries re-tested. **Fixed:** bare map as an `if` branch (#31, via S16.4.1v2), the paren-comma branch form (#32), multi-line `++` (#33), and non-fatal parse errors (#35 — a malformed file now exits 1). **Not reproduced:** the MIR float-param inference failure (#34); it was "real only" and a synthetic reconstruction runs clean on both tiers. |
| `Lambda_Issues4_Lint (retired).md` | cppcheck re-run (2.17.1). E1 still stands and is now **invisible to the analyser** after the `malloc`→`mem_alloc` migration → §14. W3 (`alloca`) fixed where reported. Counts elsewhere are historical. |
| `vibe/impl/*(fixed).md` | Archives spot-checked. `Lambda_Issue_GC_Native_Rooting` genuinely resolved (its 107/244 figure is historical discovery data — a stale memory note quoting it as live was corrected). **`Lambda_Issues0 (fixed).md` is mislabelled**: #9 was deferred there and is now resolved in Appendix A; #15 and five more items remain without resolution. |
| `Lambda_Issues3 (retired).md` | A test-enhancement proposal, not a defect ledger — and **substantially implemented**: it reported `test/std/core/` missing with 19 tests in `test/std/`; there are now 157, of which 104 sit under `core/` across all four proposed subdirectories (target was 57). |

### Direct implementation pass — 2026-08-26

Seven reproduced implementation defects were fixed after root-cause review and
confirmation against the formal rules: LR01-1 (`S2.4.1v2`, `D3.4.1`), LR01-3
(`S1.8`), LR01-4 (`S2.4.2v4`), LR02-13 (`S12.1.4`, `S12.3.4`), LR05-4
(`S10.1.2`), LR12-2 (`S7.10.6`), and LR12-8 (`S9.1.1`, `S9.1.6`). They are
moved to Appendix A with the `-R` suffix. `make test-lambda-baseline` passes
**3914/3914** after the changes.

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
suite pass; the resolution records are in Appendix A.

Counts:

| Source doc | Area | Open | Partial | Resolved | Total |
|---|---|---:|---:|---:|---:|
| LR_01 | Compilation pipeline, CLI & REPL | 8 | 2 | 5 | 15 |
| LR_02 | Parsing & AST construction | 2 | 4 | 14 | 20 |
| LR_03 | Value & type model | 5 | 1 | 2 | 8 |
| LR_04 | Numbers, decimal & datetime | 6 | 0 | 1 | 7 |
| LR_05 | Strings, symbols & vectors | 2 | 1 | 6 | 9 |
| LR_06 | C transpiler (legacy C2MIR) | 0 | 0 | 9 | 9 |
| LR_07 | MIR Direct transpiler & JIT | 12 | 1 | 3 | 16 |
| LR_08 | Memory management & GC | 9 | 0 | 1 | 10 |
| LR_09 | Runtime builtins | 5 | 0 | 4 | 9 |
| LR_10 | Error handling | 1 | 0 | 4 | 5 |
| LR_11 | Mark data API | 8 | 0 | 1 | 9 |
| LR_12 | Procedural runtime | 7 | 0 | 2 | 9 |
| LR_13 | Schema validator | 7 | 0 | 1 | 8 |
| TS / Issues8 / Lint / Issues0 | Sibling vibe ledgers | 6 | 1 | 8 | 15 |
| **Total** | | **78** | **10** | **61** | **149** |

The 149 record total exceeds the 127 items in the original source sections for two reasons.
Two original entries each split into a resolved half and a surviving residue —
LR_03 #4 (sentinels → LR03-4 + LR03-5) and LR_05 #3 (two string orderings →
LR05-R2 + LR05-R3). And two defects were **found during verification** rather
than extracted: LR02-8 through LR02-10, each marked as such in place (a fourth, LR02-11, was fixed the same day and is now LR02-R6).

The 2026-08-28 ledger cleanup removes seven non-live records: five
source-marker observations and two duplicate index entries. The resolved
`LR05-2` classification remains in place because `LR05-R2` is already owned by
the separate split half of LR05 #3.

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
therefore obsolete, and every cross-doc "diverges from C2MIR" framing (LR07-3,
LR03-3) now reads as a plain MIR Direct gap rather than a backend divergence.
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
import-level batching (see [LR01-R1](#lr01-r1)).

<a id="lr01-6"></a>**LR01-6 · Fixed and non-reentrant static buffers · OPEN**
Module BSS name `char buf[256]` (`runner.cpp:565`); REPL synthetic path `char
script_path[64]` (`main.cpp:903`); the JS CLI thread stack is a 256 MB
`JS_CLI_STACK_SIZE` allocated per run (`main.cpp:264`, applied `:316`); and
non-reentrant `static char mir_error_msg[256]` (`main.cpp:1560`).

<a id="lr01-7"></a>**LR01-7 · Stateless REPL re-execution is O(n²) · OPEN**
The whole `repl_history` StrBuf (`main.cpp:785`) is re-transpiled and re-run
every turn, with error rollback implemented as a raw byte-truncate
(`main.cpp:882`–`893`). Any non-idempotent side effect repeats each turn.

<a id="lr01-8"></a>**LR01-8 · `init_module_import` pointer-walk is layout-coupled · OPEN**
`init_module_import` (`runner.cpp:556`) still advances `uint8_t* mod_def` over
the `Mod` struct by `sizeof()` arithmetic mirroring the transpiler's implicit
layout (`:574`ff). Any change to that layout, or to the `needs_fn_call_wrapper`
branch, silently corrupts function-pointer binding; the Lambda and cross-lang
JS branches must stay in lockstep. Constant/type-table init has since moved to
name-keyed `find_func("_init_mod_consts" / "_init_mod_types")` lookups
(`:634`, `:645`) — that half is no longer layout-coupled.

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

<a id="lr02-4"></a>**LR02-4 · `AstLoopNode` / `AstNamedNode` layout divergence · OPEN**
`AstLoopNode` (`lambda/runtime/ast.hpp:289`) still carries `index_name` between
`name` and `as`, where `AstNamedNode` has `as` directly after `name`. A
wrong-type cast reads the wrong field offset. Existing capture code casts
explicitly, but any new code handling loop nodes generically is exposed.

<a id="lr02-5"></a>**LR02-5 · `match`-arm `~` references were missed · RESOLVED 2026-08-25**
`has_current_item_ref` (`build_ast.cpp`) walked a match node's scrutinee and
then iterated the arm list with an **empty loop body**, falling through to
`false`. The loop was provably dead code.

**Observable failure:** a pipe never established the current-item context an arm
needed, so

```lambda
xs |> match (1) { case int: (~) * 10
                  default: 0 }        // was: error   now: [10, 10, 10]
```

evaluated to `error`. Only this shape broke — an arm whose enclosing `match`
carries no `~` in its *scrutinee*. `xs |> match (~) { … }` always worked,
because the scrutinee walk detected the reference.

**The arm PATTERN is deliberately not walked.** `doc/Lambda_Expr_Stam.md:961`
rules that `~` inside an arm body is the **matched value**, and a `that`
constraint's `~` is the match subject as well — both rebind, so neither can
consume an enclosing pipe's item. This mirrors the `HANDLER_EXPR` case directly
above, which already models exactly that shadowing.

*Worth recording for the next reader:* the correct result of the repro is
`[10, 10, 10]`, not `[10, 20, 30]`. The scrutinee is the constant `1`, so the
arm's `~` is `1` for every piped item. Reading `~` as the pipe item is the
natural first guess and it is wrong; the docs settle it.

Covered by `test/lambda/match_arm_current_item.ls` on both tiers — five shapes
including the constraint and no-pipe controls, and verified to fail
(`subject_is_const: error`) when the walk is emptied again.

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


<a id="lr02-9"></a>**LR02-9 · Binary `&` / `!` type operators rejected in annotation position · RESOLVED 2026-08-25**
Intersection and exclusion evaluated correctly as patterns but were rejected by
a declaration or parameter annotation, with a diagnostic that named the binding
rather than the contract. Both halves are fixed.

**Three defects, not one.** The chain broke in three places, and each had to be
found from the one before:
1. `static_boundary_relation` (`build_ast.cpp`) recognised a binary **target**
   only for `OPERATOR_UNION`; `&`/`!` fell through to the generic tail and were
   rejected outright. This was the actual capability gap.
2. The type-pattern parser lowered a type-level `&` to **`OPERATOR_OR`**
   (`parse_type_pattern.cpp`, with a comment calling it "odd" but reproducing
   it), so even after (1) the annotation carried an operator the boundary
   checker does not treat as a set operation. Normalised to
   `OPERATOR_INTERSECT`, matching expression space and the sibling site in the
   same file; consumers accept `OPERATOR_OR` as the historical spelling.
3. `lambda_type_format_name` rendered only `|`, so an `int & string` contract
   printed as the bare word `type` — the diagnostic half of this entry.

Also widened `promote_type_union_expr` so a type-set operator between two type
values builds a first-class binary type in expression position too.

**Semantics are unchanged and now agree across positions** — `1` is admitted by
`number & int` and by `int ! string`, and rejected by `int & string`, exactly as
`is` reports. The rejection of `let a: int & string = 1` is *correct*: nothing
satisfies that intersection. Diagnostics now read
`cannot initialize 'a' of type int & string with int` and
`argument 1 expected int & string, got int`.

Covered by `test/lambda/type_set_operators.ls` (pattern, alias, inline
annotation and parameter positions, both tiers) and
`test/std/negative/type_set_operator_mismatch.ls` +
`NegativeScriptTest.TypeSetOperatorContractIsNamed`. Closes the implementation
half of **SO9** and the `&`/`!`-unimplemented warning in the string-pattern
design record.

<a id="lr02-14"></a>**LR02-14 · Keyword-as-name handling is a patchwork; S16.10 rules it · RESOLVED 2026-08-27**
Ruled 2026-08-27 as **S16.10** (spec v16.0.0; deliberation and probe table in
`Lambda_Design_Syntax.md` §7.24): keywords never name bindings — the whole
lexer keyword table, E201 at the declaration site, no quoted escape — while
map keys, element tags, attribute names, and `.`-member steps admit keywords.
Divergences to fix:

1. `import edit: …` parses and **every use** fails (`expected a type
   pattern` — the `edit` declaration keyword captures the statement);
   `import 'edit': …` parses and creates an **unreachable binding**
   (`'edit'.x` is silently null). Both must become E201 at the import line.
2. `let if = 1` parses; every use fails (`expected an expression`).
3. **`let type = 1` parses and `type` then silently reads the base type** —
   a silent wrong answer, the priority defect of the cluster.
4. `<if a:1, "x">` is rejected (`expected an element tag`) but is legal
   under S16.10.2 — the tag position must accept keyword words.
5. E201 covers only `last` and must extend to the whole table, in the C
   parser and the Tree-sitter reference grammar alike.

Migration: ~55 keyword-named bindings in `test/` + `lambda/` (offset 12,
group 9, state 8, to 5, by 4, …; breakdown in §7.24); 0 keyword import
aliases.

<a id="lr02-15"></a>**LR02-15 · Sys-func shadowing; S12.3.7 rules it user-first · RESOLVED 2026-08-27**
Probes 2026-08-27 (debug build): `fn sum(a) => 99` then `sum([1,2,3])`
compiles, executes on the interpreter tier, prints **no result**, and dies at
teardown (ASan dealloc failure); `fn len` / `fn min` shadows likewise;
unshadowed `sum(x) + len(x)` is fine. Ruled 2026-08-27 as **S12.3.7** (spec
v16.1.0; deliberation in `Lambda_Design_Syntax.md` §7.25): user-first,
module-lexical shadowing with a mandatory compile warning; `pub` export
extends to importers through the explicit import only; a non-callable shadow
is the not-callable error, never builtin fallback; keywords/base-type words
stay un-shadowable (S16.10.1). Implementation: one resolution point in
`build_ast` covering both tiers ("is this name module-bound?" before builtin
registry lookup), the shadow warning, and a regression test for the
crash shape.

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

Why it matters: S8.2.1v3 now makes `obj["m"]` the dynamic form of `obj.m`,
reaching the type's methods. That is only safe because the builtin tier is
call-only — otherwise `m[key]` probing on a plain map would return a bound
builtin whenever `key` happened to spell one, instead of `null`. Any future
change that lets bare member access fall through to the registry silently
breaks that guarantee. Recorded as an observation, not a defect: nothing to
fix, but the property must not regress. [OB5, [Type_Object §16](Lambda_Type_Object.md)]

<a id="lr02-16"></a>**LR02-16 · `lambda.*` namespace not implemented · OPEN**
Ruled 2026-08-27 as **S17.2.1/S17.2.2** (semantics v16.2.0) and **D7.2.4**
(design v1.38.0); deliberation in `vibe/Lambda_Package.md` §1b. Work items:

1. **`lambda.sys.*`** — expose the sys-func registry as a built-in module so
   `lambda.sys.sum(xs)` resolves to the same row the prelude provides
   unqualified. This is what makes a shadowed builtin reachable (S12.3.7).
2. **Reserve the `lambda` root** — add it to the capture-real bar in
   `lambda_lexer_word_bars_binding` so `let lambda = …` is E201 and the
   escape can never be shadowed.
3. **Shorten package paths** — `lambda.package.<name>` → `lambda.<name>`
   across ~541 import sites plus the `lambda/package/` directory layout;
   built-in module aliasing so `import math` ≡ `import lambda.math`.
4. **Move the typesetting package** — `lambda.package.math` →
   `lambda.doc.math`, freeing `lambda.math` for the built-in module. Its
   corpus lives under `test/lambda/math/`.

Sequencing note: item 2 is a one-line change but adds a reserved word, so it
rides the same migration pass as [LR02-14](#lr02-14).

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

<a id="lr03-1"></a>**LR03-1 · `item_deep_equal` is a weaker second equality walker · OPEN**
`item_deep_equal` (`lambda/core/lambda-data.cpp:1346`) remains a shallower
walker than `fn_eq`, with a single caller — Radiant's no-op elision
(`radiant/event.cpp:2300`). Its missing cases (`MAP`, `DECIMAL`, `DTIME`,
`UINT64`, `RANGE`, `VMAP`) fall to pointer equality and err conservatively
there: a missed elision forces a spurious DOM rebuild, never a wrong answer.
Remaining work (tracked as **[OI-1](#15-design-gaps-inherited-from-the-retired-outstanding-rollup-oi)**):
reimplement over a *strict* `fn_eq` variant with cross-rank promotion disabled
(elision must not equate `1` with `1.0`), no `set_runtime_error` side effect on
the depth cap, and defined next to `fn_eq` so `LAMBDA_STATIC` input builds don't
pick up the dependency. Verify VMap key hashing agrees with `fn_eq` across
numeric ranks.

<a id="lr03-2"></a>**LR03-2 · Hard-coded capacity caps · OPEN**
`TYPEMAP_HASH_CAPACITY` 32 and `TYPEMAP_HASH_DYNAMIC_MAX_CAPACITY` 32768
(`lambda/lambda-data.hpp:346`–`347`) bound the per-map hash table; on saturation
lookups silently fall back to the O(n) shape chain. `NAME_POOL_SYMBOL_LIMIT` 32
(`lambda/lambda.h:77`) and `LAMBDA_TCO_MAX_ITERATIONS` 1000000 (`:83`) are
likewise fixed. `ArrayNumShape.ndim` is bounded 1..32 — see [LR05-1](#5-strings-symbols--vectors-lr_05).

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

<a id="lr03-4"></a><a id="lr10-5"></a>**LR03-4 · `it2l` error sentinel collides with a legitimate value · OPEN**
`INT64_ERROR == INT64_MAX` (`lambda/lambda.h:1261`); `it2l`
(`lambda/core/lambda-data.cpp:424`) returns it as the failure sentinel, so a
real maximum int64 is indistinguishable from a conversion failure. Cross-link:
the same collision is called out as `INT64_ERROR == INT_LANE_INF` in
[v5 int migration in flight] — treat as one issue.

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

<a id="lr04-1"></a>**LR04-1 · "Unlimited" decimal is a 200-digit cap · RESOLVED 2026-08-28**
Literal, string, and arena ingestion now parse coefficients exactly, and
decimal `+`, `-`, and `*` use a local maximum-precision context so exact
results can grow beyond 200 digits. `g_unlimited_ctx.prec = 200`
(`lambda/core/lambda-decimal.cpp:46`) remains only the extended context for
documented inexact operations such as division and power; it is no longer used
to cap source literals or exact arithmetic under **S4.6.1**/S4.6.2. Regression:
`decimal_tiers` covers a 350-digit literal and a 53-digit fixed-tier product;
the decimal baseline passes.

<a id="lr04-2"></a>**LR04-2 · BigInt still has practical caps · OPEN**
`bigint_precision_context` caps precision at 100000 digits
(`lambda-decimal.cpp:1224`, `:1594`) and shift helpers reject counts above
100000 bits (`:1773`, `:1803`); string ingest rejects above 100000 (`:1310`).
Implementation guardrails, not mathematical limits in the surface model.

<a id="lr04-3"></a>**LR04-3 · Trapping `mpd_get_ssize` can SIGFPE · OPEN**
`decimal_to_int64` (`lambda-decimal.cpp:1163`) and `decimal_mpd_to_int64`
(`:661`) use the **trapping** `mpd_get_ssize`, which can SIGFPE on overflow.
BigInt shift/pow paths use quiet extraction before narrowing; the decimal
conversion helpers remain an unhandled-crash risk on out-of-range magnitudes.

<a id="lr04-4"></a>**LR04-4 · `decimal_cmp` swallows conversion failure as equality · OPEN**
On a failed operand conversion, `decimal_cmp` returns `0` with the comment
`// error case, treat as equal` (`lambda-decimal.cpp:1004`), so a malformed
comparand compares **equal** rather than raising — a silent-wrong-answer path
feeding `decimal_cmp_items` (`:1023`). Also tracked under **[OI-1](#15-design-gaps-inherited-from-the-retired-outstanding-rollup-oi)**.

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

<a id="lr05-1"></a>**LR05-1 · `ndim` cap of 32 is unchecked in the helpers · RESOLVED (D1.9)**
`LAMBDA_ARRAY_NUM_MAX_NDIM` is the single rank cap. Construction, GC promotion,
and equality use it; `lambda/runtime/lambda-vector.cpp` now validates descriptor
rank at the shared shape/stride decode boundary before writing a caller's
fixed-rank buffer. Every vector, structural, reduction, mask, and image caller
converts a rejected descriptor to `ItemError`; direct native reduction returns
`NaN` after logging because its ABI is `double`. The regression injects an
out-of-range descriptor and verifies shape and matrix operations fail without
decoding it. This implements D1.9's malformed-input fail-closed rule.

<a id="lr05-2"></a>**LR05-2 · Not full UCA collation · RESOLVED (not a defect; ruled by S6.2.2)**
The former expectation was wrong: Lambda's normative total order is bytewise
UTF-8, with no locale collation or accent ordering. The utf8proc casefold path
is used only for markup tag/attribute matching. The stale comment at
`lambda/core/utf_string.cpp:57` should be corrected, but implementing UCA would
contradict **S6.2.2** rather than fix Lambda's operators.

<a id="lr05-5"></a>**LR05-5 · `fn_label` bypasses the runtime allocator with raw `malloc`/`free` · RESOLVED 2026-08-28**
The flood-fill stack was allocated with raw `malloc` and released with `free`,
so the operation bypassed the checked `memtrack` allocation contract and its
failure-injection path. The success path was leak-free, but the temporary
workspace was outside the runtime's ownership and failure accounting.

The stack now uses the existing `mem_alloc`/`mem_free` pair with
`MEM_CAT_TEMP`. This implements **D4.2.1v3** and lets allocation failure return
through the existing `ItemError` path, as required by **D4.2.2v2**. No new
data structure or design ruling was added.

Regression: `RuntimeShapeTransition.LabelStackAllocationFailureReturnsError`
arms `memtrack_fault_inject(0)` and verifies that `fn_label` reports the
workspace allocation failure. The complete representation suite passes 29/29;
`make test-lambda-baseline` passes 3977/3977.

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

**All nine issues are obsolete.** The backend no longer exists in the tree.
See [Appendix A · LR06-R1…R9](#lr06-r1r9).

---

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
yields `INT64_MAX`, itself colliding with [LR03-4](#lr03-4)).

<a id="lr07-5"></a>**LR07-5 · `get_effective_type` only narrows IDENTs to ANY · OPEN**
It does not catch every post-mutation type change, leaving a stale-type boxing
hazard for non-identifier expressions
(`transpile-mir.cpp:3879`, `mir_expr_carrier_type`).

<a id="lr07-6"></a>**LR07-6 · MATCH and vectorized-comparison results are forced boxed · OPEN**
To prevent callers re-boxing an already-boxed value and then dereferencing it as
a pointer.

<a id="lr07-7"></a>**LR07-7 · Precise-root correctness is type-driven · OPEN**
BUG-001's heap-frame growth hole is closed by static side-stack slots and
publish-before-call lowering. The remaining invariant: any register carrying a
heap-capable boxed value must retain a heap/ANY MIR type. `should_gc_root_var`
(`transpile-mir.cpp:1370`, used `:1954`, `:2008`) roots unknown/manual capture
entries pessimistically. Cross-link: [LR08-3](#8-memory-management--gc-lr_08),
[LR11-6](#11-mark-data-api-lr_11), [LR12-3](#12-procedural-runtime-lr_12) — one
honest-static-typing issue with four faces.

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

<a id="lr07-13"></a>**LR07-13 · TCO iteration ceiling · OPEN**
Tail-recursive loops emit a guard raising a stack-overflow error past
`LAMBDA_TCO_MAX_ITERATIONS` (`transpile-mir.cpp:24607`–`24611`); the interpreter
shares the constant (`interp.hpp:59`).

<a id="lr07-15"></a>**LR07-15 · Object methods read the receiver as zero on the eager JIT tier · RESOLVED 2026-09-03**
An SI3v2 tier-divergence: the same script yields different results under
`LAMBDA_TIER=jit` than under `interp`/`auto`. Implicit receiver-field reads
inside an object method body evaluate to 0 on the eager whole-module MIR path.
Probe (commit `ababcb674`, **before** any 2026-09-03 change — verified by
stashing): `test/lambda/object.ls` with `type Counter { value: int, fn
double() => value * 2, fn add(n: int) => value + n }` and `let c = <Counter
value: 5>` gives `c.double()` = **10** and `c.add(3)` = **8** on `interp`, but
**0** and **3** on `jit` — the `3` shows `value` itself reading 0, not the
multiply failing.

Companion symptom, same root: a `pn` method's mutation is lost. `pn bump() {
value = value + 1 }` on `<Counter value: 5>` leaves `c.value` = 5 under `jit`
and 6 under `interp` (`temp/probe_pn_call.ls`).

Why it was not caught: the baseline runs the default AUTO selector, which routes
these scripts to T0, so `object.ls` passed at 4079/4079 while the JIT path was
wrong. Any corpus tier-parity sweep must set `LAMBDA_TIER` explicitly.

**Root cause — one missing back-pointer.** `binding_node_set_entry`
(`build_ast.cpp:2180`) wrote the `NameEntry` back onto its declaring node for
`AST_NODE_VARIABLE_DECLARATOR` and `AST_NODE_PARAM` only. An object type's field
scope-helper is an `AST_NODE_KEY_EXPR` (`direct_object_add_field` and the
base-inheritance copy at `:6910`/`:6986`), so `field_ref->entry` stayed NULL and
the `shape->binding = field_ref->entry` beside it stored NULL — even though
`ShapeEntry::binding`'s own comment (`lambda-data.hpp:316`) says object-method
field lowering depends on it.

That NULL was invisible to T0, which resolves an object-field read by *name*
against `method_self` (`interp_read_binding`), and fatal to MIR, which matches
variables by *binding identity, not spelling* (`mir_var_for_ident`,
`transpile-mir.cpp:2209`). The method prologue loaded each field from `self` and
called `publish_var_binding(mt, field_name, se->binding)` with NULL, so the
locals were registered under no binding; every implicit read then fell through
`transpile_ident_value` to its "undefined variable" arm. The write half failed
the same way: the epilogue's write-back (`:25701`) looks the local up with
`mir_var_for_binding(field->binding)` and found nothing, so a `pn` method's
mutation was dropped.

**Fix:** admit `AST_NODE_KEY_EXPR` in `binding_node_set_entry`. One arm, both
halves — reads and the `pn` write-back — on both tiers. Fixtures:
`test/lambda/object_method_receiver.ls` (read, inherited fields, float
unboxing) and `test/lambda/proc/object_method_write.ls` (write-back); both are
byte-identical under `LAMBDA_TIER=interp` and `=jit`, as is `object.ls`.
Baseline 4082/4082.

*Measurement note:* the tier selector is the `LAMBDA_TIER` environment
variable. `./lambda.exe jit run f.ls` is **not** tier selection — `jit` consumes
`run` as the script name and the file never executes (`nodes=0`, prints
`null`). Two wrong conclusions in this investigation came from that form.

<a id="lr09-8"></a>**LR09-8 · `len(element)` violated the S8.3.1 length law · RESOLVED 2026-09-03 (USER ruling)**

S8.3.1v2 states the law — `len(x)` is the number of iterations `for (i in x)` performs — and gives `len(<e a:1, b:2, "t">)` = **3** as its own example. The element arm of `fn_len` returned the child count alone, so that expression answered **1** while `[for (x in e) x]` yielded three members. Ruled closed by the user: element length is attribute count plus content-item count, for structural and nominal elements alike.

Fixed in the ELEMENT arm of `fn_len` and in `fn_len_e`, the JIT's specialization for a statically-element argument — both now `map_attr_count((Map*)elmt) + elmt->length`, so the two tiers cannot drift apart. `len_iter_law.ls` no longer records a divergence; it pins the law. The verified walk order is attribute VALUES first, then content items: `for (x in <div id:"a", cls:"b", <p "x"> <q "y">>)` yields `["a", "b", <p "x">, <q "y">]` and `len` is 4.

The fallout is real and is tracked separately as [LR09-9](#lr09-9): the change moves 44 corpus goldens, of which only 6 are the bare length number.

<a id="lr09-9"></a>**LR09-9 · The `len(e)`-bound child walk, and the `content(e)` accessor that replaces it · RESOLVED 2026-09-04 (USER ruling)**

Closing LR09-8 removed the accident that made `for (i in 0 to len(e) - 1) e[i]` a correct child walk. An IntKey subscript reaches only children (S8.2.1v3) while `len` now also counts attributes, so the loop overran and `e[i]` read `null` past the last child. It was **not** merely inefficient: the phantom nulls are indistinguishable from real children, and three shapes of silent corruption showed up — a schema validator reporting each null as *"Scalar content is not permitted directly under \<graph>"*, a rebuilt content list gaining trailing nulls, and a `group by` aggregate turning `total: 15` into `null`.

**Ruled: `content(e)`**, a system function returning the element's content sequence. `len(content(e))` is the child count and `content(e)[i]` the child index walk, so the arithmetic disappears rather than being re-spelled. Rejected alternatives: `e.content` (dot resolves the key domain first under S8.2.2v2, so it would silently return a user attribute named `content` — and `content` is a live child/attr name across the graph schema) and `size(e)` (a second length-ish name, reintroducing exactly the confusion LR09-8 removed, and no way to index).

**It is a read-only VIEW, not a copy** (USER): the returned Array shadow-copies the element's content meta fields — items pointer and length — and never copies the item slots, so a per-node walk stays allocation-free. Borrowing reuses the container view contract ArrayNum already had: `is_view` set, `is_mutable_view` clear, and `extra` holding an `ArrayNumShape` whose `base` is the owning element. Write-through is deliberately deferred; `fn_array_set` refuses a read-only view.

**Three defects the view surfaced, each worth remembering.** (1) The view must be **rooted across the descriptor allocation** — that allocation can collect, and with conservative stack scanning retired a view held only in the C frame is invisible, so it was reclaimed mid-construction and its slot handed to the next array; `content(e)` then returned an unrelated later array. (2) The descriptor is nursery data and must be **promoted** in the compact pass, or `extra` dangles after the zone reset. (3) An element's items buffer **moves**, so the view is excluded from owned-data compaction and instead rebound from its base — forcing the base's promotion first, since the sweep order is arbitrary. All three only appear under `LAMBDA_GC_FORCE_EVERY`.

**Four runtime consumers were real bugs, not migrations** — every place that pairs a count with an IntKey read, since an IntKey reaches content only (S8.2.1v3) while `len` now also counts attributes. Found by test failure: the **mapping pipe**, which sized its traversal with `fn_len`, so `g |> ~["amount"]` gained a null row per attribute and poisoned `sum` — its own comment already said elements pipe over content. Found afterwards by audit, with NO test covering them: **`last`** (`e[last]` read `null` instead of the final child, on both tiers) and the **set operators** `fn_union`/`fn_intersect`/`fn_exclude` plus the mixed-type array concat (`e | f` leaked a trailing `null`).

All five now call one shared `extern "C" int64_t fn_seq_count(Item)` — the count of positions a positional traversal visits, which is content length for an element and `fn_len` otherwise — so the rule has exactly one definition and cannot drift between the tiers. `slice`/`drop`/`take_last` need no change: `vector_length` returns -1 for an element, so those refused elements before this ruling and still do.

*The audit is the lesson.* The pipe surfaced as a golden diff; `last` and the set operators did not, because no fixture exercised them on an element. Grepping for `fn_len` callers that feed an index was what found them, and that is the check to repeat if the length law ever moves again.

That gap is now closed by `test/lambda/element_content_axes.ls`, which pins both axes together — `len` as attributes-plus-content equal to the iteration count, `content()` as the child sequence, an IntKey reading `null` past the last child, `last`, the mapping pipe, the three set operators, the degenerate shapes (bare, attributes-only, content-only), a nominal element, and a `group by` element where the key attribute is counted by `len` but not by `len(content(g))`. It was verified to FAIL, not merely to pass: reverting `fn_seq_count` to `fn_len` makes `e[last]` collapse to null and the pipe grow two phantom rows, which is exactly the silent breakage that shipped unnoticed.

**Migrated call sites** (`content()` everywhere): `graph/model.ls` `element_children`/`child_items`, `graph/transform/content.ls`, `graph/transform/html.ls`, `editor/mod_edit_schema.ls` `children_array`, and `math/optimize.ls` — where `can_merge` tested `len(a) != 1` meaning *exactly one child*, so a single class attribute silently disabled all span merging. Fixtures using the idiom to express a child walk were migrated the same way rather than re-baselined; only 6 goldens changed, all bare length numbers.

**Still open, and worth a ruling of its own:** a `group by … into g` binds an element whose attributes are the group key, so `len(g)` now counts the key alongside the members and member count must be spelled `len(content(g))`. That is correct under S8.3.1v2 but is an ergonomic wart on the group-by surface.

<a id="lr07-14"></a>**LR07-14 · Cross-cutting gaps · OPEN (rollup)**
Numeric result-domain inference is duplicated across AST / MIR / runtime;
`SysFuncInfo` has no complete data-driven argument convention, so some return
conventions still use ad-hoc switches; and there is no debug-mode validation
that a boxed value carries the representation the transpiler believes it does.
The Stack API is the physical ownership authority; `Lambda_Impl_Numbers.md` owns
the semantic-promotion consolidation.

---

## 8. Memory management & GC (LR_08)

<a id="lr08-1"></a>**LR08-1 · Decimal `mpd_t` leak (in-code TODO) · OPEN**
`gc_finalize_dead_object` does nothing for `LMD_TYPE_DECIMAL` because the
`mpd_t` from libmpdec cannot be freed from that C file: dead decimals "will have
their mpd_t leaked until context end. TODO: Add a finalization callback
mechanism." (`lambda/runtime/gc/gc_heap.c:2068`–`2069`). Mid-execution
collections leak an `mpd_t` per dead Decimal; storage is reclaimed only by
`gc_finalize_all_objects` at teardown. A real per-cycle leak in decimal-heavy
long-running scripts.

<a id="lr08-2"></a>**LR08-2 · Execution-side-stack capacity is reserved up front · OPEN**
Root and raw-number regions have fixed virtual limits. Checked prologues fail
deterministically instead of corrupting adjacent memory, but workloads that
genuinely exceed those reservations cannot grow them dynamically.

<a id="lr08-3"></a>**LR08-3 · JIT rooting still hinges on honest static types · OPEN**
The collector trusts the transpiler's `should_gc_root_var` classification. A
heap Item mislabeled as a packed scalar could miss a precise slot; publishing
all heap-capable live locals before calls narrows but does not close the hazard.
Cross-link: [LR07-7](#lr07-7).
Per CLAUDE.md rule 15, the fix is precise `RootFrame`/`Rooted` ownership — never
a return to conservative native-stack scanning.

<a id="lr08-4"></a>**LR08-4 · Wide scalar ownership must be explicit at every escaping store · OPEN**
Number-frame temporaries are reclaimed at return, so containers, JS
environments, exceptions, and other longer-lived stores must rehome payloads
into storage-owned lanes. The shared store/rehome helpers enforce the current
paths; a new raw Item store that bypasses them creates a dangling scalar
pointer.

<a id="lr08-5"></a>**LR08-5 · Hard-coded struct byte offsets in tracing and compaction · RESOLVED 2026-09-05**
D2.6.6v2/D3.4.1: the C collector now consumes `LAMBDA_GC_OFF_*` constants
derived by `offsetof` from one canonical C ABI layout. Assertions bind that
layout to the C and C++ definitions of the container chain, `TypeMap`,
`ShapeEntry`, `TypedItem`, `ArrayNumShape`, `Function`, and `VMap`; trace,
compaction, and GC test fixtures no longer embed their own byte positions.
The retained separate `item_to_ptr` high-byte-zero platform assumption is not
an offset-layout issue.

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
while cross-family `==` is `false`, the Python-style split. Residual
value-semantics work is tracked as **OI-1**: VMap key eq/hash rank consistency,
[LR04-4](#lr04-4),
and [LR03-1](#lr03-1).

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

<a id="lr09-6"></a>**LR09-6 · `set_runtime_error` message buffer cap · RESOLVED 2026-08-28**
`set_runtime_error` and `err_createf` formatted into fixed 1024-byte stack
buffers, silently truncating rich diagnostics. The common formatting path now
measures the required length and allocates the complete message through the
existing `memtrack` allocator before creating the error. This satisfies the
message-bearing error contract in **S7.4.4** and removes the duplicated
formatting path; no new data structure or design ruling was added.

The shared 64-frame native stack-trace default is recorded in
[LR10-R4](#lr10-r4).

Regression: `ErrorCreationTest.CreateFormattedErrorPreservesLongMessage`
verifies the full 1514-byte formatted message. The focused error suite passes
121/121 and `make test-lambda-baseline` passes 3978/3978.

---

## 10. Error handling (LR_10)

<a id="lr10-2"></a>**LR10-2 · Hard-coded 64 KB last-function span · OPEN**
`build_debug_info_table` computes each function's end address as the next
function's start; the *last* function has no successor and is given a fixed
64 KB span (`info->native_addr_end = native_addr_start + 65536`,
`lambda/runtime/mir.c:646`). A JIT function larger than 64 KB placed last in
address order mis-attributes return addresses past that boundary, silently
dropping or mislabeling the deepest frame.

---

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

<a id="lr11-5"></a>**LR11-5 · `deep_copy` of `PATH` is shallow · OPEN**
For non-`sys` `LMD_TYPE_PATH` values `deep_copy_typed` returns the item as-is;
`sys://` paths are copied only if already resolved
(`lambda/io/mark_builder.cpp:1081`ff). The code comment warns the result "may
reference external memory" — a latent dangling reference if the source `Input`
is torn down first.

<a id="lr11-6"></a>**LR11-6 · Conservative safety analysis (adjacent) · OPEN**
`function_needs_stack_check` is hard-`true` and `function_is_tail_recursive` is
hard-`false`, so the TCO machinery exists but is not enabled. Same issue as
[LR12-3](#lr12-3);
tracked with the GC-root issue in
[LR07-7](#lr07-7) and
[LR08-3](#lr08-3).

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

<a id="lr12-3"></a>**LR12-3 · Safety gate hard-coded, TCO disabled despite being implemented · OPEN**
`function_needs_stack_check` returns a literal `true` and
`function_is_tail_recursive` a literal `false`
(`lambda/runtime/safety_analyzer.cpp:46`–`55`, with `// Tail recursion
optimization not yet implemented`). Every user function pays for a stack check
and no function gets TCO, even though `should_use_tco` / `has_tail_call` /
`is_tco_function_safe` are fully implemented and would classify many functions
correctly. Sound but pessimistic; the static-analysis face of
[LR07-7](#lr07-7) /
[LR08-3](#lr08-3).

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
**unmarked** (i.e. believed implemented) until this pass; now `*` with an
Appendix A row.

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
Appendix A row) — the two share the insertion/argument copy path, and landing
one without the other leaves a half-aliasing model that is harder to reason
about than either endpoint.

<a id="lr12-7"></a>**LR12-7 · The procedural surface is thin and ad hoc · OPEN**
IO procedures are a hand-curated set in one file with bespoke validation per
procedure; there is no general effect/capability system, so adding a
network-write or process-spawn procedure means another bespoke `pn_*` plus a
registry row.

---

## 13. Schema validator (LR_13)

<a id="lr13-1"></a>**LR13-1 · Suggestions are built but never surfaced · OPEN**
`generate_field_suggestions` (`lambda/validator/suggestions.cpp:135`, declared
`validator.hpp:448`) is complete but has **no callers**;
`suggest_similar_names` and `suggest_corrections`
(`error_reporting.cpp:28`–`41`) both `return nullptr` with
`// suggestions not implemented`. Wiring it in remains a small, high-value fix.

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

<a id="lr13-7"></a>**LR13-7 · `printf`/emoji output in production paths · OPEN**
Contrary to CLAUDE.md rule 4, `ast_validate.cpp` has 59 direct `printf` calls
and `error_reporting.cpp` 6, writing to stdout with emoji rather than through
`log_*`. Also `error->actual.item` truthiness treats a `0`/null actual as
"absent", which can misreport a legitimately-null value.

## 13.1 Ledger hygiene observations (not issues)

These records are retained for provenance but are excluded from the counts
above. The absence of source markers is not evidence that a structural defect
is absent; active rows must be found by behavior and ownership analysis.

<a id="lr12-1"></a>**LR12-1 · The whole validator test surface cannot run, and no baseline covers it · OPEN (found 2026-09-03)**

Every validator test binary builds and then aborts at startup on a flat-namespace symbol lookup: `test_validator_gtest` and `test_validator_input_gtest` on `_ItemNull`, `test_validator_features_gtest` and `test_ast_validator_gtest` on `_g_lambda_home`. Both are ordinary core globals — `ItemNull` is defined at `lambda/core/lambda-data.cpp:190` and `g_lambda_home` at `lambda/runtime/runner.cpp:191` — and both are present in `lambda.exe`, so this is a link-composition problem in those four targets rather than a missing definition.

The `validate` CLI is separately unusable: it resolves no root type at all, failing with `REFERENCE_ERROR: Type not found or circular reference detected: Document` even on the shipped pair `test/lambda/validator/schema_comprehensive.ls` + `test_data_valid.json`. The root name is chosen by a textual scan for the last `type ` in the schema file (`validator/ast_validate.cpp:270`–`312`), so the name reaching `resolve_type_reference` looks right and the schema's type table is what comes up empty.

**Why it went unnoticed:** the validator gate runs from `test-lambda-full` (`Makefile:1624`), not `test-lambda-baseline`, so both baselines stay green over a completely dead test surface. That is the finding worth keeping — a gate outside the baseline is a gate nobody runs.

Not attributed. The `elmt code clean up` commit (2cdcc1ea1) touches none of the files involved — not `build_lambda_config.json`, not `lambda-data.cpp`, not `runner.cpp`, not any validator or test source. The two remaining candidates are the object-redesign commit (da7a97b13), which reshaped `lambda.h`/`lambda-data.cpp` heavily, and the merged upstream `DOM: seven linkage lies and a dead local` (dbe7b7dba). Confirming which needs a from-scratch build of an older tree; a worktree attempt stalled on re-fetching vendored `re2`.

**Consequence for the specs:** the D2.6.6v2 content-arity claim can only be read from the code, not run. `validate_against_element_type` does enforce `content_length`, and an element-kinded nominal type reaches that arm by tag, so the old "not implemented" note is wrong — but "conformant" cannot be asserted until this is fixed. Both conformance rows now say exactly that.

<a id="lr03-10"></a>**LR03-10 · A type with no TypeId of its own resolves to the wrong singleton · RESOLVED 2026-09-03**
`lambda_type_node_singleton` (`runtime/ast.hpp`) turns a type-annotation AST
node into the runtime type value both tiers compare against. It arms a short
list of types that need a specific singleton, then falls back to
`base_type(node->type->type_id)` — a lookup keyed on the TAG. The arms exist
precisely because a few types have no tag of their own: `date`/`time` share
`LMD_TYPE_DTIME`, `list`/`number`/`integer` have no runtime tag at all, and the
sized numerics all share `LMD_TYPE_NUM_SIZED`.

Removing `LMD_TYPE_OBJECT` put `object` in exactly that class without adding
its arm. `TYPE_OBJECT` wears the map tag to route through the container
switches, so the fallback handed back the `map` singleton, and `{x: 1} is
object` answered **true** on both tiers while a nominal element answered false.
The helper is shared by T0 and MIR, so the two tiers agreed — with each other,
and not with the ruling. Fixed by adding the `object` arm alongside the others,
and by matching `&TYPE_OBJECT` by pointer identity in `fn_is` ahead of any tag
comparison.

Two things are worth carrying forward. The tag fallback is a **silent** wrong
answer, not a failure: a type that stops having its own tag needs its arm added
in the same change, and the existing arms are the checklist. And the diagnosis
cost more than the fix, because the natural suspicion was the lookup table —
`lookup_base_type_name` was returning the right singleton all along, and the
rewrite happened one layer later, at evaluation. Printing what `fn_is` actually
received, rather than what the table returned, is what closed it.

<a id="lr03-9"></a>**LR03-9 · The C mirror in `lambda.h` never matched `struct Container` · RESOLVED 2026-09-03**
`lambda.h` carries a C mirror of the container structs "for direct field access
optimization", with the comment *"Layout must match the C++ structs in
lambda.hpp exactly"*. It did not, and nothing checked it. The real `Container`
uses eight single-byte fields (`type_id`, `flags`, `array_flags`, `map_kind`,
`cow_state`, two ctor-mask bytes, `reserved_state`), each pinned by its own
`LAMBDA_STATIC_ASSERT`. Every mirror struct instead declared `uint16_t flags`
followed by padding, so alignment put `flags` at offset 2 and the first pointer
field at 16 rather than 8 — a divergence on every one of `Map`, `List`,
`ArrayNum` and `Element`.

It was invisible because nothing in C actually read those members: `gc_heap.c`,
the one C consumer, reaches fields by raw byte offset, and every other consumer
is C++ and sees `lambda.hpp`. So the mirror was dead weight that would have
produced wrong offsets the moment any C code used it by name.

Found by the D2.6.6v2 phase-1 work: adding the first-ever layout assertions to
the mirror failed the build immediately. Fixed by giving all four mirror structs
the exact eight-byte header, and the assertions now stand as the guard. The
general lesson is worth keeping: a hand-written mirror without an assertion is
only accidentally correct, and this one had been wrong for its whole life.

<a id="lr03-8"></a>**LR03-8 · A shape transition on an object drops its nominal record · RESOLVED 2026-09-03**
The shape-transition rebuild in `lambda-eval.cpp` (the `fn_map_set` path,
near the `LMD_TYPE_ELEMENT` branch that allocates a fresh `TypeElmt` and
carries `name`/`content_length`/`ns` across) has no object arm: an object
falls into the plain-`TypeMap` else-branch, which builds a shape with no
`type_name`, no `base`, and no method table. Found by the 2026-09-03 layout
survey as latent — no corpus script extends an object with a new field.

Under S2.1.4 it is a **defect, not an error path**: Lambda objects are open by
default (OB15 part 3), extension is an ordinary member addition, and every
shape reached from a declared shape must share the one nominal record (OB16),
so the value stays an instance of its type and its methods keep resolving.
**Resolved with D2.6.6v2 phase 2.** The nominal record is now a `TypeMap`
field, and both shape-rebuild sites copy it forward: the generic transition in
`fn_map_set`'s rebuild path, and `map_extend_open_shape`, which is the one that
actually produces a grown shape. The second mattered more than expected — until
open-instance extension landed the same day, no code path could reach a grown
nominal shape at all, so the defect was unobservable rather than absent.
Fixture `test/lambda/proc/object_open_instance.ls` pins exactly the check this
entry asked for: extend `<P x: 5>` with `p.z = 9` through a `var`, then confirm
`p is P`, `p.dbl()`, `len(p)`, round-trip printing, and that a grown instance
does NOT equal an ungrown one (S5.4.2v3 compares the full key set). Verified on
both tiers and under forced GC.

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

<a id="ts-8"></a>**TS-8 · No arity overloading for user definitions · RESOLVED (not a defect — ruled S12.3.6)**
`impl/Lambda_Issue_Type_Support (retired).md`. `pn f(a)` plus `pn f(a, b)` in
one scope gives `error[E209]: duplicate definition of 'f' in the same scope`.
**Ruled 2026-08-25 as intended behaviour** (`Lambda_Formal_Semantics.md`
S12.3.6, spec v15.2.0): a name binds to exactly one function, following
ECMAScript per S1.11.

The entry framed this as an asymmetry against the builtin registry, which *is*
keyed on `(name, arity)`. That keying is a **dispatch optimization** — it lets
an intrinsic select a specialized row without a runtime arity branch — not a
language rule, so builtins are not overloadable in source either and the
asymmetry is only apparent.

Nor is expressiveness lost: Lambda already covers the intent with **optional
parameters**. Verified — `pn f(a, b?)` accepts `f(1)` → `[1]` and `f(1, 2)` →
`[1, 2]`, the `fn` form behaves the same, and `g(1, 2, 3)` past the declared
slots is rejected. `pn f(a)` and `pn f(a, b)` are one `pn f(a, b?)`.

*Adjacent nit, since fixed 2026-08-25:* the over-arity diagnostic counted only
required parameters — `fn g(a, b?)` with three arguments reported "expects **1**
argument, got 3". It now reports the range, which matters more once S12.3.6
makes optional parameters the sanctioned alternative to overloading:

| Signature | Call | Message |
|---|---|---|
| `fn g(a, b?)` | `g(1,2,3)` | expects **1 to 2** arguments, got 3 |
| `fn g(a, b, c?)` | `g(1)` | expects **2 to 3** arguments, got 1 |
| `fn add(a, b)` | `add(1)` | expects 2 arguments, got 1 *(unchanged)* |
| `fn h(a)` | `h(1,2)` | expects 1 argument, got 2 *(unchanged)* |
| `fn v(a, ...)` | `v()` | expects 1 or more arguments, got 0 *(unchanged)* |

`build_ast.cpp` `lambda_ast_validate_call_arguments`; covered by
`test/std/negative/wrong_arg_count_optional.ls` +
`NegativeScriptTest.OptionalParamArityReportsARange`.

<a id="i8-mapkey"></a>**Issues8 · Double-quoted map keys rejected · RESOLVED (not a defect — doc was wrong)**
Every double-quoted map key fails: `{"key": 1}` gives
`error[E100]: expected an expression` at the `:`, while `{'key': 1}` and
`{key: 1}` both work. Ruled 2026-08-25: **the parser is correct** — a map key is
a *symbol*, written bare when it is a name and single-quoted otherwise; a
double-quoted string is not a key form. The Issues8 entry framed this as a
hyphen problem, but hyphens were never the issue: `{'other-key': 2}` already
works. The real defect was the documentation — `doc/Lambda_Data.md` presented
`{"string_key": 1, symbol_key: 2}` as a valid "mixed key types" example, and
that line did not parse. It now reads
`{'symbol-quoted': 1, name_key: 2}` under the comment *"Keys are symbols: quote
one when it is not a bare name"*, which does parse. The two `"..."` key hits
elsewhere in `doc/` are inside ```json fences and are correct as JSON.

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

<a id="i8-dqdiag"></a>**Issues8 · Double-quoted map key gives a generic diagnostic · RESOLVED 2026-08-25**
`{"key": 1}` reported `expected an expression` at the `:`. It now says:

> `a map key is a symbol, not a string: write a bare name like {key: 1}, or
> single-quote it when it is not a name like {'data-node-id': 1}`

**Why it was generic.** The brace resolver decides by interior (S16.4.1v2), and
a string is not a key — so `{"k": 1}` was read as a **block**, parsed `"k"` as
an expression statement, and failed on the following `:`. No amount of work in
`parse_map` could have helped, because control never reached it.

**Fix.** `braced_expression_is_map` now also routes `{ STRING : … }` to the map
parser, which rejects the key with the message above. That changes no accepted
program — `{"k": …}` has no valid reading as either a map or a block — and one
message covers every brace position, since `control_body_brace_is_map`
delegates to the same probe. Verified unchanged: `{key: 1}`, `{'a-b': 2}`,
`{a: 1, b: 2}`, `{}`, and the block forms `{ let x = 1; x }`,
`{ "just a string" }` → `"just a string"`, `{ 1 + 2 }` → `3`. Covered by
`test/std/negative/map_key_double_quoted.ls` +
`NegativeScriptTest.DoubleQuotedMapKeyNamesTheRule`.

<a id="issues0-9"></a>**Issues0 #9 · ShapePool keys on a hash without comparing field names · RESOLVED 2026-08-27**
`vibe/impl/Lambda_Issues0 (fixed).md` #9 — deferred there, and the archive's
`(fixed)` name hides it. `shape_pool.cpp:22` builds the pool key with
`HASHMAP_DEFINE_FIELD3_KEY(shape_entry, ShapePoolEntry, signature.hash,
signature.length, signature.byte_size)`. Two different shapes that collide on
hash **and** match on field count and byte size are treated as identical, so one
map's shape is reused for another and fields are read from the wrong offsets —
silent data corruption. Low probability, high blast radius.

The fix keeps the signature as a fast routing key and wires the existing
`shape_pool_shapes_equal` comparison into the hashmap identity check. Lookup
uses a stack-only probe, so repeated lookups do not consume arena storage; the
element name is retained as existing cache metadata so element signatures are
confirmed as well. This implements the structural identity required by
**D3.4.2** and exact name identity in **D3.4.4v2**.

Regression: `NamespaceTest.ShapePoolCollisionDoesNotAliasDifferentFieldNames`
uses the supported `NULL`-name normalization collision and verifies that
different shapes remain distinct while identical shapes are still reused.

<a id="lint-e1"></a>**Lint E1 · Unchecked allocation dereference in `build_ast.cpp`, now invisible to cppcheck · RESOLVED 2026-08-27**
`impl/Lambda_Issues4_Lint (retired).md` E1. The root cause was seven literal
source-copy sites treating the custom `mem_alloc` contract as non-fallible:
the four manual copies listed by the retired lint entry plus three
`strview_to_cstr` callers. `mem_alloc` returns NULL under the
`memtrack_fault_should_fail()` injection hook and on a failed `malloc`, so
each unchecked copy was reachable rather than theoretical.

The sites now share `ast_copy_source_text`, which checks the allocation,
records `ERR_OUT_OF_MEMORY` (`E309`) against the literal span, and returns
`TYPE_ERROR`/a failed static-literal probe before the buffer is read. This
follows the checked allocation contract in **D4.2.1v3** and the allocation
failure handoff in **D4.2.2v2**. The consolidation removes the duplicated
copy-and-terminate code; no new data structure or design ruling was added.

Worth recording separately: the migration from `malloc` to `mem_alloc`
**silenced the static analyser without fixing the code**. cppcheck originally
flagged this as `nullPointerArithmeticOutOfMemory`; a 2026-08-25 re-run reports
nothing here, because it does not model the custom allocator. The report's own
suggested remedy — use an allocator that cannot return NULL — was only half
applied.

Regression: `AstBuildAllocationTest.SizedLiteralCopyFailureDoesNotCrash`
arms `memtrack_fault_inject(0)` and verifies direct AST construction reports
the allocation error without crashing. The focused error suite passes 120/120
and `make test-lambda-baseline` passes 3976/3976.

<a id="i8-consoleesc"></a>**Issues8 · Console formatter does not escape quotes or backslashes inside collections · RESOLVED 2026-08-27**
Printing a collection rendered member strings through raw `%s`/`%.*s` paths in
`lambda/core/print.cpp`. That omitted the Lambda escapes for quotes,
backslashes, and control characters, producing ambiguous output such as
`["init: {"flowchart": …}", "back\slash"]`.

The fix consolidates the Item and legacy TypedItem string/symbol paths on one
length-based `print_quoted_text` helper. It emits `\"`, `\\`, `\n`, `\r`,
`\t`, `\b`, and `\f` for collection members while preserving the existing
standalone-string display contract, so a serialized string is not escaped a
second time. No new data structure or design ruling was added; the supported
escape forms follow the Lambda string grammar; the common forms are documented
in `doc/Lambda_Data.md`.

Regression: `NamespaceTest.PrintCollectionEscapesStringContents` verifies quote
and backslash escaping directly through `print_item`. The 42 affected golden
outputs were regenerated from the corrected printer. Focused namespace tests,
the direct Lambda suite (784/784), and `make test-lambda-baseline` pass
3976/3976.

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
and map-key diagnostics now use (see [i8-semidiag](#i8-semidiag),
[i8-dqdiag](#i8-dqdiag)).

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

<a id="i8-markcomp"></a>**Issues8 · One-line Mark child comprehensions fail at the closing delimiter · RESOLVED (not a defect — wrong spelling)**
The entry reported `<diagnostics; for (v in vs) v>` failing with `E100` where
"the valid multiline constructor" parsed, concluding that whitespace changes the
grammar. **Both halves of that are wrong.** `;` was never an element separator,
and the multiline form it presents as valid fails identically — verified
2026-08-25.

**S16.9.3** settles the spelling: `;` has exactly one role language-wide,
statement separation; `,` takes over inside elements, and the attribute/content
boundary comma is a **biconditional** — present exactly when the element has
both. `diagnostics` here is the *tag*, not an attribute, so the element is
content-only and takes no separator:

```lambda
<diagnostics for (v in vs) v>                 // correct — parses, <diagnostics 1 2>
<diagnostics kind: "x", for (v in vs) v>      // correct — both present, comma required
<diagnostics; for (v in vs) v>                // E100 — `;` is not an element separator
<diagnostics, for (v in vs) v>                // E100 — no attributes, so no comma
<diagnostics kind: "x" for (v in vs) v>       // E100 — both present, comma missing
```

Whitespace is irrelevant; the spelling was wrong in both layouts. The author most
likely carried `;` over from statement separation — the confusion S16.9.3 exists
to retire. Residue filed separately as [i8-semidiag](#i8-semidiag).

<a id="i8-semidiag"></a>**Issues8 · `;` inside an element gives a generic diagnostic · RESOLVED 2026-08-25**
`<diagnostics; for (v in vs) v>` reported only `expected an expression`, while
the two comma mistakes already named their rule. It now says:

> `';' cannot open element content; a tag is followed directly by its content,
> and ';' only separates one content item from the next`

**Scoped by what is actually legal.** `;` *is* valid between content items —
`<div "a"; "b">` and `<div k: 1, "a"; "b">` both parse — so the check fires only
at the content-start position, where no preceding item exists. The
attribute-bearing form `<div k: 1; "a">` was already covered by the
boundary-comma check and is untouched. `lambda_parser.c` `parse_element`;
covered by `test/std/negative/element_semicolon_opens_content.ls` +
`NegativeScriptTest.ElementSemicolonCannotOpenContent`.

<a id="i8-dynspread"></a>**Issues8 · Spreading a dynamically-constructed map yields a null-key nested map · OPEN**
Map spread flattens a statically shaped map but not one built at runtime, even
though both are `type() == map`:

```
let stat    = {shape: "box"}
let dynamic = map(["shape", "box"])
{*: stat,    id: "a"}   ->  {shape: "box", id: "a"}       correct
{*: dynamic, id: "a"}   ->  {[null nested map], id: "a"}  wrong
```

`dynamic` itself is sound (`type` is `map`, prints as `{shape: "box"}`), so the
defect is in the spread's handling of a runtime-built shape, not in the map. The
same operand also loses its fields across an element-attribute spread, which is
[the entry below](#i8-attrspread) — likely one root cause for both.

<a id="i8-attrspread"></a>**Issues8 / Issues5 §23 · Element attribute spread lands the map as a child · OPEN**
`<path *attrs>` does not error, but the spread map becomes a *child* rather than
attributes: `<path {a: 1, b: 2}>` instead of `<path a: 1, b: 2>`. Recorded twice
— `impl/Lambda_Issues8 (retired).md` ("Runtime map attribute spread creates a nested element
child") and `impl/Lambda_Issues5 (retired).md` §23 — as one issue. The sibling half of the
Issues5 entry (an inline `if` as an attribute value) is **fixed**:
`<path d: "M0", 'stroke-dasharray': if (has_dash) dash else "none">` now
evaluates to `<path d: "M0", stroke-dasharray: "4 2">`.

---

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

- **OI-1 · Value equality & ordering contract.** The operator surface is sound
  (verified 2026-07-16): `fn_eq` structural and cross-rank-exact, `fn_lt_scalar`
  and `total_cmp` raw-byte and mutually consistent, `array_num_eq` width-correct
  and NaN-aware. Residue: (a) `item_deep_equal` dedup over a new `fn_eq_strict`
  — its only caller is Radiant no-op elision, where gaps cost missed elision, not
  wrong answers; (b) **VMap key eq/hash rank consistency** — `fn_eq(1, 1.0)` is
  true, so map lookup/hashing must agree across numeric ranks or the rule must be
  stated; (c) `decimal_cmp` returning equal on conversion failure
  ([LR04-4](#lr04-4)); (d) equal-value different-scale decimals must agree across
  `==`, `<` and key use. Cross-refs: [LR03-1](#lr03-1), [LR09-3](#lr09-3).
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
- **Layout-coupled raw offsets** — `init_module_import` ([LR01-8](#lr01-8))
  still needs static-assert guards or a generated offset table; GC
  trace/compaction is resolved by [LR08-5](#lr08-5).
- **One masked memory-safety bug** — the event-loop SIGSEGV band-aid remains;
  the `sys://` map-walk segfault workaround was replaced by the shape-aware
  traversal in [LR01-R3](#lr01-r3).
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

# Appendix A — Resolved and obsolete issues

Kept for provenance: each of these appeared in an `LR_*` "Known Issues" section
and was verified fixed or removed on the date recorded below. Do not re-open
without re-verifying against current source.

## A.1 Compilation pipeline (LR_01)

<a id="lr01-r1"></a>**LR01-R1 · Parallel-compile CPU cap is advisory only · RESOLVED (removed)**
The parallel import-level compile path is gone: no `pthread_create`, `ncpus`, or
`cpu_cap` remains in `lambda/runtime/runner.cpp`, and `PROFILE_MAX_IMPORT_LEVELS`
went with it. The over-subscription hazard and the hardcoded 8 MB worker stack no
longer exist. (Per-script profiling caps survive — see
[LR01-5](#lr01-5).)

<a id="lr01-r2"></a>**LR01-R2 · Precompile reversal coupling · RESOLVED (removed)**
`precompile_imports` no longer exists anywhere in `lambda/`. The fragile contract
between its slice reversal / index renumbering and `run_script_mir`'s
reverse-order import init is gone with it.

<a id="lr01-r3"></a>**LR01-R3 · `sys://` paths in maps/elements are never resolved · RESOLVED 2026-08-26**
`resolve_sys_paths_recursive` (`lambda/runtime/runner.cpp`) now walks map and
object fields through `map_shape_field_to_item`, and walks both element
attributes and children. This preserves the packed-shape ABI described by
`D3.4.1`; the old raw map-data walk was the source of the csv-related crash
that had suppressed this traversal. Resolved paths and their nested results
are recursively visited under the `S2.4.1v2` path contract. A nested map/element
probe now returns resolved values, and `make test-lambda-baseline` passes
3914/3914.

<a id="lr01-r4"></a>**LR01-R4 · Unescaped LaTeX bridge filename · RESOLVED 2026-08-26**
The LaTeX-to-HTML bridge now uses `lambda_string_literal_escape` and sizes its
script buffer from the escaped input instead of interpolating into a fixed
4096-byte array. This keeps source paths data rather than Lambda source, as
required by `S1.8`, and matches the PDF bridge's ownership and sizing pattern.
The normal smoke reaches the existing LaTeX package import-resolution failure;
the bridge construction itself is now source-safe and dynamically sized.

<a id="lr01-r5"></a>**LR01-R5 · `target_equal` compares hash-only · RESOLVED 2026-08-26**
`target_equal` retains the hash as a fast rejection, then compares target type,
scheme, and canonical URL/path content. Hashes are therefore not identity;
this follows `S2.4.2v4`. `Target_HashCollisionIsNotEqual` forces equal hashes
for two different URLs and passes in the namespace suite (38/38).

## A.2 Parsing & AST construction (LR_02)

<a id="lr02-r1"></a>**LR02-R1 · Unknown binary operator defaults to `OPERATOR_ADD` · RESOLVED**
`lambda_binary_operator_from_spelling` (`build_ast.cpp:3683`) now `return
false` on an unrecognized spelling (`:3717`), and the caller records a real
diagnostic — `record_semantic_error_span(tp, span, ERR_INVALID_OPERATION,
"unknown binary operator '%.*s'")` — and sets `node->type = &TYPE_ERROR`
(`:7382`–`7388`). Grammar/builder drift now fails loudly instead of silently
compiling as `+`.

<a id="lr02-r2"></a>**LR02-R2 · Numeric promotion relies on enum order · RESOLVED**
`std::max(left_type, right_type)` is gone from `build_ast.cpp` entirely.
Promotion now runs through the shared classifier in
`lambda/runtime/lambda-number.hpp` — `lambda_numeric_classify(family, kind_l,
kind_r)` over an explicit `LambdaNumericKind` enum
(`INT`, `INTEGER`, `FLOAT`, `DECIMAL`, `I8`…`U64`, `F16`, `F32`) — so reordering
`TypeId` no longer changes arithmetic results, and `float ∥ integer` and sized
lanes are representable.

<a id="lr02-r3"></a>**LR02-R3 · Decimal / `integer` result inference is incomplete · RESOLVED**
Superseded by the same classifier: `lambda_numeric_kind_from_type(Type*)` reads
the full `Type*` rather than reducing to `TypeId`, and `LAMBDA_NUM_INTEGER` and
`LAMBDA_NUM_DECIMAL` are distinct kinds
(`lambda-number.hpp:12`, `:14`). Arbitrary-precision integer results are no
longer conflated with ordinary decimal results.

<a id="lr02-r4"></a>**LR02-R4 · `raise` is not scope-checked · RESOLVED**
The `// TODO: Also allow in pure functions with error return type` is gone.
`build_raise_node_from_parts` (`build_ast.cpp:8011`) is scope-agnostic by
design; correctness is now enforced by the error-type machinery —
`TypeFunc::can_raise` (`:4633`), divergence classification (`:4099`–`4100`),
`validate_function_return_contract` (`:5166`, called `:8468`) and
`validate_explicit_return_boundaries`. This is the TE-16 `T^E` / `expr ^ { … }`
work landing; see [Type support enforcement design].

<a id="lr02-r5"></a>**LR02-R5 · `list` expressions forced to `&TYPE_ANY` · RESOLVED**
`direct_list_node` (`build_ast.cpp:7038`) now propagates a single item's own
type and only falls back to `set_type_any(tp, ANY_LIST)` for the general case
(`:7046`–`7047`). The adjacent `// Fix scope restoration` marker is gone;
declaration-bearing blocks take a separate, explicitly scoped path (`:7205`ff).

<a id="lr02-r6"></a>**LR02-R6 · Line-start fluent `.method(` rejected when the member name is a type keyword · RESOLVED 2026-08-24**
*Was LR02-11, found during the doc sweep; fixed the same day.*

**Symptom.** A fluent chain broken across lines was rejected whenever the member
name was a type keyword — `.map(`, `.int(`, `.string(`, `.float(`, `.array(`,
`.element(`, `.symbol(` — while `.len(`, `.sum(`, `.sort(`, `.filter(` and the
rest were accepted. The same expression on one line always worked.

**Root cause.** Two predicates that must agree had drifted apart. The member-name
parser `parse_path_segment` (`lambda/runtime/parser/lambda_parser.c`) accepts
`token_is_key(...)`, which includes `LAMBDA_TOK_BASE_TYPE`; the S16.2.4 line-start
carve-out in `parse_postfix` tested `parser->next.kind == LAMBDA_TOK_IDENTIFIER`
alone. A type keyword lexes as `LAMBDA_TOK_BASE_TYPE`, so the guard rejected
exactly the chains the member parser would have accepted.

**Fix.** The carve-out now calls `token_is_key(parser->next.kind)` — the same
shared set — so the guard admits precisely what the member parser admits.
`INTEGER`, `SLASH`, `PARENT` and `STAR_STAR` stay out because each keeps a
non-member reading at line start; `.5` therefore remains dual-role. A comment at
the fix point records the invariant so the two cannot silently desync again.

**Front-end divergence closed.** The Tree-sitter reference grammar already
accepted all these forms, so this was a C-parser-only defect and the two front
ends disagreed, against §4.4. They now agree.

**Verified.** Three ratcheting cases added to *both* harnesses
(`test/c_s16_conformance.sh`, `test/ts_s16_conformance.sh`): C 123→**126/126**,
Tree-sitter 118→**121/121**. Reverting the one-line fix fails exactly those three
and nothing else, so the ratchet bites. `make test-lambda-baseline`:
**3867/3867**.


<a id="lr02-r7"></a>**LR02-R7 · `pn ... =>` accepted by the C parser only; arrow-body errors rewritten into the element-ambiguity message · RESOLVED 2026-08-24**
Two defects closed by the S16.6.6/S16.6.7 ratification. (1) `parse_function_declaration` accepted `pn p() => expr` and `pn p() => { ... }` while the Tree-sitter reference grammar rejected both — a §4.4 front-end divergence with the C parser as the outlier; now rejected with `a procedure body is a statement block — write 'pn name() { ... }'`. Corpus cost: 1 doc site (`doc/Lambda_Procedural.md`), 0 tests. (2) The `runner.cpp` relation walk-back matched the `>` of `=>`, rewriting every arrow-body diagnostic into `'<' and '>' are ambiguous with element syntax` — `(x) => return x` produced that message instead of the parser's own; the walk-back now skips `=>` and `|>`. Harness: C 138/138, TS 128/128.

<a id="lr02-r8"></a>**LR02-R8 · Reference grammar lexed `return`/`break`/`continue` as identifiers in expression position · RESOLVED 2026-08-24**
*Was LR02-12, opened the same day while implementing S16.6.6 and closed the same day.*

**Symptom.** `if (c) return -1` parsed in the Tree-sitter reference grammar as a
**subtraction from a variable named `return`** — a silent misparse, strictly
worse than acceptance, and invisible to the compare lane because production
rejected it.

**Root cause.** Tree-sitter's lexer is context-aware and `word: $ => $.identifier`
enables keyword extraction: a keyword token is emitted only where it is
syntactically valid, otherwise the word falls back to `identifier`. In an
expression position `return_stam` is not valid but `identifier` is, so the
fallback fired. No grammar-only fix exists for this in tree-sitter 0.24 —
per-position reserved words arrived in 0.25's `reserved` sets, and the repo
pins 0.24.7.

**Fix.** A zero-width external `_expr_body_start`, withheld by the scanner when
the word at the cursor is `return`/`break`/`continue`, required at the four
S16.6.6 body positions (paren-form `if`/`for` body, `else` body, `case T:` arm,
`=>` arrow body — eight grammar sites). Withholding the token kills the
expression-body alternative, which is exactly the rejection required. The guard
is **scoped to those positions rather than to every identifier**, keeping the
§7.17 scanner blast radius small, and is **stateless** — a pure function of the
lookahead — so it carries none of that note's stale-carry hazard. The helper is
`inline:`d: as a real nonterminal it forced a reduce conflict against a trailing
binary operator (`=> x > y`).

**Verified.** C 140/140 and Tree-sitter 135/135 on identical case sets (the five
divergence cases moved back into the TS suite, plus controls for a
keyword-prefixed identifier `returnValue` and an arrow body with a binary tail).
Full 700-file `.ls` corpus cross-check: **zero movement** — the same 76
pre-existing failures before and after, measured by regenerating both ways.
`make test-lambda-baseline` 3868/3868.

<a id="lr02-r9"></a>**LR02-R9 · `for (k, v at c)` bound both names to the key · RESOLVED 2026-08-24**
*Was LR02-8, found during the verification pass; closed once S8.1.3 settled what the form means.*

**Symptom.** `for (k, v at {a: 1, b: 2}) k ++ v` yielded `['aa', 'bb']` — the value
name aliased the key. A **silent wrong answer**: the shape was right, only the
binding wrong. The `where` variant was worse still — `where v > 2` compared the
key against a number, so the filter silently returned `[]`.

**Root cause.** `AstLoopNode.name` holds the LAST binding and `index_name` the
first, so in the paired form `name` is the value slot and `index_name` the key
slot. `at` set `key_only`, which redirects `name` to the key — correct for the
single-name `for (k at c)`, but in the paired form it overwrote the value slot
while `index_name` was independently getting the key.

**Fix.** Gate `key_only` on the absence of `index_name` (`build_ast.cpp`).
`key_filter` is deliberately untouched: that is what restricts the **member set**
to name keys, so the axis still means something — paired `at` on an element
yields attribute pairs only, and on an array yields nothing (an `IntKey` is not
a name, S8.2.2v2). Both execution tiers read the same flag, so one fix covers
MIR Direct and the interpreter.

**Ruling first, then fix.** The form was unspecified — S8.1.1 paired `at` with a
single name, S8.2.1v2 specified the paired form only for `in`, and SO12 recorded
the question as open. **S8.1.3** now rules axis and arity independent: the axis
picks which members are walked, the arity picks the projection. SO12 is closed.

**Verified.** The three worked examples in `doc/Lambda_Expr_Stam.md` had never
been run and all three were wrong; they now match. Regression test
`test/lambda/for_at_pairs.ls` + `.txt` pins all six shapes (paired/single `at`,
`where`, element attrs-vs-children, empty array). Baseline 3868/3868.

<a id="lr02-r10"></a>**LR02-R10 · Spread does not expand into a call's argument list · CLOSED 2026-08-25 (won't fix; `call()` supersedes)**
*Was LR02-10.* Ruled **container-only** as S12.3.5 rather than implemented.

**Why not.** Expansion needs call-site syntax and semantics of its own, costs
the static arity check S12.3.1 relies on (a spread's length is unknown until run
time), and silently diverts calls to the dynamic ABI — a same-source-shape perf
cliff. Demand was thin: 7 variadic functions and 24 `varg()` sites in the whole
test corpus, **0** in `lambda/` packages. And `varg()` returns an *array*, so
forwarding already worked for any callee taking a collection; the only shape
with no workaround was forwarding to a callee that is itself variadic.

**What replaced it.** `call(f, args)` (S12.3.4) — one registry row over the
existing `fn_call_into` dynamic ABI, versus three sites that would have had to
agree forever. Honestly dynamic, so no static guarantee is silently lost, and
strictly more general: it forwards to fixed-arity and variadic callees alike.
`fn outer(...) => call(inner, varg())` is the motivating case and works on both
tiers. S12.1.4 admits `call` as Lambda's first effect-polymorphic function,
the first partial answer to SO28.

**Follow-through.** Docs corrected in `Lambda_Expr_Stam.md` (the "not yet
implemented" spread note became the container-only ruling), `Lambda_Func.md`,
and `Lambda_Sys_Func.md` (new Dynamic Application section).
`test/std/core/functions/variadic_args.ls` — which never parsed, using a third
spelling `values...` — is repaired and now covers the forwarding case.
Regression test `test/lambda/call_dynamic_apply.ls` + `.txt`. Residue was tracked
as LR02-13 and is now resolved in [LR02-R13](#lr02-r13).

<a id="lr02-r13"></a>**LR02-R13 · `call()`'s runtime colour check selected the wrong registry row · RESOLVED 2026-08-26**
The `call` registry contains both `SYSFUNC_CALL` and `SYSPROC_CALL` with the
same name and arity. Lookup could return the procedure row even in a function
scope, while the effect-row resolver only corrected the function row; a
dynamically selected `pn` could then run from `fn`. The resolver now normalizes
either row to the enclosing `fn`/`pn` colour. This implements `S12.1.4` and
`S12.3.4` without changing closure construction. The tracked dynamic-procedure
regression passes, as does the full baseline.

<a id="lr02-r18"></a>**LR02-R18 · Bare `pn` method reference · RESOLVED 2026-09-05**
`AstFieldNode::is_proc_method_reference` marks a resolved dotted `pn` member;
the call builder clears that mark only when it consumes the member as the direct
callee. The shared final AST pass rejects every remaining mark with E224 before
either T0 or MIR lowering. This implements **S12.3.3v2** and **D2.6.7** without
changing the runtime member lane that valid `pn` calls need. Regression:
`NegativeScriptTest.SemanticError_ProcMethodCannotBeTakenAsValue`; the retained
positive member-value fixture passes on both JIT and T0.

## A.3 Value & type model (LR_03)

<a id="lr03-r1"></a>**LR03-R1 · Two parallel type vocabularies · RESOLVED**
The `TypeSchema`/`SchemaTypeId` vocabulary in `schema_ast.hpp` was dead code and
has been removed, leaving `Type*` as the runtime's single type vocabulary. See
[LR13-R1](#lr13-r1).

<a id="lr03-r2"></a>**LR03-R2 · `vmap_from_array` dead branch · RESOLVED**
The duplicated `type_id != LMD_TYPE_ARRAY && type_id != LMD_TYPE_ARRAY` guard is
gone. `lambda/runtime/vmap.cpp:330` is now a single
`if (type_id != LMD_TYPE_ARRAY)`, with a comment (`:327`–`329`) explaining that
lists are `LMD_TYPE_ARRAY` at runtime and that `LMD_TYPE_ARRAY_NUM` is
*intentionally* rejected because its packed layout is unsuitable — so the second
clause was not a missing `ARRAY_NUM` case after all.

## A.4 Strings, symbols & vectors (LR_05)

<a id="lr05-r1"></a>**LR05-R1 · `ArrayNum ==` is representation-sensitive · RESOLVED**
`array_num_eq` (`lambda/runtime/lambda-eval.cpp:1852`, called `:2220`) checks
N-D shape as structure, value-compares element-wise across differing element
types (avoiding double-promotion precision loss on high int64/uint64 bits),
compares float arrays element-wise (NaN-correct), and memcmps same-type compact
arrays with the per-type element width from `ELEM_TYPE_SIZE`. The historical
`sum(abs(a-b)) == 0` workaround is no longer needed.
⚠ Related caution from [Typed Array 4 implementation]: `ArrayNum ==` remains
*representation-sensitive at the benchmark level* — keep goldens in step.

<a id="lr05-r2"></a>**LR05-R2 · Two string orderings coexist · RESOLVED (stale at the operator level)**
Every language comparison is raw byte order and mutually consistent: `==`
(`fn_eq`), ordered `<`/`>` (`fn_lt_scalar`/`fn_gt_scalar` — `memcmp` plus length
tiebreak), and the sort-facing total order (`total_byte_cmp`). The utf8proc
casefold comparators are used only by the markup parser for case-insensitive
tag/attribute matching; the dead Item-level wrappers were removed in
[LR05-R3](#lr05-r3).

<a id="lr05-r3"></a>**LR05-R3 · Dead `*_comp_unicode` Item wrappers · RESOLVED 2026-09-05**
The five unused Item-level Unicode comparison wrappers and their declarations
are deleted. String-level casefold helpers remain markup-only, so Lambda's core
equality and order continue to follow **S6.2.2v3** bytewise UTF-8 semantics.

<a id="lr05-r4"></a>**LR05-R4 · `index_to_item` truncates int64 → int · RESOLVED 2026-08-26**
`index_to_item` now passes its `int64_t` index directly to the 64-bit `i2it`
lane, so the `~#` value emitted by mapping pipes is not narrowed through a C
`int`. This preserves the index carrier required by `S10.1.2`; the baseline
passes 3914/3914.

<a id="lr05-r5"></a>**LR05-R5 · `fn_label` flood-fill workspace bypassed the runtime allocator · RESOLVED 2026-08-28**
The flood-fill workspace now uses the existing checked `mem_alloc`/`mem_free`
path with `MEM_CAT_TEMP` instead of raw `malloc`/`free`. This keeps temporary
allocation failure and ownership tracking aligned with **D4.2.1v3** and
**D4.2.2v2**. Regression: `RuntimeShapeTransition.LabelStackAllocationFailureReturnsError`;
the representation suite passes 29/29 and the Lambda baseline passes
3977/3977.

## A.5 C transpiler — legacy C2MIR (LR_06)

<a id="lr06-r1"></a><a id="lr06-r1r9"></a>**LR06-R1 … LR06-R9 · All nine issues · RESOLVED (backend deleted)**
`lambda/transpile.cpp`, `transpile-call.cpp`, `lambda-embed.h`, and the
`jit_compile_to_mir` entry in `mir.c` have been removed from the tree. No core
or Jube build defines `LAMBDA_C2MIR`; `lambda/main.cpp` does not parse a
`--c2mir` flag; no test target builds it. The only surviving `c2mir` references
are in the vendored MIR archive build rules (`Makefile:219`–`222`, `:380`,
`:396`, `:402`), which Lambda does not invoke. Per CLAUDE.md rule 14 the path is
frozen; per this verification it is absent. Retired with it:

1. `#ifdef LAMBDA_C2MIR`-gated stale-by-default backend.
2. GROUP BY not implemented in `transpile_for`.
3. Typed-array support diverges from MIR Direct in C2MIR's favour — *note:* the
   underlying MIR Direct gap survives independently as
   [LR07-3](#lr07-3), but there is no longer
   a more-complete backend to port from.
4. `_store_i64`/`_store_f64` SSA-reorder workaround with `MAX_LOOP_ASSIGN` cap —
   *note:* the runtime-side helpers persist as
   [LR03-3](#lr03-3).
5. `is_idiv_expr` boxed-result / INT-static-type mismatch.
6. `MAX_INFER_PROCS 32` / `MAX_INFER_CALL_SITES 64` silent inference truncation.
7. TCO iteration ceiling — *note:* survives on the MIR Direct side as
   [LR07-13](#lr07-13).
8. Documentation-vs-code divergence on `fn_band`/`fn_bor` calling convention.
9. Two compile stages, two failure surfaces (`temp/_transpiled*.c` as the
   diagnostic of record).

## A.6 MIR Direct transpiler & JIT (LR_07)

<a id="lr07-r1"></a>**LR07-R1 · Indirect calls cap at 3 arguments · RESOLVED**
The `mir: calls with >3 args not yet fully supported` log and its wrong-value
return are gone. `transpile_call`'s dynamic path
(`transpile-mir.cpp:18132`ff) now dispatches
`fn_call0_into` / `fn_call1_into` / `fn_call2_into` / `fn_call3_into` for
0–3 args and **`fn_call_into` for any higher arity** (`:18152`), with each
argument boxed and rooted through `create_gc_root_slot` before the call.

<a id="lr07-r2"></a>**LR07-R2 · Parallel inference metadata tables · RESOLVED**
The `param_types[16]`, `param_mir[16]`, fixed alias-name table, and copied
32-entry parameter-name table are retired (no occurrences remain). Per-parameter
inference lives on the AST / function-analysis records. Core source arity is
capped only by the intentional `LAMBDA_MAX_FUNCTION_ARGS` language limit;
LambdaJS source formals stay dynamically represented. Remaining fixed
source-name staging buffers are tracked in
`vibe/Lambda_Design_Function_Arg.md`.

<a id="lr07-r12"></a>**LR07-R12 · Magic JIT layout offsets · RESOLVED 2026-09-05**
The JIT's `EvalContext.heap` and `Heap.gc` hops now derive from `offsetof` once,
with layout assertions; the remaining equivalent `64`-byte runtime-state load
uses the same named offset. Generated MIR no longer inherits these struct
positions as literals.

## A.7 Memory management & GC (LR_08)

<a id="lr08-r8"></a>**LR08-R8 · Dead free/frame stubs · RESOLVED 2026-09-05**
The unreferenced `free_item`, `free_container`, `frame_start`, and `frame_end`
no-ops and the lone public declaration are removed. Current ownership is the
precise GC and root-frame model required by **D1.5**; no compatibility caller
remained in the tree.

## A.8 Error handling (LR_10)

<a id="lr10-r1"></a>**LR10-R1 · Error code / table drift · RESOLVED**
`ERR_RETURN_OUTSIDE_FUNCTION` (227) and `ERR_UNHANDLED_ERROR` (228) now have
rows in `error_code_table[]`
(`lambda/runtime/lambda-error.cpp:128`–`129`), matching the enum
(`lambda-error.h:98`–`99`). `err_code_name`/`err_code_message` resolve them
instead of returning `"UNKNOWN_ERROR"`. There is still no compile-time check
that enum and table agree, so the two-places rule stands as a maintenance note.

<a id="lr10-r2"></a>**LR10-R2 · `err_free_stack_trace` leaks strdup'd native frame names · RESOLVED**
`err_free_stack_trace` (`lambda-error.cpp:1178`–`1188`) now frees the duplicated
name for native frames before freeing the node:

```c
// native frame names are duplicated during capture; Lambda frame names are debug-table owned.
if (trace->is_native && trace->function_name) mem_free((void*)trace->function_name);
```

Lambda-JIT frames still point at table-owned names, so the ownership split is
now explicit and correct.

<a id="lr10-r3"></a>**LR10-R3 · Release stack-trace frame counter · RESOLVED 2026-09-05**
`total_frames_found` and both increments are now ordinary code rather than
depending on release logging macro elision. The diagnostic path is build-mode
independent, preserving the error information expected by **S7.4.4**.

<a id="lr10-r4"></a>**LR10-R4 · Mismatched stack-trace depths · RESOLVED 2026-09-05**
All ordinary Lambda error paths use
`LAMBDA_ERROR_STACK_TRACE_DEFAULT_MAX_FRAMES` (64), the same default used by
raw and materialized capture. `set_runtime_error_no_trace` remains the explicit
low-stack escape hatch. Regression:
`StackTraceTest.RawStackTraceUsesSharedDefaultDepth`.

## A.9 Mark data API (LR_11)

<a id="lr11-r1"></a>**LR11-R1 · Stale `.bak` in tree · RESOLVED (for Lambda sources)**
`lambda/mark_editor.cpp.bak` is gone, as are the sibling Lambda-side `.bak`
files. The only remaining `.bak` files are inside the **vendored**
`lambda/tree-sitter-typescript/` import
(`define-grammar.js.bak`, `src/grammar.json.bak`, `src/parser.c.bak`), which
CLAUDE.md rule 16 puts off limits for in-place edits — they are upstream
artefacts, not Lambda drift.

## A.10 Schema validator (LR_13)

<a id="lr13-r1"></a><a id="a8-schema-validator-lr_13"></a>**LR13-R1 · The dead unified-schema model · RESOLVED**
`schema_builder.cpp` (which could not compile — it referenced an undefined
`VariableMemPool` and was excluded from every build target) and `schema_ast.hpp`
were deleted, along with their three stale `exclude_source_files` entries and
`schema_builder.cpp.bak`. The two surviving structs (`TypeDefinition`,
`TypeRegistryEntry`) moved to `validator/validator.hpp`. This retires the "two
parallel type vocabularies" hazard ([LR03-R1](#a3-value--type-model-lr_03)); the
`TODO` it carried (map fields → runtime shape) went with it.

## A.11 Runtime builtins (LR_09)

<a id="lr09-r1"></a>**LR09-R1 · String-comparison inconsistency · RESOLVED (stale)**
`fn_eq`, `fn_lt_scalar`/`fn_gt_scalar`, and the sort total order all compare
strings by raw bytes and are mutually consistent. The utf8proc casefold
comparators are markup-parser-only and their Item-level wrappers have no callers
([LR05-R3](#lr05-r3)). Any future
collation support must be an explicit opt-in governing equality and ordering
together, not an operator change.

<a id="lr09-r2"></a>**LR09-R2 · `split` does not split on a pattern delimiter · RESOLVED**
Not a missing implementation: `pattern_split` (`re2_wrapper.cpp:1124`) computed
the right segments all along, and `list_push` then merged them back together.
`list_push` concatenates a pushed string onto the previous element unless
the eval context suspends it (`collection_io.cpp:90` — the condition does not
consult `is_content`, so it applies to every string push; the suspension flag
has since been retired, see below). `fn_split`'s
**string** path suspends merging around its own loop (`lambda-eval.cpp:5553`),
but the **pattern** path returns before reaching it (`:5530`, and `fn_split3` at
`:5677`), so every pattern split collapsed into one element. The keep-delimiters
form was the proof: segments *and* delimiters were all produced correctly, then
concatenated back into the input verbatim.

`pattern_split` now owns the suspension via an RAII guard, covering both callers
and restoring the flag on all of its early-return paths.

Fixing that exposed a second, independent defect in the same loop: one `pos`
cursor served as both the start of the pending segment and the resume point for
the next search, so a zero-length match's `pos++` stepped the *segment start*
over a character that then appeared in no segment at all —
`split("ab", \(d*))` returned `["", "", ""]`, losing `a` and `b`. The cursor is
now split into `seg_start` and `search`, and a zero-width advance steps a whole
codepoint so slices stay on character boundaries.

With the segments correct, the zero-width edge was still under-determined —
Python emits leading/trailing empties there, ECMAScript does not. Ratified as
**S17.1.1 / S1.11 (spec v15.1.0, decision record C18): `split` follows
ECMAScript.** The argument was internal rather than comparative: Lambda's own
empty-*string* delimiter already behaved like JS (`split("ab", "")` =
`["a", "b"]`), so following Python would have made the pattern path contradict
its sibling in the same function — the very inconsistency this fix set out to
remove. `pattern_split` now implements ECMAScript's `e == p` rule (a match
ending on the segment start contributes no segment, only advancing the search),
its loop bound is `search < len` rather than `<= len`, and both paths return
`[]` for an empty subject whose delimiter matches empty and `[""]` otherwise.

All 14 edge cases — leading, trailing, no-match, empty subject, empty
delimiter, zero-width, and UTF-8 zero-width — now match Node byte-for-byte, and
the six examples in `doc/Lambda_Sys_Func.md:463`–`468` hold. Covered by
`test/lambda/split_pattern.ls` across both tiers; `doc/Lambda_Sys_Func.md` gains
an edge-case table and the `doc/Lambda_Cheatsheet.md` defect note is removed.
Note the original ledger table's expected value for `split("a1b22c3", \(d+))`
was internally inconsistent — it omitted the trailing empty segment that the
spec and the sibling `\(d)` row require.

Follow-up: `pattern_split` and `fn_split`/`fn_split3` were later converted from
`list_push` + a merging suspension to plain `array_push` (D2.6.5), removing the
RAII guard, both flag set-sites and all six restore points — net −18 lines, and
`split` no longer touches the global flag at all. Output is byte-identical.

<a id="lr09-r3"></a>**LR09-R3 · `varg()` applies content normalization to the argument list · RESOLVED**
A variadic call collected its rest arguments with `list_push`, which applies
S16.7's content rules: `null` is dropped outright (`collection_runtime.cpp:286`)
and a string is concatenated onto the previous element unless the eval context
suspends merging. An argument list is neither the script top level nor a
container, so neither rule had a ruling behind it — and both destroy arity:
`n("a","b")` arrived as `["ab"]`, `n(1,null,2)` as `[1,2]`, and
`n("x",null,"y")` as `["xy"]`, three arguments collapsed into one. Numeric
arguments are unaffected, which is why it survived: every variadic example in
the docs and tests summed numbers, and `len(varg())` was the only quick tell.

Both builders now append with `array_push`, which writes the item verbatim:
`emit_variadic_args` (`transpile-mir.cpp`) for the MIR tier, and the
dynamic-call adapter (`lambda-eval.cpp:1231`) for T0 and `call()`. `array_push`
keeps the same content-list flattening as `list_push`, so the change removes
exactly the normalization this list never wanted and nothing else — an argument
that *is* a content list still arrives as one value
(`n(for (x in [1,2]) x)` → `[[1, 2]]`).

Verified on both tiers, including `varg(i)` indexing, a fixed-plus-rest
signature, an all-`null` argument list, and `call()` (S12.3.4), which shares the
adapter. `test/std/core/functions/variadic_args.ls` loses the note that kept it
to numeric arguments and now covers the string/`null` cases directly.

**Generalized to D2.6.5** (Formal Design v1.27.0): the append API *is* the
choice of content normalization — `list_push` for element and script top-level
content, `array_push` for every other collection — and a builder must never
re-express the choice as ambient state — the process-wide suppression flag that
used to exist for exactly that purpose has been retired.

A sweep of all 152 `list_push` call sites followed. The ~120 in
`lambda/input/markup/**` are correct: they build genuine element content. Of the
rest, **19 more sites had the same defect** and were converted: 17 in
`lambda-vector.cpp` (`reverse`, `take`, `drop`, `zip`, `array_split`, `shape`,
`math_random`, pipe-collect, vector ops), the JS→Lambda `start()` argument list
(`concurrency_js.cpp:169`), and `call()`'s packed-array widening
(`lambda-eval.cpp:1284`). `reverse(["a","b","c"])` returned `["cba"]` and
`take(["a","b","c"], 2)` returned `["ab"]`; `reverse([1,null,2])` returned
`[2,1]`. Reviewed and deliberately left on `list_push`: the markup parsers,
`collection_runtime.cpp` (that *is* the content recursion), `pattern_find`/
`fn_find` (push maps), `input-mark.cpp` (pushes into an element), and `path.c`.

`test/std/core/functions/collection_reverse.expected` had **encoded both bugs as
expected output** (`["cba"]`, `[false, true]`) — the suite was ratifying the
defect. Regenerated. The neighbouring tests could not have caught it either:
`take_drop.ls` used only numbers and `zip.ls` only ever paired a number with a
string, so two adjacent strings never met; both now cover strings and `null`.

Note the merge half is ambient-dependent — string merging is gated on an active
input context while null-stripping is unconditional (Design Appendix A, D2.6.5),
which is why the same function could merge in one call path and not another.

The `disable_string_merging` flag turned out to be **vestigial**: its sole
assignment set it to `false` (`input.cpp:1061`) and nothing anywhere set it
`true`, so no input format selected normalization through it. It has now been
retired — removed from `EvalContext` and `InputAllocationContext`, from
`list_push_with_owner`'s signature and both of its callers, and from the merge
gate — with output byte-identical before and after. Per-format policy
lives in the *builder* instead — MarkBuilder formats (latex, json, xml, yaml,
toml, csv, pdf, …) append with `array_append` and never normalize, which is why
LaTeX may hold consecutive strings, while the markup family (markdown, asciidoc,
textile, wiki) calls `list_push` and merges them. `input-ics.cpp` and
`input-mark.cpp` use both and so mix the two policies — worth reconciling, along
with retiring the dead flag.

<a id="lr09-r4"></a><a id="lr10-3"></a>**LR09-R4 · `set_runtime_error` message buffer cap · RESOLVED 2026-08-28**
`err_createf` and `set_runtime_error` now share the exact-size variadic
formatter backed by `mem_alloc`, so long diagnostics are not silently
truncated at 1023 bytes. The shared 64-frame trace default is recorded in
[LR10-R4](#lr10-r4). This also closes the duplicate LR10-3 index entry; its stable anchor
is retained here. Regression: `ErrorCreationTest.CreateFormattedErrorPreservesLongMessage`;
the error suite passes 121/121 and the Lambda baseline passes 3978/3978.

## A.12 Procedural runtime (LR_12)

<a id="lr12-r2"></a>**LR12-R2 · Mutation builtins swallow type errors · RESOLVED 2026-08-26**
`pn_push` and `pn_splice` now return `ItemError` for invalid owners, indices,
counts, views, and N-D arrays instead of returning the unchanged input. Their
registry rows publish `may_return_error`, so `or` recovery can observe the
failure. The successful owner-returning convention remains unchanged; only
the unresolved choice between updated-owner and unit conventions in
`S7.10.6` remains open. Targeted invalid-mutation probes and the full baseline
pass.

<a id="lr12-r8"></a>**LR12-R8 · `push`/`splice` mutate a module-level `let` in place, falsifying `fn` purity · RESOLVED 2026-08-26**
The COW selector only found local `MirVarEntry` bindings, so a module-level
binding fell through to the raw in-place mutator. The fix applies the existing
E211 immutable-root validation to the builtin's owner argument during AST
construction, rather than silently copying in the COW path. This enforces
`S9.1.1` and `S9.1.6`: mutation through a module-level `let` is rejected, while
the caller must use an allowed mutable owner. A targeted module-let probe now
raises E211 and the full baseline passes 3914/3914.

---

## A.13 Sibling vibe ledgers

<a id="issues0-r9"></a>**Issues0 #9-R · ShapePool hash collision reused a different shape · RESOLVED 2026-08-27**
The hashmap now confirms the existing structural shape comparison after the
signature routing key, and retains element names as cache identity metadata.
This prevents a colliding signature from reusing a shape with different field
names, types, offsets, flags, or element identity. The focused regression is
`NamespaceTest.ShapePoolCollisionDoesNotAliasDifferentFieldNames`; the full
namespace suite passes 39/39 and `make test-lambda-baseline` passes 3976/3976.

<a id="i8-consoleesc-r"></a>**Issues8 · Console formatter does not escape quotes or backslashes inside collections · RESOLVED 2026-08-27**
The collection printer's Item and legacy TypedItem string/symbol branches used
raw buffer interpolation, so quotes, backslashes, and control characters made
collection output ambiguous. The shared length-based `print_quoted_text` helper
now emits the grammar-supported Lambda escapes for collection members. Standalone
strings retain their existing display behavior to avoid double-escaping an
already serialized string; no new data structure or design ruling was needed.

Regression: `NamespaceTest.PrintCollectionEscapesStringContents` covers quote
and backslash members. The 42 affected golden outputs were regenerated from
the fixed printer, and `make test-lambda-baseline` passes 3976/3976.

<a id="lint-e1-r"></a>**Lint E1 · Unchecked allocation dereference in `build_ast.cpp` · RESOLVED 2026-08-27**
The custom `mem_alloc` calls and `strview_to_cstr` literal copies were
fallible, but their callers read the returned buffers before checking them.
`ast_copy_source_text` now centralizes the seven literal source-copy sites,
reports `E309`, and returns the existing `TYPE_ERROR` recovery value. This
keeps the literal path aligned with **D4.2.1v3** and **D4.2.2v2** without
introducing a new data structure or design rule.

Regression: `AstBuildAllocationTest.SizedLiteralCopyFailureDoesNotCrash`.
The focused error suite passes 120/120 and `make test-lambda-baseline` passes
3976/3976.

---

# Appendix B — Cross-cutting clusters

Several ledger entries are one defect wearing different masks. Fix them
together, not individually.

| Cluster | Entries | Root |
|---|---|---|
| **Honest static types** | LR07-7, LR08-3, LR11-6, LR12-3 | The collector, the TCO gate, and the stack-check gate all trust transpiler type classification. Until that is provable, all three stay pessimistic. Fix per CLAUDE.md rule 15 with precise `RootFrame`/`Rooted` ownership. |
| **Representation ↔ semantics coupling** | LR03-3, LR07-1, LR07-5, LR07-14 | Expression results carry no `ValueRep`; each consumer re-derives it. See [Result32 lane-parity + Tune19], [Compiling lane design]. |
| **Value-semantics residue (OI-1)** | LR03-1, LR04-4, LR09-3 | Second equality walker, `decimal_cmp` failure-as-equality, VMap key eq/hash rank consistency. Tracked as OI-1 in this ledger's [§15](#15-design-gaps-inherited-from-the-retired-outstanding-rollup-oi). |
| **`INT64_MAX` sentinel collision** | LR03-4, LR07-4 | `INT64_ERROR == INT64_MAX` and `INT_LANE_INF` share one bit pattern; index OOB also lands on `INT64_MAX`. The former LR10-5 entry is a preserved alias for LR03-4. See [v5 int migration in flight]. |
| **Hard-coded byte offsets** | LR01-8 | Module binding still reads struct fields at literal offsets that no `static_assert` protects. GC tracing is resolved by [LR08-5](#lr08-5). |
| **Silent-truncation caps** | LR01-5, LR01-6, LR03-2, LR05-6, LR07-11, LR08-6, LR08-10, LR11-4, LR13-4 | Every one of these fails by quietly dropping data rather than erroring. The truncate-vs-error inconsistency (LR11-4) is the clearest statement of the pattern. |
| **Surface syntax (S16) residue** | LR02-16, S16.9.5, i8-genafterlet, SO36, O3, §7.17 | S16.1–S16.6.7 are conformant on the harness (140/140 C, 135/135 Tree-sitter); S16.6.8/S16.6.9 (procedural blocks are not expressions; branch homogeneity) were ratified AND implemented 2026-08-24 in build_ast (E312); harness now 152/152 C, 135/135 Tree-sitter. SO36 (pn calls in expressions) is deliberately open. What remains is not the line-delimiter design but the type sublanguage and the paired `for`: forms that parse and then behave wrongly or inconsistently by position. See [Design_Syntax §6–§7](Lambda_Design_Syntax.md). |
| **Process globals** | LR01-12, LR12-6 | `g_template_registry` and `g_dry_run` block concurrent runtimes. See RG1–RG14 in [Runtime globals audit], RC1–RC8 in [Radiant concurrency design]. |

---

# Appendix C — Maintaining this ledger

1. **This file is the working list; `LR_*` sections stay as design record.**
   When an `LR_*` "Known Issues" section changes, mirror the change here with
   the same `LRnn-k` ID. IDs are stable — a resolved issue keeps its number and
   moves to Appendix A with an `-R` suffix rather than being renumbered.
2. **Re-verify before acting.** Every `file:line` in this document was resolved
   on 2026-08-24 against `c568f0f93` and will drift. Grep the quoted identifier,
   not the line number.
3. **Cite rulings by formal-spec ID** (CLAUDE.md rule 17): `S#` from
   `doc/Lambda_Formal_Semantics.md`, `D#` from `doc/Lambda_Formal_Design.md`;
   vibe ledger IDs (OI-#, TE-#, RG-#, RC-#, TIG#) only where no formal ruling
   covers the point.
4. **Do not close an issue from a doc edit alone.** A resolution needs a
   verified source anchor, as every Appendix A entry has.
