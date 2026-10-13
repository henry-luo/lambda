# LaTeX Phase V: package coverage implementation plan

**Design:** [Lambda_Pkg_Latex.md](../Lambda_Pkg_Latex.md) (§6 package model, §9 compatibility matrix, §10 verification rules, §11 outstanding work); [TeX engine design](../latex/Lambda_Design_TeX_Engine.md) §5 and §10; [Lambda_Pkg_Math.md](../Lambda_Pkg_Math.md) §3, §7 and §10.2; [Lambda_Pkg_Slides.md](../Lambda_Pkg_Slides.md) for beamer.
**Status:** Proposed 2026-10-12, not started. No formal ruling is changed here. Questions that would touch a ruling, or that are product choices, are collected in §17 and wait for the user.
**Formal linkage:** **D7.2.7** (expansion only, never typesetting; resolution order bundled → adapter → beside-document file → located `unknown-package`), **D7.2.1–D7.2.4** (script adapters in a static registry), **S1.8** (no document text becomes Lambda code), **S12.1.1v2** (rendering stays pure and deterministic), **D7.1.2v2** and **D7.5.2** (resources through the one IO door, path-gated), **S7.4.1–S7.4.4** (located diagnostics).
**Scope:** the commonly used LaTeX packages and classes that `lambda.latex` does not support, plus the kernel, `hyperref` and `graphicx` gaps those packages build on. Diagrams (`tikz-cd`, `circuitikz`, `forest`, `chemfig`, `xy`) belong to the TikZ design (§15).

## 1. Baseline

Probed on 2026-10-12 at `2665b0c81`. The probe documents are scratch files under `temp/latex_probe/` and are not committed.

**Probe through the package.** Parse with `latex.parse_file` or `latex.parse_source`. A bare `input(path, {type: "latex"})` expands without the adapter registry (Pkg_Latex §4.3), so `article` is never recorded as loaded and bundled packages fail spuriously: `\usepackage{xkeyval}` then reports "xkeyval loaded before \documentclass". That error is a probe artifact, not a defect.

**Unknown packages.** Each of these 79 packages produces `unknown-package`:

| Area | Packages |
|---|---|
| Math | `mathtools`, `bm`, `physics`, `cancel`, `amscd`, `bbm`, `dsfont`, `stmaryrd`, `upgreek`, `nicefrac`, `xfrac`, `mhchem` |
| Floats and layout | `float`, `subfig`, `wrapfig`, `placeins`, `rotating`, `pdflscape`, `lscape`, `adjustbox`, `threeparttable`, `pdfpages` |
| Spacing, headings, front matter | `setspace`, `parskip`, `titlesec`, `sectsty`, `tocloft`, `titling`, `authblk`, `appendix`, `footmisc`, `ragged2e`, `lineno` |
| Algorithms and code | `algorithm`, `algpseudocode`, `algorithm2e`, `minted`, `fancyvrb` |
| Boxes and text effects | `mdframed`, `framed`, `fancybox`, `soul`, `ulem` |
| Tables | `colortbl`, `hhline`, `dcolumn`, `csvsimple`, `pgfplotstable` |
| Fonts and engines | `fontspec`, `unicode-math`, `newtxtext`, `newtxmath`, `mathptmx`, `times`, `libertine`, `textcomp`, `gensymb`, `pifont`, `marvosym`, `fontawesome5` |
| Diagrams | `tikz-cd`, `circuitikz`, `forest`, `chemfig` |
| Back matter | `glossaries`, `acronym`, `nomencl`, `makeidx` |
| Cross-references | `bookmark`, `nameref`, `varioref`, `xr` |
| Utilities | `xspace`, `lipsum`, `xstring`, `datetime2`, `epstopdf`, `paralist`, `xparse` |

The 31-sample corpus (`test/latex/samples/`) declares 21 unknown packages. `wrapfig` and `amscd` appear in two samples each. `setspace`, `makeidx`, `cancel`, `fancybox`, `unicode-math`, `latexsym`, `indentfirst`, `amsxtra`, `mathabx`, `floatrow`, `minitoc`, `extsizes` and `boxedminipage` appear once.

**Text commands of unknown packages** become `latex-unsupported` placeholders: `\hl`, `\sout`, `\uline`, `\ce` and `\onehalfspacing`. The `algorithm` and `wrapfigure` environments are replaced as whole blocks.

**Math commands fall through silently.** The math typesetter paints an unmapped command's name as upright text, followed by its arguments, and reports nothing ([typeset.ls:209](../../lmd/package/math/typeset.ls) `command`, final branch; [typeset.ls:266](../../lmd/package/math/typeset.ls) `command_node`). Pkg_Math §3.1 and §10.2 already record this as outstanding.

- **Recognized:** `\cancel`, `\bcancel`, `\xcancel`, `\mathclap`, `\xrightarrow`, `\boldsymbol`, `\mathscr`, `dcases`, `pmatrix*`, `\iint`, `\oiint`, `\degree` and the CD environment.
- **Painted as names:** `\bm`, `\coloneqq`, `\abs`, `\norm`, `\dv`, `\pdv`, `\bra`, `\ket`, `\braket`, `\qty`, `\eval`, `\vb`, `\grad`, `\Tr`, `\mqty`, `\cancelto`, `\mathbbm`, `\mathds`, `\upalpha`, `\nicefrac`, `\sfrac`, `\ce`, `\llbracket` and `\shortrightarrow`.

**Kernel and supported-adapter gaps.** These are unsupported although no package is missing, and several packages below build on them:

| Area | Unsupported commands and environments |
|---|---|
| LaTeX kernel, text | `\textup`, `\textmd`, `\textnormal`, `\normalfont`, `\textsuperscript`, `\textsubscript`, `\ensuremath`, `\fontfamily`, `\usefont`, `\linespread`, `\mathversion` |
| LaTeX kernel, rules and fills | `\rule`, `\dotfill`, `\hrulefill`, `\hfil`, `\vfil`, `\vfill`, `\null`, `\stretch`, `\centerline`, `\indent` |
| LaTeX kernel, page | `\nopagebreak`, `\samepage`, `\enlargethispage`, `\raggedbottom`, `\flushbottom`, `\sloppy`, `\cleardoublepage`, `\pagenumbering`, `\onecolumn`, `\twocolumn`, `\markboth`, `\markright` |
| LaTeX kernel, structure | `\and`, `\thanks`, `\footnotemark`, `\footnotetext`, `\addcontentsline`, `\addtocontents`, `\listoffigures`, `\listoftables`, `\nocite`, `\tabularnewline`; environments `titlepage`, `list`, `trivlist`, `tabbing`, `eqnarray`, `alltt` |
| `hyperref` (supported) | `\hyperlink`, `\hypertarget`, `\phantomsection`, `\texorpdfstring` |
| `graphicx` (supported) | `\resizebox`, `\scalebox`, `\rotatebox`, `\reflectbox` |

**Classes.** Profiles exist for `article`, `report`, `book`, `amsart` and `standalone` ([registry.ls:88](../../lmd/package/latex/packages/registry.ls)). Any other class without a beside-document `.cls` (`scrartcl`, `memoir`, `IEEEtran`, `beamer`, …) renders as `article` with no diagnostic. D7.2.7 resolves a class name like a package name and ends in the located `unknown-package` diagnostic, so this is a conformance gap.

**Already in place.** Some missing pieces are smaller than they look:

- The textcomp symbols already render without the package, from `EXTENDED_SYMBOLS` in [symbols.ls:175](../../lmd/package/latex/symbols.ls).
- `\NewDocumentCommand` is native to the kernel, so `xparse` only needs its name answered (engine design §10).
- The parser already keeps `Verbatim`, `BVerbatim`, `minted` and `alltt` bodies raw (`lambda/input/input-latex-tables.cpp:90`).
- The math CD environment exists for `amscd`, although its dimensions are uncertified (Pkg_Math §7).
- `[H]` placement is harmless, because continuous output never moves floats.

## 2. Principles for this phase

- **The support definition stays strict** (Pkg_Latex §1). A name enters `CATALOG` only with a defined effect and a checked output path. Registering a name so that its warning disappears is never support.
- **Choose the mechanism by the package's kind** (Pkg_Latex §6.3):
  - Programming packages run on the engine.
  - Semantic packages get script adapters.
  - Math notation goes to `lambda.doc.math`.
  - Drawing goes to `lambda.doc.tikz`.
  - Layout capabilities go to Radiant.
- **No package policy in native code** (Pkg_Latex §2). Parser changes are limited to neutral source preservation.
- **Every bounded profile names its limits.** An unsupported option, key or command produces a located diagnostic (**S7.4.1–S7.4.4**).
- **Extract the shared shape before adding variants** (AGENTS rule 13). Several milestones start with a small extraction: the adapter hook tables, float kinds, the font-request table and the frame-box model.

## 3. Milestones

| # | Scope | Size | Depends on |
|---|---|---|---|
| M0 | Groundwork: adapter hook tables, float kinds and counters, the math fall-through diagnostic, the font-request table, the unknown-class diagnostic, `xparse` | L | — |
| M1 | Kernel, `hyperref` and `graphicx` completion | M | M0.1, M0.2 |
| M2 | Declarations and symbol tables | S | M0.1, M0.3, M0.4 |
| M3 | Text, paragraph and box packages | M | M0.1 |
| M4 | Floats and algorithms | L | M0.2 |
| M5 | Tables and page inserts | M | M0.1, M1 |
| M6 | Front matter, headings and contents | M | M0.2 |
| M7 | Glossaries, acronyms, nomenclature and index | M | M0.1 |
| M8 | Math packages | L | M0.3 |
| M9 | Fonts and Unicode engines | S–M | M0.4 |
| M10 | Classes | L | M6 |

Sizes are relative: S is about a day, M a few days, and L one to two weeks.

**Recommended order:** M0, M1, M2, M4, M8, M3, M5, M6, M7, M9, M10.

- M1 comes first because the kernel gaps hit nearly every real document.
- M2 is cheap and clears corpus diagnostics.
- Algorithms (M4) and math notation (M8) are the most frequent unknown packages in technical papers.

## 4. M0: groundwork

### M0.1 Adapter hook tables

[render.ls](../../lmd/package/latex/render.ls) carries 57 `registry.active(info.packages, …)` gates and [analyze.ls](../../lmd/package/latex/analyze.ls) carries 10. Most share one shape: `case 'x': if (registry.active(info.packages, "p")) handler(...) else render_generic_default(el, info)`. [registry.ls:119](../../lmd/package/latex/packages/registry.ls) `package_for_command` is a hand-written if-chain naming each package's commands. Sixty more packages in these chains would multiply both.

- **Handler tables.** Each adapter module exports `RENDER`, and `WALK` when it needs analysis. Each is a map from a command or environment name to a handler `fn(el, info, r)`. `r` is a renderer record that `render.ls` builds once (`children`, `node`, `children_skip_brack`), so adapters never import `render.ls` and the import graph stays acyclic. Lambda maps hold function values (checked: `{add: a}.add(2, 3)` returns 5), so no language work is needed.
- **Dispatch order.** `render_element` ([render.ls:91](../../lmd/package/latex/render.ls)) consults the active packages' tables, in reverse load order, after the citation handler and before the core arms. A later package therefore wins, as in LaTeX: `ulem` must be able to redefine `\emph`. `walk_node` ([analyze.ls:123](../../lmd/package/latex/analyze.ls)) does the same with `WALK`.
- **Command ownership is data.** `CATALOG` entries take their command lists from the module tables, so `package_for_command` becomes a lookup and unsupported-command attribution stays correct.
- **Conflicts are data.** A `conflicts` field on `CATALOG` entries replaces the hand-coded natbib/biblatex check in `registry.ls`. That pair keeps its existing code, `conflicting-bibliography-packages`, because diagnostic codes are stable. New pairs (`algorithm` with `algorithm2e`, `subfig` with `subcaption`) report `conflicting-packages`.
- **Migrate the existing gates in the same milestone.** The migration is golden-neutral: `make test-lambda-baseline` passes with no golden change.

### M0.2 Float kinds, counters and list-of

Analysis hard-codes the `figure`, `table` and listing counters and caption names ([analyze.ls:341](../../lmd/package/latex/analyze.ls) `walk_figure`, [analyze.ls:356](../../lmd/package/latex/analyze.ls) `walk_table`), and nothing renders a list of figures. These packages all need the same three things: `float` (`\newfloat`), `algorithm`, `algorithm2e`, minted's `listing`, `subfig`, `wrapfig`, `rotating` and tocloft's `\newlistof`.

- **One float-kind table.** For example `{figure: {counter: "figure", name: "Figure", list: "lof"}, table: …, lstlisting: …}`. Adapters extend it, and so does `\newfloat` during analysis. Captions take their name and number from the table. Localized names come from the existing language strings.
- **List-of rendering.** `\listoffigures`, `\listoftables` and `\listof…` render from analysis entries the way [render_toc](../../lmd/package/latex/render.ls) renders headings.
- **General counters.** Counters become a general map, so `\newcounter`, `\stepcounter`, `\addtocounter` and `\setcounter` work. This closes the counter item of Pkg_Latex §11.2, and float kinds need dynamic counters anyway.
- **Float label context.** The generic float walk replaces `walk_figure` and `walk_table`, so fix the §11.2 defect "label context leaking out of floats" there.
- **Appendix lettering.** Fix §11.2 "appendices in book and report" in the same counter code. M3's `appendix` package depends on it.

### M0.3 Math fall-through diagnostic

The math typesetter reports a command it cannot map as a structured diagnostic instead of only painting its name. `math_bridge.ls` surfaces it as `unsupported-math-command`, located at the formula's offset. The painted fallback may stay for previews (Pkg_Math §3.2).

Without this, no math package can meet the support definition, because a silent fallback looks exactly like success. This is Pkg_Math §10.2 work, owned by `lambda.doc.math`. Expect golden changes where existing tests already contain unmapped commands. Review each one as a real gap or a missing mapping, and never resolve it by suppressing the diagnostic.

### M0.4 Font-request table

[latex.ls:124](../../lmd/package/latex/latex.ls) builds five near-identical `font-substitution` blocks, for `cmbright`, `eucal`, `mathrsfs`, `gfsporson` and `helvet`. Move them to a `font` field on `CATALOG` entries (`{face, css_family, message}`), so the font packages of M9 and the NFSS family codes of M1 are data rows.

### M0.5 Unknown classes

Report the located `unknown-package` diagnostic for a class name that has neither a profile nor a beside-document `.cls`, as D7.2.7 already rules. The document still renders with the `article` profile.

### M0.6 `xparse` from the kernel

Answer `\usepackage{xparse}` from the kernel's package bookkeeping (`\ver@xparse.sty`), as current LaTeX does. This closes the engine design §10 item. It is neutral engine bookkeeping, not package policy.

## 5. M1: kernel, hyperref and graphicx completion

| Group | Profile | Mapping |
|---|---|---|
| Text | `\textup`, `\textmd`, `\textnormal`, `\normalfont`, `\textsuperscript`, `\textsubscript`, `\ensuremath` | `font_decl` spans; `<sup>`/`<sub>`; `\ensuremath` goes through the math bridge |
| NFSS | `\fontfamily`, `\fontseries`, `\fontshape`, `\usefont`, `\linespread`, `\mathversion` | Family codes (`cmr`, `cmss`, `cmtt`, `ptm`, `phv`, `ppl`, …) resolve through the M0.4 table; `\linespread` sets line height; `\mathversion{bold}` behaves like `\boldmath`; unknown codes are diagnosed |
| Rules and fills | `\rule`, `\dotfill`, `\hrulefill`, `\hfil`, `\vfil`, `\vfill`, `\null`, `\stretch`, `\centerline`, `\indent` | `boxes.ls` and `spacing.ls`; fills reuse the `latex-hfill` flex spacer |
| Page | `\nopagebreak`, `\samepage`, `\enlargethispage`, `\raggedbottom`, `\flushbottom`, `\sloppy`, `\fussy`, `\cleardoublepage`, `\pagenumbering`, `\markboth`, `\markright`, `\onecolumn`, `\twocolumn` | Hints with no continuous effect render nothing; `\cleardoublepage` acts like `\clearpage`; numbering and marks feed `page_style.ls` on the paged path and are diagnosed in continuous output; `twocolumn` maps to CSS columns, and spanning floats are diagnosed |
| Front matter | `\and`, `\thanks`, `titlepage` | Author separators and title footnote marks in the title block |
| Footnotes | `\footnotemark[n]`, `\footnotetext[n]` | Share the analysis footnote numbering |
| Contents | `\addcontentsline`, `\addtocontents`, `\listoffigures`, `\listoftables` | M0.2 lists; `\addcontentsline` adds an entry anchored at the call |
| Environments | `list`, `trivlist`, `tabbing`, `eqnarray`, `eqnarray*`, `alltt` | `list` goes to the list model with its label argument; `tabbing` becomes a grid built from `\=`, `\>` and `\\`; `eqnarray` uses the amsmath alignment model with `rcl` columns and its numbering; `alltt` keeps commands inside a verbatim body, which needs a neutral parser change because its body is raw today |
| Bibliography | `\nocite` | Analysis records the keys for every bibliography path |
| `hyperref` | `\hyperlink`, `\hypertarget`, `\phantomsection`, `\texorpdfstring` | Anchors and links; `\phantomsection` anchors the next `\addcontentsline`; `\texorpdfstring` renders its first argument and feeds its second to outline titles |
| `graphicx` | `\resizebox`, `\scalebox`, `\rotatebox`, `\reflectbox` | CSS sizing and transforms sharing `graphicx.ls`; `!` keeps the aspect ratio; layout-box effects of rotation are diagnosed as approximations |

The §9 `hyperref` row claims "anchors", but `\hypertarget` is unsupported. Correct the row when M1 lands.

## 6. M2: declarations and symbol tables

Each item below is a `CATALOG` row with a defined effect. Most also need a small table entry.

- **`textcomp`**: declaration only, because its symbols already render.
- **`gensymb`**: `\degree`, `\celsius`, `\ohm`, `\micro` and `\perthousand` in text and math, as aliases of the existing textcomp symbols.
- **`xparse`**: done by M0.6.
- **`nameref`**: provided by `hyperref`; a declaration.
- **`placeins`**: `\FloatBarrier` renders nothing, because continuous output never moves floats.
- **`indentfirst`**: CSS for the first paragraph after a heading.
- **`epstopdf`**: a declaration. EPS sources stay diagnosed (Pkg_Latex §11.4).
- **`bookmark`**: `\bookmark` through the existing `hyperref.explicit_bookmark`, plus a bounded option set.
- **`amscd`**: the existing math CD environment; Partial, because its dimensions are uncertified.
- **`cancel`**: `\cancel`, `\bcancel` and `\xcancel` already render; Partial until M8 adds `\cancelto`.
- **`latexsym`, `stmaryrd`, `esint`, `amsxtra`, `upgreek`, `bbm`, `dsfont`**: math symbol and alphabet tables in `lambda.doc.math`, after M0.3. `bbm` and `dsfont` need double-struck digits (U+1D7D8–U+1D7E1), which the bundled KaTeX fonts lack. An unresolvable glyph must stay visible (Pkg_Math §3.2).
- **`pifont`**: `\ding{n}` maps ZapfDingbats codes to the Unicode Dingbats block. `dinglist` and `dingautolist` go to the list model.
- **`marvosym`**: a bounded Unicode table (`\Email`, `\Letter`, `\Mobilefone`, `\EUR`, …). Other symbols are diagnosed.

## 7. M3: text, paragraph and box packages

| Package | Profile | Mapping and limits |
|---|---|---|
| `soul` | `\hl`, `\ul`, `\st`, `\so`, `\caps`, `\sethlcolor`, `\setulcolor`, `\setstcolor` | `<mark>` or background, text decoration, letter spacing, small caps; colors through `color.ls` |
| `ulem` | `\uline`, `\uuline`, `\uwave`, `\sout`, `\xout`, `\dashuline`, `\dotuline`, `\normalem`, `\ULforem` | CSS decoration styles. Without `[normalem]`, ulem redefines `\emph` as underline, so honor that (M0.1 lets the package override the core arm). `\xout` has no CSS equivalent and is diagnosed as an approximation |
| `setspace` | `\singlespacing`, `\onehalfspacing`, `\doublespacing`, `\setstretch`; environments `singlespace`, `onehalfspace`, `doublespace`, `spacing`; package options | Line height = the class baselineskip ratio × stretch, using setspace's per-size table (`\onehalfspacing` is 1.25, 1.213 or 1.241 at 10, 11 or 12pt; `\doublespacing` is 1.667, 1.618 or 1.655) |
| `parskip` | Options `skip`, `indent`, `parfill` | Paragraph indent 0 and paragraph spacing of half a baselineskip, as CSS on the root |
| `ragged2e` | `\RaggedRight`, `\RaggedLeft`, `\Centering`, `\justifying`; environments `FlushLeft`, `FlushRight`, `Center`, `justify` | The existing alignment declarations, with `hyphens: auto` (ragged2e permits hyphenation) |
| `xspace` | `\xspace` | A space unless the next sibling starts with punctuation or a closing character (xspace's exception list), or the engine route of §17.1 |
| `lipsum`, `blindtext` | `\lipsum[a-b]`, `\blindtext`, `\Blindtext` | Bundled filler text as data, or `lipsum.sty` on the engine (§17.1) |
| `paralist` | `compactitem`, `compactenum`, `compactdesc`, `asparaitem`, `asparaenum`, `inparaitem`, `inparaenum`, the enumerate label argument | The `enumitem` list model (compact means `nosep`; inline and run-in forms) |
| `fancyvrb` | `Verbatim`, `BVerbatim`, `LVerbatim`; keys `numbers`, `frame`, `fontsize`, `firstnumber`; `\fvset` | The listings code-block renderer. `commandchars` and `\VerbatimInput` are diagnosed |
| `minted` | `minted{lang}`, `\mint`, `\mintinline`, `\setminted`, the `listing` float | Code blocks with a language class, unhighlighted like `listings`. The float comes from M0.2. `\inputminted` and highlighting styles are diagnosed |
| `framed`, `mdframed` | `framed`, `shaded`, `snugshade`, `leftbar`; `mdframed` with bounded keys, `\newmdenv`, `\mdfdefinestyle` | First extract a frame-box model from `tcolorbox.ls`, which becomes the third user of the shape; other keys are diagnosed |
| `fancybox`, `boxedminipage` | `\shadowbox`, `\doublebox`, `\ovalbox`, `\Ovalbox`; the `boxedminipage` environment | CSS boxes in `elements/boxes.ls` |
| `footmisc` | Options `symbol`, `bottom`; `\footref` | Symbol marks (`*`, `†`, `‡`, …); `\footref` repeats a mark. `perpage` and `para` need pages and are diagnosed |
| `appendix` | `appendices`, `subappendices`, `\appendixpage`, `\addappheadtotoc` | Appendix semantics from M0.2's corrected appendix counters |

## 8. M4: floats and algorithms

- **`float`.** Supports `\newfloat{type}{placement}{ext}[within]`, `\floatname`, `\floatstyle` (`plain`, `plaintop`, `ruled`, `boxed`), `\restylefloat`, `\listof{type}{title}` and `[H]`, all on M0.2 float kinds.
- **`algorithm` with `algorithmic` or `algpseudocode`.**
  - The `algorithm` float, numbered "Algorithm N", with `\listofalgorithms`.
  - The algpseudocode profile: `\State`, `\Statex`, `\If`/`\ElsIf`/`\Else`/`\EndIf`, `\For`/`\ForAll`/`\EndFor`, `\While`/`\EndWhile`, `\Repeat`/`\Until`, `\Loop`/`\EndLoop`, `\Procedure`/`\EndProcedure`, `\Function`/`\EndFunction`, `\Require`, `\Ensure`, `\Return`, `\Call` and `\Comment`.
  - The older `algorithmic` package's upper-case commands (`\STATE`, `\IF`…`\ENDIF`, `\REQUIRE`, …) are a second keyword table over the same model.
  - The adapter folds the flat command sequence into nested blocks using open and close keyword tables.
  - `algorithmic[n]` numbers lines, and `\label` on a numbered line resolves to its line number.
  - `\algnewcommand`, `\algrenewcommand` and `\algdef` are diagnosed.
- **`algorithm2e`.**
  - Its own `algorithm` float.
  - Commands: `\KwIn`, `\KwOut`, `\KwData`, `\KwResult`, `\KwRet`, `\KwTo`; `\If`, `\uIf`, `\eIf`, `\ElseIf`, `\Else`; `\For`, `\ForEach`, `\ForAll`, `\While`, `\Repeat`; `\SetKwFunction`, `\SetKwProg`, `\SetKwInOut`, `\SetKw`; `\tcp` and `\tcc` comments; `\;` line ends; `\SetAlgoLined`, `\SetAlgoNoLine`, `\LinesNumbered` and `\DontPrintSemicolon`.
  - Block bodies are brace arguments, so this adapter folds by argument structure into the same block model.
  - Loading it together with `algorithm` reports `conflicting-packages` (M0.1), as LaTeX fails.
- **Algorithm output.** The float is `<figure class="latex-algorithm">`. Its body is `div.latex-algorithmic` with explicit line numbers, because nesting breaks CSS list counters. Keywords are `<strong>`, and math goes through the bridge. Comments are right-aligned, with the `▷` mark in algpseudocode.
- **`subfig` and `subfigure`.** `\subfloat[cap]{body}`, `\subfigure[cap]{body}` and `\subref`, on the existing subcaption subfloat model. Loading one with `subcaption` reports `conflicting-packages`.
- **`wrapfig`.** `wrapfigure` and `wraptable` with placement, `[lines]`, `[overhang]` and width. They become a floated figure (CSS float and width) with figure or table numbering. `i`/`o` placements map to right/left. The `[lines]` argument is a TeX shape hint that CSS floating supersedes.
- **`rotating`.** `sidewaysfigure` and `sidewaystable` are float kinds with rotated content. `turn` and `rotate` share M1's transforms. Layout effects are diagnosed as approximations.
- **`pdflscape` and `lscape`.** `landscape` renders its content. Rotating a paged PDF page needs a per-page orientation host capability and is diagnosed.

## 9. M5: tables and page inserts

- **`threeparttable`.** The `threeparttable` and `tablenotes` environments and `\tnote`, on the shared table model (Pkg_Latex §8.4).
- **`colortbl`.** `\columncolor`, `\cellcolor`, `\arrayrulecolor` and `\doublerulesepcolor`; `\rowcolor` already works through `xcolor[table]`.
- **`hhline`.** `\hhline{…}` becomes per-column border segments on the following row.
- **`dcolumn`.** `D{sep}{sep}{places}` columns reuse the siunitx `S` decimal grid.
- **`adjustbox`.** `\adjustbox{keys}{…}` and the environment. Keys `width`, `height`, `totalheight`, `max width`, `max height`, `scale`, `angle`, `trim`, `clip`, `frame`, `bgcolor`, `center` and `valign` reuse the `graphicx.ls` sizing and M1 transforms. Other keys are diagnosed.
- **`pdfpages`.** `\includepdf[pages=…, scale, angle]` embeds each page through the existing graphicx PDF path as a page-sized block. `nup`, `pagecommand` and `landscape` are diagnosed.
- **`csvsimple`.**
  - `\csvautotabular`, `\csvautobooktabular` and `\csvreader`.
  - The CSV file is acquired at the input boundary like a `.bib` file (**D7.1.2v2**, **D7.5.2**) and read with Lambda's CSV input.
  - `\csvreader` row templates need per-row substitution. Running `csvsimple-l3` on the engine, which already has expl3 and path-gated reads, is the cleaner route and falls under §17.1. Otherwise the script supports `\csvautotabular` only.

## 10. M6: front matter, headings and contents

- **`titlesec`.**
  - `\titleformat` with the `hang`, `block`, `display` and `runin` shapes. The format is read through `font_decl`, and the label is a template over the number.
  - `\titlespacing*` sets left indent and spacing before and after; `\titlelabel` is supported.
  - Options: `compact`, `small`, `medium`, `big`, `raggedright`, `center`, and the font options.
  - Arbitrary TeX in a format is diagnosed.
- **`sectsty`.** `\allsectionsfont`, `\partfont`, `\chapterfont`, `\sectionfont`, `\subsectionfont`, `\subsubsectionfont` and `\paragraphfont` become heading CSS.
- **`titling`.** `\pretitle`/`\posttitle`, `\preauthor`/`\postauthor`, `\predate`/`\postdate`, `\thetitle`, `\theauthor`, `\thedate`, `\droptitle` and the `\maketitlehook` slots.
- **`authblk`.** `\author[n]`, `\affil[n]`, `\Authsep`, `\Authand`, `\Authands`, `\Authfont` and `\Affilfont`, plus the `auth-*` and `affil-*` options. They render as a title block with marks and an affiliation list.
- **`tocloft`.** The font and leader families (`\cftsecfont`, `\cftsecleader`, …), `\cftsetindents`, the `\cftbefore…skip` lengths, and `\newlistof` through M0.2. The continuous ToC has no page numbers, so page-number styling is accepted without effect. Other commands are diagnosed.
- **`minitoc`.** `\dominitoc`, `\minitoc` and `\mtcaddchapter` render per-chapter contents from the analysis headings.
- **`varioref`.** `\vref`, `\Vref`, `\vpageref` and `\vrefrange` render the reference plus a page phrase. Continuous output diagnoses the page phrase, as it does for `\pageref`.

## 11. M7: glossaries, acronyms, nomenclature and index

**One term-registry model.** Analysis collects every definition and computes first use per label in source order. This needs no mutation and fits the existing two-pass design (**S12.1.1v2**). Definitions come from `\newglossaryentry`, `\newacronym`, `\acro`, `\nomenclature` and `\index`.

- **`glossaries`.**
  - Commands: `\gls`, `\Gls`, `\glspl`, `\Glspl`, `\glstext`, `\glsdesc`, `\acrshort`, `\acrlong`, `\acrfull`, `\glsreset`, `\makeglossaries`, `\printglossary[type, title]` and `\printglossaries`.
  - `\loadglsentries{file}` is read by the engine's path-gated `\input`.
  - `glossaries-extra` is out of scope.
- **`acronym`.**
  - The `acronym` environment with `\acro{label}[short]{long}`.
  - Commands: `\ac`, `\acf`, `\acs`, `\acl`, `\acp`, `\acfp`, `\acsp`, `\aclp`, `\acused` and `\acresetall`.
  - Options: `printonlyused` is supported; `withpage` needs pages and is diagnosed.
- **`nomencl`.** `\makenomenclature`, `\nomenclature[prefix]{symbol}{description}`, `\printnomenclature[width]` and `\nomname`. Entries are sorted by prefix and symbol into a description list.
- **`makeidx` and `imakeidx`.**
  - `\makeindex`, `\printindex` and `\indexname`.
  - `\index` with `key@display`, `!` subentries, `|see`, `|seealso`, `|(`…`|)` ranges and page formats.
  - Locators are links back to the `\index` anchors rather than page numbers, the convention the biblatex backrefs already use (Pkg_Latex §9).
  - Sorting is by case-folded code points, a stated limit: locale collation is the host capability of Pkg_Latex §11.3.

## 12. M8: math packages

This is `lambda.doc.math` work, after M0.3. Every addition updates Pkg_Math §7.

- **Tables.**
  - `bm`: `\bm` maps to `\boldsymbol`.
  - Symbol and alphabet tables from M2.
  - `mathabx` as a subset: it reshapes many standard symbols, and those shapes are not reproduced, which is diagnosed.
- **`mathtools`.**
  - Colon relations: `\coloneqq`, `\Coloneqq`, `\eqqcolon`, `\vcentcolon` and the rest.
  - Other commands: `\mathllap`, `\mathrlap`, `\prescript`, extensible arrows (`\xmapsto`, `\xhookrightarrow`, …), `\smashoperator`, `\adjustlimits`, `\cramped`, `\splitfrac`, `\splitdfrac` and `\shortintertext`.
  - `\DeclarePairedDelimiter` and `\DeclarePairedDelimiterX`, with the star and the size argument.
  - `\newtagform`, `\usetagform` and `\renewtagform`, owned by the LaTeX package's equation numbering.
  - `\mathtoolsset` is diagnosed.
- **`physics`.**
  - Brackets: `\abs`, `\norm`, `\eval`, `\order`, `\qty`, `\pqty`, `\bqty`, `\Bqty`, `\vqty`.
  - Derivatives: `\dv`, `\pdv`, `\fdv`, with order and inline forms.
  - Dirac notation: `\bra`, `\ket`, `\braket`, `\ketbra`, `\expval`, `\mel`.
  - Vectors: `\vb`, `\va`, `\vu`, `\vdot`, `\cross`, `\grad`, `\div`, `\curl`, `\laplacian`.
  - Operators: `\Tr`, `\tr`, `\rank`, `\erf`, `\Res`, and the rest.
  - Matrices: `\mqty`, `\pmqty`, `\bmqty`, `\vmqty`, `\imat`, `\xmat`, `\dmat`.
- **Route for macro layers.** `\DeclarePairedDelimiter` and all of `physics` expand into constructs the math parser already handles: `\left…\right`, `\frac`, `\mathrm`, `\boldsymbol` and the matrix environments. Math is expanded by the engine (engine design §2.1), so there are two routes:
  - **(a) Define them on the engine.** Either upstream `physics.sty` (LPPL, written with `\DeclareDocumentCommand`, which the kernel provides natively) or a small Lambda-owned binding `.sty`. This keeps user `\renewcommand` working and gives each command one definition. It needs §17.1.
  - **(b) Map them in the math package's tables.** This is the fallback.
- **`cancel`**: `\cancelto{value}{expr}`, an arrow box transform.
- **`nicefrac` and `xfrac`**: `\nicefrac` and `\sfrac` become a new slanted-fraction construct. Text-mode uses go through the math bridge.
- **`mhchem`**: §17.2.
- **`unicode-math`**: M9.

## 13. M9: fonts and Unicode engines

- **Text font packages as M0.4 rows.** `times`, `mathptmx`, `newtxtext`, `newtxmath`, `txfonts`, `newpxtext`, `newpxmath`, `mathpazo`, `palatino`, `libertine`, `libertinus`, `charter`, `kpfonts`, `fourier`, `sourcesanspro`, `inconsolata`, `beramono` and `courier`. Each becomes a CSS family stack. A face Lambda does not bundle reports `font-substitution` (Pkg_Latex §7.2). Bundling is §17.3.
- **`fontspec`.**
  - `\setmainfont`, `\setsansfont`, `\setmonofont`, `\newfontfamily`, `\fontspec` and `\addfontfeatures` become CSS family requests.
  - The transform is pure and cannot see installed fonts, so a family Lambda does not bundle always reports `font-substitution`.
  - The `Ligatures`, `Numbers` and `Scale` features map to CSS. Other features are diagnosed.
  - Fold the current silent `\newfontfamily` handling (polyglossia) into this.
- **`unicode-math`.**
  - `\setmathfont` maps to the math package's explicit `font_family` option (Pkg_Math §3.2), with substitution when the face is not supplied.
  - `\symbf`, `\symit`, `\symcal`, `\symbb`, `\symsf` and `\symtt` map to the existing alphabets.
  - Unicode math input passes through.
- **Symbol fonts.** `fontawesome5` depends on §17.3. `marvosym` was covered in M2.

## 14. M10: classes

- **Class-profile model.** `CLASSES` entries gain a `base` (article, report or book numbering), an option schema, and a command table for class front matter in the M0.1 shape.
- **Tier A: aliases with small command sets.**
  - `extarticle`, `extreport` and `extbook`, with 8–20pt size options.
  - `scrartcl`, `scrreprt` and `scrbook`. KOMA commands: `\subtitle`, `\subject`, `\publishers`, `\dedication`, `\titlehead`, `\extratitle`, `\uppertitleback`, `\lowertitleback`, `\addchap`, `\addsec`, `\minisec`, and `\captionabove` and `\captionbelow`. `\KOMAoptions`, `\setkomafont` and `\addtokomafont` are a bounded subset.
  - `amsbook` and `amsproc`.
  - `memoir`, on the book base: named `\chapterstyle` subset, `\epigraph`, built-in `\newfloat` and `\listof` through M0.2. Its layout commands are diagnosed.
- **Tier B: journal classes.**
  - `IEEEtran`: `\IEEEauthorblockN`, `\IEEEauthorblockA`, `\IEEEPARstart`, the `IEEEkeywords` environment, conference and journal options. `IEEEtrantools` is already supported.
  - `llncs`: `\institute`, `\inst`, `\email`, `\orcidID`, `\titlerunning`, `\authorrunning`, `\keywords`.
  - `elsarticle`: the `frontmatter` environment, `\address` and `\affiliation`, `\ead`, `\cortext`, `\fntext`, the `keyword` environment with `\sep`, `\journal`.
  - `revtex4-2`: `\affiliation`, `\email`, `\keywords`, `superscriptaddress`.
  - `acmart`, the largest: `\affiliation{\institution…}`, `\ccsdesc`, `CCSXML`, `\keywords`, the teaser figure. Rights and metadata commands are diagnosed.
- **Tier C: own document models.**
  - `letter`: the `letter` environment, `\address`, `\signature`, `\opening`, `\closing`, `\ps`, `\encl`, `\cc`.
  - `exam`: `questions`, `parts`, `subparts`, `choices`, `checkboxes`; `\question[points]`, `\choice`, `\CorrectChoice`; `solution` with `\printanswers`.
  - `moderncv` is low priority.
- **Beside-document classes.** Exposing kernel internals as constructors (`\@startsection`, `\@maketitle`, `\@float`, `\@makecaption`, …) is the general lever (engine design §10). It is engine work; its timing is §17.4.
- **beamer.** §17.5.

## 15. Out of this plan

- **Diagrams** belong to [Lambda_Pkg_Tikz.md](../Lambda_Pkg_Tikz.md): `tikz-cd`, `circuitikz`, `forest`, `chemfig`, `pgfplotstable`, `xy`/`xymatrix` and `tkz-tab`. Simple `tikz-cd` grids could lower onto the math CD model.
- **`lineno`.** Line numbers depend on Radiant's line breaking and need a layout-time line-counter host capability (Pkg_Latex §11.3 class).
- **`xr` and `xr-hyper`.** Cross-document labels need analysis of a second document.
- **Low frequency:** `floatrow`, `datetime2`, `glossaries-extra`, `maybemath`, `answers`, `arcs`. Add each when a corpus document needs it.
- **`luacode`**: never (**D7.2.7**, **S1.8**).

## 16. Verification and acceptance

- **Tests per milestone.** Add a `test/lambda/latex/test_latex_pkg_<group>.ls` script with its `.txt` golden and fixtures under `test/lambda/latex/fixtures/` (AGENTS rule 8).
- **Fixtures per package.** Each package gets a passing fixture and a failing fixture, in which an unsupported option or command yields its located diagnostic.
- **Targets.** HTML is checked always. SVG and PDF are checked wherever the §9 row claims them, following the target-profile pattern of `test_latex_packages_diagnostics.ls` and `test/latex/samples/audit.py`.
- **Baseline.** `make test-lambda-baseline` passes at every milestone, and M0.1 changes no golden.
- **Documents.**
  - Update the Pkg_Latex §9 rows and the "Declarations with defined effects" row only from checked results (Pkg_Latex §10).
  - Update Pkg_Math §7 for math additions.
  - Update `test/latex/samples/COMPATIBILITY.md` when a sample's status changes. The `wrapfig`, `amscd`, `setspace`, `makeidx`, `cancel` and `fancybox` samples should improve.
- **Visible text against TeX.** Use pinned TeX Live 2025 runs, as under `test/latex/reference/phase3/`. Ask before installing any TeX package that is not already present locally.

## 17. Decisions for the user

1. **Bundled packages that emit document tokens.** D7.2.7 bundles *programming* packages. Should `.sty` files whose expansion produces document material also run on the engine as bundled packages? Candidates are upstream `physics.sty`, a Lambda-owned `mathtools` binding, `xspace`, `lipsum` and `csvsimple-l3`. Pkg_Latex §6.3 calls these "thin bindings" and has not applied one yet.
   - *Recommendation:* allow pure macro layers, meaning expansion only, with no font, box or page primitives. User `\renewcommand` keeps working, and no macro language is reimplemented in script.
   - This may need D7.2.7's wording revised. That ruling is yours to make.
2. **`mhchem`.** Should `\ce` and `\pu` get a bounded script parser (formulas, charges, states, arrows), or stay unsupported? Pkg_Math §7 and §10.3 currently exclude general chemistry.
   - *Recommendation:* a bounded profile owned by `lambda.doc.math`.
3. **Fonts.** Should Lambda bundle OFL faces (TeX Gyre Termes, Pagella and Heros; Libertinus; Font Awesome Free), or only report substitution?
   - *Recommendation:* substitution only for now; revisit against the release size budget.
4. **Classes.** Should tier A and B profiles come first, or the kernel-internals constructor route?
   - *Recommendation:* profiles first, because they cover the common templates sooner.
5. **beamer.** Should beamer get a sub-design, `vibe/latex/Lambda_Design_Beamer.md`, lowering frames onto a `lambda.slide` deck (Pkg_Slides SLD1–SLD4 and §7 cues)? Frames become slides, overlay specifications become cues, and blocks, columns and themes map with diagnostics. The alternative is to keep it out of scope.
   - *Recommendation:* the sub-design. Until then, beamer reports `unknown-package` (M0.5).
6. **Priority evidence.** The ordering here is judgment plus the 31-sample corpus. Measuring `\usepackage` frequency on a public corpus, such as arXiv sources or Overleaf gallery templates, would require downloads. Ask for it if wanted.

## Progress log

- 2026-10-12: plan written from the §1 probes at `2665b0c81`; not started.
