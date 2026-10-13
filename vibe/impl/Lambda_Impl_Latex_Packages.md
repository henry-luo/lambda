# LaTeX Phase V: package coverage implementation plan

**Design:** [Lambda_Pkg_Latex.md](../Lambda_Pkg_Latex.md) (§6 package model, §9 compatibility matrix, §10 verification rules, §11 outstanding work); [TeX engine design](../latex/Lambda_Design_TeX_Engine.md) §5 and §10; [Lambda_Pkg_Math.md](../Lambda_Pkg_Math.md) §3, §7 and §10.2; [Lambda_Pkg_Slides.md](../Lambda_Pkg_Slides.md) for beamer.
**Status:** In progress. Proposed 2026-10-12; the user approved the §17 recommendations on 2026-10-13. M0, M1, M2, M4, M8, M3, M5, M6, M7 and M9 were implemented on 2026-10-13 in worktree `latex-phase5-m0`. §17.1 was ratified as **D7.2.7v2** when the first binding (physics) landed.
**Formal linkage:** **D7.2.7v2** (expansion only, never typesetting; bundled packages are upstream programming packages or Lambda-owned bindings; resolution order bundled → adapter → beside-document file → located `unknown-package`), **D7.2.1–D7.2.4** (script adapters in a static registry), **S1.8** (no document text becomes Lambda code), **S12.1.1v2** (rendering stays pure and deterministic), **D7.1.2v2** and **D7.5.2** (resources through the one IO door, path-gated), **S7.4.1–S7.4.4** (located diagnostics).
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

As built on 2026-10-13:

- **Command ownership is data.** Each `CATALOG` entry lists the commands and environments it owns in a `commands` field. `registry.owner(packages, command)` returns the owning package among the active ones, and the later-loaded package wins, as in LaTeX. `package_for_command` became a lookup over the same data, so dispatch and unsupported-command attribution share one source.
- **Handler tables.** `render.ls` holds `PACKAGE_RENDER` and `analyze.ls` holds `PACKAGE_WALK`. Each maps a package to a map from command name to a handler `fn(el, info)` or `fn(el, st)`. The tables sit beside the existing helpers, which already live in those files; an adapter module with its own handlers contributes its functions to the table. Lambda maps hold function values, so no language work was needed.
- **Dispatch order.** `render_element` and `walk_element` call `registry.handler(table, packages, tag)` after the citation handler and before the core arms, so a package can redefine a core command (`ulem`'s `\emph`). An owned command without a handler falls through to the core arms. That keeps the cases where the core arm still decides, such as `lstlisting` as plain verbatim without `listings`, or the CSL-or-biblatex arms.
- **What migrated.** The 25 dispatch-shaped arms moved into the tables. The other gates are feature checks inside core handlers, for example semantic inference inside `display_math`, and they stay. A test checks that every table handler is owned by its package in the catalogue.
- **Conflicts are data.** A `conflicts` field on `CATALOG` entries replaces the hand-coded natbib/biblatex check. That pair keeps its code, `conflicting-bibliography-packages`, because diagnostic codes are stable. New pairs report `conflicting-packages`.
- **Golden-neutral.** All 61 LaTeX scripts matched their goldens after the migration.

### M0.2 Float kinds, counters and list-of

Analysis hard-codes the `figure`, `table` and listing counters and caption names ([analyze.ls:341](../../lmd/package/latex/analyze.ls) `walk_figure`, [analyze.ls:356](../../lmd/package/latex/analyze.ls) `walk_table`), and nothing renders a list of figures. These packages all need the same three things: `float` (`\newfloat`), `algorithm`, `algorithm2e`, minted's `listing`, `subfig`, `wrapfig`, `rotating` and tocloft's `\newlistof`.

As built on 2026-10-13:

- **One float-kind table.** [floats.ls](../../lmd/package/latex/floats.ls) holds `KINDS`, with `figure` and `table`. Each kind names its counter, caption label, list command and list title. Analysis, caption labels and lists all read it. M4 extends it for `float`'s `\newfloat` and the algorithm floats. Listings keep their own numbering. No localized strings exist yet, so the names are English, like the ToC's "Contents".
- **One float walk.** `walk_float` replaces `walk_figure` and `walk_table`. Each float records its kind, number, caption list text (the short form when given; starred captions are not listed) and the id of its own label. Analysis outputs one `floats` list instead of `figures` and `tables`, and render looks numbers up with `get_float_num`.
- **List-of rendering.** `\listoffigures` and `\listoftables` render like the ToC. An entry links to the float's label when there is one, and is plain text otherwise. Adding ids to every float would change existing output.
- **General counters.** Built-in and declared counters share one map, and `step_counter` follows LaTeX's reset chain plus the resets that `\newcounter{c}[p]` declares. `\newcounter`, `\setcounter`, `\addtocounter`, `\stepcounter` and `\refstepcounter` now work; the last one makes the counter the target of a following `\label`. The separate custom-counter list and its per-counter match arms are gone. This closes the counter item of Pkg_Latex §11.2.
- **Label scope.** Floats, theorems (built-in and `\newtheorem`), legacy `equation` and listings restore the enclosing label context when they end, because an environment's `\refstepcounter` is local to it. This closes the §11.2 "label context leaking out of floats" item.
- **Appendix lettering.** `\appendix` restarts the chapter counter in book and report, and the section counter otherwise. This closes the §11.2 appendix item.

### M0.3 Math fall-through diagnostic

Without this, no math package can meet the support definition, because a silent fallback looks exactly like success. This is Pkg_Math §10.2 work, owned by `lambda.doc.math`.

As built on 2026-10-13:

- **One predicate.** The math package's `maps_command(key)` says whether a generic command has a rendering. `command()` uses the same name lists, so the predicate cannot drift from the painter. `math.unmapped_commands(ast)` lists the unmapped commands of a math AST. The painted fallback stays, for previews (Pkg_Math §3.2).
- **Located report.** `math_bridge.issues` scans the LaTeX AST and reports `unsupported-math-command`, located at the formula. It reads amsmath environments (and `equation` under amsmath) from their row sources, as rendering does, rather than from the pre-parsed `ast`, which still contains `\label`.
- **Exclusions.** The scan skips math that an adapter typesets itself: bussproofs proof trees and semantic inference displays. It excludes declared operators and `\directlua`, which is reported as `unsupported-lua`.
- **Why not tag the output.** Several adapters call the math package directly, and rendered math carries no source offset, so tagging the output would lose the location.
- **No golden changed.** The one interim change was a false positive, now fixed.
- **Corpus findings.** Across the 31 samples the report finds two real gaps, both painted as names today: `\Box` in `math_proof` (M2 symbol table) and the old `{\cal L}` font switch in `math_test_flight` (M1/M8).
- **Existing gap.** A declared operator in inline math renders through the name fallback, upright but without operator spacing. Only amsmath rows expand declared operators.

### M0.4 Font-request table

`latex.ls` built five near-identical `font-substitution` blocks, for `cmbright`, `eucal`, `mathrsfs`, `gfsporson` and `helvet`. They are now a `font` field on `CATALOG` entries: `{face, role, css, message}`.

- `role` says which family the face replaces: `main`, `sans`, `greek` or `math`.
- `registry.font_issues` reports one substitution per active entry.
- `registry.font_css(packages, role)` supplies the CSS stack for the document root and for `\textporson`.

The font packages of M9 and the NFSS family codes of M1 become data rows.

### M0.5 Unknown classes

Report the located `unknown-package` diagnostic for a class name that has neither a profile nor a beside-document `.cls`, as D7.2.7 already rules. The document still renders with the `article` profile.

### M0.6 `xparse` from the kernel

Built as a `CATALOG` row with no engine change. The engine records adapter names as loaded, so `\ver@xparse.sty` exists and `\@ifpackageloaded{xparse}` is true, and the kernel's native `\NewDocumentCommand` does the work. Upstream `xparse.sty` was not vendored: it patches the l3 `\__cmd_…` internals, which the native kernel does not have. This closes the engine design §10 item.

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

The §9 `hyperref` row claimed "anchors" while `\hypertarget` was unsupported; the row was corrected when M1 landed.

As built on 2026-10-13, every row above except `alltt` is supported. The notable choices:

- **NFSS declarations** act as a group's or paragraph's leading declaration, like the other font switches. `font_decl.decl_css` reads their arguments, and a value outside the tables is reported in place. Family codes for faces Lambda does not ship (`ptm`, `phv`, …) report `font-substitution`. A preamble `\linespread` sets the document's line height to 1.2 × factor.
- **Fills.** `\hfill` used to be an unstyled marker. A paragraph that holds `\hfill`, `\hfil`, `\dotfill`, `\hrulefill` or `\hspace{\stretch{n}}` now lays out as a flex row in which the fills grow, because CSS has no inline fill. `\vspace{\stretch{n}}` and `\vfill` render nothing.
- **Marks.** `\markboth` and `\markright` set the `latex-chapter` and `latex-section` running strings through a hidden element, which Radiant's paged path reads (`paged_hidden_marks`).
- **Page numbering.** `\pagenumbering` sets the page-counter style for `\thepage` and running heads. Output has one numbering, so a change reports `page-numbering-approximation` and the last style applies; `gobble` prints nothing.
- **Columns.** The `twocolumn` class option sets the document in CSS columns; the title block, `\twocolumn[header]`, `figure*` and `table*` span them. Changing the column count mid-document is reported.
- **Title.** `\thanks` marks use LaTeX's `\fnsymbol` sequence, and their notes follow the title block. Plain-text extraction now skips `\thanks`, renders `\and` as ", " and takes `\texorpdfstring`'s TeX branch, so title metadata no longer absorbs the notes.
- **Footnotes** are keyed by source offset. This also fixes the Pkg_Latex §11.2 "identical footnote text" defect. `\footnote[n]` and `\footnotemark[n]` use n without stepping the counter, and `\footnotetext` takes n or the current counter. The footnote list writes `value` where a number leaves the sequence. The unused `footnote_map` was removed.
- **Contents.** `\addcontentsline{toc|lof|lot}` adds an entry anchored at the call; a leading `\numberline{n}` is its number. `\addtocontents` writes arbitrary material into the `.toc`, so it is ignored and only formatting is lost.
- **hyperref.** `\hypertarget`, `\hyperlink`, `\phantomsection` and `\texorpdfstring` are hyperref-owned handlers. Outline titles use `\texorpdfstring`'s PDF branch (`util.pdf_text_of`).
- **Environments.** `tabbing` needed a neutral parser change ([Input_Latex.md](../input/Input_Latex.md)): `\=` had been read as a macron accent. It renders as a grid in which `\=` and `\>` separate cells and `\kill` rows are dropped. `eqnarray` uses the amsmath row model with rcl columns whether or not amsmath is loaded. `list` items show the label argument unless `\item[x]` gives one.
- **graphicx.** The content of `\resizebox`, `\scalebox`, `\rotatebox` and `\reflectbox` is the last argument (`util.last_argument_start`).
  - A lone `\includegraphics` is sized through its own keys, so its layout box is exact.
  - Other content gets a CSS transform and a `transform-approximation` diagnostic.
  - **Bug fixed:** the existing `\includegraphics[angle=…]` emitted CSS `rotate(θ)`, which turns clockwise, while graphicx angles turn counterclockwise. It now emits `rotate(-θ)`, and `test_latex_pdf_graphics` asserts the corrected form.
- **`\nocite`.** Every bibliography path already collected `\nocite` through `bib_model`. Without a bibliography it renders nothing.
- **Deferred: `alltt`.** Its body is raw, and parsing commands while keeping spaces and line breaks needs its own parser mode. It stays reported as unsupported, rather than showing commands as literal text.

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

As built on 2026-10-13, every item above except `amsxtra` is in place:

- **Catalogue rows.** `textcomp`, `gensymb`, `nameref`, `epstopdf`, `amscd`, `cancel`, `latexsym`, `stmaryrd`, `esint`, `upgreek`, `bbm` and `dsfont` are rows whose commands render. `placeins` (`\FloatBarrier`), `indentfirst` (package CSS), `bookmark`, `pifont` and `marvosym` own commands. Implications are now catalogue data (`implies`): `subcaption` loads `caption` and `bookmark` loads `hyperref`.
- **Math symbol tables** in `lambda.doc.math`: latexsym, a stmaryrd subset (double brackets, short and `Mapsto` arrows, boxed operators, `\bigsqcap`, `\lightning`), esint's oriented and finite-part integrals, gensymb's math forms, and upgreek's upright letters. The parser's own symbol names that had no table entry also landed: `\square`, `\diamond`, `\triangleleft`, `\triangleright`, `\bigtriangleup` and `\bigtriangledown` were painted as words.
  - The unmapped-command scan now covers `symbol_command` nodes too, which is how that gap was hidden.
  - `\leqslant`, `\geqslant` and `\lozenge` were added for amssymb.
  - A script diffs the parser's symbol names against the tables; no gaps remain.
- **Alphabets.** `\mathbbm` and `\mathds` alias `\mathbb` (`font.alphabet_alias`). Double-struck letters render, but `\mathbb{1}` and so `\mathbbm{1}` show a plain 1: the bundled math fonts have no double-struck digits. That is an existing math-package limitation, so bbm and dsfont are Partial.
- **`\cal`** joins the parser's math font switches (`\rm`, `\bf`, …). The typesetter reads the switch's variant from `font.ls`'s style table instead of a duplicated inline map. With this and `\Box`, the 31-sample corpus reports no unmapped math commands.
- **bookmark.** `\bookmark[level=…]{title}` marks its own position, as `\pdfbookmark` does, through a shared outline anchor. An outline entry marks the element that carries it, so `dest=` and the other keys are reported.
- **pifont.** `\ding{n}` follows the Unicode ZapfDingbats mapping. `dinglist` labels every item with one glyph and `dingautolist` counts up from it; the list model now takes a per-item label function. `\dingline` and `\dingfill` are owned but unsupported.
- **Deferred: `amsxtra`.** Its `\sphat`-style superscript accents attach to the preceding atom and need parser work. The corpus sample that loads it uses none, so it stays an unknown package rather than an empty row.

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

As built on 2026-10-13:

- **Source-order settings.** Analysis state gained `settings`, a list of `{key, value, offset}` that analysis appends as it walks. `util.setting_at(settings, key, offset)` returns the value in effect at a source offset. soul's `\set…color`, ulem's `\normalem`/`\ULforem`, mdframed's `\newmdenv`/`\mdfdefinestyle` and `\fvset`/`\setminted` all use it, so a setting holds from where it is given until changed.
- **xspace** is the first engine binding with no upstream package: `tex/binding/xspace.sty` defines `\xspace` as `\lambda@xspace`.
  - That is a new expandable engine primitive. It reads the next token, puts it back, and puts a space before it unless the token is in xspace's exception list: `,.'/?;:!~-)`, a group delimiter, a space, `\/`, `\ `, `\footnote`, `\footnotemark`, `\bgroup` or `\egroup`.
  - A `\futurelet` definition worked in body text but leaked inside `\section` and `\footnote`, because argument mode does not execute assignments (§12). A primitive needs none.
- **soul and ulem** share `decorations.ls`. Each command maps to CSS text decoration, letter spacing or a background.
  - soul's colors resolve through `color.ls` from the setting in effect where the command stands. `\so` and `\caps` use soul's default spacing.
  - Without `normalem`, ulem's `\emph` underlines, until a later `\normalem`; `\ULforem` underlines again. The parser now gives text-style commands a `source_offset` for this.
  - `\xout` strikes through and reports `xout-approximation`.
- **setspace, parskip and ragged2e** share `paragraph.ls`.
  - Line height is the 1.2 baselineskip ratio times setspace's per-size stretch (`font_decl.line_height_for`, as for `\linespread`). The package option or a preamble declaration sets the document's line height. The environments become `div.latex-<name>`.
  - A body declaration spaces the rest of its group: `find_leading_decl` finds it for the paragraph, and `next_render_info` carries it to the paragraphs after it.
  - parskip adds a stylesheet rule with no indent and half a baselineskip between paragraphs. `skip` and `indent` override either.
  - ragged2e reuses the alignment wrapper, now `font_decl.wrap_align_decl`, adding `hyphens:auto`.
- **lipsum and blindtext** do not bundle their text: `filler.ls` cycles three paragraphs of Cicero's de Finibus 1.10.32–33, the public-domain source of lorem ipsum. Each use reports `filler-text-approximation`. `\lipsum[a-b]` keeps its paragraph count and `\lipsum*` runs the paragraphs together inline.
- **paralist**, in `paralist.ls`:
  - Compact lists drop list spacing. Run-in lists (`asparaenum`, …) set each item as a paragraph led by its label. Inline lists (`inparaenum`, …) run their items into the paragraph: `render_children` rejoins the text the parser split around the environment.
  - The label template argument (`[(a)]`, `[{Step} 1:]`) works on paralist's lists and on `enumerate`. The first `1`, `a`, `A`, `i` or `I` outside braces is the counter. The enumerate label diagnostic now fires only for a template with no counter.
  - `util.roman` was promoted from `docclass/book.ls` for this.
- **fancyvrb and minted** render through the listings code renderer, which gained a `language` class (`codeblocks.ls`).
  - The parser keeps the environments' bodies raw and reads `\Verb`, `\mint` and `\mintinline` like `\verb`, with either braces or a delimiter. minted's `{language}` becomes a `language` attribute.
  - Keys with a listings counterpart map onto it: `numbers`/`linenos`, `firstnumber`, `stepnumber`, `frame`, `fontsize`. Other keys report `unsupported-listing-option`. Frame styles other than `single` draw a box and report `frame-approximation`, and a minted language reports `listing-highlighting-omitted`.
  - minted makes `listing` a float kind, with `\listoflistings`.
- **Frames.** `boxes.frame_css` is the shared frame-box model (background, line, sides, padding, radius, shadow).
  - tcolorbox now uses it, and so do framed's six environments and `leftbar` (`frames.ls`), mdframed and fancybox's four boxes. `boxedminipage` is a framed inline block.
  - mdframed reads a bounded key profile and `frametitle`. Its defaults are a .4pt black line, a white background and mdframed's margins. `\mdfdefinestyle` styles and `\newmdenv` environments come from settings, and other keys report `unsupported-mdframed-key`.
  - `util.leading_option` now spans braced groups, so `[frametitle={A, B}]` no longer leaks into the body.
- **footmisc** (`footmisc.ls`):
  - `symbol` and `symbol*` mark footnotes with `\fnsymbol`. `util.fnsymbol` was promoted from the `\thanks` marks, which now use it too. The notes list then prints the marks itself.
  - `norule` drops the rule. `bottom` and `stable` already hold for end-of-document notes. `perpage`, `para` and the rest report `unsupported-option`.
  - A `\label` inside a note now refers to it, because analysis walks the note body with the footnote as context (`\@currentlabel` is the mark). Before, labels inside notes were never recorded. `\footref{label}` repeats the mark, linking to the label's anchor in the note. An unknown label reports `unresolved-reference` under footmisc.
- **appendix** (`appendix.ls`):
  - `appendices` letters the class's top level. It continues an earlier environment's letters, and gives the counter its value back at the end, so numbering resumes after it (`\restoreapp` and `\@pprestoresec`).
  - `subappendices` letters the level below: 1.A in book and report, and 1.A for article subsections.
  - `\appendixpage` sets an "Appendices" title. `\addappheadtotoc` adds an unnumbered, anchored contents entry. The options `toc` and `page` do both at the start of `appendices`.
  - `title` and `titletoc` name headings and contents entries "Appendix A". `header` names running heads and reports `unsupported-option`.
  - The names are English, like the other fixed names.
- **Interpreter defects found on the way** ([LR01-19](../Lambda_Issue_Ledger%20(fixed).md#lr01-19), [LR01-20](../Lambda_Issue_Ledger%20(fixed).md#lr01-20)): a closure created in a module-level comprehension read its captured loop variable from module storage, and a function literal bound in a `let` list was skipped as a declaration. Both are fixed in `interp.cpp` and pinned on every tier by `test/lambda/closure_capture_comprehension.ls`.

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

As built on 2026-10-13:

- **Float kinds are dynamic.** Analysis starts from `floats.kinds_for(packages)`: the base figure and table, plus the `algorithm` kind when `algorithm` or `algorithm2e` is active.
  - That kind's style follows the `plain`/`ruled`/`boxed` options; the defaults are ruled for algorithm and plain for algorithm2e.
  - `section` or `chapter` numbers it within that heading.
  - `\newfloat`, `\floatname`, `\floatstyle` and `\restylefloat` change the kinds as analysis walks.
  - `render_float` serves figure, package and `\newfloat` floats. Captions take their name from the document's kinds, and a non-plain style adds a class; tables take it too. `\listof{type}{title}` and `\listofalgorithms` reuse the list-of rendering.
- **Placement leak fixed.** The parser kept an environment's `[option]` out of the body only for `table`, so `\begin{figure}[htbp]` printed "[htbp]". `util.leading_option` now strips it for every float and reads `algorithmic`'s `[n]`. The parser cannot know package or `\newfloat` environments, so the fix is in script.
- **Preamble commands as data.** A `preamble` field on `CATALOG` entries lists owned commands that belong in the preamble, so they are not reported there as unsupported.
- **algorithmic.** [algorithmic.ls](../../lmd/package/latex/packages/algorithmic.ls) folds the keyword commands of algpseudocode and the older capitalised algorithmic into lines with depth, numbers and labels. Analysis binds `\label` to line numbers, and render draws the lines. `util.flatten_paragraphs` was promoted from render.ls for this.
  - Keyword labels come from the document's `\algorithmic<word>` definitions. `\algrenewcommand` leaves the new label as the macro's argument, and `\renewcommand` is collected as a macro.
  - **Limitation:** with `\renewcommand`, the engine still reports the label macro as undefined, because no algorithmic.sty runs. Defining the label macros needs the §17.1 binding.
- **algorithm2e.** [algorithm2e.ls](../../lmd/package/latex/packages/algorithm2e.ls) folds the float body:
  - `\;` ends a statement.
  - Block commands (`\If`, `\uIf`, `\eIf`, `\ElseIf`, `\Else`, `\For`, `\ForEach`, `\ForAll`, `\While`, `\Repeat`) re-parse each brace argument from its raw source, because the parser flattens argument groups. User macros defined in the document are therefore not expanded inside those bodies.
  - `\KwIn`/`\KwOut`/`\KwData`/`\KwResult` lines, `\KwRet`, `\KwTo`, `\tcp` and `\tcc` render. Numbering, semicolons and end lines follow the options and the settings commands, which apply to every algorithm rather than one.
  - `\SetKwFunction` and the other keyword definitions are reported.
- **subfig and subfigure.** `\subfloat` and `\subfigure` letter their subfloats within the enclosing float and caption them from the last optional argument. `\subref` works with them. A label belongs inside the caption argument, as subfig documents.
- **wrapfig.** `environment_header` reads the header groups that the parser leaves as body text.
- **rotating.** Sideways floats render upright with `sideways-approximation`. `turn`, `rotate` and `sideways` reuse graphicx's transform helpers (promoted to `pub`).
- **landscape.** The content renders on the continuous page, with `landscape-approximation` for PDF.

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

As built on 2026-10-13:

- **The column preamble moved to `packages/array.ls`.** It is the parser that was in render.ls, now taking one context map (`si`, `dcolumn`, colors, settings, the table's offset). It adds what the table model lacked:
  - **Vertical rules.** `|` was dropped before. A rule before a column is the cell's left border, and rules after the last column its right border; `||` draws a double line.
  - **`\newcolumntype{x}[n]{body}`.** It was silently ignored. Analysis records each definition as a source-order setting, and the preamble expands defined letters, arguments included, before reading it. A `*{n}{…}` pattern is expanded when it is read.
  - The column decorations: rules and `\columncolor` fold into the column that follows them, which becomes a map only when it is decorated, so undecorated tables render unchanged.
- **colortbl** (and `xcolor[table]`, now a data implication: a CATALOG row's `option_implies` adds packages for its options, and implications resolve level by level).
  - `\columncolor` in a `>{…}` prefix colors the column; `\rowcolor` the row; `\cellcolor` the cell, as a marker that the row builder turns into the cell's background. A colored row hides the column color, and a cell color hides both, which is colortbl's order. Every color command takes the `[model]{spec}` form through `color.command_color` (promoted from color.ls; `\rowcolor[HTML]{…}` failed before).
  - `\arrayrulecolor` is a source-order setting. Vertical rules take the color in effect where the table starts, and each horizontal rule the one in effect where it stands; a colored `\hline` or booktabs rule is drawn as a full-width colored line, so uncolored tables keep their markup.
  - `\doublerulesepcolor` is accepted and reports `rule-separation-color-approximation`: a CSS double line's gap shows the background.
- **hhline** (`hhline.ls`). `\hhline{…}` is a rule row with one cell per column: `=` double, `-` single, `~` none, and `*{n}{…}` repeats. The vertical marks change nothing, since columns draw their own vertical rules.
- **dcolumn.** `D{in}{out}{places}` is a decimal-grid column on the siunitx `S` model (`siunitx.dcolumn_descriptor`). Cells split at the input separator without number formatting, the output separator prints (`\cdot` as a centered dot), and `places` sets the minimum widths. A `\multicolumn` cell stays out of the measured grid, for `S` columns too.
- **threeparttable.** The wrapper is a CSS table whose caption and `tablenotes` are table captions, so they wrap at the table's width. `\tnote` is an italic superscript. Notes hang their labels; `para`, `flushleft`, `online` and `normal` work as package or environment options. `measuredfigure` shares the wrapper.
- **adjustbox** (`adjustbox.ls`). A lone `\includegraphics` is sized through graphicx's keys; other content gets an inline box with the size, `max width`/`max height`, `scale` and `angle`, and reports `transform-approximation` like graphicx's boxes. `frame`, `fbox` and `bgcolor` use the frame-box model, `valign` sets the vertical alignment, and `center` centers the box on a line. `trim` works on a lone image only. Other keys are reported.
  - `graphicx.lone_image` and `sole_image` replace two copies of the lone-image test, and `util.environment_header` and `util.group_source` were promoted from render.ls for the environment form.
- **pdfpages** (`pdfpages.ls`). `\includepdf` reads the PDF once (`graphicx.pdf_document`, `render_pdf_page`) and inserts each selected page as a block: page numbers, ranges either way round, open ranges, `last`, `-` for all and `{}` for a blank page. A page fills the line, and `scale` is a fraction of that; `width`/`height`, `angle`, `frame` and `\includepdfset` defaults work. `nup`, `pagecommand` and `landscape` report `pdfpages-layout-approximation`. `\includepdf` is recorded as an asset like `\includegraphics`.
- **csvsimple** is an engine binding, `tex/binding/csvsimple.sty`, written in expl3 from the csvsimple-l3 manual. `\csvreader` reads the file through the engine's path-gated `\openin` (`\ior_get`), and puts back, for each line, the line's assignments followed by the user's command list, inside the table keys' `\begin{tabular}…\end{tabular}`. Document macros in the command list therefore work, which a script substitution could not offer.
  - Profile: `\csvreader`, `\csvautotabular`, `\csvautobooktabular`, `\csvautolongtable`, `\csvautobooklongtable`, `\csvset`, `\csvstyle`, `\csvnames`; `\csvcoli`…, `\csvlinetotablerow`, `\thecsvrow`; keys for the separator, the head line and column names, the table and line hooks, `filter equal`/`not equal`/`strcmp`, and `respect …`, where the data's special characters become the commands that print them.
  - Reading a file and setting keys are assignments, so `\csvreader` belongs in body text, not in a constructor's arguments (engine design §4).

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

As built on 2026-10-13:

- **Declaration lists.** titlesec formats, sectsty fonts, tocloft hooks, authblk fonts and titling's pre/post code are all raw TeX declaration lists. One reader, `font_decl.declarations(raw, colors)`, turns such a list into CSS: font switches and sizes, `\color`, `\fontsize`, the NFSS commands, alignment (`\centering`, ragged2e's and titlesec's `\fil…` commands, the center and flush environments), `\MakeUppercase`/`\MakeLowercase`, `\titlerule`, `\vspace`/`\vskip`, and printed text. It lists every other command, and each package reports those where they are given.
- **Bindings that declare macros.** tocloft, authblk, titling and minitoc are configured by redefining their macros (`\renewcommand{\cftsecfont}{…}`), which the engine rejects as undefined unless something defines them. Four bindings (`tex/binding/{tocloft,authblk,titling,minitoc}.sty`) define those macros with the packages' defaults and nothing else. The document's `\renewcommand` is then legal and echoed, and the adapters read it through `macros.last_macro`, the definition in effect after the preamble.
  - The packages' commands (`\cftsetindents`, `\affil`, `\pretitle`, `\minitoc`) stay undefined, so they reach the adapters as constructors.
  - tocloft's `\newlistof` makes its entry's macros and counter in the binding, then passes itself to the adapter with `\noexpand`.
- **titlesec and sectsty** (`titles.ls`). Analysis records `\titleformat` (with `\titleformat*` keeping the rest of the earlier format), `\titlespacing`, `\titlelabel` and the sectsty commands as source-order settings. The heading renderer asks `titles.heading_style` for the style in effect at the heading.
  - The format styles the heading. The label template gets the displayed number for `\the…` and `\thetitle`, and "Chapter" or, in an appendix, "Appendix" for `\chaptertitlename`. `sep` spaces the label, and `display` sets the label on its own line. The before-code styles the title text (`\MakeUppercase` as `text-transform`), and a `\titlerule` draws the heading's bottom border.
  - `\titlespacing` margins read `*n` as n ex. Without the star, the next paragraph is indented (a `latex-indent-after` class).
  - The size, font, alignment and `compact` options style every level. `runin`, `leftmargin`, `rightmargin`, `drop` and `wrap` render as `hang` and report `title-shape-approximation`; `frame` draws a border.
  - Headings with neither package render as before.
- **tocloft** (`contents.ls`).
  - Each entry level's `\cft…font`, `\cft…presnum`, `\cft…aftersnum` and `\cft…aftersnumb`, `\cftsetindents` and the `\cftbefore…skip` lengths style the contents and the float lists. Analysis now records every `\setlength` and `\setcounter` as a setting for such reads.
  - The title takes `\cft…titlefont` and `\cftafter…title`.
  - Page numbers, leaders and dot separations are accepted without effect, since the continuous contents have no page numbers.
  - `\newlistof` adds a list-only kind to the float kinds: `floats.listed_kind`, which `kind_of` never treats as an environment. It is fed by `\addcontentsline` through the kinds' new `ext` field (`floats.kind_of_file`, replacing the lof/lot test) and printed by `\listof<entry>`.
  - In the kernel, the contents and list titles now follow a renamed `\contentsname`, `\listfigurename` or `\listtablename`.
- **Fix: `\protect\numberline`.** `\addcontentsline` ignored `\numberline` behind `\protect`, the usual form, so the number ran into the entry text. Analysis now skips `\protect`.
- **minitoc.** `\minitoc`, `\secttoc` and `\parttoc` list the headings under the enclosing chapter, section or part, down to `minitocdepth`/`secttocdepth`/`parttocdepth`. The title comes from `\mtcsettitle` or the redefined `\mtctitle`. Heading entries gained their source offset for this. `\dominitoc` and the other setup commands render nothing.
- **titling** (`frontmatter.ls`).
  - `\pretitle`/`\posttitle` and the author and date pairs are read as one declaration list per part, since an environment opened in one closes in the other. They give the part CSS, alignment, the space after it and their own printed text.
  - `\droptitle` moves the title block, the four `\maketitlehook…` slots render, and `\thetitle`, `\theauthor` and `\thedate` print the given parts.
  - **Parser change:** those six commands' arguments stay raw text, like a `\newenvironment` body (`is_code_fragment_command` in `input-latex-tables.cpp`), because the parser rejected `\pretitle{\begin{center}…}` as an unclosed environment.
- **authblk.** Analysis collects `\author[marks]` and `\affil[marks]` in order.
  - With marks, the names are joined with `\Authsep`, `\Authand` and `\Authands` and carry their marks, and each affiliation follows on its own line with its mark. Without marks, the authors before each run of affiliations form a block with them.
  - `\Authfont`, `\Affilfont` and the `auth-*`/`affil-*` options style both.
- **varioref** (`varioref.ls`). `\vref`, `\Vref`, `\vpageref`, `\Vpageref`, `\vrefrange`, `\vpagerefrange` and `\fullref` print the reference (cleveref's typed form when cleveref is loaded) with an English page phrase. The phrase carries `\pageref`'s page anchor, which paged output fills.
  - Continuous output reports `unresolved-page-counter`, and unknown labels report `unresolved-reference`.
  - The "next page" and "facing page" forms need both pages, so they are not produced.
  - `page_ref`, `number_ref` and `cref_keys` were factored out of the `\pageref`, `\ref` and `\cref` renderers for reuse.
- **Also fixed:** M5's csvsimple catalog row had not been written (an edit was refused, and bundled bindings resolve without a row). It is added, so the `l3` and `legacy` options are validated.

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

As built on 2026-10-13:

- **The registry** (`terms.ls`). Analysis keeps the definitions (`gls_entries`, `ac_entries`, `nomencl`, `index_entries`) and, for every use, its first-use flag by source offset (`gls_uses`, `ac_uses`).
  - These live in one nested `terms` field of the analysis state (`terms.EMPTY`, `with_terms`).
  - The walk copies the state map at every node. As eleven top-level fields, the registry slowed the 31-sample corpus by about a quarter: the `algebra` sample went from 35 s to 44 s, and the corpus test exceeded its 60 s limit. Nested, it costs one field. The labels whose first use is over (`gls_firsts`, `ac_firsts`) thread through the walk, so `\glsreset`, `\glsunset`, `\glsresetall`, `\glsunsetall`, `\acused` and `\acresetall` take effect in source order. Render prints each use and each list from that state.
- **Fields are TeX source.** Entry names, descriptions, acronym forms, nomenclature symbols and index displays are kept as raw text and rendered through `util.parse_fragment`, which parses a fragment on its own (plain text skips the parse). algorithm2e's block bodies, which did the same by hand, now use it.
  - **Limitation:** a fragment does not pass through the document's expansion, so document macros inside these fields are not expanded.
- **glossaries.**
  - `\newglossaryentry` (and `\longnewglossaryentry`) take glossaries' defaults: text and first from the name, the plural from the text. `\newacronym` goes to the acronym glossary under the `acronym` option; its first use is "long (short)".
  - The `\gls` family covers plural, `\Gls` and `\GLS` forms; `\glstext`, `\glsfirst`, `\glsplural`, `\glsname`, `\glsdesc` and `\glssymbol` with their capitals; the `\glsentry…` forms; the `\acr…` forms with plurals and capitals; and `\glsdisp`, `\glslink`, `\glsadd` and `\glsaddall`.
  - Only the `\gls` family and `\glsdisp` end a first use, as in glossaries.
  - `\printglossary[type, title]`, `\printacronyms` and `\printglossaries` print the listed entries of each type, sorted by their sort keys, as a description list. Each entry has its description, the post-description dot (`nopostdot`, or a redefined `\glspostdescription`) and section locators linking back to its uses (`nonumberlist` drops them).
  - A use links to its entry when that entry's glossary is printed. `\newglossary` adds types. The `toc` option adds a contents entry per printed glossary, and it follows a renamed `\glossaryname` or `\acronymname`.
  - `\loadglsentries` reads a file beside the document through the binding's `\input`.
  - Styles other than the list styles report `glossary-style-approximation`, and acronym styles other than long-short report `acronym-style-approximation`.
- **acronym.**
  - `\acro`/`\acrodef` (the short form defaults to the label) and `\acroplural`.
  - The `\ac` family: `\ac`, `\acp`, `\acf`, `\acfp`, `\acs`, `\acsp`, `\acsu`, `\acl`, `\aclp`, `\aclu`, with capitals. A redefined `\acsfont`, `\acffont` or `\acfsfont` styles its part (`font_decl.argument_font_css` reads `\textsc{#1}`-style bodies).
  - The environment prints its entries as a description list. The options `printonlyused`, `nolist`, `dua` and `nohyperlinks` work. `withpage`, `footnote` and `smaller` are reported as unsupported options.
- **nomencl.** `\nomenclature[prefix]{symbol}{description}` entries print sorted by prefix and symbol under `\nomname`. `intoc` adds the contents entry.
- **makeidx and imakeidx.**
  - `\index` leaves an anchor.
  - `\printindex` prints every entry path in case-folded sort order, with letter groups apart. Subentries come from `!`, with their parents printed even if they have no locators of their own.
  - Locators are the section of each use, one per section, linking back to the anchor.
  - The index also handles `|textbf`/`|textit` formats, `|(`…`|)` ranges, `|see`/`|seealso`, `key@display` and the `"` quote. It goes in two CSS columns.
  - imakeidx's `\makeindex[name, title, intoc, columns]` and `\index[name]`/`\printindex[name]` give named indexes.
- **Lists.** A list's heading is a chapter-level heading in book and report and a section-level one otherwise. Its contents entry, from the generalized `add_unnumbered_head` (once appendix's), carries the name macro so that the contents follow a renamed title.
- **Bindings and the kernel.** `tex/binding/{glossaries,nomencl,acronym}.sty` declare the macros documents rename. The kernel now declares `\indexname`, `\listfigurename`, `\listtablename`, `\bibname`, `\chaptername`, `\partname` and `\appendixname` like `\contentsname`, so renaming them is legal.
- `util.option_on(options, key)` replaces two copies of the same option test.

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

As built on 2026-10-13, with route (a) chosen by the user and **D7.2.7v2** ratified the same day:

- **Bindings.** The engine searches `lmd/package/latex/tex/binding/` after the upstream files. Bindings are written from the package manuals, so the vendor rule does not apply; each is also a `CATALOG` row, so options are validated and the declaration reaches the script.
  - Upstream `physics.sty` is not installed locally and leans on xparse embellishments the kernel diagnoses, so `physics.sty` is a Lambda-owned binding of the physics 1.3 manual. It covers every group above plus `\dd`, `\var`, the quick-quad words, `\Pmqty`/`\smqty` forms and `\zmat`/`\pmat`/`\admat`, and the `arrowdel`, `italicdiff` and `notrig` options.
  - Every binding command expands with no assignment at use time. The engine expands constructor arguments without executing assignments, so this keeps the commands working inside `\section`, `\caption` and `\footnote`.
  - Names math already has (`\sin`, `\div`, `\Re`) reach their plain meaning through `\noexpand`, since the engine passes an undefined math command through.
  - physics and siunitx both define `\qty`; loading both is `conflicting-packages`.
- **mathtools** is split by owner:
  - The macro layer is the `mathtools.sty` binding: `\DeclarePairedDelimiter`, `X` and `XPP`; `\prescript`; `\splitfrac`/`\splitdfrac`; `\cramped`, `\smashoperator` and `\adjustlimits`, which keep their content; `\MoveEqLeft`; and `\shortintertext` as `\intertext`.
  - `\DeclarePairedDelimiterX` builds one body macro per size (plain, starred, `\big` to `\Bigg`) at declaration time, replacing `\delimsize`. A call then needs no assignment.
  - Colon relations are math symbols, composed from bundled glyphs into one relation atom. Environments are math (below).
  - Tag forms are equation numbering: analysis records `\newtagform` and the current `\usetagform`. One `amsmath.tag_text` frames every tag (`equation`, alignment rows, the legacy path and `\eqref`), replacing three hand-written copies. A number format outside the text font commands reports `unsupported-tag-format`.
  - `\mathtoolsset` is reported as unsupported.
- **Math package (`lambda.doc.math`).**
  - `\bm`/`\hm` are `\boldsymbol` aliases.
  - `\cancelto` is a box transform with the value at the arrow's tip.
  - `\nicefrac`/`\sfrac` are slanted fractions with nicefrac.sty's raise and kerns.
  - The parser knows the mathtools environments. Starred cases parse their second column as text through the `\text` span builder, and `multlined`/`lgathered`/`rgathered` options are read and ignored.
  - An environment the parser does not know now keeps its name, and `unmapped_commands` reports it as `begin{name}`. Before, `\begin{psmallmatrix}a\end{psmallmatrix}` silently rendered "a psmallmatrix".
- **mhchem.**
  - `\ce` and `\pu` parse as `chemistry` nodes holding their raw text. `mhchem.ls` turns the bounded notation into TeX math, which the ordinary parser reads; the font pass, the typesetter and the unmapped scan all see that translated math.
  - In text, `\nicefrac`, `\sfrac`, `\ce` and `\pu` render through `math_bridge.command_ast`, which rebuilds the command from its argument groups. `\ensuremath` now shares its renderer.
- **Analysis diagnostics.** Analysis state gained a `diagnostics` list, which `render_result` reports. Tag forms are its first user.
- **Follow-up the same day:** `\cdot` painted low because the fallback order put KaTeX_AMS, whose U+22C5 is `\centerdot` on the baseline, before KaTeX_Main; Main now comes first and `\varnothing` keeps the AMS shape by name. `\boldsymbol` now bolds every symbol the bundled bold face has. A symbol it lacks (∇, ⋅) keeps its regular glyph with `math-bold-symbol-approximation`, so `\grad` and `\vdot` stay regular, and `\bm` falls back to poor man's bold (Pkg_Math §5.1, §10.1).

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

As built on 2026-10-13, with §17.3's "substitution only":

- **Families through the root's variables.** The stylesheet sets `--latex-serif`, `--latex-sans` and `--latex-mono` on `.latex-document` to the bundled Computer Modern faces. The family declarations and classes read them, so a document's choice reaches every sans and mono span:
  - `font_decl.SERIF_CSS`/`SANS_CSS`/`MONO_CSS`, one definition each in place of the copies in font_decl, caption, amsmath and listings;
  - `.latex-sf`, `.latex-ttfamily`, `code.latex-code` and the rest.
  - The `\ttfamily` declaration used to set a generic `monospace` inline, which no stylesheet could override.
- **The text font packages** are catalogue rows. A row's `font` may now list several faces: times sets serif, sans and mono. The rows cover the 18 packages of the plan.
  - Each face is a CSS stack of installed look-alikes (TeX Gyre, the system faces) and reports `font-substitution`.
  - A math face reports that math keeps the bundled faces.
  - `sourcesanspro[default]` also replaces the main family (`default_option`).
  - `document_style` sets the root's variables from these rows.
- **fontspec** (`fontspec.ls`).
  - `\setmainfont`, `\setsansfont` and `\setmonofont` set the roles (fontspec wins over a font package). `\newfontfamily`/`\setfontfamily`/`\newfontface` make switches, which act as leading declarations of a group or paragraph like the NFSS ones (`font_switch_css`). `\fontspec{Name}` and `\addfontfeatures` work the same way.
  - Features: `Ligatures`, `Numbers`, a numeric `Scale`, `Color` and `Letters=SmallCaps` map to CSS. `Script`, `Language`, `Mapping` and `Renderer` need none. Other features (`Path`, the font-file keys, `Scale=MatchLowercase`) report `unsupported-font-feature`.
  - The main role's features go on the root, and the sans and mono roles' on their elements through the stylesheet.
  - Every face that is not Computer or Latin Modern reports `font-substitution`.
  - Polyglossia's `\newfontfamily` reading moved here from language.ls (polyglossia now implies fontspec), and so did its substitution reports.
- **unicode-math.** The binding `tex/binding/unicode-math.sty` turns `\symup`, `\symit`, `\symbf`, `\symsf`, `\symtt`, `\symbb`, `\symcal`, `\symscr`, `\symfrak` and `\symbfit`, and the `\mathbfit`-style forms, into the math package's alphabets.
  - Bold sans, calligraphic and Fraktur lose their bold.
  - `\setmathfont` reports a substitution unless it names Latin Modern or New Computer Modern Math. Math keeps the bundled faces, because the math renderer measures with them.
  - `math-style`, `bold-style` and `sans-style` other than TeX's report `unsupported-option`.
  - Unicode math input passes through the math parser.
- **fontawesome5** (`icons.ls`). `\faIcon{name}` and every `\fa<Name>` command render the icon's Unicode look-alike where there is a clear one (✉, ☎, ★, ✓, …), else `[name]`. The package reports one substitution.
  - The `\fa<Name>` commands are owned through a new catalogue field, `command_prefix`. `registry.owner` checks it cheaply, and a handler table's `"*"` entry serves those commands.

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
   - *Approved 2026-10-13.* The D7.2.7 revision will be proposed verbatim, with its companion edits, when the first such binding lands (M3 `xspace` or M8 `physics`), and applied only after the user confirms it.
   - *Ratified 2026-10-13 as **D7.2.7v2*** (spec 30.0.0) when the physics binding landed: bundled packages are upstream programming packages or Lambda-owned bindings that only expand user commands into constructs the digester and adapters own.
2. **`mhchem`.** Should `\ce` and `\pu` get a bounded script parser (formulas, charges, states, arrows), or stay unsupported? Pkg_Math §7 and §10.3 currently exclude general chemistry.
   - *Recommendation:* a bounded profile owned by `lambda.doc.math`. *Approved 2026-10-13*; Pkg_Math §7 and §10.3 are revised when the profile is built.
3. **Fonts.** Should Lambda bundle OFL faces (TeX Gyre Termes, Pagella and Heros; Libertinus; Font Awesome Free), or only report substitution?
   - *Recommendation:* substitution only for now; revisit against the release size budget. *Approved 2026-10-13.*
4. **Classes.** Should tier A and B profiles come first, or the kernel-internals constructor route?
   - *Recommendation:* profiles first, because they cover the common templates sooner. *Approved 2026-10-13.*
5. **beamer.** Should beamer get a sub-design, `vibe/latex/Lambda_Design_Beamer.md`, lowering frames onto a `lambda.slide` deck (Pkg_Slides SLD1–SLD4 and §7 cues)? Frames become slides, overlay specifications become cues, and blocks, columns and themes map with diagnostics. The alternative is to keep it out of scope.
   - *Recommendation:* the sub-design. Until then, beamer reports `unknown-package` (M0.5). *Approved 2026-10-13.*
6. **Priority evidence.** The ordering here is judgment plus the 31-sample corpus. Measuring `\usepackage` frequency on a public corpus, such as arXiv sources or Overleaf gallery templates, would require downloads. Ask for it if wanted.
   - There was no recommendation and no request, so nothing is downloaded and the order in §3 stands.

## Progress log

- 2026-10-12: plan written from the §1 probes at `2665b0c81`; not started.
- 2026-10-13: the user approved the §17 recommendations and asked for M0 in a worktree, `latex-phase5-m0`, branched from `origin/master` `836c0a773`. M0.1 to M0.6 were built as recorded in §4.
  - New module `lmd/package/latex/floats.ls`; the math package gained `maps_command` and `unmapped_commands`.
  - New test `test/lambda/latex/test_latex_phase5_groundwork.ls` with its golden. The other 61 LaTeX scripts and the 32 math scripts match their goldens unchanged.
  - Removed: the custom-counter list and its per-counter match arms, `get_figure_num`/`get_table_num` and their text-matching duplicates, two copies of the amsmath environment condition (now `amsmath.handles`), and the duplicated `alltikzpeople` arms.
- 2026-10-13: M0 committed on the worktree branch (547f9d76e). M1 was built as recorded in §5, except `alltt`.
  - New test `test/lambda/latex/test_latex_phase5_kernel.ls` with its golden. The other 62 LaTeX scripts and the 32 math scripts match their goldens.
  - `test_latex_font_decl` passes declaration elements to `wrap_font_decl`; its expected output is unchanged. `test_latex_pdf_graphics` asserts the corrected rotation sign.
  - `render_document` and `render_body` share one article builder.
- 2026-10-13: M1 committed (0ee19211f). M2 was built as recorded in §6, except `amsxtra`.
  - New module `lmd/package/latex/packages/dingbats.ls`.
  - New test `test/lambda/latex/test_latex_phase5_declarations.ls` with its golden. The other 63 LaTeX scripts and the 32 math scripts match their goldens.
  - The parser change (`\cal` as a math font switch) is one name in `input-latex-c.cpp`.
- 2026-10-13: M2 committed (1ec9a2aa8). M4 was built as recorded in §8.
  - New modules: `packages/algorithmic.ls` and `packages/algorithm2e.ls`.
  - New test `test/lambda/latex/test_latex_phase5_floats.ls` with its golden. The other 64 LaTeX scripts and the 32 math scripts match their goldens.
- 2026-10-13: M4 committed (1e7ca8a77). The user chose route (a) for physics. M8 was built as recorded in §12, and **D7.2.7v2** was ratified.
  - New files: the bindings `lmd/package/latex/tex/binding/physics.sty` and `mathtools.sty`, and `lmd/package/math/mhchem.ls`.
  - New tests: `test/lambda/latex/test_latex_phase5_math.ls` and `test/lambda/math/test_math_phase5_packages.ls`, with goldens. The other 65 LaTeX scripts and 32 math scripts match their goldens, and `make test-lambda-baseline` passes 6807/6807.
  - The parser changes are in `input-latex-c.cpp`: environment names, `ignored_begin` names, starred-cases text columns, `\cancelto`, slanted fractions and `chemistry` nodes. The engine change is the `binding/` search in `bundled_path`.
  - `strip_backslash` was promoted from `macros.ls` to `util.ls`.
  - The math test failed on the JIT tier alone, which exposed a transpiler defect, [LR07-47](../Lambda_Issue_Ledger%20(fixed).md#lr07-47). The new `chemistry` branch made `unmapped_commands` tail-recursive, and TCO then turned the self call inside its `for` comprehension into a jump. It is fixed in `emit_for_result` and pinned on every tier by `test/lambda/tail_call_in_comprehension.ls`.
- 2026-10-13: the user asked for the cdot and bold-symbol fix (a2fb3c633), then for M3. M3 was built as recorded in §7.
  - New modules in `lmd/package/latex/packages/`: `decorations.ls`, `paragraph.ls`, `filler.ls`, `paralist.ls`, `codeblocks.ls`, `frames.ls`, `footmisc.ls` and `appendix.ls`; the binding `tex/binding/xspace.sty` and the engine primitive `\lambda@xspace`.
  - New test `test/lambda/latex/test_latex_phase5_text.ls` with its golden, identical on every tier. The other 66 LaTeX scripts and the math scripts match their goldens, except `test_latex_argument_arrays`: `\textbf` now carries `source_offset`, and `len` counts attributes (S8.3.1v2).
  - `make test-lambda-baseline` passed 6790 of 6811 under `LAMBDA_LOG_LEVEL=error`, which kept `log.txt` small on a nearly full disk. The other 21 are the MIR ratchet and two lint tests, which read warnings that the error level suppresses; each passes when rerun without the override.
  - The interpreter defects LR01-19 and LR01-20 were found by soul's settings table and fixed (§7).
- 2026-10-13: M3 committed (bf2714af6). M5 was built as recorded in §9.
  - New modules in `lmd/package/latex/packages/`: `array.ls`, `hhline.ls`, `adjustbox.ls` and `pdfpages.ls`; the binding `tex/binding/csvsimple.sty`.
  - New test `test/lambda/latex/test_latex_phase5_tables.ls` with its golden and two CSV fixtures, identical on every tier. The other LaTeX and math scripts match their goldens, except two expectations that follow from vertical rules: `test_latex_tables` checks the cells of a `|l|c|r|` table, now bordered, and `test_latex_css` finds `latex-table` in the stylesheet, now true through `.latex-tablenotes`.
  - `make test-lambda-baseline` passed 6790 of 6812 under `LAMBDA_LOG_LEVEL=error` (1.1 GB of disk left). The MIR ratchet and the two lint tests pass without the override, as in M3, and `UiDtnaTests.PackageContractsAgreeAcrossTiers`, whose JIT run of `dtna_contracts` printed nothing in the loaded run, passes alone; M5 does not touch that package.
- 2026-10-13: M5 committed (f19242ca8). M6 was built as recorded in §10.
  - New modules in `lmd/package/latex/packages/`: `titles.ls`, `contents.ls`, `frontmatter.ls` and `varioref.ls`; the bindings `tex/binding/{tocloft,authblk,titling,minitoc}.sty`.
  - New test `test/lambda/latex/test_latex_phase5_front.ls` with its golden, identical on every tier. The other LaTeX, math and TeX tests match their goldens.
  - `make test-lambda-baseline` passed 6792 of 6813 under `LAMBDA_LOG_LEVEL=error`; the other 21 are the MIR ratchet and the two lint tests, which pass without the override.
- 2026-10-13: M6 committed (d7fdba0fe). M7 was built as recorded in §11.
  - New module `lmd/package/latex/packages/terms.ls`; the bindings `tex/binding/{glossaries,nomencl,acronym}.sty`; the kernel's class name constructors.
  - New test `test/lambda/latex/test_latex_phase5_terms.ls` with its golden and a glossary file, identical on every tier. The other LaTeX, math and TeX tests match their goldens.
  - `make test-lambda-baseline` passed 6792 of 6814 under `LAMBDA_LOG_LEVEL=error`: the 21 warning-reading tests pass without the override, and `test_latex_phase3_corpus` timed out under the baseline's parallel load. Alone it passes in about 37 s, and an interleaved A/B against M6's package tree shows no slowdown once the registry was nested (§11).
- 2026-10-13: M7 committed (86cc5393e). M9 was built as recorded in §13.
  - New modules in `lmd/package/latex/packages/`: `fontspec.ls` and `icons.ls`; the binding `tex/binding/unicode-math.sty`.
  - New test `test/lambda/latex/test_latex_phase5_fonts.ls` with its golden, identical on every tier. `test_latex_font_decl`'s golden and four phase5 expectations now read the family variables, and the groundwork ownership check skips the `"*"` catch-all. The other LaTeX, math and TeX tests match their goldens.
  - `make test-lambda-baseline` passed 6794 of 6815 under `LAMBDA_LOG_LEVEL=error`; the other 21 are the warning-reading tests, which pass without the override.
