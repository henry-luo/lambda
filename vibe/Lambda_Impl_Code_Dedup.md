# Code Deduplication — Status, Tooling, and Coding-Practice Proposal

**Date:** 2026-07-13
**Baseline commit:** `028a75323`
**Companion docs:** `vibe/Lambda_Code_Clean_Up.md` (2026-07-11 six-agent survey, lambda-wide), `vibe/radiant/Radiant_Impl_Clean_Up.md` (2026-07-13 five-agent survey + phased plan, radiant-specific).

This doc records (1) the measured duplication status of the repo, (2) the adopted counter-measures (scan tooling, agent instructions), and (3) two coding-practice directions — coherent module headers and C+ struct-based classes — proposed *instead of* banning `static` C functions.

---

## 1. Current duplication status (measured)

### 1.1 Scan setup

Tool: **Lizard 1.23.0** (`pip3 install lizard`), token-based near-miss clone detection via `-Eduplicate`. Reproduce with:

```bash
lizard -Eduplicate -l cpp radiant/
lizard -Eduplicate -l cpp -x "*tree-sitter*" -x "*lambda-embed.h" lambda/
```

Exclusions are generated code only: the tree-sitter parsers and `lambda/lambda-embed.h` (a Makefile-generated hex dump of `lambda.h`; including it inflates the lambda rate from 14.07% to 15.52%, and including tree-sitter inflates it to a meaningless 55.9%).

### 1.2 Headline numbers

| Tree | LOC scanned | Duplicate blocks | **Duplicate rate** |
|---|---|---|---|
| `radiant/` (283 files) | ~198k | 819 | **9.12%** |
| `lambda/` excl. generated (212 files) | ~487k | 3,519 | **14.07%** |

Caveat: Lizard's rate counts tokens inside any detected duplicate window, so data-table-heavy files (e.g. `css_properties.cpp`, whose "clones" are rows of a legitimate property descriptor table) inflate it somewhat. Treat the rate as a **ratchet metric** (direction matters, absolute value is fuzzy), and the block list as the work queue.

### 1.3 Top offenders (appearances in duplicate blocks)

**radiant/** — the scan independently confirms the manual five-agent survey; every top file below already has a named finding and phase in `Radiant_Impl_Clean_Up.md`:

| File | Dup-block appearances | Known cause (finding ID) |
|---|---|---|
| resolve_css_style.cpp | 898 | 4-side/per-property boilerplate (C1–C3) |
| layout_table.cpp | 240 | border getters, row/col passes (L2, L7) |
| state_store.cpp | 153 | 9 cloned transition loggers (E1) |
| intrinsic_sizing.cpp | 142 | re-implements grid/flex/table ×2 (L6) |
| render_form.cpp | 129 | 7 cloned painter prologues (P1) |
| layout_block.cpp | 119 | margin/float blocks |
| event.cpp | 106 | 9 cloned dispatch scaffolds (E2) |

**lambda/** — the top files match the open items in `Lambda_Code_Clean_Up.md`:

| File | Dup-block appearances | Known cause |
|---|---|---|
| js/js_mir_expression_lowering.cpp | 1,495 | 357-strncmp builtin ladder (ledger §6.6) |
| js/js_runtime.cpp | 1,415 | giant `switch(builtin_id)` ~line 9470 (§6.6) |
| input/css/css_properties.cpp | 1,255 | mostly legit descriptor-table rows (data, not logic) |
| transpile-mir.cpp | 987 | emit-call families (§6.4 MirEmitter remainder) |
| js/js_globals.cpp | 876 | builtin registration blocks |
| module/radiant/radiant_dom_bridge.cpp | 381 | DOM strcmp chains (→ `vibe/Lambda_Jube_DOM3.md` tables) |
| py/transpile_py_mir.cpp + rb/transpile_rb_mir.cpp | 292 + 284 | guest transpilers cloning the MIR emitter (→ Unified AST plan) |

Reading: **the duplication is concentrated, not smeared** — a dozen files account for the bulk of both trees' duplicate mass, and every one of them already has a designed remedy (radiant phases 0–7; the §6.4/§6.6 ledger items; the Jube DOM3 property tables; the Unified-AST MirEmitter unification). The problem is not "where", it's *keeping it from regrowing* — which is what the rest of this doc is about.

### 1.4 Structural root causes (why it regrows)

1. **Discoverability, not laziness.** The writer of the Nth copy didn't know the helper existed — three file-local `static` editing-surface writers, `make_string_item` redefined in 22 JS files, event_sim re-implementing event.cpp statics it couldn't call. 140 headers in radiant (105 of them 1:1 per-.cpp headers) with ~1,555 scattered free-function declarations means there is no single place to *look*.
2. **Per-variant families with no parameterization idiom** — 4 border sides, 9 event kinds, 7 form controls, 5 border sources: each variant hand-expanded instead of table/loop-driven.
3. **Op sets switched in many places** — a paint op touches 5–6 switches, a DL op 11 switch sites, a JS builtin 3 places. Nothing mechanically forces single-point-of-change.

---

## 2. Adopted counter-measures

### The detection ladder (four levels)

Duplication detection runs at four levels. Each level catches what the one below structurally cannot (see Appendix A for why), at increasing cost and decreasing frequency; each level's output seeds the one above:

| Level | Instrument | Catches (clone types) | Cadence | Deterministic? |
|---|---|---|---|---|
| **L1** | Token scanner — Lizard ratchet (DD1) | type-1 exact, type-2 renamed (partial) | every `make lint` | yes — the CI gate |
| **L2** | Embedding similarity index (DD6, sketch) | type-2 fully, type-3 gapped, some type-4 | on demand / before campaigns | semi — tunable threshold, review queue not pass/fail |
| **L3** | LLM code review/scan (DD7) | type-3/4 semantic + the *negatives* (similar code that must NOT be unified) | quarterly or per clean-up campaign | no |
| **L4** | Human code review (DD8) | policy: unify vs legitimate divergence; delete-vs-keep decisions | per campaign / release | n/a — judgment |

L1 is the only level in the CI critical path. L2–L4 are audit tiers: L2 mechanically proposes candidate pairs, L3 turns candidates + its own reading into verified findings docs, L4 decides what actually changes.

### DD1 — Level 1: clone scanning wired into lint, as a ratchet (ADOPTED; tool = Lizard)

Tools evaluated:

| Tool          | Language/stack   | Near-miss clones (renamed identifiers)                          | Notes                                                         |
| ------------- | ---------------- | --------------------------------------------------------------- | ------------------------------------------------------------- |
| **Lizard**    | Python           | partial (`-Eduplicate`)                                         | also gives CCN + function-length per function; single pip dep |
| PMD CPD       | **Java/JVM**     | yes (`--ignore-identifiers --ignore-literals`) — best detection | rejected: pulls the Java stack into the toolchain             |
| jscpd         | Node             | no (exact-token)                                                | nice CI reports                                               |
| Duplo         | C++ (standalone) | no (exact-line)                                                 | minimal dep, weakest detection                                |
| Simian        | Java, commercial | yes                                                             | license + JVM                                                 |
| SonarQube CPD | Java server      | yes                                                             | far too heavy                                                 |
| NiCad         | research (TXL)   | yes (type-3)                                                    | not maintainable as a dev dep                                 |

**Decision: Lizard**, to avoid the Java stack (CPD's superior near-miss detection noted for the record; revisit only if Lizard's ratchet proves too coarse).

Mechanics:
- `utils/check_dup.sh` (new): runs the two commands from §1.1, extracts `Total duplicate rate` + block count per tree, compares against a committed baseline file (`utils/lint/dup_baseline.txt` holding the §1.2 numbers).
- `make lint` gains a `dup-ratchet` step: **fail if either tree's duplicate-block count exceeds baseline**; shrinking updates the baseline (same philosophy as the existing lint ratchets).
- Complement (cheap, custom): a `utils/lint/dead-code/`-style scanner that hashes normalized `static` function bodies across files and reports identical hashes in ≥2 files — catches exactly the cross-file static-copy class (22× `make_string_item`) that token-window scanning reports noisily.
- Frequency: ratchet on every `make lint`; full block-list review feeds the L3 audits (DD7).

### DD6 — Level 2: embedding similarity index (SKETCH — expand when we build it)

**Purpose:** mechanically surface renamed and *gapped* clones (type-2/3, some type-4) that token windows fragment or miss — e.g. the 9 state-transition loggers, or `event_sim`'s re-implementation of `event.cpp` logic — without spending LLM tokens on a full sweep. Output is a **ranked review queue of candidate pairs**, consumed by L3/L4; it is deliberately NOT a CI gate (similarity thresholds are tunable, not ratchetable).

Sketch of the pipeline (`utils/find_similar_code.py`, est. ~120 lines):

1. **Chunk** — function inventory with spans from Lizard's own function-boundary output (already a dependency; tree-sitter is the fallback). Filter: functions ≥ ~40 tokens; skip generated dirs (tree-sitter, lambda-embed.h) and `test/` goldens.
2. **Embed** — one vector per function body via a code-specific embedding model. Local-first so nothing leaves the machine: `jina-embeddings-v2-base-code` or Nomic Embed Code via `sentence-transformers` (runs fine on an M-series CPU/MPS). API alternative if quality demands it: Voyage `voyage-code-3`.
3. **Compare** — all-pairs cosine with plain NumPy. Scale check: radiant+lambda ≈ 15–25k functions → a 25k×768 matrix is ~75 MB and the full similarity product runs in seconds; FAISS/LanceDB only becomes relevant at ~10× that.
4. **Report** — pairs above ~0.85 cosine, ranked, with file:line spans; exclude same-file adjacent overloads and near-identical tiny wrappers. Optional tail: pipe the top-N pairs to a small LLM for a cheap "same logic — unify candidate / policy twin / false positive" triage label.
5. **Cache** — embeddings keyed by (file, function, content-hash) so re-runs only embed changed functions.

Open items deferred to build time (this section becomes a mini-spec then): model choice benchmark on known clones from §1.3, threshold calibration against the verified findings/negatives in the two survey docs (they are a ready-made labeled test set), and whether the triage tail runs by default.

### DD7 — Level 3: periodic LLM code review/scan

**Purpose:** the semantic tier — finds type-3/4 duplication (algorithm mirrors, re-implementations under different names) and, equally valuable, the **verified negatives**: similar-looking code that must *not* be merged (e.g. render_svg vs render_svg_inline running in opposite directions). No mechanical tool produces negatives.

**Method (proven template — the 2026-07 surveys that produced `Lambda_Code_Clean_Up.md` and `Radiant_Impl_Clean_Up.md`):**
- Parallel agents, one per subsystem, each given the subsystem file list + structural hypotheses to verify *by reading code* (findings must cite file:line; name-based inference forbidden).
- Seeds: the L1 block list (top offenders) + the L2 candidate queue once it exists.
- Adversarial pass on findings before they enter a plan (kill plausible-but-wrong findings).
- Output: a findings doc with LOC/risk estimates, ranked by LOC-saved÷risk, negatives recorded in a §1.1-style list so future audits don't rehash them.

**Cadence:** quarterly, or immediately before each clean-up campaign (a campaign should never start from stale findings). **Cost note:** a full-tree sweep is thousands of agent-read files — budget it as a deliberate spend, not background noise.

### DD8 — Level 4: periodic human code review

The judgment tier. Humans decide what the lower levels cannot: unify vs legitimate policy divergence, delete-vs-keep calls on whole subsystems (the D1–D3-style owner decisions in `Radiant_Impl_Clean_Up.md`), naming/API shape for extracted helpers, and whether a "duplication" is actually two things that will evolve apart. Mechanics: review the L3 findings doc per campaign (approve/reject per finding, as done for the RQ1–RQ4 decisions in `Radiant_Imp_Code_Dedup.md`), plus ordinary PR review armed with the CLAUDE.md/AGENTS.md rule-13 lens. Cadence: per campaign and per release.

### DD2 — Agent-instruction additions (ADOPTED)

Add to the CRITICAL rules of **both** `CLAUDE.md` and `AGENTS.md` (agent-instruction changes always go to both files; most code here is agent-written, so writing-time prevention has to live in the agent's instructions). **DONE 2026-07-13** — landed as rule 13 + two DON'T/DO rows in both files:

> **N. NEVER duplicate code.** Grep for an existing helper before writing one. At the 3rd near-identical variant (type/kind/case), extract the shared shape first. To reuse another file's `static`, promote it to the module header — never copy it.

And the corresponding DON'T/DO table rows:

| DON'T                                            | DO                                            |
| ------------------------------------------------ | --------------------------------------------- |
| Copy a `static` helper into another file         | Promote it to the module header, then call it |
| Add a 3rd/4th copy of a per-type/kind/case block | Extract a parameterized helper or table first |

### DD3 — Banning `static` C functions: **KIV** (not adopted)

Arguments recorded for the record:
- *For a ban:* duplicate extern definitions collide at link time, mechanically forcing dedup.
- *Against:* the collision rarely fires in practice (copies drift in name); the root cause is discoverability, not linkage; a ban pollutes the global namespace, grows the symbol table, and invites accidental coupling. `static` remains the correct default for genuinely file-local helpers (~2,900 uses in radiant today).

**Status: KIV.** The promotion convention (DD2's last sentence) plus the duplicate-static-body scanner (DD1) address the same failure mode without the costs. Revisit only if the scanner shows cross-file static copies still growing after DD4/DD5 land.

---

## 3. Coding-practice directions (the structural fix)

Both directions attack root cause #1 (discoverability) at the source: give every function exactly one obvious home, and make that home small enough to read.

### DD4 — Coherent module headers; ban per-C-file headers (PROPOSED)

**Current state (radiant as the worst case):** 140 headers for 136 `.cpp` files — 105 sources have their own private mirror header. Function declarations (~1,555) are scattered across all of them, so neither humans nor agents can survey "what already exists" without opening dozens of files. The per-file header is pure ceremony: it documents nothing, groups nothing, and *hides* the API by shredding it.

**Proposal:** each module exposes a **few coherent headers, organized by domain, not by source file**. Radiant target shape (~8–10 headers replacing ~140):

| Header                       | Absorbs (examples)                                                                                   |
| ---------------------------- | ---------------------------------------------------------------------------------------------------- |
| `view.hpp` (exists)          | view/DOM structs, casts, predicates — already the model citizen                                      |
| `layout.hpp` (exists, grows) | layout API: block/inline/flex/grid/table/intrinsic entry points, box metrics, alignment, percentages |
| `render.hpp` (new)           | paint IR ops, painters, backends, display-list API (`paint_ir.h`, `display_list.h`, `render_*.hpp`…) |
| `event.hpp` (new)            | dispatch, hit-testing, state machine, interaction state                                              |
| `edit.hpp` (new)             | editing surfaces, ranges, caret/selection, clipboard                                                 |
| `style.hpp` (new)            | CSS/HTML style resolution, animation, fonts                                                          |
| `state.hpp` (new)            | state store, schema, dump/log                                                                        |
| `shell.hpp` (new)            | window, surface, webview, frame clock, session                                                       |

A `.cpp` may keep a *private* header only for genuinely internal shared-between-2-3-files details; the default is: **declarations go in the module header or stay `static`**.

Why this is the highest-leverage practice change:
- **(a) Glossary for agentic coding** — an agent greps one header and sees every existing struct and function in the domain before writing a new one. This directly kills the "didn't know it existed" failure mode of §1.4.
- **(b) The API doc** — the header *is* the reference; `doc/dev/radiant/RAD_*.md` can point at it instead of restating signatures.
- **(c) The design space** — new features start by reading (and extending) the module header, so design review happens where the duplication decision is made.

Migration (incremental, zero behavior change):
1. Pilot on one subsystem per tree — radiant `render.hpp` (111 files, worst shredding) and `lambda/format/` (already visitor-unified, easy win).
2. Mechanical: new header includes the old ones → move declarations → old headers become one-line `#include` shims → retire shims as `.cpp` includes are updated (can be spread over many commits).
3. Gate per step: clean build + subsystem baseline suite; `verify_loc_reduction.sh` bonus — shim retirement is itself a LOC reduction (~2–4 lines of guard/include ceremony per dead header).
4. Lint: a simple structural rule (`utils/lint/rules/structural`) flagging *new* `foo.hpp` created alongside `foo.cpp` in migrated modules.

### DD5 — Migrate C style → C+ struct-based classes (PROPOSED, gradual)

The convention already exists and is documented (`doc/dev/C_Plus_Convention.md`: inline member functions on structs, struct inheritance as layout extension, no vtables/STL/exceptions; MIR requires C ABI at JIT boundaries — those stay `extern "C"` free functions). The core (`Item::type_id()`, `Map::get()`, `DomNode` hierarchy) already practices it. What's proposed is applying it **systematically** where today ~2,900 file-local statics and free-function families float unattached.

Why classes reduce duplication (not just aesthetics):
- **Discoverability by construction** — `range.` autocompletes/greps to every existing operation on a DOM range; the 3-files-of-range-logic situation (`dom_range.cpp` / `dom_range_resolver.cpp` / `editing_target_range.cpp`) can't quietly happen when the method list lives on the struct.
- **Controlled growth** — adding a member field or method edits the class definition in the module header (DD4), a visible, reviewable single point — instead of a free function appearing in whichever `.cpp` was open at the time.
- **Per-variant families become methods over state** — `FormControlBox::paint_checkbox()` et al. share the constructor that today is 7 copied prologues; the pattern generalizes.

Adoption rules (no big-bang rewrite):
1. **New code**: any new struct with ≥3 associated free functions is written class-style from day one.
2. **When touching**: a clean-up phase that extracts a helper family (e.g. Radiant P1's `FormControlBox`, E2's `JsDispatchScope`) lands it as a struct with methods, not another free-function cluster — the already-planned phases become the migration vehicle.
3. **Never at the MIR/JIT boundary**: runtime functions callable from JIT'd code keep C ABI (`extern "C"` wrappers over the methods where needed).
4. **No forced conversion** of stable, cohesive C-style files — the goal is structure where growth happens, not churn where it doesn't.

DD4 and DD5 are mutually reinforcing and share the pilot: consolidating `render.hpp` naturally surfaces which free-function clusters are really methods on `PaintOp` / `DisplayList` / `RenderContext`.

---

## 4. Sequencing

| Step | What | Effort | Depends on |
|---|---|---|---|
| 1 | DD1: `utils/check_dup.sh` + baseline + `make lint` ratchet | ~½ day | — |
| 2 | DD2: CLAUDE.md + AGENTS.md rule + DON'T/DO rows | ✅ done 2026-07-13 | — |
| 3 | DD1b: duplicate-static-body scanner in `utils/lint/dead-code/` | ~½ day | — |
| 4 | DD4 pilot: radiant `render.hpp` + `lambda/format/` header consolidation | days | 1 (ratchet guards it) |
| 5 | DD5 via clean-up phases: Radiant_Impl_Clean_Up P1/E2 etc. land as structs | ongoing | 4 (headers give the structs a home) |
| 6 | DD4 rollout to remaining modules; structural lint rule | ongoing | 4 |
| 7 | DD3 revisit: check scanner trend; ban stays KIV unless copies still grow | quarterly | 3 |
| 8 | DD7 (L3): LLM sweep before each clean-up campaign / quarterly | recurring | 1 (seeds from ratchet block list) |
| 9 | DD6 (L2): build `utils/find_similar_code.py` when the next L3 sweep needs cheaper seeding — expand the DD6 sketch into a mini-spec first | future, ~1 day | — |
| 10 | DD8 (L4): human review of each L3 findings doc; owner decisions per campaign | recurring | 8 |

The existing clean-up campaigns pay down the measured 9.12% / 14.07%; DD1–DD2 stop the regrowth; DD4–DD5 remove the structural reason it happened in the first place.

---

## Appendix A — How the clone scanners work internally (background)

Neither Lizard nor PMD CPD parses code into an AST or does semantic analysis. Both are **token-stream** tools sharing one pipeline shape:

```
source → lexer → token stream → normalize tokens → hash fixed-size windows
       → find equal-hash windows across the codebase
       → extend matches into maximal runs → report blocks + locations
```

"Linting" is therefore just reporting — the tools print duplicate blocks and a rate; the pass/fail gate is the ratchet wiring we add around them (DD1).

### A.1 Lizard `-Eduplicate` (verified against `lizard_ext/lizardduplicate.py`, v1.23.0)

1. **Tokenize** with the same lightweight per-language state-machine lexer Lizard uses for cyclomatic-complexity counting; it tracks function boundaries but builds no parse tree.
2. **Unify tokens** (`_unified_token`) — the interesting part:
   - Every number/string literal → `'1'`; the `-` operator → `+` (so `x - 3` matches `y + 7`).
   - Identifiers → **scope-consistent placeholders**: first distinct identifier in a scope becomes `v0`, next `v1`, … So `a+b` matches `x+y` (both `v0+v1`), but `a+a` does **not** match `x+y` (`v0+v0` vs `v0+v1`) — the same *pattern of variable reuse* is required, which suppresses false positives.
   - **Member names after `.` / `->` are deliberately NOT unified** — `s->width.top` and `s->width.left` remain different tokens.
3. **Window + hash**: sliding 31-token window over the unified stream; the window's concatenated token string is its hash key. Windows with ≥10 constant tokens are skipped (a data-table suppressor — imperfect, as the `css_properties.cpp` result in §1.3 shows).
4. **Group + extend**: equal-hash windows appearing in 2+ places are candidates; a BFS advances all matching occurrences window-by-window while they keep matching, stops at function boundaries, and reports maximal runs (sub-blocks of already-reported clones filtered).
5. **Duplicate rate** = duplicated tokens ÷ total tokens.

Consequence for our codebase: the 4-side border blocks in `resolve_css_style.cpp` differ mainly in `.top`/`.left`/`.right`/`.bottom` **member** accesses — exactly what Lizard refuses to unify — so it reports them as fragmented shorter matches rather than one clean 4-way clone. (The file still tops the radiant scan on its other verbatim repetition.)

### A.2 PMD CPD

1. **Tokenize** with PMD's real per-language lexers; comments/whitespace dropped, line mapping kept.
2. **Normalize** only on request: `--ignore-identifiers` maps *every* identifier — member names included — to one placeholder; `--ignore-literals` likewise. Blanket substitution, no consistency constraint.
3. **Match** via the **Karp–Rabin rolling hash**: hash every window of `--minimum-tokens` length in O(1) per step, bucket equal hashes, verify token-by-token (hash-collision guard), greedily extend to maximal shared runs.
4. **Report** each duplication with all occurrences; `--fail-on-violation`-style flags provide a CI gate.

Because its normalization is blanket, CPD with `--ignore-identifiers` *would* report the four border-side blocks as one 4-way clone (`.top` vs `.left` unify too) — the concrete sense in which its near-miss detection is stronger than Lizard's — at the cost of more false positives (`a+a` ≡ `x+y` under blanket renaming) and the JVM dependency that ruled it out (DD1).

### A.3 Limits of both (clone taxonomy)

- **Type-1** (exact copies): both catch.
- **Type-2** (renamed): Lizard catches with its consistency constraints (members excluded); CPD catches fully with the ignore flags.
- **Type-3** (gapped — a statement inserted/deleted mid-clone): **neither catches.** The token run breaks into two shorter matches or falls under threshold. This is why the 9 state-transition loggers (identical skeleton, different one-line value writer in the middle) appear as fragmented matches, not one 9-way clone.
- **Type-4** (same behavior, different code): out of reach for any token tool.

Practical conclusion: token scanners are a cheap regression **ratchet**, not a substitute for structural review — the manual/agent surveys (§1.3 companions) found duplication classes the scanners under-report, and the DD1b static-body hash scanner covers a specific cross-file class the token windows report only noisily.
