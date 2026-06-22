# Lambda Lint — A Unified Project-Policy Linter

## Status

Three phases are now shipped end-to-end. Rollout status of individual rules
is in §5; per-phase design and tradeoffs in §3 (Phase 1), §6 (Phase 2), §7
(Phase 3).

| Area | Status |
|------|--------|
| Runner | Shipped as `utils/lint/run.sh` (bash + jq, not the originally-proposed Python). |
| sgconfig + rule layout | Shipped at repo root + `utils/lint/rules/c-cpp/*.yml` + `utils/lint/rules/lambda/*.yml`. |
| Suppression | Option B shipped — semantic `*_OK` markers (`metadata.suppress` per rule). |
| 10 legacy `check-*` targets | Replaced by ast-grep rules; legacy targets deleted. |
| `check-state-machine` | Kept as Python; dispatched by `run.sh` via `STRUCTURAL_CHECKS`. |
| Make integration | Single entry point: `make lint` (formerly `lint-policy`). |
| Reports | Shipped beyond proposal — `make lint ARGS=--report` writes `test_output/lint/Report_NNN.{md,json,tsv}` with per-finding `backend:` tag for AI-agent batch fix workflows. |
| `.ls` custom-language | Wired in `sgconfig.yml`; first `.ls` rule (`ls-todo-inventory`) shipped. |
| Cross-engine manifest | Shipped — `utils/lint/manifest.yml` indexes all 35 rules with category/tags/CLAUDE-rule refs; `manifest_check.sh` smoke-check enforces sync. |
| CI integration | **Deferred — manual invocation only for now.** See §3.5. |
| §4 future rules | Tier-1 (high-confidence) shipped; broad rules shipped at `warning`; cross-language rules deferred pending user spec. See §5. |
| **Phase 2 — alint structural engine** | **Shipped (§6).** `ls-test-has-golden` (CLAUDE rule 8) ports cleanly; manifest unification (Option 2) shipped. |
| **Phase 3 — clang-tidy + libclang type-aware engine** | **Shipped (§7).** `int-cast-type-aware-decl` via tuned `bugprone-narrowing-conversions`, `int-cast-type-aware` via libclang AST walk. Hybrid: built-in for the cheap case, Python for the custom case. |
| **Phase 4 — Retire cppcheck; hybrid unused-function check** | **Proposed (§8).** cppcheck's unique value reduces to its `unusedFunction` check. Replicate via ast-grep candidate set → ast-grep heuristic exclusions → libclang USR verification on the residue, then delete `make lint-cppcheck`. Tier placement (lint vs. lint-full) gated on measured runtime. |

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

### 3.8 Linting Lambda `.ls` — shipped

ast-grep supports **custom languages**: point `sgconfig.yml` at a compiled
tree-sitter grammar and its file extensions. The `tree-sitter-lambda` dylib is
built by the existing toolchain; registering it lets the same engine match
patterns over the 3,000-script `.ls` corpus.

Shipped wiring (commit history):

```yaml
# sgconfig.yml
customLanguages:
  lambda:
    libraryPath: lambda/tree-sitter-lambda/libtree-sitter-lambda.dylib
    extensions: [ls]
    expandoChar: $
```

First `.ls` rule shipped: [`ls-todo-inventory`](../utils/lint/rules/lambda/ls-todo-inventory.yml)
(TODO/FIXME inventory across `.ls` scripts). Validates the wiring end-to-end —
ast-grep correctly excludes a TODO inside a string literal that a raw grep
would have flagged, confirming the tree-sitter parse is real.

Same recipe applies to lambda/js source via `tree-sitter-javascript` whenever
js-engine-policy rules are spec'd (§4.5).

### 3.9 ast-grep quirks discovered during implementation

These bit me while authoring rules. None are blockers, but each costs an hour
to discover from scratch — recording them so the next author doesn't.

**1. `not:` blocks silently drop `constraints:`.** Constraints are declared at
the rule-config top level (sibling to `rule:`) and apply globally to the named
metavars. When the same metavar appears inside a `not:` block, its constraint
is **not** applied — the `not:` block matches as if the constraint were absent.

```yaml
# WRONG — looks like "match alloca($X) but not literal-arg alloca", but the
# `not:` actually matches every alloca call, cancelling the outer match.
constraints:
  N: { kind: number_literal }
rule:
  all:
    - pattern: alloca($X)
    - not:
        pattern: alloca($N)
```

Workaround: **invert the logic** from "match everything minus safe shapes" to
"positively enumerate bad shapes", using distinct metavar names per pattern
in `any:` so each gets its own top-level constraint:

```yaml
constraints:
  N1: { regex: "^[a-z][a-zA-Z0-9_]*$" }
  N2: { regex: "^[a-z][a-zA-Z0-9_]*$" }
  X:  { regex: "^[a-z][a-zA-Z0-9_]*$" }
rule:
  any:
    - pattern: alloca($N1 * sizeof($T))   # uses $N1
    - pattern: alloca(sizeof($T) * $N2)   # uses $N2
    - pattern: alloca($X)                 # uses $X
```

Real-world example: [`alloca-static-size`](../utils/lint/rules/c-cpp/alloca-static-size.yml)
uses this inversion. Practical consequence: rules can't easily express
"everything except these shapes" — they have to enumerate the targets.

**2. Standalone patterns can parse as declarations, not calls.** In C++,
`isspace(*p)` and `alloca($N * $M)` as standalone fragments parse as
function-style declarations (parameter list with `*p` as a pointer
declarator). Constraints over them fire on the wrong nodes, or fire on
nothing.

Workaround: wrap in a `context:` that forces the call form, then `selector:`
the inner `call_expression`:

```yaml
- pattern:
    context: "void f() { isspace(*$p); }"
    selector: call_expression
```

Used by [`unsafe-string-scan`](../utils/lint/rules/c-cpp/unsafe-string-scan.yml)
and [`alloca-static-size`](../utils/lint/rules/c-cpp/alloca-static-size.yml).
Without it, the rule misses anywhere from a third to most of the real hits.

**3. `constraints:` is not a per-pattern field inside `any:`/`all:`.** Putting
`constraints:` on a single item inside an `any:` list throws
`unknown field 'constraints', expected one of pattern, kind, regex, ...`.
Constraints are rule-config-level only — use the distinct-metavar-name pattern
from quirk 1 to address different patterns' vars independently.

These are tree-sitter-grammar / rule-engine limitations, not bugs in our use of
ast-grep. They are surfaced here so they don't get rediscovered each time —
hour-cost is high enough that the workarounds belong in the design doc, not
just in commit messages.

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
| `checked-pool-alloc` | deref of `pool_calloc`/`arena_alloc` result with no NULL check | **blocked on allocator contract**: pool/arena allocators must first be centralized under the Memory Context so failure mode (return NULL vs. abort) is well-defined; only then can the rule decide what counts as "missing check". Also needs clang static analyzer (dataflow). |

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

### 5.1 Shipped (32 ast-grep rules + 1 alint + 2 clang-tidy + 1 structural Python = 36 total)

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
| `no-int-layout-decl` | warning | 1 | §4.3 | Pattern-only sibling of `no-int-cast-radiant`; precise version still belongs to clang-tidy |
| `alloca-static-size` | warning | 57 | §4.2 (added) | `alloca()` with non-static size — stack-overflow risk. Syntactic heuristic; misses cast-prefixed runtime args (false negatives, not positives). Uses the §3.9 quirks workarounds |
| `ls-todo-inventory` | info | 4 | §4.5 | First rule against the registered tree-sitter-lambda grammar — validates §3.8 wiring |
| `ls-test-has-golden` (alint) | warning | 133 | §4.1 | CLAUDE.md rule 8; structural (sibling-file) rule on the alint backend, not ast-grep |
| `int-cast-type-aware-decl` (clang-tidy) | warning | 36 | §4.3, §7 | Built-in `bugprone-narrowing-conversions` tuned to float→int only. Catches `int x = float_expr` precisely (no allowlist). |
| `int-cast-type-aware` (libclang) | warning | 0 | §4.3, §7 | Walks Clang AST for explicit casts (`(int)x`, `static_cast<int>(x)`) where the source is float-typed. Preventive — all existing layout-file casts are already `INT_CAST_OK`-marked. |

### 5.2 Outstanding

Cross-language rules (require user specification of *what* to ban):

| Rule | Status | Blocker |
|------|--------|---------|
| `ls-deprecated-syntax` | Infra ready (custom-lang wired; `ls-todo-inventory` validates the path) | Need list of retired `.ls` constructs from the user |
| `ls-banned-builtin` | Infra ready | Need list of deprecated/unsafe `.ls` builtins from the user |
| `js-engine-policy` | Not started | Need invariant catalog for LambdaJS internals from the user |

Type-aware rules (require clang-tidy, not ast-grep):

| Rule | Status | Notes |
|------|--------|-------|
| `int-cast-type-aware` | **Shipped (§7)** | libclang Python check; 0 findings today because all existing explicit casts are `INT_CAST_OK`-marked. Empirical test on `layout_block.cpp`: ~60% of those markers cover non-float operands and can be deleted in a cleanup pass. |
| `int-cast-type-aware-decl` | **Shipped (§7)** | Built-in `bugprone-narrowing-conversions` tuned to float→int only. 36 findings — supersedes the pattern-only `no-int-layout-decl` for layout files. |
| `checked-pool-alloc` | **Deferred — blocked upstream** | Two preconditions must land first: (1) pool/arena allocators centralized under the **Memory Context** project, so the allocator-failure contract (return NULL vs. abort/longjmp) is project-wide and rule-decidable; (2) clang **static analyzer** (dataflow over the CFG), since the rule isn't an AST shape — it's "is every alloc result NULL-checked before the first deref on every path?". Different machinery than Phase 3's `bugprone-*` + libclang; lives on the existing `analyze:` target's side of the wall. Re-evaluate once Memory Context lands. |

Medium-effort pattern rules that proved subjective or noisy and were deferred
after surveying the codebase:

| Rule | Surveyed | Verdict |
|------|---------:|---------|
| `c-style-cast-in-cpp` | 28,772 raw hits | Too noisy without type info to distinguish `(void)x` discards from real downcasts. Belongs to clang-tidy. |
| `log-prefix-convention` | 66% already conform | "Distinct prefix" is subjective — no false-positive-free regex exists at the heuristic level we need. |
| `require-itemnull-on-error` | n/a | Not a node pattern — needs return-context analysis (structural class). Would go on the alint side if expressible there, otherwise Python. |

Infrastructure deferred (explicit, not blocked):

| Item | Section | Note |
|------|---------|------|
| CI integration | §3.5 | `make lint` is manual-only today; will pin ast-grep version and wire `.github/workflows/*.yml` when adopted |
| Parse-error diagnostic surfacing | §3.7 | `run.sh` does not currently warn on new ast-grep parse errors. Tail-risk mitigation; add when the rule corpus is large enough that silent drift becomes plausible |
| Native `// ast-grep-ignore` markers | §3.3 Option A | Status quo (Option B / semantic `*_OK` markers) is working; tool-native form is a reversible later move |
| §3.6.4 grep backstop | §3.6 | Explicitly skipped — parity-confidence was high enough to not need it |

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

## 7. Phase 3 — Type-aware rules via clang-tidy + libclang

### 7.1 Why a third engine

ast-grep does node patterns over file contents. alint does filesystem shape
and cross-file relations. Both are **type-blind** — they see `view->width` as a
member-access expression with an identifier `width`, with no idea whether
`width` is `float`, `int`, `size_t`, or an enum.

Several rules in §4 are blocked exclusively on this:

- `int-cast-type-aware` (§4.3) — flag `(int)$X` only when `$X`'s actual type
  is `float`/`double`. The pattern-only `no-int-cast-radiant` flags every
  `(int)` cast and requires ~284 `// INT_CAST_OK` markers project-wide for
  legitimate cases (string length, pointer diff, enum, repeat count).
- `int-cast-type-aware-decl` (§4.3) — flag `int x = expr` when `expr`'s
  resolved type is `float`/`double`. The pattern-only `no-int-layout-decl`
  approximates with a hand-maintained allowlist of ~30 field names, missing
  any local arithmetic where the result type is float without an obvious
  field name.

§1 surveyed clang-tidy and called it "best for *type-dependent* rules". Phase 3
is where that prediction comes due — not as a replacement for ast-grep/alint,
but as a **third backend** for the rules they fundamentally can't reach.

### 7.2 Approaches considered for type-aware rules

We're already running clang-tidy via the existing `tidy:` Makefile target. The
question wasn't "use clang-tidy or not" — it was *how* to extend it with
project-specific checks. Three concrete paths:

| Approach | What you write | Build chain | Distribution |
|---|---|---|---|
| **clang-tidy plugin** loaded via `-load=./check.dylib` | ~50 lines C++ subclassing `ClangTidyCheck`, MatchFinder DSL | CMake against `/opt/homebrew/opt/llvm`; must match clang-tidy's LLVM ABI version | Platform-specific .dylib; rebuild on every LLVM upgrade |
| **Patch + rebuild upstream clang-tidy** | Same C++, landed in `llvm-project/clang-tools-extra` | Full LLVM build from source | Replace system clang-tidy |
| **External libclang tool** (Python or C++) | ~150 lines Python using `clang.cindex` | None — `pip install libclang` in a venv | Cross-platform; libclang has a stable C ABI |

All three give the same matching power because they all walk the same Clang
AST. The choice is about **build/maintain cost** vs. **native integration**
into clang-tidy's output / `--fix` / `// NOLINT` machinery.

### 7.3 Why the hybrid we shipped (built-in + libclang)

The two rules have very different cost profiles, so they get different
implementations:

**`int-cast-type-aware-decl`** — uses **built-in clang-tidy**. Specifically,
`bugprone-narrowing-conversions` already flags implicit narrowing on
initialization. We just tune its options to fire only on float/double→int
(the truncation case), not on integer-narrowing (e.g. `int x = long_y;`,
which the codebase legitimately does for pixel buffers) or
integer-to-float precision loss (which is irrelevant to the int-cast rule
class).

Config (in `utils/lint/tidy/run_tidy.sh`, passed via `--config=`):

```yaml
{Checks: "-*,bugprone-narrowing-conversions", CheckOptions: [
  {key: bugprone-narrowing-conversions.WarnOnFloatingPointNarrowingConversion,         value: "1"},
  {key: bugprone-narrowing-conversions.WarnOnIntegerNarrowingConversion,               value: "0"},
  {key: bugprone-narrowing-conversions.WarnOnIntegerToFloatingPointNarrowingConversion, value: "0"}
]}
```

We additionally pipe-filter the output to `to 'int'` destinations — the
config can suppress float-source narrowing TO float (e.g. `double → float`,
not what we want), but only by source-type category, not by destination.
The pipe-filter is one line.

**`int-cast-type-aware`** — uses **libclang Python**. `bugprone-narrowing-conversions`
deliberately *skips* explicit C-style and `static_cast<>` conversions:

> Explicit conversions are not narrowing because they reflect the user's
> deliberate intent.

For our codebase that "deliberate intent" is *exactly the bug we suspect*.
No built-in check fires here, so we wrote `utils/lint/tidy/explicit_cast_check.py`:

- Parse each file with `clang.cindex.Index.parse(file, args=COMPILE_ARGS)`
- Walk the AST for `CSTYLE_CAST_EXPR` / `CXX_STATIC_CAST_EXPR` cursors
- For each, descend through implicit-conversion wrapper nodes (libclang
  reports the immediate child of a cast with the *destination* type, not
  the source — the real source type sits one or more `UNEXPOSED_EXPR`
  wrappers deeper)
- If destination is `int`-like and source is `float`/`double`, emit a record
- Honor the existing `// INT_CAST_OK` markers — same suppression scheme as
  every other rule in the stack

Three reasons libclang Python beat the plugin path here, given where we are
in the stack:

1. **We already own the suppression and output formats.** Native clang-tidy
   gives you `// NOLINT(check-name)` and a fixed diagnostic format. We use
   `// INT_CAST_OK: <reason>` markers (consistent with `RAWALLOC_OK`,
   `RETAINED_FIELD_OK`, `STATE_STORE_OK`, …) and unified NDJSON via
   `run.sh` → `Report_NNN.{md,json,tsv}`. Going native means either losing
   convention consistency or building translation glue.
2. **No C++ build chain enters the lint pipeline.** ast-grep is a Rust
   binary, alint is a Rust binary, clang-tidy is already installed. Adding
   a libclang Python tool keeps the pipeline at "scripts that call binaries".
   The plugin path adds CMake, a .dylib artefact, and an LLVM dev-headers
   build dependency. Ongoing maintenance cost.
3. **libclang's C ABI is stable across LLVM versions.** A clang-tidy plugin
   must be ABI-compatible with the *exact* clang-tidy binary loading it —
   `brew upgrade llvm` can break the plugin with cryptic errors. libclang
   Python keeps working at any LLVM version.

The plugin would be the right call later if we need (a) `--fix` rewrites,
(b) editor/LSP integration, or (c) a corpus of >5 type-aware checks where
the ASTMatcher DSL pays off vs. Python AST walks. We're at 1; libclang wins
on amortized cost today.

### 7.4 Integration into the existing pipeline

Slots in as a third backend next to ast-grep and alint. The existing
severity gate / suppression filter / `Report_NNN.*` generation extend
without further change.

```
ast-grep   ──┐
alint      ──┼─► unified NDJSON ─► severity + suppression ─► Report_NNN.{md,json,tsv}
clang-tidy ──┘   (backend: tag preserved per finding)
```

`utils/lint/tidy/run_tidy.sh` orchestrates the two sub-tools, scoped to the
same 19-file layout set `no-int-cast-radiant` covers:

1. `clang-tidy --config=... --checks='-*,bugprone-narrowing-conversions'`
   on each file → text output `file:line:col: warning: ...` → awk normalises
   to unified NDJSON with `ruleId: int-cast-type-aware-decl`, `backend: clang-tidy`
2. `python3 explicit_cast_check.py` on the same files → NDJSON with
   `ruleId: int-cast-type-aware`, `backend: clang-tidy` directly

`run.sh` runs the wrapper, concatenates onto the ast-grep + alint stream,
and the rest of the pipeline is unchanged. `--rule <regex>` filters across
all three backends; `--list` shows all three groups; the report's
`counts_by_backend` block reflects the split.

### 7.5 Empirical payoff (cleanup opportunity)

Strip test on `radiant/layout_block.cpp`: temporarily remove every
`// INT_CAST_OK` marker, re-run `int-cast-type-aware`. Result: of the 5
markers in that file, only **2 cover real float→int casts**; the other 3
are over the existing pattern rule's false-positive class (pointer diff,
integer width, etc.) and can be safely deleted.

Extrapolating to the **115 markers across the layout corpus** (the rule's
file scope), the type-aware version implies **65-70 markers can be removed**.
Per the standing "no need to fix in this session" guidance from earlier in
the project, the deletion sweep is left as a future agent task; Report_005
contains the per-marker data an agent can act on.

The deeper claim from §4.3 — "retire most of the 284 `INT_CAST_OK` markers"
— is the *project-wide* number across all 19+ files where `no-int-cast-radiant`
runs. Phase 3's scope today is just the layout subset; expanding the
`run_tidy.sh` `FILES=(…)` array to the broader scope is a one-line change
once we want to walk all 284.

### 7.6 Migration plan (forward-looking)

Phase 3 is shipped; this is the plan for the cleanup sweep + follow-up
investments, not the engine itself.

1. **Cleanup sweep (agent task).** Run `make lint ARGS='--rule ^int-cast-type-aware'`,
   walk the per-file findings, delete `INT_CAST_OK` markers on lines NOT in
   the finding set. Verify by re-running the rule (count should stay 0).
2. **Expand `run_tidy.sh` scope** to all 19+ files in
   `no-int-cast-radiant`'s coverage — the layout subset was the smallest
   useful scope to ship; the broader sweep multiplies the marker-deletion
   payoff.
3. **Consider clang-tidy plugin** if and when (a) `--fix` rewrites become
   useful (auto-suggest `roundf()` instead of `(int)`), (b) editors that
   drive clang-tidy directly should see our checks inline, or (c) more
   than ~5 type-aware rules accumulate. Until any of those, the libclang
   Python tool is the right level of investment.
4. **`checked-pool-alloc`** stays deferred (§5.2). Two blockers:
   - **Upstream — allocator contract.** Pool/arena allocators are being
     centralized under the in-flight **Memory Context** project (registry +
     factory). Until that lands, the failure mode of `pool_calloc` /
     `arena_alloc` / friends isn't uniform (some abort, some return NULL,
     some longjmp out via an error path). A "did you NULL-check this?" rule
     can't be written before that contract is project-wide — what to check
     for depends on what the allocator can do.
   - **Tooling — analyzer not matcher.** This is dataflow over the CFG ("is
     every alloc result NULL-checked before the first deref on every
     path?"), not a single-node AST shape. That's the Clang static
     analyzer's territory (existing `analyze:` Makefile target), not
     bugprone-style matchers or libclang AST walks.
   Re-evaluate once Memory Context lands; until then the rule is documented,
   not built.

---

## 8. Phase 4 — Retire cppcheck; hybrid unused-function check

### 8.1 Why this phase

cppcheck is the last *separate* lint binary in the toolchain after Phase 3 —
not part of `make lint` or `make lint-full`, scoped only to `lambda/`,
never failing the build, produces its own XML report under
`analysis-results-lint/`. Its position is the same `make tidy` was in before
Phase 3 retired that target: a developer-reference tool, not a gate.

The path forward is one of three:

1. **Integrate cppcheck into `make lint`** — wire its XML output into the
   unified NDJSON pipeline, add a `backend: cppcheck` family. The cost would
   be similar to Phase 3's clang-tidy wiring; the benefit is bounded by
   what cppcheck catches that the existing stack doesn't.
2. **Keep cppcheck as-is** — leave it for developers who occasionally run it.
   No new code, but a permanently-fragmented "what does `make lint` mean?"
3. **Retire cppcheck entirely**, replicating its one unique capability
   (`unusedFunction`) on the existing stack.

§8.2 shows that nearly all of cppcheck's catches overlap with the
clang-tidy `bugprone-*` / `clang-analyzer-*` families we already ship in
Phase 3 — the only meaningful gap is dead-function detection. Path (3) is
therefore the cleanest move: one new rule, one fewer toolchain dependency,
one less artefact directory.

### 8.2 cppcheck overlap audit

Mapping cppcheck's check families to what's already in `make lint-full`:

| cppcheck family | Example check | Already covered by | Notes |
|---|---|---|---|
| `error` | NULL deref, OOB array access, uninitialized read, double-free, leak | `tidy-clang-analyzer-core.*` | clang-analyzer is path-sensitive; cppcheck's value flow is shallower. Net: redundant. |
| `warning` | Suspicious assignment, uninitialized var, comparison-vs-NULL after deref | `tidy-bugprone-*` + `tidy-clang-analyzer-*` | Significant overlap. Net: redundant. |
| `style` | Redundant casts, redundant `else`, missing `override` | clang-tidy `readability-*` (currently disabled — too noisy) | Both have the same volume problem; cppcheck doesn't escape it. |
| `performance` | `strlen` in loop bound, pass-by-value of big types | `tidy-bugprone-*` partial; `performance-*` (disabled) | Marginal unique value. |
| `portability` | Implicit endianness, size-of-pointer assumptions | Not in our stack | Genuine gap — but the codebase is macOS/Linux focused, so the practical loss is small. |
| `unusedFunction` | Dead functions across the whole tree | **Not in our stack** | **Genuine unique value.** Neither ast-grep, alint, nor clang-tidy ships a whole-program dead-function check. |
| `missingInclude` | IWYU-style missing direct includes | `misc-include-cleaner` (disabled — too noisy) | Both have the noise problem. |

After Phase 3 wired clang-tidy's bug-finding pass project-wide, the audit
above is the proof that cppcheck's last load-bearing capability is exactly
one: `unusedFunction`. Replicate that and cppcheck becomes deletable.

### 8.3 The hybrid unused-function check

Whole-program dead-function detection has the same shape problem as
Phase 3's clang-tidy pass — *every* file has to be looked at to prove
nothing calls a given function. The naive solution (libclang AST walker
over the whole tree, two passes for definitions and references) costs the
same ~3-4 minutes as Phase 3's bug-finding pass. We can do significantly
better by exploiting an asymmetry: **ast-grep can cheaply prove "the name
appears nowhere," but it cannot prove "the name is called"** (overloads,
templates, macro-synthesized dispatch all confound it). The hybrid uses
ast-grep for the cheap negative-proof step and falls back to libclang
*only* for the residue.

**Stage 1 — ast-grep candidate gathering** (~5 s):

```bash
# All function definition names
ast-grep ... 'kind: function_definition, has: { kind: identifier, $NAME }'
  → /tmp/defs.txt

# All references: calls, address-of, initializer-list bare identifiers,
# function-pointer-arg bare identifiers
ast-grep ... 'any: [call_expression with $NAME; &$NAME;
                    initializer_list with $NAME; argument_list with $NAME]'
  → /tmp/refs.txt

candidates = defs - refs                        # ~200-400 names
```

**Stage 2 — ast-grep heuristic exclusion** (~5 s). Each rule below knocks
out a class of false positives without any semantic analysis:

| Heuristic | Rule body | Filters out |
|---|---|---|
| Macro body contains the candidate's name | `kind: preproc_function_def, regex: $NAME` | Macro-synthesized calls |
| Bare identifier inside `{ ... }` initializer | `kind: initializer_list, has: $NAME` | Function-pointer tables |
| Definition has `__attribute__((used))` / `[[gnu::used]]` / `[[maybe_unused]]` | `kind: attribute, regex: 'used\|maybe_unused'` | Compiler-forced retention |
| Definition `virtual` / followed by `override` / `final` | `kind: function_declarator, has: 'override\|final'` | vtable-dispatched |
| Definition has `extern "C"` linkage | `kind: linkage_specification, regex: '"C"'` | External-linkage exports |
| Definition is inside a `TEST(...)` / `TEST_F(...)` macro expansion | `kind: expansion_macro_invocation` | GTest registration |
| Definition matches `operator<X>` pattern | `pattern: $RET operator $OP(...)` | Operator overloads (often implicit) |

Expected reduction: 200-400 candidates → **~20-50** survivors.

**Stage 3 — libclang USR verification** (~30-60 s, parallelized):

```python
# Pre-filter the file set: only parse files that even mention any
# candidate name as a literal substring. Cheap grep across the tree.
relevant_files = grep_files_containing_any(candidates)   # ~50-150 files

for c in candidates:
    if not any_use_found_via_libclang(c, relevant_files):
        emit_finding(c, severity="warning", rule_id="unused-function")
```

Per candidate the libclang walk looks for:

- `CallExpr` whose `cursor.referenced.spelling` matches
- `UnaryOperator(&, DeclRefExpr)` whose referenced matches
- `TemplateArgument` whose value matches (catches template-bound dispatch)
- `cursor.kind` in `{CXX_DEDUCED_AUTO_TYPE, CXX_FUNCTIONAL_CAST_EXPR, ...}` for implicit calls

Early-exit per candidate the moment any use is found. The grep pre-filter
caps libclang's file count at ≤150 instead of 600 — the asymmetric exploit
that makes the cost work.

**Expected runtime envelope**:

| Stage | Wall | Survivors |
|---|---:|---:|
| Stage 1 (gather) | ~5 s | ~300 |
| Stage 2 (heuristics) | ~5 s | ~30-50 |
| Stage 3 (libclang verify) | ~30-60 s | ~10-30 |
| **Total** | **~1 min** | actionable unused set |

That ~1 minute target informs the §8.4 tier-placement decision.

### 8.4 Performance-gated tier placement

Per project policy: `make lint` is the fast iterative gate (~8 s today);
`make lint-full` is the comprehensive pre-merge gate (~4 min today). Where
the unused-function check lands depends on its measured wall-clock once
Stage 3 is implemented:

| Measured runtime | Placement |
|---|---|
| **≤ 10 s** | `make lint` and `make lint-full` both. Fast enough that iteration speed isn't hurt. |
| **> 10 s** | `make lint-full` only. `make lint` stays sub-10-s. Auto-enables under `--rule ^unused-function` (same pattern as `tidy-*` rules in §7). |

The 10 s threshold is the cliff: anything that pushes `make lint` past 10 s
materially changes how developers use it (it stops being a pre-commit
reflex). The Stage 1+2 portion alone is well under 10 s and could in
principle run in `lint` while Stage 3 runs only in `lint-full`, but
splitting the check across tiers complicates the rule's meaning (does
"unused-function: 0 findings" mean "verified clean" or "no Stage-3-verified
clean"?). Better: ship as one indivisible check, place by total cost.

The choice is empirical — only made after Stage 3 is implemented and
benchmarked on the real codebase.

### 8.5 Integration & migration plan

Mirrors §7.4 plumbing. New scaffolding:

```
utils/lint/dead-code/
├── stage1_candidates.sh        # ast-grep defs minus refs
├── stage2_exclusions.sh        # 7 heuristic-exclusion ast-grep rules
├── stage3_verify.py            # libclang USR walk over pre-filtered file set
└── run_dead_code.sh            # orchestrator → unified NDJSON
                                # rule_id: "unused-function"
                                # backend: "hybrid"
                                # suppress marker: UNUSED_FUNCTION_OK
```

Migration steps:

1. **Implement Stage 1 + Stage 2.** Ship as standalone; measure how many
   candidates survive on the real codebase. If the count is already small
   enough that Stage 3 isn't useful (say <50), short-circuit Stage 3 and
   ship as ast-grep-only.
2. **Implement Stage 3.** Time the full pipeline.
3. **Decide tier placement** per §8.4's 10 s rule.
4. **Wire into `run.sh`** as the third clang-tidy-tier backend (next to
   `tidy-*` and `int-cast-type-aware`). Same severity gate, same
   suppression filter, same Report_NNN.* plumbing.
5. **Add manifest entry** — one rule `unused-function`, category
   `dead-code`, backend `hybrid`.
6. **Retire cppcheck:**
   - Delete the `lint-cppcheck:` Makefile target
   - Remove from `.PHONY` and `make help`
   - Delete `analysis-results-lint/` references
   - Remove cppcheck from any CI configuration (none today — it was never
     in CI gating to begin with)
7. **Document.** Update [doc/dev/Lambda_Lint_Rules.md](../doc/dev/Lambda_Lint_Rules.md)
   §5 with the new rule; remove cppcheck from §2 backends table; note in
   the Appendix that Phase 4 retired cppcheck.

The retirement is cheap precisely because cppcheck was never load-bearing
in CI — no migration of CI configurations, no findings backlog to
reconcile, no external consumers of `analysis-results-lint/`.

### 8.6 Honest limitations

The hybrid catches the bulk of mechanically-detectable dead functions. It
will *not* catch:

- **String-mediated dispatch**: `dlsym(handle, "foo")`, reflection-style
  lookups, plugin systems where the function name lives in a string
  literal at the call site. Same fundamental limitation as cppcheck.
- **Build-time-only dispatch**: codegen scripts, parser-generator outputs
  that produce call sites at build time but not at runtime.
- **Functions called only through template-template parameters** computed
  during compilation, where libclang's USR resolution can't follow the
  binding statically.

For any of these, the answer is the same as the rest of the lint stack:
the `UNUSED_FUNCTION_OK` inline marker. Short allowlist kept in source,
not in a hand-maintained config file.

What the hybrid *does* handle that pure ast-grep cannot: overloaded
functions where one signature is called and another isn't (USR
distinguishes them), implicit calls (constructors, destructors, conversion
operators that libclang surfaces), and template-instantiated functions
whose binding is statically resolvable. That's why Stage 3 exists at all.

---

## 9. Summary

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

**Phase 2 — alint structural engine** (shipped, §6):

- **Adopted alint** as the *second* engine, alongside ast-grep, for the rule
  classes ast-grep fundamentally cannot reach: sibling-file existence,
  cross-file value relations, file-graph properties (acyclicity, layering
  firewalls, doc-link integrity, codegen-freshness).
- Same operational profile as ast-grep — single static Rust binary,
  brew-installable, declarative YAML, agent-aware output format — slots into
  `run.sh`'s existing severity gate / suppression / `Report_NNN.*` plumbing
  with minimal change.
- Replaced `check_ls_golden.py` (the only Python script that's a filesystem
  shape check); kept `check_state_machine.py` (the only Python script that
  parses C++ source to correlate enum tables — neither pattern nor structural
  in nature).
- Shipped **Option 2 (shared manifest)** for cross-engine unification:
  native YAML formats stay (full LSP/autocomplete/doc support), plus a thin
  `utils/lint/manifest.yml` index keyed by rule id with cross-cutting metadata
  (category, claude-rule reference, tags, ownership). `manifest_check.sh`
  smoke-check enforces sync.
- Migration was parity-first (shadow-diff `ls-test-has-golden` against the
  existing Python; identical 133-file missing-golden set, exact set equality).

**Phase 3 — clang-tidy + libclang type-aware engine** (shipped, §7):

- **Added clang-tidy as a third backend** for the rules ast-grep and alint
  can't reach — the type-dependent ones. ast-grep matches AST shapes; alint
  reasons over filesystem structure; clang-tidy + libclang ask Sema "what's
  the actual type of this expression?"
- **Hybrid implementation**: `int-cast-type-aware-decl` uses built-in
  `bugprone-narrowing-conversions` (tuned to float→int only); `int-cast-type-aware`
  uses a small libclang Python tool because no built-in check fires on explicit
  casts by design. Chose libclang Python over a clang-tidy plugin because (a) we
  already own the suppression and output formats, (b) no C++ build chain enters
  the lint pipeline, (c) libclang's C ABI is stable across LLVM versions.
- **Project-wide bug-finding pass** added in the second wave: tuned subset of
  `bugprone-* + clang-analyzer-* + cert-*` (with 6 noisy checks disabled) across
  all `.cpp` under `lambda/` + `radiant/`. ~4 min wall-clock at 8-wide
  parallelism — split off into `make lint-full` so the iterative `make lint`
  (now ast-grep + alint only) stays sub-10-s.
- Empirically reclaims ~60% of `INT_CAST_OK` markers in the layout subset
  (per a strip test on `layout_block.cpp`). Same plumbing pattern as Phase 2:
  NDJSON normalisation in `run.sh`, `backend: clang-tidy` tag per finding.

**Phase 4 — Retire cppcheck; hybrid unused-function check** (proposed, §8):

- **Retire `lint-cppcheck`** as the last separate lint binary in the toolchain.
  An overlap audit (§8.2) shows nearly every cppcheck check family is already
  covered by Phase 3's `tidy-bugprone-*` / `tidy-clang-analyzer-*`; the only
  load-bearing unique capability is whole-program `unusedFunction`.
- **Replace `unusedFunction` with a hybrid checker** (§8.3): ast-grep gathers
  the candidate set (functions whose name appears in no non-definition
  context), ast-grep applies 7 heuristic exclusion rules (macro-body, init-list,
  `__attribute__((used))`, `virtual`/`override`, `extern "C"`, GTest macros,
  operator-overloads), and libclang verifies the residue via USR resolution
  over a grep-pre-filtered subset of files. Expected runtime ~1 min total —
  candidate count drops from ~300 to ~30 before any libclang work happens.
- **Tier placement is performance-gated**: ≤ 10 s measured → both `make lint`
  and `make lint-full`; > 10 s → `lint-full` only with auto-enable under
  `--rule ^unused-function`. Decision made empirically after Stage 3 lands.
- Once the hybrid ships and cppcheck is deleted, the toolchain is **four
  open-source backends** (ast-grep, alint, clang-tidy/libclang, Python
  structural) — no remaining tool outside `make lint` / `lint-full`.

---

## Appendix A — CodeQL: how it compares to our stack

This appendix records why CodeQL was *not* part of any phase, and the
specific triggers that would change that. The short version: CodeQL operates
one tier deeper than we currently need, at a setup cost we're not yet
willing to pay.

### A.1 What CodeQL is

CodeQL is the static-analysis engine originally built by **Semmle** (Oxford
program-analysis spinout), acquired by GitHub in 2019 and rebranded.
It powers GitHub Code Scanning and ships as part of GitHub Advanced Security.

The analysis model is **database-driven, not pattern-driven**:

1. A *language extractor* compiles your source into a **relational database**
   — every AST node, type, scope, control-flow edge, and dataflow successor
   becomes a row in some table.
2. Queries are written in **QL**, a declarative, side-effect-free,
   strongly-typed language descended from Datalog. A query is logically a
   set of predicates over the database tables; the evaluator finds all
   tuples satisfying them.
3. Results are emitted as SARIF, which feeds directly into GitHub Code
   Scanning's PR-annotation UI.

The defining property: QL queries can range over **multiple translation
units, multiple files, and full control/data flow paths** in one expression.
That capability is what ast-grep and tree-sitter–class tools fundamentally
lack — they're per-file syntactic matchers.

### A.2 Components / stack

CodeQL is closed-source for the engine and CLI; what's open lives at
[github.com/github/codeql](https://github.com/github/codeql) (the QL
libraries and bundled queries). Implementation details below are inferred
from Semmle's academic record, GitHub engineering posts, job postings, and
the visible artefacts in the CLI distribution.

| Component | Language | Notes |
|---|---|---|
| **QL evaluator (the engine)** | OCaml | The historical Semmle engine. Closed-source. Same lineage as Coq, Flow, Infer, and Hack's typechecker — program-analysis work has converged on OCaml because algebraic data types + pattern matching + native-code performance + a strong module system are exactly what an evaluator needs. |
| **CodeQL CLI** (`codeql` binary) | Java | A small native wrapper launches a JAR. Visible inside the distribution. |
| **C/C++ extractor** | C++ (wraps Clang's frontend) | Uses Clang for parse + Sema, then emits database tuples. |
| **Java extractor** | Java (wraps Eclipse JDT) | |
| **Python extractor** | Java + an embedded Python parser | |
| **JavaScript / TypeScript extractor** | TypeScript (uses the TS compiler) | |
| **Go / Ruby / Swift / C#** extractors | each in/around their respective toolchain | |
| **QL standard libraries** (`codeql/cpp-all`, `codeql/javascript-all`, …) | QL itself | The `.qll` files defining `Expr`, `Function`, `DataFlow::Node`, taint configurations, etc. Open-source. |
| **Bundled query packs** (`codeql/cpp-queries`, etc.) | QL | The actual security/quality queries you run. Open-source. |
| **`.qlpack` packaging** | YAML manifests | Semver dependencies resolved from the registry on `ghcr.io/codeql`. |

Distribution is per-language: installing CodeQL for one language pulls down
the engine, the CLI, the matching extractor, the language's `*-all` library
pack, and any query packs you reference — usually a few hundred MB combined
before you've analysed anything.

### A.3 Tier comparison vs. our stack

CodeQL spans more tiers than any single backend we use, but it's slower and
heavier in every one of them. The comparison really lives in **what tier
each rule needs**:

| Tier | Example rule | Our backend | CodeQL |
|---|---|---|---|
| Syntactic pattern | `no-raw-alloc`, `dup-typeid-or` | **ast-grep** — ~6 s, seconds-to-author | Possible, heavyweight |
| Filesystem shape | `ls-test-has-golden`, file-graph integrity | **alint** — ~0.3 s, declarative YAML | Not its design centre — possible via custom extractor work |
| Type-aware (single-procedure) | `int-cast-type-aware-decl` (float→int narrowing) | **clang-tidy** built-in / **libclang** AST walk — ~4 min for the broad sweep | Trivially expressible in QL via the type system; same cost shape (slow database build) |
| Inter-procedural dataflow | `checked-pool-alloc` (proposal §4.2, deferred) | **clang-analyzer** (existing `make analyze`) | **CodeQL is purpose-built for this.** QL's `DataFlow::Configuration` framework is the canonical way to express "source → sink unless sanitised" properties. |
| Taint analysis | "untrusted input flows into `system()` without escaping" | **Not in our stack** | **CodeQL's signature capability.** Web-app CVE classes are why GitHub bought Semmle. |
| Security query packs (encyclopedic) | OWASP / CWE coverage | **Not in our stack** | **Ships hundreds, maintained.** Often the deciding reason to adopt for web-facing code. |

clang-analyzer (which we already invoke via `make analyze` and as the
`tidy-clang-analyzer-*` family inside `make lint-full`) gives us the
**dataflow tier for C++ specifically** without CodeQL's database build.
That single fact is most of why we haven't adopted CodeQL: the slowest tier
we currently care about is already covered by a tool we already run.

### A.4 What we'd gain — and what it costs

**Gains** (real, not hypothetical — these are CodeQL's actual strengths):

- Inter-procedural taint analysis with a mature framework (`DataFlow::Configuration`)
- Path-sensitive null-deref / use-after-free tracking across function boundaries
- Encyclopedic security query packs maintained by GitHub Security Lab
- Native GitHub Code Scanning integration via SARIF — PR annotations, alert dashboards, compliance reporting
- QL is genuinely a good language for these analyses once you've climbed the learning curve

**Costs**:

- **Database build**: minutes to hours per language; must be rebuilt on any
  meaningful source change. This is the workflow killer — incompatible with
  the iteration-speed split we just engineered (`make lint` ~6 s versus
  `make lint-full` ~4 min). CodeQL would be `make lint-codeql` ~30 min.
- **Disk + memory**: GB-sized databases, hundreds of MB of qlpacks. The
  `~10 MB ast-grep binary + a YAML file` model evaporates.
- **Learning curve**: QL is declarative-Datalog-flavoured. Authoring even a
  trivial query is qualitatively different from writing an ast-grep pattern;
  competent rule authors need a project-internal ramp-up.
- **Language coverage gap**: no `tree-sitter-lambda`-equivalent extractor
  exists for our `.ls` corpus. Writing one is substantial work — a non-trivial
  amount of the proposal §3.8 / Phase 1 effort, but for QL instead of ast-grep.
- **License**: free for public repos via GitHub Code Scanning; **paid (GitHub
  Advanced Security)** for closed-source commercial use. ast-grep, alint, and
  clang-tidy are all unconditionally free.
- **Tier overlap**: CodeQL spans tiers we already cover. Adopting it would
  mean either (a) running it in parallel with the existing stack and
  reconciling overlapping findings, or (b) retiring some of the existing
  backends. Neither is cheap.

### A.5 Adoption triggers

CodeQL becomes a sensible addition when at least one of these holds:

1. We start shipping a **web-facing component** where untrusted input reaches
   query/exec/file sinks. Taint analysis is then load-bearing, and CodeQL is
   the industry-leading tool for it.
2. We need **GitHub Code Scanning compliance** for a customer or contract —
   SARIF + GitHub-native PR annotations are part of the deal.
3. The deferred `checked-pool-alloc` rule (proposal §4.2) becomes urgent AND
   clang-analyzer's existing dataflow checkers prove insufficient for our
   specific allocator-failure contract. (Reminder: that rule is itself
   currently blocked on the Memory Context centralisation, §4.2 / §7.6 — so
   even this trigger is two preconditions deep.)
4. The security-query-pack value proposition shifts — e.g., GitHub releases
   CVE-class queries specifically targeting transpilers / JIT engines /
   layout engines that map onto our code.

Until one of those fires, the cost/benefit is not in CodeQL's favour for
this codebase. The existing four-tier stack (ast-grep / alint / clang-tidy +
libclang / clang-analyzer) covers the analyses we actually need without the
database-build overhead, the SARIF tooling chain, the QL learning curve, or
the closed-source dependency.
