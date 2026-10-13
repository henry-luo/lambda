# TeX expansion engine for `lambda.latex`

**Status:** Implemented on master since 2026-10-07 (commit `6cdcd8f1f`; math-mode entry added 2026-10-10 in `16060078e`). Consolidated on 2026-10-12 and checked against HEAD `1df04d4e4`. The engine boundary, the **S1.8** reading in §2.4, the file gate in §8, the resolution order in §5 and the single front end in §6 were ratified as **D7.2.7** on 2026-10-12, and **D7.2.7v2** admitted Lambda-owned bindings among the bundled packages on 2026-10-13; this document keeps the argument. Items the code does not yet meet are marked *not built* and collected in §10.

**Scope:** The TeX tokenizer, expansion and state machine that runs in front of the LaTeX digester, the Lambda-owned LaTeX2e programming kernel, the bundled packages (upstream programming packages and Lambda-owned bindings), and package/class resolution for files beside the document. The overall LaTeX package design is [Lambda_Pkg_Latex.md](../Lambda_Pkg_Latex.md). Bibliography processing is [Lambda_Design_BibTeX_CSL.md](Lambda_Design_BibTeX_CSL.md). PGF/PGFPlots drawing is [Lambda_Pkg_Tikz.md](../Lambda_Pkg_Tikz.md). The digester's AST contract is [Input_Latex.md](../input/Input_Latex.md).

**Formal linkage:** **D7.2.7v2** (this design's ruling), **S1.8** (no runtime string becomes Lambda code), **S12.1.1v2 / S12.4.1** (pure transform, eager input value), **S7.4.1–S7.4.4** (located diagnostics), **S2.6.3–S2.6.5, D2.6.5v4** (command-argument content contract), **D7.1.1** (lower layer owns hooks with null-safe defaults), **D7.1.2v2 / D7.5.2** (resource acquisition, path gate), **D7.2.1–D7.2.4** (script packages and the static adapter registry), CLAUDE.md rule 16 (vendored files are unmodified).

**Implementation record:** [Lambda_Impl_Latex_Phase4.md](../impl/Lambda_Impl_Latex_Phase4.md).

## 1. Purpose and boundary

The engine lets real TeX *programming* run while Lambda keeps owning *typesetting*. Document-local classes and packages, and the programming packages they depend on (`etoolbox`, `xkeyval`, `kvoptions`, expl3 and so on), execute as TeX: catcodes, macros with delimited parameters, registers, grouping, conditionals and the e-TeX extensions. What they produce is handed to the existing digester and script adapters, which still decide what every document construct means in HTML.

The engine stops before typesetting. TeX's paragraph builder, page builder, `\output` routine and font machinery are not reproduced. Radiant owns line breaking and pagination, and a second layout engine would contradict it and produce boxes with no semantics: a `\section` executed through upstream `latex.ltx` becomes a vertical skip, a font switch and a penalty rather than a heading.

**Why this boundary.** Two pure alternatives fail. Reimplementing every package as a script adapter never reaches ordinary journal classes, thesis templates or macro packages that use kernel internals, `\@ifnextchar`, delimited parameters or expl3. A full TeX implementation with typesetting duplicates Radiant and loses the semantic HTML the package exists to produce; tex4ht and lwarp show how fragile "typeset, then convert" is. The most complete LaTeX-to-HTML converter, LaTeXML, has the shape chosen here: a faithful reimplementation of TeX expansion plus per-package semantic bindings.

**LaTeXML lineage.** LaTeXML's Mouth (catcode tokenizer) and Gullet (expansion) correspond to this engine. Its Stomach (digestion) corresponds to the single direct-parser digester. Its `DefConstructor` bindings correspond to script adapters selected from a static registry, where a package name is data and never a dynamic import path.

## 2. Architecture: expansion before digestion

```text
document + .tex/.sty/.cls files beside it        bundled programming packages
        |   (read-only, path-gated)                       |
        v                                                 v
TeX engine: catcode tokenizer -> expansion -> state (registers, groups, save stack)
        |        ^ Lambda-owned kernel, cached format     ^ adapter names + raw commands (registry)
        v
reconstructed source + offset map
        |
DirectLatexParser (the only digester) -> LaTeX Mark AST (S2.6.3-S2.6.5, D2.6.5v4)
        |
script analysis -> render -> render_result
```

### 2.1 One digester, reconstructed source

The engine does not hand the parser a token stream. It writes a *reconstructed source* plus an offset map:

- A token that reaches the output straight from the main file is copied as its exact bytes, together with the source text between it and the previous such token (whitespace, comments).
- A token produced by expansion is written in canonical form, and every byte of it maps back to the call site.
- Raw constructs are copied as source spans without tokenization: the raw-text environments (the `verbatim`/`fancyvrb` family, `lstlisting`, `minted`, `alltt`, `comment`, `filecontents`, `luacode`), `\verb`, `\lstinline`, `\directlua` and `tikzpicture`. Math is not raw: the engine expands it like any other text, so document macros reach formulas.

Where nothing expands, the reconstruction is byte-identical to the input, so existing documents keep their AST. The digester's `source_offset` maps through the offset map (`result_map_offset` in `input-tex.hpp`), so AST offsets and diagnostics still point into the main file. There is no second front end (CLAUDE.md rule 13).

### 2.2 Constructors and the echo rule

A control sequence the engine does not define passes through unchanged. Standard LaTeX document commands (sectioning, lists, floats, tables, references) are declared to the engine as *constructors* through the kernel's `\lambdaconstructor`: they are defined, so `\ifdefined` and `\@ifundefined` answer correctly, but they pass through like undefined commands and reach the script as AST elements.

Some definition commands are executed *and* echoed into the reconstructed text, because script adapters read them as data: the `\newcommand`, `\newenvironment` and `\def` families, `\NewDocumentCommand`, the counter commands and the length commands (`echo_definition` in `input-tex-prims.cpp`). `semantic` and `bussproofs` depend on this. Definitions inside beside-document packages are not echoed; their effects reach the script through expansion. Definitions in math mode are not echoed. A package may redefine one of these commands; `calc` turns `\setlength`, `\addtolength`, `\setcounter` and `\addtocounter` into macros that accept expressions. A document call of such a redefined length or counter command is still echoed verbatim, and the package's definition then runs on the same arguments, so the script keeps seeing the call and the expression semantics still apply. Inside a passed-through command's arguments (§2.3) the call passes through verbatim like the kernel version instead of expanding into assignment code that argument mode never runs (`latex_redefined_echo` in `input-tex-latex.cpp`).

### 2.3 Arguments: signatures decide

A TeX macro does not declare how many arguments it takes, so the groups right after a passed-through constructor are treated as that constructor's arguments, which LaTeX's own command would read as data. By default they are processed in *argument mode*: macros expand, as `\protected@edef` would, but registers, primitives and assignments pass through untouched. That keeps `\includegraphics[width=0.8\linewidth]` intact (`pass_constructor_args`).

A script adapter whose CTAN package reads its arguments as tokens declares them *raw* in the registry's `RAW_ARGUMENT_COMMANDS`: the bussproofs inference commands and `semantic`'s `\inference`. Raw arguments reach the adapter token for token, including bussproofs' `$`-delimited forms. Package semantics stay in script (**D7.2.1–D7.2.4**).

### 2.4 Purity and the S1.8 reading

The engine runs at the input boundary inside `parse(...)` and yields an eager value (**S12.4.1**). Given the same source and resources, the output is always the same (**S12.1.1v2**). `\time`, `\day`, `\month`, `\year` are fixed to 2000-01-01 12:00 and the random seed is fixed, so nothing reads the clock.

**S1.8** comes from the ruling that a runtime string never becomes *Lambda* code. The engine reads TeX source as TeX inside a sandbox. That text never becomes a Lambda import, Lambda source or `compile()` input, the same reading under which Radiant runs a document's `<script>` JavaScript through LambdaJS. The engine can do nothing beyond reading path-gated local files and returning text. `\directlua` stays unsupported: it would need a second language runtime.

## 3. Engine profile

| Layer | Contract | Status |
|---|---|---|
| Input and catcodes | UTF-8 input with all 16 catcodes; `\catcode`, `\lccode`, `\uccode`, `\sfcode`, `\mathcode`; `\endlinechar`; `^^` notation; active characters; `\makeatletter`; `\scantokens`. Non-UTF-8 `inputenc` encodings are diagnosed. | Built |
| Expansion | Delimited and undelimited parameters; `\expandafter`, `\noexpand`, `\csname`, `\edef`/`\xdef`, `\protected`, `\unexpanded`, `\detokenize`, `\expanded`; all TeX and e-TeX conditionals; `\futurelet`, `\afterassignment`, `\aftergroup`; `\numexpr`/`\dimexpr`/`\glueexpr`/`\muexpr`; `\pdfstrcmp`, `\pdfmdfivesum`, `\pdfuniformdeviate`. Only the primitives the kernel, the bundled packages and expl3 need; other engine extensions are undefined and diagnosed when used. | Built |
| State | Count, dimen, skip, muskip and toks registers in their e-TeX ranges; `\global`; the save stack; `\let`; the `\chardef` family; arithmetic with TeX's exact rounding. | Built |
| Files | `\input`, `\include`, `\InputIfFileExists`, `\IfFileExists`, `\openin`/`\read`/`\readline` for files beside the document. `\openout`/`\write` go to in-memory streams (`.aux`, `.toc`, `.lof`) that a following pass reads. Never `\write18`, absolute paths, parent-directory escapes or disk writes. | Reads built. Write streams *not built*: written text is discarded and there is one pass. |
| Boxes | `\setbox`, `\hbox`, `\vbox`, `\wd`/`\ht`/`\dp`, `\settowidth` and `\ifdim` tests use dimensions from a layout-metrics hook owned by the lower layer with a null-safe default (**D7.1.1**). Radiant supplies measured metrics; without it, bundled font metrics give approximate values plus an `approximate-box-metrics` diagnostic. Code that inspects built lists (`\lastbox`, `\unskip` on built lists, `\showbox`) is diagnosed. `\halign` is diagnosed. | Box primitives built. Metrics hook *not built*: widths are estimates (characters × half a quad), and `\lastbox` silently returns a void box. |
| Kernel | A Lambda-owned LaTeX2e programming layer plus expl3, preloaded into a cached format (§4). Document-level commands stay constructors. | Built; format cached in memory only |

## 4. Kernel and bundled packages

**Clean-room kernel.** `input-tex-kernel.cpp` and `input-tex-latex.cpp` implement the LaTeX2e programming layer natively rather than loading upstream `latex.ltx`, which would turn document commands into TeX typesetting. It covers the `\newcommand` family with optional arguments, `\newenvironment`, `\begin`/`\end`, `\DeclareOption`/`\ProcessOptions`, `\RequirePackage`, `\@ifnextchar`, `\@ifundefined`, `\@ifdefinable`, `\@whilenum`/`\@whiledim`/`\@whilesw`, `\AtBeginDocument` and related hooks, plain-style register allocation (`\alloc@`, `\count10`–`\count19`) so `etex.sty` can extend it, package bookkeeping (`\ver@`, `\opt@`, `\@filelist`, `\filename@parse`), the `\PackageWarning` family, and a native `\NewDocumentCommand` whose argument processors and embellishments are diagnosed.

**Bundled packages.** `lmd/package/latex/tex/` ships unmodified LPPL files from TeX Live 2025, with version and hash recorded in its `VENDOR.md`: `keyval`, `kvoptions`, `kvsetkeys`, `kvdefinekeys`, `xkeyval`, `etoolbox`, `ifthen`, `calc`, `etex`, `etexcmds`, `iftex`, `infwarerr`, `ltxcmds`, `pdftexcmds` and the expl3 generic loader (l3kernel 2025-01-18), plus the Unicode 16.0.0 data files expl3 reads. They are engine resources shipped in the package tree, not lookups in a TeX installation, and they fall under the vendor rule (CLAUDE.md rule 16). The criterion for adding one is that it is a programming package: it emits no typeset output of its own.

**Bindings.** `lmd/package/latex/tex/binding/` holds Lambda-owned `.sty` files, which the engine finds after the upstream files (`bundled_path` in `input-tex-latex.cpp`). A binding expands a package's user commands into constructs the digester and the script adapters already own, such as `\left…\right`, `\frac`, `\boldsymbol` and the matrix environments; it never typesets and carries no semantics of its own (**D7.2.7v2**). It is written from the package's manual, not copied, so the vendor rule does not apply. Each binding is also a registry row, so the declaration still reaches the script, which validates options and owns whatever is not macro expansion.
- **Expandable at use time.** A constructor's arguments are expanded without executing assignments (§2.3), so a binding's commands must expand with no `\def` or register assignment, or they would break inside `\section`, `\caption` and `\footnote`. Assignments are fine at declaration time, in the preamble. The exception is a command that builds a block from a file and so belongs in body text: `\csvreader` opens, reads and sets keys, which are assignments, and is documented as not working inside a constructor's arguments.
- **Built:** `physics.sty` (physics 1.3: bracing, vectors, operators, quick-quad text, derivatives, Dirac notation and matrix macros, with its three options) and `mathtools.sty` (the macro layer of mathtools 1.31: paired delimiters with `\delimsize`, `\prescript`, `\splitfrac`, `\adjustlimits`, `\MoveEqLeft`). A command whose name math already has, such as `\sin` under physics, reaches the plain meaning through `\noexpand`, because the engine passes an undefined math command through unchanged. `xspace.sty` (xspace 1.13) defines `\xspace` as `\lambda@xspace`. `csvsimple.sty` (the csvsimple-l3 interface, in expl3) reads a CSV file beside the document with `\ior_get` and puts back, line by line, the line's macro assignments and the user's command list inside the table its keys build, so document macros in the command list expand as in LaTeX.
- **Engine primitives for bindings.** A binding that needs to look ahead cannot use `\futurelet`, which is an assignment. `\lambda@xspace` is an expandable engine primitive instead: it reads the next token, puts it back, and puts a space token before it unless the token is in xspace's exception list (`,.'/?;:!~-)`, a group delimiter, a space, the active `~`, `\/`, `\ `, `\footnote`, `\footnotemark`, `\bgroup`, `\egroup`). The `lambda@` prefix keeps these primitives out of the document's namespace. Such a primitive is expansion, not typesetting: it only chooses which tokens follow.

**Format.** Loading expl3 is expensive, so the engine builds an expl3 format image once per process and restarts a run from it when a document requests expl3. The image is held in memory; an on-disk cache keyed by the engine and kernel versions (**D7.2.3**) is *not built*.

## 5. Package and class resolution

`\documentclass`, `\LoadClass`, `\usepackage` and `\RequirePackage` resolve each name once, in source order. A duplicate load with the same options is idempotent. The engine receives the adapter names from the script registry (`registry.adapter_names()`), so the static registry stays the single list of script adapters (**D7.2.4**).

The order for packages, ruled in **D7.2.7v2** (`do_usepackage` in `input-tex-latex.cpp`):

1. **Bundled package.** Its `.sty` runs on the engine, whether it is an upstream programming package or a Lambda-owned binding (§4). If the name is also a script adapter (`ifthen`), the declaration additionally passes through, so the adapter still sees it.
2. **Script adapter.** The declaration passes through to the script registry, which validates options and activates the adapter.
3. **A `name.sty` beside the document**, run by the engine, considered only when the name is not an adapter.
4. **Otherwise** the declaration passes through as unknown, and the script registry reports the located `unknown-package` diagnostic.

Classes follow the same rule without the bundled step: a `name.cls` beside the document runs when the name is not a built-in class profile.

**Shadowing.** A beside-document file with an adapter's name does not replace the adapter, because the adapter owns the HTML semantics that the file's TeX typesetting code cannot provide. The design calls for a `local-package-shadowed` diagnostic in that case; it is *not built*, and the file is skipped silently today.

**One owner per control sequence.** A TeX definition in the document or a local package overrides an adapter's constructor from that point in source order, as it would in LaTeX. Patching adapter internals, such as `\patchcmd` on an adapter-owned command or `\let` to a kernel internal the profile does not define, fails the way a failed LaTeX patch does and produces a located diagnostic.

## 6. Entry points and front-end selection

Expansion is the only front end (**D7.2.7v2**): `parse_latex_direct` always runs the engine, and there is no unexpanded parse to fall back to. Only an allocation failure stops the engine, and that yields an error rather than a raw parse.

| Entry | Behavior |
|---|---|
| `parse(src, {type: "tex", ini, base, packages, raw})` | Runs the engine alone and returns `{text, messages, diagnostics, packages}`. Used for diagnostics and the conformance corpus. |
| `parse(src, {type: "latex", base, packages, raw})` and `input(path, "latex")` | Run the engine, then the digester on the reconstructed source. The root carries `tex_diagnostics` and `tex_packages`, which the script reports as `tex` package diagnostics. A file input resolves beside-document files from its own location when no `base` is given. |
| `lambda.latex.latex.parse_source`/`parse_file` | Pass the registry's adapter names and raw commands. Every `render_file*`/`render_string*` entry, the native document transform (`render_document_file`, used by `lambda convert` and Radiant's LaTeX loader) and the doc viewer parse this way. |
| Math: `parse(src, {type: "math"})`, Markdown math | `parse_math_direct_to_ast` runs the engine in math mode when the formula contains a definition command (`\def`, `\gdef`, `\edef`, `\xdef`, `\let`, the `\newcommand` family, the `*DocumentCommand` family). Other formulas have nothing to expand and use the recovery parser directly. Math inside a LaTeX document is expanded with the document. |

**Adapter knowledge comes from the caller.** The input layer cannot consult the script registry without calling upward (**D7.1.1**), so the adapter names and raw commands travel as parse options. A bare `input(path, "latex")` still expands, but without them: a local file that shares an adapter's name runs, and adapter commands such as bussproofs' inference rules get argument-mode rather than raw arguments. Callers that render with `lambda.latex` therefore parse through it.

**What retired with the raw front end.** The `expand` parse option and the `LAMBDA_TEX_EXPAND` parity knob are gone. So is the script's macro substitution in `render_generic`, which reimplemented a weak subset of TeX (no `\def`, delimited parameters, grouping or `\let`) by splicing raw arguments into definitions and re-parsing. After expansion, an element that reaches the renderer is a command the engine passed through on purpose: a theorem environment, an adapter command, or an unsupported command that the fallback reports. The definition table in `macros.ls` remains only as data for its readers: TikZ islands until island expansion lands (§7), bussproofs labels, and `\familydefault`. TeX's control-word rule now applies everywhere: `\course \H o` drops the space after `\course`, as TeX does.

## 7. Islands and drawing

Raw islands (`tikzpicture` and the raw-text environments) are copied as source spans. PGF/TikZ sources never run on the engine; drawing belongs to the native `lambda.doc.tikz` profile in [Lambda_Pkg_Tikz.md](../Lambda_Pkg_Tikz.md).

**Island expansion** is designed but *not built*. For `tikzpicture`, `axis` and `\tikz` bodies the engine would expand only macros defined by the document or a local package, pass every other control sequence through to the Lambda-owned TikZ parser, and record `\tikzset`, `\pgfplotsset`, `\usetikzlibrary` and `\usepgfplotslibrary` from local packages in source order. A local package that programs PGF internals (`\pgfdeclareshape`, `\pgfdeclaredecoration`, `.code` handlers in `\pgfkeys`) would be diagnosed. Today `tikzpicture` is copied raw, and `tikz_bridge.ls` expands only macros visible in the document AST, so macros from local packages do not reach pictures.

## 8. Safety and limits

- **Budgets.** Expansion steps (20M per run, 400M for the format build), input-stack depth (6,000), token-list size (4M tokens, `tex-capacity`), and local files (128 files, 16 MB). Register-use and pass budgets apply once multi-pass runs exist.
- **Errors become diagnostics.** Budget exhaustion, endless expansion, undefined control sequences, runaway arguments and missing braces become located diagnostics with a stable `tex-*`/`latex-*` code, file and line, and the engine recovers the way TeX's non-stop mode does, so the rest of the document still renders (**S7.4.1–S7.4.4**). An open conditional at end of file names the file and line where it began. A bounded expansion trace for text produced by a macro is *not built*.
- **File access.** Reads are rooted at the document's directory; absolute paths and `..` escapes are rejected with `tex-file-outside` (**D7.5.2**). Nothing comes from the network, a TeX installation or the shell. Reads currently use a direct text-file read rather than the central IO door; moving them behind it is outstanding.
- **Caches.** Format snapshots, and any future cache of expanded results, are derived caches keyed by source and resource digests, engine and kernel versions, and options (**D7.2.3, D7.1.2v2**).

## 9. Verification

- **Conformance corpus.** `test_tex_engine_conformance.ls` compares 151 `\immediate\write16` records over six fixtures in `test/latex/fixtures/tex_engine/` (expansion, conditionals, registers and arithmetic, e-TeX, tokens, PDF strings) with `pdftex -ini -etex` from TeX Live 2025; `make_reference.py` regenerates the expectations. This is the engine's own gate in the sense of **D7.3.5**.
- **Packages.** `test_latex_phase4_bundled` loads a local package that uses kvoptions, etoolbox, ifthen, keyval, xkeyval and calc, with values matching pdflatex. `test_latex_phase4_expl3` and `test_latex_phase4_local_packages` cover expl3 and a beside-document class and package.
- **Parity.** With expansion on, the Phase III corpus test passes, and 30 of the 31 samples produce HTML byte-identical to the raw path; `algebra` differs only in whitespace.
- **Coverage.** Every `test/lambda/latex` script now parses through the engine. Scripts that use a bare `input(..., "latex")` run without the adapter registry (§6).

## 10. Outstanding work

**Engineering gaps against this design**

- Layout-metrics hook for box dimensions; diagnose `\lastbox` and other built-list inspection.
- In-memory write streams and multi-pass runs for `.aux`/`.toc` cross-references.
- `local-package-shadowed` diagnostic.
- Bounded expansion trace on diagnostics.
- Reads through the central IO door.
- The 2026-10-07 design takes the fixed date and random seed from options; the code fixes them as constants. Determinism (**S12.1.1v2**) holds either way.
- On-disk format cache; the l3backend loader is still a stub.
- Island expansion (§7).

**Direction for broader package coverage**

- **Widen the bundled set** with further programming packages that emit no output, such as `xstring`, `environ`, `trimspaces`, `letltxmacro`, `xpatch`, `refcount`, `xfp` and `lastpage`'s programming layer. This is vendor and test work.
- **Expose kernel internals as constructors.** Journal classes redefine `\section`, `\maketitle`, floats and captions through `\@startsection`, `\@maketitle`, `\@float`, `\@makecaption`, `\list`/`\@item` and the tabular and bibliography internals. If the kernel exposes those internals as constructors that map to semantic elements, a class that redefines through them still yields headings and figures. This is the LaTeXML binding model and the largest lever for real-world classes.
- **More thin TeX bindings.** Where a package's user macros are option handling and argument parsing over a few output points, write the binding as a small `.sty` on the engine (§4) instead of reimplementing the macros in script. The script adapter keeps the HTML semantics, and user `\renewcommand` of package commands works unchanged. physics, mathtools, xspace and csvsimple were the first; `siunitx`, whose number parser is expl3, is the next candidate.
- **Corpus.** Build a corpus of real classes and packages kept beside their documents, with provenance and licenses: journal and thesis templates and macro packages that use delimited parameters, option processing, `etoolbox` hooks and expl3. Compare semantic HTML and selected visible text with pinned TeX output.

## Appendix A. Brief history

| Date | Step |
|---|---|
| 2026-01 | Early plans for the C++ `lambda/tex/` pipeline rejected a full TeX implementation: the macro layer is Turing-complete, catcode changes cannot be expressed in a static grammar, and CTAN is too broad. A LaTeXML-style tokenizer/expansion/digestion design in C++ was sketched but not built; that pipeline was removed on 2026-03-04. |
| 2026-10-07 | The user set the Phase IV scope: a full TeX engine for `.sty`/`.cls` and biblatex style files, files only from beside the document, and PGF/PGFPlots growth in native script rather than by running PGF. M1 (engine and digester integration), M2 (pdfTeX conformance) and M3 (bundled packages and expl3 through a cached format) landed the same day, then merged to master. |
| 2026-10-10 | Math-mode expansion for formulas that contain definitions. The CSL design recommended deferring M4. |
| 2026-10-12 | This design was extracted from `Lambda_Pkg_Latex3.md` §9 and checked against the code. |
| 2026-10-12 | The user dropped M4 (biblatex style files on the engine) in favor of mapping guide-level biblatex styles to CSL. |
| 2026-10-12 | Expansion became the only front end: the raw parse branch, the `expand` option, the `LAMBDA_TEX_EXPAND` knob and the script macro substitution were removed, and the native document transform parses through `lambda.latex`. The boundary, the S1.8 reading, the file gate, the resolution order and the single front end were ratified as **D7.2.7**, confirming the implemented resolution order. |
| 2026-10-13 | **D7.2.7v2**: the user approved Lambda-owned bindings as bundled packages (Phase V plan §17.1). The engine searches `binding/` after the upstream files; physics and the mathtools macro layer are the first bindings. |
| 2026-10-13 | The xspace binding and its `\lambda@xspace` lookahead primitive (Phase V M3). |
| 2026-10-13 | The csvsimple binding, the first to read a file and the first written in expl3 (Phase V M5). |

## Appendix B. Superseded rulings

- ~~Only programming packages, which emit no document material, are bundled.~~ Replaced 2026-10-13 by **D7.2.7v2**: Lambda-owned bindings that only expand user commands into constructs the digester and adapters own are bundled too (§4).
- ~~Package resolution consults a script adapter first, then a bundled programming package, then a file beside the document.~~ Replaced 2026-10-12 by the implemented order ratified in **D7.2.7**: a bundled programming package runs first, and an adapter of the same name still receives the declaration (§5).
- ~~An unexpanded parse remains for callers that do not request expansion, and `LAMBDA_TEX_EXPAND=1` forces expansion for parity runs.~~ Replaced 2026-10-12 by **D7.2.7**: expansion is the only front end (§6).
- ~~`\usepackage` text never becomes a native `.sty` loader; package selection is a static script registry only.~~ Replaced 2026-10-07: `.sty`/`.cls` files beside the document and the bundled programming packages run on this engine. The static adapter registry and the rule that no package text becomes a Lambda import remain (§2.4, §5).
- ~~The engine takes biblatex's formatting role for `.bbx`/`.cbx`/`.lbx`/`.dbx` files beside the document (M4): Lambda keeps Biber's role and writes a `.bbl`-equivalent formatting program, and a Lambda-owned `lambda-biblatex.sty` implements the style-author interface.~~ Dropped by the user on 2026-10-12. It would have reimplemented biblatex's formatting layer for the few documents that keep style files beside them. Guide-level biblatex styles map to CSL styles instead ([Lambda_Design_BibTeX_CSL.md](Lambda_Design_BibTeX_CSL.md) §7.4), and biblatex style files are never executed.
- ~~A complete TeX engine is deferred (TikZ proposal §10).~~ Replaced 2026-10-07 for expansion only; running PGF sources on the engine remains rejected (§7).
