# LaTeX Phase IV implementation plan

**Design:** [Lambda_Pkg_Latex3.md §9](../Lambda_Pkg_Latex3.md#9-phase-iv--tex-engine-for-document-local-packages-and-styles-extended-pgfpgfplots-proposed).
**Started:** 2026-10-07 (USER: "proceed to impl in a worktree"), branch `worktree-latex-phase4`.
The §9.8 recommendations are taken as the working choices; each is revisited here if evidence contradicts it.

## Integration shape (M1)

The engine sits in front of the existing direct parser (the digester). Rather than feed the digester a token stream, it hands over a *reconstructed source* plus an offset map:

- A token that reached the output unexpanded, straight from the main file, is copied as its exact source bytes. The source text between two such tokens that are adjacent in the file (whitespace, comments) is copied too.
- A token produced by expansion is written in canonical form. Every byte of it maps back to the main-file offset of the call site.
- Raw constructs (`verbatim`-family environments, `\verb`, `\lstinline`, `\directlua`, `filecontents`, `tikzpicture` islands) are copied as source spans without tokenization.

When nothing is expanded, the reconstruction is byte-identical to the input, so existing documents keep their AST. The digester's `source_offset` maps through the offset map, so AST offsets and diagnostics still point into the main file (**S7.4.1–S7.4.4**). This realizes §9.2's "one digester": the direct parser stays the only producer of the LaTeX Mark AST (**S2.6.3–S2.6.5, D2.6.5v4**).

**Echo rule.** Some definition and counter commands are invoked directly from main-file tokens: the `\newcommand` family, `\newenvironment` family, `\def` family, `\newcounter`/`\setcounter`/`\addtocounter`/`\stepcounter`, and `\newlength`/`\setlength`/`\addtolength`. These are executed *and* their exact source span is echoed, so the script analysis keeps seeing them. Other primitives invoked from main-file tokens are echoed only when they have a visible typesetting effect.

**Constructors.** A control sequence the engine does not define is passed through unchanged. Standard LaTeX document commands are declared as *constructors*: defined to the engine, so `\ifdefined` and `\@ifundefined` are correct, but passed through like undefined ones.

## Milestones

| # | Scope | Status |
|---|---|---|
| M1 | Engine core: catcode tokenizer with provenance, control-sequence table, save stack, registers, macros (delimited/undelimited, `\long`/`\outer`/`\protected`), all expandable primitives and conditionals (TeX + e-TeX), number/dimen/glue scanning with TeX's exact rounding, `\the`/`\meaning`/`\string`, budgets, reconstruction + offset map; native LaTeX layer (command/environment definitions with optional arguments, `\begin`/`\end`, raw captures, counters/lengths, options, `\usepackage`/`\RequirePackage`/`\documentclass` resolution to beside-document files); `parse(..., {type: "tex"})` diagnostic entry; digester integration | done |
| M2 | Conformance corpus: `\immediate\write16` traces compared with `pdftex -ini -etex` from the local TeX Live 2025 for catcodes, expansion, conditionals, grouping, registers, arithmetic | done |
| M3 | Bundled programming layer: `keyval`, `ifthen`, `calc`, `etoolbox`, `kvoptions`, `xkeyval`; expl3 generic loader; format snapshot cache | done; on-disk format cache open |
| M4 | biblatex style files (`.bbx`/`.cbx`/`.lbx`/`.dbx`) on the two-pass model of §9.5 | — |
| M5 | PGF/PGFPlots native extensions (§9.6) and island expansion | six bounded rows done ([Tikz Phase IV](Lambda_Impl_Tikz_Phase4.md)); rest diagnosed |
| M6 | Corpus of local packages, docs (§6 matrix, doc/dev), baselines | — |

## M4 design: biblatex style files on the engine

This realizes §9.5 without moving package policy out of script.

1. **Selection.** When `style=`, `bibstyle=` or `citestyle=` names a style that is not built in, but whose `.bbx`/`.cbx` exists beside the document, the `biblatex` adapter switches to engine formatting for that bibliography context.
2. **Biber's role stays in script.** `bib_data`/`bib_model`/`bib_style` still parse, inherit, sort, label and disambiguate. The adapter writes one *formatting program* per reference section. The program is ordinary TeX: `\lmdbibentry{key}{type}{field=value ...}` records followed by `\lmdbibprint{key}` and `\lmdbibcite{command}{keys}{prenote}{postnote}` requests, with `\lmdbibsep` markers between results.
3. **Formatting layer.** A bundled, Lambda-owned TeX file `lambda-biblatex.sty` implements biblatex's style-author interface on the engine:
   - drivers, bibmacros, and field, list and name formats;
   - `\printfield`, `\printnames`, `\printlist`, `\printtext`, `\printdate`;
   - the punctuation tracker, entry and field tests, `\DeclareCiteCommand`, `\bibstring`/`.lbx` strings, and `\RequireBibliographyStyle`.
   
   Field data come from the entry record.
4. **Built-in base styles.** `\usebibmacro{name}` and `\printfield[format]{field}` without a TeX definition are left as constructors. They reach the digester as elements that `bib_style.ls` renders for the current entry, so `standard`'s bibmacros keep one script implementation.
5. **Result.** The adapter parses the program's reconstructed output with `type: "latex"` and splits it at the markers. Each fragment replaces the built-in rendering of one bibliography item or citation; links and targets stay with the adapter.

Acceptance needs a pinned BibLaTeX/Biber reference run, which requires installing `biblatex` and `biber` into the local TeX Live; ask before downloading.

## Progress log

- 2026-10-07: worktree created from `origin/master` 95e3e4922; plan written.
- 2026-10-07, M1:
  - **Engine.** `lambda/input/input-tex-{engine,scan,prims,main,latex,kernel}.cpp` plus `input-tex.hpp` (API) and `input-tex-internal.hpp`.
  - **Entry points.** `parse(src, {type: "tex", ini, base, packages, raw})` returns `{text, messages, diagnostics, packages}`. `parse(src, {type: "latex", expand: true, ...})` feeds the direct parser, and `lambda.latex.latex.parse_file`/`parse_source` request it.
  - **Parity knob.** `LAMBDA_TEX_EXPAND=1` forces expansion for every LaTeX parse.
  - **Argument mode.** The groups after a passed-through command expand macros but do not execute assignments. Registry-declared `RAW_ARGUMENT_COMMANDS` (bussproofs, `semantic`) get token-for-token arguments.
  - **Echo rule.** Covers the `\newcommand`, `\def`, counter and length families.
  - **Package files.** Spaces from package files are dropped, as in vertical mode.
- 2026-10-07, parity:
  - All 40 `test/lambda/latex` tests pass through the package entry points.
  - 30 of the 31 samples produce byte-identical HTML with and without the engine. `algebra` differs only in whitespace: TeX reads `\` at end of line as a control space and does not keep the newline.
  - With the knob forced on, `test_latex_phase3_boundaries` expects `\course \H o` to keep the space after `\course`. TeX does not (a control word swallows following spaces), so that expectation changes once expansion is the default.
- 2026-10-07, M2: `test/lambda/latex/test_tex_engine_conformance.ls` compares 121 `\immediate\write16` records over the expansion, conditional, register/arithmetic and e-TeX fixtures with `pdftex -ini -etex` (TeX Live 2025). Every record matches; `test/latex/fixtures/tex_engine/make_reference.py` regenerates the expectations.
- 2026-10-07, M3 (bundled packages):
  - **Files.** `lmd/package/latex/tex/` holds 18 unmodified LPPL files from TeX Live 2025. `VENDOR.md` records version and hash for each. Resolution order is adapter, then bundled, then beside-document file.
  - **Kernel.** The Lambda-owned kernel (`input-tex-kernel.cpp`) gained the LaTeX internals these packages assume:
    - plain-style register allocation (`\alloc@`, `\count10`–`\count19`), so `etex.sty` can extend it;
    - `\@star@or@long`/`\new@command`/`\@argdef` with a native `\@yargdef`;
    - `\@ifdefinable` that runs its body after its conditionals close;
    - `\@whilenum`/`\@whiledim`/`\@whilesw`;
    - package bookkeeping (`\ver@`, `\opt@`, `\@filelist`, `\@addtofilelist`, `\filename@parse`);
    - `\PackageWarning` family messages with `\on@line`.
  - **Diagnostics.** `tex-incomplete-if` now names the file and line where each open conditional began.
  - **Tests.**
    - `test_latex_phase4_bundled.ls`: a beside-document package that uses kvoptions, etoolbox, ifthen, keyval, xkeyval and calc. Every value matches pdflatex, including `2cm+1cm` = `85.35825pt`.
    - `test_latex_phase4_local_packages.ls`: a beside-document class and package.
  - **Digester.** `\par` now ends a paragraph exactly like a blank line, as TeX tokenizes it. Package environments such as `\newenvironment{remark}{\par...}{\par}` emit it. Before, the renderer reported `par` as unsupported, with or without the engine.
  - **Results.**
    - All 43 `test/lambda/latex` tests pass.
    - HTML parity is unchanged: 30 of 31 samples are byte-identical, and `algebra` differs only in whitespace, now with the real `calc.sty` loaded.
- 2026-10-07, M3 (expl3 and the format):
  - **Format.** `input-tex-format.cpp` is TeX's `\dump` and undump. It covers:
    - names and meanings, with macros shared through `\let` stored once;
    - the registers, boxes included;
    - parameters, code tables and fonts;
    - the LaTeX-layer lists.

    The preload mirrors latex.ltx (ltexpl): the kernel predefines the `\@expl@...@@` file hooks, and the format reads `\input expl3.ltx` with `@` a letter. The process builds the expl3 format once, on first use (mutex-guarded, 4.9 MB image). Stored tokens drop their provenance, so they reconstruct canonically.
  - **Restart rule.** LaTeX preloads expl3; here, a request for `expl3` stops the run, and the driver reruns the document once from the expl3 format. The request can come from `\usepackage`/`\RequirePackage`, or from the kernel stubs `\ExplSyntaxOn` and `\ProvidesExpl{Package,Class,File}`.
  - **File hooks.** The native package loader now runs LaTeX's own token sequences:
    - at the start of a file, `\@pushfilename\xdef\@currname{name}`;
    - at its end, the `\AtEndOfPackage` hook, then `\@popfilename`, then the next file (`\@onefilewithoptions` order).

    expl3's syntax save and restore depends on both.
  - **Engine fidelity fixes found by running expl3-code.tex:**
    - `\fontdimen` grows the last-loaded font (one font-memory array, TeX §§578-580), which expl3's intarray/fparray rely on;
    - `\ifeof` is real;
    - `\read` runs its own line reader (multi-line until braces balance; an unmatched `}` ends the record; the line takes `\endlinechar` under current catcodes) and no longer swallows the caller's next token or fires `\everyeof`;
    - `\readline` appends `\endlinechar`;
    - file names drop `"` (TeX Live quoting), and `\input` shares `scan_file_name`;
    - `\pdffilesize`/`\pdffilemoddate`/`\pdfmdfivesum [file]`/`\pdfescapestring`/`\pdfescapename`/`\pdfunescapehex` are exact. File queries see only what `\input` could read (D7.5.2); anything else reads as missing.
    - Macro calls pop exhausted input levels before pushing (TeX §390). This keeps tail recursion flat.
    - Allocation now follows plain (`\count19` languages, `\insc@unt` = `\count20`).
  - **Memory.**
    - Expansion buffers and backed-up tokens are pooled and recycled when their input level pops.
    - Transient texts (`\unexpanded`, `\detokenize`, `\pdfstrcmp`, `\message`, file names...) scan into scope-owned lists (`scan_toks_into`).
    - One token list is capped at 4M tokens (`tex-capacity`), as TeX's main memory is.

    Loading expl3 took 4.8 GB before this work and takes about 0.5 GB now, once per process. Old macro bodies are not yet reference-counted; the format dump drops them.
  - **Cost.** Building the format takes about 6 s (pdfTeX needs about 3.8 s for the same generic load). After that, an expl3 document costs about 0.25 s.
  - **Bundled.** `expl3.ltx`, `expl3.sty` and `expl3-code.tex` (l3kernel 2025-01-18), plus four Unicode 16.0.0 data files (Unicode License v3), are recorded in `VENDOR.md` (bundle 3.8 MB).
  - **Tests.**
    - `test_latex_phase4_expl3.ls`: a beside-document package in expl3 syntax with `\NewDocumentCommand`, tl/seq/int/fp and `\text_uppercase:n`. Its text matches pdflatex.
    - A `pdfstrings` conformance fixture: 6 fixtures, all identical to pdfTeX.
    - All 44 LaTeX tests pass. HTML parity is unchanged.
  - **Open.**
    - An on-disk format cache, so each process skips the 6 s build.
    - Reference counting for macro bodies.
    - The l3backend: `\@expl@sys@load@backend@@` is a stub, because the native loader has no `\@onefilewithoptions`.
- 2026-10-07, M5 (PGF/PGFPlots; details in [Lambda_Impl_Tikz_Phase4.md](Lambda_Impl_Tikz_Phase4.md) and `vibe/Lambda_Pkg_Tikz.md` §12):
  - **Implemented** in `lmd/package/doc/tikz/` and `input-tikz.cpp`:
    - `positioning` and `calc` coordinates;
    - `arrows.meta` tips and `shapes.geometric` nodes;
    - parameterized styles and `pic`;
    - PGFPlots bars, stacked bars, error bars, `\closedcycle` areas and `fill between`;
    - inline and file tables, and `symbolic coords`;
    - `groupplots`.
  - **Checked against TeX:** 49 positioning and anchor positions match a pinned TeX Live 2025 + PGF 3.1.10 run within 0.002 cm. PGFPlots geometry has no local reference yet (pgfplots is not installed).
  - **Integration on this branch:**
    - `render.ls` passes `base_uri` to `tikz.render_picture`, so file tables resolve beside the document. `test_latex_phase4_pgfplots_table.ls` checks this.
    - The registry accepts the `calc` and `shapes.geometric` libraries.
  - **Results:** all 51 LaTeX tests pass.
  - **Found on the way:** a runtime defect. `++` flattens arrays of packed float arrays, and spreading them yields nulls. It is filed as a separate task; the plot code avoids it.
