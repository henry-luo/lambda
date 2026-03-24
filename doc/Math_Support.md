# Lambda Math Support

Lambda supports mathematical typesetting via a dedicated LaTeX math parser and a pure Lambda Script rendering pipeline. Math can appear in three contexts: **standalone**, embedded in **Markdown**, or embedded in a **LaTeX document**.

---

## 1. Input Modes

### 1.1 Standalone Math

Pass a raw math expression string directly to `input()` with type `'math'` or `'math-latex'`:

```lambda
let ast = input('\frac{a+b}{c}', 'math')
```

| Type string      | Meaning                                  |
|-----------------|------------------------------------------|
| `'math'`        | LaTeX math (default)                     |
| `'math-latex'`  | Explicit LaTeX math                      |
| `'math-ascii'`  | ASCII Math flavor (see §7)               |

The result is a Lambda element tree rooted at a `math` element, which can be rendered to HTML via the `lambda.package.math` package.

### 1.2 Math in Markdown

Inline and block math are extracted automatically when parsing a Markdown document:

```markdown
Inline: $E = mc^2$.

Display block:
$$
\int_0^\infty e^{-x^2}\,dx = \frac{\sqrt{\pi}}{2}
$$
```

- **Inline** — delimited by single `$...$`. Backslash-escaped `\$` is not treated as a math delimiter.
- **Display block** — delimited by `$$...$$`, either on a single line or spanning multiple lines (opening `$$`, body lines, closing `$$` on its own line).

```lambda
let doc = input('./report.md', 'markdown')
```

### 1.3 Math in LaTeX Documents

When parsing a full LaTeX document (`type: 'latex'`), math mode content inside `$...$`, `$$...$$`, or math environments is detected automatically and parsed by the same tree-sitter-latex-math grammar.

```lambda
let doc = input('./paper.tex', 'latex')
```

---

## 2. Rendering Math to HTML

Math is rendered to HTML using the `lambda.package.math` package. The package converts the parsed AST into a MathLive-compatible `<span>` element tree, which can be serialised with `format(result, 'html')`.

### 2.1 Lambda API

```lambda
import math: lambda.package.math

// Parse and render in one step
let ast     = input('\sum_{k=1}^{n} k^2', 'math')
let inline  = math.render_inline(ast)        // inline (text) style
let display = math.render_display(ast)       // display (block) style
let alone   = math.render_standalone(ast)    // display + embedded CSS

// Lower-level: pass options explicitly
let html_el = math.render_math(ast, {display: true, standalone: false, color: "navy"})

// Serialise
let html_str = format(html_el, 'html')

// Get the CSS stylesheet string separately
let css = math.stylesheet()
```

| Function               | Description                                    |
|------------------------|------------------------------------------------|
| `render_inline(ast)`   | Renders in text (inline) style                 |
| `render_display(ast)`  | Renders in display (block) style               |
| `render_standalone(ast)` | Display mode + wraps with `<style>` block    |
| `render_math(ast, opts)` | Full control via options map                 |
| `stylesheet()`         | Returns standalone CSS string                  |

### 2.2 Render Options

| Option       | Type    | Default | Description                        |
|--------------|---------|---------|------------------------------------|
| `display`    | bool    | `false` | Display (block) vs inline style    |
| `standalone` | bool    | `false` | Embed CSS `<style>` in output      |
| `color`      | string  | inherit | Override foreground color          |

### 2.3 Output Format

The output HTML uses **MathLive CSS class names** (`ML__latex`, `ML__mfrac`, etc.) and **KaTeX-compatible sizing fonts** (`KaTeX_Size1`–`KaTeX_Size4`) for large delimiters. The package CSS (identical to MathLive's) must be loaded on the page for correct rendering; `render_standalone` embeds it automatically.

---

## 3. Structural Constructs

### 3.1 Fractions

| LaTeX              | Rendered as            | Notes                           |
|--------------------|------------------------|---------------------------------|
| `\frac{n}{d}`      | Standard fraction      |                                 |
| `\dfrac{n}{d}`     | Display-size fraction  | Forces display style            |
| `\tfrac{n}{d}`     | Text-size fraction     | Forces text style               |
| `\cfrac{n}{d}`     | Continued fraction     |                                 |
| `\binom{n}{k}`     | Binomial coefficient   | Parenthesised, no bar           |
| `\dbinom{n}{k}`    | Display-size binom     |                                 |
| `\tbinom{n}{k}`    | Text-size binom        |                                 |
| `\genfrac{l}{r}{t}{s}{n}{d}` | Generalised fraction |              |
| `n \over d`        | Infix fraction         | Plain TeX style                 |
| `n \choose k`      | Infix binomial         | `(n k)` form                    |
| `n \above t d`     | Infix with thickness   |                                 |
| `n \atop d`        | Fraction without bar   |                                 |
| `n \brace d`       | Braced infix           |                                 |
| `n \brack d`       | Bracketed infix        |                                 |

### 3.2 Radicals

| LaTeX              | Rendered as            |
|--------------------|------------------------|
| `\sqrt{x}`         | Square root            |
| `\sqrt[n]{x}`      | n-th root              |

### 3.3 Subscripts and Superscripts

```latex
a_i          % subscript
a^2          % superscript
a_i^n        % both
a_{ij}^{n+1} % grouped
```

Limit-style placement for big operators is automatic in display mode:

```latex
\sum_{k=1}^{n}     % limits above/below in display, inline otherwise
\lim_{x \to 0}     % limits below in display
```

Force or suppress limits explicitly:

```latex
\sum\limits_{k=1}^{n}    % always above/below
\int\nolimits_0^1        % always inline
```

### 3.4 Accents

| Command          | Accent | Command          | Accent          |
|------------------|--------|------------------|-----------------|
| `\hat{a}`        | â      | `\widehat{ABC}`  | wide â          |
| `\tilde{a}`      | ã      | `\widetilde{AB}` | wide ã          |
| `\bar{a}`        | ā      | `\overline{AB}`  | overline        |
| `\vec{a}`        | a⃗     | `\dot{a}`        | ȧ               |
| `\ddot{a}`       | ä      | `\acute{a}`      | á               |
| `\grave{a}`      | à      | `\breve{a}`      | ă               |
| `\check{a}`      | ǎ      | `\mathring{a}`   | å               |
| `\dddot{a}`      | triple dot       | `\ddddot{a}` | quad dot    |
| `\overbrace{..}` | overbrace        | `\underbrace{..}` | underbrace |
| `\overleftarrow{..}` | arrow      | `\overrightarrow{..}` | arrow  |
| `\overleftrightarrow{..}` | arrow | `\underleftarrow{..}` | arrow |
| `\underrightarrow{..}` | arrow  | `\underleftrightarrow{..}` | arrow |

### 3.5 Delimiters

**Auto-sizing** (`\left` / `\right`):

```latex
\left( \frac{a}{b} \right)
\left[ x \right]
\left\{ n \in \mathbb{N} \right\}
\left| x \right|
\left\langle v \right\rangle
\left\lfloor x \right\rfloor
\left\lceil x \right\rceil
\left. \frac{df}{dx} \right|_{x=0}   % \left. is an invisible left delimiter
```

**Fixed-size** (`\big` family):

| Command   | Scale |
|-----------|-------|
| `\big`    | 1.2×  |
| `\Big`    | 1.8×  |
| `\bigg`   | 2.4×  |
| `\Bigg`   | 3.0×  |

Each has `l` / `r` / `m` variants: `\bigl(`, `\bigr)`, `\bigm|`.

**Middle delimiter**:

```latex
\left( a \middle| b \right)
```

Supported delimiter characters: `( ) [ ] \{ \} | \| \vert \Vert \langle \rangle \lfloor \rfloor \lceil \rceil \lmoustache \rmoustache / \backslash \uparrow \downarrow \updownarrow \Uparrow \Downarrow \Updownarrow \lbrace \rbrace \lbrack \rbrack \lgroup \rgroup \lvert \rvert \lVert \rVert`.

### 3.6 Environments (Matrices and Aligned)

Use `\begin{env}...\end{env}` syntax. Separate columns with `&` and rows with `\\`.

| Environment   | Delimiters       | Notes                         |
|---------------|------------------|-------------------------------|
| `matrix`      | none             |                               |
| `pmatrix`     | `( )`            |                               |
| `bmatrix`     | `[ ]`            |                               |
| `vmatrix`     | `\| \|`          |                               |
| `Vmatrix`     | `‖ ‖`            |                               |
| `Bmatrix`     | `\{ \}`          |                               |
| `smallmatrix` | none, script size| Compact inline matrix         |
| `cases`       | `\{` left only   | Piecewise definitions         |
| `dcases`      | `\{` left, display size |                          |
| `rcases`      | right `\}` only  |                               |
| `aligned`     | none             | Multi-line aligned equations  |
| `gathered`    | none             | Centered multi-line           |
| `align`       | none             | Full align environment        |
| `align*`      | none             | Unnumbered align              |
| `split`       | none             | Split across lines            |
| `array`       | none             | General array with col spec   |
| `subarray`    | none             | Small array for scripts       |
| `equation`    | none             | Single equation (no numbering)|
| `equation*`   | none             | Unnumbered equation           |
| `gather`      | none             | Centered equations            |
| `gather*`     | none             | Unnumbered gather             |
| `multline`    | none             | Multi-line single equation    |
| `multline*`   | none             | Unnumbered multline           |

Plain TeX alternatives: `\matrix{...}`, `\pmatrix{...}`, `\bordermatrix{...}`.

Example:

```latex
\begin{pmatrix}
  a & b \\
  c & d
\end{pmatrix}
```

```latex
f(x) = \begin{cases}
  x^2 & \text{if } x \geq 0 \\
  0   & \text{otherwise}
\end{cases}
```

---

## 4. Symbols

### 4.1 Greek Letters

| Command       | Symbol | Command       | Symbol |
|---------------|--------|---------------|--------|
| `\alpha`      | α      | `\beta`       | β      |
| `\gamma`      | γ      | `\delta`      | δ      |
| `\epsilon`    | ϵ      | `\varepsilon` | ε      |
| `\zeta`       | ζ      | `\eta`        | η      |
| `\theta`      | θ      | `\vartheta`   | ϑ      |
| `\iota`       | ι      | `\kappa`      | κ      |
| `\lambda`     | λ      | `\mu`         | μ      |
| `\nu`         | ν      | `\xi`         | ξ      |
| `\pi`         | π      | `\varpi`      | ϖ      |
| `\rho`        | ρ      | `\varrho`     | ϱ      |
| `\sigma`      | σ      | `\varsigma`   | ς      |
| `\tau`        | τ      | `\upsilon`    | υ      |
| `\phi`        | ϕ      | `\varphi`     | φ      |
| `\chi`        | χ      | `\psi`        | ψ      |
| `\omega`      | ω      |               |        |

Uppercase Greek: `\Gamma Δ`, `\Delta Δ`, `\Theta Θ`, `\Lambda Λ`, `\Xi Ξ`, `\Pi Π`, `\Sigma Σ`, `\Upsilon Υ`, `\Phi Φ`, `\Psi Ψ`, `\Omega Ω`.

### 4.2 Binary Operators

`\pm ±`, `\mp ∓`, `\times ×`, `\div ÷`, `\cdot ⋅`, `\ast ∗`, `\star ⋆`, `\circ ∘`, `\bullet ∙`, `\oplus ⊕`, `\ominus ⊖`, `\otimes ⊗`, `\oslash ⊘`, `\odot ⊙`, `\dagger †`, `\ddagger ‡`, `\cap ∩`, `\cup ∪`, `\sqcap ⊓`, `\sqcup ⊔`, `\vee ∨`, `\wedge ∧`, `\setminus ∖`, `\wr ≀`, `\amalg ⨿`, `\land ∧`, `\lor ∨`.

### 4.3 Relations

`\leq ≤`, `\le ≤`, `\geq ≥`, `\ge ≥`, `\neq ≠`, `\ne ≠`, `\equiv ≡`, `\prec ≺`, `\succ ≻`, `\sim ∼`, `\simeq ≃`, `\approx ≈`, `\cong ≅`, `\subset ⊂`, `\supset ⊃`, `\subseteq ⊆`, `\supseteq ⊇`, `\sqsubseteq ⊑`, `\sqsupseteq ⊒`, `\in ∈`, `\ni ∋`, `\notin ∉`, `\vdash ⊢`, `\dashv ⊣`, `\models ⊨`, `\mid ∣`, `\parallel ∥`, `\perp ⊥`, `\propto ∝`, `\asymp ≍`, `\bowtie ⋈`, `\ll ≪`, `\gg ≫`, `\doteq ≐`, `\trianglelefteq ⊴`, `\trianglerighteq ⊵`.

### 4.4 Arrows

`\leftarrow ←`, `\rightarrow →` (= `\to`), `\leftrightarrow ↔`, `\uparrow ↑`, `\downarrow ↓`, `\updownarrow ↕`, `\Leftarrow ⇐`, `\Rightarrow ⇒`, `\Leftrightarrow ⇔` (= `\iff`), `\Uparrow ⇑`, `\Downarrow ⇓`, `\Updownarrow ⇕`, `\longleftarrow ⟵`, `\longrightarrow ⟶`, `\longleftrightarrow ⟷`, `\Longleftarrow ⟸`, `\Longrightarrow ⟹` (= `\implies`), `\Longleftrightarrow ⟺`, `\hookrightarrow ↪`, `\hookleftarrow ↩`, `\mapsto ↦`, `\longmapsto ⟼`, `\nearrow ↗`, `\nwarrow ↖`, `\searrow ↘`, `\swarrow ↙`, `\gets ←`.

### 4.5 Big Operators

These render at 1.5× size in display mode with limits placed above/below:

`\sum ∑`, `\prod ∏`, `\coprod ∐`, `\int ∫`, `\iint ∬`, `\iiint ∭`, `\oint ∮`, `\bigcup ⋃`, `\bigcap ⋂`, `\bigsqcup ⊔`, `\bigvee ⋁`, `\bigwedge ⋀`, `\bigoplus ⨁`, `\bigotimes ⨂`, `\bigodot ⨀`, `\biguplus ⨄`.

### 4.6 Operator Names (Upright)

These render in upright (roman) font: `\arccos`, `\arcsin`, `\arctan`, `\arg`, `\cos`, `\cosh`, `\cot`, `\coth`, `\csc`, `\deg`, `\det`, `\dim`, `\exp`, `\gcd`, `\hom`, `\inf`, `\ker`, `\lg`, `\lim`, `\liminf`, `\limsup`, `\ln`, `\log`, `\max`, `\min`, `\Pr`, `\sec`, `\sin`, `\sinh`, `\sup`, `\tan`, `\tanh`.

Custom operator names via `\operatorname{name}`.

### 4.7 Miscellaneous Symbols

`\infty ∞`, `\nabla ∇`, `\partial ∂`, `\forall ∀`, `\exists ∃`, `\nexists ∄`, `\emptyset ∅`, `\varnothing ∅`, `\neg ¬` (= `\lnot`), `\surd √`, `\top ⊤`, `\bot ⊥`, `\angle ∠`, `\triangle △`, `\backslash ∖`, `\ell ℓ`, `\wp ℘`, `\Re ℜ`, `\Im ℑ`, `\aleph ℵ`, `\beth ℶ`, `\gimel ℷ`, `\hbar ℏ`, `\imath ı`, `\jmath ȷ`, `\prime ′`, `\ldots …`, `\cdots ⋯`, `\vdots ⋮`, `\ddots ⋱`, `\checkmark ✓`, `\maltese ✠`, `\degree °`, `\copyright ©`, `\flat ♭`, `\natural ♮`, `\sharp ♯`, `\clubsuit ♣`, `\diamondsuit ♢`, `\heartsuit ♡`, `\spadesuit ♠`.

---

## 5. Style and Font Commands

### 5.1 Math Font Families

| Command        | Style              | Example        |
|----------------|--------------------|----------------|
| `\mathrm{A}`   | Roman (upright)    | A              |
| `\mathbf{A}`   | Bold               | **A**          |
| `\mathit{A}`   | Italic (default)   | *A*            |
| `\mathbb{R}`   | Blackboard bold    | ℝ              |
| `\mathcal{L}`  | Calligraphic       | ℒ              |
| `\mathfrak{g}` | Fraktur            | 𝔤              |
| `\mathtt{x}`   | Typewriter         | x              |
| `\mathsf{x}`   | Sans-serif         | x              |
| `\mathscr{F}`  | Script             | ℱ              |

### 5.2 Style Switching

| Command              | Effect                                 |
|----------------------|----------------------------------------|
| `\displaystyle`      | Force display-mode size inside inline  |
| `\textstyle`         | Force text-mode size inside display    |
| `\scriptstyle`       | Force subscript size                   |
| `\scriptscriptstyle` | Force double-subscript size            |

### 5.3 Text Mode Commands

Embeds upright roman text inside math:

```latex
x \in \mathbb{R} \quad \text{where } x > 0
```

| Command       | Notes                           |
|---------------|---------------------------------|
| `\text{...}`  | Roman text, spaces preserved    |
| `\textrm{...}`| Roman text                      |
| `\textbf{...}`| Bold text                       |
| `\textit{...}`| Italic text                     |
| `\textsf{...}`| Sans-serif text                 |
| `\texttt{...}`| Typewriter text                 |
| `\mbox{...}`  | Same as `\text`                 |

---

## 6. Spacing

### 6.1 Named Spacing Commands

| Command        | Width      | Description        |
|----------------|------------|--------------------|
| `\,`           | 3/18 em    | Thin space         |
| `\thinspace`   | 3/18 em    | Thin space (alias) |
| `\:`           | 4/18 em    | Medium space       |
| `\medspace`    | 4/18 em    | Medium (alias)     |
| `\;`           | 5/18 em    | Thick space        |
| `\thickspace`  | 5/18 em    | Thick (alias)      |
| `\!`           | −3/18 em   | Negative thin      |
| `\negthinspace`| −3/18 em   | Negative thin (alias) |
| `\enspace`     | 0.5 em     |                    |
| `\quad`        | 1 em       |                    |
| `\qquad`       | 2 em       |                    |

### 6.2 Explicit Dimensions

```latex
\hspace{1.5em}    % horizontal space
\hspace*{0.5em}   % non-breakable variant
\hskip 2pt        % plain TeX skip
\kern 3pt         % kern (raw)
\mskip 4mu        % math-unit kern (18mu = 1em)
\mkern 2mu        % math kern
```

Supported units: `em`, `ex`, `pt`, `pc`, `cm`, `mm`, `in`, `mu`.

### 6.3 Inter-Atom Spacing

Spacing between adjacent atoms follows the TeX 8×8 spacing table (ord/op/bin/rel/open/close/punct/inner). This is applied automatically by the renderer.

---

## 7. Color

```latex
\textcolor{red}{x+y}          % named color
\textcolor{#0000ff}{x+y}      % hex color
\color{blue} x + y            % sets foreground for rest of group
\colorbox{yellow}{x+y}        % background color box
```

Supported named colors: CSS color keywords (e.g., `red`, `blue`, `green`, `black`, `white`, `gray`, `navy`, `orange`, …) plus any hex value `#rrggbb`.

---

## 8. Enclosures and Overlap

| Command         | Effect                                      |
|-----------------|---------------------------------------------|
| `\boxed{x}`     | Box with border around content              |
| `\fbox{x}`      | Same as `\boxed`                            |
| `\bbox[...]{x}` | Box with options                            |
| `\phantom{x}`   | Invisible, takes up full space of `x`       |
| `\hphantom{x}`  | Invisible, keeps width only                 |
| `\vphantom{x}`  | Invisible, keeps height/depth only          |
| `\smash{x}`     | Zero height/depth (visible)                 |
| `\llap{x}`      | Right-overlap (zero-width, right-aligned)   |
| `\rlap{x}`      | Left-overlap (zero-width, left-aligned)     |
| `\clap{x}`      | Centre-overlap (zero-width, centred)        |

> **Note:** `\cancel`, `\bcancel`, `\xcancel` are defined in the grammar but not yet rendered — they are planned for a future update.

---

## 9. Over/Under and Stacking

| Command                    | Effect                              |
|----------------------------|-------------------------------------|
| `\overset{A}{B}`           | A above B                           |
| `\underset{A}{B}`          | A below B                           |
| `\overline{x}`             | Overline bar (also in accents)      |
| `\underline{x}`            | Underline                           |
| `\overbrace{x}`            | Overbrace                           |
| `\underbrace{x}`           | Underbrace                          |
| `\overrightarrow{AB}`      | Arrow over                          |
| `\overleftarrow{AB}`       | Arrow over (left)                   |
| `\overleftrightarrow{AB}`  | Arrow over (both)                   |
| `\underrightarrow{AB}`     | Arrow under (right)                 |
| `\underleftarrow{AB}`      | Arrow under (left)                  |

---

## 10. ASCII Math Mode

Lambda supports ASCII Math, a more concise notation parsed by the same grammar with flavor `'ascii'`. ASCII Math uses plain-text tokens for common symbols:

```lambda
let ast = input('(a + b) / sqrt(c^2 + d^2)', 'math-ascii')
```

Key ASCII tokens:

| ASCII token | Equivalent        | ASCII token | Equivalent      |
|-------------|-------------------|-------------|-----------------|
| `alpha`     | `\alpha`          | `beta`      | `\beta`         |
| `sqrt(x)`   | `\sqrt{x}`        | `x/y`       | `\frac{x}{y}`   |
| `->`        | `\rightarrow`     | `<->`       | `\leftrightarrow` |
| `<=>`       | `\Leftrightarrow` | `=>`        | `\Rightarrow`   |
| `<=`        | `\leq`            | `>=`        | `\geq`          |
| `!=`        | `\neq`            | `~~`        | `\approx`       |
| `~=`        | `\simeq`          | `xx`        | `\times`        |
| `+-`        | `\pm`             | `-+`        | `\mp`           |
| `**`        | `\star`           | `-:`        | `\div`          |
| `\|->` (pipe-arrow) | `\mapsto` | `..`       | `\ldots`        |

Quoted text is supported in ASCII math: `"hello"` embeds text directly.

---

## 11. Missing and Unsupported Features

The following constructs are not currently supported. Unknown commands are passed through as-is (with an error CSS class) rather than causing a parse failure.

### 11.1 Unsupported Symbols and Commands

| Missing              | Notes                                                       |
|----------------------|-------------------------------------------------------------|
| `\not\in`, `\not=`   | Negation overlay (`\not` command) — not in grammar          |
| `\nleq`, `\ngeq`, `\nsim`, `\notin` (extended set) | Many negated relations not in symbol table |
| `\boldsymbol{x}`     | Bold symbol — use `\mathbf` for roman bold                  |
| `\pmb{x}`            | Poor man's bold — not supported                             |
| `\widecheck{x}`      | Wide check accent — not in accent table                     |
| `\iiiint`            | 4-fold integral (only `\int`, `\iint`, `\iiint` supported)  |
| `\oiiint`            | Triple contour integral                                     |
| `\LaTeX`, `\TeX`     | Logo commands — not in symbol table                         |
| `\S`, `\P`           | Section/paragraph symbols                                   |
| `\dag`, `\ddag`      | Already covered by `\dagger`/`\ddagger`                     |

### 11.2 Unsupported Structural Constructs

| Missing                    | Notes                                                       |
|----------------------------|-------------------------------------------------------------|
| `\xrightarrow[sub]{sup}`   | Extensible arrow with super/subscript — not in grammar      |
| `\xleftarrow[sub]{sup}`    | Extensible left arrow — not in grammar                      |
| `\stackrel{a}{b}`          | Treated as unknown command (use `\overset`)                 |
| `\substack{...}`           | Sub/superscript multi-line — not in grammar                 |
| `\sideset{l}{r}{\op}`      | Side limits on operators — not supported                    |
| `\underbrace{x}_{text}`    | Subscript on underbrace (the brace itself works, its label as literal subscript does not auto-render) |
| `\cancel`, `\bcancel`, `\xcancel` | Strike-through (grammar parses them, rendering not yet implemented) |
| `\tag{n}`                  | Equation tags/numbering — not supported                     |
| `\label{key}` / `\ref{key}`| Cross-referencing — not supported                          |
| `\intertext{...}`          | Text between aligned rows — not supported                   |
| `\vspace`, `\hfill`        | Vertical space / fill — not supported                       |
| `\raisebox`                | Raised box — not supported                                  |

### 11.3 Macro and Document-Level Features

| Missing                    | Notes                                                   |
|----------------------------|---------------------------------------------------------|
| `\newcommand`, `\renewcommand` | User-defined macros — not supported               |
| `\DeclareMathOperator`     | Custom operator names — use `\operatorname{name}` directly |
| `\def`, `\let`             | Low-level TeX macro definitions — not supported         |
| `\usepackage`              | LaTeX packages — not supported                          |
| Chemistry (`\ce{...}`)     | mhchem notation — out of scope                          |
| TikZ / PGF                 | Drawing commands — out of scope                         |

### 11.4 Output Limitations

| Missing               | Notes                                               |
|-----------------------|-----------------------------------------------------|
| MathML output         | Only HTML output is supported                       |
| DVI / PDF math output | HTML rendering pipeline only (C++ TeX pipeline can produce DVI) |
| `\text{...}` with nested math | Text re-entering math mode inside `\text` is not supported |
| Interactive editing   | Cursor, selection, virtual keyboard — out of scope  |

---

## 12. Quick Reference

```latex
% Fractions
\frac{a}{b}   \dfrac{a}{b}   \binom{n}{k}   a \over b

% Radicals
\sqrt{x}   \sqrt[3]{x}

% Scripts
x^2   x_i   x_i^n   \sum_{k=1}^{n}

% Delimiters
\left( \frac{a}{b} \right)   \bigl[ x \bigr]

% Matrix
\begin{pmatrix} a & b \\ c & d \end{pmatrix}

% Accents
\hat{x}   \vec{v}   \overline{AB}   \overbrace{1+2+3}^{n}

% Style
\mathbf{v}   \mathbb{R}   \mathcal{L}   \text{for all }

% Style switching
\displaystyle \sum_{k=1}^n   \scriptstyle x

% Spacing
a \, b   a \; b   a \quad b   \hspace{1em}

% Color
\textcolor{red}{E=mc^2}   \colorbox{yellow}{x}

% Enclosure
\boxed{E=mc^2}   \phantom{xx}

% Over/under
\overset{\text{def}}{=}   \underset{n \to \infty}{\lim}

% Operators
\sin x   \log_2 n   \operatorname{rank}(A)
```
