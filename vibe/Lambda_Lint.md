# Lambda Lint — A Unified Project-Policy Linter

## Status

**Phase 1 (ast-grep pattern rules) is shipped** with the adjustments below.
**Phase 2 (alint structural rules)** is proposed in §6; not yet implemented.
Rollout status of individual rules is tracked in §5.

| Area | Status |
|------|--------|
| Runner | Shipped as `utils/lint/run.sh` (bash + jq, not the originally-proposed Python). |
| sgconfig + rule layout | Shipped at repo root + `utils/lint/rules/c-cpp/*.yml`. |
| Suppression | Option B shipped — semantic `*_OK` markers (`metadata.suppress` per rule). |
| 10 legacy `check-*` targets | Replaced by 19 ast-grep rules; legacy targets deleted. |
| `check-state-machine` | Kept as Python; dispatched by `run.sh` via `STRUCTURAL_CHECKS`. |
| Make integration | Single entry point: `make lint` (formerly `lint-policy`). |
| Reports | Shipped beyond proposal — `make lint ARGS=--report` writes `test_output/lint/Report_NNN.{md,json,tsv}` for AI-agent batch fix workflows. |
| `.ls` custom-language | Wired in `sgconfig.yml`; first `.ls` rule still TODO. |
| CI integration | **Deferred — manual invocation only for now.** See §3.5. |
| §4 future rules | Tier-1 (high-confidence) shipped; baselined-via-warning rules shipped at `warning`; type-aware / cross-language rules deferred. See §5. |
| **Phase 2 — alint structural engine** | **Proposed (§6).** Replaces `check_ls_golden.py` and unlocks cross-file/file-graph/pair-hash rule families. Manifest unification recommended over codegen. |

## Background

The Lambda/Radiant codebase enforces a growing set of **project-specific code
policies** — bans, ownership invariants, and migration guardrails — that no
off-the-shelf analyzer knows about. Today each policy is a hand-rolled
`check-*` target in the `Makefile`, implemented as a tower of `grep`/`perl`
pipelines (plus two `utils/*.py` scripts). They work, but:

- **Every check reinvents the same machinery** — pattern + allowlist excludes +
  inline-suppression marker + file scoping + count/print/`exit 1`. The
  `check-radiant-casts` target alone is ~60 lines of brittle shell with four
  near-duplicate `grep` blocks and two hand-maintained file lists.
- **`grep` matches text, not code.** The long `grep -v` allowlist chains
  (`mem_alloc`, `arena_alloc`, `rpmalloc`, comment lines, `^\s*\*`, specific
  files…) exist *only* to undo `grep`'s lack of syntactic awareness. They are a
  maintenance tax and a source of silent false-negatives.
- **It is C/C++-only.** We also own 3,000+ `.ls` scripts and a LambdaJS engine,
  and have no way to apply structural policy rules to them.
- **New rules are high-friction.** Adding a guardrail means writing another
  bespoke shell block, so policies that *should* be enforced (CLAUDE.md rules on
  `printf` debugging, `std::` types, `/tmp` paths) are enforced only by reviewer
  discipline.

This document proposes consolidating the pattern-based policy checks under a
single tool — **[ast-grep](https://ast-grep.github.io/)** — with a thin runner
that also fronts the genuinely-structural checks that remain as code.

### Current lint inventory

| Target | Impl | Class | Disposition |
|--------|------|-------|-------------|
| `check-tag-or` | perl | duplicate-OR (`$X == LMD_TYPE_ARRAY \|\| $X == LMD_TYPE_ARRAY`) | → **ast-grep** (repeated metavar) |
| `check-raw-alloc` | grep | call-ban + allowlist | → **ast-grep** |
| `check-int-cast` | grep | cast-ban in radiant layout files | → **ast-grep** |
| `check-radiant-casts` | grep ×4 | `View*`/`Dom*` cast-ban | → **ast-grep** |
| `check-radiant-ownership` | grep | retained-field write-ban | → **ast-grep** |
| `check-item-cast` | grep | raw `Item` payload cast-ban | → **ast-grep** |
| `check-css-temp-decl` | grep | fragile CSS shorthand temp decl | → **ast-grep** |
| `check-state-store` | grep ×9 | forbidden field access/write | → **ast-grep** (mostly) |
| `check-string-scan` | `utils/check_string_scans.py` | unsafe `strchr`/ctype scan | → **ast-grep** (core) + py (soft) |
| `check-state-machine` | `utils/check_state_machine.py` | enum→table **coverage** | **keep as code** (structural) |
| `check` | grep | TODO/FIXME inventory | optional → ast-grep |
| `lint` | cppcheck | full static analysis | **keep** (orthogonal) |
| `tidy` | clang-tidy | full static analysis | **keep** (orthogonal) |
| `analyze` | clang analyzer | dataflow analysis | **keep** (orthogonal) |
| `format` | clang-format | formatting | **keep** (orthogonal) |

The 10 `check-*` policy targets are the unification target. `cppcheck` /
`clang-tidy` / `clang --analyze` are **not** replaced — they do real
semantic/dataflow analysis ast-grep cannot, and remain separate CI gates.

---

## 1. Tools surveyed

| Tool | Mechanism | Multi-language | Dep weight | Soundness for "ban everywhere" | Verdict |
|------|-----------|----------------|------------|-------------------------------|---------|
| **`grep`/`perl` (status quo)** | text/regex | yes (text) | none | exhaustive (never skips), but text-only → allowlist tax + false negatives | baseline |
| **In-house config-driven grep runner** | text/regex + YAML config | yes (text) | none (Python) | exhaustive | safe but still text-level; doesn't address `grep`'s core weakness |
| **clang-tidy / clang-query** | Clang AST + types | C/C++ only | heavy (LLVM, `compile_commands.json`, C++ plugin to extend) | type-aware, most precise | best for *type-dependent* rules; overkill + monolingual as a unifier |
| **cppcheck** | C/C++ analysis | C/C++ only | medium | its own checks, not policy DSL | keep as a separate gate; not a policy engine |
| **Semgrep** | tree-sitter + own engine | many | heavy (Python/OCaml; version-pinned) | parses-then-**skips whole file** on parse error → false negatives; C++ is beta | workable, ranked below ast-grep |
| **ast-grep (`sg`)** | tree-sitter | many (+ **custom grammars**) | light (single Rust binary) | error-tolerant partial trees (per-region, not whole-file skip) | **chosen** |

---

## 2. Why ast-grep

### 2.1 vs `grep` — code, not text

Nearly all the allowlist machinery in the current targets is `grep` overhead:

```make
# check-raw-alloc today: pattern + 14 lines of `grep -v` to undo text matching
grep -rn '\bmalloc\s*(\|\bcalloc\s*(\| ...' lambda/ radiant/ \
  | grep -v 'mem_alloc\|mem_calloc\| ...' \
  | grep -v 'raw_malloc\|raw_calloc\| ...' \
  | grep -v 'arena_alloc\|pool_alloc\| ...' \
  | grep -v 'rpmalloc\|rpfree\| ...' \
  | grep -v '^\s*//' | grep -v '^\s*\*' | grep -v '^\s*#' ...
```

In ast-grep the pattern `malloc($$$ARGS)` matches **a call expression whose
callee is the identifier `malloc`**. It does not match `mem_alloc(...)`,
`pool_alloc(...)`, `rpmalloc(...)`, the word inside a comment, or a string
literal — because none of those are a call to a function *named* `malloc`. The
entire `grep -v` allowlist evaporates; what remains is the actual policy.

The same wins apply across the board:
- `check-radiant-casts`'s four duplicated `grep` blocks (C-cast vs `static_cast`,
  two file groups) collapse into one rule with `any:` alternatives — comments and
  whitespace/multiline variants are handled by the parser, not by us.
- `check-tag-or`'s perl duplicate-detection becomes a one-line pattern:
  `$X == LMD_TYPE_ARRAY || $X == LMD_TYPE_ARRAY` — ast-grep requires the repeated
  `$X` to bind the **same** node, which is exactly the invariant.

### 2.2 vs Semgrep — lighter, and safer on parse errors

Both are tree-sitter-class structural matchers with near-identical YAML rule
ergonomics, so the deciding factors are operational:

- **Dependency.** ast-grep is a single self-contained Rust binary — trivial to
  pin, vendor, and run in CI. Semgrep drags in a Python/OCaml runtime.
- **Soundness on broken parses.** This matters for *exhaustive bans* like "no raw
  `malloc` anywhere." Semgrep parses a file and, on a parse error, can **skip the
  whole file** — a skipped file silently passes, so a violation slips through.
  ast-grep uses tree-sitter's error recovery: it produces a *partial* tree with
  `ERROR` nodes and keeps matching the valid regions. Coverage degrades locally,
  not file-wide. (Residual caveat + backstop in §3.7.)
- **C++ maturity.** Semgrep's C++ support is explicitly beta. Our "C+"
  (C-compatible subset, light on templates) is friendlier to both, but ast-grep's
  tree-sitter-C/C++ is the same grammar family we already depend on.
- **The registry is irrelevant here.** Semgrep's main edge is its community rule
  registry; every rule we need is bespoke, so that edge is moot.

### 2.3 vs clang-tidy — right tool for a *different* job

clang-tidy is the only surveyed tool that understands **types**, which would make
exactly one of our checks more precise: `check-int-cast` could flag only `(int)`
casts whose operand is *actually* `float`-typed, eliminating most of the 284
`INT_CAST_OK` annotations. But:

- it is C/C++-only (fails reason #1);
- extending it means a compiled C++ plugin + a maintained `compile_commands.json`;
- it does not naturally express "ban X except in this allowlist" policies.

So clang-tidy stays a **separate semantic gate** (the existing `tidy` target),
and is noted in §4 as a *future precision upgrade* for the int-cast rule
specifically — not as the unifier.

### 2.4 The two reasons that tip it

1. **Multi-language by design.** One rule engine spans C, C++, the LambdaJS
   sources, and — via **custom grammars** — Lambda `.ls` itself (§3.8). No other
   surveyed tool lints our whole stack.
2. **We are already a tree-sitter shop.** Lambda's parser, the JS engine, and the
   grammar tooling are all tree-sitter. ast-grep's node-kind / field / pattern
   model is the *same mental model* the team already uses in
   `grammar.js` / `ts-enum.h`. Authoring rules reuses knowledge we have, and we
   can register our own `tree-sitter-lambda` grammar directly.

---

## 3. Design: replacing the existing checks

### 3.1 Layout

```
sgconfig.yml                  # ast-grep project config (repo root)
utils/lint/
  rules/
    c-cpp/
      no-raw-alloc.yml
      no-int-cast-radiant.yml
      no-radiant-view-cast.yml
      retained-field-write.yml
      no-item-payload-cast.yml
      css-temp-decl.yml
      dup-typeid-or.yml
      state-store-*.yml
      unsafe-string-scan.yml
    structural/
      check_state_machine.py  # stays code (coverage)
      check_ls_golden.py      # NEW: every *.ls test has a *.txt (CLAUDE rule 8)
  run.py                      # thin runner: sg scan + dispatch structural + aggregate
```

`sgconfig.yml`:

```yaml
ruleDirs:
  - utils/lint/rules
# custom Lambda grammar registered here later (see §3.8)
```

### 3.2 Rule anatomy (worked examples)

**`no-raw-alloc.yml`** — replaces ~30 lines of shell:

```yaml
id: no-raw-alloc
language: c          # applies to .c/.h; a sibling rule sets language: cpp
severity: error
message: >
  Raw allocation call. Use mem_alloc/mem_free (or raw_malloc/raw_free with a
  trailing // RAWALLOC_OK: <reason>), arena_alloc/pool_calloc for Lambda objects.
rule:
  any:
    - pattern: malloc($$$A)
    - pattern: calloc($$$A)
    - pattern: realloc($$$A)
    - pattern: free($$$A)
    - pattern: strdup($$$A)
    - pattern: strndup($$$A)
files:
  - "lambda/**/*.{c,cpp,h,hpp}"
  - "radiant/**/*.{c,cpp,h,hpp}"
ignores:
  - "**/tree-sitter*/**"
  - "lambda/lambda-wasm-main.c"
  - "**/windows_compat.h"
```

The `mem_alloc` / `arena_alloc` / `rpmalloc` / comment / `^\s*\*` exclusions are
**gone** — they were artifacts of text matching. File scoping that was a hand-typed
list becomes glob `files:`/`ignores:`.

**`no-int-cast-radiant.yml`** — `check-int-cast`, parity (still syntactic; see §4
for the type-aware upgrade):

```yaml
id: no-int-cast-radiant
language: cpp
severity: error
message: "Unmarked (int) cast in layout code truncates float dimensions. Add // INT_CAST_OK: <reason> or drop the cast."
rule:
  pattern: (int)$EXPR
files:
  - "radiant/layout_*.cpp"
  - "radiant/grid_*.cpp"
  - "radiant/intrinsic_sizing.cpp"
```

**`no-radiant-view-cast.yml`** — folds all four `check-radiant-casts` grep blocks
into one rule via `any:` (C-cast + `static_cast`, both node forms handled by the
parser):

```yaml
id: no-radiant-view-cast
language: cpp
severity: error
message: "Unsafe View*/Dom* downcast. Use lam::view_as_*/dom_as<> from lib/tagged.hpp."
rule:
  any:
    - pattern: (ViewBlock *)$X
    - pattern: (ViewText *)$X
    # …Span/Element/Table*/Marker, Dom{Node,Element,Text,Comment}
    - pattern: static_cast<ViewBlock *>($X)
    - pattern: static_cast<DomElement *>($X)
    # …
files: ["radiant/**/*.{cpp,hpp,mm}"]
```

**`dup-typeid-or.yml`** — `check-tag-or`, perl → repeated-metavar:

```yaml
id: dup-typeid-or
language: cpp
severity: error
message: "Duplicate `$X == LMD_TYPE_ARRAY` in the same OR — likely a copy-paste bug (second operand should differ)."
rule:
  pattern: $X == LMD_TYPE_ARRAY || $X == LMD_TYPE_ARRAY
files: ["lambda/**/*.{c,cpp,h,hpp}"]
```

**`retained-field-write.yml`** — `check-radiant-ownership`, assignment with a
member-access LHS, scoped by relational rules instead of `grep -v` on filenames:

```yaml
id: retained-field-write
language: cpp
severity: error
message: "Direct write to a retained Radiant field. Route through radiant/retained_fields.hpp ownership helpers."
rule:
  any:
    - pattern: $OBJ->tag_name = $V
    - pattern: $OBJ->class_names = $V
    - pattern: $OBJ->family = $V
    - pattern: $OBJ->image = $V
    - pattern: $OBJ->text_content = $V
    - pattern: $OBJ->source_path = $V
    - pattern: $OBJ->source_data = $V
files: ["radiant/**/*.{cpp,hpp}", "lambda/input/css/dom_element.{cpp,hpp}"]
ignores: ["radiant/retained_fields.hpp"]
```

`check-state-store`'s nine pattern groups port the same way (one rule file each,
named for the invariant); the field-write groups become `$OBJ->disabled = $V`
style patterns with `files`/`ignores` scoping. `check-css-temp-decl` and
`check-item-cast` are single-pattern rules.

### 3.3 Suppression

Today's markers: `INT_CAST_OK` ×284, `RAWALLOC_OK` ×21, plus single-digit
`RADIANT_CAST_OK` / `CSS_TEMP_DECL_OK` (≈307 total). Two options:

- **(A) Adopt ast-grep's native ignore** — `// ast-grep-ignore: <rule-id>`. One-time
  codemod converts each existing marker to the rule-scoped form. Cleanest
  long-term; native to the tool.
- **(B) Keep the semantic markers** — wrap `sg scan --json` in `run.py` and drop
  any finding whose line carries the legacy `*_OK` comment. Zero churn, preserves
  the searchable, self-documenting `// INT_CAST_OK: <reason>` convention the team
  already reads.

**Recommendation:** start with (B) for a frictionless cutover (the marker-as-
documentation habit is worth keeping), and treat (A) as optional later. `run.py`
owns the suppression policy either way, so the choice is reversible.

### 3.4 The runner & Make integration

`utils/lint/run.py`:
1. invoke `sg scan --json utils/lint/rules` (one parse pass, all pattern rules);
2. apply the suppression filter (§3.3 option B);
3. dispatch the structural Python checks (`check_state_machine.py`,
   `check_ls_golden.py`);
4. aggregate findings, print a per-rule summary, exit non-zero on any error-level
   finding; support `--format=github` for CI annotations.

Make targets become thin and backward-compatible:

```make
lint:               ; @utils/lint/run.sh           # umbrella entry point
check-raw-alloc:    ; @$(PYTHON) utils/lint/run.py --rule no-raw-alloc
check-int-cast:     ; @$(PYTHON) utils/lint/run.py --rule no-int-cast-radiant
# … each legacy target delegates to one rule id (no broken muscle memory / CI)
```

### 3.5 CI

**Deferred.** For now `make lint` is run manually as a developer/release-time
check, not wired into any `.github/workflows/*.yml`. When CI integration
happens later, it should run as its own gate alongside `analyze` / `tidy` /
`lint-cppcheck` (per `vibe/Lambda_CICD.md` §code-quality), use the official
**ast-grep GitHub Action** or `sg scan --json` → annotations, and **pin the
ast-grep version** in CI — matcher behavior can shift across releases.

### 3.6 Migration plan (parity-first, low-risk)

1. **Stand up infra** — add `sgconfig.yml` + `run.py`; pin ast-grep.
2. **Shadow mode** — port 2 representative rules (`no-raw-alloc`,
   `no-radiant-view-cast`). Run old target + new rule on the current tree and
   **diff the findings**. Investigate every delta until they match (the deltas
   are usually `grep` false-positives the AST correctly drops — confirm, don't
   assume).
3. **Cut over rule-by-rule** — once a rule reaches parity, repoint its Make target
   to `run.py` and delete the shell/perl block.
4. **Backstop the highest-stakes ban** — keep a trivial `grep` cross-check for raw
   allocation for one release cycle as a soundness net against partial-parse gaps
   (§3.7); retire once confidence is established.
5. **Keep structural checks as code** — `check-state-machine` stays Python under
   the runner.

### 3.7 The one honest caveat

ast-grep matches over tree-sitter trees. If a code region lands inside an `ERROR`
node (unparseable macro soup, mid-edit file), a match there can be missed —
*locally*, unlike Semgrep's whole-file skip. Mitigations: `run.py` surfaces
ast-grep's parse-error diagnostics (treat a new parse error as a CI warning), and
the §3.6.4 grep backstop guarantees the single most important ban stays
exhaustive during migration. In practice our "C+" sources parse cleanly, so this
is a tail risk, not a daily one.

### 3.8 Forward: linting Lambda `.ls` (the multi-language payoff)

ast-grep supports **custom languages**: point `sgconfig.yml` at a compiled
tree-sitter grammar and its file extensions. We already build
`tree-sitter-lambda` — registering it lets us write structural rules over the
3,000-script `.ls` corpus with the same engine (deprecated-syntax sweeps,
banned-builtin usage, style invariants). Same story for the LambdaJS sources via
tree-sitter-javascript. This is the capability `grep`, clang-tidy, and cppcheck
fundamentally cannot offer.

```yaml
# sgconfig.yml
customLanguages:
  lambda:
    libraryPath: lambda/tree-sitter-lambda/libtree-sitter-lambda.dylib
    extensions: [ls]
```

---

## 4. Future lint rules for a safer codebase

ast-grep makes new policies cheap, so we can enforce rules that today rely only on
reviewer discipline. Feasibility: **easy** (single pattern), **medium** (needs
relational `inside`/`has`/`not` + baselining), **type/structural** (needs
clang-tidy or a code script — *not* ast-grep).

### 4.1 Enforce CLAUDE.md rules that currently have no checker

| Rule | Catches | CLAUDE.md | Feasibility | Note |
|------|---------|-----------|-------------|------|
| `no-printf-debug` | `printf`/`fprintf`/`std::cout` for debugging → use `log_*` | Rule 4 | medium | 1,507 hits today — must scope (allow CLI/REPL/help output dirs) + baseline before turning to `error` |
| `no-std-containers` | `std::string`/`vector`/`map`/`unordered_map` → use `lib/` (`Str`,`ArrayList`,`HashMap`) | Rule 3 | easy–medium | 178 hits — `pattern: std::vector<$$$T>` etc.; baseline existing, block new |
| `no-tmp-path` | string literals starting `"/tmp"` → use `./temp/` | Rule 2 | easy | regex constraint on string-literal node |
| `ls-test-has-golden` | every new `test/**/*.ls` has a sibling `*.txt` | Rule 8 | structural | filesystem coverage check in `run.py`, not a pattern |

These four convert written-but-unenforced rules into CI gates — arguably the
highest-value items here, since they close the gap between policy and enforcement.

### 4.2 Memory & lifetime safety (extends current themes)

| Rule | Catches | Feasibility |
|------|---------|-------------|
| `no-new-delete` | raw `new`/`delete` in pool/arena-managed code | easy |
| `no-manual-refcnt` | direct `->ref_cnt++`/`--` instead of GC API | easy |
| `no-unsafe-libc-str` | `strcpy`/`strcat`/`sprintf`/`gets` → bounded variants (classic overflow class) | easy |
| `checked-pool-alloc` | deref of `pool_calloc`/`arena_alloc` result with no NULL check | type/structural (clang analyzer) |

### 4.3 Radiant layout correctness

| Rule | Catches | Feasibility |
|------|---------|-------------|
| `no-int-layout-decl` | `int x = view->width;` capturing a float field (extends `check-int-cast` beyond casts) | medium (pattern) / type (precise) |
| `int-cast-type-aware` | upgrade `no-int-cast-radiant` to flag only float-typed operands → retires most of the 284 `INT_CAST_OK` | **clang-tidy** (type info) |
| `no-raw-css-value-stack` | extend `check-css-temp-decl` patterns across all CSS resolution files | easy |

### 4.4 API & structure hygiene

| Rule | Catches | Feasibility |
|------|---------|-------------|
| `no-using-namespace-std-in-header` | `using namespace std;` in `*.hpp`/`*.h` | easy |
| `c-style-cast-in-cpp` | C-style casts in `.cpp` → `static_cast`/`reinterpret_cast` (broad; baseline first) | easy |
| `require-itemnull-on-error` | error paths returning a bare `NULL`/`0` instead of `ItemNull`/`ItemError` | medium |
| `log-prefix-convention` | `log_*` first arg lacks a distinct searchable prefix (Rule 9) | medium (regex on literal) |
| `todo-inventory` | `TODO`/`FIXME`/`XXX` reporting (replaces `make check`) | easy |

### 4.5 Cross-language (after §3.8)

| Rule | Catches | Feasibility |
|------|---------|-------------|
| `ls-deprecated-syntax` | retired Lambda constructs across the 3,000-script corpus | custom-lang |
| `ls-banned-builtin` | use of deprecated/unsafe system functions in `.ls` | custom-lang |
| `js-engine-policy` | LambdaJS-internal invariants over `lambda/js/**` sources | js |

---

## 5. Rule-rollout status

### 5.1 Shipped (28 ast-grep rules + 2 structural)

Severity drives the gate: `error` rules fail `make lint`; `warning` and `info`
surface in the report but don't block. New broad rules ship at `warning` to
avoid a fix-everything cliff — they can graduate to `error` later as backlog
clears.

**Legacy replacements (19 error rules, exact parity with the deleted Make
targets):** `css-temp-decl`, `dup-typeid-or`, `no-int-cast-radiant`,
`no-item-payload-cast`, `no-radiant-view-cast-{layout,render}`, `no-raw-alloc`,
`retained-field-write`, `unsafe-string-scan`, plus the 9-rule `state-store-*`
family.

**§4 future rules shipped:**

| Rule | Severity | Findings | §4 ref | Notes |
|------|----------|---------:|--------|-------|
| `no-tmp-path` | error | 1 | §4.1 | 5 legit cross-platform fallbacks marked `TMP_PATH_OK` |
| `no-manual-refcnt` | error | 0 | §4.2 | Preventive — blocks future regressions |
| `no-using-namespace-std-in-header` | error | 0 | §4.4 | Preventive |
| `no-raw-css-value-stack` | error | 0 | §4.3 | Extends `css-temp-decl` outside `resolve_css_style.cpp` |
| `no-unsafe-libc-str` | warning | 28 | §4.2 | `strcpy`/`strcat`/`sprintf`/`gets` |
| `no-new-delete` | warning | 70 | §4.2 | Scoped to non-JS code; JS engine excluded |
| `no-std-containers` | warning | 12 | §4.1 | CLAUDE.md rule 3 |
| `no-printf-debug` | warning | 1266 | §4.1 | CLAUDE.md rule 4; CLI/REPL/help/main excluded |
| `todo-inventory` | info | 92 | §4.4 | Backlog inventory |
| `ls-test-has-golden` | structural | 133 | §4.1 | CLAUDE.md rule 8; preventive, exit-0 default + `--strict` mode |

### 5.2 Outstanding

Cross-language rules (require user specification of *what* to ban):

| Rule | Status | Blocker |
|------|--------|---------|
| `ls-deprecated-syntax` | Infra ready (custom-lang wired) | Need list of retired `.ls` constructs |
| `ls-banned-builtin` | Infra ready | Need list of deprecated/unsafe `.ls` builtins |
| `js-engine-policy` | Not started | Need invariant catalog for LambdaJS internals |

Type-aware rules (need clang-tidy, not ast-grep — separate semantic gate):

| Rule | Status | Notes |
|------|--------|-------|
| `int-cast-type-aware` | Not started | Would retire most of the 284 `INT_CAST_OK` markers; requires clang-tidy plugin or compile DB |
| `checked-pool-alloc` | Not started | Requires clang analyzer (dataflow) |
| `no-int-layout-decl` | Not started | Type-aware variant of `no-int-cast-radiant` |

Medium-effort pattern rules (not yet authored — open by default):

| Rule | Status | Notes |
|------|--------|-------|
| `c-style-cast-in-cpp` | Not started | Broad; baseline first |
| `require-itemnull-on-error` | Not started | Needs return-context analysis (likely structural) |
| `log-prefix-convention` | Not started | Regex on the literal first arg of `log_*` |

CI integration deferred (§3.5) — `make lint` is manual today.

---

## 6. Phase 2 — Structural rules via alint

### 6.1 Why a second engine

ast-grep matches **node patterns over file contents**. Powerful for "ban X
syntax in C/C++", but it cannot express any rule that talks about the
filesystem shape itself:

- "for every `X.ls` under `test/lambda/`, a sibling `X.txt` must exist"
  (CLAUDE.md rule 8 — already shipped as `check_ls_golden.py`)
- "every workspace member listed in `Cargo.toml` must resolve to a directory"
- "this generated file must embed the source file's current `sha256` digest"
- "the file → file reference graph is acyclic" / "domain code may not import
  from infra" / "every doc cross-link resolves"
- "no `.ls` may sit at a deprecated layout path"

Today these go in Python under `utils/lint/rules/structural/`. That works,
but each new rule = new Python file with bespoke filesystem-walk logic.
Phase 2 replaces the structural Python with a declarative, configurable
engine — mirroring what Phase 1 did for pattern rules.

### 6.2 Prior art surveyed for structural linting

Same methodology as §1. The candidates:

| Tool | Mechanism | Verdict |
|------|-----------|---------|
| **DIY Python/shell** (status quo) | filesystem walk + ad-hoc logic | Works; doesn't scale — each rule a new script. |
| **ls-lint** | Go binary; regex over file/dir names | Too narrow — naming only; no sibling/cross-file/graph. |
| **Repolinter** (Google) | Node; repo structure linter | **Archived 2026-02.** alint's `oss-baseline` is its strict superset. Don't adopt. |
| **conftest + OPA / Rego** | Rego policy engine | Powerful but heavy: requires learning Rego, verbose configs. Overkill for "X.ls must have X.txt". |
| **dependency-cruiser / arch-go** | JS-only / Go-only | Don't fit our polyglot stack. |
| **alint** | Single static Rust binary; declarative YAML; 89 rule kinds across 13 families | **Chosen.** |

### 6.3 Why alint

The deciding factors mirror why ast-grep won Phase 1:

1. **Same operational profile as ast-grep.** Single Rust binary, brew-installable
   (`brew tap asamarts/alint && brew install alint`), telemetry-free, reproducible
   build. No new runtime dependency we don't already have.
2. **The exact rule kinds we need are first-class:**
   - `pair` (X has companion Y) — replaces `check_ls_golden.py` in 5 lines
   - `cross_file` with `relations: equals|subset|superset|set_equals|identical|resolves` —
     covers manifest-paths-exist, version-coherence, byte-identical mirrors
   - `file_graph` with `acyclic | no_dangling | no_orphans | forbidden_edges | fresh` —
     unlocks layering firewalls and codegen-freshness checks we can't write today
   - `file_exists` / `file_absent` / `dir_exists` / `dir_absent` — preventive infra
3. **Agent-aware output.** Native `--format=agent` JSON with `agent_instruction`
   per violation; also `json` (stable schema), `sarif`, `github`, `markdown`,
   `junit`, `gitlab`. Slots into the `Report_NNN.{md,json,tsv}` workflow with
   zero translation effort.
4. **Bundled rulesets** (`oss-baseline`, `python`, `node`, `rust`,
   `ci/github-actions`, …) auto-gate on ecosystem facts — listing one for an
   ecosystem we don't have is a silent no-op. Useful in a polyglot tree.
5. **Performance.** ~1.1 s on a 100K-file workspace (public benchmarks);
   our tree is ~5K files.
6. **Active.** Updated within the week, v0.13.0, Apache-2/MIT, 30 OSS case
   studies under `examples/`.

clang-tidy stays the orthogonal *type-aware* gate (one int-cast upgrade in §4.3);
ast-grep is the *pattern* gate; alint becomes the *structural* gate. Three
complementary engines, one runner.

### 6.4 Integration into the existing pipeline

alint slots in next to ast-grep as a second backend. Severity gating,
suppression, reports, the `make lint` entry point — all extend cleanly.

**Architecture (mirrors what Phase 1 shipped):**

```
                    ┌──────────────────────┐
                    │  utils/lint/run.sh   │
                    └──────┬─────────┬─────┘
        pattern rules      │         │      structural rules
                ┌──────────┘         └──────────┐
                ▼                               ▼
    ┌──────────────────────┐       ┌──────────────────────┐
    │ ast-grep             │       │ alint                │
    │ scan --json=stream   │       │ check --format=agent │
    └──────────────────────┘       └──────────────────────┘
                │                               │
                └───────────────┬───────────────┘
                                ▼   unified jq pipeline
                ┌────────────────────────────────────────┐
                │  suppression filter + severity gate +  │
                │  Report_NNN.{md, json, tsv}            │
                └────────────────────────────────────────┘
                                │
                                ▼ (residual)
                       ┌───────────────────────┐
                       │ structural Python:    │
                       │ check_state_machine   │
                       │ (enum→table coverage) │
                       └───────────────────────┘
```

**Required changes to `run.sh`:**

- Add an `alint check --format=agent` pass after the ast-grep pass
- A small `jq` filter normalises alint's JSON records into the same shape
  ast-grep produces (`rule_id`, `severity`, `file`, `line`, `message`, `text`,
  `source_line`). The two backends differ in a handful of field names but the
  records are structurally similar.
- Severity mapping is direct: alint uses `level: error|warning|info|hint`,
  which lines up with the existing error-gates-build / warning-surfaces-in-
  report semantics.
- Suppression. ast-grep currently uses inline `// <SUPP>` markers driven by
  rule `metadata.suppress`. alint has its own config-level
  `paths_exclude` / per-rule `ignore`. We apply *both*: any alint finding whose
  source line carries the rule's declared marker is also dropped in `run.sh` —
  same Option-B suppression policy as §3.3, just applied uniformly across
  backends.
- Reports gain one extra field per finding: `backend: ast-grep | alint`. Agents
  reading `Report_NNN.{json,tsv}` can filter on it; humans reading the markdown
  see findings grouped per backend.
- `--rule <regex>` continues to work — it now matches against both backends'
  rule ids. `--list` shows ast-grep rules, alint rules, and the residual
  structural Python entries.

**Changes to `STRUCTURAL_CHECKS`:**

- Delete `ls-test-has-golden` (Python) — replaced by the alint `pair` rule
- Keep `check_state_machine.py` — it parses C++ enum headers and a C++ table
  source and asserts every enum value has a matching row. Neither ast-grep
  (it'd need cross-file source correlation) nor alint (it'd need to *parse*
  C++) can express it; it stays Python.

### 6.5 Should we unify the two rule configs?

The instinct is yes. Three concrete designs, with tradeoffs:

#### Option 1 — Native formats + shared runner (no unification)

Status quo extended. ast-grep rules stay under `utils/lint/rules/c-cpp/*.yml`;
alint rules go in `.alint.yml` at repo root (alint's standard convention).
`run.sh` dispatches both with a unified CLI; reports merge findings.

- **Pros**: each tool keeps its native format → full editor / LSP / autocomplete /
  doc support; zero codegen step; easy to debug; `alint init` and ast-grep's
  `--inline-rules` work out of the box.
- **Cons**: two YAML schemas to learn; no single "all our rules" registry beyond
  `run.sh --list`.

#### Option 2 — Shared rule manifest (small unification, recommended)

Keep each backend's native rule format (Option 1's wins), but add
`utils/lint/manifest.yml` — a thin index keyed by rule id with cross-cutting
metadata:

```yaml
version: 1
rules:
  no-raw-alloc:
    backend: ast-grep
    file: rules/c-cpp/no-raw-alloc.yml
    category: memory-safety
    enforces: claude-md/rule-1
    tags: [memory, mandatory]

  ls-test-has-golden:
    backend: alint
    file: .alint.yml#ls-test-has-golden
    category: test-hygiene
    enforces: claude-md/rule-8
    tags: [test, structural]
```

- **Pros**: single audit-able index of every rule with cross-cutting metadata
  (category, claude-rule reference, ownership, tags). Cheap to build and maintain.
  No codegen. A small CI smoke-check enforces `manifest entries == native files`.
- **Cons**: tiny duplication risk between manifest and native files (closed by
  the smoke-check); one extra file to touch when adding a rule.

#### Option 3 — Unified rule source → codegen (full unification)

Single source `utils/lint/rules.yml`. Each entry has shared metadata plus a
`backend:` selector and a `body:` blob passed verbatim to that backend's native
format. A `make lint-regen` step writes `utils/lint/_generated/ast-grep/<id>.yml`
and `utils/lint/_generated/.alint.yml`.

```yaml
version: 1
rules:
  - id: no-raw-alloc
    backend: ast-grep
    severity: error
    message: "Raw allocation call. Use mem_alloc/mem_free…"
    suppress: RAWALLOC_OK
    files: ["lambda/**/*.{c,cpp,h,hpp}", "radiant/**/*.{c,cpp,h,hpp}"]
    ignores: ["**/tree-sitter*/**"]
    body:                                # verbatim to ast-grep
      language: cpp
      rule:
        any:
          - pattern: malloc($$$A)
          - pattern: free($$$A)
          # …

  - id: ls-test-has-golden
    backend: alint
    severity: warning
    message: "Every test/lambda/**/*.ls must have a sibling *.txt"
    body:                                # verbatim to alint
      kind: pair
      primary: "test/lambda/**/*.ls"
      partner: "{dir}/{stem}.txt"
```

- **Pros**: one place for every rule; enforced shared metadata schema; can
  validate ("every rule has a message", "no two rules share an id"); cross-rule
  indexes (category, tags) live in the same file.
- **Cons**: editing flow adds a regen step (edit → regen → debug); each tool's
  editor LSP / autocomplete is lost on the unified file; the `body:` blob is
  opaque (you still need to know each tool's native syntax — the "unification"
  is mostly wrapping the metadata, not the rule logic); generated files in
  version control add PR-diff noise.

**Recommendation: Option 2.** It captures the unification value — a single
audit-able index, cross-cutting metadata — without giving up the native-format
benefits (LSP, autocomplete, debuggability, zero codegen). Option 3 is
technically tidier but the daily-edit cost outweighs the benefit: the two
backends' rule bodies are syntactically and semantically disjoint, so unifying
them is mostly wrapping fields, not real abstraction. If the rule corpus grows
past ~50 and manifest drift becomes a real problem, Option 3 is a logical next
step — but at our current ~30 rules, Option 2 is the right level of investment.

A pragmatic refinement we should adopt either way: `run.sh --list` already
discovers ast-grep rules from disk; extend it to read alint's bundled `list`
subcommand and the manifest, so the single source of truth for "what rules
do we have" remains a runtime aggregation, not a hand-maintained list.

### 6.6 Migration plan (parity-first, mirrors §3.6)

1. **Stand up infra.** `brew install alint`; pin version in CLAUDE.md and
   the runner; add `.alint.yml` at repo root with just the `ls-test-has-golden`
   `pair` rule.
2. **Wire `run.sh`.** Add the `alint check --format=agent` pass; jq-translate
   to the unified record shape; extend report JSON with the `backend:` field.
3. **Shadow-mode.** Run the alint rule and `check_ls_golden.py` side by side;
   diff their missing-golden lists; investigate any deltas (likely none —
   exclusions in the Python script port directly to alint `paths_exclude`).
4. **Cut over.** Once parity holds, delete `check_ls_golden.py` and its
   `STRUCTURAL_CHECKS` entry.
5. **Add the manifest (Option 2).** `utils/lint/manifest.yml` with one entry
   per existing rule; extend `--list` to read it; add a CI smoke-check
   asserting `manifest ids == discovered rule ids`.
6. **Author new structural rules.** Each becomes one YAML stanza in
   `.alint.yml` — e.g. a `pair_hash` rule that pins generated parser artefacts
   to their `grammar.js` source, or a `file_graph: { require: forbidden_edges }`
   rule asserting `lambda/cli/**` doesn't reach into `radiant/internal/**`.

§4.5 cross-language rules (`ls-deprecated-syntax`, `ls-banned-builtin`) are
independent — they live on the ast-grep side, since we wired tree-sitter-lambda
in Phase 1.

---

## 7. Summary

**Phase 1 — ast-grep pattern engine** (shipped):

- **Adopted ast-grep** as the single engine for project-specific *pattern*
  policies, driven by per-rule YAML under `utils/lint/rules/c-cpp/` and a
  bash+jq runner (`utils/lint/run.sh`) that also fronts the structural checks
  and aggregates results into one `make lint`.
- It wins over `grep` (code-aware → kills the allowlist tax), over Semgrep
  (lighter binary, safer on parse errors), and over clang-tidy (multi-language;
  clang-tidy stays a complementary *semantic* gate, ideal for the one type-aware
  int-cast upgrade).
- **Kept** cppcheck / clang-tidy / clang-analyzer as orthogonal gates, and kept
  `check-state-machine` as code — pattern tools cannot express coverage
  invariants.
- Migration was parity-first (shadow-diff each rule, repoint the legacy target,
  delete the shell block).
- Payoff: cheap new rules let us **enforce CLAUDE.md rules 2/3/4 in CI** when
  CI integration lands, and a registered `tree-sitter-lambda` grammar brings
  the 3,000-script `.ls` corpus under the same linter.

**Phase 2 — alint structural engine** (proposed, §6):

- **Adopt alint** as the *second* engine, alongside ast-grep, for the rule
  classes ast-grep fundamentally cannot reach: sibling-file existence,
  cross-file value relations, file-graph properties (acyclicity, layering
  firewalls, doc-link integrity, codegen-freshness).
- Same operational profile as ast-grep — single static Rust binary,
  brew-installable, declarative YAML, agent-aware output format — so it slots
  into `run.sh`'s existing severity gate / suppression / `Report_NNN.*` plumbing
  with minimal change.
- Replaces `check_ls_golden.py` (the only Python script that's a filesystem
  shape check); keeps `check_state_machine.py` (the only Python script that
  parses C++ source to correlate enum tables — neither pattern nor structural
  in nature).
- Recommend **Option 2 (shared manifest)** for cross-engine unification:
  native YAML formats stay (full LSP/autocomplete/doc support), plus a thin
  `utils/lint/manifest.yml` index keyed by rule id with cross-cutting metadata
  (category, claude-rule reference, tags, ownership). Avoids a codegen step
  and its daily-edit friction.
- Migration parity-first (shadow-diff `ls-test-has-golden` against the existing
  Python before deleting).
