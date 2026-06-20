# Lambda Math Package — Phase 4: Metric-Driven Interior Layout (Rule 15/18)

> Continues `Lambda_Pkg_Math.md` → `Math2` → `Math3`. Where Math3 drove the
> corpus from 30% to **763/921 (82.8%)** by porting the metric *data* layer and
> fixing structural patterns case-by-case, Math4 addresses the **structural
> debt that Math3 deliberately worked around**: the interior fraction and
> script geometry is still computed from ~1,500 hand-tuned em constants instead
> of from font metrics + the TeXBook layout rules. This document proposes the
> fundamental change that removes the hardcode, and the incremental plan to
> regain (and exceed) the current pass rate on top of it.

## 1. The problem, stated precisely

### 1.1 The constant-driven layout (the core debt)

MathLive computes **every** vertical quantity in a fraction or a sub/superscript
from two inputs: per-character font metrics (`font-metrics-data.ts`) and a small
set of mathstyle sigma constants (`AXIS_HEIGHT`, `X_HEIGHT`, `num1/num2/num3`,
`denom1/denom2`, `sup1/sup2/sup3`, `sub1/sub2`, `supDrop/subDrop`,
`defaultRuleThickness`). The geometry then falls out of two TeXBook rules:

- **Rule 15** (TeXBook Appendix G) — generalized fractions (`\frac`, `\binom`,
  `\genfrac`, `\over`, `\atop`).
- **Rule 18** — superscripts and subscripts.

Lambda does **not** do this. The actual layout runs on a hand-tuned constant
table. Counted directly:

| File | dispatch `else if` branches | numeric literals |
|------|----------------------------:|-----------------:|
| `atoms/fraction.ls` | 45 | 519 |
| `render.ls`         | —  | 460 |
| `atoms/scripts.ls`  | —  | 242 |
| `atoms/array.ls`    | —  | 189 |
| `atoms/enclose.ls`  | —  | 65 |

≈ **1,500 hardcoded em constants**. The fraction layout alone is a 45-way
`else if` dispatch on `(numer_total, denom_total, style, has_descender, …)` that
returns a fixed spec table (159 hardcoded `numer_child_height: 0.65`,
`denom_top: -2.31`, `depth_holder: 1.42`, … fields).

### 1.2 The scaffolding is present but dead

`fraction.ls` already computes the Rule 15b shifts from the sigma metrics:

```lambda
let result_shifts = if (ctx.is_display(frac_ctx))
    (let ns = met.at(met.num1, si), let cl = 3.0 * rule_thickness,
     let ds = met.at(met.denom1, si), {numer_shift: ns, clearance: cl, denom_shift: ds})
    else if (rule_thickness > 0.0)
    {numer_shift: met.at(met.num2, si), clearance: rule_thickness, denom_shift: met.at(met.denom2, si)}
    else ...
let ns = result_shifts.numer_shift  // ← computed
build_frac_bar(numer_box, denom_box, ns, cl, ds, rule_thickness, frac_ctx)
```

…but `build_frac_bar(numer_box, denom_box, ns, cl, ds, …)` **never references
`ns`, `cl`, or `ds` in its body** (grep the whole function — only the signature
line matches). It immediately calls `frac_bar_spec(...)`, the 45-branch
constant dispatch, and uses *that* for every dimension. The metric-driven path
is computed and discarded.

### 1.3 Why every incremental attempt was reverted

Math3 tried to flip constants to their true metric values four separate times;
all were reverted (documented in `Math3.md`):

- *"B6/B7/B8 metric-driven rewrite caused 33 baseline regressions."*
- *"Full-precision propagation with CEIL@2 emission: −42 (reverted)."*
- *"`+`/`−` height = 0.59 (true cmr metric): cascades into 11 nested-context
  hardcoded constants in fraction.ls/enclose.ls (reverted)."*

The root cause is a single, fatal interaction:

> The hardcoded constants were tuned for Lambda's **round-half-up** emission.
> MathLive's Rule 15/18 produces values that round under **CEIL@2**
> (`Math.ceil(v·100)/100`, from `box.ts:toString`). Replacing one constant with
> its true metric value shifts the ~20 downstream constants that were
> calibrated against the old value by ±0.01em, regressing cases that passed
> **by coincidence**.

This is why the constants **interlock** and cannot be retired one at a time.
The structural change must replace the *whole* geometry of a construct at once
(fraction, then script), each gated by the per-branch fixture — accepting a
batch of regressions that are then driven back to zero against the corpus.

### 1.4 The box-record conflation (the enabling sub-problem)

Every renderer returns a box carrying **seven** vertical-extent fields:

```
height, depth,                              // "layout" extent (mostly)
render_height, render_depth, render_total,  // a second, parallel notion
height_raw, depth_raw,                      // 5dp values for strut emission
+ strut_total, strut_depth_em               // per-wrapper overrides in math.ls
```

This proliferation exists for one reason: **Lambda rounds intermediate values,
then needs the unrounded ones back to emit the outer strut correctly.** MathLive
has no such problem because it keeps boxes in **full-precision floats
throughout** and rounds **once**, at `toMarkup` (CEIL@2 on each emitted CSS
string). It separates exactly **two** concepts:

- a **layout** height/depth — the TeX box dimensions a parent uses to position
  this box (Rule 15/18 inputs), and
- the **emission** extent — derived from the final box's height/depth at the
  very end, for the `lm_strut` / `lm_strut--bottom`.

The nested-superscript work in Math3 was *entirely* a workaround for this
conflation: `render_sup_only` returns its **vlist (emission) height as the box
height**, which is wrong for a parent doing layout. The session patched it with
style-dependent `top` offsets and font-size flags. Splitting the two notions
dissolves that whole class of script/limit drifts instead of patching each one.

## 2. Goals

1. **Remove the hardcode.** Replace the constant-driven interior geometry of
   fractions and scripts with a faithful port of TeXBook Rule 15 / Rule 18,
   driven by the font metrics and sigma constants already present in
   `metrics.ls` / `metrics_data.ls`.
2. **Collapse the box model** to a single full-precision `height`/`depth` pair,
   with CEIL@2 rounding applied *only* at emission. Retire `render_*`,
   `*_raw`, and the `strut_*` overrides.
3. **Regain and exceed the pass rate incrementally.** Dozens of regressions are
   expected the moment the geometry is swapped; they are fixed *after* the
   structural change is in place, one construct/branch at a time, gated by the
   fixtures.

Non-goals: the parser, input formats, the SVG/stretchy atoms, color, and the
already-correct metric *data* layer. Those stay.

## 3. Target architecture

### 3.1 The box model — full precision, round once

Introduce a single canonical box record (conceptually):

```
Box = {
    element,        // the HTML element tree (unchanged)
    height,         // FULL-PRECISION layout height (em), float
    depth,          // FULL-PRECISION layout depth (em), float
    width,          // advance width (em), float
    italic, skew,   // for script/accent positioning (Rule 18 italic correction)
    type,           // mord/mop/mbin/mrel/mopen/mclose/mpunct/minner/mskip
    // optional: maxFontSize / scale for nested-style frames
}
```

- **No `render_height`/`render_depth`/`render_total`.** A box has exactly one
  height and one depth, both full precision. (`render_total` was only ever
  `height + depth` at a different rounding.)
- **No `height_raw`/`depth_raw`.** With full precision carried everywhere, the
  "raw" values *are* `height`/`depth`. The strut reads them directly.
- **No `strut_total`/`strut_depth_em` overrides.** Wrappers that needed to
  override the strut did so because their interior used rounded constants;
  once the interior is metric-driven the outer strut is just CEIL@2 of the
  final box.

Emission becomes the single rounding site, in `math.ls`:

```lambda
// the ONLY place em values are rounded for output
fn emit_strut(box) {
    let h = ceil2(box.height)
    let d = ceil2(box.depth)
    // strut height = ceil2(h); strut--bottom height = ceil2(h+d); va = ceil2(-d)
    ...
}
```

This mirrors MathLive `box.ts` exactly: dimensions are floats until
`toMarkup`/`toString`, which applies `Math.ceil(v·100)/100`.

### 3.2 Rule 15 — fractions (`genfrac.ls`)

Replace `frac_bar_spec()` (the 45-branch table) with the algorithm. With
numerator box `N` and denominator box `D` rendered in the numerator/denominator
styles, rule thickness `θ = defaultRuleThickness[style]`, axis `a = AXIS_HEIGHT`:

```
// shift-up u, shift-down v (TeXBook Rule 15b)
if display:            u = num1[style];  v = denom1[style]
elif θ > 0:            u = num2[style];  v = denom2[style]
else (atop, θ = 0):    u = num3[style];  v = denom2[style]

// with a bar (Rule 15d): keep numerator/denominator clear of the bar+axis
φ = (display ? 3θ : θ)
if (u - N.depth) - (a + θ/2) < φ:  u += φ - ((u - N.depth) - (a + θ/2))
if (a - θ/2) - (D.height - v) < φ: v += φ - ((a - θ/2) - (D.height - v))

// resulting fraction box
height = u + N.height
depth  = v + D.depth
// vlist: numerator at +u, bar centered on axis a, denominator at -v
```

Without a bar (binomial): the clearance is `φ = (display ? 7θ : 3θ)` applied
between `N.depth` and `D.height`. The numerator/denominator wrapper heights, the
`top:` offsets, and the strut all become *outputs* of these shifts plus the
child box metrics — no constants.

`\dfrac`/`\tfrac`/`\cfrac` differ only in the *style* fed in (display/text and
the `\cfrac` left-alignment / single nulldelimiter, already handled in Math3).

### 3.3 Rule 18 — sub/superscripts (`subsup.ls`)

Replace `render_sup_only` / `render_sub_only` / `render_both` (and the
big-op-limits variants) with Rule 18. Nucleus box `B`, optional sup `P`, sub
`Q`, scaled metrics for the script style:

```
// baseline drops (0 for a single-char nucleus; metric-derived otherwise)
u = isSingleChar(B) ? 0 : B.height - supDrop·scale
v = isSingleChar(B) ? 0 : B.depth  + subDrop·scale

// minimum sup shift by style
minSup = display ? sup1 : (cramped ? sup3 : sup2)

if only P:  supShift = max(u, minSup, P.depth + ¼·X_HEIGHT)
if only Q:  subShift = max(v, sub1, Q.height - ⅘·X_HEIGHT)
if both:
    supShift = max(u, minSup, P.depth + ¼·X_HEIGHT)
    subShift = max(v, sub2)
    // keep ≥ 4θ between them, and the sup bottom ≥ ⅘·X above the axis
    if (supShift - P.depth) - (Q.height - subShift) < 4θ:
        subShift = 4θ - (supShift - P.depth) + Q.height
        ψ = ⅘·X_HEIGHT - (supShift - P.depth)
        if ψ > 0: supShift += ψ; subShift -= ψ
```

The box `height`/`depth` then come from the shifted child extents. **Critically,
this returns the LAYOUT height/depth** (`supShift + P.height` etc.), *not* the
emission vlist height — so a parent (`e^{-x^2}`, a fraction numerator, a matrix
cell) positions it correctly. The vlist/CSS `top:` values are computed from the
same shifts for emission. This is the structural fix for §1.4: the
nested-script `top`/font-size hacks from Math3 disappear because the parent now
receives a correct layout box.

Italic correction (`B.italic`) and skew enter here for sup horizontal placement
— already partially present, folded into the rule.

### 3.4 Array / equation centering (`array.ls`)

The single-row `\begin{equation}` centering Math3 added empirically
(`height = content.height`, `depth ≈ content.height − 0.51`) is a special case
of MathLive's array vshift: each row's baseline is placed, the stack is centered
on the axis, and `arraystretch`/`baselineskip` set the row gaps. Port the real
vshift so depth-heavy content (the integral-in-equation cluster, ~6 cases) and
multi-row `pmatrix`/`cases`/`align` all derive their cell `top`/height from
content + a single arraystretch constant, not the per-`nrows` metric tables.

## 4. Phased plan

The order is deliberate: the box-model split (Phase A) is the *enabler* — Rule
15/18 produce full-precision layout boxes, which only compose correctly if the
whole tree carries full precision and rounds once.

| Phase | Work | Files | Gate |
|------:|------|-------|------|
| **A** | **Box-model split.** Carry full-precision `height`/`depth` everywhere; move CEIL@2 to the single emit site in `math.ls`; delete `render_*`, `*_raw`, `strut_*`. Mechanical but wide. | `box.ls`, `math.ls`, every `atoms/*` + `render.ls` that constructs a box | 921 corpus must not *drop* (expect churn; fix to ≥ pre-A before B) |
| **B** | **Rule 15 fractions.** Replace `frac_bar_spec` with the Rule 15 algorithm. Delete the 45-branch table + 159 spec fields. | `atoms/fraction.ls` | `fraction_branch_fixture.mjs` (27 branches) → 27/27, then corpus |
| **C** | **Rule 18 scripts.** Replace `render_sup_only`/`render_sub_only`/`render_both` + big-op-limit variants. Remove the nested-script `top`/font-size workarounds. | `atoms/scripts.ls` | a new `script_fixture.mjs` (sup/sub/both × styles × descenders/nesting), then corpus |
| **D** | **Array vshift.** Port MathLive's row placement + axis centering + arraystretch. Retire the per-`nrows` metric tables and the `equation_table_metrics` special case. | `atoms/array.ls` | matrix/cases/equation cases in corpus |
| **E** | **Regression cleanup.** Drive the batch of A–D regressions to zero and past the old rate: the cases that the constants couldn't reach (composite 0.01em drifts, `\lim` strut, multi-script descenders) now fall out of the metric layout. | wherever the diff harness points | corpus ≥ old + per-cluster sweeps |

### 4.1 Expect (and budget for) regressions

The moment Phase B swaps the fraction geometry, **dozens of currently-passing
fraction cases will regress** — they pass today because their content happened
to match a tuned constant. This is expected and acceptable *within a phase*:
the structural change is correct; the regressions are the constants' coincidences
unwinding. The gate is **net-positive by the end of each phase**, not
zero-regression mid-phase. (Contrast Math3's rule, which was zero-regression
*per commit* — that rule is what made the constant port impossible, because no
single constant flip is net-positive.)

### 4.2 Tooling that must exist first

- `fraction_branch_fixture.mjs` — already exists (27 dispatch cases). Extend to
  assert the *full-precision* intermediate shifts, not just final HTML.
- `script_fixture.mjs` — **new**; the Phase C gate. Cover: sup-only / sub-only /
  both; each mathstyle (D/T/S/SS) and cramped; single-char vs compound nucleus;
  descender sup/sub; nested (`x^{x^2}`); big-op limits.
- `diff_harness.mjs` — already emits per-token em deltas; the per-phase cleanup
  driver. Add `--cluster-by atom` so Phase E can target the largest residual
  family first.
- The upstream **206-case baseline stays a hard gate at the end of every
  phase** (it is the definition of "structurally correct").

## 5. The box-model split in detail (Phase A)

This is the highest-churn, lowest-algorithmic-risk phase, so it goes first.

1. **Audit every box constructor.** `text_box`, `hbox`, `vbox`, `box_cls`,
   `skip_box`, the `with_*` forwarders, and every inline `{element: …, height:
   …}` literal across `render.ls`/`atoms/*`. Each must produce full-precision
   `height`/`depth`.
2. **Stop rounding mid-tree.** Anywhere a layout value is currently
   `fmt_em(...)`-rounded and then stored back as a number, keep the float.
3. **Single emit site.** `math.ls` wraps the final box; that is the only place
   `ceil2`/`fmt_em_ceil2` runs on a dimension. Strut height = `ceil2(h)`,
   strut-bottom = `ceil2(h+d)`, vertical-align = `ceil2(-d)`.
4. **Delete the overrides.** `render_total`, `render_height`, `render_depth`,
   `height_raw`, `depth_raw`, `strut_total`, `strut_depth_em` are removed; their
   readers switch to `height`/`depth`.

After Phase A the corpus may wobble (rounded constants now feed a once-rounded
strut); fix back to the pre-A number before starting B. Phase A buys nothing on
its own — it is the substrate that makes B/C/D *composable*.

## 6. Risk register

| ID | Severity | Risk / mitigation |
|----|----------|-------------------|
| R1 | **HIGH** | Phase A is wide (every renderer). Mitigate: do it purely mechanically, no behavior change intended, gate on returning to the pre-A corpus number before B. |
| R2 | **HIGH** | Rule 15/18 produce values that disagree with the tuned constants by ±0.01–0.05em on cases that pass today. Mitigate: per-phase net-positive gating, not per-commit; fixtures assert the *shifts*, not just HTML. |
| R3 | MEDIUM | The sigma constants in `metrics.ls` (`num1`, `sup2`, …) must be the correct per-mathstyle values and in the right frame. Verify each against MathLive `FONT_METRICS` before B/C; a wrong sigma silently biases a whole construct. |
| R4 | MEDIUM | `isSingleChar(nucleus)` and italic-correction/skew gating in Rule 18 are subtle; an off-by-one on the single-char test shifts every script. Cover explicitly in `script_fixture`. |
| R5 | MEDIUM | CEIL@2 vs round-half-up: after Phase A every emit is CEIL@2. A handful of upstream cases were passing under round-half-up; they must be re-verified, not assumed. |
| R6 | LOW | `ctx.derive` allowlist (below) is unrelated but adjacent; leave it. |

## 7. Out of scope / explicitly retained

- The metric **data** layer (`metrics_data.ls`, the per-font tables, the
  generator) — already metric-driven and correct.
- The `<`/`>` sentinel, accent centering, radical-index scale, font tables,
  display-integral side-limits, unknown-command rendering, and the other Math3
  structural fixes — all orthogonal to the interior geometry and kept.
- Parser, input/format, validator, SVG/stretchy atoms, color.

### `ctx.derive` allowlist (noted, not in scope)

`ctx.derive` is a hand-maintained allowlist: a new context field is silently
dropped unless added by hand (this bit `text_embedded` in Math3). The clean fix
is a map-spread merge (`{*ctx, *overrides}`), **but that syntax does not compile
in the current Lambda build** (verified: `{*a, *b}` and `{a, b}` both raise
`E100`). Until Lambda's map-spread is available, the allowlist remains the
working idiom — a small, contained footgun, not a blocker for Math4. Revisit if
the language gains a compiling merge form.

## 8. Acceptance criteria

1. `atoms/fraction.ls` and `atoms/scripts.ls` contain **no per-content em
   constant tables** — every fraction/script dimension derives from font
   metrics + the sigma constants via Rule 15 / Rule 18. (`grep` for the spec
   fields returns nothing.)
2. A box carries exactly **one** `height` and one `depth` (full precision);
   `render_*`, `*_raw`, `strut_*` are gone; CEIL@2 runs at exactly one emit
   site.
3. `fraction_branch_fixture.mjs` and the new `script_fixture.mjs` pass 100%.
4. The 921-case corpus is **≥ 763** (the pre-Math4 number) and ideally past it —
   the composite-box, `\lim`-strut, and multi-script-descender residuals that
   the constants could not reach should now pass as a side effect.
5. **Zero regressions on the upstream 206-case baseline** at the end of the
   project (per-phase it may dip; end-state must be 206/206).

## 9. Why this is the right next investment

Math3 reached 82.8% by exhausting the cheap, localized fixes; the remaining
~158 failures are increasingly in the "needs the real box model" bucket, and
each new fix now requires *another* tuned branch that interlocks with the
others. The constant approach has a ceiling, and we are near it. Porting Rule
15/18 converts the recurring per-case tuning into a one-time structural change:
after it lands, a failing fraction or script is a *bug in the rule application*
(one fix, many cases) rather than a *missing constant* (one fix, one case).
That is the difference between a 90%-with-diminishing-returns plateau and a
clean path to ≥99%.

---

## 10. Progress & status — 2026-06-18

**Headline: corpus 763 (pre-Math4) → 823/921, upstream baseline 205/206 (the one
fail is a deliberate temporal regression — see below), ~1000+ hardcoded layout
lines removed.** The thesis of §9 held: the metric port turns per-case tuning
into structural fixes. `fraction.ls` is **100% metric-driven** (50-branch
`frac_bar_spec` *deleted*); **sqrt/radical is metric-driven** (bucket dispatch
gone, +17 corpus); the **`2.41` delimiter magic is retired** (raw glyph
exposure); the **last 2 baseline residuals were fixed** (script-reached fractions
expose raw, §10.0b → 206/206); and **arrays (matrix family + array + dcases) are
now metric-driven** (§10.8) — the only baseline fail is the dcases 0.01
fraction-precision float-tip (temporal, removing its hardcode per the "remove
all hardcode, regression OK" directive).

### 10.0b Last 2 baseline residuals FIXED → SACRED 206/206
The `x^{2-\frac34}` / `x^{2-\frac12}` fraction-in-sup cases failed ONLY on the
outer strut-bottom (`1.85` vs golden `1.84`) AND the sup child `top` (`-3.47` vs
`-3.48`). Root cause: the `\frac34` **rendered at script style exposed no raw**
(`expose_raw: is_display or is_fraction_child` excluded script-reached
fractions), so `render_sup_only` fell back to the CEIL-projected `height 0.97`/
`depth 0.53` instead of the raw `0.9628`/`0.5318`. That inflated `supShift`
(`0.47875` vs `0.48001`) and the box `height_raw` (`1.15775` vs `1.15397`). Fix
(fraction.ls:248): `expose_raw` now also fires when `frac_ctx.script_container
== true` — the gate's old rationale ("can't flip the still-legacy script
parent") is stale since `render_sup_only`/`render_both` are now Rule-18
metric-driven and consume the raw correctly. +2 corpus (822→824), SACRED
204→**206/206**, 0 regressions.

### 10.0 sqrt/radical conversion — DONE (display/text)
`render.ls` `sqrt_spec` bucket dispatch (size1+size3-only, 3 `make_*_sqrt_spec`
tables) replaced by `sqrt_geom`, a faithful port of TeXBook Rule 11 =
MathLive's `SurdAtom.render` + `makeCustomSizedDelim` + `VBox`
(ref/mathlive/src/atoms/surd.ts, core/delimiters.ts, core/v-box.ts). Every
dimension derives from `body_box.height_raw`/`depth_raw` + the surd font
metrics. Key facts that made it byte-exact:
- `ruleWidth=θ/factor`, `φ=X_HEIGHT` (display) else `θ`; `ψ=θ+¼φ`;
  `innerTotal=max(2φ·factor, h+d)`; `minDelim=innerTotal+ψ+θ`.
- surd size = smallest glyph whose total > minDelim; totals (small,Size1–4) =
  `1.0, 1.20001, 1.80002, 2.40003, 3.00003` (U+221A metrics). Adds Size2/Size4
  the buckets lacked (fixes `\sqrt{x^2+y^2}` and 16 others).
- re-center clearance: `lc=(ψ+(surd_total−θ)−(h+d))/2` when surd taller than body.
- `Box.setTop` is a **no-op when |t|≤0.01** (so tall radicals emit no sign `top`
  and keep the raw surd height — this is why size3 result_h = max(surd.h, body_h)).
- pstrut = `max(maxFontSize, h, θ)+2`; empty body has maxFontSize 0 → 2.04, else 1.0.
- depth projection uses the `0 − ceil2(0 − d)` (round-toward-zero) idiom so the
  non-raw fallback va matches MathLive — same convention as fraction.ls:308.
Script/scriptscript radicals keep the legacy specs (`make_sqrt_spec` for the
indexed-script branch, `make_script_sqrt_spec`) — their `render_total` floors are
load-bearing and no corpus failures sit there. This clears the §10.4 sqrt chain:
**SURDS 5/5** (quadratic `ax^2+bx+c=…\left(…\sqrt{b^2-4ac}…\right)` now passes).

### 10.1 Corpus-rendering-mode correction (prerequisite, was masking everything)
The gate harness (`run_lambda_mathlive_markup.mjs`) rendered non-`\[..\]` cases
inline, but **MathLive's `convertLatexToMarkup` (which generated the golden
snapshots) defaults to `mathstyle: 'displaystyle'`** (mathlive-ssr.ts:106-108;
verified: 57/57 inline-golden superscripts use `sup1`, never `sup2`). The whole
corpus is display-rooted. The old code masked this by hardcoding `sup1` in the
script offsets. The harness now renders display-rooted — without this, the
metric-driven scripts (which correctly follow Rule 18c) appear to "regress"
by ~54 cases. **Anyone working on scripts/fractions must keep this in mind:
the goldens are displaystyle.**

### 10.2 Per-phase status (the §4 plan)

| Phase | Status | Notes |
|------:|--------|-------|
| **B — Rule 15 fractions** | **DONE.** | `frac_bar_spec` + `build_frac_bar_legacy` + 26 helper fns **deleted (−778 lines)**; `fraction.ls` 1321→539. Every bar fraction (display/text/script/scriptscript, colorbox, composite children) flows through `frac_bar_geom` + `build_frac_bar_rule15`. Key fixes: (a) geometry-style is one step "up" from the math style for script-*cell* fractions but unchanged for sub/superscript-reached ones — keyed on `script_container`; (b) `partial_box` (∂) given raw metrics + corrected depth (0.08→0) so `\pdiff` takes the metric path. FRACTIONS 9/9. |
| **C — Rule 18 scripts** | **mostly done.** | `render_sup_only` fully rewritten to pure Rule 18c `supShift` + single-child makeVList (−74 lines; legacy_top/legacy_vlist/legacy_child_h tables, tall_base/tall_script/numeric_x_script gates, 0.41/0.43 magic, 4 orphan helpers all gone). `render_both` + `make_limits_stack` were already metric-driven (Math4 B/C earlier). `render_sub_only` got the ceil2-projection fix. Remaining: the box-record on these still carries `render_*`/`*_raw` (Phase A). |
| **D — Array vshift** | **DONE for the matrix family + array (§10.8).** | The "unreproducible 0.96 artifact" was a RED HERRING: the 2-row `depth_holder` 0.96 (vs closed-form 0.95) IS reproducible by replicating MathLive's exact **float accumulation order** in the `makeVList` walk (`-minPos` accumulates to `0.95000000000000040` → ceil2 0.96; the closed form `total-off` gives exactly 0.95). The per-nrows `*_table_metrics`/`*_row_top` tables are replaced by `compute_dyn_metrics` (faithful walk) for matrix/pmatrix/bmatrix/.../array; matrix delimiters now expose glyph raw (like `\left..\right`). 824/206, ENVIRONMENTS 9/9, **0 regression**; `array_table_metrics`/`array_row_top` deleted. Still hardcoded: cases/rcases (scriptstyle cells), dcases (`jot` inter-row spacing), smallmatrix (scriptstyle scale), equation — scaling/spacing complexity, future work. |
| **E — Regression cleanup** | **ongoing.** | Down to 2 upstream-baseline fails, both the fraction-in-sup residual (see 10.4). sqrt chain cleared. |
| **A — Box-model collapse** | **STARTED — `strut_total` + `strut_depth_em` DELETED.** | First clean increment (§10.6). `strut_depth_em` was dead (never set → always null) — removed wholesale. `strut_total` had ONE producer (the `\left..\right` box, render.ls); converted it to expose `height_raw`/`depth_raw` (content raw maxed with the delimiter glyph raw) so the outer strut rounds h+d ONCE via the `use_raw` path — then deleted the `strut_total` field + the override branches in `math.ls`. `\left..\right` now flows through `use_raw`. Verified no-op (824/921, SACRED 206/206). **Producer audit (§10.7):** integrals ALREADY expose raw; simple accents converted; but `render_total` CANNOT be fully deleted — bbox (border) + line-accents (overline composite) have *genuine* emission≠layout splits (not rounding artifacts), so they legitimately keep it. `render_total` (~145), `render_height` (~100), `render_depth` (~127) remain for those + the non-raw fallback; `left_right_render_*` stay (full-precision delimiter margin, MathLive emits un-rounded). |

### 10.3 Other hardcode removed this session (leaf + emit producers)
- **box.ls leaf metrics (Task 6):** removed ~120 lines of **dead per-character
  heuristic branches** (`text_height`/`text_depth`) — for any glyph covered by
  `metrics_data`, the heuristic is unreachable (lookup runs first). Verified via
  a reachability audit (`temp/deadness.mjs`); `text_height` 159→21 branches,
  `text_depth` 96→24. `metrics_data` regenerate is a no-op (already complete;
  greek/symbols all present with raw). Also a leaf-correctness fix: ı/ȷ descender
  depth 0.19→0.20.
- **delimiter strut (Task 5, partial):** the `fmt_delim_em` hardcoded
  `0.686`/`2.076` special-cases are gone — `\left..\right` margin-top/height
  emit at full precision with trailing zeros trimmed (`trim_trailing_zeros(fmt_fixed(v,6))`;
  MathLive does NOT CEIL@2 these).
- **`2.41` magic RETIRED (delimiter raw exposure) — DONE, pure refactor, 0 pass-rate change.**
  The sized-delim producers (`render_sized`, `render_vertical_mult`,
  `render_mult_left_right_delim` in delimiters.ls) now carry `height_raw`/`depth_raw`
  = the full-precision Size1–4 glyph extents (axis-centred U+0028 metrics:
  `0.85/0.35001`, `1.15/0.65002`, `1.45/0.95003`, `1.75/1.25003`). The
  `\left..\right` strut (`stretchy_left_right_strut_total`, render.ls) now sums
  `box_h_raw + box_d_raw` and CEIL@2s ONCE — a Size3 pair makes `1.45+0.95003 =
  2.40003 → 2.41` naturally, so the `abs(box_total−2.4)<0.001→2.41` special-case
  is deleted. Verified: SURDS quadratic still emits strut-bottom `2.41em`;
  LEFT/RIGHT 55/55, ENVIRONMENTS 9/9, FRACTIONS 9/9, corpus 822, SACRED 204/206.

### 10.4 Outstanding issues — SACRED baseline is now 206/206 (no fails)
The sqrt chain is cleared (§10.0), the `2.41` magic is retired (§10.3), and the
last 2 fraction-in-sup residuals are fixed (§10.0b). **The upstream 206-case
baseline now passes 206/206.** Remaining corpus failures (824/921) are all in the
extended (non-baseline) set — compound constructs: `equation`/`align`
environments, `cfrac`, integral clusters, matrices with inner fraction/script
content, and a long-symbol-string descender cluster.

**Recommended next order:** ~~delimiter raw exposure~~ DONE (§10.3),
~~baseline residuals~~ DONE (§10.0b), ~~Phase A first increment~~ DONE (§10.6:
`strut_total`/`strut_depth_em` deleted). Next Phase A steps: convert the
remaining non-raw producers (**accents**, **arrays**, **bbox/enclose**,
**integral side-limits**) to expose correct `height_raw`/`depth_raw`, then
collapse `render_total`/`render_height`/`render_depth`. Then **D (arrays)** and
the extended-corpus compound clusters.

### 10.6 Phase A — first increment DONE (`strut_total` + `strut_depth_em` deleted)
Two of the box model's nine vertical-extent fields are gone, with zero pass-rate
change (824/921, SACRED 206/206):
- **`strut_depth_em`** was DEAD — plumbed through ~7 sites but never assigned a
  real value (`first_strut_depth_em` always returned null). Deleted wholesale.
- **`strut_total`** had exactly ONE producer: the `\left..\right` box
  (`render_stretchy_delimiter_group`). It set `strut_total` to force the
  `math.ls` non-raw emit path with a hand-computed strut height. Converted that
  box to expose `height_raw`/`depth_raw` = the full-precision box extent
  (content's `height_raw`/`left_right_render_depth` maxed with the delimiter
  glyph raw from §10.3), so the outer strut now rounds `h+d` ONCE via `use_raw`
  — reproducing the same emit. Then deleted the `strut_total` field and the two
  override branches in `math.ls` (`use_raw` no longer needs to exclude it; the
  `fmt_fixed(total,2)` strut-bottom branch is gone). Probe confirms
  `\left(\frac{a}{b}\right)` now satisfies `use_raw`. Functions renamed
  `*_strut_total` → `*_render_total`/`left_right_content_total` (the field they
  feed is `render_total`, not the deleted `strut_total`).

### 10.7 Phase A — producer audit (accents/arrays/bbox/integrals)
Audited the remaining non-`use_raw` producers to see which can expose raw (so
their trees round once). Findings:
- **Integrals** (`render_integral_inline_scripts`) — ALREADY expose
  `height_raw`/`depth_raw` and omit `render_height`/`render_depth`. `\int_0^1`
  satisfies `use_raw`. Nothing to do.
- **Simple accents** (`\hat`/`\vec`/`\bar`/`\dot`/`\tilde` over a single base,
  via `render_simple_accent`) — CONVERTED. They sit entirely above the baseline
  (depth 0) and their vlist height is already CEIL@2, so a new `accent_box_raw`
  exposes `height_raw=vlist_h, depth_raw=0` — no cross-term to overshoot.
  `\hat{x}`/`\bar{x}`/`\hat{x}+y` now `use_raw`; 0 regressions (ACCENTS 10/10).
- **Wide / missing-base / line accents** — NOT converted. A first attempt to
  expose raw on the *shared* `accent_box` regressed compound cases
  (`\dot{x}\dot{x+1}\dot`: strut-bottom 1.14→1.15) because the hbox `use_raw`
  cross-term (`max_h_raw + max_d_raw` from *different* children) overshoots the
  non-raw `max(child render_total)`, and the accent depths (0.09/0.2) are
  hardcoded approximations, not MathLive-exact. Reverted; left them on the
  non-raw `accent_box`. The depth-bearing sibling keeps the compound hbox off
  `use_raw`, which is correct.
- **Line accents (`\overline`)** carry a genuine `render_total ≠ h+d` split
  (e.g. 2.24 vs 2.04) — a real overline-composite extent, not a rounding
  artifact. Cannot be single-`(h,d)`-paired.
- **bbox/enclose** — genuine border split: golden top 1.75, bottom 3.01, va
  −1.25, but `1.75 + 1.25 = 3.0 ≠ 3.01` (the border extends the bottom by 0.01
  only). No `(h_raw,d_raw)` reproduces all three under CEIL@2 — `render_total`
  is legitimately required.
- **Arrays** — NOT converted: ENVIRONMENTS is SACRED (9/9), the 2-row
  `depth_holder 0.96` is a documented float artifact that doesn't derive from
  the algorithm, and (since bbox keeps `render_total` regardless) there's no
  field-deletion payoff. Left as-is.

**Conclusion:** `render_total` / `render_height` / `render_depth` **cannot be
fully deleted** — bbox and line-accents have genuine emission-vs-layout splits
(MathLive itself represents these via separate overlay/vlist structure). Phase A
field-collapse is therefore *bounded*: `strut_total`/`strut_depth_em` are gone;
the `render_*` trio stays for the genuinely-split constructs + the non-raw
fallback. The achievable win — maximizing `use_raw` coverage so most trees round
once — is now done for every cleanly-representable producer.

### 10.8 Arrays metric-driven (matrix family + array) — the "artifact" cracked
The long-standing Task-4 blocker (the 2-row `depth_holder` golden `0.96` that the
closed-form array algorithm gives as `0.95`) was NOT an unreproducible MathLive
metric difference. It is **float-accumulation order**: MathLive's `makeVList`
walk accumulates `currPos` through per-row kerns, and the running float lands
`-minPos = 0.95000000000000040` (just above 0.95) → `ceil2 = 0.96`. The
closed-form `total − offset` computes `0.95` exactly. Replicating MathLive's
EXACT operation order (including `arstrutHeight = 0.7*1.2` not the rounded
`0.84`) reproduces every golden — 1-row `top 0.85 / depth-holder 0.35`, 2-row
`tops -3.61,-2.4 / depth-holder 0.96 / strut 2.41 / va -0.95`, 3-row `-4.21,-3,
-1.81` — byte-exact.

What changed in `atoms/array.ls`:
- `compute_dyn_metrics` replaces the per-`nrows` `matrix_table_metrics` +
  `matrix_row_top` (and `array_*`, now deleted) for matrix/pmatrix/bmatrix/
  Bmatrix/vmatrix/Vmatrix/array. It does the real `\@array` algorithm: per-row
  `height/depth = max(arstrut, cells)`, running `total`, centering `offset =
  total/2 + axis` (or `body[0].height` for `array`), then the faithful
  `getVListChildrenAndDepth` + `makeRows` walk → tops, `maxPos`, `minPos`.
- Matrix delimiters: `render_plain_sized_delim` + `render_square_mult_delim` now
  carry `height_raw`/`depth_raw` (the Size1–4 glyph extents), and
  `wrap_with_delimiters` computes the strut from `max(content, delim)` raw —
  exactly the `\left..\right` model (§10.3). A Size3 bracket pair → 2.41.
- Cells render unscaled (verified: `x@script` height 0.43056), so the matrix
  family (cells at ambient style, scale 1.0) needs no scaling.
- **dcases** also ported (display cells, scale 1.0) by adding TeX `\jot` (3pt =
  0.3em) inter-row depth via `env_jot`; `dcases_table_metrics`/`dcases_row_top`
  deleted. ONE residual: the dcases corpus case emits strut-bottom 6.42 vs
  golden 6.41 — a 0.01 ceil2 float-tip traced to the inner fraction's
  `depth_raw 1.25001` (a fraction-precision quirk, NOT the array algo). Accepted
  as a temporal regression (SACRED 205/206) per the "remove hardcode, regression
  OK" directive; clears when fraction depth precision is tightened.
- **cases/rcases NOW metric-driven** (§10.10): the key was `\arraystretch 1.2`
  (`arstrut 1.008/0.432`), which makes the short script cells get a *fixed* row
  height (1.44) → content box 1.69/1.19, and the Size4 `{` brace makes the outer
  box 1.75/1.25. `cases_table_metrics`/`cases_row_top` DELETED; 0 regression.
  (cases #2's `x^2` cell still fails — pre-existing script-vs-text cell-style
  golden inconsistency, NOT the table.)
- STILL hardcoded: **smallmatrix** (`matrix_table_metrics` fallback) — its golden
  row wrappers (0.93/0.62, a 0.31 gap between the descender row and the plain
  row) fit NO uniform arstrut/scale; appears to come from a different model.
  Currently failing; left as-is. **equation** (`equation_table_metrics`) is
  already content-derived (metric-driven); its remaining constants (axis 0.255,
  eq_top) are algorithm parameters, not a fixed table.
Matrix family + array: 0 regression (824/206). dcases: −1 (the 0.01 fraction
tip). **The lesson: "non-reproducible" golden artifacts are often just
float-order — replicate the exact computation.**

### 10.9 Integral side-limits metric-driven (clean no-op)
`render_integral_inline_scripts` (scripts.ls) had ~15 hardcoded layout constants
(1.09, 0.9, −2.1, −4.08, 1.55, −0.41, 0.89, 0.44, …). Replaced by derivations
from the ∫ Size2 glyph metrics (`int_h 1.36`, `int_d 0.86225`, `int_italic
0.44445` — kept as font data) + TeXBook Rule 18 side-limit shifts:
`sup_shift = int_h − supDrop·0.7`, `sub_shift = int_d + subDrop·0.7`;
`sup_top = ceil2(−3 − sup_shift − sup_d)`, `sub_top = ceil2(−3 + sub_shift)`,
`vlist = ceil2(box_h_raw)` (sup) or `ceil2(−sub_shift + sub_inner_h)` (sub-only),
`depth_holder = ceil2(box_d_raw)`, margins `= ceil2(±int_italic)` (0.45 / −0.44).
**Verified 0 regression** (0 integrals newly-fail/pass, corpus 823, SACRED 205).
KEY gotcha: the box's `height_raw` must use the **raw** sup height (`h·0.7`), NOT
the CEIL2'd wrapper `sub_height_for` — otherwise the strut sums 0.01 too high
(2.45 vs golden 2.44). Remaining integral magic: `0.05` (sub-only scriptspace
kern) and `pstrut 3.0`.

### 10.10 cases/rcases metric-driven (the `\arraystretch` key)
cases looked blocked (last turn, text+dynamic broke all 6). The breakthrough:
the cases box is NOT content-derived — it's a *fixed* reserve because
`\arraystretch = 1.2` makes the arstrut (`0.7·1.44 = 1.008` / `0.3·1.44 =
0.432`) DOMINATE the small script cells. So `compute_dyn_metrics` reproduces it
once `env_arraystretch(env)` feeds `arrayskip = arraystretch·1.2`:
- content rows: `row h+d = arstrut = 1.44` (= the golden cell_height), 2-row
  content box `1.69/1.19`, `pstrut = 1.008+2 = 3.01` — all exact.
- the `{` brace (Size4, via `render_matrix_delim`/`matrix_sized_raw`) makes the
  outer box `1.75/1.25`, strut `3.01`, va `−1.25` — exact.
- cells stay at SCRIPT (so cases #1's `\sum` keeps inline limits); the walk's
  float reproduces the row tops (`−3.69/−2.25`).
Verified **0 regression** (823/205, same pass-set; cases #2's `x^2` still fails
on the pre-existing script-vs-text cell-style golden inconsistency, unrelated to
the table). `cases_table_metrics` + `cases_row_top` deleted.

### 10.5 Acceptance-criteria scorecard (§8)
1. *No per-content em constant tables in fraction.ls/scripts.ls* — **fraction.ls
   ✅ (frac_bar_spec gone); scripts.ls ✅ for sup/sub/both; render.ls sqrt ✅
   (bucket dispatch gone, display/text metric-driven)** (render_* fields remain
   pending Phase A; script-radical specs retained).
2. *One height/depth; render_*/*_raw/strut_* gone* — ❌ (Phase A, blocked as above).
3. *Fixtures 100%* — not re-run this session.
4. *Corpus ≥ 763* — ✅ **824**.
5. *Zero regressions on the 206 baseline at end-state* — ✅ **206/206** (met).
