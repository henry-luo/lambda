# Lambda Documentation Convention

**Version:** 1.1.1 (2026-08-27)

**Status:** normative for how Lambda documentation is organized, written, and
kept honest. Codifies the conventions previously scattered across CLAUDE.md
rule 17, the formal specs' own headers, and working practice.

---

## 1. The document tiers

| Tier | Location | Role | Authority |
|---|---|---|---|
| **Root ADRs** | `doc/Lambda_Formal_Semantics.md`, `doc/Lambda_Formal_Design.md` | Final rulings only — the single sources of truth | **Normative — wins over everything** |
| **Working design docs** | `vibe/*.md` | Secondary ADRs: history, alternatives, reasonings, detailed design | **Normative where no formal ruling covers the point** |
| **Impl plans & progress** | `vibe/impl/*.md` | Full implementation plans, phases, progress, status; issue-fix records | Informative (history) |
| **Issue ledger** | `vibe/Lambda_Issue_Ledger.md` | The one central issue ledger for Lambda | Working record, not normative |
| **Reference grammar** | `lambda/tree-sitter-lambda/grammar.js` | Pseudo design doc: the whole surface language at one glance | **Third in the authority chain** — below vibe (it misses some corner cases) |
| **Detailed design (final)** | `doc/dev/**` | The distilled, detailed design of each subsystem | Informative — never authority |
| **User-facing docs** | `doc/*.md` (everything else) | Formal user-facing language and tool documentation | Descriptive of the rulings |

## 2. Authority and escalation

- **Authority order: Formal specs → vibe design records → the reference
  grammar (`lambda/tree-sitter-lambda/grammar.js`).** Nothing else is in the
  chain. `doc/dev` and `vibe/impl` are informative — they carry history and
  detail, never rulings.
- **The reference grammar is a pseudo design doc.** `grammar.js` shows what
  the surface language is like in one glance and is worth consulting as such;
  it fails to implement some corner cases, which is why it sits below the vibe
  records. This ratifies Design_Syntax ledger 18's order (spec/design doc →
  Tree-sitter grammar → C parser) — deliberately the reverse of execution
  order.
- **Implementation behavior is never evidence for a ruling** and never earns a
  documented exception (Design_Syntax ledger 18). The production C parser is
  implementation, not authority: a divergence between a doc and the
  implementation is an implementation bug to fix, unless a ruling says
  otherwise.
- **Escalation rule (USER, 2026-08-27): always ask the user** when a ruling is
  missing, when there is doubt or ambiguity, when documents conflict with each
  other, or when a document conflicts with the code. Never resolve such a
  conflict silently, and never invent a ruling to fill a gap.

## 3. The formal specs (root ADRs)

The two formal specs are Lambda's root Architecture Decision Records.

**Structure.**

- **Ruling IDs** are section-path IDs: `S4.6.2` is the second ruling of §4.6 of
  the semantics spec; `D2.3.1` likewise in the design spec. The `S` and bare
  `D` series are reserved for these two documents.
- **Revision in place**: a revised ruling keeps its ID with a version suffix
  (`S4.6.2v2`), replacing its predecessor; superseded text is **not carried**
  in the spec (it lives in the vibe record).
- **Doc-level semver**: MAJOR — an existing ruling changed meaning, or added
  rulings break existing programs; MINOR — rulings added compatibly; PATCH —
  editorial.
- **Implementation marks**: a ruling marked `*` is not, or only partially,
  implemented; Appendix A carries the footnote. Unmarked rulings are believed
  implemented; any conformance gap is a bug, never a semantics change.
- **Appendices**: A — implementation footnotes (status + date + verification
  numbers + live residue + a pointer; **never** implementation war stories);
  B — open design issues (`SO#` / `DO#`; the question + a pointer, **never**
  the deliberation); C — the decision-record index mapping sections to vibe
  records.

**Style: the distillation discipline.** The specs stay **brief** — rulings
plus compact one-line why-tags and aphorisms. What stays and what moves:

- **Stays**: the ruling; a compact consequence or principle that pins meaning
  (*"Reads ask a question; writes issue a command"*); a minimal example that
  fixes an edge; a link to the detailed vibe section when a ruling needs more
  background.
- **Moves to vibe**: deliberation history, rejected alternatives and why they
  lost, migration narratives, implementation anecdotes, comparative
  discussions of other languages beyond a citation.
- Before removing text from a spec, **verify the vibe record already carries
  the argument** — add it there first if not. Content is never lost, only
  relocated.

**Citation (CLAUDE.md rule 17).** In chat and in every new or updated design
or implementation doc, cite the `S#`/`D#` ruling when one covers the point;
cite a vibe ledger ID only when none does (vibe IDs remain fine as secondary
anchors: "S7.7.2 [TE-18]"). **When a ruling changes, update BOTH sides** — the
formal spec (in-place `v2` revision + semver bump) and the relevant vibe
design doc. Never let the two diverge silently.

## 4. Working design docs (`vibe/`)

These are the secondary ADRs. They keep what the specs deliberately do not:
the history, the options and alternatives considered, the reasonings, the
detailed design and rulings, and the pitfalls/mistakes/errors made along the
way — a wrong turn recorded is a wrong turn not repeated.

- **Numbering**: each area has its own ledger series (`TE-#`, `K#`, `CW#`,
  `JA#`, `PTH#`, `FC#`, `PD#`, `REH-D#`, …), distinct from the specs' `S#`/`D#`.
  **Never mint a new ID series** for a new doc in an existing area — extend
  the area's series, using sub-numbering (`JR3.3`–`JR3.9` extending `JR3`) for
  follow-on rulings, with a forward pointer at the series home.
- **Style going forward**: design docs focus on capturing the **design and
  the reasonings**. Implementation details, where needed, are kept **briefly
  in an Appendix** — not interleaved with the design (older docs mixed them;
  do not imitate that).
- **Body states current rulings only**: superseded wording moves to a
  struck-through superseded-rulings appendix (the Design_Syntax "Appendix S"
  pattern), annotated with what replaced it — not deleted, not left inline.
- **Headers** carry: ratification status and date, the spec-linkage map
  (which sections became which `S#`/`D#`), and scope.
- Ratification flow: a design is argued in vibe → ratified into the formal
  spec with mapped IDs → the spec states the ruling briefly and the vibe doc
  keeps the argument.
- Caution: a few `vibe/` files are special artifacts, not working design docs
  (`vibe/Lambda_Semantics.md` holds the Redex model — never overwrite it with
  prose).

## 5. Implementation plans (`vibe/impl/`)

Full implementation plans, phase breakdowns, progress tracking, and
implementation status live here — including the implementation records of
issue fixes.

- Filename status suffixes are the convention: `Lambda_Impl_X (done).md` for
  completed plans, `... (retired).md` for retired/archived documents that keep
  their detail and evidence.
- An impl doc may freely carry code-level detail, measurements, and war
  stories — that is its job.

## 6. The issue ledger (`vibe/Lambda_Issue_Ledger.md`)

- **The only issue ledger.** Add new issues here, never to a new sibling file
  or an archived one. Retired sibling ledgers live in `vibe/impl/` with the
  `(retired)` suffix; each keeps its detail while its live residue is indexed
  in the central ledger.
- **Verification discipline**: entries are periodically re-checked against a
  named commit and marked **OPEN** (reproduced, `file:line` anchors
  re-resolved), **PARTIAL** (residue stated), or **RESOLVED** (moved to the
  ledger's resolved appendix). Entries carry stable HTML anchors
  (`<a id="lr02-r9">`) so specs and design docs can deep-link.
- The actual fixing of an issue is recorded in `vibe/impl/*`; the ledger holds
  the issue, its status, and the pointer.

## 7. Detailed design (`doc/dev/`)

`doc/dev` captures the "final", detailed design of each subsystem — the
distilled architecture documents (`lambda/LR_00`–`LR_13`,
`radiant/RAD_00`–`RAD_22`, `js/JS_00`–`JS_16`, each set indexed by its `_00`
overview) plus developer guides (`C_Plus_Convention.md`, `Make_Guide.md`,
`Developer_Guide.md`, …).

- **Informative, never authority.** When a `doc/dev` statement disagrees with
  a formal ruling or a vibe decision record, the `doc/dev` text is stale —
  escalate per §2 if in doubt.
- **Verified-against headers**: each detailed-design doc carries, directly
  under its title, the line
  `> **Last verified against tree:** YYYY-MM-DD`.
  Going stale is allowed, but visibly dated. Periodic re-sync sweeps update
  the content and bump the date; there is no per-change sync duty. (Initial
  sweep 2026-08-27 stamped all 63 `doc/dev` docs from git last-edit dates,
  marked *"initial stamp from git history"* — replace that marker with a
  plain date on the first real re-verification.)
- **Cite `file:line` + exact symbol names**, never quoted code — line numbers
  drift; the symbol name is what the reader confirms against.
- Per-doc "known issues" sections were consolidated into the central ledger;
  do not grow new ones — the `LR_*`/`RAD_*`/`JS_*` documents are the *design*
  record, the ledger is the *issue* record.

## 8. User-facing docs (`doc/*.md`)

The remaining `doc/` files are the formal user-facing documentation: the
language reference set (`Lambda_Reference.md`, `Lambda_Data.md`,
`Lambda_Type.md`, `Lambda_Expr_Stam.md`, `Lambda_Func.md`, …), tool guides
(`Lambda_CLI.md`, `Lambda_Validator_Guide.md`), and per-area support docs
(`Python_Support.md`, `Math_Support.md`, …).

- They **describe the rulings**; on any doubt the formal specs win.
- **Every `lambda` code block must parse** against the current grammar. A
  syntax migration is not complete until the user-doc sweep is done — the S16
  migration left 60 of 172 blocks unparseable (tracked as O4), which is the
  failure mode this rule exists to prevent.
- Keep examples runnable and outputs current; prefer small examples that a
  regression test also covers.

## 9. Cross-cutting rules

- **CLAUDE.md ↔ AGENTS.md mirroring**: any documentation-convention change
  that touches agent instructions is made in both files.
- **Dates are absolute** (2026-08-27), never relative ("last week").
- **Filenames**: `Lambda_Design_<Area>.md` for vibe design docs,
  `Lambda_Semantics_<Area>.md` for semantics records,
  `Lambda_Impl_<Area>.md` for impl plans; status suffixes per §5.
- **Links over restatement**: when one doc needs another's content, link to
  it (deep anchors where available) rather than copying — a copy is a fork
  that will silently diverge.
