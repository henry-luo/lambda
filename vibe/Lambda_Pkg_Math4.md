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
