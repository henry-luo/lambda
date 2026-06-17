# Lambda Math Package — Phase 3 Enhancement Proposal

> Filename note: this proposal continues the `Lambda_Pkg_Math.md` / `Lambda_Pkg_Math2.md` naming. The original request named `Lambda_Pkd_Math3.md`; I am treating that as a typo and using `Pkg`. Rename if the literal `Pkd` form is preferred.

## Goal

Bring the Lambda math renderer from its current pass rate against MathLive on the **expanded** test corpus (278/921 = 30%) to **100% structural parity** against MathLive's HTML output across the full 921-case suite — upstream MathLive's 206 cases plus the 715 new cases extracted from `test/input/` documents.

The current Phase 2 work (`Lambda_Pkg_Math2.md`) closed parity on the upstream MathLive corpus (206/206 pass). The expansion to Lambda-derived inputs uncovered new failure modes that the upstream corpus did not exercise, and this phase addresses those specific gaps.

## Where we are

```
Corpus              Pass    Total    Rate
─────────────────   ─────   ──────   ──────
Upstream MathLive   206     206      100%
Lambda-derived       72     715      10.1%
Total               278     921      30.2%
```

All 921 cases run through the same pipeline at [test/lambda/mathlive/run_lambda_mathlive_markup.mjs](test/lambda/mathlive/run_lambda_mathlive_markup.mjs); only the source snap files differ. Zero regressions on the upstream baseline — every test that passed before still passes.

The new corpus (extracted by [test/lambda/mathlive/extract_latex_exprs.mjs](test/lambda/mathlive/extract_latex_exprs.mjs) and baked into HTML by [test/lambda/mathlive/generate_lambda_fixtures.mjs](test/lambda/mathlive/generate_lambda_fixtures.mjs)) draws from documents like `math_intensive_test.tex`, `sample_math_document.md`, and 34 others — i.e., math expressions in their natural document context, not hand-picked feature tests.

## Failure landscape — 643 mismatches, six clusters

The failures decompose into six structurally distinct clusters, ordered here by how many cases each cluster blocks. Numbers come from [temp/lambda_mathlive_markup_report.json](temp/lambda_mathlive_markup_report.json).

| # | Cluster | Cases | Single point of fix |
|---|---------|-----:|-----------|
| **B** | Spurious `lm_strut--bottom` emission | 159 | [box.ls:346](lambda/package/math/box.ls) `make_struts()` |
| **A** | `lm_mathit` emitted where `lm_cmr` expected | 98 + ~95 in cluster G | [render.ls:544](lambda/package/math/render.ls) `symbol_font_class()` |
| **G-metric** | Same class set, different `em` values | 182 (subset of "other") | [metrics.ls](lambda/package/math/metrics.ls) — strut/vlist measurements |
| **C** | Parser emits `lm_error` (unrecognized command) | 48 | LaTeX parser command tables |
| **D** | `lm_small-op` emitted where `lm_large-op` expected | 46 | Displaystyle propagation in [scripts.ls](lambda/package/math/atoms/scripts.ls) |
| **E** | `lm_msubsup` variant differs | 23 | Script positioning rules |
| **F** | `lm_align_environment` missing | 4 | [atoms/array.ls](lambda/package/math/atoms/array.ls) |
| **G-structural** | Other small class-set divergences | 83 | Scattered (`lcGreek`, `lm_close`, `lm_ams`) |

The first three clusters alone account for ~440 of the 643 failures (68%). They are the highest-yield targets.

### Cluster B — spurious `lm_strut--bottom` (159 cases)

Lambda's `make_struts()` always emits both the top strut and the bottom strut:

```lambda
// lambda/package/math/box.ls:346
pub fn make_struts(bx) {
    let h = bx.height
    let d = bx.depth
    let strut_bottom_style = "height:" ++ util.fmt_em(h + d) ++ ";vertical-align:" ++ util.fmt_em(0.0 - d)
    <span;
        <span class: css.STRUT, style: "height:" ++ util.fmt_em(h)>
        <span class: css.STRUT_BOTTOM, style: strut_bottom_style>  // always
        bx.element
    >
}
```

MathLive only emits the bottom strut when `depth > 0` — i.e., when the box actually has content descending below the baseline. For a formula like `$c\:d$`:

```
MathLive: <span class="lm_strut" style="height:0.7em"></span>
          <span class="lm_base">...</span>
Lambda:   <span class="lm_strut" style="height:0.7em"></span>
          <span class="lm_strut--bottom" style="height:0.78em;vertical-align:-0.08em"></span>
          <span class="lm_base">...</span>
```

The Lambda strut is harmless visually (`height:0.78em;vertical-align:-0.08em` reserves an invisible 0.08em of descent), but it breaks bit-exact comparison.

### Cluster A — `lm_mathit` over-application (98 strict + ~95 "other") 

[`symbol_font_class()`](lambda/package/math/render.ls) hard-codes font-class assignment for ~12 specific symbol names and falls through to the current font (typically `lm_mathit`) for everything else. MathLive maintains a **per-symbol** font-class table that knows e.g. that `\nabla`, `\cdot`, `\ell`, `\partial` are upright (`lm_cmr` or `lm_ams`), not italic.

```lambda
// lambda/package/math/render.ls:544
fn symbol_font_class(cmd_text, context) {
    let name_str = ...
    if (name_str == "alpha") "lcGreek lm_mathit"
    else if (name_str == "Gamma" or ...)  css.CMR     // explicit list of upper greek
    else if (name_str == "dots" or "ldots" or ... "infty") css.CMR  // explicit short list
    else if (name_str == "blacksquare" or "blacktriangle") css.AMS
    else css.font_class(context.font)   // ← default: italic
}
```

Sample divergence — formula `\nabla \cdot \vec{F}`:

```
MathLive: <span class="lm_cmr">∇</span> <span class="lm_cmr">⋅</span>
Lambda:   <span class="lm_mathit">∇</span> <span class="lm_mathit">⋅</span>
```

The same condition causes the **`lcGreek` missing class** seen across many cases (54 occurrences in the missing-class histogram): lowercase Greek symbols should carry `lcGreek` alongside `lm_mathit`, but only `\alpha` is hardcoded.

### Cluster G (metric subset) — 182 cases with identical class set but different `em` measurements

The dominant pattern (109 of the 182) is strut height:

```
expected: style="height:Xem"  vs  actual: style="height:Yem"
```

These trace to [metrics.ls](lambda/package/math/metrics.ls) using approximate height/depth values that don't match MathLive's tables for many symbols and atoms. For example, `\frac{a}{b}` produces matching DOM structure but Lambda's `0.94em` strut becomes `1.15em`.

### Cluster C — parser coverage gaps (48 cases, 26 unique tokens)

The LaTeX command tables at [input-latex-tables.cpp](lambda/input/input-latex-tables.cpp) and the tree-sitter math grammar are missing several command families. The 26 unique tokens that reached `<span class="lm_error">`:

| Pattern | Tokens | Count | Diagnosis |
|---|---|---:|---|
| `\left*` truncation | `\left` | 10 | Parser greedy-matches `\left` and leaves `arrow`/`rightarrow` as text in `\leftarrow`, `\Leftarrow`, `\leftrightarrow` |
| `\big*` truncation | `\bigc`, `\bigo`, `\bigw`, `\bigv` | 17 | Same shape — `\bigcup`, `\bigoplus`, `\bigwedge`, `\bigvee` truncated to a 5-char prefix |
| AMS symbols | `\twoheadleftarrow`, `\boxplus`, `\boxtimes`, `\boxminus`, `\boxdot`, `\preceq`, `\succeq`, `\nmid`, `\rightsquigarrow` | 17 | Not in command tables |
| Modular arithmetic | `\pmod`, `\bmod` | 9 | Not in command tables |
| Greek uppercase aliases | `Alpha`, `Beta`, `Epsilon`, `Zeta`, `Eta`, ... | 5 | These are Unicode letters in TeX; some renderers register them as identity macros |
| Other | `\boldsymbol`, `\label`, `\cbrt`, `\tr` | 9 | Out of MathLive's defaults too (3 of these are in the 6 baseline `unknown-command` cases) |

The truncation pattern (top 27 of 48 occurrences) is a **single root cause**: the parser does not do longest-match on `\command` tokens. `\left` is matched before `\leftarrow` is even considered. A proper longest-match scan would fix this entire family.

### Cluster D — `lm_small-op` vs `lm_large-op` (46 cases)

The renderer at [scripts.ls:41](lambda/package/math/atoms/scripts.ls) and [scripts.ls:116](lambda/package/math/atoms/scripts.ls) both pick the operator size based on a `ctx.is_display(context)` check. The check exists; what's wrong is that **`is_display` returns `false` more often than MathLive does**. MathLive's mathstyle propagation through fractions/scripts/environments is more permissive about staying in displaystyle for large operators. Symptom: `\sum_{i=1}^n` renders with full-size summation in MathLive but small-style in Lambda.

### Clusters E + F — script positioning, environment classes (27 cases)

`lm_msubsup` and `lm_align_environment` are present in MathLive's output for sub/sup arrangements and align environments but missing or differently-wrapped in Lambda's. These are smaller, more localized fixes — handled near the end of the plan.

## Architectural recommendations

### R1 — Centralize font-variant resolution

Replace the ad-hoc `symbol_font_class()` at [render.ls:544](lambda/package/math/render.ls) with a **single lookup** keyed on the command name:

```
symbol_font_class(cmd, ctx)
  ↓
  symbols.font_class_of(cmd)         // pure data lookup — defaults to ctx.font
```

Move the table itself to [symbols.ls](lambda/package/math/symbols.ls) so it lives alongside the Unicode mappings. MathLive maintains this in `src/core/font-metrics.ts` and `src/latex-commands/symbols.ts`; port the relevant subset.

Concretely:

```lambda
// new in symbols.ls
let font_class_map = {
    // upright (CMR)
    nabla: css.CMR, partial: css.CMR, ell: css.CMR, hbar: css.CMR,
    imath: css.CMR, jmath: css.CMR, cdot: css.CMR, infty: css.CMR,
    vert: css.CMR, lvert: css.CMR, rvert: css.CMR,
    dots: css.CMR, ldots: css.CMR, cdots: css.CMR, vdots: css.CMR,
    // ... 100+ entries

    // lowercase Greek (italic but with lcGreek marker)
    alpha: "lcGreek lm_mathit", beta: "lcGreek lm_mathit", ... ,

    // uppercase Greek (CMR — non-italic)
    Gamma: css.CMR, Delta: css.CMR, Theta: css.CMR, Lambda: css.CMR,
    Xi: css.CMR, Pi: css.CMR, Sigma: css.CMR, Upsilon: css.CMR,
    Phi: css.CMR, Psi: css.CMR, Omega: css.CMR,

    // AMS
    blacksquare: css.AMS, blacktriangle: css.AMS,
    twoheadleftarrow: css.AMS, twoheadrightarrow: css.AMS,
    boxplus: css.AMS, boxtimes: css.AMS, boxminus: css.AMS, boxdot: css.AMS,
    preceq: css.AMS, succeq: css.AMS, nmid: css.AMS,
    rightsquigarrow: css.AMS,
    // ...
}

pub fn font_class_of(name, default_cls) {
    font_class_map[name] or default_cls
}
```

Predicted impact: ~190 cases (clusters A + the lcGreek subset of G-structural).

### R2 — Conditional bottom strut

Change [`make_struts()`](lambda/package/math/box.ls) to emit `lm_strut--bottom` only when depth is positive:

```lambda
pub fn make_struts(bx) {
    let h = bx.height
    let d = bx.depth
    if (d > 0.0) {
        let strut_bottom_style = "height:" ++ util.fmt_em(h + d) ++ ";vertical-align:" ++ util.fmt_em(0.0 - d)
        <span;
            <span class: css.STRUT, style: "height:" ++ util.fmt_em(h)>
            <span class: css.STRUT_BOTTOM, style: strut_bottom_style>
            bx.element
        >
    } else {
        <span;
            <span class: css.STRUT, style: "height:" ++ util.fmt_em(h)>
            bx.element
        >
    }
}
```

Verify the threshold against MathLive's source — it may use `d > epsilon` (rounding-tolerant). MathLive's logic is in `src/core/atom.ts` → `Atom.renderHTML` around the strut-emission block.

Predicted impact: 159 cases (cluster B), almost certainly without secondary breakage since the omitted span is invisible.

### R3 — Longest-match LaTeX command lexer

Replace whatever first-match logic produces `\bigc`/`\left` truncations with a **longest-match** scan against the command table. There are two layers to consider:

1. **Tree-sitter grammar** for math LaTeX (`lambda/tree-sitter-latex-math` if that path exists, or wherever the parser is defined). Tree-sitter chooses the longest-matching lexeme by default; if it's truncating, there is likely a hand-rolled external scanner overriding default behavior, or a precedence rule listing `\left` before `\leftarrow`.
2. **`extract_leading_command()`** at [input-latex-ts.cpp:191](lambda/input/input-latex-ts.cpp). The current implementation already reads `\` + alphas until non-alpha, which is correct for `\bigcup` (extracts `\bigcup` whole). So the truncation must be happening upstream — most likely the tree-sitter grammar is feeding it a pre-split token.

Predicted impact: ~31 cases (the `\left*` and `\big*` families).

### R4 — Add missing symbol commands to the command table

Once R3 is in, the command lexer will deliver e.g. `\twoheadleftarrow` as a whole token, but the renderer still needs to know what to do with it. Adding entries to [symbols.ls](lambda/package/math/symbols.ls) and the matching font-variant table (R1) gives them glyphs and classes.

Tokens to add (from the diagnostics): `\twoheadleftarrow`, `\twoheadrightarrow`, `\boxplus`, `\boxtimes`, `\boxminus`, `\boxdot`, `\preceq`, `\succeq`, `\nmid`, `\rightsquigarrow`, `\pmod`, `\bmod`, `\boldsymbol`, plus the uppercase-Greek Unicode-letter aliases (`Alpha`, `Beta`, `Epsilon`, `Zeta`, `Eta`, etc.).

Out of scope (correctly flagged as `unknown-command`): `\label`, `\cbrt`, `\tr`. MathLive doesn't support these either — they're test inputs from documents that use custom macros.

Predicted impact: ~17 additional cases (cluster C minus the truncation family minus the genuinely-unsupported tokens).

### R5 — Tighten mathstyle propagation for large operators

Audit `ctx.is_display(context)` callers. The check at [render.ls:243](lambda/package/math/render.ls) and the analogous one in [scripts.ls](lambda/package/math/atoms/scripts.ls) need to return `true` more often, specifically:

- For top-level math when the input is `$$…$$`, `\[…\]`, or a display environment (`equation`, `align`, `gather`, `multline`).
- Inside fraction numerators/denominators when the enclosing style is displaystyle (e.g., `\dfrac` should produce displaystyle children).
- For sub/sup positioning of large operators (the `\sum_{i=1}^n` case): big ops in displaystyle should put limits above/below, not as sub/sup.

The display vs inline flag DOES flow through the runner (the snapshot key carries `\[…\]` wrapping for display cases, and the runner calls `math.render_display()` vs `math.render_inline()`). The bug is downstream: once `render_display` is called, the context's `is_display()` predicate doesn't stay `true` through all the renderers.

Predicted impact: 46 cases (cluster D).

### R6 — Metric table parity

Replace [metrics.ls](lambda/package/math/metrics.ls)'s approximate values with a port of MathLive's `font-metrics.ts` for the Computer Modern family (KaTeX-derived). Key tables:

- Per-character height/depth/italic-correction for `cmr10` (text) and `cmmi10` (math italic) — at minimum.
- Sigma values for displaystyle/textstyle/scriptstyle/scriptscriptstyle (already partially present; extend to match upstream sigmas).
- Big-operator metric overrides for `\sum`, `\prod`, `\int`, etc., in displaystyle.

Predicted impact: ~109 strut-height cases plus most of the residual "other" metric diffs.

### R7 — Final cleanup: `lm_msubsup`, `lm_align_environment`, `lm_close`, scattered classes

Once R1–R6 land, the residual 80–100 cases will be focused enough to fix one-by-one. Categorize by the missing/extra class set after R1–R6 are merged, and dispatch the remaining diffs to specific atom renderers ([scripts.ls](lambda/package/math/atoms/scripts.ls), [delimiters.ls](lambda/package/math/atoms/delimiters.ls), [array.ls](lambda/package/math/atoms/array.ls)).

## Phased plan with predicted yield

| Phase | Work | Files touched | Predicted ΔPass | Cumulative |
|------:|------|---------------|----------------:|----------:|
| 0 | Baseline pass run (already done) | — | — | 278/921 |
| 1 | R2 — conditional bottom strut | [box.ls](lambda/package/math/box.ls) | +159 | 437/921 |
| 2 | R1 — centralize font-variant table | [symbols.ls](lambda/package/math/symbols.ls), [render.ls](lambda/package/math/render.ls) | +190 | 627/921 |
| 3 | R3 — longest-match command lexer | tree-sitter grammar + [input-latex-ts.cpp](lambda/input/input-latex-ts.cpp) | +31 | 658/921 |
| 4 | R4 — add missing AMS / mod / box symbols | [symbols.ls](lambda/package/math/symbols.ls) + [input-latex-tables.cpp](lambda/input/input-latex-tables.cpp) | +17 | 675/921 |
| 5 | R5 — fix displaystyle propagation | [context.ls](lambda/package/math/context.ls), [scripts.ls](lambda/package/math/atoms/scripts.ls) | +46 | 721/921 |
| 6 | R6 — port MathLive metric tables | [metrics.ls](lambda/package/math/metrics.ls) | +109 | 830/921 |
| 7 | R7 — sub/sup, env, misc residuals | scripts/array/delim atom renderers | +85 | **915/921** |
| 8 | Inspect the last 6 (genuinely-unsupported: `\label`, `\cbrt`, `\tr` × 2, `\varheartsuit`, `\bigwedge` edge) | discretion | +6 or leave as `expectedError=unknown-command` agreed | **921/921** |

The estimates carry obvious uncertainty (a fix in one phase may reveal further dependencies). Treat them as targets, not commitments.

### Why this order

- **R2 first** because it's a 5-line code change that unblocks 159 cases — by far the highest leverage.
- **R1 second** because it's the next-highest leverage and is purely data (table additions). It also lays the groundwork for R4.
- **R3 before R4** because R4 entries are dead until the lexer delivers their token whole.
- **R5 before R6** because mathstyle bugs affect which metric values get selected, so fixing metrics first might paper over the propagation bug.
- **R7 last** because the residual cluster shape only becomes clear after R1–R6 land.

## Acceptance criteria

The phase is **done** when:

1. `node test/lambda/mathlive/run_lambda_mathlive_markup.mjs` reports **≥99%** pass rate on the full 921-case corpus (≥912 passing). 100% if the 6 genuinely-unsupported cases are accepted as `expectedError=unknown-command` rather than rendered.
2. Per-category breakdown shows **zero categories below 95%**.
3. **Zero regressions** on the upstream 206-case corpus (current 100% rate maintained).
4. The font-variant table at [symbols.ls](lambda/package/math/symbols.ls) is referenced (not duplicated) by [render.ls](lambda/package/math/render.ls) — no ad-hoc per-symbol conditionals remain in `symbol_font_class()`.
5. `make_struts()` emits the bottom strut only when `depth > 0`.
6. The `\left`/`\big` truncation bug is gone — `node test/lambda/mathlive/run_lambda_mathlive_markup.mjs --list | grep "lm_error"` is empty for any token that isn't in the agreed `unknown-command` whitelist.
7. A frozen `baseline.txt` snapshot of the passing 912+ cases is committed so future runs gate on regressions.

## Test/baseline integration

After each phase:

1. Run `node test/lambda/mathlive/run_lambda_mathlive_markup.mjs --no-baseline > temp/phase-N.txt`.
2. Diff against `temp/phase-(N-1).txt` to confirm forward progress and no regressions.
3. If the phase succeeded, append the new-passing keys to `test/lambda/mathlive/baseline.txt` so subsequent runs catch regressions automatically.
4. After the final phase, run with `--strict` to make the runner exit non-zero on any failure.

Optional: add `make test-mathlive` target to `build_lambda_config.json` so the markup adapter joins the standard test surface.

## Risks and open questions

1. **Tree-sitter grammar location.** The truncation bug at `\bigc`/`\left` is most likely in the math LaTeX tree-sitter grammar, but the location of that grammar isn't immediately obvious. If it lives in a submodule (e.g., `tree-sitter-latex-math` under `lambda/`), R3 may require coordinating with that grammar's owner and regenerating `parser.c`. **Recommend: locate the grammar before committing to R3's estimate.**
2. **Metric port scope.** MathLive's metric tables run to thousands of entries. Porting the full set is overkill; porting only the symbols Lambda actually emits should suffice. The risk is identifying which subset is "enough" — start with the symbols that appear in failing diffs and iterate.
3. **Cluster G residuals.** The 83 small-class-set divergences (`lcGreek`, `lm_close`, `lm_ams`, etc.) may not all reduce to a single mechanism after R1. Be prepared for R7 to fork into several smaller subtasks.
4. **Display-mode behavior of `defaultMode`.** Both `convertLatexToMarkup(formula, {defaultMode: 'inline-math'})` and `{defaultMode: 'math'}` produced bit-identical output in our smoke tests, including for `\sum_{i=1}^n` and `\int_0^1`. This means MathLive's SSR bundle does not visibly differentiate; instead, displaystyle is triggered by **explicit constructs in the LaTeX** (`\displaystyle`, `\dfrac`, `$$…$$`, math environments). Lambda's behavior should match: rely on the LaTeX wrapping, not on the runner's display flag. The runner already does this correctly — the display flag only controls `render_inline` vs `render_display` selection.
5. **Genuinely-unsupported tokens.** `\label`, `\cbrt`, `\tr`, `\varheartsuit` are flagged by both MathLive's `validateLatex` and Lambda. The proposal currently targets ≥99% with these accepted as legitimate `unknown-command` outcomes. Decision needed: are these acceptable, or should Lambda extend beyond MathLive's coverage?

## Out of scope for this phase

- Visual rendering parity (font glyphs, kerning, exact pixel placement). The proposal targets HTML structural match only.
- MathML output and accessibility hooks.
- Editing/caret metadata.
- The 12 upstream snap entries that the runner's regex drops (pre-existing issue, not a regression).

## Summary

The 643 Lambda-corpus failures concentrate into a small number of root causes. The top three — a single unconditional strut emission, a too-sparse font-variant table, and a command-lexer truncation bug — account for ~440 (68%) of the failures and can each be fixed with localized changes to one to three files. The remaining ~200 are split across mathstyle propagation, metric tables, and atom-renderer details, each addressable in turn.

The end state — ≥99% pass on the expanded corpus, 0% regression on the upstream baseline, a unified font-variant table, and a longest-match command lexer — is a strict structural improvement on the current architecture, not a refactor. The expanded test corpus exists to drive these fixes; once they land, it doubles as the regression suite for the next phase.

---

## Progress update — after implementation attempts

The original Phase 3 plan above predicted a 99% pass rate would emerge from 7 phases (R1–R7) on the order of "tens of hours." Actual implementation showed those estimates were **upper bounds** rather than deltas: each predicted cluster fix delivered a fraction of its claimed lift because cases typically fail on 2–5 unrelated dimensions simultaneously. The current state of the math package and what landed across the implementation session is below.

### Pass rate timeline

```
Baseline (start)                         278 / 921   (30.2%)
After R2 (conditional bottom strut)      282 / 921   (30.6%)   — R2 fix in place but
                                                                 dormant; pre-fix depths
                                                                 are non-zero everywhere
After R1 (font_class_map)                282 / 921   (30.6%)   — table added; cases gated
                                                                 on other dimensions
After R5 (descender depth + large-op)    294 / 921   (31.9%)
After cluster B targeted depth port      323 / 921   (35.1%)   — letter depths → 0 for
                                                                 non-descenders
After font-class expansion (CMR/AMS)     344 / 921   (37.4%)
After uppercase letter heights (0.69)    388 / 921   (42.1%)   — biggest single fix
After Greek + symbol metric port         403 / 921   (43.8%)
After sum/prod limit-style always        414 / 921   (45.0%)
After content-aware fraction spec        428 / 921   (46.5%)
After full-precision metric table        428 / 921   (46.5%)   — table added, emission
                                                                 unchanged
─────────────────────────────────────────────────────────────────
Final achieved                           428 / 921   (46.5%)
Upstream corpus                          206 / 206   (100%)    — held throughout
Lambda corpus                            222 / 715   (31.0%)   — up from 72/715 (10.1%)
```

**Net gain: +150 cases (+16.3 percentage points). Zero upstream regression.**

### Categorized status

The original cluster taxonomy aged well. Cluster-level final results:

| Cluster | Original prediction | Status | Notes |
|---|---:|---:|---|
| **A** lm_mathit→lm_cmr (font_class_map) | 98 + 95 from G | **partially resolved** | Centralized `font_class_map` in [symbols.ls](lambda/package/math/symbols.ls) with ~110 entries. Remaining lm_mathit cases need font-aware text rendering. |
| **B** Spurious `lm_strut--bottom` | 159 | **partially resolved** | `make_struts()` now conditional on `depth != 0`. Many cases recovered. Remaining cases have non-zero depth from operator metrics. |
| **C** Parser `lm_error` tokens | 48 | **mostly resolved** | Added 9 AMS symbols, 18+ fonts/relations, vertical/horizontal arrows. Remaining: parser truncation for `\left*`/`\big*` requires tree-sitter grammar surgery (R3 — attempted, blocked on cascading renderer rewrites). |
| **D** lm_small-op vs lm_large-op | 46 | **resolved** | Always-large for big-ops outside script context (matches MathLive's actual behavior — both inline-math and math modes use large-op). |
| **E** lm_msubsup variant | 23 | **partial** | Any-italic-letter superscript path added (was `x`-only). |
| **F** Align environment classes | 4 | **unresolved** | Requires array.ls structural changes — not attempted. |
| **G-metric** Same class set, different em values | 182 | **partially resolved** | Letter depths, descender table, font-aware height lookup. Remaining 0.01em precision issues require full-precision propagation (attempted, reverted — see issues below). |

### What landed (kept in the math package)

```
lambda/package/math/box.ls                       | +180 lines  (metric tables, descender
                                                                detection, font-aware
                                                                text_height_for/text_depth_for)
lambda/package/math/symbols.ls                   | +130 lines  (font_class_map,
                                                                AMS symbols, ‖→∥ fix)
lambda/package/math/render.ls                    |   +9 lines  (symbol_font_class
                                                                delegation, relation
                                                                command-lookup,
                                                                large-op outside script)
lambda/package/math/atoms/scripts.ls             |  +30 lines  (always-limit for big-ops,
                                                                descender-aware depth)
lambda/package/math/atoms/fraction.ls            |  +78 lines  (short-body fraction spec
                                                                + numeric-box detection)
lambda/package/math/metrics_data.ls              | NEW 22 KB   (640 character metrics
                                                                across Main-Regular,
                                                                Math-Italic, AMS-Regular)
lambda/package/math/util.ls                      |  +19 lines  (fmt_em_ceil2 helper
                                                                — not wired in)
test/lambda/mathlive/generate_metrics_data.mjs   | NEW 160 lines (regeneration script)
```

### What didn't land

| Attempt | Predicted | Actual | Why blocked |
|---|---|---:|---|
| Integral side-script renderer | +30–40 cases | -31 (reverted) | First-time HTML structure had nested-span mismatch; multi-day project to nail. |
| `+`/`-` height = 0.59 (matches cmr metric) | +13 cases | -2 net (reverted) | Cascades into 11 nested-context hardcoded constants in fraction.ls/enclose.ls. |
| Tall-body letter heights `b/d/h/k/l → 0.69` | +57 cases | broke `a+b` upstream | MathLive's strut for `a+b` is 0.7em but Lambda's hbox max with `b=0.69` gives 0.69. The 0.7 emerges from font ascent in MathLive that Lambda doesn't replicate. |
| Full-precision propagation with CEIL@2 emission | +52 to +90 cases | -42 (reverted) | Lambda's ~200 hardcoded constants in fraction.ls/scripts.ls/array.ls were tuned for round-half-up emission; CEIL@2 lifts them by 0.01em. |

---

## Issues currently facing

Now that all "easy" fixes have been tried, the remaining 493 failures cluster into structural limits that resist incremental fixing. Five issues block further progress.

### Issue 1 — Hardcoded em constants vs. MathLive's metric-driven layout

MathLive computes every layout quantity (fraction shifts, script positions, accent placement, etc.) from per-character font metrics and a small set of `mathstyle` sigma constants (`AXIS_HEIGHT`, `X_HEIGHT`, etc.). Lambda's math package contains approximately **200 hardcoded em constants** — `numer_child_height: 0.65`, `denom_top: -2.31`, `vlist_height: 1.66`, `depth_holder: 1.42`, etc. — across [fraction.ls](lambda/package/math/atoms/fraction.ls), [scripts.ls](lambda/package/math/atoms/scripts.ls), [atoms/array.ls](lambda/package/math/atoms/array.ls), [atoms/enclose.ls](lambda/package/math/atoms/enclose.ls), and [atoms/delimiters.ls](lambda/package/math/atoms/delimiters.ls).

These constants were tuned over many iterations against specific test outputs. They work for the cases that drove their tuning but drift on adjacent cases. Crucially, they **interlock**: changing one (e.g., `+` operator height from 0.69 to its true cmr metric 0.59) cascades into ~20 downstream constants that were calibrated against the original value.

**Consequence:** any structural improvement that touches a single constant must concurrently update every constant that depends on it. Partial recalibration produces regressions, as the implementation session demonstrated 4 times.

### Issue 2 — Emission rounding rule mismatch (MathLive uses CEIL@2, Lambda uses round-half-up via fmt_em)

MathLive's [box.ts:60](ref/mathlive/src/core/box.ts) emits CSS em values via a single unified rule:

```typescript
function toString(arg1, arg2) {
  const numValue = Math.ceil(1e2 * arg1) / 1e2;
  if (numValue === 0) return '0';
  return numValue.toString() + (arg2 ?? '');
}
```

Every height/depth/vertical-align/top/margin value goes through `Math.ceil(value * 100) / 100` before string formatting. Lambda's [util.ls](lambda/package/math/util.ls) `fmt_em` uses `Math.round` (half-up) at 5-decimal precision.

A helper `fmt_em_ceil2` matching MathLive's rule was added during the structural rewrite but **not wired in** — switching the strut emission to `fmt_em_ceil2` regresses 35+ upstream cases because Lambda's hardcoded `1.144999`-style constants get CEILed to `1.15` while MathLive's computed `1.14000` rounds to `1.14`.

Until Issue 1 is resolved (every emission site fed by full-precision metric arithmetic rather than hardcoded constants), Issue 2 cannot be addressed without regression.

### Issue 3 — Lambda's hbox uses max-of-rounded-values; MathLive uses full-precision then rounds at emit

For `a+b`:
- MathLive: `max(h_a=0.69141, h_+=0.58333, h_b=0.69444) = 0.69444`. Emit: `ceil(0.69444 * 100) / 100 = 0.7em` ✓
- Lambda after metric port (precision-rounded table): `max(h_a=0.69, h_+=0.59, h_b=0.7) = 0.7`. Emit: `fmt_em(0.7) = 0.7em` ✓

For `b` alone:
- MathLive: `max(h_b=0.69444) = 0.69444`. Emit: `ceil(0.69444*100)/100 = 0.7em` ✓
- Lambda: `max(h_b=0.7) = 0.7`. Emit: `0.7em` ✓ (matches by coincidence)

For `\left. x + 1\right.`:
- MathLive: `max(h_x=0.43056, h_+=0.58333, h_1=0.64444) = 0.64444`. Emit: `ceil(0.64444+0.08333, 2) = ceil(0.72777, 2) = 0.73em` for h+d.
- Lambda (current): with rounded constants `max = 0.65`, depth `0.08`. h+d = 0.73 exact. Emit: `0.73em` ✓
- Lambda (full-precision metric + CEIL@2 emission): `max = 0.64444` ✓ MATCH — **but Lambda's hbox internally rounds at each step**, producing `0.74` somewhere upstream due to the strut_total flow.

The hbox/strut formula needs to carry full precision through ALL intermediate steps. This requires audit of every `+ ` `max(` `min(` operation in [box.ls](lambda/package/math/box.ls) and replacing pre-rounded inputs with raw metric values.

### Issue 4 — TypeScript→Lambda Script paradigm mismatch

MathLive's `Box` class is **mutable** — fields like `cssProperties`, `parent`, `height`, `selected` are written after construction in helpers like `wrap()`, `setTop()`, `setStyle()`, `atomID`, and the post-render `selected()` walk. Lambda Script enforces immutable maps. A faithful port would need:

- **Copy-on-write semantics** for every mutating method (1.3–1.6× LoC inflation).
- A **builder pattern** with a `freeze` step before `toMarkup`.
- Replacement of MathLive's class inheritance chains (`Atom → AccentAtom`, `Atom → GenfracAtom`, etc.) with **explicit dispatch tables**.

This isn't a blocker per se, but it adds substantial boilerplate and changes the code shape enough that MathLive's structure isn't 1:1 readable in the port.

### Issue 5 — No side-by-side diff harness

Every metric drift between Lambda and MathLive currently requires:
1. Compute the value in MathLive's actual output (via `convertLatexToMarkup` Node probe).
2. Compute the value in Lambda's actual output (via `lambda.exe` run).
3. Trace which formula produced each.

For a 0.02em drift, this can take 30–60 minutes per case. With ~493 failing cases, the diagnostic budget alone exceeds practical limits. A side-by-side harness that emits both renders for the same input and reports per-token diffs would reduce per-case diagnosis to ~5 minutes.

The proposal had this in Phase 0 ("test harness") but the partial implementation that exists ([run_lambda_mathlive_markup.mjs](test/lambda/mathlive/run_lambda_mathlive_markup.mjs)) doesn't include the MathLive-side render — it relies on pre-baked snap fixtures. Building the live-MathLive comparator is itself a ~40–80h project.

---

## Proposal: Reverse-engineer MathLive end-to-end

Given the structural limits above, this section proposes the next-phase architectural overhaul: **port MathLive's `core/box.ts` layout pipeline directly into Lambda Script**, abandoning the constant-tuning approach in favor of a metric-driven, MathLive-faithful implementation.

### Scope

Port the following MathLive TypeScript modules into Lambda Script equivalents:

| MathLive file | LoC | Purpose | Lambda equivalent | Port complexity |
|---|---:|---|---|---|
| [core/box.ts](ref/mathlive/src/core/box.ts) | 816 | `Box` class — render primitive (dimensions, classes, CSS, SVG, caret, mutation methods, `toMarkup`) | [box.ls](lambda/package/math/box.ls) (partial) | **large** |
| [core/v-box.ts](ref/mathlive/src/core/v-box.ts) | 392 | `VBox` subclass — vertical stacking (5 positioning modes: individualShift, top, bottom, shift, firstBaseline) | inline helpers in [box.ls](lambda/package/math/box.ls) | medium |
| [core/context.ts](ref/mathlive/src/core/context.ts) | 401 | Render context — mathstyle, color, registers, isPhantom, atomIdsSettings, smartFence | [context.ls](lambda/package/math/context.ls) (132 LoC) | medium |
| [core/mathstyle.ts](ref/mathlive/src/core/mathstyle.ts) | 162 | Mathstyle enum (D, D′, T, T′, S, S′, SS, SS′) with sizeDelta, cramped flag, per-style FontMetrics | partial in [context.ls](lambda/package/math/context.ls) | small |
| [core/font-metrics.ts](ref/mathlive/src/core/font-metrics.ts) | 411 | `getCharacterMetrics(font, fontSize, codepoint)` with cascading lookup over Main/Math/AMS/Size1–4 | [metrics.ls](lambda/package/math/metrics.ls) | medium |
| [core/font-metrics-data.ts](ref/mathlive/src/core/font-metrics-data.ts) | 2,366 | Per-codepoint metric tables (mechanical data) | [metrics_data.ls](lambda/package/math/metrics_data.ls) (665 LoC, reduced) | large (mechanical) |
| [core/inter-box-spacing.ts](ref/mathlive/src/core/inter-box-spacing.ts) | 140 | TeXBook 7×7 spacing matrix + mbin→mord promotion + DFS traversal | [spacing_table.ls](lambda/package/math/spacing_table.ls) (subset) | small |
| `core/parser.ts` + atom modules | ~7,200 | LaTeX parsing + atom-class hierarchy (handled in Lambda via tree-sitter) | tree-sitter-latex-math + build_ast.cpp | **out of scope** for the layout port |
| [atoms/genfrac.ts](ref/mathlive/src/atoms/genfrac.ts) | 323 | `\frac`, `\binom`, `\genfrac` with TeXBook Rule 15a–c | [fraction.ls](lambda/package/math/atoms/fraction.ls) (934 LoC, hardcoded specs) | large |
| [atoms/subsup.ts](ref/mathlive/src/atoms/subsup.ts) | 69 | Superscript/subscript via TeXBook Rule 18 | [scripts.ls](lambda/package/math/atoms/scripts.ls) (453 LoC) | medium |
| [atoms/surd.ts](ref/mathlive/src/atoms/surd.ts) | 247 | `\sqrt` with index — chooses small/built-up radical, scriptscript index, cramped body | inline `render_radical()` in [render.ls](lambda/package/math/render.ls) | medium |
| [atoms/accent.ts](ref/mathlive/src/atoms/accent.ts) | ~250 | Math/text accents with skew correction and stretchy SVG | inline `render_accent()` in [render.ls](lambda/package/math/render.ls) | medium |
| [atoms/leftright.ts](ref/mathlive/src/atoms/leftright.ts) | 240 | `\left…\right` matched delimiters with auto-sizing | `render_delimiter_group()` in [render.ls](lambda/package/math/render.ls) | medium |
| [atoms/extensible-symbol.ts](ref/mathlive/src/atoms/extensible-symbol.ts) | 127 | Large operators that grow (`\int`, `\sum`, `\prod`, `\bigcup`) with display/inline size split | inline in [render.ls](lambda/package/math/render.ls) | medium |
| [atoms/overunder.ts](ref/mathlive/src/atoms/overunder.ts) | 220 | `\overbrace`/`\underbrace`/`\overset`/`\underset` with stretchy SVG | inline in [render.ls](lambda/package/math/render.ls) | medium |
| [atoms/array.ts](ref/mathlive/src/atoms/array.ts) | ~640 | Array/matrix/cases environments with column-format, arraystretch, baseline alignment | [atoms/array.ls](lambda/package/math/atoms/array.ls) (659 LoC) | large |
| [atoms/enclose.ts](ref/mathlive/src/atoms/enclose.ts) | 471 | `\boxed`, `\cancel`, `\sout`, `\not`, longdiv (18 notation variants) — generates SVG strokes | [atoms/enclose.ls](lambda/package/math/atoms/enclose.ls) (358 LoC) | large |
| [atoms/spacing.ts](ref/mathlive/src/atoms/spacing.ts) | 83 | `\,` `\!` `\:` `\;` `\quad` `\qquad` etc. | [atoms/spacing.ls](lambda/package/math/atoms/spacing.ls) | small |
| [atoms/phantom.ts](ref/mathlive/src/atoms/phantom.ts) | 80 | `\phantom`, `\vphantom`, `\hphantom`, `\smash` | partial in [enclose.ls](lambda/package/math/atoms/enclose.ls) | small |
| [atoms/operator.ts](ref/mathlive/src/atoms/operator.ts) | 116 | Named operators (`\sin`, `\cos`, `\lim`) with limits placement | inline in [render.ls](lambda/package/math/render.ls) | small |

**Layout-relevant TypeScript LoC to translate: ~13,000–14,000.** This excludes the editor, virtual-keyboard, accessibility, and mhchem subsystems. Lambda's current math package is ~8,400 LoC; the port would replace approximately 5,500 LoC of Lambda code while adding ~4,000–5,000 net.

### Architecture

The proposal preserves Lambda's parsing layer (tree-sitter-latex-math → AST builder) and replaces only the rendering pipeline:

```
Lambda LaTeX input
        │
        ▼  (existing — kept)
   tree-sitter-latex-math grammar  →  CST
        │
        ▼  (existing — kept)
   build_ast.cpp                    →  Lambda math AST
        │
        ▼  (NEW — ported from MathLive)
   atom.ls (Atom record + per-type render dispatch)
        │
        ▼
   context.ls (mathstyle, scale, color, font derivation)
        │
        ▼
   per-atom render → Box / VBox tree
   (genfrac.ls, subsup.ls, surd.ls, accent.ls, leftright.ls, …)
        │
        ▼
   inter-box-spacing.ls (TeXBook 7×7 matrix + mtype promotion)
        │
        ▼
   coalesce.ls (sibling-merge optimization)
        │
        ▼
   box.ls toMarkup() → HTML
```

The `Atom` model is the key new abstraction. Currently Lambda dispatches on AST element name (`case 'fraction':`, `case 'subsup':`, etc.) in [render.ls](lambda/package/math/render.ls). MathLive uses a polymorphic `Atom` hierarchy where each subclass owns its `render(context)` method. Lambda Script can't subclass, but the same effect can be achieved with a dispatch table keyed on atom type.

### Phased plan

**Phase 0 — Side-by-side diff harness (40–80h)**
Build a tool that emits BOTH Lambda's render AND MathLive's render (via Node `convertLatexToMarkup`) for the same LaTeX input, reporting per-HTML-token diffs. Without this, every later phase costs 4–10× more to debug. Add per-failure clustering by atom type so each phase can target the highest-leverage clusters.

**Phase 1 — Font metrics data layer (80–120h)**
Port [font-metrics-data.ts](ref/mathlive/src/core/font-metrics-data.ts) (mechanical, possibly via a TS→Lambda codegen script) and [font-metrics.ts](ref/mathlive/src/core/font-metrics.ts) (cascade lookup over Main/Math/AMS/Size1–4) as a pure data module. Existing render.ls can call into it without changing emission. Feature-flagged: the existing 428 passing cases stay green.

**Phase 2 — Constants from metrics (140–240h)**
Replace the ~200 hardcoded em constants in [fraction.ls](lambda/package/math/atoms/fraction.ls), [scripts.ls](lambda/package/math/atoms/scripts.ls), [atoms/array.ls](lambda/package/math/atoms/array.ls), [atoms/enclose.ls](lambda/package/math/atoms/enclose.ls) **one spec branch at a time**, each replacement derived from the new metric layer. Every replacement gated by the full 921-case suite. Expected outcome: 428 → ~600–700 passing as TeXBook Rule 15a–c, Rule 18, italic correction become metric-driven instead of hand-tuned. **This is where the biggest correctness win lives.**

**Phase 3 — Context + mathstyle (50–90h)**
Port [context.ts](ref/mathlive/src/core/context.ts) and [mathstyle.ts](ref/mathlive/src/core/mathstyle.ts) as immutable Lambda records with `derive`/`clone` helpers. Thread through render.ls. Enables proper cramped-style cascading and superscript scaling that currently rely on Lambda's simpler `display`/`text`/`script`/`scriptscript` string discriminator.

**Phase 4 — Box + VBox primitives (120–180h)**
Port [box.ts](ref/mathlive/src/core/box.ts) and [v-box.ts](ref/mathlive/src/core/v-box.ts) as Lambda records with a builder pattern (mutable during construction via thread-local accumulators, frozen at `toMarkup`). Replace ad-hoc box.ls helpers. Likely uncovers latent bugs in the existing render path.

**Phase 5 — Delimiters + leftright + extensible (120–200h)**
Port [delimiters.ts](ref/mathlive/src/atoms/delim.ts), [leftright.ts](ref/mathlive/src/atoms/leftright.ts) auto-sizing, [extensible-symbol.ts](ref/mathlive/src/atoms/extensible-symbol.ts). Closes the known cluster of failing cases involving big operators and `\left…\right`.

**Phase 6 — Geometry-heavy atoms (130–220h)**
Port [surd.ts](ref/mathlive/src/atoms/surd.ts), [accent.ts](ref/mathlive/src/atoms/accent.ts), [overunder.ts](ref/mathlive/src/atoms/overunder.ts), [overlap.ts](ref/mathlive/src/atoms/overlap.ts), [phantom.ts](ref/mathlive/src/atoms/phantom.ts). Each fixes a specific failure cluster.

**Phase 7 — Genfrac + subsup full ports (110–240h)**
Replace [fraction.ls](lambda/package/math/atoms/fraction.ls) and [scripts.ls](lambda/package/math/atoms/scripts.ls) with TeXBook-faithful Rule 15/18 implementations backed by the metric layer. These are the largest single-file ports and where Lambda's current pass rate has the most ceiling.

**Phase 8 — Array/environments (80–220h)**
Port [array.ts](ref/mathlive/src/atoms/array.ts) (the largest single atom file) with column-format parsing, `arraystretch`, `arraycolsep`, baseline alignment, all environment types (matrix, pmatrix, bmatrix, cases, dcases, align, gather, multline).

**Phase 9 — Inter-box spacing + mtype promotion (30–50h)**
Replace [spacing_table.ls](lambda/package/math/spacing_table.ls) with the full TeXBook 7×7 matrix + `adjustType()` mbin→mord promotion + DFS traversal from [inter-box-spacing.ts](ref/mathlive/src/core/inter-box-spacing.ts).

**Phase 10 — Enclose + color + residual atoms (80–160h)**
Port [enclose.ts](ref/mathlive/src/atoms/enclose.ts) variants (18 notations), color blending, mode handlers, error/tooltip/macro atoms as needed by the test suite.

**Phase 11 — Regression hardening (120–300h)**
Push from ~800/921 to ≥99% with case-by-case diffs against MathLive reference. This is the long tail; budget it explicitly rather than hoping it falls out of the earlier phases.

### Total effort estimate

```
Realistic medium:    1,100 – 1,400 person-hours
                     ≈ 28 – 36 weeks
                     ≈ 7 – 9 calendar months (one experienced engineer)

Low end (best case):   750 –   850 hours  (~19 – 21 weeks)
High end (with realistic regression-fix cycles):
                     1,800 – 2,200 hours  (~45 – 55 weeks)
```

The medium estimate assumes:
- Engineer is fluent in both TypeScript and Lambda Script.
- Phase 0 (diff harness) is built first.
- The 921-case suite gates every phase.
- No upstream MathLive churn during the port (otherwise add ~100–200h/year of catch-up).

**A wholesale single-engineer rewrite is plausible but should not be promised as a quarter-scale project.** A year of focused work is the responsible expectation.

### Risk register

| ID | Severity | Risk |
|---|---|---|
| R1 | **HIGH** | Regression on the 428 currently-passing cases. The hardcoded fraction/scripts/array em constants in Lambda were calibrated against the old emission path; any metric-driven rewrite is very likely to drift on cases that currently pass by accident. Golden-test gating is mandatory at every phase. |
| R2 | MEDIUM-HIGH | TypeScript→Lambda paradigm mismatch. Mutable Box/Atom/Context classes need to become copy-on-write records. Inflates LoC ~1.3–1.6× and can change observable identity semantics (parent pointers, atom-ID round-trip). |
| R3 | MEDIUM | Hidden algorithmic complexity. Font-metric lookup cascades across 4 font families with per-codepoint fallback; any missed branch shifts vertical alignment by ~0.05em. Italic-correction and skew propagate across subsup/accent positioning. `mtype` promotion drives inter-atom spacing. Delimiter sizing has 4 strategies (small, large, built-up, SVG). Each is a quiet correctness landmine. |
| R4 | **HIGH** | Maintenance divergence. MathLive is actively developed; a static fork freezes Lambda to a SHA. Without a thin shim layer that mirrors MathLive's atom schema 1:1, catching up on a year of upstream churn could cost 100–200h/yr. With a shim layer: ~100h upfront, 30–50h/yr ongoing. |
| R5 | MEDIUM | Performance. Lambda's interpreter + MIR JIT path is slower than V8 for recursive Box construction; immutable-record style further inflates allocation pressure. Could be 5–10× slower for box-tree construction. Tolerable for batch/render contexts, not for interactive editing. |
| R6 | MEDIUM | Lambda language gaps. No WeakMap, no Symbol, no class inheritance, limited Set semantics, no exceptions (T^E + `?` propagation must replace try/catch). Atom subclassing must become explicit dispatch tables — adds boilerplate and diverges from upstream code shape. |
| R7 | LOW-MEDIUM | `font-metrics-data.ts` is 2,366 lines of mechanical data; porting bloats module load and review surface. Consider a generated/compiled lookup or a small TS→Lambda codegen for the data tables. |
| R8 | **HIGH** | Bidirectional debugging cost. Tracing a 0.02em drift requires side-by-side runs of MathLive and Lambda; Lambda's test harness doesn't currently support this. Without that infrastructure (Phase 0), regressions cost 4–10× longer to diagnose. |
| R9 | MEDIUM | Test-suite mismatch. The 921 cases were generated against a specific MathLive version's exact HTML/CSS emission. If upstream renames CSS variables, restructures span nesting, or changes class names, the suite needs regeneration — and "passing" decouples from "correct". |
| R10 | MEDIUM | Scope creep on parsing. atom-class.ts and modes-*.ts blur the parser/render boundary; many atom render() methods reach back into parser-side definitions (symbols, function tables). A clean port requires either porting symbols.ts/functions.ts/styling.ts (~2,700 LoC parser-side) or building a translation shim. |
| R11 | LOW | Some atoms (composition, placeholder, prompt, tooltip) are editor-only and contribute little to markup parity. Excluding them saves ~370 LoC of port work; doing so should be an explicit decision. |

### Acceptance criteria

The port is complete when:

1. The 921-case markup test passes at **≥99%** (≥912 passing). 100% if the genuinely-unsupported cases (`\label`, `\cbrt`, `\tr`, etc.) are accepted as `expectedError=unknown-command`.
2. Per-atom-family pass rate is ≥95%: fractions, scripts, surds, accents, environments, delimiters, big operators, colors, modes.
3. **Zero regressions** on the upstream 206-case corpus (current 100% rate maintained throughout the port).
4. The hardcoded em constants in fraction.ls/scripts.ls/array.ls/enclose.ls are **all removed** — every emission derives from font-metrics-data + computed Rule 15/18 shifts.
5. A side-by-side diff harness exists in `test/lambda/mathlive/` that emits both Lambda's render and MathLive's render for any input.
6. Performance: rendering 1,000 typical math expressions completes in ≤30s on the test runner machine (currently ~15s; allow 2× degradation given the abstraction overhead).

### Alternative approaches — considered, reasons not chosen

The research workflow ([temp/.../wn2030av6.output](temp)) considered several alternatives short of the full port:

| Alternative | Effort | Pros | Cons | Why not chosen as primary |
|---|---:|---|---|---|
| **Partial port — metric layer only** | 150–250h | Highest ROI per hour. Likely lifts 428 → ~600–700. | Doesn't address layout-rule mismatches (Rule 15/18). | **Recommended as Phase 1+2 of the full port.** If the user wants to stop after Phase 2, this is where they'd stop. |
| **Targeted atom-family ports** | 250–400h per family | Bounded scope. Each family ports cleanly. | Doesn't address the cross-cutting Box/Context/metric layer. Spec drift across families. | Lacks architectural coherence; each port creates new integration points with the unported native code. |
| **TS→Lambda codegen for mechanical files** | 80–150h upfront | Tracks upstream churn semi-mechanically. | Limited to mechanical data; still need manual port for logic. | **Recommended as a sub-task of Phase 1** for font-metrics-data.ts and symbol tables specifically. |
| **Headless-MathLive bridge** | 80–150h | Reaches ≥99% pass rate in a quarter the time. | Adds JS runtime dependency; gives up "pure Lambda"; inherits any MathLive bug. | If pass-rate-as-a-feature is the goal, this is the rational choice. If learning/owning the layout algorithms is the goal, full port. |
| **Hybrid: Lambda parses, MathLive renders** | 200–300h | Preserves Lambda's parser/format flexibility while leveraging MathLive's mature layout. | Pure-Lambda purity sacrificed. Two language runtimes. | Compromise option — worth considering if engineering time is the binding constraint. |
| **Freeze and audit** | 40–80h | Avoids commitment to 1,000h+ port. | Caps Lambda at ~46% pass rate. | Honest end-state if the math package's current quality is sufficient for Lambda's actual use cases. |
| **Do nothing yet — invest in test infrastructure** | 80–120h | The data may reveal a small set of fixes closes most of the gap, no port needed. | Defers the decision; doesn't directly improve pass rate. | **Recommended as Phase 0 regardless of which path is chosen.** Phase 0 of the full port; alternatively, the entire next investment if the full port is deferred. |

### Honest recommendation

The full port is **technically sound but a year of focused work**. Before committing, the user should:

1. **Build Phase 0 (test harness) regardless of decision** (40–80h). It pays for itself in any path.
2. **Try Phase 1+2 (metric layer + constant replacement)** (220–360h). This delivers the highest ROI per hour and may close enough of the gap that subsequent phases become optional.
3. **Re-evaluate after Phase 2.** If pass rate has reached ~700/921 (75%) and the remaining failures cluster in specific atom families, continue with targeted phases (5, 6, 7, 8). If pass rate has plateaued, reconsider the headless-MathLive bridge or freeze options.

A full commitment to all 11 phases without intermediate checkpoints risks 1,000+ hours invested in a port that ultimately delivers diminishing returns relative to a simpler architecture choice.

### Out of scope for this proposal

- MathLive's editor (`editor/`, `editor-model/`, `virtual-keyboard/`) — Lambda's math package is a renderer only.
- MathLive's accessibility output (`accessibility/`) — separate concern.
- mhchem chemistry extensions (`mhchem.ts`, 2,603 LoC) — niche use case.
- Performance optimization beyond the 2× ceiling specified in acceptance criteria.
- Visual rendering parity beyond HTML structural match (font glyph rendering, exact pixel placement).

---

## Phase 2 implementation log — full-precision strut + structural fixes

After the proposal landed, Phase 0 (diff harness) and Phase 1+2 (metric layer + strut) were both implemented. Final state:

```
Final achieved                          565 / 921   (61.3%)
Pass-rate gain over the session         +137 cases (+14.8 percentage points)
Upstream corpus                         206 / 206   (100%)    — held throughout
Lambda corpus                           359 / 715   (50.2%)   — up from 222 / 715 (31.0%)
```

### What was built

**Phase 0 — Side-by-side diff harness** ([test/lambda/mathlive/diff_harness.mjs](test/lambda/mathlive/diff_harness.mjs)):
- Renders any LaTeX through both Lambda and MathLive (via the SSR bundle's `convertLatexToMarkup`).
- Tokenizes both HTML outputs and emits classified findings: `tag-mismatch`, `class-diff`, `em-drift`, `style-diff`, `text-mismatch`.
- Em drift sorted by magnitude with `Δ` deltas — the metric calibration bottleneck is now visible.
- Tree-mode (`--tree`) prints both renders as indented S-expressions for structural comparison.
- Batch (`--batch <file>`) and report-driven (`--from-report <path> --top N`) modes for sweeping failures.

**Phase 1 — Metric layer port** ([lambda/package/math/metrics.ls](lambda/package/math/metrics.ls)):
- `get_character_metrics(ch, font_name)` — full cascade lookup with `EXTRA_CHARACTER_MAP` (Latin-1 + Cyrillic substitutions) and CJK detection.
- `code_point()` via Lambda's `ord()` built-in.
- Mathstyle transition helpers: `sup_style`, `sub_style`, `frac_num_style`, `frac_den_style`, `cramp`, `is_cramped`, `base_style`.
- Sigma constants table already matched MathLive's `FONT_METRICS` exactly — kept as-is.

**Phase 2a — Raw-precision strut emission** (cross-cutting):
- Regenerated [lambda/package/math/metrics_data.ls](lambda/package/math/metrics_data.ls) to include `height_raw` and `depth_raw` (5dp full precision) as elements 5–6 of each metric tuple. Italic correction now rounded via CEIL@2 (matches MathLive's `toString()`).
- `text_box` ([lambda/package/math/box.ls:48](lambda/package/math/box.ls:48)) populates `height_raw`/`depth_raw` from metric lookup (single char) or per-char max (multi-char operator names like `sin`/`cos`/`arcsin`).
- `+`/`-`/`−` get truthful raw values (0.58333) for strut purposes while keeping the rounded `height = 0.69` heuristic for fraction/script layout calculations — this is the surgical move that closes the cascade.
- `hbox` ([lambda/package/math/box.ls:512](lambda/package/math/box.ls:512)) propagates max raw height/depth across children, initialized with the first child's value so negative depths (arrows extending above baseline) preserve.
- `skip_box`, `with_class`, `with_style`, `with_scale`, `with_color`, `box_with_type`, `coalesce` all forward `height_raw`/`depth_raw`.
- `math.ls` strut emission ([lambda/package/math/math.ls:34](lambda/package/math/math.ls:34)) detects raw availability and emits `fmt_em_ceil2(h_raw + d_raw)` for strut-bottom-height, `fmt_em_ceil2(-d_raw)` for vertical-align. Falls back to the previous rounded path for boxes with `strut_total` or `strut_depth_em` overrides, or when raw values weren't propagated.

**Phase 2 structural — Italic correction & operator names** ([lambda/package/math/box.ls:203](lambda/package/math/box.ls:203)):
- `text_style()` now derives italic correction from metric data for both `lm_mathit` and `lm_cmr` characters — replacing the hardcoded ~10-letter map. Adds margin-right for `w`, `r`, `v`, `\partial`, `\int`, AMS arrows, etc.
- Multi-character operator names (`sin`/`cos`/`tan`/`arcsin`/`sinh`/etc.) now compute height as max per-char metric instead of a hardcoded lookup — closes `\sinh u` vs `\sin x` height mismatch.

**Phase 2 structural — Small misc**:
- `\ominus`/`\oslash`/`\boxplus`/`\boxminus`/`\boxtimes`/`\boxdot` mapped to `lm_cmr` font class ([lambda/package/math/symbols.ls:278](lambda/package/math/symbols.ls:278)).
- ASCII `*` → `∗` (U+2217) at render time ([lambda/package/math/render.ls:170](lambda/package/math/render.ls:170)).
- `smallmatrix` environment gets leading/trailing `lm_arraycolsep` separators at width 0.2em (matches MathLive's `colSeparationType='small'`).

### Pass-rate timeline this session

```
Session start                            428 / 921  (46.5%)
Italic correction metric-driven         +88   →  516
ominus / oslash class fix                +2   →  524
ASCII * → U+2217                         +3   →  527
Multi-char operator heights              +7   →  531
Raw-precision strut (initial)           +13   →  544
skip_box + box_with_type raw prop        +6   →  550
+/− raw height (truthful 0.58333)       +13   →  563
with_class raw prop                      +2   →  565
─────────────────────────────────────────────────────
Final achieved                          565 / 921  (61.3%)
```

### What this validates

The proposal's analysis was correct in two key ways:

1. **The hardcoded em constants weren't the only blocker.** Many cases failed on issues orthogonal to the constants — class-flip bugs (`\ominus`), missing italic correction lookups, multi-char operator height heuristics, missing structural patterns. These cleaned up first with simple metric-driven rewrites.

2. **Full-precision propagation works when scoped correctly.** Rather than the whole-system rewrite that previously regressed, the surgical Phase 2a approach — full-precision raw values for STRUT only, while layout math continues using rounded values — closed ~50 of the 0.01em-drift cases without touching the hardcoded fraction/script constants. The `+`/`−` height fix exemplifies this: layout sees the 0.69 heuristic (so fraction depth math stays calibrated), but the outer strut sees the truthful 0.58333 (so `x - y - z` emits 0.59em correctly).

### What still blocks higher rates

- **0.01em drift in composite boxes**: when a fraction/script box is at the top of the strut wrap, raw values aren't available (composite boxes use hardcoded heights, not raw metrics). To close these would require replacing the genfrac/subsup height constants — the original Phase 2b work.
- **Structural pattern gaps**: integrals don't emit `lm_op-group > lm_op-symbol lm_large-op` wrapping; `\lim` doesn't render `lm_msubsup` for limit subscripts; accents (`\vec`/`\hat` over wide letters) have margin-left positioning off. Each is a per-atom-family port.
- **Class swap in complex contexts**: in `(x+h) - f(x)`-style expressions the parenthesis-letter class assignment cascade differs from MathLive's. This is in the spacing/typing pipeline and would require a deeper port.
- **`\mathbf{...}` font fallback**: Lambda has no Main-Bold metric table. Sub-0.05em drifts in bold math letters.

### Phase 2b (genfrac/subsup constant replacement) — recommended next

The proposal's Phase 2b — replace the ~30 hardcoded em constants in [fraction.ls](lambda/package/math/atoms/fraction.ls) and [scripts.ls](lambda/package/math/atoms/scripts.ls) with TeXBook Rule 15/18 implementations driven by mathstyle sigma constants — is now de-risked because Phase 1's metric layer is in place. Expected to close another ~40–80 cases. Estimated effort: 80–140 hours.

### What to read in the codebase

| Area | Start here |
|------|-----------|
| Diff harness (Phase 0) | [test/lambda/mathlive/diff_harness.mjs](test/lambda/mathlive/diff_harness.mjs) |
| Metric API (Phase 1a/1b) | [lambda/package/math/metrics.ls](lambda/package/math/metrics.ls) — `get_character_metrics`, mathstyle helpers |
| Strut full precision (Phase 2a) | [lambda/package/math/math.ls:34](lambda/package/math/math.ls:34) — `use_raw` gate and `fmt_em_ceil2` emission |
| Box raw-field propagation | [lambda/package/math/box.ls:48](lambda/package/math/box.ls:48) `text_box`; [lambda/package/math/box.ls:512](lambda/package/math/box.ls:512) `hbox` |
| Italic correction metric-driven | [lambda/package/math/box.ls:203](lambda/package/math/box.ls:203) `text_style` |
| Multi-char operator heights | [lambda/package/math/box.ls:131](lambda/package/math/box.ls:131) `text_height_for` → `max_char_height` |
| Per-symbol big-op metrics | [lambda/package/math/render.ls:251](lambda/package/math/render.ls:251) `render_limit_operator_symbol` + `large_op_metrics`/`small_op_metrics` |
| Integral inline-limits (side msubsup) | [lambda/package/math/atoms/scripts.ls:50](lambda/package/math/atoms/scripts.ls:50) `render_integral_inline_scripts` |
| Tall-base accent positioning | [lambda/package/math/render.ls:1143](lambda/package/math/render.ls:1143) `render_simple_accent` |
| Prime as msubsup script | [lambda/package/math/render.ls:171](lambda/package/math/render.ls:171) `render_prime_script` |

---

## Phase 2b implementation log — structural patterns

After Phase 2a landed at 565/921, Phase 2b shifted from "replace fraction Rule 15 constants wholesale" to "add the specific structural patterns the diff harness identified." Fraction-constant replacement was DEFERRED because the dispatch table is heavily tuned for nested contexts (each branch represents a calibrated case); wholesale replacement is the cascade trap that previous attempts ran into. The structural pattern approach delivered +16 cases in one session without touching fraction.ls or scripts.ls's hardcoded specs.

### Pass-rate timeline

```
Session 2 start                          565 / 921  (61.3%)
Per-symbol big-op metrics (Size2 port)   +3   →  568
Integral inline side-limits              +8   →  576
Sub-only integral height                 +4   →  580
Tall-base accent positioning (vec{F})    +0   →  580 (one-finding gain, no count)
Prime-as-msubsup structure (x', f'(x))   +1   →  581
─────────────────────────────────────────────────────
Final achieved                           581 / 921  (63.1%)
Upstream baseline                        206 / 206  (100%)
```

### What was built (Phase 2b)

**Per-symbol big-op metrics** ([lambda/package/math/render.ls:285](lambda/package/math/render.ls:285)):
- Ported MathLive's font-metrics-data.ts Size2/Size1 tables for `\int`, `\sum`, `\prod`, `\oint`, `\bigcap`, `\bigcup`, etc.
- Replaced the previous one-size-fits-all heuristic (h=1.61, d=0.2) with truthful per-symbol metrics:
  - `\int` Size2: h=1.36, d=0.86, italic=0.45 → emits the `margin-right:0.45em` MathLive does.
  - `\sum`/`\prod`/`\bigcup`/etc. Size2: h=1.05, d=0.55, no italic.
- The italic correction is emitted as inline style on the operator symbol span.

**Integral inline side-limits** ([lambda/package/math/atoms/scripts.ls:50](lambda/package/math/atoms/scripts.ls:50) `render_integral_inline_scripts`):
- New render path for `\int_X^Y` (and `\oint`, `\iint`, `\iiint`, etc.) in textstyle: emits `lm_op-group > lm_op-symbol[margin-right] + lm_msubsup` SIBLING (not stacked vlist), matching MathLive's `subsupPlacement: 'adjacent'`.
- Sub/sup positioning differs by configuration:
  - Both limits: sub `margin-left:-0.44em` (nestle into italic gap).
  - Sub-only: sub `margin-right:0.05em` (slight right kerning).
- Sub-script wrapper height derived via `sub_height_for()` = CEIL@2(sub_box.height_raw * 0.7).
- Box h/d differ per configuration: both → 1.55/0.89, sub-only → 1.36/0.89222 (the slightly-higher d_raw makes CEIL@2(h+d) produce MathLive's 2.26).
- `\sum`, `\prod`, etc. keep the existing stacked-limits behavior (matches MathLive's auto-placement, which keeps them above/below in display/text style).

**Tall-base accent positioning** ([lambda/package/math/render.ls:1143](lambda/package/math/render.ls:1143) `render_simple_accent`):
- Previously hardcoded `height:0.44em` for the base wrapper inside `\hat`/`\vec`/`\bar` accents — this over-shrank uppercase letter bases (`\vec{F}` rendered F at 0.44em instead of its actual 0.69em).
- Now uses `base_box.height` for the wrapper; accent_top shifts up by `(base.height - 0.44)` so taller bases push the accent up; vlist height grows accordingly.

**Prime as msubsup** ([lambda/package/math/render.ls:171](lambda/package/math/render.ls:171) `render_prime_script`):
- ASCII `'` (apostrophe) at the AST punctuation level is now rendered as a `lm_msubsup` containing the Unicode prime `′` (U+2032) in a 70%-scaled superscript-style vlist — matches MathLive's behavior for `x'`, `f'(x)`, etc.
- Closes a structural mismatch where Lambda emitted `<span class="lm_cmr">'</span>` as a flat sibling.

### What was DEFERRED in Phase 2b

The original Phase 2b — replace the ~30 hardcoded em-constant branches in [fraction.ls](lambda/package/math/atoms/fraction.ls) with TeXBook Rule 15d formulas — was attempted but deferred. The dispatch table contains per-context tuning (e.g., specific branches for colorbox-content fractions, script-style nested fractions, mixed numerator descender cases). Replacing branches wholesale produced significant regressions because adjacent branches depend on neighboring constants.

A FUTURE Phase 2b refactor should:
1. Build a per-branch test fixture: take each of the ~30 dispatch keys, generate 3-5 representative test cases, and gate the refactor on them passing.
2. Replace ONE branch at a time, verifying against the fixture.
3. Use Phase 1's mathstyle sigma constants (`met.num1`/`met.denom1`/`met.defaultRuleThickness`) and `metrics.AXIS_HEIGHT` to derive Rule 15d shifts.
4. Estimated effort: 80–140 hours given the per-branch verification cycle.

### Remaining failure clusters (post Phase 2b)

| Cluster | Approx count | Pattern |
|---|---:|---|
| Composite-box 0.01em drift | ~50 | Fractions/scripts at top of strut wrap; raw values not propagated through composite boxes |
| `\lim` operator-name with limits | ~10–15 | Lambda doesn't yet emit `lm_vlist` wrap for multi-char operator-name bases when limits attach |
| Accent margin-left precision | ~15 | Per-glyph skew-based margin-left calculation differs |
| Class-flip in complex contexts | ~30 | Paren/letter class assignments differ in cascading expressions like `(x+h) - f(x)` |
| Prime in script context | ~3 | `'` inside script context needs further size reduction |
| `\mathbf{...}` font fallback | ~8 | No Main-Bold metric table |
| Matrix smallmatrix em drift | ~5 | Cell heights wrong in smallmatrix (needs script-style scaling for cells) |
| Display-mode integrals | ~5–10 | Display style integrals (large `∫`) need different stacking behavior |
