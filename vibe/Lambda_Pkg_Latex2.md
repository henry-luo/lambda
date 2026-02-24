# Lambda LaTeX Package — Phase 2 Enhancement Proposal

**Date:** 2026-02-24  
**Status:** Proposal  
**Scope:** Achieve 100% LaTeX.js parity, incorporate key LaTeXML features, fully render `latex-showcase.tex`

---

## 1. Goals

1. **100% feature parity with LaTeX.js** — every command and environment that LaTeX.js supports must produce equivalent HTML output from the Lambda package.
2. **Incorporate key LaTeXML features** — adopt higher-value features from LaTeXML that improve real-world document handling (BibTeX, `\newtheorem`, counter manipulation, booktabs, etc.).
3. **Fully render `latex-showcase.tex`** — the LaTeX.js showcase document (`test/input/latex-showcase.tex`, ~500 lines) must produce complete, correct HTML with no missing content.

---

## 2. Current State

The Phase 1 package (~3,400 lines across 15 files + ~1,500 lines math package) covers:

- Document structure, sectioning (7 levels, auto-numbered, 3 docclasses)
- Text formatting, font families, font sizes (all 10)
- Math (inline, display, equation, align — delegated to math package)
- Lists (itemize, enumerate, description), block environments
- Theorem-like environments (8 types), tables, figures
- Cross-references, bibliography, footnotes
- Custom macros (`\newcommand` with `#1`–`#9`)
- Diacritics (15 commands), ligatures, ~30 symbol commands
- Standalone HTML output with CSS

Baseline: **434/434** unit tests passing. Full test script: `test/lambda/test_latex_pkg.ls`.

---

## 3. Gap Analysis

### 3.1 Gaps Exposed by `latex-showcase.tex`

The showcase document uses features in 6 categories not currently handled:

| Category | Missing Commands | Showcase Sections |
|----------|-----------------|-------------------|
| Box commands | `\fbox`, `\mbox`, `\makebox`, `\framebox`, `\parbox`, `\frame`, `\raisebox` | §5 (Boxes) |
| Picture environment | `\begin{picture}`, `\put`, `\line`, `\vector`, `\circle`, `\oval`, `\qbezier`, `\multiput`, `\linethickness`, `\thicklines`, `\thinlines` | §9 (Picture) |
| Spacing commands | `\noindent`, `\bigskip`, `\medskip`, `\smallskip`, `\medbreak`, `\smallbreak`, `\bigbreak`, `\,`, `\thinspace`, `\negthinspace`, `\enspace`, `\quad`, `\qquad`, `\\[length]` | §7 (Spacing), throughout |
| Extended symbols (100+) | `\textalpha`–`\textomega` (48 Greek), currency symbols (14), old-style numerals (10), math symbols (15), arrows (4), misc (20+), `\IJ`/`\TH`/`\DH`/`\DJ`/`\NG` | §1 (Characters), §12 (Symbols) |
| Character codes | `\symbol{"hex}`, `\char"hex`, `^^XX`, `^^^^XXXX` | §1 (Characters) |
| Font declarations & misc | `\itshape`, `\bfseries`, `\ttfamily` (environment forms), `\raggedleft`, `\centering`, `\raggedright`, `\appendix`, `\noindent`, `\/` | §8 (Verse), §13 (Fonts) |
| Low-level boxes | `\llap`, `\rlap`, `\smash`, `\phantom`, `\hphantom`, `\vphantom` | §5.1 (Low-level) |

### 3.2 Gaps for LaTeX.js Parity

Beyond the showcase, LaTeX.js also supports:

| Feature | Description |
|---------|-------------|
| `\textcolor{color}{text}` | Text coloring (xcolor) |
| `\colorbox{color}{text}` | Background color |
| `\color{name}` | Scoped color declaration |
| `\definecolor{name}{model}{spec}` | Custom color definition |
| `\includegraphics` options | `width`, `height`, `scale`, `angle` |
| Nested list styles | Per-level bullet/number formatting |
| `\centering`, `\raggedright` | Declaration-form alignment |
| enumitem package | Custom list labels (`[label=\alph*)]`) |
| hyperref metadata | `\hypersetup`, clickable internal links |

### 3.3 LaTeXML Features to Incorporate

| Feature | Value | Effort |
|---------|-------|--------|
| `\newtheorem{name}{Label}[counter]` | Custom theorem definitions with shared counters | Medium |
| Counter commands (`\setcounter`, `\addtocounter`, `\value`) | Runtime counter manipulation | Medium |
| `secnumdepth` counter | Controls which sectioning levels are numbered | Low |
| `booktabs` (`\toprule`, `\midrule`, `\bottomrule`) | Publication-quality table rules | Low |
| `\input{file}` / `\include{chapter}` | File inclusion | Medium |
| `\newcommand` optional arguments | `\newcommand{\cmd}[2][default]{...}` | Medium |
| `\autoref`, `\nameref` | Smart cross-reference text | Low |

---

## 4. Implementation Plan

### Phase 2A — Symbol Expansion (Estimated: ~200 lines)

**Priority: Highest ROI — pure data entry, unblocks §1 and §12 of showcase**

Add 100+ symbol mappings to `symbols.ls`:

**4A.1 — textgreek package (48 symbols)**

```
\textalpha → α    \textAlpha → Α
\textbeta  → β    \textBeta  → Β
\textgamma → γ    \textGamma → Γ
\textdelta → δ    \textDelta → Δ
\textepsilon → ε  \textEpsilon → Ε
\textzeta  → ζ    \textZeta  → Ζ
\texteta   → η    \textEta   → Η
\texttheta → θ    \textTheta → Θ
\textiota  → ι    \textIota  → Ι
\textkappa → κ    \textKappa → Κ
\textlambda → λ   \textLambda → Λ
\textmu    → μ    \textMu    → Μ
\textnu    → ν    \textNu    → Ν
\textxi    → ξ    \textXi    → Ξ
\textomikron → ο  \textOmikron → Ο
\textpi    → π    \textPi    → Π
\textrho   → ρ    \textRho   → Ρ
\textsigma → σ    \textSigma → Σ
\texttau   → τ    \textTau   → Τ
\textupsilon → υ  \textUpsilon → Υ
\textphi   → φ    \textPhi   → Φ
\textchi   → χ    \textChi   → Χ
\textpsi   → ψ    \textPsi   → Ψ
\textomega → ω    \textOmega → Ω
```

**4A.2 — textcomp/gensymb symbols (60+)**

Currency: `\texteuro` → €, `\textcent` → ¢, `\textsterling` → £, `\textbaht` → ฿, `\textcolonmonetary` → ₡, `\textcurrency` → ¤, `\textdong` → ₫, `\textflorin` → ƒ, `\textlira` → ₤, `\textnaira` → ₦, `\textpeso` → ₱, `\textwon` → ₩, `\textyen` → ¥

Old-style numerals: `\textzerooldstyle` → 𝟶 through `\textnineoldstyle` → 𝟿 (or standard digits with CSS)

Math: `\textperthousand` → ‰, `\textpertenthousand` → ‱, `\textonehalf` → ½, `\textthreequarters` → ¾, `\textonequarter` → ¼, `\textfractionsolidus` → ⁄, `\textdiv` → ÷, `\texttimes` → ×, `\textminus` → −, `\textpm` → ±, `\textsurd` → √, `\textlnot` → ¬, `\textasteriskcentered` → ∗, `\textonesuperior` → ¹, `\texttwosuperior` → ², `\textthreesuperior` → ³

Arrows: `\textleftarrow` → ←, `\textuparrow` → ↑, `\textrightarrow` → →, `\textdownarrow` → ↓

Misc: `\checkmark` → ✓, `\textreferencemark` → ※, `\textordfeminine` → ª, `\textordmasculine` → º, `\textmarried` → ⚭, `\textdivorced` → ⚮, `\textbardbl` → ‖, `\textbrokenbar` → ¦, `\textbigcircle` → ◯, `\textcircledP` → Ⓟ, `\textregistered` → ®, `\textservicemark` → ℠, `\texttrademark` → ™, `\textnumero` → №, `\textrecipe` → ℞, `\textestimated` → ℮, `\textmusicalnote` → ♪, `\textdiscount` → %, `\textcelsius` → ℃, `\textdegree` → °

**4A.3 — Additional non-ASCII symbols (10)**

`\IJ` → Ĳ, `\ij` → ĳ, `\TH` → Þ, `\th` → þ, `\DH` → Ð, `\dh` → ð, `\DJ` → Đ, `\dj` → đ, `\NG` → Ŋ, `\ng` → ŋ

**4A.4 — Character code commands**

- `\symbol{"XXXX}` — resolve hex code to Unicode character
- `\char"XX` — same, TeX-style
- `^^XX` / `^^^^XXXX` — character code input (may need parser support)

**Files modified:** `symbols.ls` (add mappings), `render2.ls` (add dispatch cases for new symbol commands)

---

### Phase 2B — Box Commands (Estimated: ~250 lines)

**Priority: High — used extensively in showcase §5, common in real documents**

Add a new module `elements/boxes.ls`:

| Command | HTML Output |
|---------|-------------|
| `\mbox{text}` | `<span style="white-space:nowrap">text</span>` |
| `\fbox{text}` | `<span class="latex-fbox" style="border:1px solid;padding:3px">text</span>` |
| `\frame{text}` | `<span class="latex-frame" style="border:1px solid">text</span>` |
| `\makebox[w][pos]{text}` | `<span style="display:inline-block;width:w;text-align:pos">text</span>` |
| `\framebox[w][pos]{text}` | Like `\makebox` + border |
| `\parbox[pos][h][ipos]{w}{text}` | `<div style="display:inline-block;width:w;vertical-align:pos">text</div>` |
| `\raisebox{lift}[h][d]{text}` | `<span style="position:relative;bottom:lift">text</span>` |

Low-level boxes:

| Command | HTML Output |
|---------|-------------|
| `\llap{text}` | `<span style="display:inline-block;width:0;text-align:right">text</span>` |
| `\rlap{text}` | `<span style="display:inline-block;width:0;text-align:left">text</span>` |
| `\smash{text}` | `<span style="display:inline-block;height:0;overflow:visible">text</span>` |
| `\phantom{text}` | `<span style="visibility:hidden">text</span>` |
| `\hphantom{text}` | `<span style="visibility:hidden;height:0;overflow:hidden">text</span>` |
| `\vphantom{text}` | `<span style="visibility:hidden;width:0;overflow:hidden">text</span>` |

**Implementation approach:**
1. Parse box arguments: extract optional `[width]`, `[pos]`, `{content}` from child elements
2. Map `pos` codes: `c` → center, `l` → left, `r` → right, `s` → justify
3. Map vertical `pos`: `t` → top, `b` → bottom, `c`/`m` → middle → CSS `vertical-align`
4. Recursively render inner content via `dispatcher.render_children_of()`

**Files:** Create `elements/boxes.ls`, update `render2.ls` dispatch table

---

### Phase 2C — Spacing & Layout Commands (Estimated: ~150 lines)

**Priority: High — used throughout showcase, essential for document formatting**

Update `elements/spacing.ls`:

| Command | HTML Output |
|---------|-------------|
| `\noindent` | Set flag to suppress `text-indent` on next `<p>` |
| `\bigskip` | `<div style="margin-top:12pt">` |
| `\medskip` | `<div style="margin-top:6pt">` |
| `\smallskip` | `<div style="margin-top:3pt">` |
| `\bigbreak` | Same as `\bigskip` (with penalty) |
| `\medbreak` | Same as `\medskip` |
| `\smallbreak` | Same as `\smallskip` |
| `\\[length]` | `<br style="margin-bottom:length">` or `<div style="margin-top:length">` |
| `\,` / `\thinspace` | `<span style="margin-left:0.16667em">` (or `\u2009`) |
| `\negthinspace` | `<span style="margin-left:-0.16667em">` |
| `\enspace` | `\u2002` (en space) |
| `\quad` | `\u2003` (em space, 1em) |
| `\qquad` | `<span style="margin-left:2em">` |
| `\/` | Zero-width (italic correction, ligature break) |
| `\ ` (backslash-space) | Normal space (already handled?) |

**Files:** Update `elements/spacing.ls`, `render2.ls` dispatch

---

### Phase 2D — Font Declarations & Scoping (Estimated: ~100 lines)

**Priority: Medium — needed for showcase §13 and common in real documents**

Add declaration-form font commands that apply within a group `{...}` or environment:

| Declaration | Equivalent | CSS |
|-------------|------------|-----|
| `\bfseries` | `\textbf` | `font-weight:bold` |
| `\itshape` | `\textit` | `font-style:italic` |
| `\ttfamily` | `\texttt` | `font-family:monospace` |
| `\rmfamily` | `\textrm` | `font-family:serif` |
| `\sffamily` | `\textsf` | `font-family:sans-serif` |
| `\scshape` | `\textsc` | `font-variant:small-caps` |
| `\slshape` | `\textsl` | `font-style:oblique` |
| `\upshape` | — | `font-style:normal` |
| `\mdseries` | — | `font-weight:normal` |

These declarations wrap their scope in the appropriate `<span style="...">` or `<em>`/`<strong>`.

When used as environments (`\begin{itshape}...\end{itshape}`), the entire body is wrapped.

Alignment declarations:

| Command | CSS |
|---------|-----|
| `\centering` | `text-align:center` on containing block |
| `\raggedright` | `text-align:left` |
| `\raggedleft` | `text-align:right` |

**Files:** Update `render2.ls` dispatch, possibly new `elements/font_decl.ls`

---

### Phase 2E — `\appendix` and Counter Control (Estimated: ~80 lines)

**Priority: Medium — needed for showcase §14, useful for real documents**

**`\appendix` command:**
- Sets a flag in the analysis pass `info`
- After `\appendix`, section numbering switches to A, B, C… (uppercase letters)
- Subsections become A.1, A.2, B.1, etc.
- Implementation: modify `analyze.ls` to detect `\appendix` and switch counter formatting from `arabic` to `Alph` for sections

**`\setcounter{secnumdepth}{N}`:**
- Currently parsed but ignored
- Extract value in analysis pass; suppress numbering for levels deeper than N
- Showcase uses `\setcounter{secnumdepth}{2}` — only number down to subsection

**Counter display commands (LaTeXML feature):**
- `\arabic{counter}` → "1", "2", "3"
- `\roman{counter}` → "i", "ii", "iii"
- `\Roman{counter}` → "I", "II", "III"
- `\alph{counter}` → "a", "b", "c"
- `\Alph{counter}` → "A", "B", "C"
- `\fnsymbol{counter}` → *, †, ‡, §, ¶

**Files:** `analyze.ls` (appendix detection, secnumdepth), `render2.ls` (dispatch `\appendix`), docclass modules (counter formatting)

---

### Phase 2F — Color Support (Estimated: ~120 lines)

**Priority: Medium — LaTeX.js parity, increasingly common**

| Command | HTML Output |
|---------|-------------|
| `\textcolor{red}{text}` | `<span style="color:red">text</span>` |
| `\color{blue}` | Scoped: wraps remaining group content in `<span style="color:blue">` |
| `\colorbox{yellow}{text}` | `<span style="background-color:yellow;padding:2px">text</span>` |
| `\fcolorbox{border}{bg}{text}` | `<span style="border:1px solid border;background-color:bg;padding:2px">text</span>` |
| `\definecolor{name}{model}{spec}` | Register in color map; resolve in subsequent `\textcolor` etc. |
| `\pagecolor{color}` | Set `background-color` on `<body>` or `<article>` |

**Color models to support:**
- Named colors: `red`, `blue`, `green`, `black`, `white`, `cyan`, `magenta`, `yellow`, `darkgray`, `gray`, `lightgray`, `brown`, `lime`, `olive`, `orange`, `pink`, `purple`, `teal`, `violet`
- RGB: `{R,G,B}` where values are 0–1 → `rgb(R*255, G*255, B*255)`
- HTML: `{RRGGBB}` → `#RRGGBB`

**Files:** New `elements/color.ls`, update `render2.ls` dispatch

---

### Phase 2G — Picture Environment → SVG (Estimated: ~400 lines)

**Priority: Medium-High — 7 diagrams in showcase, but largest implementation effort**

Render `\begin{picture}(w,h)(x0,y0)...\end{picture}` as inline SVG.

**Architecture:**
```
picture AST → walk children → build SVG elements → output <svg>
```

**Commands to implement:**

| Command | SVG Output |
|---------|------------|
| `\put(x,y){object}` | Position object at (x,y) via `transform="translate(x,y)"` |
| `\line(dx,dy){length}` | `<line x1 y1 x2 y2>` |
| `\vector(dx,dy){length}` | `<line>` with `marker-end="url(#arrowhead)"` |
| `\circle{diameter}` | `<circle r="d/2">` (stroke only) |
| `\circle*{diameter}` | `<circle r="d/2">` (filled) |
| `\oval(w,h)[part]` | `<rect rx ry>` or partial rounded rect via `<path>` |
| `\qbezier(x1,y1)(cx,cy)(x2,y2)` | `<path d="M x1,y1 Q cx,cy x2,y2">` |
| `\multiput(x,y)(dx,dy){n}{object}` | Loop generating n copies offset by (dx,dy) |
| `\linethickness{len}` | Set `stroke-width` |
| `\thicklines` | Set `stroke-width:0.8pt` |
| `\thinlines` | Set `stroke-width:0.4pt` |
| Text inside `\put` | `<text>` or `<foreignObject>` for LaTeX content |
| Math inside `\put` | Render math, wrap in `<foreignObject>` |

**Unit handling:**
- `\setlength{\unitlength}{Xcm}` → scale factor for all coordinates
- Convert `\unitlength` to SVG viewBox pixels (1cm = ~37.8px, 1mm = ~3.78px)

**SVG wrapper:**
```html
<svg class="latex-picture" viewBox="x0 y0 w h" width="W" height="H"
     style="overflow:visible" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="arrowhead">...</marker></defs>
  <!-- child elements -->
</svg>
```

Note: Y-axis in LaTeX picture is bottom-up; SVG is top-down. Apply `transform="scale(1,-1)"` or flip coordinates.

**Files:** New `elements/picture.ls` (~300 lines), update `render2.ls` dispatch, add SVG-aware serialization to `to_html.ls`

---

### Phase 2H — Enhanced List Rendering (Estimated: ~100 lines)

**Priority: Medium — LaTeX.js parity, improves visual fidelity**

**Nested list styles:**

itemize levels:
1. • (bullet, `\u2022`)
2. – (en-dash, `\u2013`)
3. ∗ (asterisk, `\u2217`)
4. · (middle dot, `\u00B7`)

enumerate levels:
1. `1.`, `2.`, `3.` (Arabic)
2. `(a)`, `(b)`, `(c)` (lowercase alpha in parens)
3. `i.`, `ii.`, `iii.` (lowercase Roman)
4. `A.`, `B.`, `C.` (uppercase alpha)

**Implementation:** Track nesting depth in render context. Set CSS `list-style-type` per level, or use custom `<li>` markers via `::marker` or `content`.

**Custom item labels:** `\item[-]` → render with specified label instead of default marker.

**Files:** Update list rendering in `render2.ls`, add CSS rules to `css.ls`

---

### Phase 2I — `\includegraphics` Options (Estimated: ~60 lines)

**Priority: Medium — needed for real documents, LaTeX.js parity**

Parse optional arguments `[key=value,...]` from the `\includegraphics` node:

| Option | CSS/HTML |
|--------|----------|
| `width=Xcm` | `style="width:Xcm"` |
| `height=Xcm` | `style="height:Xcm"` |
| `scale=0.5` | `style="transform:scale(0.5)"` |
| `angle=90` | `style="transform:rotate(90deg)"` |
| `keepaspectratio` | Add `object-fit:contain` |
| `trim=l b r t`, `clip` | `style="clip-path:inset(t r b l)"` (approximate) |

**Files:** Update `render2.ls` `render_includegraphics()`, add option parser to `util.ls`

---

### Phase 2J — LaTeXML-Inspired Enhancements (Estimated: ~200 lines)

**Priority: Lower — valuable but less urgent than LaTeX.js parity**

**2J.1 — `\newtheorem` (custom theorem definitions)**
```latex
\newtheorem{thm}{Theorem}[section]       % numbered within sections
\newtheorem{lem}[thm]{Lemma}             % shares counter with thm
\newtheorem*{remark*}{Remark}            % unnumbered variant
```
- Collect definitions in analysis pass
- Register custom environment names with their labels and counter groups
- Render using the same `render_theorem()` infrastructure with custom label text

**2J.2 — booktabs table rules**
| Command | HTML |
|---------|------|
| `\toprule` | `border-top:2px solid` on first row |
| `\midrule` | `border-top:1px solid` on current row |
| `\bottomrule` | `border-bottom:2px solid` on last row |
| `\cmidrule{2-4}` | `border-top` on specific columns |

**2J.3 — `\input{file}` / `\include{file}`**
- Resolve relative path from current document
- Parse included file as LaTeX, merge AST into parent
- Requires runtime file I/O (use `input()` function)

**2J.4 — `\newcommand` optional arguments**
```latex
\newcommand{\greeting}[2][World]{Hello, #1 and #2!}
\greeting{Alice}        % → Hello, World and Alice!
\greeting[Bob]{Alice}   % → Hello, Bob and Alice!
```
- Detect optional argument pattern `[default]` in macro definition
- During expansion: if first child is `[...]`, use it; otherwise use default

**2J.5 — `\autoref` / `\nameref`**
- `\autoref{sec:intro}` → "Section 1" (auto-generates type prefix)
- `\nameref{sec:intro}` → "Introduction" (uses the section title)
- Requires label-to-type mapping in analysis pass

**Files:** `analyze.ls` (theorem defs, `\autoref` labels), `render2.ls` (new dispatches), `macros.ls` (optional args)

---

## 5. File Impact Summary

| File | Changes |
|------|---------|
| `symbols.ls` | +100 symbol mappings (textgreek, textcomp, gensymb, non-ASCII) |
| `render2.ls` | ~40 new dispatch cases (boxes, spacing, font decls, colors, picture, symbols) |
| `elements/spacing.ls` | Add `\noindent`, `\bigskip`, `\medskip`, `\smallskip`, breaks, `\quad`, `\qquad`, `\enspace`, `\\[len]` |
| `elements/boxes.ls` | **New file** — `\fbox`, `\mbox`, `\makebox`, `\framebox`, `\parbox`, `\raisebox`, `\llap`, `\rlap`, `\smash`, `\phantom` |
| `elements/picture.ls` | **New file** — Picture environment → SVG rendering |
| `elements/color.ls` | **New file** — `\textcolor`, `\color`, `\colorbox`, `\definecolor` |
| `analyze.ls` | `\appendix` detection, `secnumdepth`, `\newtheorem` collection |
| `macros.ls` | Optional argument support for `\newcommand` |
| `css.ls` | Styles for boxes, picture, colors, nested list markers, booktabs |
| `to_html.ls` | SVG element serialization support |
| `util.ls` | Option parser for `[key=val,...]`, character code resolution |
| `normalize.ls` | Minimal changes (symbol normalization) |

**Estimated total new code:** ~1,500–1,800 lines of Lambda Script

---

## 6. Implementation Order & Milestones

### Milestone 1: Symbol & Spacing Parity (Phases 2A + 2C)
- **Effort:** ~350 lines, ~1 session
- **Unlocks:** Showcase §1 (Characters), §7 (Spacing), §12 (Symbols)
- **Test:** Run showcase, verify all symbol sections render correctly

### Milestone 2: Box Commands (Phase 2B)
- **Effort:** ~250 lines, ~1 session
- **Unlocks:** Showcase §5 (Boxes), §5.1 (Low-level boxes)
- **Test:** All `\fbox`, `\makebox`, `\parbox` examples render with borders and alignment

### Milestone 3: Font Declarations & Misc (Phases 2D + 2E)
- **Effort:** ~180 lines, ~0.5 session
- **Unlocks:** Showcase §8.4 (Verse — `\raggedleft`), §13 (Fonts — `\itshape`), §14 (`\appendix`), `secnumdepth`
- **Test:** Font declaration environments work, appendix sections numbered A, B, C

### Milestone 4: Color & Lists (Phases 2F + 2H)
- **Effort:** ~220 lines, ~1 session
- **Unlocks:** Full LaTeX.js parity for text colors, proper nested list styling
- **Test:** `\textcolor{red}{text}` renders, nested lists show correct markers per level

### Milestone 5: `\includegraphics` Options (Phase 2I)
- **Effort:** ~60 lines, ~0.5 session
- **Unlocks:** `width`, `height`, `scale` on images
- **Test:** Images render with specified dimensions

### Milestone 6: Picture Environment (Phase 2G)
- **Effort:** ~400 lines, ~2 sessions
- **Unlocks:** Showcase §9 (Picture) — all 7 diagrams
- **Test:** SVG output matches picture geometry; lines, circles, vectors, Bézier curves visible

### Milestone 7: LaTeXML Features (Phase 2J)
- **Effort:** ~200 lines, ~1 session
- **Unlocks:** `\newtheorem`, booktabs, optional macro args, `\input`, `\autoref`
- **Test:** Custom theorem envs render with correct labels and numbering

---

## 7. Testing Strategy

### 7.1 Showcase Regression Test

Add `latex-showcase.tex` as an integration test:
```bash
./lambda.exe convert test/input/latex-showcase.tex -t html -o test_output/latex-showcase.html --full-document
```

Verify:
- No missing content (every section produces output)
- All 100+ symbols render as correct Unicode characters
- Box examples show borders and alignment
- Picture diagrams produce SVG
- Font declarations apply correctly
- Appendix sections numbered A, B

### 7.2 Per-Feature Unit Tests

For each phase, add test cases to `test/lambda/` with `.ls` + `.txt` pairs:

| Test File | Coverage |
|-----------|----------|
| `test_latex_symbols.ls` | All 100+ new symbol commands |
| `test_latex_boxes.ls` | `\fbox`, `\makebox`, `\framebox`, `\parbox`, `\phantom` |
| `test_latex_spacing.ls` | `\noindent`, `\bigskip`, `\quad`, `\\[len]`, etc. |
| `test_latex_color.ls` | `\textcolor`, `\colorbox`, `\definecolor` |
| `test_latex_picture.ls` | Picture env → SVG output |
| `test_latex_appendix.ls` | `\appendix`, `secnumdepth` |
| `test_latex_font_decl.ls` | `\itshape`, `\bfseries`, `\raggedleft` |

### 7.3 Baseline Must Pass

All 434 existing baseline tests must continue passing after each phase. Run:
```bash
make test-lambda-baseline
```

---

## 8. Success Criteria

| Criterion | Metric |
|-----------|--------|
| LaTeX.js parity | Every LaTeX.js-supported command produces equivalent HTML |
| Showcase renders | `latex-showcase.tex` produces complete HTML with no missing sections |
| Symbol coverage | 100+ new symbols render as correct Unicode |
| Box commands | All 10 box commands produce correct layout |
| Picture environment | All 7 showcase diagrams render as SVG |
| No regressions | 434+ baseline tests pass |
| Code quality | All new code follows Lambda conventions (pure functional, no mutation) |

---

## 9. Risks and Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Picture environment SVG complexity | Medium | High | Start with basic `\line`/`\circle`; add `\qbezier`/`\oval` incrementally |
| Parser doesn't emit nodes for some commands | Medium | Medium | Check tree-sitter grammar coverage; add missing rules if needed via `grammar.js` |
| `\noindent` requires paragraph-level state | Low | Medium | Track as a flag in render context, apply to next `<p>` element |
| Color scoping across group boundaries | Medium | Low | Wrap scoped `\color` in `<span>` within its group |
| SVG serialization in `to_html.ls` | Low | Medium | SVG elements use same tag/attribute model; extend serializer for self-closing SVG tags |
| Performance with 100+ symbol lookups | Low | Low | Use map lookup (O(1)) not linear search |
