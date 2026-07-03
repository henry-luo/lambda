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

| ID | Item | Notes |
|----|------|-------|
| A6/B8 | OOB indexing + builtin error-value leaks | **Ranked next** — the last big semantic hole; `T^E` enforcement bypassed by builtins; array OOB → error value vs string OOB → null |
| A4 | `symbol == string` raises vs docs' `false` | impl and docs must agree; decide strict-incomparable vs total equality |
| A5 | ArrayNum `==` representation-sensitivity; NaN reflexivity carve-out | `==` is the verification harness's primitive — needs a written definition (task_38782787) |
| A8 | Covariance: `is` vs assignment disagree; log-only failure with silent continuation | decide the assignment-subtyping rule; fix the failure mode |
| A2 | Document `/` vs `div` failure modes; truncating div, C-style `%` | documentation only; machine-tier corners already ruled in C3 |
| A9 | REPL rollback replays session | tooling bug fix |
| A10 | Spec gaps: aspirational generics text, `as` semantics, open vs closed map matching, `match` arm ordering, nested `~` scoping | documentation / delete-or-implement decisions |
| B4 | `~` overloaded (pipe / that / match / self) — nested resolution unstated | document innermost-wins + escape idiom; longer-term named binders |
| B5 | `|` union-vs-pipe disambiguation with first-class types | rule must be written for the formal grammar anyway |
| B6 | `[int]` = 1-tuple footgun | decide: re-meaning vs compiler hint |
| B7 | Inference unobservability (gradual guarantee) as an *enforced* invariant | checkable via the formal model + boxed-vs-JIT differential testing |
| — | C1–C4 implementation follow-ups | listed in the predecessor's §C1.7, §C2.3, §C3.3, §C4.4 — including the object-mutation bug cluster, `text` type, `math.max_int`, migration audits, docs rewrites |

---

## Decision records

*(C5 onward — to be added as discussions are held.)*
