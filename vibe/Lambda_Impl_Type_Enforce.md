# Lambda — Type Enforcement Implementation Plan, Round 2

**Status:** PLAN — 2026-07-30. Round 1 landed in commit `274625d56` (" type enforcement impl");
this plan covers what remains, headlined by the two items requested: value-aware numeric
admission and `any \ error` at fn/pn boundaries.
**Design authority:** `vibe/Lambda_Design_Type_Enforcement.md` (TE-1–TE-14, §10 decisions);
normative summary in `doc/Lambda_Formal_Semantics.md` §11.4.
**Evidence:** probe scripts `temp/tsd_*.ls`, re-run 2026-07-30 against the current binary;
coverage survey of `274625d56` (file:line anchors below are current-tree).
**Standing gates for every phase:** `make test-lambda-baseline` at 100%; every new test `.ls`
gets its `.txt` golden; typed-benchmark column within noise (enforcement stays ~free on hot
paths); the C backend (`transpile.cpp`) is FROZEN and receives none of this work.

---

## 1. Where the implementation stands (round-1 coverage)

Round 1 was far larger than a first cut — the **entire original corruption catalog is dead**
(all five silent-failure modes now produce diagnostics), verified by probe re-run:

| Boundary | Static | Dynamic | Anchor |
|---|---|---|---|
| Declaration init (`let/var x: T = e`) | ✅ E201 (`cannot initialize`) incl. **null** (TE-11 shipped) | ✅ `type check at declaration 'x' failed` | `build_ast.cpp:1188`/`:5365`; `transpile-mir.cpp:6607-6626` |
| Named-map/union literal fields | ✅ per-field E201 + missing-required-field | ✅ validator-delegated with path detail (`validator at .a: …`) | `build_ast.cpp:1146`; `lambda-eval.cpp:1455-1460` |
| Re-assignment | ✅ E201 | ✅ checked before commit | `build_ast.cpp:8257`; `transpile-mir.cpp:11612` |
| Call arguments | ✅ E207 (incl. statically-known error arg) | ✅ caller site + boxed callee prologue; error short-circuit preserves the original diagnosis | `build_ast.cpp:2937`; `transpile-mir.cpp:10613`,`:10677`,`:14492-14527` |
| Arity | ✅ E206 | ✅ dynamic-dispatch count check | `build_ast.cpp:2884`; `lambda-eval.cpp:716` |
| Returns (explicit + implicit tail) | ✅ E208 per-return-site walk | ✅ `type check at function return` | `build_ast.cpp:8456-8503`; `transpile-mir.cpp:11494`,`:15424` |
| Typed map field write | ✅ (member form) | ✅ transactional COW (`lambda_map_set_checked*`) | `build_ast.cpp:8204`; `lambda-eval.cpp:7021-7105` |
| Typed array element write | ✅ E201 | ✅ `lambda_array_set_checked*` | `build_ast.cpp:8163`; `lambda-eval.cpp:7116-7154` |
| `var` (inout) param publish | n/a | ✅ `_inplace` variants, pre-state validated | `transpile-mir.cpp:12391`,`:15250` |

Substrate that round 2 builds on: the `StaticBoundaryResult` tri-state
PROVEN/REJECTED/DEFERRED (`build_ast.cpp:1058-1080` — TE-2 implemented literally);
`declared_type` retained on `AstNamedNode`/`NameEntry` (`ast-core.hpp:255`,`:353`);
`emit_checked_boundary` (`transpile-mir.cpp:2241`, 8 sites) → `lambda_type_check`
(`lambda-eval.cpp:1442`) as the single runtime choke point; rich `lambda_type_error` objects
with validator paths and stack traces; JIT registry entries for the six checked helpers
(`sys_func_registry.c:1719-1724`); `f()^` now typed from the callee's declared return.

---

## 2. Work item 1 — value-aware numeric admission (`dynamic 3.0 → int` passes)

**Semantics** (TE-5/TE-6 as corrected 2026-07-30): at a DEFERRED boundary, a numeric value
whose *mathematical value* exactly embeds in the target **passes and is re-represented**
(`float 3.0 → int 3`; integral decimal → int; in-range value → sized int); an inexact or
out-of-range value fails with the rich error. Static positions still reject the whole class
(`let x: int = 3.0` stays E201). `is`/`match` stay type-directional (`3.0 is int` remains
false).

**Current state:** `lambda_type_matches`' numeric arm (`lambda-eval.cpp:1293-1300`) delegates
to `numeric_type_subsumes` — carrier-only, no value inspection; `validator_numeric_item_embeds`
and `decimal_to_int64_exact` are unreferenced by boundary code. Verified: an ANY-held `3.0`
into `fn f(x: int)` errors "expected int, got float 3". Same carrier-only logic also rejects
in-range `int → int8` regardless of value.

**Placement rule (critical):** the value-aware arm goes in **`lambda_type_check` only** —
never in `lambda_type_matches`, which is shared with `fn_is` (adding it there would silently
flip `3.0 is int` to true, deliberately left undecided).

**Tasks:**
1. New runtime helper `lambda_numeric_value_admit(Item, Type* target)` → returns the
   *converted* Item on exact embed, or a miss signal. Cover: float→int/int64 (integral value
   in band: `d == trunc(d)` within the target range), decimal→int/int64/float (reuse
   `decimal_to_int64_exact`; float only when shortest-round-trip exact per §4.5), any
   numeric→NUM_SIZED (range check — fixes the `int → int8` in-range rejection), int→float
   (always exact in the int53 band). Mirror `validator_numeric_item_embeds`' lattice so the
   validator and the boundary agree.
2. In `lambda_type_check` (`:1442`): after `lambda_type_matches` fails, if both sides are
   numeric, call the helper; on hit **return the converted item** (this is the
   re-representation — a check/emitter detail per TE-6, not a fourth relation).
3. Audit the 8 `emit_checked_boundary` sites + the checked writers to confirm every consumer
   uses `lambda_type_check`'s *returned* item (not the original register) before any unbox —
   the conversion is only real if the post-check value is the one that flows.
4. Tests (goldens pin codes/messages, not just empty stdout): `f(any 3.0) → 4`,
   `f(any 3.5) → E201-family error`, `let x: int = <any 3.0> → 3`, decimal-exact and
   sized-int range cases, and non-regressions `3.0 is int == false`,
   `match (3.0) { case int: … }` not taken, static `let x: int = 3.0` still E201.

Small, self-contained; no front-end change. **Phase A.**

---

## 3. Work item 2 — `any \ error` at fn/pn boundaries (restatement pts 1–5)

**Semantics** (TE-5 restatement, 2026-07-30): an unannotated `fn f(a, b)` is implicitly
contracted `(any \ error, any \ error) -> any \ error` and *enforced*: inference finding an
error-possible return is a reported type error (contain / disclose `| error` / impose `^`).
An error argument reaching an `any \ error` param never enters the function — the call's
result is that error. Explicit `any` is the opt-in; bare `var` is true `any`. `any \ error`
is ledger-level: it never drives representation on faith (R3), and there is **no surface
spelling** — it is the unwritten default.

**Current state:** no `any \ error` construct exists anywhere; unannotated params skip the
checked prologue entirely (`transpile-mir.cpp:14492` gate requires a declared contract), so a
dynamic error flows *into* the body as data; no openness/effect inference exists — nothing
computes "this body can produce error" (the §10.7 firewall is likewise unimplemented; a
`can_raise`-calling body is just typed ANY).

**Tasks, in dependency order:**

1. **Type substrate.** Internal singleton `TYPE_ANY_SANS_ERROR` (no grammar, no printing as a
   user spelling — diagnostics say "non-error value expected"). Arms in
   `static_boundary_relation` (`build_ast.cpp:1080`) and `lambda_type_matches`: like ANY but
   `runtime_type_accepts_error` → false. Distinguish *unannotated* (`declared_type == NULL` →
   implicit `any \ error`) from *explicit* `any` (declared, error-transparent) — the
   `declared_type` field already encodes exactly this.
2. **Param guard (runtime half of pt 5).** In the boxed public prologue
   (`transpile-mir.cpp:14492-14527`), unannotated params emit a cheap error-tag test (not a
   full `lambda_type_check`): tag == ERROR → return that item unchanged (original diagnosis
   preserved, same as the annotated path at `:14510`/`:14525`). Explicit-`any` params skip it.
   Consequence to add as lint later: `case error:` on such a param is provably dead.
3. **`| error` honesty in inference (prerequisite for pt 1's static half).** Call expressions
   already take the callee's declared return; extend so `T | error`-declared callees (user and
   §7.3-classified sys fns) type as the union. **Arithmetic contributes nothing** — per
   **C14c (2026-07-30)** no division form error-originates: `/` and now `div`/`%` all stay in
   number (`int div int → float`, IEEE `inf`/`nan` on a computed zero), so pure-math bodies
   are clean by construction and the firewall's error-originating class is exactly the set of
   `| error`/`^`-declared calls. (The former division-openness sweep is deleted; see Phase E
   for the C14c adaptation work.)
4. **Return contracts, unified with the firewall (§10.7 + pt 1 in one mechanism).** Every fn
   gets a return contract: the declared type, else implicit `any \ error`. Check the body's
   effective type against it with `static_boundary_relation`: REJECTED when the error
   constituent is present → E208-family diagnostic naming the first cause ("call to 'g' may
   return error") and the three-way menu. No fixpoint — assume contracts, check bodies
   (declared or implicit alike). ANY-typed bodies remain DEFERRED (runtime return check
   already exists); laundered dynamic errors stay R3 ledger-optimism — the static half only
   rejects what inference *proves* error-possible.
5. **`or`-narrowing** (P0 rule, prerequisite for 4 not to over-fire):
   `type(a or b) = (type(a) − {error, null}) | type(b)` at `build_ast.cpp:4829-4831` (today:
   flat ANY). Makes `int(s) or 0` statically `int`, so contained bodies prove clean.
6. **`var b` bare declaration** types true `any` (front-end detail; verify current behavior).
7. ~~Division-openness sweep~~ — **deleted by C14c** (division no longer error-originates).
   The corpus work moves to Phase E's retyping sweep instead (`div` results into int-typed
   contexts become float↛int static errors; `div … or` rescue patterns change meaning).

**Phase B** (order: 1→2 land independently; 5 → 3 → 4 as one arc; 7 gates 3's operator arm).

---

## 4. Remaining gap inventory (everything else in the doc not yet implemented)

| Gap | Design ref | Size | Phase |
|---|---|---|---|
| E228 acknowledgment extension (error-admitting binding/param/tail-return, `case error:` arm, `or` rescue) + third message suggestion — verified still firing on `let d: map \| error = input(…)` | TE-13 must-engage forms | S | C |
| Value-aware numeric admission | TE-5/TE-6 | S | **A** |
| `any \ error` substrate + param guard + return contracts + firewall | TE-5 restatement, §10.7 | L | **B** |
| `or`-narrowing static rule | TE-13/P0 | S | B (prereq) |
| Fault channel: stack-overflow + OOM + `==`-depth routed as unchecked faults, transparent unwind, pn `^err`/global catch; pre-reserved fault objects (OOM cannot allocate its diagnostic) | §10.7 impl note, C14 | M | D |
| `input(url, {schema: Q})` convenience | TE-10/P3 leftover | S | C |
| Bracket-form typed map write static check (`p["f"] = e` — dynamic-only today) | B-table | S | C |
| Test hardening: round-1 negative goldens assert empty stdout only — pin error codes/messages | gates | S | C |
| Diagnostic nits: union types print as "type" in E201 (`tsd_t6`); `lambda_array_set_checked_impl:7144` reports the element value where it validates the whole candidate | polish | S | C |
| `match case error:` dead-arm lint on default params; type-valued `or`-operand lint | TE-5, pitfall note | S | C/lint |
| Sys-func registry retrofit + `len` branch; TS-3/TS-5 perf; witness caching; two-entry specialization | deferred by decision | — | perf stage |
| `is (int[])` paren types; `!`-exclusion fix; schema-`any` uniformity | pattern-grammar/validator bucket | — | out of scope |

Explicitly not planned: any enforcement in `transpile.cpp` (C2MIR path is FROZEN — CLAUDE
rule 14).

---

## 5. Doc audit — inconsistent revisions and their disposition

Full-doc consistency audit run 2026-07-30 against the decided positions. **Four direct
contradictions found and fixed in the design doc the same day:**

| # | Was (revision text) | Fixed to |
|---|---|---|
| F1 | §10.12 still said a deferred `int` boundary rejects a float "regardless of magnitude" | value-aware per corrected TE-5/TE-6 |
| F2 | The P1 phase text still specified the "Go-like runtime match" | the TE-6 match (type-directional + exact-value admission) |
| F4 | TE-13/TE-9 still said an open *undeclared* fn is silently `T \| error` | every fn return is a firewall — declared or implicit `any \ error` (restatement) |
| F5 | TE-8's emission ladder minted a *new* type error when a clean target received an error | error-in/error-out first arm, matching the shipped `lambda_type_check:1447` |

Also fixed: the doc's status header over-claimed "IMPLEMENTED — correctness scope complete"
while TE-6's value arm, the §10.7 firewall, E228 forms, and `or`-narrowing are unimplemented —
now reads "ROUND 1 IMPLEMENTED" with this plan as the round-2 tracker.

**Needs user adjudication (deliberately not fixed):**

- **F3 — error payload.** The original user decision was **two-form error items**
  (`[tag][16-bit code]` inline, zero-alloc, for predefined system errors; object pointer when
  elaborate). The revision reversed it to "rich object always; code-only rejected" and erased
  the original from the doc entirely. Note the fault channel independently needs
  pre-reserved/static fault objects (OOM cannot allocate its own diagnostic), so a natural
  hybrid exists: rich objects for checked channels, pre-reserved minimal objects for faults —
  leaving open only whether hot-path checked failures may use inline codes (the original
  motivation).
- **F10 — TE-14 and §10.3 re-attributed.** Both were "DECIDED (user)" — the two-version model
  with once-per-function check consolidation, and open-fn unboxed variants as
  implementation-discretionary. The revision demoted them to "later implementation decision /
  DEFERRED". The consolidation is in fact *implemented* (the boxed public prologue).
  Recommendation: restore the decided-by-user labels with an implementation-status note.
- **F11 — §8.1 invented ruling:** dynamic calls beyond an eight-argument physical dispatch
  ceiling report an arity diagnostic for a *valid* signature — an implementation limit
  documented as language behavior, licensed by no design section. Accept-as-documented-limit
  or lift the ceiling.
- **F12 — §8.1 narrows validation diagnostics** to "the first validator path" where TE-9
  specifies path detail generally. Accept as v1 or track as diagnostic-completeness debt.

**Mechanical cleanups (batchable, low risk; fold into Phase C):** two stale "§0" references;
ten bare "§7.3"-style citations that collide with this doc's own section numbers (qualify
with the file name); "sub-questions 2–9" → 2–15; §10.7's self-referential "mostly dissolves"
note; the §5.5 missing horizontal rule; §8-vs-§8.1 tense reconciliation (delivered inventory
vs future-tense phase text); harmonize `^err`'s "b : T" with the "effectively `T?`
until `err` is checked" caveat (both true — static type vs flow discipline — say so once);
round-1 negative-test goldens assert empty stdout only — pin error codes/messages.

---

## 6. Phase summary and exit evidence

- **Phase A — value-aware numerics.** Exit: `temp/tsd_dyn2`-shaped golden gives
  `[2, 4, error]`; `3.0 is int` still false; baseline green.
- **Phase B — `any \ error` + firewall arc.** Exit: a fn whose body's result includes a
  `| error`-declared call is a compile error under a plain-`T` or implicit contract, with the
  first-cause diagnostic and menu; the disclosed variant runs. (Division examples moved to
  Phase E: `fn avg(a: int, b: int) int { a div b }` fails as a plain E208 return mismatch —
  body float, declared int — while `avg(...) float { a div b }` and `{ a / b }` pass clean,
  per C14c.) `f(<dynamic error>)` returns the original error without entering `f`;
  `let n: int = int("abc") or 0` types statically clean; baseline green.
- **Phase E — C14c number-domain adaptation + decimal inf/nan** (may run before or parallel to
  B; it is independent of the `any \ error` substrate):
  1. Retype `div`/`%` inference to `/`'s domain-selection table (`int div int → float`;
     large-int domains → decimal per §4.7); update `numeric` promotion tables and MIR emission
     (result lanes float; truncation semantics preserved).
  2. Runtime: scalar `div`/`%` (flex + machine tiers) return float with IEEE zero handling;
     delete the "integer division by zero" error sites; vectorized int division returns float
     arrays with per-lane `inf`/`nan` (whole-op single-error retired; pre-mask idiom optional).
  3. **Decimal inf/nan unblock** (user item, 2026-07-30): mpdecimal supports them natively;
     remove the wrapper filters (`lambda-decimal.cpp:306`, `:374`, `:420`) and the
     "decimal division: division by zero" error site so decimal-domain `/` (and large-int
     `div`) by zero yields Decimal-Infinity/NaN. **Printing**: decimal poison prints
     *distinctly* from float — tentatively `decimal.inf` / `decimal.nan` (user-proposed
     spelling; settle the literal/round-trip story — today `nan`/`inf` parse as float in
     Mark). Integrate with equality/order (decimal nan joins the nan poison band; §5.1/§6.2
     unchanged in shape) and with `is nan`.
  4. Corpus sweep: `div` results flowing into int-typed contexts (annotations, params,
     returns) become float↛int static errors — fix by `int(...)` conversion, retyping, or the
     explicit guard `if (b != 0) a div b else 0`; grep and adjust `div … or` rescue patterns
     (meaning change: inf/nan are truthy).
  5. Exit: `fz(1,0)`-family goldens show `div` returning `inf`/`nan`; `1n / 0n` →
     `decimal.inf`; vector `[6,7] div [3,0]` → `[2.0, inf]`; benchmarks green; baseline green.
- **Phase C — acknowledgment forms, input schema, diagnostics polish, test hardening.** Exit:
  `let d: map | error = input(…)` satisfies E228; negative goldens pin codes.
- **Phase D — fault channel.** Exit: deep-recursion and OOM scripts die with the fault
  diagnostic through a pn `^err`/global catch, not corruption; fault objects pre-reserved.
