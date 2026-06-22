# Lambda Lint — Operational Reference

This document describes the lint stack as it ships today: how `make lint`
works, how to read its findings, and what each rule checks. For the design
rationale and proposal history, see [`vibe/Lambda_Lint.md`](../../vibe/Lambda_Lint.md).

## 1. What it is

`make lint` runs the **project-specific policy linter** — a single entry
point that aggregates findings from four backends, applies inline
suppression markers, gates the build on severity, and (optionally) writes a
structured report for AI-agent batch cleanup.

It is *not* a replacement for the orthogonal semantic gates (`tidy`,
`analyze`, `lint-cppcheck`); those still exist and do different work.

```
┌────────────────────────────────────────────────────────────────────┐
│                          make lint                                 │
│                  utils/lint/run.sh  (bash + jq)                    │
└─────┬──────────────┬──────────────┬───────────────────┬────────────┘
      ▼              ▼              ▼                   ▼
 ┌──────────┐  ┌──────────┐  ┌─────────────┐   ┌─────────────────┐
 │ ast-grep │  │  alint   │  │ clang-tidy  │   │ structural .py  │
 │ pattern  │  │ filesys. │  │ + libclang  │   │ check_state_*   │
 │  rules   │  │ rules    │  │ type-aware  │   │ (coverage)      │
 └─────┬────┘  └────┬─────┘  └──────┬──────┘   └────────┬────────┘
       │            │               │                   │
       └────────┬───┴───────────────┘                   │
                ▼                                       │
   unified NDJSON  ─►  suppression filter  ─►  severity gate
                                                        ▼
                                          Report_NNN.{md, json, tsv}
                                          (test_output/lint/, on --report)
```

## 2. Backends

| Backend | What it does | Where rules live |
|---|---|---|
| **ast-grep** | Node patterns over file *contents* (C/C++ and Lambda `.ls` via the registered `tree-sitter-lambda` grammar) | `utils/lint/rules/c-cpp/*.yml`, `utils/lint/rules/lambda/*.yml` |
| **alint** | Filesystem *shape* and cross-file relations (sibling-file existence, file-graph integrity) | `.alint.yml` at repo root |
| **clang-tidy + libclang** | *Type-aware* checks that need Clang's Sema (narrowing conversions, explicit float→int casts) | `utils/lint/tidy/run_tidy.sh` (orchestrator) + `utils/lint/tidy/explicit_cast_check.py` (libclang) |
| **Structural Python** | Coverage invariants that aren't expressible in any pattern engine (e.g. "every enum value has a table row") | `utils/check_state_machine.py` |

A thin cross-engine **manifest** at `utils/lint/manifest.yml` indexes every
rule with metadata (category, claude-md reference, tags). The
`utils/lint/manifest_check.sh` smoke-check (run automatically by
`make lint --list`) asserts the manifest is in sync with the on-disk rules.

## 3. Running

```bash
make lint                                         # full sweep — the CI gate
make lint ARGS='--rule ^no-raw-alloc$'            # one rule (regex over rule ids)
make lint ARGS='--rule ^state-store-'             # whole family
make lint ARGS='--structural-only'                # skip pattern rules
make lint ARGS=--report                           # also write Report_NNN.{md,json,tsv}
make lint ARGS=--list                             # list every rule across backends
make lint ARGS=--format=github                    # GitHub Actions annotations
```

Direct invocation: `utils/lint/run.sh [args]` works the same way.

For a single backend run during development:

```bash
ast-grep scan -c sgconfig.yml --filter '^no-raw-alloc$'   # ast-grep only
alint check                                                # alint only
utils/lint/tidy/run_tidy.sh                                # clang-tidy + libclang
```

## 4. Severity and suppression

Three severities map directly to gate behaviour:

| Severity | Gate behaviour | Use |
|---|---|---|
| `error` | **fails** `make lint` (exit non-zero) | Hard policy bans; preventive rules with 0 existing hits |
| `warning` | surfaces in output and reports, **does not fail** | Broad rules with existing hits we haven't cleaned up yet; type-aware-but-imprecise rules |
| `info` | surfaces, doesn't fail | Inventories (TODO, ls-todo) |

Inline suppression: most rules declare a `metadata.suppress: <MARKER>` in
their YAML. A finding is dropped when its source line carries a trailing
comment containing that marker:

```cpp
return (int)view->width;  // INT_CAST_OK: rounded to nearest pixel for hit-test
```

The marker convention (`RAWALLOC_OK`, `INT_CAST_OK`, `STATE_STORE_OK`, …) is
self-documenting and greppable, parallel to the rule that flagged the site.

## 5. Rule reference

Findings counts shown reflect the working-tree state at last sweep
(Report_005). Severity drives the gate; the **`enforces`** column points
back to the rule in [CLAUDE.md](../../CLAUDE.md) when one is enforced.

### 5.1 Memory safety

Catches policy violations around allocation, lifetime, and ownership.

| Rule | Backend | Sev. | Suppress | Enforces | What it catches |
|---|---|---|---|---|---|
| `no-raw-alloc` | ast-grep | error | `RAWALLOC_OK` | CLAUDE rule 1 | Raw `malloc` / `calloc` / `realloc` / `free` / `strdup` / `strndup` in `lambda/`, `radiant/`. Use `mem_alloc`/`mem_free` or `raw_malloc`+marker. |
| `alloca-static-size` | ast-grep | warning | `ALLOCA_OK` | — | `alloca()` with a non-static size — runtime variable, `var × sizeof(T)`, or function-call result. Stack-overflow risk. |
| `no-new-delete` | ast-grep | warning | `NEW_DELETE_OK` | — | Raw `new` / `delete` in pool/arena-managed code (excluding `lambda/js/` engine). |
| `no-manual-refcnt` | ast-grep | error | `REFCNT_OK` | — | Direct mutation of `->ref_cnt` instead of the GC API. Preventive (0 hits today). |
| `no-unsafe-libc-str` | ast-grep | warning | `UNSAFE_LIBC_OK` | — | Classic-overflow libc primitives: `strcpy`, `strcat`, `sprintf`, `gets`. Prefer bounded variants or `lib/`'s `StringBuf`/`Str`. |
| `retained-field-write` | ast-grep | error | `RETAINED_FIELD_OK` | — | Direct write to a retained Radiant field (`tag_name`, `class_names`, `family`, `image`, `text_content`, `source_path`, `source_data`). Route through `radiant/retained_fields.hpp` ownership helpers. |

### 5.2 Layout correctness

Catches float-vs-int truncation in Radiant layout code and the related CSS
shorthand-temporary pitfall.

| Rule | Backend | Sev. | Suppress | Enforces | What it catches |
|---|---|---|---|---|---|
| `int-cast-type-aware-decl` | clang-tidy | warning | `INT_CAST_OK` | CLAUDE rule 11 | `int x = float_expr;` initializations — truncates float to int. Uses built-in `bugprone-narrowing-conversions` tuned to float→int only. Precise (type-aware). |
| `int-cast-type-aware` | libclang | warning | `INT_CAST_OK` | CLAUDE rule 11 | Explicit `(int)float_expr` / `static_cast<int>(float_expr)` casts. The type-aware sibling of the retired `no-int-cast-radiant`; only fires when the source resolves to `float`/`double`. |
| `no-int-layout-decl` | ast-grep | warning | `INT_LAYOUT_OK` | CLAUDE rule 11 | Pattern-only sibling: `int x = obj->field` where `field` is in a small allowlist of known float layout dimensions (`width`/`height`/`padding`/…). |
| `css-temp-decl` | ast-grep | error | `CSS_TEMP_DECL_OK` | — | Fragile CSS shorthand temporary declarations in `resolve_css_style.cpp` (`CssDeclaration X = *decl;`, `obj.value = &local;`). Use `lam::CssTempDecl` / `lam::CssTempListDecl<N>` from `radiant/css_temp_decl.hpp`. |
| `no-raw-css-value-stack` | ast-grep | error | `CSS_TEMP_DECL_OK` | — | Same hazard, scoped to other `radiant/*.cpp` files (extends `css-temp-decl`). |

### 5.3 StateStore migration invariants

Family of 10 rules guarding the radiant **StateStore migration**: external
code must read and write document/view state through the StateStore API,
not raw mirror fields. All `error`, all suppressed via `STATE_STORE_OK`.

| Rule | What it catches |
|---|---|
| `state-store-form-mirror` | External access to `FormControlProp` mirror fields (`disabled`, `readonly`, `selection_*`, …). |
| `state-store-form-mirror-internal` | The StateStore touching a non-text form mirror directly. |
| `state-store-form-pseudo-cache` | `placeholder_shown` / `focus_visible` cached on the form control (must derive from StateStore). |
| `state-store-pseudo-state` | Legacy `pseudo_state` API / field reintroduced (`dom_element_*_pseudo_state`, `DomElement::pseudo_state`, `typedef DocState StateStore`). |
| `state-store-focus-globals` | Text-control focus globals / slot accessors (`g_active_element`, `tc_active_element_slot`, …). |
| `state-store-scroll-mirror` | External `->h_scroll_position` / `->v_scroll_position` / `->h_max_scroll` / `->v_max_scroll` access. |
| `state-store-scroll-interaction` | ScrollPane interaction-state mirrors (hover/drag) reintroduced. |
| `state-store-scroll-api` | Pane-only `scroll_state_*` API used outside the StateStore. |
| `state-store-view-state-write` | External direct writes to `->data.form/scroll.*` or `->flags.hovered/active/focused`. |
| `state-store-doc-state-write` | External writes to DocState ownership fields (`hover_target`, `open_dropdown`, `context_menu_*`, `scroll_*`, `needs_repaint`, …). |

### 5.4 API & structure hygiene

| Rule | Backend | Sev. | Suppress | Enforces | What it catches |
|---|---|---|---|---|---|
| `no-item-payload-cast` | ast-grep | error | `ITEM_CAST_OK` | — | Raw Item-payload casts in migrated Lambda files (`(Array*)item.item`, `0x00FFFFFFFFFFFFFF` mask). Use `lib/lambda_typed.hpp` helpers. |
| `no-radiant-view-cast-layout` | ast-grep | error | `RADIANT_CAST_OK` | — | Unsafe `View*` / `Dom*` cast in layout files. Use `lam::view_as_*` / `dom_as<>` from `lib/tagged.hpp`. |
| `no-radiant-view-cast-render` | ast-grep | error | `RADIANT_CAST_OK` | — | Same, in render/event/driver files (different leaf-type set). |
| `no-std-containers` | ast-grep | warning | `STD_CONTAINER_OK` | CLAUDE rule 3 | `std::string` / `std::vector` / `std::map` / `std::unordered_map` / etc. Use `lib/` equivalents (`Str`, `ArrayList`, `HashMap`). |
| `no-using-namespace-std-in-header` | ast-grep | error | — | — | `using namespace std;` in a `.h` / `.hpp` (poisons every TU that includes it). |

### 5.5 Debug hygiene

| Rule | Backend | Sev. | Suppress | Enforces | What it catches |
|---|---|---|---|---|---|
| `no-printf-debug` | ast-grep | warning | `PRINTF_OK` | CLAUDE rule 4 | `printf` / `fprintf` / `std::cout` / `std::cerr` used for diagnostics. Use `log_debug`/`log_info`/`log_error` from `lib/log.h`. CLI/REPL/help/main excluded. |
| `no-tmp-path` | ast-grep | error | `TMP_PATH_OK` | CLAUDE rule 2 | String literals starting `"/tmp"`. Use `./temp/` (project temp dir). |

### 5.6 Correctness

| Rule | Backend | Sev. | Suppress | What it catches |
|---|---|---|---|---|
| `unsafe-string-scan` | ast-grep | error | `STR_SCAN_LOCAL_OK` | Unsafe open-coded string scans: `strchr(set, *p)` (matches `'\0'`) and `is*(*p)` / `is*(p[i])` ctype on raw `char*` (UB for bytes ≥ 0x80). Use NUL-safe helpers in `lib/str.h §17` or cast operand to `(unsigned char)`. |
| `dup-typeid-or` | ast-grep | error | — | Duplicate `$X == LMD_TYPE_ARRAY \|\| $X == LMD_TYPE_ARRAY` — copy-paste bug; the second operand was meant to differ. |
| `state-machine` | python | error | — | Coverage check over Radiant's declarative state-machine schema: every active enum value in a `COMPLETE_FAMILIES` set must have a matching table row. Lives in `utils/check_state_machine.py`; dispatched as a structural check. |

### 5.7 Test hygiene

| Rule | Backend | Sev. | Suppress | Enforces | What it catches |
|---|---|---|---|---|---|
| `ls-test-has-golden` | alint | warning | — | CLAUDE rule 8 | Every `test/lambda/**/*.ls` script must have a sibling `*.txt` golden. `mod_*`, `_*`, `schema_*` files and `test/lambda/validator/**` excluded by convention. |

### 5.8 Inventory

Informational rules — surface findings in reports but never fail the gate.

| Rule | Backend | Sev. | What it catches |
|---|---|---|---|
| `todo-inventory` | ast-grep | info | TODO/FIXME/XXX/HACK markers in `lambda/` / `radiant/` / `lib/` C/C++ comments. |
| `ls-todo-inventory` | ast-grep | info | Same, in `.ls` Lambda scripts. First rule against the registered `tree-sitter-lambda` grammar. |

## 6. Reports

`make lint ARGS=--report` writes three sibling files under
`test_output/lint/Report_NNN.*` (auto-incrementing index):

- **`.md`** — human summary: header table, summary-by-rule, top files, per-rule sections with code snippets
- **`.json`** — full structured findings: `meta`, `counts_by_rule`, `counts_by_backend`, `counts_by_file`, `findings[]` with `file`, `line`, `column`, `byte_offset`, `text`, `source_line`, `backend`, `severity`, `message`. The canonical input for an AI agent doing batch cleanup.
- **`.tsv`** — flat one-row-per-finding: `backend`, `rule_id`, `severity`, `file`, `line`, `column`, `text`, `message`. For `awk` / `cut` / quick grep.

The same `(backend, rule_id, file, line)` ordering across all three formats.

## 7. Adding a new rule

1. **Pick the right backend.**
   - ast-grep: node pattern over file content (C/C++ or `.ls`)
   - alint: filesystem shape or cross-file relation
   - clang-tidy/libclang: requires type info (Sema) or per-CFG reasoning
   - Structural Python: coverage invariant nothing else can express
2. **Author the rule** in that backend's native format. See the existing
   rules in `utils/lint/rules/c-cpp/*.yml` (ast-grep), `.alint.yml`
   (alint), or `utils/lint/tidy/explicit_cast_check.py` (libclang) for
   worked examples.
3. **Add a manifest entry** to `utils/lint/manifest.yml` with
   `backend`, `file`, `category`, `enforces` (if a CLAUDE rule), `tags`.
4. **Run `make lint ARGS=--list`** — the smoke check at the bottom
   confirms the rule is in sync.
5. **Run `make lint --rule ^<your-rule>$`** in isolation; fix or mark
   any findings.

The ast-grep authoring quirks worth knowing (the `not:` / `constraints:`
interaction, the standalone-pattern-as-declaration trap, distinct
metavar names per `any:` pattern) are documented in
[`vibe/Lambda_Lint.md §3.9`](../../vibe/Lambda_Lint.md).
