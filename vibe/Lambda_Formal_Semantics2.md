# Lambda Formal Semantics 2 — Continuation

**Status:** open backlog — discussions to be held
**Predecessor:** [Lambda_Formal_Semantics.md](Lambda_Formal_Semantics.md) — concluded 2026-07-04 with the core principles and designs (decision records C1–C4, findings A1–A10, recommendations B1–B8, and the overall assessment). Finding/decision IDs below refer to that document. New decisions here continue the numbering at **C5**.

The core principles decided there, which future decisions should stay consistent
with:

1. **Emptiness ≠ nothingness** — `""` is a string (falsy); containers are things.
2. **Values vs. containers** (the 2×2) — falsy = `{null, false, error, ""}` only.
3. **Exact until you ask for float** — 53-bit flex `int` ⊂ float64 promoting on
   overflow; Go-aligned machine tier; literal strictness; smallest-exact-home data.
4. **Mutation is visible or it doesn't exist** — `let` final; bindings copy (COW,
   unobservable); `var` the sole mutability marker; `var` params the sole sharing;
   closures immutable (non-escaping nested-`pn` relaxation spec'd, deferred).

---

## Open backlog (carried from the predecessor's triage)

| ID    | Item                                                                                                                         | Notes                                                                                                                                                            |
| ----- | ---------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A6/B8 | OOB indexing + builtin error-value leaks                                                                                     | **RESOLVED** (§C5): total reads (absence → null), checked writes (raised error), builtin failures via declared `T^`; sub-rulings pending in §C5.3                |
| A4    | `symbol == string` raises vs docs' `false`                                                                                   | **RESOLVED** (§C8): `==` is total; cross-family → `false` (all cross-family pairs raise today, not just symbol/string)                                           |
| A5    | ArrayNum `==` representation-sensitivity; NaN reflexivity carve-out                                                          | **Largely RESOLVED** (§C8.5): full `==` model specified (poison carve-outs by design; exact numeric comparison; intensional fn equality; depth limit). Remaining: implement + fix the six-item equality bug list                                              |
| A8    | Covariance: `is` vs assignment disagree; log-only failure with silent continuation                                           | **RESOLVED** (§C12): covariance where values copy, invariance for `var` params (borrows); representation widening at COW-copy; failure mode fixed in impl plan 0.7 |
| A2    | Document `/` vs `div` failure modes; truncating div, C-style `%`                                                             | documentation only; machine-tier corners already ruled in C3                                                                                                     |
| A9    | REPL rollback replays session                                                                                                | tooling bug fix                                                                                                                                                  |
| A10   | Spec gaps: aspirational generics text, `as` semantics, open vs closed map matching, `match` arm ordering, nested `~` scoping | documentation / delete-or-implement decisions                                                                                                                    |
| B4    | `~` overloaded (pipe / that / match / self) — nested resolution unstated                                                     | document innermost-wins + escape idiom; longer-term named binders                                                                                                |
| B5    | union-vs-pipe disambiguation with first-class types                                                                          | **RESOLVED** (§C6): `\|` = union everywhere; `\|>` = pipe (dual-mode on free `~`); file output syntax pending (§C6a)                                             |
| B6    | `[int]` = 1-tuple footgun                                                                                                    | **RESOLVED** (§C7): keep composition semantics (TS precedent); enforce + positional annotation warning; mixed-pattern bug cluster to fix                          |
| B7    | Inference unobservability (gradual guarantee) as an *enforced* invariant                                                     | checkable via the formal model + boxed-vs-JIT differential testing                                                                                               |
| —     | C1–C4 implementation follow-ups                                                                                              | listed in the predecessor's §C1.7, §C2.3, §C3.3, §C4.4 — including the object-mutation bug cluster, `text` type, `math.max_int`, migration audits, docs rewrites |

---

## Decision records

### C5. Out-of-bounds access and builtin failures (2026-07-04) — RESOLVED: total reads, checked writes, declared failures

**Decision (designer-approved):**

1. **Reads are total; absence is null.** `arr[i]` out of bounds (including negative
   indices) → `null` — matching `map.missing` → null, `"hello"[99]` → null, and
   empty query results. With C2 truthiness, `arr[i] or default` is the coalescing
   idiom. No error values from reads, ever.
2. **Null propagation through chained access is part of the model** —
   `data.users[5].name` → null end to end (verified working: `m.missing.deeper` →
   null). Built-in optional chaining, now stated rather than folklore.
3. **Slices clamp** to bounds (strings already do: `"hello"[2 to 99]` → `"llo"`);
   arrays must match; an empty intersection yields an empty result.
4. **Writes are checked: OOB write is a raised error** — not null, not a silent
   no-op. Reads ask a question ("what's at index 5?" — "nothing"); writes issue a
   command, and a command that silently does nothing hides bugs. Growth stays
   explicit via `push`/`splice`. (JS's auto-extending sparse arrays are the
   cautionary tale.)
5. **The restored invariant: error values originate only from `raise` inside a
   declared `T^E` context.** Builtins that genuinely *fail* (rather than find
   absence) — `1 div 0`, failed conversions like `int("abc")`, I/O — get declared
   `T^` signatures and participate in compile-time enforcement, exactly as
   `input()` already does. No builtin ever emits a naked error value. This closes
   A6/B8 completely and restores `T^E` as the language's single error channel.

**Designer arguments for absence-as-null (recorded):**

- **Less panicking** — a missing field or short row should not kill a pipeline; it
  flows through as a visible null in the output.
- **Consistency with map semantics** — `a.b` on a missing key already returns null,
  not error; array indexing erroring was the anomaly.
- **Lambda is set-oriented, like SQL.** Null is the friendly value for set
  processing: it lets the set operation *continue* over the remaining rows/items,
  whereas a raised error aborts the whole set computation. A 10,000-record
  transform should produce 10,000 results with nulls where data was absent — not
  die at record 4,371. (SQL's NULL semantics are the fifty-year precedent for
  exactly this trade.)

#### C5.1 Probe evidence

| Probe                          | Result                                               | Verdict                                                                                          |
| ------------------------------ | ---------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| `m.missing`                    | `null`                                               | the model to generalize                                                                          |
| `m.missing.deeper`             | `null`                                               | chained null propagation already works                                                           |
| `null + 1`                     | `null`                                               | null absorbs through arithmetic                                                                  |
| `null ++ "x"`                  | `"x"`                                                | concat identity                                                                                  |
| `len(null)`                    | `0`                                                  | total                                                                                            |
| `arr[5]` (OOB read)            | **`error` value, flows unchecked**                   | the anomaly → becomes null                                                                       |
| `"hello"[99]`                  | `null`                                               | strings already correct                                                                          |
| `"hello"[2 to 99]`             | `"llo"`                                              | string slices clamp — the model for arrays                                                       |
| `[10,20,30][1 to 2]`           | **`10`** (!)                                         | array slicing broken outright — returns first element instead of `[20, 30]`; bug, fix regardless |
| `arr[10] = 99` (OOB write, pn) | log-only error, execution continues, array unchanged | silent-continue family (A8) → becomes a raised error                                             |
| `sum([1, null, 2])`            | **`error`**                                          | inconsistent with `+` propagating null — sub-ruling needed (C5.3)                                |

The expression algebra was already SQL-flavored (null-absorbing) end to end; array
indexing and aggregation were the two holdouts.

#### C5.2 Accepted tension (recorded to avoid relitigation)

OOB-read-as-null hides off-by-one bugs that an error would catch loudly — they
surface later as nulls in the output instead. This is the deliberate price of
less-panicking, set-oriented semantics: the same trade maps already made, mitigated
by nulls being *visible* in results and goldens rather than silently coerced into
other values.

#### C5.3 Sub-ruling: aggregation null policy (2026-07-04) — RESOLVED: strict propagation

**Decision (designer-approved):** a `null` in an aggregation input makes the
aggregate `null` — uniformly across `sum`/`avg`/`min`/`max`/`math.prod` and the
statistics family (`mean`/`median`/`variance`/`deviation`/`quantile`). Skipping is
always **explicit**, two ways:

- the general idiom `xs[!null]` — the existing exclusion-type child query, verified
  working today (`sum([1, null, 2][!null])` → `3`) — a *visible declaration* that
  missing data was deliberately dropped;
- a **`skip_null`-style option parameter on the denominator-sensitive statistical
  functions** (`avg`/`mean`/`variance`/`deviation`/`median`/`quantile`), R's
  `na.rm = TRUE` precedent, using Lambda's named-parameter calls — designer
  requested; exact parameter name to be fixed at implementation. `sum`/`min`/`max`/
  `prod` use the idiom (skipping doesn't change their meaning, only their input).

**The deciding argument (designer): "`1 + NULL` is `NULL`."** SQL's own scalar
algebra propagates null — yet `SUM` skips. SQL is internally inconsistent on this
point, and following SQL here would import its self-contradiction into a language
whose scalar `+` already propagates null (probed, C5.1).

##### The full argument stack for strict propagation

1. **Algebraic consistency.** `sum = reduce(+, v, 0)` stays *literally* true:
   Lambda's `+` is null-propagating, so the fold is too. One clean rule in the
   formal model, no aggregate special case. Under skip, `sum` and `+` obey
   different algebras.
2. **Statistical integrity.** Skipping silently changes the denominator:
   `avg([1, null, 2])` → 1.5 pretends the dataset had two points; for
   `variance`/`median`/`quantile` the statistic itself is silently altered. A null
   in an aggregate *should be news*; R and Julia force acknowledgment for exactly
   this reason.
3. **Precedent — the missing-data-careful languages all propagate.**
   R: `sum(c(1, NA, 2))` → `NA`, skip via explicit `na.rm = TRUE`.
   Julia: `sum([1, missing, 2])` → `missing`, skip via `skipmissing()` — a 2018
   design made after explicitly surveying SQL and R.
   NumPy: `nan` propagates; skipping is the separate `nansum` family.
   IEEE 754: aggregating over `nan` yields `nan` — which Lambda's floats already
   inherit, so propagation **unifies null and nan aggregation behavior** instead of
   giving Lambda two different missing-value regimes.
   (pandas' skip-by-default vs NumPy's propagation is a chronic, documented source
   of confusion between the two — a live demonstration of the cost of divergence.)
4. **Expressiveness asymmetry (verified).** Under propagate-default, skip is
   `sum(xs[!null])` — seven characters, existing feature. Under skip-default,
   recovering strictness is `if (null in xs) null else sum(xs)` — clunky, easy to
   forget, and silent when forgotten.
5. **The set-oriented argument (C5) is neutral here.** Propagation *continues* the
   set computation and returns a value; only the old `error` behavior aborts. Both
   candidate policies are "less panicking"; they differ only in whether missingness
   is silently absorbed or visibly reported.

##### The issues with SQL's skip semantics (recorded)

- **Scalar/aggregate contradiction**: `1 + NULL = NULL` but `SUM(col)` skips —
  the aggregate disobeys the operator it is nominally built from. A textbook
  criticism of the standard.
- **Silent denominator change**: `AVG` over data with NULLs divides by the non-NULL
  count — statistically load-bearing and invisible at the call site.
- **Broken monoid identity**: `SUM` over an empty (or all-NULL) set is `NULL`, not
  0 — so `SUM` is not even a proper fold with identity. (Lambda already does better:
  `sum([])` → 0, `math.prod([])` → 1, verified.)
- **`COUNT(*)` vs `COUNT(col)` asymmetry**: one counts rows, the other skips NULLs —
  a permanent source of subtle bugs and interview trivia.
- Collectively: SQL's aggregate-NULL rules are internally exceptional, must be
  memorized case by case, and are a chronic real-world bug source. SQL remains the
  strong reference for C5's *reads-are-null* decision; on aggregation it is the
  cautionary tale.

##### Adjacent corner pinned (probed)

Empty-collection aggregates: `sum([])` → 0 and `math.prod([])` → 1 (monoid
identities — correct, and better than SQL). But `avg([])` and `min([])` return
**naked error values**, which C5 rule 5 outlaws: identity-less aggregates over an
empty collection should yield **`null`** (absence of an answer, consistent with the
reads-total model), not an error value. Folded into the C5.4 builtin audit.

#### C5.3a Key membership / absent-vs-present-null (2026-07-04) — principle ruled; surface pending

**The issue**: with reads total (C5), `m.k` → null means either "key absent" or
"key present with value null", and there is no language-level way to ask which.
Probed findings: the *representation* already distinguishes — `{a:1, k:null}`
stores the null-valued field and `format` emits `"k": null` (JSON round-trip
fidelity works); `in` is **value membership on all containers** (`'a' in {a:1}`
→ false, `1 in {a:1}` → true, consistent with arrays); the runtime has the
capability (`Map::has_field`, `Element::has_attr`, O(1) via `field_index`) but
it is unreachable from Lambda.

**Designer ruling — why `in` must stay uniform value-membership (the structural
argument, recorded as a general principle):** **an element is both a map and a
list** — if `in` were value-membership for lists but key-membership for maps, it
would be ambiguous on elements. Therefore: *any operator over containers must
have one meaning across the map/list duality, because elements are both.* (This
principle governs future container-operator choices generally.)

**RESOLVED (designer ruling, 2026-07-04): `k at m` — `at` as the key-membership
operator.** The decision path, recorded:

- (a) `has(m, 'k')` function + `m.has(k)` method — **rejected**: method surfaces
  on open containers are shadow-vulnerable (the `.name`-footgun generalized: a
  wild-JSON `"has"` field would shadow the method). This ruled out functions
  and established that the surface must be a **keyword operator**
  (shadow-proof).
- (b) `m has k` operator — review's initial recommendation; **superseded by**:
- (c) **`k at m` (adopted)** — reuse the existing `at` keyword. Superior to
  `has` on the axes that matter:
  1. **No new reserved word** — `at` already exists (`for (k at m)`); zero
     identifier-sweep migration.
  2. **Iteration symmetry (the deciding argument)**: `for (x in coll)` ↔
     `x in coll`; `for (k at m)` ↔ `k at m` — the membership pair *is* the
     iteration pair. The key-vs-value axis has **one spelling everywhere**:
     `in` = values, `at` = keys. Self-teaching: whatever `for..in` walks, `in`
     tests; whatever `for..at` walks, `at` tests.
  3. **Uniform operand order**: member left, container right, for both
     operators (`x in coll`, `k at m`) — `has` reversed it.
  4. Everything from the `has` case carries over: shadow-proof; element-duality
     facet resolution (`in` = list facet/children, `at` = map facet/attributes:
     `x in elem` vs `'class' at elem`); **index possession** `i at arr` ⇔
     `0 ≤ i < len(arr)` (where the English reads best: "is there something at
     index i") giving the C5 checked-write idiom `if (i at arr) arr[i] = v`.
  5. Grammar precedent exists: `in` already serves as both for-iteration
     keyword and membership operator; `at` receives identical treatment, at
     the `is`/`in` precedence level.
- Noted trade-off: `has` read as better bare English (`config has 'timeout'`);
  the symmetry buys more than that costs.
- **Caveat to document**: Smalltalk/C++/Ruby use `at` as the *index accessor*
  (`container.at(key)` → value); readers may initially expect access rather
  than membership. Mitigated by the reversed operand order (`k at m` — no
  accessor spells it key-first) — but state it in the docs.
- Follow-ups: verify/define `for (k at elem)` iterates **attributes** (facet
  coherence); implement `at` membership over maps, elements, arrays (index),
  and decide ranges; document the `in`/`at` pair as the value/key axis.

#### C5.4 Follow-ups (C5)

1. Implement: array OOB/negative-index reads → null (replacing error values); OOB
   writes → raised error (replacing log-and-continue); array slice clamping;
   aggregation strict null propagation (§C5.3) + `skip_null` option on the
   statistical functions; `avg([])`/`min([])` → null (not error values).
2. Fix the broken array slice (`a[i to j]` returning `a[0]`) — independent bug.
3. Audit builtins for naked error-value emission; give genuine-failure builtins
   declared `T^` signatures (div, conversions, I/O) under rule 5.
4. Docs: state the absence model (reads total, null propagation, slice clamping,
   write errors) and the `or`-coalescing idiom.
5. Formal model: reads become total functions; rule 5's invariant — "error values
   only from declared `raise`" — becomes a checkable property of the rule set.

### C6. `|` union vs. pipe disambiguation (2026-07-04) — RESOLVED: `|` = union, `|>` = pipe

**Decision (designer ruling):**

1. **`|` means union/alternative — everywhere.** Type expressions, match
   or-patterns, string patterns, and *value expressions* (types are first-class:
   `let T = int | string`, `validate(x, int | string)`, and the semantics DSL's
   `syntax` declarations all parse uniformly). One symbol, one concept.
2. **`|>` becomes the pipe** — "as most languages use it" (F#, Elixir, Elm, OCaml,
   base R 4.1); `|:` was considered and rejected: "it will cause people to ponder
   what it is."
3. **File write/append gets new syntax — to be decided.** `|>` (write) and `|>>`
   (append) are freed. Candidates on the table from the discussion: `into`/`onto`
   keywords (matching Lambda's keyword-operator tradition: `and or not is in to
   that where`); a `|>>`-family variant was disfavored (pipe `|>` next to
   file-append `|>>` is a confusion factory). → tracked as C6a.
4. **The pipe's dual-mode semantics (designer clarification, part of the ruling):**
   `|>` dispatches *syntactically* on whether the right-hand side contains `~`:
   - **no `~` in the body** → **whole-value application**: `data |> sum` ≡
     `sum(data)`;
   - **`~` present** → **mapping pipe**: `data |> ~ * 2` maps per item, binding
     `~` to each.
   Precision for the formal grammar: the test is a **free** `~` — a `~` bound by a
   nested `~`-binding construct (an inner pipe or `that`-clause) does not make the
   outer pipe a mapping pipe. The distinction is decided at parse time, so the
   formal model gets two desugarings selected statically, no runtime dispatch.
   (Edge to pin in the spec: a `~`-free body that does not evaluate to a callable —
   `xs |> 42` — should be a compile/type error, not "replace each item with 42".)

#### C6.1 Why the collision resolves this way (recorded)

The collision is **one-directional**: union needs expression position (first-class
types), but pipe never needs type position. Keeping `|` overloaded would require
dispatching the meaning of `a | b` on operand *types* — semantic overloading, poison
for local parsing, type checking, and the formal grammar. Splitting is mandatory;
the only question was which concept keeps the glyph, and "the one that appears in
both worlds" wins it.

**Usage data (fixtures):** general pipe ≈ 140 sites across 38 files (migration
required under *any* respelling — mechanical); file-output `|>` = **8 sites in one
file** (eviction is nearly free). The niche operator was squatting on the world's
standard pipe symbol.

#### C6.2 Candidates considered and killed (recorded)

- **`~>`** — visually charming (the operator that binds `~` containing `~`) but
  **fatally whitespace-ambiguous**: pipe bodies and `that`-clauses are full of
  `~ > 3` comparisons; `xs ~> 3` vs `xs |> ~ > 3` would differ by one space with
  silently different meanings (the lexer greedily takes `~>`). Corollary for the
  DSL proposal: the rewrite arrow should be **`-->`** (the classic PL reduction
  arrow), not the `~>` penciled into the proposal's open question 1 — rule
  templates contain expressions, so the same hazard applies there. *(Review
  recommendation; to be reflected in the DSL proposal.)*
- **`|:`** — grammatically safe (no legal `|`-adjacent-`:` context today; typos in
  both directions produce errors, not silent changes) and the colon has a "binds"
  story in Lambda; rejected for novelty — zero precedent anywhere, and one
  character away from union, a permanent scanning tax on exactly the distinction
  being clarified.
- **`->`** — probably free, reads fine, but means function-arrow in half the
  world's languages and sits confusably next to Lambda's own `=>`.

#### C6.3 Migration notes

- ~140 pipe sites in fixtures plus docs/stdlib: mechanical rewrite `|` → `|>` in
  pipe position.
- **Transition guard**: old file-output code `data |> "out.txt"` does not become a
  syntax error under the new regime — it becomes a pipe mapping to a constant
  string, *silently*. A pipe whose `~`-free body is a bare string/path literal is
  almost certainly this mistake: make it a compile error ("file output has moved")
  during transition.
- Typo safety of the new regime: writing `|` where `|>` was meant yields a union of
  non-types → compile error, caught, never a silent wrong answer.

#### C6a. Pending: file write/append syntax

Requirements: write + append forms; procedural-only; must not visually collide with
`|>`. Candidates: `into` / `onto` keywords; others welcome. 8 call sites to migrate.

### C7. `[int]` and array-pattern composition (2026-07-04) — RESOLVED: keep composition semantics; positional warning

**Decision (designer ruling):** `[int]` keeps its meaning — an array pattern of
exactly one `int` — because Lambda's bracket types are **structural patterns where
values and types compose freely per position**:

```lambda
[1, int, "str"]   // array of exactly 3 items: 1st the value 1, 2nd any int, 3rd the value "str"
[1]               // array of exactly one item: the value 1
[int]             // array of exactly one item: any int
```

`[1]` and `[int]` may surprise newcomers, but they are forced by compositionality —
and that compositionality ("type patterns compose just like values compose") is a
deliberate strength: Lambda's type patterns express what most languages' type
systems cannot. **Banning `[1]`/`[int]` would be too strong** (it would break
composition exactly at n = 1); a **parser warning is definitely needed**.

#### C7.1 Discussion record

- **The original finding (B6)** claimed `[int]`-as-one-tuple collides with "the
  near-universal reading" (`[Int]` = list in Haskell/Swift, `list[int]` in Python).
  **Corrected during discussion**: the world is *split* — **TypeScript reads
  `[number]` as a 1-tuple** (`number[]` is the array), and TS even allows mixed
  literal/type tuple members like `[1, number, "str"]` — precisely Lambda's
  composition design. TypeScript, the structurally-typed scripting-adjacent
  neighbor, is the precedent that matters; the composition semantics has the
  *better* precedent, not the worse one.
- **Options killed**: *re-meaning* `[T]` ≡ `T[]` breaks composition at n = 1
  (`[1, int]` a 2-pattern but `[int]` an array-of?) and leaves `[1]` incoherent;
  *banning* bare `[T]`/`[v]` punishes the feature's core use. Exactly-one already
  has an alternative spelling (`int[1]`), but that is an argument for a good hint,
  not for surgery on the pattern algebra.
- **DSL connection (load-bearing)**: value/type positional mixing in patterns is
  the foundation the semantics DSL's destructuring patterns build on
  (proposal §4.1, "one pattern language") — `[1, int, "str"]` is a pattern the way
  `<add (a: Expr) (b: Expr)>` is a pattern. Protecting composition here protects
  Stage 0.

#### C7.2 Probe evidence — the design is currently aspirational (bug cluster)

| Probe | Result | Verdict |
|---|---|---|
| `type Triple = [1, int, "str"]`; `[1, 5, "str"] is Triple` | **false**, with runtime error `pattern matching requires string or symbol, got type: array[num]` | mixed value/type array patterns don't work at all |
| `[1, 2] is [int]` (via named type) | **true** (!) | exactly-one arity not enforced — `[int]` currently means nothing |
| `let xs: [int] = [1, 2, 3]` | compiles and runs silently | annotation unenforced |
| `[5] is [int]` (inline) | runtime error + REPL rollback | `is [T]` fails to parse inline — likely collision with child-query `expr[T]` grammar |

So both the footgun *and* the strength are unimplemented: the composition design
must be built, not merely defended. (A10 aspirational-docs family.)

#### C7.3 The three-layer mitigation (review refinement, adopted)

1. **Enforce the semantics first** — once `[int]` really means exactly-one,
   `let xs: [int] = [1, 2, 3]` becomes a natural compile **error**, and that error
   message carries the teaching: *"`[int]` matches an array of exactly one int —
   did you mean `int[]`?"* Fires only on actual mismatch.
2. **The parser warning, made positional**: a suppressible lint hint for a *bare
   single-type* `[T]` specifically in **annotation position** (params, `let`/`var`,
   return types), where list-of intent is overwhelmingly likely. **No warning in
   pattern positions** (match arms, `type`/schema declarations, mixed-member
   patterns) — that is the intended power, and warning there would punish the
   feature's core use.
3. **Fix the `is [T]` inline parse crash** (child-query grammar collision) —
   prerequisite for the pattern family being usable inline at all.

#### C7.4 Follow-ups

1. Implement the array-pattern composition semantics (mixed value/type members,
   arity enforcement, annotation enforcement) + the three-layer mitigation.
2. Docs: invert the presentation — a section titled "types compose like values,"
   *leading* with `[1, int, "str"]`, instead of today's lone footnote warning that
   `[int]` is a tuple. Present the strength; the corner case becomes a consequence.
3. Bug catalog: mixed array patterns erroring, `[int]` arity unenforced, annotation
   unenforced, `is [T]` parse crash + REPL rollback.

### C8. Equality across type families (2026-07-04) — RESOLVED: `==` is total; cross-family → `false`

**Decision (designer ruling):** `symbol == string` returns **`false`** — as the
docs always specified (`'name' == "name"` → false, `Lambda_Cheatsheet.md`); the
implementation raising `== not found` is the bug.

**Generalization (review, consistent with the ruling and adopted with it):** `==`
is a **total** operation over all value pairs. Comparison across *value families*
returns `false`, never an error. Within a family, the existing cross-representation
equality stays: `1 == 1.0` (numeric promotion), `(1 to 3) == [1, 2, 3]`
(cross-type sequences), `[1] == [1.0]`. `!=` is the exact negation:
`'a' != "a"` → true.

#### C8.1 Probe evidence — the partiality is implementation-wide

A4 recorded only `'a' == "a"` raising. Scope probing shows **every** cross-family
comparison raises today, each triggering the A9 REPL rollback:

`1 == "1"` · `true == 1` · `'a' == 1` · `[1] == "x"` · `{a:1} == [1]` — all
"Error during execution."

(`null == 0` and `null == false` correctly return false already — null-vs-anything
was implemented; everything else was not.)

#### C8.2 Arguments (recorded)

- **The docs already ruled**: symbol ≠ string is a documented design point (symbols
  are identifiers, strings are text; `'json' != "json"`); the equality operator is
  described as total structural deep equality. Implementation diverged.
- **C5 rule 5**: error values originate only from `raise` in declared `T^E`
  contexts — a raising `==` violates the language's own error discipline.
- **Set-oriented (the C5 argument again)**: filters over heterogeneous data —
  `data that (~.id == 'x')` where some ids are strings, some numbers — must
  *not-match* and continue, not abort the pipeline at the first mixed-type row.
- **Match totality**: `case 'a':` against a string scrutinee simply doesn't match;
  no arm can crash a `match`.

#### C8.3 Toward the full `==` definition (feeds A5)

This ruling supplies A5's cornerstone. The remaining work is enumerating the value
families and carve-outs in one place: numbers (cross-representation: int, sized,
float, decimal), sequences (array/list/range), strings, symbols, bools, datetimes,
binaries, maps/elements (structural), null. Cross-family → false. Known carve-outs
to document: `nan != nan` (IEEE, non-reflexivity), the ArrayNum
representation-sensitivity **bug** (task_38782787 — must be fixed, not documented),
and error-value equality (undecided — part of A5).

#### C8.4 Follow-ups

1. Fix the `== not found` raise path → total comparison (all family pairs).
2. This is also the canonical reproducer for the A9 REPL rollback bug.
3. A5 remains open: write the single formal definition of `==` (families,
   promotions, carve-outs) + fix ArrayNum `==`.

#### C8.5 The equality model, completed (2026-07-04, designer rulings)

**Governing principle (designer): `==` is deep value equality under Lambda —
anywhere not following this is a bug** — with exactly two *designed* carve-outs
(poison values, below). Point-by-point rulings from the review of the principle's
consequences:

1. **Element equality returning false is an implementation bug.**
   `<div class:"x"> == <div class:"x">` → false today (pointer-comparison
   fallback). Priority-one fix: elements are the document model's core type and
   `==` is the verification harness's primitive.
2. **Range ≡ array, semantically — verified correct today.** Probes: symmetric
   both directions, `(1 to 3) == [1.0, 2.0, 3.0]` (promotion inside sequences),
   `[[1,2]] == [1 to 2]` (nested), `(2 to 1) == []` (empty). The one corner of
   deep equality that is fully right in the implementation.
3. **`f == f` → false is a bug.** Function equality is *intensional*: **same
   definition site + deep-equal captured values** ⇒ equal. This is decidable,
   cheap, gives reflexivity and alias-equality (`let g = f; f == g` → true).
   Source-text comparison was considered (slightly more generous: textually
   identical definitions with equal captures are extensionally the same function)
   but has alpha-renaming problems (`(x) => x+1` vs `(y) => y+1`) and string
   costs; definition-site identity is the recommendation. Documented property:
   function `==` is conservative — `true` guarantees same behavior; `false` does
   not prove different behavior (extensional equality is undecidable).
4. **Type equality follows value equality** once values are ironed out — with a
   normalization caveat to pin down in A5: `==` on types is *representational*
   (compare normalized forms, so `int|string == string|int` should hold via
   canonical ordering), not *semantic equivalence* (undecidable once `that`
   constraints are involved).
5. **Error values never compare equal — by design.** `err == anything` → false,
   including `error("x") == error("x")`. Purpose: **no silent error
   fall-through** — in `if (a == b) {...}`, an error value must never satisfy the
   guard and enter the branch. The explicit error check is the dedicated syntax:
   `if (^err) {...}`. (Current impl logs `unknown comparing type: error` and
   returns false — right answer, wrong mechanism; make it a clean rule, no log.)
6. **NaN follows IEEE: `nan == nan` → false — and error is *modeled after NaN*
   in its `==` semantics.** Both are poison values whose equality is always
   false, so equality guards never admit them. The review's SameValueZero
   recommendation (reflexivity restoration) is **overruled** by the
   fall-through-prevention argument: `==` is deliberately *not* an equivalence
   relation — it is deep value equality with two non-reflexive poison carve-outs
   (`nan`, `error`), and this asymmetry is the design, not an accident.
   *Consequence to pin down later*: dedup/grouping (`unique`, `set`, map keys)
   over poison values — note SQL splits here (`NULL <> NULL` in `=`, but
   `DISTINCT`/`GROUP BY` treat NULLs as one group); Lambda will need the same
   split decided for `nan`/`error`.
7. **Float equality is hard and stays hard (designer: no good solution).**
   `0.3n != 0.3` and `0.1 + 0.2 != 0.3` are inherent. Consequences adopted:
   cross-representation numeric equality must be **exact mathematical
   comparison** — so today's `0.1n == 0.1` → true (probed) is a lossy-conversion
   **bug**; the honest answer is false, since float `0.1` is exactly
   0.1000000000000000055511…, not the decimal value 0.1 (`1.5n == 1.5` stays
   true — 1.5 is exact in binary). Remedy for the ergonomic gap: add
   `math.isclose(a, b, eps)` (NumPy/Python precedent) and document "don't `==`
   floats" as the idiom.
8. **Cycles and depth.** Self-referencing values are unconstructible under
   Lambda today (and C4 value semantics keeps it that way natively — assigning a
   structure into its own field snapshots it, so deep `==` terminates by
   construction). But **interop can import cycles** — JS objects are freely
   cyclic — so deep equality needs a **practical depth limit**: recommend
   exceeding it *raises* a declared error (a silent false would be a wrong
   answer; a hang is worse), with a generous default. Documented caution for
   `==` over deeply-wired interop data.

**The complete `==` specification in one paragraph:** total over all value pairs;
cross-family → false; within-family → deep value equality with exact
cross-representation numeric comparison and sequence-family unification
(array/list/range); two designed poison carve-outs (`nan`, `error`) that never
equal anything; function equality intensional (definition site + captures); type
equality representational; depth-limited with a raised error at the limit.

**Equality bug list (consolidated):** element `==` (priority one) · function
self-equality · ArrayNum representation-sensitivity (task_38782787) · cross-family
raises (C8) · decimal↔float lossy comparison · error-comparison via logged
`unknown comparing type` instead of clean rule · map `==` order-independence
(§C8.6 — semantic change).

#### C8.6 Dedup/grouping and map order (2026-07-04, designer rulings)

**1. Dedup and grouping are defined by `==`, with no special cases.**
`null == null` → true (null is a value; it equals itself), while `nan`/`error`
never equal anything (C8.5). Extended to `unique`, `set`, and map keys, this
gives exactly the designer's intent: **nulls group together; each `nan`/`error`
stands by its own** — and it needs *zero* special-casing, because it falls out of
defining dedup purely via `==`.

Verified already implemented: `unique([null, 1, null])` → `[null, 1]`;
`unique([nan, nan, 1])` → `[nan, nan, 1]`.

The contrast with SQL is worth recording: SQL needed a DISTINCT exception
(`NULL = NULL` is UNKNOWN, yet DISTINCT/GROUP BY group NULLs) — an internal
contradiction between its equality and its grouping. Lambda avoids the split
entirely because `null == null` is simply true; grouping and equality agree by
construction. Divergence note for the docs: JS `Set` (SameValueZero) and Python
`set` (identity shortcut) keep a *single* NaN; Lambda keeps every nan — the
consistent-with-`==` choice, with the poison rationale ("two unknowns are not
known duplicates"). Adjacent items flagged, not yet ruled: `sort`/`order by`
placement of poison values (SQL's NULLS FIRST/LAST precedent); whether `nan`/
`error` can even be map keys (Lambda keys are symbols/strings — likely moot;
confirm for dynamic `map()` construction).

**2. Maps are ordered under Lambda; `==` is order-sensitive:
`{a:1, b:2} != {b:2, a:1}`.** This must be clearly documented — and it is a
**semantic change**: the implementation today returns `true` (probed), and
`Lambda_Cheatsheet.md` + `Lambda_Data.md` explicitly document order-*independent*
map equality. Both change. Rationale: maps are insertion-ordered data in a
document-processing language — member order is part of the value (JSON round-trip
fidelity, attribute order preservation), so deep *value* equality must see it.

Points to document alongside the change:

- **Type matching stays order-insensitive.** `{b:2, a:1} is {a: int}` continues
  to hold — structural `is`/pattern matching looks keys up by name; only *value
  equality* (`==`) is order-sensitive. Two different questions, two different
  order sensitivities — state this pair explicitly or it will be reported as a
  bug from both directions.
- **Precedent context**: Python dicts are insertion-ordered but compare
  order-*independently*; JS objects are ordered with no deep `==` at all. Lambda
  is stricter than both — the document-fidelity rationale is the justification
  and should accompany the doc.
- **Flagged for ruling — element attributes.** Elements extend maps; children are
  obviously ordered, but XML InfoSet and the HTML DOM treat *attribute* order as
  insignificant. Does element `==` compare attributes order-sensitively (uniform
  with the map ruling) or order-insensitively (XML precedent)? Needs an explicit
  ruling when element `==` is fixed (it is currently broken outright, C8.5-1).

Follow-ups: implement order-sensitive map `==`; fix the two doc sites claiming
order-independence; migration audit for code/fixtures relying on
order-independent map equality.
*(All three cancelled by the C8.6-R revision below.)*

#### C8.6-R REVISED (designer refinement, 2026-07-04): the two-level model — ordered storage, unordered equality

The order-sensitive `==` ruling above is **reverted** and replaced by a two-level
resolution that separates representation from identity:

1. **Data model: map keys are ordered** — parsed and stored in source order.
   Reasons (designer): (a) **source round-trip fidelity**; (b) **`for (k at map)`
   has determined execution order**; (c) some specs genuinely require key order
   (e.g. **CSS rules**).
2. **Equality: keys compare unordered — for both maps and elements.** Con
   acknowledged: slower comparison. Pro: **consistent with JSON and XML, the
   major document standards** (both define object members / attributes as
   unordered).

Consequences, recorded:

- **The element-attribute fork (flagged above) is resolved automatically**:
  attributes are maps → unordered `==`, matching XML InfoSet; children remain
  ordered (list facet). The review's uniform-order-sensitive + `canon()`
  recommendation is **overruled**; the two-level model gives the domain-standard
  behavior by default rather than behind an explicit call.
- **This is exactly Python's dict model** (insertion-ordered storage and
  iteration since 3.7; order-independent `==`) — proven at scale.
- **Zero implementation change for `==`**: current behavior is already
  order-independent (probed true), and the code comment and docs were correct
  all along. The three follow-ups above (semantic change, doc fixes, migration
  audit) are cancelled; C8.7 note 1's "delete the search loop" plan is obsolete.
- **Performance mitigation**: `TypeMap.field_index` (existing per-shape hash
  table) can make the different-shape comparison path O(n) expected instead of
  the current O(n²) linear scan — the acknowledged con is an optimization task,
  not a semantic tax.
- **Hashing obligation**: anywhere maps are hashed (the C9 normalized-AST
  content hash; any future map-as-set-key) must hash in **canonical (sorted)
  key order** — equal values must hash equal, and unordered `==` makes source
  order non-canonical for hashing.
- **Accepted consequence**: `a == b` does *not* imply `format(a) == format(b)`
  — equal maps may serialize with different key orders (Python's exact
  situation). Note for the verification harness: golden comparison is textual,
  so models must reproduce source *order* to match goldens even though `==`
  would not care — which level 1 (ordered storage) is precisely what
  guarantees.

#### C8.7 Implementation notes (code-level, 2026-07-04)

From reading the equality implementation (`lambda/lambda-eval.cpp`, dispatcher
`fn_eq_depth` at ~1097, entry `fn_eq` at ~1289):

1. **`map_eq` (line ~962) — how map equality is coded today.** Two paths:
   *(a)* same-shape fast path — both maps share one `TypeMap` (shape pool), walk
   fields by slot, O(n); *(b)* different shapes — length precheck, then for each
   key of A a **linear name+namespace search through B's shape chain**
   (lines ~987–996), O(n²). Verification note: behavior today is
   **order-independent** (probe: `{a:1,b:2} == {b:2,a:1}` → true), delivered by
   path (b)'s name search; code, comment, and docs agree with each other — and
   all three change under the C8.6 ruling. **Implementing the ruling = deleting
   the search loop**: ordered comparison is a single parallel walk of both shape
   chains (positional key-name/ns compare + value compare), O(n). The new
   semantics is strictly cheaper — the O(n²) path existed *only* to provide
   order-independence, which matters for large maps common in markup documents
   (the designer's rationale for the ruling).
2. **Element `==` root cause (the priority-one bug).** `element_eq`
   (line ~1011) compares tag/ns/children correctly, but compares attributes via
   `map_eq((Map*)a, (Map*)b, …)` (line ~1024) — and the cast is wrong:
   `Map : Container` puts `type`/`data` right after Container, while
   `Element : List` puts them **after** List's `items/length/extra/capacity`.
   So `((Map*)element)->type` reads `Element.items` (the children pointer)
   reinterpreted as a `TypeMap*` → garbage shape walk → always false. Fix: pass
   the element's own `type`/`data` (or refactor `map_eq` to take `type + data`
   directly). Once fixed, attribute order-sensitivity automatically follows
   whatever `map_eq` does — the C8.6 attribute-order question is a real fork at
   this exact call.
3. **`error == error` is an accident, not a rule**: no `LMD_TYPE_ERROR` branch
   exists in `fn_eq_depth`; it falls through to
   `log_error("unknown comparing type") + BOOL_ERROR` (line ~1264). C8.5's
   ruling needs an explicit clean branch → `BOOL_FALSE`, no log.
4. **The C8 cross-family fix has two layers**: the REPL's `== not found` is the
   **MIR transpiler** rejecting statically-known type mismatches at
   operator-resolution time; dynamically, `fn_eq_depth` returns `BOOL_ERROR`
   for mismatched container types (line ~1217). Both must yield `false`.
5. **The depth limit already exists**: `EQ_MAX_DEPTH = 256` (line ~917), log +
   `BOOL_ERROR`. Under C8.5 it should become a properly *raised* error; the
   value deserves review (deep JSON exceeds 256 more easily than HTML).
6. **`array_num_eq` (line ~938) looks correct**: cross-element-type comparison
   promotes via doubles; the float path deliberately compares element-wise "to
   respect NaN != NaN" — the poison semantics already implemented here. The
   task_38782787 representation bug is likely narrower than feared.
7. **Side finds for the sweep list**: `fn_lt_scalar` compares strings with
   `strcmp` — NUL-unsafe and ignores the length prefix (inconsistent with
   `fn_eq`'s `memcmp(len)`); and `<` accepts symbol operands while `==`
   symbol-vs-string errors — cross-operator inconsistency to clean with C8.
8. **Function site identity (designer ruling: site equality, not source
   equality).** Site = **static AST node identity** — stamp
   `(module id, AST node index)` into the closure at creation. **Memory address
   explicitly rejected**: it breaks C4 value semantics (COW-copying a closure
   would change its address → `let g = f; f == g` false again) and is unstable
   across JIT tiers/recompilation (MIR-Direct vs C2MIR give the same source
   different code addresses). Key fact: Lambda has no `eval` and no runtime code
   synthesis, so *every* function value — including "dynamically constructed"
   closures — is created by evaluating a static `fn` node and therefore always
   has a site; dynamic construction varies only the *captures*, which is what
   the site + deep-equal-captures rule compares (`make_adder(10) ==
   make_adder(10)` → true; `== make_adder(20)` → false). Builtins use their
   builtin index as the site. *(Extended for true dynamic construction by §C9.)*

### C9. Dynamic function construction (2026-07-04) — direction set; quote syntax open

Lambda currently cannot construct functions at runtime — a deliberate stance
("closed over metaprogramming") that C8.7's site-equality argument leaned on.
**Designer decision: this is a feature gap; dynamic construction will be added.**
The reopening is deliberate and guard-railed; recorded rulings:

1. **`eval("str")` is 100% ruled out.** Sharpened into the governing principle:
   **strings are never code.** Code enters the system as source *files*
   (`input(f, 'lambda')` → AST data) or as constructed AST values — no API
   accepts a runtime string for execution or compilation (`compile()` takes AST
   elements only; the review's earlier string-convenience sketch is deleted by
   this ruling). This kills the injection class wholesale and preserves
   analyzability — a stronger line than "no eval".
2. **`input(f, 'lambda')` as AST loading — likely adopted** (also proposed
   independently as DSL prerequisite, proposal §4.4): parse a Lambda source file
   to its AST as an element tree.
3. **Code-as-data + eval — the Lisp lineage — is definitely wanted.** The open
   question is **syntax**: raw element construction (`<fn ...>`, `<expr ...>`) vs
   a quote/macro-like form. Review analysis recorded for the decision:
   - **Key insight: Lambda element literals are already quasiquotation with
     inverted defaults.** Element structure (tags, attr names) is literal while
     embedded expressions evaluate — so `<add <lit n> <var 'x>>` is a working
     code template *today*: splicing is native; the "quoted" parts are the tags.
     Lambda is homoiconic already; only the convenient authoring form is missing.
   - **Recommended architecture (Elixir/Julia precedent — both converged here):**
     Layer 1: the **element AST is the canonical representation**, one schema
     derived from the grammar's node names, with three producers —
     `input(f,'lambda')`, hand-built elements, and the quote form. **Coherence
     rule (more important than any syntax choice): all three produce the same
     AST schema** — one currency for loaded, constructed, and quoted code. Since
     the schema is a type declaration, the **validator checks constructed ASTs
     before `compile()`** — homoiconicity with schema checking, which Lisp never
     had. Layer 2: **`quote { ... }` as authoring sugar** — real Lambda syntax,
     parser-checked at authoring time, yielding the element AST; splice marker
     candidate `$` (appears free in the grammar; `~` is taken): `$x`, `$(expr)`,
     `$*xs` for list-splice. Inside `quote`, defaults invert to Lisp-normal
     (literal except `$`-holes); outside, element literals keep
     evaluate-by-default. Two forms, two defaults, one output format.
   - **Hygiene: deferred, not ignored.** Quote templates introducing binders can
     capture variables (the classic Lisp problem). Contained in v1 by closed
     compilation (below) — no ambient capture — with the DSL's planned `fresh()`
     as the manual remedy; full hygiene revisited only if compile-*time* macros
     are ever considered (a much bigger door).
4. **Equality of dynamically constructed functions: normalized-AST hash /
   comparison — adopted** (the Unison content-address approach), extending
   C8.7-8. Sub-decisions recorded: normalization strips source positions and
   should **alpha-normalize** (de Bruijn-index bound variables) so
   `(x) => x + 1 == (y) => y + 1` — alpha-equivalence is the mathematically
   right function-value equality and cheap to compute; hash is the fast path
   with full normalized-tree comparison on hash equality (collisions cannot
   lie). Captured/env values compare by deep `==` as in C8.5.

Additional design rules from the discussion (review, pending ratification):

- **Closed compilation with explicit environment** (the one good lesson of JS's
  `new Function`): the constructed function sees stdlib + explicitly passed
  bindings only, never ambient scope — preserving C4 (no hidden captures) and
  the security story.
- **`compile(ast, env?) fn^`** — parse/validate/type failures are ordinary
  `T^E` errors (C5 rule 5); compilation itself is deterministic and pure (can
  live in `fn`); the produced function carries its own `fn`/`pn` nature from
  its body.
- Implementation reality: the runtime is already a JIT (parse → AST → MIR at
  startup); this exposes existing machinery. LambdaJS will need runtime JS
  compilation for web compat (`eval`/`new Function` in guest JS) regardless —
  the Lambda-level design should land first rather than be inherited by
  accident.
- Survey context (recorded): Haskell = no runtime eval, interpret-data instead;
  Lisp/Clojure/Elixir/Julia = homoiconic quote/eval (the adopted lineage);
  MetaOCaml/F#/C# expression trees = typed staged codegen (**explicitly
  deferred** — `code<T>` types are research-grade commitment); JS/Python string
  eval = the rejected tier; Unison content-addressed functions = the adopted
  identity model.

**C9a ruled (2026-07-04): Layer 1 adopted as the starting point — hand-built
element ASTs (`<fn ...>`, `<add ...>`); Layer 2 (`quote { ... }`) kept-in-view,
deferred to future.** The designer confirmed the inverted-quasiquote semantics
with two test readings, both correct by design:

- `<add ; if (a) e1 else e2>` — the nested `if` is an *expression child* →
  **evaluates** at construction time (splice);
- `<add <if ...>>` — the nested *element literal* is structure → **unevaluated**
  (quoted data).

Rule of thumb: **expression child = splice; element-literal child = quoted. The
angle bracket is the quote mark.**

**Probe results — what works today vs. the Layer-1 grammar worklist:**

Working: pure element trees `<add <ref 'x'> <lit 1>>` (the DSL term syntax);
single expression child `<add ; n>`, `<add ; (n + 1)>`; host-`if` *selecting
between* quoted fragments at statement level
(`let sel = if (a) <ref 'x'> else <lit 0>`); keyword tags `<if>`, `<fn>`,
`<for>` parse fine.

Broken/missing (grammar work required for Layer 1 to be usable):

1. **Expression children only parse when *sole* child** — `<add ; sel <lit 9>>`
   (computed child next to quoted child — the bread-and-butter template form)
   and `<add ; 1 2>` both fail. The fix — general expression children in
   whitespace-separated sequences — is the single change that delivers the
   evaluate-semantics everywhere.
2. **Bare `if` in content position** parses as if-*statement* ("Missing { }").
   Recommendation: require parens for control-flow expression children
   (`(if (a) e1 else e2)`) rather than resolving the ambiguity in content.
3. **`<var ...>` breaks the parser** (keyword-tag support is partial: if/fn/for
   OK, `var` not) — see the C9a revision below: bare keyword tags are abandoned.
4. Children are whitespace-separated; commas are rejected — fine, but document
   it.

**C9a revision (designer, 2026-07-04): namespaced AST tags; quote form promoted.**
Bare keywords as tags are **not a good idea — too much syntax-ambiguity risk**
(confirmed by the partial keyword-tag support probed above: accidental, not
designed). Resolution:

- **The canonical AST schema uses a namespace: `<lm.if ...>`, `<lm.var ...>`,
  `<lm.add ...>`.** Verified working today with `import lm: '<uri>'` — including
  `lm.var` (whose bare form breaks) and nested trees
  (`<lm.add <lm.var name:'x'> <lm.lit val:1>>`). Namespacing rides existing
  machinery (the `svg.`-style namespace imports), dodges keyword lexing
  entirely, and also solves the tag-vs-object-type-name collision flagged
  earlier. **Precedent: XSLT's `xsl:` prefix** — the most-deployed
  code-as-markup language solved instruction/literal separation exactly this
  way. Recommendation: predeclare `lm` as an ambient builtin namespace (like
  `math`/`io`) so AST construction needs no import boilerplate.
- **The namespaced syntax is "not nice, but more robust" (designer)** — accepted
  because the canonical form is primarily a *machine* format (what
  `input(f,'lambda')` emits, what `compile()` consumes, what models
  pattern-match); verbosity there is cheap. **For nice human authoring, the
  `quote { ... }` construct is promoted from KIV to the planned authoring
  form.** This lands on the exact Elixir/Julia division of labor: nobody
  hand-writes Elixir AST tuples either — humans write `quote`, machines
  exchange the data structure.
- Worklist reprioritized accordingly: the keyword-tag inventory (item 3) is
  moot; expression-children-in-sequences (item 1) remains wanted for template
  building; the quote-form design (syntax, `$`-splices, parse-time checking)
  moves from deferred to next-in-line for this feature.

**C9a addendum (designer ruling, 2026-07-04): take both tag forms — "for sure."**

1. **Quoted-symbol tags — `<'if' ...>`, `<'var' ...>` — blessed as general
   grammar orthogonality.** Verified working today: they parse, and the quoted
   tag denotes the *plain* symbol (prints as bare `<if ...>` — a write-time
   lexer escape, not a distinct identity). Any symbol is usable as a tag; the
   right form for *ad-hoc* keyword-named elements in user documents.
2. **`lm.` namespace stays as the canonical AST schema** — because the schema's
   problem is not spelling `var` but *being distinguishable from the world's
   documents*: **HTML has a standard `<var>` element** (variables in prose), so
   plain-symbol AST tags would make `doc?<var>` unable to tell markup from code
   in the mixed pipelines that are Lambda's core domain (`<section>`, `<code>`,
   `<output>` are further plausible collisions). Only the namespace provides
   identity separation — XSLT's exact reasoning for `xsl:` (XML had no lexing
   problem at all). Quoted tags solve lexing; the namespace solves identity;
   both are kept for their respective jobs.

**Schema-design footgun discovered while probing (must fix before models are
written):** `<'var' name: 'x'>.name` returns `'x'`, not the tag — the `name:`
*attribute* shadows the built-in `.name` tag property (documented precedence).
Since AST `var` nodes naturally carry a `name:` attribute, the tag accessor is
unreliable for exactly the nodes that need it most. Required: a shadow-proof tag
accessor (e.g. a `tag(elem)` system function) as a prerequisite for the AST
schema and for model-writing generally.

The docs' "closed over metaprogramming" statements must be updated when this
lands, citing this record.

### C10. Vectorized operators (2026-07-04) — RESOLVED: arithmetic stays vectorized; bare comparisons reverted; keyword elementwise family

**Decision (designer rulings):**

1. **`+ - * /` stay vectorized** — `[1,2,3] + 1 = [2,3,4]`, "for sure to have."
2. **Vectorized `< <= >= >` is REVERTED.** The recently-added NumPy-style
   behavior (`[1,2,3] > 1` → `[false,true,true]`) is withdrawn; bare comparison
   operators return to scalar-only, rejoining `==`/`!=` as one uniform,
   C8-coherent comparison family.
3. **Elementwise comparison gets separate operators**, leaning **XPath/XQuery
   keyword family: `eq ne lt le gt ge`** — `img gt threshold` → bool mask.

#### C10.1 The killing exhibit (probed)

`if ([1,2,3] > 99)` **took the then-branch**: the mask is `[false,false,false]`,
but per C2 containers are always truthy — the condition is silently true
regardless of contents. NumPy at least raises ("truth value of an array is
ambiguous"); Lambda's C2 axiom (correct in itself) makes the combination
*silently wrong* instead. Designer: "this blow destroys the entire vectorized
`<` attempt." Also probed: the existing spellings already produce the same
results — `[1,2,3] | ~ > 1` → the identical mask; `[1,2,3] that (~ > 1)` →
`[2,3]` (the filtering most masks exist for, without NumPy's `a[a > 1]`
double-mention).

#### C10.2 The reasoning (recorded)

- **The principled line**: `+ - * /` on arrays is *vector algebra* (vector
  spaces have addition and scalar multiplication — `[1,2]+[3,4]` is
  mathematics); vector spaces have no `<`. Elementwise comparison is a
  computational idiom NumPy invented because Python lambdas were too slow —
  Lambda has pipes, `that`, and a JIT that can compile them to the same
  `vec_cmp` SIMD kernel, so operator overloading was never needed for
  performance (the unobservability principle again: vectorization is an
  implementation property, not a semantic one).
- **The rift argument (designer's own)**: `==`/`!=` can never vectorize (C8
  deep equality is non-negotiable), so vectorized `<` would split the
  comparison family's result types permanently.
- **Julia's dotted family (`.> .< .==`) considered and rejected** — the
  principled precedent (bare = scalar/algebraic, dotted = elementwise, and
  `.==` restores elementwise equality without touching `==`), but **fatally
  ambiguous in Lambda's grammar** (designer: `5.>[...]` — trailing-dot float
  literals and the `arr.0` const-index form contest the dot).
- **`eq ne lt le gt ge` fits an established borrowing**: Lambda already took
  `div` from the XPath/XQuery operator-keyword family, and the house style is
  keyword operators (`and or not is in to div that where`).
- **Caveat recorded — lexeme-meaning inversion**: in XPath/XQuery these
  keywords are the *scalar* ("value") comparisons (the symbolic forms carry
  sequence/existential semantics); Perl, Bash tests, and Fortran also use them
  for scalar comparison. Lambda inverts: symbols scalar, keywords elementwise.
  Accepted because nobody types `gt` by accident (the C2 trap requires the
  *natural* spelling to be dangerous; the keyword is always deliberate) — but
  it must be documented prominently.
- **Residual trap mitigation**: `if (img gt t)` is still a silently-truthy
  mask. Recommended lint: elementwise-comparison result used directly in
  condition position → warning.

#### C10.3 Semantics to pin at implementation

1. Elementwise semantics = scalar `==`/`<` applied per element pair (so nan
   elements produce `false` mask entries — C8.5 poison semantics per element);
   broadcasting rules identical to vectorized arithmetic (scalar↔array,
   array↔array of equal length; mismatch behavior must match arithmetic's).
2. `eq`/`ne` elementwise are *permitted* in this family precisely because they
   are distinct operators — bare `==` is untouched.
3. Scope: numeric typed arrays (ArrayNum) primary; define for generic arrays
   uniformly via per-element scalar comparison.
4. Mask consumption details: does `sum(mask)` count trues (NumPy-style
   bool-as-0/1), mask-indexing `a[mask]`, mask logic (`and`/`or` on masks?) —
   to be specified with the image-toolkit migration.
5. Revert task: remove the `vec_cmp` dispatch from `fn_lt/fn_gt/fn_le/fn_ge`
   (added per the C8.7 code read: `ARRAY_NUM → vec_cmp`); retarget the
   `vec_cmp` kernel to the keyword operators. Migration: image toolkit and
   `numpy_props`/`image_props` tests using bare `>` masks → keyword forms.
6. Grammar: six new operator keywords — check collisions with user identifiers
   named `eq`/`ne`/`lt`/`le`/`gt`/`ge` in existing fixtures/stdlib.

### C11. Total sort order (2026-07-04) — RESOLVED in §C11.4 — and the sort() bug discovery

**The issue** (the "poison-placement flag" from C8.6): under C8.5, `nan` and
`error` are *incomparable* (`nan < x` and `x < nan` both false), so `<` is not a
total order — but comparison sorts require one, or they produce arbitrary or
corrupted output. `sort`/`order by` need a defined **total order over the entire
value domain**.

#### C11.1 Probe discovery: sort() is broken far beyond poison placement

| Probe | Result | Verdict |
|---|---|---|
| `sort([3.0, nan, 1.0, 2.0])` | `[3, nan, 1, 2]` | one nan poisons the sort — nothing sorted |
| `sort([3, null, 1])` | `[3, nan, 1]` | **null converted to nan**; unsorted |
| `sort(["b", "a", "c"])` | **`[nan, nan, nan]`** | **string data silently destroyed** — sort coerces all elements to float |
| `sort([2, "b", 1, "a"])` | `[2, nan, 1, nan]` | mixed arrays: strings become nan |
| `for (x in [...] order by x)` with nan | unsorted | same collapse in for-clauses |

`sort()` is numeric-only-by-coercion today (silent-destruction family, A8-kin);
the docs present it as a general collection function. Fix required under any
ruling: sort must use the type-aware comparison path, never float coercion.
(Related: `fn_lt`'s NUL-unsafe `strcmp` — C8.7 side-find — is the string
comparator sort would rely on.)

#### C11.2 Proposal (review, pending designer ruling)

1. **Two relations, by design**: comparison operators keep C8.5 semantics
   (`nan < 3` stays false; poison incomparable); `sort`/`order by` use a
   separate internal **total order**. Precedents for the split: IEEE 754
   defines `totalOrder` separately from `<` for exactly this; Java splits `<`
   from `Double.compare`; SQL splits `WHERE NULL` from `ORDER BY` placement.
2. **Poison/null placement: pinned tail** — real values first, then `nan`,
   then `error`, then **`null` dead last** ("most absent sorts last"); stable
   among themselves; **pinned regardless of `desc`** (Julia/NumPy behavior —
   `desc` means "biggest real values first", not "nulls first"; simpler than
   SQL's configurable flip).
3. **Cross-family order: Erlang-style total term order** (family rank:
   numbers < strings < symbols < datetimes < … < containers, then
   within-family) — Lambda arrays are heterogeneous (`any[]` is normal, unlike
   SQL's typed columns), and erroring on mixed arrays would fight C5's
   set-oriented spirit. Erlang's term order is the proven precedent for "sort
   anything, deterministically."
4. Details to pin: sort stability (document as stable); container ordering
   (lexicographic by the same total order, or defer with an error); `order by`
   with multiple keys inherits the same total order per key.

#### C11.3 Prior-art survey (correcting two claims in C11.2)

**Q1 — is poison/null pinned at tail? No — three camps:**

- *Total-order participants (flip with direction)*: SQL vendors — famously split
  on which end (PostgreSQL/Oracle: NULL largest, flips to first under DESC;
  MySQL/SQL Server: NULL smallest; the standard punted with `NULLS FIRST/LAST`
  syntax); **Julia** (correction: not pinned — `isless` total order is
  `numbers < NaN < missing`, and `rev=true` reverses wholesale); Java
  (`Double.compare`: NaN largest; `nullsFirst/nullsLast` wrappers = opt-in
  pinning); IEEE 754 `totalOrder` (NaN by sign — at *both* ends:
  `-NaN < -∞ < … < +∞ < +NaN`).
- *Pinned-at-tail regardless of direction (analytics/display camp)*: **pandas**
  (`na_position='last'` independent of `ascending` — the truest precedent),
  **Excel** (blanks always last), **JavaScript** (`undefined` pinned last by
  spec; NaN under numeric comparators corrupts the sort — also the cautionary
  tale), **R** (`na.last` independent of `decreasing`; `sort()` default *drops*
  NAs).
- *Refuse*: **Rust** — `f64` doesn't implement `Ord`; sorting floats requires
  explicitly choosing `total_cmp`.

**Q2 — cross-family ordering references, split cleanly by domain:**

- *Spec'd total order — Lambda's document/data kin, unanimously*:
  **Erlang/Elixir term order** (`number < atom < … < tuple < map < nil < list
  < binary` — the classic); **jq** (`null < false < true < numbers < strings <
  arrays < objects`); **MongoDB** BSON sort order; **CouchDB** view collation;
  **SQLite** storage classes (`NULL < INTEGER/REAL < TEXT < BLOB`) — the
  dynamically-typed DB, closest to Lambda's `any[]`.
- *Error on mixed — general-purpose app languages*: **Python 3 deliberately
  removed** Python 2's cross-type ordering (`1 < "a"` → TypeError; the famous
  2→3 break — implicit cross-type comparison hid bugs); Ruby, Clojure likewise.
  JS's coerce-to-string default (`[10,9,1]` → `[1,10,9]`) is the third-way
  cautionary tale. Note: this camp's objection targets *operator* comparisons,
  which Lambda already keeps strict (C8/C10) — the total order lives only
  inside sort (the two-relation design), so both camps' concerns are satisfied.
- **Null-placement correction to C11.2**: the document-domain precedents are
  unanimous that **null sorts FIRST** (smallest — jq, CouchDB, MongoDB,
  SQLite); null-*last* is the analytics camp's convention. The ruling therefore
  chooses between two coherent packages:
  1. **Document-collation package**: one pure total order, null smallest,
     `desc` = full reversal (simplest formal rule; jq/Mongo/Couch/SQLite
     conformance);
  2. **Analytics package**: term order for real values + `null`/`nan`/`error`
     pinned at tail regardless of direction (pandas/Excel; "real data first,
     always").
  Formal-rule simplicity mildly favors (1); display ergonomics favor (2).

#### C11.4 RESOLVED (designer ruling, 2026-07-04): the Lambda total order

**Package (1) adopted — one pure total order, `desc` = full reversal** ("a more
consistent mental model on data ordering"). The designer's proposed rank order,
refined by review under the governing principle — **the sort order is a total
refinement of `==`: equal values always tie** (else sorting becomes
representation-sensitive, the task_38782787 disease) — yields:

> **null < false < true < number (by value) < datetime < symbol (path is a
> special symbol — same band) < string < binary < sequence (range = list =
> array) < map < object < element < type < function < nan < error**

*(Final form per designer refinement: path folded into the symbol band rather
than a separate rank — consistent with the refinement principle if path/symbol
text-equality ties; and `type < function` rank order fixed.)*

Rulings and refinements recorded:

- **Q3 (designer asked): no `int < float < decimal` ranking — numbers order by
  mathematical value** across all representations (int, sized, int64, float,
  decimal): `2.5` must sort before `3`, and C8's `1 == 1.0` / `1.5n == 1.5`
  force equal-value ties regardless of representation. `-inf` smallest number,
  `+inf` largest; `-0.0 = 0.0 = 0n` tie; ties broken by stability.
- **Range/array split corrected**: the designer's draft had `range < array` as
  separate ranks — but `(1 to 3) == [1,2,3]` (C8 cross-type sequence equality)
  forces **range, `list`, and array into one *sequence* band**, lexicographic
  by elements (ArrayNum transparently included).
- **Types added for totality**: `list` (merged above), **function** (rank after
  element; within-band tie + stability, or site-identity order if cross-run
  determinism is wanted), **type values** (rank after function; tie/stability),
  **path** (slotted after string).
- **nan/error placement — globally last, by design**: this blends the packages
  coherently (null-first per the document-collation kin; poison-beyond-
  everything per the analytics instinct) while remaining a *pure* total order
  (fixed ranks, `desc` reverses everything, no pinning exception). Mental
  model: *null = less than everything (absence); nan/error = beyond everything
  (broken)*. Within-band, nans tie and errors tie (a rank does not require
  mutual comparability; stability preserves input order).
- **Within-band rules**: `false < true`; strings/symbols/binaries bytewise
  UTF-8 (= codepoint order; no locale collation — jq precedent, CouchDB's ICU
  as the cautionary tale; forces the `fn_lt` NUL-unsafe `strcmp` fix);
  **maps compare via canonically sorted keys** (must agree with C8.6-R's
  unordered `==`: equal maps tie — same obligation as the hashing note);
  elements by tag, then attrs (canonical-key), then children; datetime by time
  value (date/time/datetime cross-kind rule to pin at implementation).
- Sort is **stable** (documented); `order by` multi-key applies the same total
  order per key; `desc` = full reversal including null/nan/error.

### C12. Array covariance / assignment subtyping (2026-07-04) — RESOLVED: covariance where values copy, invariance where they're borrowed

**Decision (designer-approved):** resolves A8, the last open finding of the
original review.

1. **Covariance holds** — `int[] <: any[]` — for `is` checks, reads, plain
   (value) parameters, and **assignment**: `var ys: any[] = xs` is legal and
   gives `ys` an independent value (C4 bindings-copy). The docs' covariance
   claim becomes true everywhere it is sound.
2. **`var` parameters are invariant**: passing a `var xs: int[]` argument to
   `pn f(var a: any[])` is a **compile error** ("`var` parameters require an
   exact type match — declare `xs` as `any[]`, or use a value parameter").
3. **Representation widening happens at COW-copy time**: `ArrayNum` (unboxed)
   remains an invisible optimization — a value statically typed `any[]` may
   stay unboxed until a write forces widening; the current
   `ensure_typed_array` runtime rejection is replaced by lazy widening.

**Why (recorded):** covariant *mutable* arrays are the classic soundness hole
(Java's `ArrayStoreException` wart) — but the hole **requires aliasing**:
`ys[0] = "boom"` is only dangerous if visible through `xs`. C4's mutable value
semantics removed aliasing everywhere except the one deliberate channel, `var`
parameters (borrows). Therefore covariance is sound by construction wherever
values copy, and the borrow channel — which carries all the danger — carries
the restriction (invariance). Precedent: Rust (`&mut T` invariant; owned/shared
covariant). A8 thus stops being an array-special problem and becomes a
**corollary of C4** ("mutation is visible or it doesn't exist"). The A8 probe's
log-only failure mode (`ensure_typed_array` logs, execution continues with an
invalid binding) is fixed independently (impl plan 0.7); the invariance compile
check lands with the C4 `var`-param machinery (impl plan Phase 5).
