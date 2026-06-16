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
