# Lambda Core Runtime — Central Issue Ledger

> **Consolidated from the per-area "Known Issues & Future Improvements" sections of
> [`doc/dev/lambda/LR_01`–`LR_13`](../doc/dev/lambda/LR_00_Overview.md).**
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

Counts:

| Source doc | Area | Open | Partial | Resolved | Total |
|---|---|---:|---:|---:|---:|
| LR_01 | Compilation pipeline, CLI & REPL | 11 | 2 | 2 | 15 |
| LR_02 | Parsing & AST construction | 6 | 4 | 10 | 20 |
| LR_03 | Value & type model | 6 | 1 | 2 | 9 |
| LR_04 | Numbers, decimal & datetime | 8 | 0 | 0 | 8 |
| LR_05 | Strings, symbols & vectors | 7 | 1 | 2 | 10 |
| LR_06 | C transpiler (legacy C2MIR) | 0 | 0 | 9 | 9 |
| LR_07 | MIR Direct transpiler & JIT | 13 | 1 | 2 | 16 |
| LR_08 | Memory management & GC | 10 | 0 | 0 | 10 |
| LR_09 | Runtime builtins | 9 | 0 | 1 | 10 |
| LR_10 | Error handling | 5 | 1 | 2 | 8 |
| LR_11 | Mark data API | 8 | 0 | 1 | 9 |
| LR_12 | Procedural runtime | 7 | 0 | 0 | 7 |
| LR_13 | Schema validator | 7 | 0 | 1 | 8 |
| **Total** | | **97** | **10** | **32** | **139** |

The 131 total exceeds the 127 items in the source sections for two reasons.
Two original entries each split into a resolved half and a surviving residue —
LR_03 #4 (sentinels → LR03-4 + LR03-5) and LR_05 #3 (two string orderings →
LR05-R2 + LR05-3). And two defects were **found during verification** rather
than extracted: LR02-8 through LR02-10, each marked as such in place (a fourth, LR02-11, was fixed the same day and is now LR02-R6).

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

<a id="lr01-1"></a>**LR01-1 · `sys://` paths in maps/elements are never resolved · OPEN**
`resolve_sys_paths_recursive` (`lambda/runtime/runner.cpp:1530`) traverses only
`LMD_TYPE_PATH` and array/list. Map and Element traversal is still skipped
behind `// TODO: Investigate why map->data access crashes for some maps`
(`runner.cpp:1546`) because walking map data segfaulted on a csv test. A real
correctness gap, not just a cap.

<a id="lr01-2"></a>**LR01-2 · `serve` is a stub · OPEN**
The subcommand exists but does nothing: `// TODO: Phase 5 — instantiate Server,
configure, and run` (`lambda/main.cpp:3818`).

<a id="lr01-3"></a>**LR01-3 · Unescaped LaTeX bridge filename · OPEN**
The inline LaTeX→HTML bridge `snprintf`s `input_file` directly into a Lambda
string literal in a `char script_buf[4096]` with **no escaping**
(`main.cpp:1448`–`1462`); a path containing `"` or `\` yields broken or
injectable Lambda source. The PDF bridge escapes correctly via
`lambda_string_literal_escape` (`main.cpp:1021`, used `:1057`) — the two paths
are still inconsistent.

<a id="lr01-4"></a>**LR01-4 · `target_equal` compares hash-only · OPEN**
Now at `lambda/core/target_identity.cpp:4`–`8`: `return first->url_hash ==
second->url_hash;` with no fallback string compare. A hash collision yields
false-positive target equality.

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

<a id="lr02-5"></a>**LR02-5 · `match`-arm `~` references can be missed · OPEN**
`has_current_item_ref` (`build_ast.cpp:3954`) walks a match node's scrutinee and
then iterates the arm list **without inspecting anything**:

```c
AstMatchArm* arm = match_node->first_arm;
while (arm) { arm = (AstMatchArm*)arm->next; }
return false;                                  // build_ast.cpp:3987–3991
```

A `~` reference inside a match arm under a pipe goes undetected. The loop is now
provably dead code, which makes this strictly worse than the doc's "may be
missed" phrasing.

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


<a id="lr02-9"></a>**LR02-9 · Binary `&` / `!` type operators work in pattern position but not in annotation position · OPEN**
*Found during the 2026-08-24 verification pass; not from the LR_02 section.*
Intersection (`&`) and exclusion (`!`) parse everywhere `|` does, and evaluate
**correctly as patterns** — but a declaration annotation rejects them:

| Position | `int \| string` | `int & string` | `int ! string` |
|---|---|---|---|
| `x is (…)` | ✓ | ✓ (`false` for `x = 1`) | ✓ (`true` for `x = 1`) |
| `match { case … }` | ✓ | ✓ | ✓ |
| `let a: … = 1` | ✓ | `E201: cannot initialize 'a' of type type with int` | same |
| `fn f(a: …)` | ✓ | `E207: argument 1 expected type, got int` | same |

So the type expression is built, and the pattern path consumes it, but the
annotation path receives a `LMD_TYPE_TYPE`-tagged *value* instead of a type. The
asymmetry is visible in the consumers: `pattern_ast_literal_set`
(`build_ast.cpp:4309`) and `interp_pattern_matches` (`interp.cpp:2013`) each
branch on `OPERATOR_UNION` **only**, and `OPERATOR_INTERSECT` /
`OPERATOR_EXCLUDE` (`build_ast.cpp:3713`–`3714`) fall through them — the
`is`/`match` results above come from a separate `fn_is` path, not these.

Two consequences worth separating: the **capability** gap (annotations cannot
use `&`/`!`) and the **diagnostic** gap — both errors name the binding or the
argument, never the unsupported operator, so a reader is sent to the wrong
place. The diagnostic is the cheap half.

This is the implementation face of **SO9** ("a surface spelling for
`any \ error`; the `!` exclusion operator route is broken") and of the
`&`/`!`-unimplemented warning in the string-pattern design record.


<a id="lr02-13"></a>**LR02-13 · `call()`'s runtime colour check cannot see closures with a NULL `fn_type` · OPEN**
*Found 2026-08-25 implementing S12.1.4.* The pn-from-fn check reads the target's
colour from `Function.fn_type` → `TypeFunc.is_proc`, but `to_closure` and
`to_closure_named` leave `fn_type` NULL; it is filled only by
`lambda_function_set_type`. A **dynamically selected** pn target therefore slips
through: `fn f() => call([p][0], [1])` runs the procedure instead of erroring.
The static check in `build_ast` covers the ordinary case — a directly named
target is rejected at compile time — so the gap is confined to a callee the
static side cannot resolve, which is exactly where S12.1.4 expected the runtime
check to carry the weight. Fix by populating `fn_type` at closure creation.

---

## 3. Value & type model (LR_03)

<a id="lr03-1"></a>**LR03-1 · `item_deep_equal` is a weaker second equality walker · OPEN**
`item_deep_equal` (`lambda/core/lambda-data.cpp:1346`) remains a shallower
walker than `fn_eq`, with a single caller — Radiant's no-op elision
(`radiant/event.cpp:2300`). Its missing cases (`MAP`, `DECIMAL`, `DTIME`,
`UINT64`, `RANGE`, `VMAP`) fall to pointer equality and err conservatively
there: a missed elision forces a spurious DOM rebuild, never a wrong answer.
Remaining work (tracked as **OI-1** in `vibe/Lambda_Issues_Outstanding.md`):
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

<a id="lr03-4"></a>**LR03-4 · `it2l` error sentinel collides with a legitimate value · OPEN**
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

<a id="lr03-7"></a>**LR03-7 · Latent, not annotated · OPEN (note)**
The core value-model files still carry no `TODO`/`FIXME`/`HACK`/`XXX` markers;
the issues above are structural and will not surface in a tag grep.

---

## 4. Numbers, decimal & datetime (LR_04)

<a id="lr04-1"></a>**LR04-1 · "Unlimited" decimal is a 200-digit cap · OPEN**
`g_unlimited_ctx.prec = 200` (`lambda/core/lambda-decimal.cpp:46`) is a
workaround for `mpd_pow` crashing at `mpd_maxcontext` precision; the in-code
comment still reads "far more than needed for any practical computation"
(`:44`). Computations needing more than 200 significant digits silently round,
while the surface name says "unlimited".

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
feeding `decimal_cmp_items` (`:1023`). Also tracked under **OI-1**.

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

<a id="lr04-8"></a>**LR04-8 · No literal `TODO`/`FIXME` markers · OPEN (note)**
Expressed only as "for now" / "far more than needed" comments.

---

## 5. Strings, symbols & vectors (LR_05)

<a id="lr05-1"></a>**LR05-1 · `ndim` cap of 32 is unchecked in the helpers · PARTIAL**
*Fixed at construction:* `lambda/runtime/lambda-data-runtime.cpp:465` now
rejects `ndim < 1 || ndim > 32`, and `gc_heap.c:1925` / `lambda-eval.cpp:1825`,
`:1833` re-check before use.
*Residue:* the broadcast/stride helpers in
`lambda/runtime/lambda-vector.cpp` still declare fixed `int64_t shp[32],
str[32]` stack buffers with **no bound re-check** (`:696`, `:794`, `:3123`,
`:3271`, `:3357`, `:3391`, `:3463`, `:3581`, `:3663`, `:3680`). Any path that
reaches them with an unvalidated shape overruns the stack.

<a id="lr05-2"></a>**LR05-2 · Not full UCA collation · OPEN**
The utf8proc path is casefold-then-`memcmp`, not the Unicode Collation
Algorithm: no locale tailoring, no weight tables, no accent ordering. The
comment at `lambda/core/utf_string.cpp:57` still overstates it as "proper
Unicode collation".

<a id="lr05-3"></a>**LR05-3 · Dead `*_comp_unicode` Item wrappers · OPEN**
`equal_comp_unicode` / `less_comp_unicode` / `greater_comp_unicode` /
`less_equal_comp_unicode` / `greater_equal_comp_unicode`
(`lambda/core/utf_string.h:23`–`27`, defined `utf_string.cpp:144`ff) still have
**no callers**. Candidates for deletion, or for a future explicit collator API
that governs equality *and* ordering together (SQL/XQuery model) — never a
change to the core operators.

<a id="lr05-4"></a>**LR05-4 · `index_to_item` truncates int64 → int · OPEN**
`lambda/runtime/lambda-vector.cpp:1922`–`1924` does `i2it((int)index)`, casting
the `int64_t` index to 32-bit before boxing. Pipe `map`/`where` iteration over
collections longer than 2³¹ produces a wrapped `~#` index.

<a id="lr05-5"></a>**LR05-5 · `fn_label` bypasses the GC with raw `malloc`/`free` · OPEN**
The flood-fill stack is `malloc`'d at `lambda-vector.cpp:4549` and released with
`free`, bypassing the runtime mempool/GC — contrary to CLAUDE.md's allocator
rule. Leak-free on the success path, but untracked memory that would leak if an
early return were inserted between the two.

<a id="lr05-6"></a>**LR05-6 · Fixed-size buffer truncation in stencils · OPEN**
`STENCIL_MEDIAN_CAP 4096` (`lambda-vector.cpp:4045`) rejects larger median
kernels (`:4070`) and backs a 4096-element stack `medbuf` (`:4086`); `fn_otsu`
hard-codes 256 bins in a stack `int64_t h[256]` (`:4501`).

<a id="lr05-7"></a>**LR05-7 · utf8proc allocation crosses the allocator boundary · OPEN**
Normalizers return raw utf8proc-allocated buffers that callers must `raw_free`
(`utf_string.cpp:64`–`65`, `:90`); the `RAWALLOC_OK` annotations acknowledge
this sits outside the pool/GC discipline.

<a id="lr05-8"></a>**LR05-8 · No `TODO`/`FIXME` markers · OPEN (note)**

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

<a id="lr07-12"></a>**LR07-12 · Magic struct offset for `heap->gc` · OPEN**
The prologue loads the GC handle with a hard-coded offset of `8`
(`transpile-mir.cpp:24078`, `:26721`) under a suppressed
`-Winvalid-offsetof` (`:62`, `:4470`) — a magic constant rather than an
`offsetof`, fragile if `Heap` layout changes. The adjacent `EvalContext.heap`
load *does* use `offsetof` (`:24074`, `:26717`) and there is a `static_assert`
on it (`:63`), so only the second hop is unprotected.

<a id="lr07-13"></a>**LR07-13 · TCO iteration ceiling · OPEN**
Tail-recursive loops emit a guard raising a stack-overflow error past
`LAMBDA_TCO_MAX_ITERATIONS` (`transpile-mir.cpp:24607`–`24611`); the interpreter
shares the constant (`interp.hpp:59`).

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

<a id="lr08-5"></a>**LR08-5 · Hard-coded struct byte offsets in tracing and compaction · OPEN**
`gc_trace_object` (`gc_heap.c:1413`) and `gc_compact_data` (`:1872`) read fields
at fixed offsets — Array items @+8 (`:1459`, `:1490`), length @+16 (`:1491`),
Map type @+8 / data @+16 (`:1526`–`:1527`), ShapeEntry type @+8 (`:1564`), and a
`+16` type-length read at `:1408`. Any change to
`Container`/`Map`/`Element`/`TypeMap`/`ShapeEntry`/`Function` layout silently
corrupts tracing or compaction; the `TypeId` enum aliasing defends the *enum
values* but not the *offsets*. `item_to_ptr` likewise assumes high-byte-zero
heap pointers — a documented-but-unenforced platform assumption.

<a id="lr08-6"></a>**LR08-6 · `SHAPE_POOL_MAX_CHAIN_LENGTH` = 64 silently returns NULL · OPEN**
Maps/elements with more than 64 fields get no pooled shape
(`lambda/core/shape_pool.cpp:182`–`183`, `:247`) — only a `log_warn`, with a
possible NULL-deref downstream depending on caller handling.

<a id="lr08-7"></a>**LR08-7 · Deep recursion consumes root and number watermarks as well as C stack · OPEN**
Frames no longer allocate heap root blocks, but recursion accumulates each
function's statically reserved slots until the epilogue restores them. The
side-stack bound check or the C-stack guard terminates pathological depth,
whichever fires first.

<a id="lr08-8"></a>**LR08-8 · Dead stubs · OPEN**
`free_item`, `free_container`, `frame_start`, `frame_end`
(`lambda/runtime/lambda-mem.cpp:1291`–`1305`, declared `transpiler.hpp:61`) are
no-op API-compat relics — harmless but dead code.

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

<a id="lr09-6"></a>**LR09-6 · `set_runtime_error` buffer cap · OPEN**
The message is formatted into a fixed `char message[1024]` stack buffer
(`lambda-eval.cpp:141`–`147`) and silently truncated; the stack-trace depth is
hard-coded to 32 frames. Shared with the broader error machinery
([§10](#10-error-handling-lr_10)).

<a id="lr09-7"></a>**LR09-7 · No tags in source · OPEN (note)**
The registry caveats carry no `TODO`/`FIXME`/`HACK`; they are discoverable only
by reading the commented-out block, the "transpiler special case" notes, and the
`NULL` pointers.


<a id="lr09-9"></a>**LR09-9 · `varg()` applies content normalization to the argument list · OPEN**
*Found 2026-08-25 while repairing `test/std/core/functions/variadic_args.ls`; pre-existing (reproduced at clean HEAD).*
A variadic function's arguments are collected as **element content** rather than
as a plain array, so S16.7's content rules — adjacent strings merged, nulls
stripped — are applied to them:

| Call (`fn n(...) => varg()`) | Result | Expected |
|---|---|---|
| `n("a", "b")` | `["ab"]` | `["a", "b"]` |
| `n("a", "b", "c")` | `["abc"]` | `["a", "b", "c"]` |
| `n("a", null, "b")` | `["ab"]` | `["a", null, "b"]` |
| `n(1, null, 2)` | `[1, 2]` | `[1, null, 2]` |
| `n("a", 1, "b")` | `["a", 1, "b"]` | ✓ (non-adjacent strings survive) |

Numeric arguments are unaffected, which is why it went unnoticed: every
variadic example in the docs and tests sums numbers. `len(varg())` is the
quickest tell — it reports 1 for `n("a","b")`.

S16.7 governs the **script top level**, and containers explicitly do *not*
normalize; an argument list is neither, so the normalization has no ruling
behind it here. Fixing it means collecting varargs as a plain array rather
than through the content builder.

<a id="lr09-8"></a>**LR09-8 · `split` does not split on a pattern delimiter · OPEN**
*Found during the 2026-08-24 doc sweep; not from the LR_09 section.*
`split(str, delim)` is documented to accept "both plain strings and named
patterns" as the delimiter. It works for a **string** delimiter and silently
misbehaves for a **pattern** one — the matches are stripped and the remainder is
returned as a single element, i.e. it behaves like `replace(pattern, "")`:

| Call | Result | Expected |
|---|---|---|
| `split("a,b,c", ",")` | `["a", "b", "c"]` | ✓ |
| `split("a-b-c", "-")` | `["a", "b", "c"]` | ✓ |
| `split("a1b2c3", \(d))` | `["abc"]` | `["a", "b", "c", ""]` |
| `split("hello   world", \(s+))` | `["helloworld"]` | `["hello", "world"]` |
| `split("a1b22c3", \(d+))` | `["abc"]` | `["a", "b", "c"]` |
| `split("a1b2c3", \(d), true)` | `["a1b2c3"]` | keep-delimiters form; input returned unchanged |

The failure is silent: the return is a well-formed one-element array, so a caller
that indexes `[0]` gets a plausible-looking string rather than an error. `len()`
is the quickest tell — it is `1` for every pattern case above.

The sibling pattern-aware functions are **correct**, which localises the defect
to `split`'s delimiter handling rather than to pattern matching itself:
`replace("a1b2c3", \(d), "X")` → `"aXbXcX"`, and
`len(find("a1b22c333", \(d+)))` → `3`.

`doc/Lambda_Cheatsheet.md` now shows the intended results with a note pointing
here, rather than results it does not produce.

---

## 10. Error handling (LR_10)

<a id="lr10-1"></a>**LR10-1 · `total_frames_found` asymmetric `NDEBUG` guard · OPEN (latent)**
The counter is declared and incremented only under `#ifndef NDEBUG`
(`lambda/runtime/lambda-error.cpp:636`–`638`, `:686`–`688`, `:723`–`725`), yet
the final `log_info(... total_frames_found)` at `:750` references it with **no
guard**. This is not a release build break: under `NDEBUG` (and without
`LOG_IMPL`) `lib/log.h` defines `log_info(...)` as `((void)0)`, a variadic macro
that textually discards its arguments, so the preprocessed output never mentions
the identifier. The asymmetry is nonetheless fragile — it depends entirely on
`log_info` staying macro-elided. The clean fix is to move the
declaration/increment out of the guard, or guard the log line to match.

<a id="lr10-2"></a>**LR10-2 · Hard-coded 64 KB last-function span · OPEN**
`build_debug_info_table` computes each function's end address as the next
function's start; the *last* function has no successor and is given a fixed
64 KB span (`info->native_addr_end = native_addr_start + 65536`,
`lambda/runtime/mir.c:646`). A JIT function larger than 64 KB placed last in
address order mis-attributes return addresses past that boundary, silently
dropping or mislabeling the deepest frame.

<a id="lr10-3"></a>**LR10-3 · Fixed error buffers truncate silently · OPEN**
`err_createf` / `set_runtime_error` format into 1024-byte stack buffers,
`err_format` into `char buffer[4096]` (`lambda-error.cpp:857`),
`err_format_with_context` into `char buffer[8192]` (`:894`), `err_format_json`
into `[4096]` (`:1074`), and the caret span is capped at 20 characters (`:954`).
All truncate without signaling.

<a id="lr10-4"></a>**LR10-4 · Two stack-trace capture depths and a trace-free path · PARTIAL**
`set_runtime_error` and `fn_error` pass `max_frames = 32` while
`err_capture_stack_trace` defaults to 64 when passed `<= 0`
(`lambda-error.cpp:617`); `set_runtime_error_no_trace` captures nothing.
*Improved:* the newer `err_capture_raw_stack_trace` clamps explicitly —
default 64, hard max 128, capacity max 1024 (`:755`–`760`).
*Residue:* the callers still pass 32, so deep recursion — the very case where a
trace is most wanted — is silently truncated.

<a id="lr10-5"></a>**LR10-5 · `it2l` sentinel collides with a legitimate value · OPEN**
Owned by [LR03-4](#lr03-4)
but bears on error handling: any caller using the sentinel to detect conversion
failure cannot distinguish it from a real maximum int64.

<a id="lr10-6"></a>**LR10-6 · No source-level markers · OPEN (note)**

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

<a id="lr12-2"></a>**LR12-2 · Mutation builtins swallow type errors · OPEN**
`pn_push` (`lambda/runtime/collection_runtime.cpp:211`–`219`) returns its input
unchanged with only a `log_error` when handed a non-`LMD_TYPE_ARRAY` value;
`pn_splice` (`:227`) does the same on a wrong-type array (`:230`) and on a
non-integer `start`/`count` (`:237`). None propagate an error Item, so a
mis-typed `push`/`splice` fails invisibly — the script sees an unmodified array
with no signal.

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

---

# Appendix A — Resolved and obsolete issues

Kept for provenance: each of these appeared in an `LR_*` "Known Issues" section
and was verified fixed or removed on 2026-08-24. Do not re-open without
re-verifying against current source.

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
Regression test `test/lambda/call_dynamic_apply.ls` + `.txt`. Residue tracked as
LR02-13.

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
tag/attribute matching. The dead Item-level wrappers survive as
[LR05-3](#lr05-3).

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

## A.7 Error handling (LR_10)

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

## A.8 Mark data API (LR_11)

<a id="lr11-r1"></a>**LR11-R1 · Stale `.bak` in tree · RESOLVED (for Lambda sources)**
`lambda/mark_editor.cpp.bak` is gone, as are the sibling Lambda-side `.bak`
files. The only remaining `.bak` files are inside the **vendored**
`lambda/tree-sitter-typescript/` import
(`define-grammar.js.bak`, `src/grammar.json.bak`, `src/parser.c.bak`), which
CLAUDE.md rule 16 puts off limits for in-place edits — they are upstream
artefacts, not Lambda drift.

## A.9 Schema validator (LR_13)

<a id="lr13-r1"></a><a id="a8-schema-validator-lr_13"></a>**LR13-R1 · The dead unified-schema model · RESOLVED**
`schema_builder.cpp` (which could not compile — it referenced an undefined
`VariableMemPool` and was excluded from every build target) and `schema_ast.hpp`
were deleted, along with their three stale `exclude_source_files` entries and
`schema_builder.cpp.bak`. The two surviving structs (`TypeDefinition`,
`TypeRegistryEntry`) moved to `validator/validator.hpp`. This retires the "two
parallel type vocabularies" hazard ([LR03-R1](#a3-value--type-model-lr_03)); the
`TODO` it carried (map fields → runtime shape) went with it.

## A.10 Runtime builtins (LR_09)

<a id="lr09-r1"></a>**LR09-R1 · String-comparison inconsistency · RESOLVED (stale)**
`fn_eq`, `fn_lt_scalar`/`fn_gt_scalar`, and the sort total order all compare
strings by raw bytes and are mutually consistent. The utf8proc casefold
comparators are markup-parser-only and their Item-level wrappers have no callers
([LR05-3](#lr05-3)). Any future
collation support must be an explicit opt-in governing equality and ordering
together, not an operator change.

---

# Appendix B — Cross-cutting clusters

Several ledger entries are one defect wearing different masks. Fix them
together, not individually.

| Cluster | Entries | Root |
|---|---|---|
| **Honest static types** | LR07-7, LR08-3, LR11-6, LR12-3 | The collector, the TCO gate, and the stack-check gate all trust transpiler type classification. Until that is provable, all three stay pessimistic. Fix per CLAUDE.md rule 15 with precise `RootFrame`/`Rooted` ownership. |
| **Representation ↔ semantics coupling** | LR03-3, LR07-1, LR07-5, LR07-14 | Expression results carry no `ValueRep`; each consumer re-derives it. See [Result32 lane-parity + Tune19], [Compiling lane design]. |
| **Value-semantics residue (OI-1)** | LR03-1, LR04-4, LR09-3 | Second equality walker, `decimal_cmp` failure-as-equality, VMap key eq/hash rank consistency. Tracked as OI-1 in `vibe/Lambda_Issues_Outstanding.md`. |
| **`INT64_MAX` sentinel collision** | LR03-4, LR07-4, LR10-5 | `INT64_ERROR == INT64_MAX` and `INT_LANE_INF` share one bit pattern; index OOB also lands on `INT64_MAX`. See [v5 int migration in flight]. |
| **Hard-coded byte offsets** | LR01-8, LR07-12, LR08-5 | Three subsystems read struct fields at literal offsets that no `static_assert` protects. A single layout change corrupts module binding, GC tracing, or the JIT prologue silently. |
| **Silent-truncation caps** | LR01-5, LR01-6, LR03-2, LR05-6, LR07-11, LR08-6, LR08-10, LR09-6, LR10-3, LR11-4, LR13-4 | Every one of these fails by quietly dropping data rather than erroring. The truncate-vs-error inconsistency (LR11-4) is the clearest statement of the pattern. |
| **Surface syntax (S16) residue** | LR02-9, LR02-10, SO9, SO36, O3, §7.17 | S16.1–S16.6.7 are conformant on the harness (140/140 C, 135/135 Tree-sitter); S16.6.8/S16.6.9 (procedural blocks are not expressions; branch homogeneity) were ratified AND implemented 2026-08-24 in build_ast (E312); harness now 152/152 C, 135/135 Tree-sitter. SO36 (pn calls in expressions) is deliberately open. What remains is not the line-delimiter design but the type sublanguage and the paired `for`: forms that parse and then behave wrongly or inconsistently by position. See [Design_Syntax §6–§7](Lambda_Design_Syntax.md). |
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
