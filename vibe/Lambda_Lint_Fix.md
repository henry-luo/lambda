# Lambda Lint — Structural Fix Proposal

## 0. Scope

This is the fix-side companion to [vibe/Lambda_Lint.md](Lambda_Lint.md). That
document specifies the *engine* (ast-grep + alint + clang-tidy + hybrid
unused-function) and the *rule corpus*. This one decides what to do about the
findings the corpus produces today.

Snapshot from `make lint` on `math4-rule15-18` (2026-06-22):

| Severity | Rule | Findings |
|---|---|---:|
| ❌ error    | no-radiant-view-cast-render        |     10 |
| ❌ error    | no-raw-alloc                       |     22 |
| ❌ error    | no-tmp-path                        |      1 |
| ❌ error    | retained-field-write               |      1 |
| ❌ error    | state-store-doc-state-write        |      6 |
| ❌ error    | state-store-form-mirror            |     13 |
| ⚠️ warning  | alloca-static-size                 |     57 |
| ⚠️ warning  | ls-test-has-golden                 |    133 |
| ⚠️ warning  | no-asymmetric-arg-null-check       |     11 |
| ⚠️ warning  | no-int-layout-decl                 |      1 |
| ⚠️ warning  | no-new-delete                      |     70 |
| ⚠️ warning  | no-printf-debug                    | 1,266 |
| ⚠️ warning  | no-std-containers                  |     12 |
| ⚠️ warning  | no-unsafe-libc-str                 |     28 |
| ⚠️ warning  | unused-depth-param                 |      6 |
| ⚠️ warning  | unused-function                    |    173 |
| ℹ️ info     | large-stack-array                  |     36 |
| ℹ️ info     | ls-todo-inventory                  |      4 |
| ℹ️ info     | todo-inventory                     |     92 |
|             | **Total**                          | **~1,952** |

Two things stand out:
1. `make lint` is **already failing** (53 error-level findings, all in
   radiant + a single `/tmp` path in `lambda/main.cpp`). The lint gate is
   not currently green.
2. The 1,266 `no-printf-debug` warnings are the headline number, but ~85 %
   live in files that **legitimately emit to stdout** (s-expression dumper,
   PDF writer, Ruby/Python/Bash `puts`/`print` builtins, CLI subcommand
   handlers). These are scope problems, not coding problems.

The structural fixes split cleanly along that boundary: tighten rule scope
*first*, then tackle the residue.

---

## 1. Operating principles

The fix order is set by what shipping lint policy is trying to buy us, in
order of value-per-touch:

1. **Get the gate green.** 53 errors block `make lint` today. Until that's
   zero, the noise from warnings doesn't get reviewed at all because the
   gate exits non-zero before anyone scrolls past them.
2. **Fix false-positive *rules*, not files.** Whenever a finding cluster is
   ≥90 % FP in one directory, the right fix is the YAML, not 600 PRINTF_OK
   markers. Suppression markers are for audited single-line exceptions
   ([Lambda_Lint.md §3.3](Lambda_Lint.md#33-suppression)), not for unscoping
   a rule by hand.
3. **Address real findings at the call-site root cause.** Rule 1 of
   CLAUDE.md: never hard-code or work around. The real bugs in this report
   (matrix() null deref, state-store migration debt, RE2 ownership glue,
   MIR alloca sizing) are each *one structural decision* away from being
   fixed everywhere at once.
4. **Treat unbounded findings as research backlog.** `unused-function`
   (173) and `todo-inventory` (92) are inventories, not gates. Their value
   is in how they're triaged later, not in batch-suppressing them now.

The proposal below is grouped by these four bands.

---

## 2. Band 1 — Make the lint gate green (53 errors)

### 2.1 `no-tmp-path` (1)

[lambda/main.cpp:2524](lambda/main.cpp:2524):

```cpp
const char* temp_svg = "/tmp/lambda_graph_temp.svg";
```

**Root cause:** legacy hard-coded path. CLAUDE.md rule 2.
**Fix:** rewrite to `"./temp/lambda_graph_temp.svg"`, ensure `./temp/`
exists at startup (it does — every test fixture writes there). One-line
change.

### 2.2 `retained-field-write` (1) — **false positive, fix the rule**

[radiant/state_schema.cpp:868](radiant/state_schema.cpp:868):

```cpp
scope->family = family;   // ← scope is a TransitionScope, not a DomNode
```

The rule's pattern `$OBJ->family = $V` cannot see that `scope` is a state-
machine `TransitionScope*`, where `family` is an enum tag — not a
`DomElement::family` retained pointer. Name collision, not a violation.

**Structural fix:** tighten
[utils/lint/rules/c-cpp/retained-field-write.yml](utils/lint/rules/c-cpp/retained-field-write.yml)
in one of two ways:

- **A. Add an `ignores:` entry for `radiant/state_schema.cpp`** — cheap,
  but every future name-colliding `family` field anywhere else relights
  the rule.
- **B. Constrain `$OBJ` by name regex** — same workaround the
  `state-store-*` rules already use:
  ```yaml
  constraints:
    OBJ: { regex: "^(elem|node|dom|view|block|text|child|parent|node_ptr|.*->retained.*)$" }
  ```
  Cleaner — the rule self-documents what subjects it cares about.

**Recommendation: B.** This is the same pattern used by
`state-store-form-mirror` (constrain `O` to `^form|->form`), so the rule
corpus stays internally consistent.

### 2.3 `state-store-doc-state-write` (6)

All 6 are direct `state->open_dropdown = NULL` / `state->needs_repaint =
true` writes in [radiant/event.cpp](radiant/event.cpp) and
[radiant/editing_dispatch.cpp](radiant/editing_dispatch.cpp). The
surrounding code already calls `doc_state_set_*` helpers for the
*other* fields in the same blocks — these specific fields just don't have
helpers yet.

**Root cause:** the StateStore migration shipped helpers for the
ownership-target fields (`hover_target`, `active_target`, drag/scroll) but
not for the *flag* fields (`needs_repaint`, `open_dropdown`,
`context_menu_*`). Migration debt, not application bugs.

**Structural fix:** finish the helper coverage in
[radiant/state_store.cpp](radiant/state_store.cpp):

```cpp
void doc_state_request_repaint(DocState*);
void doc_state_close_dropdown(DocState*);
void doc_state_clear_context_menu(DocState*);
```

Then sweep the 6 sites to call them. The StateStore is the right home
for invalidation tracking (`needs_repaint = true` should also bump the
state version on the store side, which raw assignment skips today).

### 2.4 `state-store-form-mirror` (13)

All 13 are in event.cpp text-control paths (`rich_select_all_sync_*`,
selection rendering, dispatch) — reading and writing
`form->selection_start` / `selection_end` / `selection_direction`
directly.

**Root cause:** same migration debt — these mirror fields exist for
read-side speed but the migration to "writes go through the
projection" stopped short of the text-selection path.

**Structural fix:** add the missing projection writers
(`form_control_set_selection(form, start, end, direction)`) and route the
13 sites through them. The read sites in event.cpp:2476/2477/2933/2934/
3629 are 9 of the 13 — those are *reads*, which the rule shouldn't fire
on. Refine the rule to match only the assignment form
(`pattern: $O->$F = $V` with the existing `constraints`) so reads stay
silent; this also matches what `state-store-doc-state-write` does
(`kind: assignment_expression`).

### 2.5 `no-radiant-view-cast-render` (10)

All 10 are `static_cast<DomText*>(...)` / `static_cast<DomElement*>(...)`
in [radiant/event.cpp](radiant/event.cpp) (5) and
[radiant/render_form.cpp](radiant/render_form.cpp) (4) and
[radiant/ui_context.cpp:269](radiant/ui_context.cpp:269) (1 C-cast). The
rule already excludes the tagged-helper namespace; these are sites that
predate it.

**Root cause:** un-migrated callers of the type-safe `dom_as<>` /
`view_as_*` helpers from `lib/tagged.hpp`.

**Structural fix:** mechanical sweep. Each `static_cast<DomText*>(x)`
becomes `lam::dom_as<DomText>(x)`. The render_form.cpp sites are inside
text-control rendering where `block` is known to be a DomElement — a
single call to `dom_require_element(block)` (already used elsewhere in
this file) covers each callsite.

### 2.6 `no-raw-alloc` (22)

Three distinct clusters, three different fixes:

| Cluster | Files | Count | Fix |
|---|---|---:|---|
| **JS bt-regex parser** | `lambda/js/js_bt_regex.cpp` | 13 | Internal parser pre-MIR; the `ranges.data` / `items.data` / `alts.data` / `names.data` arrays use `realloc`/`free` because they're dynamic growth. → Replace with `lam::ArrayList` (which uses the MEM_CAT_LAYOUT pool) or a small bump-allocator scoped to the parse session. Already the pattern in sibling parsers. |
| **Layout snapshot lifecycle** | `radiant/layout_pass.cpp` | 4 | Snapshot is allocated `calloc`-then-`free` across 3 paths. → Move to a per-pass scratch arena (`lycon->scratch`), which is already used by `TableMetadata` in `layout_table.cpp`. |
| **Frame clock + JS runtime + RE2 ad-hoc** | `radiant/frame_clock.cpp`, `lambda/js/js_runtime_function.cpp`, `lambda/js/js_runtime.cpp`, `lambda/js/js_regex_wrapper.cpp` | 5 | Each is one or two genuine `mem_alloc` candidates that escaped the original migration. Mechanical 1-line conversions. |

For each cluster, do the structural conversion first, then verify zero
findings remain rather than marking with `RAWALLOC_OK`.

---

## 3. Band 2 — Fix false-positive *rules* before fixing findings

These four warning clusters are dominated by false positives caused by
under-scoped rules. Suppressing each line individually would mean
hundreds-to-thousands of `*_OK` markers; fixing the rule clears the noise
in one PR and surfaces what's actually left.

### 3.1 `no-printf-debug` (1,266 → estimated ~150)

The findings by file (Top 12, covering 96 % of the corpus):

| File | Hits | Why printf is legitimate |
|---|---:|---|
| `lambda/emit_sexpr.cpp` | 628 | Deliberate s-expression emitter to stdout — its job is to print. |
| `lib/` (pdf_writer.c, hashmap.c test, file.c errors, etc.) | 118 | PDF byte writer, hash-map self-test, file.c error reporters. |
| `lambda/rb/rb_print.cpp` + `rb_runtime.cpp` | 111 | Ruby `print`/`puts`/`p` builtin implementations. |
| `radiant/webdriver/cmd_webdriver.cpp` | 45 | CLI subcommand handler — same family as `lambda/main.cpp` (already excluded). |
| `lambda/main-repl.cpp` | 51 | REPL output. Same family as `lambda/cli/` and `lambda/repl/` already excluded. |
| `lambda/bash/transpile_bash_mir.cpp` + `bash_runtime.cpp` | 68 | Bash `echo`/`printf` builtin implementations. |
| `lambda/py/py_stdlib.cpp` | 60 | Python `print` builtin and stdlib output. |
| `lambda/validator/ast_validate.cpp` | 55 | `lambda validate` CLI subcommand. |
| `radiant/cmd_layout.cpp` | 14 | `lambda layout` CLI subcommand. |
| `lambda/headless_stubs.cpp` | 11 | Headless build stubs (CLI-only build). |
| `lambda/npm` | 10 | npm-compat CLI. |
| `radiant/event_sim.cpp` + `render_img.cpp` + misc | ~30 | Test/sim drivers and CLI dump paths. |

**Root cause:** the rule's exclusion list (lambda/cli/**, lambda/repl/**,
lambda/main.cpp, lambda/lambda-error.cpp, lambda/lambda-help.cpp,
lambda/print.cpp) covers only the original main-binary CLI paths. The
*polyglot runtime* introduced after the rule shipped (Ruby/Python/Bash
print builtins, the s-expression bridge, the validator CLI, the webdriver
CLI, the layout CLI) was never added.

**Structural fix:** extend
[utils/lint/rules/c-cpp/no-printf-debug.yml](utils/lint/rules/c-cpp/no-printf-debug.yml)
`ignores:` to cover the categories that are *by design* stdout emitters:

```yaml
ignores:
  - "**/tree-sitter*/**"
  # CLI subcommand handlers (sibling of lambda/main.cpp)
  - "lambda/main-repl.cpp"
  - "lambda/cli/**"
  - "lambda/repl/**"
  - "lambda/main.cpp"
  - "lambda/lambda-error.cpp"
  - "lambda/lambda-help.cpp"
  - "lambda/print.cpp"
  - "lambda/validator/ast_validate.cpp"   # `lambda validate` subcommand
  - "lambda/headless_stubs.cpp"           # CLI-only build stubs
  - "lambda/npm/**"                       # npm-compat CLI
  - "radiant/webdriver/**"                # `lambda webdriver` subcommand
  - "radiant/cmd_layout.cpp"              # `lambda layout` subcommand
  # Source-language print/puts builtin implementations (these *are* the
  # stdout-writing side of the runtime, not debug prints).
  - "lambda/rb/rb_print.cpp"
  - "lambda/rb/rb_runtime.cpp"
  - "lambda/bash/transpile_bash_mir.cpp"
  - "lambda/bash/bash_runtime.cpp"
  - "lambda/py/py_stdlib.cpp"
  # Lambda's own s-expression dumper (Racket bridge)
  - "lambda/emit_sexpr.cpp"
  # PDF byte writer is by definition I/O-producing
  - "lib/pdf_writer.c"
```

The handful left after this (lib/font/woff2 fprintf-on-error, scattered
debug prints in lib/strbuf.c, lib/file.c, lib/test_utils_pool.c,
specificity.c, lambda/build_ast.cpp, lambda/mark_editor.cpp, etc.) are the
*actual* debug-print sites the rule was designed to catch. Estimate
~150 remaining, which is the size suppression markers were designed for.

After the rule fix lands, sweep the residue (convert to `log_*`) and then
graduate the rule to `severity: error` so new debug prints can't slip
in. This is the high-leverage move — one YAML PR turns the
biggest-by-far cluster from "ignored noise" into "actionable + gated."

### 3.2 `no-asymmetric-arg-null-check` (11 → ~4 real)

Two clusters in [radiant/resolve_css_style.cpp](radiant/resolve_css_style.cpp):

**False positives** (5): lines 1253, 1255, 1258, 1260 (hsl/hsla), 6673
(translateY-percent). Each dereference is inside an enclosing
`if (func->args[N] && ...)` that the rule's flat pattern can't see —
the per-arg null guard *is* present. Example at 1252:

```cpp
if (func->args[1] && func->args[1]->type == CSS_VALUE_TYPE_PERCENTAGE)
    s = func->args[1]->data.percentage.value / 100.0;     // ← flagged, but guarded
```

**True positives** (6): lines 6715 (scale-Y), 6826–6830 (matrix). These
do `if (func->arg_count >= 6) { ...->args[0]->data...; ...->args[1]->data...; }`
with **no** per-arg null check — exactly the Radiant audit finding #9 the
rule was designed to catch.

**Structural fix:** two parts.

1. **Tighten the rule.** Add a `not:` clause that skips matches *inside*
   an `if_statement` whose condition tests the same `$N`:
   ```yaml
   rule:
     all:
       - pattern: $OBJ->args[$N]->data.$F.value
       - not:
           inside:
             kind: if_statement
             has:
               field: condition
               regex: "args\\s*\\[\\s*$N\\s*\\]\\s*&&"
   ```
   *(This trips the §3.9 ast-grep quirk that `not:` blocks silently drop
   `constraints:`. The workaround there — enumerate bad shapes rather than
   subtract good shapes — applies; if the `inside`/`regex` combo can't
   express it reliably, fall back to a per-pattern hand-enumeration of the
   guarded forms.)*

2. **Fix the 6 true positives** in scale-Y and matrix(). Same pattern
   as the already-correct translate3d/scale3d/matrix3d sites
   immediately below them (6837 onward) — add `if (func->args[N])` guards.

### 3.3 `no-std-containers` (12 → 0 with one wrapper)

All 12 are in [lambda/rb/rb_runtime.cpp](lambda/rb/rb_runtime.cpp) (10)
and [lambda/re2_wrapper.cpp](lambda/re2_wrapper.cpp) (2). Every one
constructs `std::string` to feed the RE2 C++ API:

```cpp
std::string pat(s->chars, s->len);
re2::RE2* re = new re2::RE2(pat, opts);
```

The RE2 C++ API takes `re2::StringPiece` / `absl::string_view` / `const
std::string&` — there's no `const char*, size_t` overload.

**Structural fix:** introduce one helper in `lambda/re2_wrapper.hpp`
that takes `(const char*, size_t)` and constructs an
`re2::StringPiece` *inside* (StringPiece is a non-owning view —
no `std::string` allocation). Then:

- Either delete `std::string` from these 12 sites and route through the
  helper (zero findings, real perf win — no copies).
- Or scope the `no-std-containers` rule's `files:` to exclude
  `**/re2_wrapper.{cpp,hpp}` only, accepting that std::string is
  unavoidable at the RE2 boundary specifically. Then `lambda/rb/rb_runtime.cpp`
  routes through the wrapper and stops importing the dependency
  directly.

**Recommendation:** option A (helper) is the project-direction-correct
fix — it deletes 12 `std::string` ctors from a hot path. Option B is the
30-second escape if A is judged out of scope this cycle.

### 3.4 `no-new-delete` (70 → ~10 real)

Five clusters, in decreasing scope-fix yield:

| Cluster | Files | Count | Disposition |
|---|---|---:|---|
| **RE2 ownership glue** | `lambda/re2_wrapper.cpp`, `lambda/rb/rb_runtime.cpp`, `lambda/py/py_stdlib.cpp` | 31 | RE2's API is `new re2::RE2(...)`. → Either route through a `re2_wrapper.cpp` factory + `ignores:` that one file, or accept the `// NEW_DELETE_OK: RE2 C++ API` marker. The structural choice is the same as §3.3. |
| **Placement-new patterns** | `lambda/lambda-data-runtime.cpp:44`, `radiant/graph_layout_types.hpp:42, 64`, `radiant/resolve_htm_style.cpp:1596,1637,1856,1963`, `radiant/layout_counters.cpp:35` | 9 | All `new (raw) Type(...)` — the placement form is the *correct* pattern (object lives in pool memory, only the constructor runs). → Refine rule to exclude `kind: new_expression` whose first child is a `parenthesized_expression` (placement-new). One YAML edit. |
| **`radiant/text_control.cpp`, `lambda/mark_editor.cpp`, `lambda/edit_bridge.cpp`, `lambda/input/markup/markup_parser.cpp`, `lambda/input/input.cpp`, `radiant/event_sim.cpp`** | various | 21 | Real `new`/`delete` on long-lived objects (singletons, editor sessions, parsers). These should migrate to placement-new into pool/arena storage. Per-class migration; ≈one PR per cluster. |
| **`radiant/layout_table.cpp`** | `TableMetadata` | 2 | Already uses `&lycon->scratch` as constructor arg, so the storage IS the scratch arena — but `new TableMetadata(&scratch, ...)` is still heap-`new`. → One placement-new conversion. |
| **`lambda/safety_analyzer.cpp:21`** | Singleton | 1 | Static lifetime — fine, mark with `NEW_DELETE_OK: process-lifetime singleton`. |

After the placement-new YAML fix (9 lines) and the re2_wrapper consolidation
(31 lines), residue is ≈30 real migration candidates, scoped to a
follow-up.

### 3.5 `alloca-static-size` (57 → ≈0 with one macro)

The rule's note explicitly says it's a syntactic heuristic — it cannot
verify that `argc`/`nargs`/`nops`/`nparams` are bounded at the
*caller* level. They are: every callsite is a MIR codegen path
where the argument count is the function's signature, capped by parser
limits.

**Concentration:**

| File | Hits | Pattern |
|---|---:|---|
| `lambda/js/js_runtime.cpp` | 13 | `alloca(argc * sizeof(Item))` |
| `lambda/js/js_mir_*_lowering.cpp` | 9 | `alloca(param_count * sizeof(MIR_var_t))` |
| `lambda/rb/transpile_rb_mir.cpp` | 7 | same shape |
| `lambda/transpile-mir.cpp` | 6 | same shape |
| `lambda/bash/transpile_bash_mir.cpp` | 2 | `alloca(item_count * sizeof(MIR_label_t))` |
| radiant gradient/font stops, shape pool, rb_class | 6 | `alloca(n * sizeof(stop_t))` — bounded by parser |
| miscellaneous | 14 | scattered |

**Structural fix:** introduce a single bounded-alloca helper in
`lib/lambda_alloca.h`:

```c
// Asserts size <= LAMBDA_ALLOCA_MAX (default 4 KiB) so silent stack
// overflow becomes a build/test failure instead of a UB crash.
#define LAMBDA_ALLOCA(n, elem) \
    ((assert((size_t)(n) * sizeof(elem) <= LAMBDA_ALLOCA_MAX), \
      (elem*)alloca((size_t)(n) * sizeof(elem))))
```

Then sweep all 57 sites to use `LAMBDA_ALLOCA(argc, Item)`. The rule
already excludes any call without the unguarded `n * sizeof(T)` shape;
once the helper is the only spelling, the rule simply sees zero hits.

This also closes the safety gap the rule was originally documenting:
"argc might be huge" — the assert makes that observable. Pair with a
test that constructs a 100k-arg call and watches it abort cleanly.

---

## 4. Band 3 — Address real findings

After Band 1 + 2 the corpus shrinks to roughly:

| Rule | Real findings | Fix |
|---|---:|---|
| `no-unsafe-libc-str` | 28 | `strcpy`/`strcat` into known-size buffers. Most safe-by-construction (e.g. `strcpy(env_name, "_rb__env")` into a buffer larger than the literal). → A `safe_strcpy(dst, src, dst_cap)` helper in `lib/str.h` + sweep. The unconditional benefit is matching the CLAUDE.md "no `std::` types" theme on the C side too — one cap-aware helper, no more raw strcpy. |
| `no-int-layout-decl` | 1 | `int old_size = flex_layout->lines[i].cross_size;` at `radiant/layout_flex.cpp:5174` — single fix, change to `float`. CLAUDE.md rule 11. |
| `unused-depth-param` | 6 | Per [vibe/Lambda_Lint.md §5.1](Lambda_Lint.md#51-shipped) and the rule note, these flag functions carrying a `depth`/`recursion_depth` parameter that is never compared. Radiant audit finding #13. → Add the guard (`if (depth > MAX_DEPTH) return;`) where recursion is the intent, or remove the param. `evaluate_calc_expression`, the `event_log_editing_*` chain (3 fn), and `clip_shapes_*` — each is a 1–3 line change, but each *also* documents a real recursion-bound gap. |

### Band 3 also includes the `ls-test-has-golden` (133) cluster

These are existing `.ls` test scripts without a sibling `.txt` golden.
Distribution:

| Subdir | Count | Convention |
|---|---:|---|
| `test/lambda/input/` | 55 | These *should* have goldens — they're parse/format roundtrip tests. Add the goldens. |
| `test/lambda/negative/` | 48 | These intentionally fail. Either: convert subtree to require `.err` instead of `.txt` (rule extension), or rename them all `_*.ls` to match the existing fixture convention. **Recommendation: extend the rule** — see below. |
| `test/lambda/typeset/` | 10 | Some are runners (`run_all_tests.ls`), some are integration. Runners should be renamed to a `runner_` prefix and excluded. |
| `test/lambda/wip/` | 9 | Work-in-progress. → Move to `_wip/` or rename `_*.ls`. They aren't shippable tests yet. |
| `test/lambda/chart`, `ui`, `ext`, `latex`, `proc*`, `typeset/layout/` | 11 | Generate goldens or rename per the existing prefix convention. |

**Structural fix:** extend
[.alint.yml](.alint.yml) `ls-test-has-golden` to accept either a
`.txt` (positive expected output) **or** a `.err` (expected-failure
marker) sibling:

```yaml
require:
  - kind: any_of
    paths:
      - "{dir}/{stem}.txt"
      - "{dir}/{stem}.err"
```

Then:
1. Sweep `test/lambda/negative/` to add `.err` files (they often already
   have one — just inconsistent extension/naming).
2. Move `test/lambda/wip/` under `test/lambda/_wip/` to opt out of the
   rule entirely.
3. Generate missing goldens for `input/` parser tests by running them
   once and capturing stdout (the standard `.txt`-update pattern from
   `make test-lambda-baseline`).

This shrinks 133 → estimated ≤20 genuine "test ships without a golden"
findings, which become real test-hygiene tasks.

### `large-stack-array` (36)

All bounded by `MAX_GRID_TRACKS` / `MAX_GRID_ITEMS` / `MAX_MULTICOL_BLOCKS`
/ a 4 KiB normalized-text buffer. These are real stack pressure on deep
layout recursion (radiant audit findings #14, #15) but bounded at
compile time.

**Structural fix:** move them to a per-layout-pass scratch arena, which
is already the pattern in `radiant/layout_table.cpp` (`lycon->scratch`).
Concretely:

- `radiant/layout_multicol.cpp` (10 sites) — allocate from `lycon->scratch`
  in `layout_multicol_block()`, free at pass end. Single PR per file is
  reasonable.
- `radiant/grid_*.hpp` (~20 sites in `EnhancedGridTrack`, `GridItemInfo`,
  `FracEntry`, `FlexEntry`, `phase3_base`, `min_floor`, `frozen`, etc.)
  — these are inside helper classes that the grid layout instantiates
  on the stack. Refactor to take a scratch-arena reference and
  allocate the data arrays there.
- Normalized-text buffers (`normalized_buffer[4096]`) in
  `radiant/intrinsic_sizing.cpp`, `radiant/layout_flex*.cpp` — already
  `static thread_local` in some places. Unify under a single
  per-thread scratch pad in `radiant/layout.hpp`.

This is the largest piece of mechanical work in the proposal (≈3 days)
but it's the structural fix the audit findings explicitly call for. Keep
the rule at `info` until done, then graduate to `warning`.

---

## 5. Band 4 — Inventory / research-grade findings

These don't have a structural fix; they're work-list inputs.

### 5.1 `unused-function` (173)

The rule is intentionally lexical (Stage 3 libclang USR verification was
deferred — see [Lambda_Lint.md §5.1](Lambda_Lint.md#51-shipped)). FP rate
is unknown but non-trivial because:
- Header-only `static inline` helpers (radiant grid hpp files, format-utils)
  legitimately have low caller counts.
- Functions called via macro-synthesized dispatch (`TEST_F`, etc.)
  evade the grep.
- `lambda/lambda.h:210` and `lib/format/format-utils.hpp` (~30 entries)
  suggest those headers are part of the public API and used by clients
  outside the lint scope.

**Structural action:** ship Stage 3 of
[Lambda_Lint.md §8.3](Lambda_Lint.md#83-the-hybrid-unused-function-check).
The libclang USR verification path is the right precision step. Until
then, the 173 list is a research input, not a sweep target — do not
suppress individually. Add a single `UNUSED_FUNCTION_OK:` marker to any
specific function you confirm is reachable via macro/dlsym/extern.

### 5.2 `todo-inventory` (92) + `ls-todo-inventory` (4)

By design — these are `info`-level backlog scans. No action.

---

## 6. Sequenced rollout

The order is chosen so each step measurably shrinks the next step's
work, and so the `make lint` exit code becomes green as early as
possible.

| Step | Work | What it changes |
|---|---|---|
| **1.** `no-tmp-path` fix (§2.1) | 1 line | -1 error |
| **2.** `retained-field-write` rule constraint (§2.2) | YAML | -1 error |
| **3.** `state-store-form-mirror` rule narrowed to assignments (§2.4) | YAML | -9 false positives |
| **4.** Sweep `static_cast<DomText*>` → `dom_as<>` (§2.5) | 10 sites | -10 errors |
| **5.** `state-store-{doc,form}` helper additions + sweep (§2.3, §2.4) | helpers + 10 sites | -10 errors → **gate is now green** |
| **6.** `js_bt_regex` → `lam::ArrayList` (§2.6 row 1) | 1 file | -13 errors |
| **7.** `layout_pass` snapshot → scratch arena (§2.6 row 2) | 1 file | -4 errors |
| **8.** Remaining `no-raw-alloc` sites (§2.6 row 3) | 5 sites | -5 errors → **0 errors** |
| **9.** `no-printf-debug` rule scope expansion (§3.1) | YAML | -1,100 warnings |
| **10.** `no-asymmetric-arg-null-check` rule tighten + matrix() fix (§3.2) | YAML + ~10 lines | -5 FP, +6 real fixes |
| **11.** `no-new-delete` placement-new exclusion (§3.4 row 2) | YAML | -9 warnings |
| **12.** `LAMBDA_ALLOCA` macro + sweep (§3.5) | helper + 57 sites | -57 warnings |
| **13.** RE2 wrapper consolidation (§3.3, §3.4 row 1) | 1 wrapper + sweep | -12 + -31 warnings |
| **14.** Remaining `no-printf-debug` residue → `log_*` then graduate to error | ~150 sites | gate guards future debug prints |
| **15.** `ls-test-has-golden` rule extension + sweep (§3.5/4) | YAML + goldens | -110 warnings |
| **16.** `no-unsafe-libc-str` helper + sweep (§4) | 1 helper + 28 sites | -28 warnings |
| **17.** `unused-depth-param` guards (§4) | 6 sites | -6 warnings |
| **18.** `large-stack-array` → scratch arena (§4) | 36 sites | -36 info |
| **19.** Ship `unused-function` Stage 3 (§5.1) | libclang pass | precision upgrade — find list shrinks |
| **20.** Remaining `no-new-delete` migration (§3.4 row 3) | per-cluster | -21 warnings |

After step **8**, `make lint` exits 0. After step **14**, the noise floor
is low enough that warnings can be reviewed in PRs. After step **19**, the
last opinionated rule has precision parity with the structural ones.

Steps 1–8 are roughly one working day. Steps 9–14 are roughly one
working week. Steps 15–20 are 2–3 weeks of mechanical work scoped to
their own PRs.

---

## 7. Rules that should change after this lands

The fix sequence above also implies five small follow-ups to the rule
infrastructure itself, recorded here so they don't get lost:

1. **Graduate `no-printf-debug` to `error`** once §3.1 + step 14 are
   complete. The current `warning` was scaffolding for the 1,266-finding
   backlog — once the scope is right and the residue is swept, there's no
   excuse for new debug prints.

2. **Graduate `large-stack-array` to `warning`** once §4 sweep lands.
   Same logic.

3. **Refine `no-asymmetric-arg-null-check`** as in §3.2 — or document
   the residual FP class as known. Either way, fix or document; don't
   leave the warning in its current "trusts every dereference" state.

4. **Refine `no-new-delete`** to exclude placement-new (§3.4 row 2),
   which is the *correct* pool/arena pattern this codebase uses.

5. **Refine `state-store-form-mirror`** to fire only on writes (§2.4),
   matching `state-store-doc-state-write`'s shape. Reads of mirror
   fields are by-design fast paths.

These are 5 short PRs against [utils/lint/rules/c-cpp/](utils/lint/rules/c-cpp/)
that the work above naturally produces; capturing them here avoids each
one being re-derived from scratch in its own review.

---

## 8. What this proposal explicitly is NOT

- Not a rewrite of the lint engine. [vibe/Lambda_Lint.md](Lambda_Lint.md)
  remains the architecture spec; this proposal lives downstream of it.
- Not a sweep that batch-adds `*_OK` markers. Suppression markers are
  for single-line audited exceptions — using them to silence
  thousand-line clusters is the anti-pattern this proposal is built to
  avoid.
- Not a CI/gate change. CI integration is deferred per
  [Lambda_Lint.md §3.5](Lambda_Lint.md#35-ci); the proposal above is a
  developer-workflow cleanup. Once it lands, the gate question becomes
  trivial.
- Not opinionated about *who* does the work. The bands are sized for
  parallelism: band 1 is one developer-day; bands 2–3 split cleanly across
  multiple PRs and authors; band 4 is a research backlog the team can
  pick up opportunistically.

The single biggest leverage is **step 9** — one YAML edit to
`no-printf-debug` deletes the largest cluster of noise in the report
and converts it from "ignored" to "reviewable + gateable." If only one
step from this proposal ships, step 9 is the one.
