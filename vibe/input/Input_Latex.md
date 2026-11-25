# LaTeX input: migration to Lambda Script

## 2026-11-11: requested full script port

On 2026-11-25 the user directed retirement before fixing remaining Script test
failures. D10.2.2v2 assigns TeX and LaTeX parsing to the Lambda Script engine
in the runtime; native clients migrate to the runtime document parser, and Input
does not import the runtime (D9.1.3v2). The migration remains incomplete:
standalone Input dispatch, embedded math, resource acquisition, and the native
engine build and tests still require migration.

Local runtime `input()` and `parse()` now use Input's existing format detection
to hand detected TeX or LaTeX source back to the runtime for Script parsing.
The handoff retains the acquired bytes and security context, and Input does not
call or import the runtime (S1.8, D9.1.3v2, D10.2.2v2). Explicit archive member
parses use the same handoff without external resource grants (S12.4.1v2,
D11.1.2). Native standalone, lazy member-content, and HTTP entry points still
need migration.

The Lambda-owned [script TeX engine implementation plan](../impl/Lambda_Impl_Latex_Script_Engine.md) tracks package-specific progress and issues. General Lambda language and runtime issues remain in the Lambda issue ledger.

The requested direction supersedes the partial expl3 port below: move the TeX
engine and LaTeX document processing to Lambda Script, with no C++ TeX/LaTeX
implementation on the migrated path. The reason is to keep execution and
transformation in one language and avoid a native-to-script call boundary.

At the original request, `parse(..., {type: "latex"})` and `parse(..., {type: "tex"})`
ran the input-owned C++ engine directly. The LaTeX entry also used
the C++ `DirectLatexParser` to build the Mark AST. A package-only replacement
would leave these public entry points and native input clients on the old path.
The migration must specify their replacement or migration, AST and source-map
compatibility, diagnostics, native callers, resource access and lifetime rules.
The user's direction calls for revising D10.2.2's input-boundary wording through
the formal design process; TeX source must remain data, as required by S1.8.
The revised design also needs to state how native clients obtain LaTeX parsing
without making input import the Lambda runtime (D9.1.3v2).

The baseline includes the six TeX engine conformance fixtures, the bundled
expl3 and Unicode resources, package fixtures, and LaTeX parsing and rendering
tests. Preserve their results. An incremental implementation must not route
ordinary documents to a partial engine or silently change the meaning of input.
Measure runtime and memory in a release build once an end-to-end path exists.

The script package, runtime entry points, formal documents and their tests are
Lambda-owned; the input provider and its callers are input-owned. Preserve the
current boundary under **D9.1.1v4 / D9.1.3v2** until the requested migration is
specified and ratified.

The script port now contains an incremental reader, token input levels and an
experimental macro expander under `lmd/package/doc/latex/tex_engine/`. It handles
macro definitions and aliases, delimited arguments, scoped definitions and
registers, count, dimension and glue arithmetic, scaled-point unit conversion,
explicit groups and after-group tokens, category codes and code tables, selected
conditionals and expansion primitives, integer expressions, math-mode scopes,
math hooks and mode conditionals, and nested input via `\scantokens` and
`\everyeof`.
The reader, stream and expansion golden tests pass in both interpreter and JIT
modes. Its output matches all 151 reference messages in the six existing TeX
engine fixtures, with an explicitly empty local snapshot for the PDF-string fixture.
Script MD5 matches standard vectors and multi-block padding cases; the font
model follows the existing engine's simplified metrics and parameter behavior.
Empty box queries are supported; box construction is not yet implemented.
Additional dimension rounding and unit cases match pdfTeX; this does not
establish complete TeX conformance.

The experimental engine can answer PDF file queries and expand `\input` from
explicit snapshots of caller-authorized local and bundled resources. It keeps
those scopes separate, tracks captured source IDs and byte offsets, and handles
nested input, `\endinput`, and conditional file probes and inclusion. The engine
opens no paths. The runtime facade requests missing snapshot entries and captures
them through the caller's authority (D9.1.2v2, D11.1.2); direct snapshot callers
still receive an explicit unsupported result when a scope is absent. Names reject
absolute paths and `..`, and local
captures count against the native 128-file and 16 MiB limits. Included source
bytes currently must be valid UTF-8. The runtime searches the bundled root and
its binding directory. Synthetic source mapping and resource qualification for
the remaining native entry points remain incomplete.

The experimental `tex_engine.profile` loads the ported Lambda LaTeX kernel as
an approved package resource and carries its definitions and category codes
into a document. It recognizes LaTeX primitives in a separate profile and
reports unported operations explicitly. Kernel preload, `\newif`, constructor
passthrough, scoped parameters, command and environment definitions with optional
arguments, environment grouping, and the resource path are tested in interpreter
and JIT modes. Captured local and bundled package files can be loaded, including
nested loads, metadata, end-of-package and document hooks, and restoration of
package category codes. Declared, passed, inherited and starred package
options are processed, and expandable package and class option queries work
inside definitions. Captured local classes can load a base class, pass class
options and run end-of-class hooks. The runtime supplies requested captures for
explicit TeX and LaTeX calls; this path still requires full corpus qualification.

The script profile supports LaTeX counter declarations, global set/add/step
operations, child resets, `\value`, and Arabic, Roman, alphabetic and symbol
formats using the same count registers as low-level TeX. Direct calls retain
source for the digester; counters declared by packages stay in the engine.
`\refstepcounter` currently has the native engine's numeric step/reset behavior;
reference-label state remains unfinished. An implicit `\numexpr` is bounded to
its counter argument and agrees with pdfLaTeX, while native input currently has
LI-014's lookahead defect (S1.8, D10.2.2).

The script TeX engine buffers terminal output for `\message`, `\errmessage`,
`\write` and `\showtokens`, including the current `\newlinechar` and flush
rules. Focused comparisons cover expansion, consecutive messages, empty
writes, changed newline characters and Unicode in both execution backends.
Output stream open/close state and writes match the native input engine's
modeled behavior, which discards file contents; this does not provide filesystem
output. The script retains native-compatible structured `\errmessage` diagnostics for
ordinary messages and source files; other diagnostic types and oversized
messages remain unfinished (S1.8, D10.2.2).

The script digester supports common mathematical style commands and `\in`,
and preserves ordinary emitted active text such as `~`. Active characters that
become LaTeX syntax are explicitly unsupported until expanded-output parsing is
qualified (S1.8, D10.2.2).

The experimental profile also accepts `script_expl3: true`. This selects the
hand-ported Lambda Script expl3 core: syntax-category transitions; local,
global, and protected control-sequence definitions; tested `N`- and `c`-argument
control-sequence existence and free-name conditionals and predicates; `NN`,
`cN`, `Nc` and `cc` meaning-comparison conditionals and predicates; and
existence aliases for token lists, sequences, integers, booleans, comma lists,
property lists, strings and dimensions. It includes token-list declaration
and clearing through tested `N` and `c` forms; assignment; local/global
meaning-copy with `NN`, `cN`, `Nc` and `cc` arguments; append and prepend with
tested `Nn`, `Nx` and `cn` forms; tested `N`/`c` use and counting; `N`/`c`
emptiness conditionals; `n`/`V` blankness, `NN`/`nn` equality conditionals,
`n`/`V` stringification, `n`/`N` item counting, and standard scratch token lists. The
integer subset includes non-allocating zero and one constants, integer
expressions, tested absolute-value, minimum and maximum helpers, `nNnTF`/`nNnT`/
`nNnF` comparisons with direct relation tokens or zero-argument macros
expanding to a single relation, chained `nTF`/`nT`/`nF` comparisons with
short-circuiting, and tested zero, odd and even conditionals. The sequence
subset includes construction and clearing through tested `N` and `c` forms,
scoped updates with tested `Nn` and `cn` insertion, local/global meaning-copy with `NN`, `cN`, `Nc` and `cc` arguments,
tested `Nn`/`cn` use, `N`/`c` counting and `Nn`/`cn` indexing, the standard
empty-sequence meaning and `NTF`/`NT`/`NF` and `cTF`/`cT`/`cF` emptiness
conditionals, tested `NnTF`/`NnT`/`NnF` membership, local/global reversal,
tested positive and negative indices, sequence bodies constructed by ordinary
TeX macros, and comparator-based sorting. Tested `Nn` and `cn` forms of local
and global sequence and token-list insertion use protected TeX macros,
preserving argument timing and introspection; their AST mapping remains guarded
(S1.8, D10.2.2). ASCII text case
conversion expands ordinary macros while preserving math and reference keys; it
reports unsupported Unicode and reference-option cases. Values remain
TeX tokens with their category codes and follow TeX group scope, including
`globaldefs`; they are not converted into Lambda source (S1.8, D10.2.2). The
profile supports script-backed `expl3` package selection, `\ProvidesExplPackage`
files, nested package syntax restoration, and document commands with mandatory
and one leading defaulted optional argument. Unsupported operations and other
document-command signatures report an issue. Exact self-recursive package
bootstraps now stop with an unsupported issue after the package has already been
attempted, rather than repeatedly emitting a missing-package declaration. The
project expl3 fixture loads and
its greeting, macro, integer, sorting and token-list commands match pdfLaTeX;
its ASCII text case-conversion and floating-point commands also match pdfLaTeX.
The expandable `\fp_eval:n` subset handles decimal arithmetic, parentheses,
`sqrt`, `abs`, and `round` with 16 significant digits and ties to even. It
explicitly rejects unsupported functions, fractional rounding places, literals
with more than 30 digits, exponents outside -200 to 200, and results outside
that magnitude range; these bounds are narrower than expl3. The option does
not replace the complete expl3 package and is not enabled on
production entry points. Interpreter and JIT tests compare supported cases with
the native engine, including the order of calls to sorting comparators.

The script expansion result records any untouched text after
`\end{document}` separately. An opt-in trace records original spans for direct
macro and environment definitions, including prefixes; foreign reads are marked
incomplete, and echo is not enabled by default. The experimental digester can
reconstruct echoed macro and environment declarations, including character-
delimited TeX parameters, multiline definitions, simple or starred robust
commands, and supported document-command definitions. Control-sequence delimiters remain unsupported. The digester also builds document,
paragraph, heading, nested environment, list, basic preamble declaration and
selected inline-formatting AST nodes including text underline, literal escapes,
common spacing, row separators and supported text accent nodes, as well as basic
math with fractions, radicals, underline and overline accents,
subscripts and superscripts. Its supported examples are compared with the native parser in
both execution backends. Lambda's computed element tags (S16.8.10) now let the
digester construct arbitrary generic command and environment names from the
input. It handles generic commands with multiple required, nested optional,
and directly sourced parenthesized arguments (including coordinate chains and
comment-separated groups), and ordinary nested environments. The token stream
tracks the last fresh main-file read, so synthetic inline-formatting commands
use native-compatible offsets across tested macro arguments, lookahead, case
shifts and package files. Expanded parenthesized arguments and foreign generic
commands still need reconstructed output spans and explicitly report issues.
Direct and macro-expanded spacing commands follow the native reconstruction
rule for control-word separators; other special commands and specialized
environment bodies remain unsupported (S1.8, D10.2.2).
The expansion layer scans font-relative `em`/`ex` and `px` dimensions and
handles explicit glue, kern and rule commands and moves inline `\hbox`, `\vbox`
and `\vtop` material with `\raise`, `\lower`, `\moveleft` and `\moveright`.
It also handles `\shipout` of inline boxes using the native input engine's
discard model, including assignment and message effects. Registered boxes,
cross-boundary `\unskip`, and movement or shipout AST source mapping remain
unsupported; native discard-boundary parity is blocked by
[LI-019](<../Lib_Input_Issue_Ledger (fixed).md#li-019>).
`\vcenter` scans its box specification and executes its body with the tested
grouping behavior; its math and document AST source mapping remains guarded.
For numeric `\mathchar`, `\delimiter`, `\radical`, and `\mathaccent`, the
Script engine scans tested operands, reports the native unsupported-math
character diagnostic, and continues; numeric math rendering, malformed operand
recovery, and AST source mapping remain unsupported (S1.8, D10.2.2).
Tested `\show` and `\showbox` inspection messages and the native input model's
`\showlists`, `\showgroups`, and `\showifs` no-ops are supported. `\showthe`
scans tested quantities and token lists without expanding the list contents.
Inspection AST source mapping and registered boxes remain guarded. The digester
also guards generated `\meaning` and `\string` text requiring escaped-output
source mapping, while retaining ordinary text digestion (S1.8, D10.2.2).
Characters emitted by `\chardef` retain a main-source position for tested
reconstruction cases, including character code zero. Emitted math-character
control names that the native scanner would split remain guarded.
For tested direct rule commands, the Script engine retains the original source
span; for expanded rules it emits the native formatted command. Supported
generated rules and simple inline boxes also produce AST nodes
and HTML. The script also scans well-formed hyphenation patterns, paragraph
shapes and penalty-list assignments, reads shape and penalty quantities, and
tracks font hyphen and skew characters and letter-spaced aliases on shared font
identities, resolves math font-family assignments and font identifiers, and
expands font-valued `\the` to the original font control token. Page and interaction primitives
follow the native input engine's fixed results; neither currently builds pages.
The script expands `\fontname`, `\jobname` and `\pdftexrevision`. The profile's
optional `base_path` supplies the document path used for `\jobname`; when it is
absent, the engine uses `texput`. Missing font identifiers recover to `nullfont`
with a diagnostic, while downstream malformed numeric and dimension scans still
need recovery work. The pdfTeX random and timer primitives follow the native
input engine's deterministic model: a seeded 32-bit sequence, a zero normal
deviate and a zero elapsed timer (S12.1.1v2). Numeric, dimension, glue and token
register `\the` values retain definition echoes for the tested cases.
Input streams can open and close captured resources, test EOF, and read lines
with `\readline` or tokenize them with `\read` for tested valid UTF-8 inputs.
Invalid UTF-8, surrogate endline characters and malformed read headers remain
unsupported. Direct-source `\verb` captures terminated raw text with an ASCII
delimiter in main and included files; malformed input, macro-sourced commands,
other verbatim forms and raw-inline AST mapping remain unsupported. Native AST
comparison for some valid raw content is blocked by
[LI-018](<../Lib_Input_Issue_Ledger (fixed).md#li-018>). An experimental Script
reconstructor preserves source spans and native token
serialization for tested text cases; it is not yet wired into the production
path. AST source mapping for some
assignments and font tokens, malformed-input recovery, full-corpus parity and
release performance remain open (S1.8, D10.2.2).
The installed engine also executes assignments inside some constructor and
unknown-command arguments that native TeX passes through. A corrected argument
mode remains staged because existing reference results expect the old behavior;
custom environments inside arguments additionally expose [LI-017](<../Lib_Input_Issue_Ledger (fixed).md#li-017>).
The digester reports `complete: false`, and the remaining definition-echo parity gap remains visible
in its test. It does not yet cover the full LaTeX grammar or source reconstruction.

These modules are not yet connected to production LaTeX parsing or capable of
loading the complete expl3 implementation. The remaining TeX primitives, format and package loading, AST
digestion, source maps, and public-entry migration must be implemented and
qualified before retiring the C++ path.

## Goal and reasons

The following is the earlier partial-port proposal, retained as background.

Port a useful, explicitly bounded subset of expl3 to Lambda Script as part of
`lambda.doc.latex`. Lambda's TeX engine already handles tokenization, category
codes, expansion, groups, assignments and other TeX operations, so the port can
build on that machinery. Script implementations can use Lambda's runtime and,
where eligible and hot, MIR JIT compilation (D8.3.1). JIT promotion and a
startup or throughput improvement are outcomes to measure, not guarantees.

The experimental script subset described above implements the first
control-sequence and token-list operations. The existing expl3 implementation
remains necessary for unsupported operations and packages. D10.2.2 requires TeX expansion before the LaTeX digester, with
TeX source kept as TeX; D9.1.3v2 forbids input from importing the runtime.
Shipped resources and document reads remain subject to D11.2.1.

## First subset

Use the existing `test/lambda/latex/fixtures/local_packages/lmdexpl.sty` as an
initial integration target, supplemented by smaller reference fixtures. Its
operations suggest these incremental groups:

1. Syntax and definitions: `\ExplSyntaxOn`, `\ExplSyntaxOff`,
   `\ProvidesExplPackage`, `\cs_new:Npn` and the document-command forms
   needed to call a defined function. TeX must still perform argument scanning,
   category-code changes, macro definition and group handling.
2. Token lists: `\tl_new:N`, `\tl_const:Nn`, `\tl_set:Nn`, `\tl_set:Nx`,
   `\tl_use:N` and `\tl_clear:N`, including local and global behavior where
   a public operation specifies it. Stored values must retain token identity,
   category codes and expansion semantics; strings alone cannot represent them.
3. Sequences: `\seq_new:N`, `\seq_set_from_clist:Nn`, `\seq_use:Nn`, and
   `\seq_sort:Nn`, including comparator expansion and group behavior.
4. Calculations and text: `\int_eval:n`, the fixture's `\fp_eval:n` expression
   subset, `\text_uppercase:n`, and `\str_compare:nNnTF` with sort-return
   markers. Numeric limits, rounding, Unicode behavior and errors must match
   the specified supported inputs.

These are candidate operations, not a claim that they are the most frequently
used across all expl3 code. Each group is complete only after its argument
variants and error behavior are specified and compared with the bundled
expl3/TeX reference. Unsupported variants must be identifiable; a partial port
must not silently claim full expl3 compatibility. Existing packages, including
`tex/binding/csvsimple.sty`, use additional operations and generated variants.

## Integration boundary

Today `lambda.doc.latex.latex.parse_source` calls `parse`; the input-owned
`run_tex_engine` expands the entire document before the direct parser returns
the AST. `EngineOptions` and `InputParseOptions` carry adapter names and raw
command names, but have no mechanism to invoke a Lambda Script function during
expansion. Existing adapters see reconstructed document commands after expansion;
they cannot supply expandable token results, definitions, or scoped assignments
back to a running TeX engine.

The Lambda and input owners therefore need a jointly specified boundary before
the subset can execute within TeX expansion. Possible designs include a
host-supplied synchronous operation interface or a resumable engine driven by
the host. The design must define token and control-sequence representation,
argument expansion modes, return tokens, group lifetime, local/global updates,
diagnostics, reentrancy and failure behavior. Runtime values crossing it require
precise roots and explicit ownership. Input must not import the runtime, and
resource authority must remain with the requesting principal. Neither a callback
ABI nor a change to D10.2.2 is assumed by this record.

The package and runtime side belong to Lambda; the TeX engine side belongs to
input (D9.1.3v2). A script-only module called after `parse` would not implement
the requested TeX-visible operations. Likewise, treating tokens as plain text
would lose observable TeX semantics.

## Qualification

Compare supported operations against the pinned bundled expl3 with nested
groups, assignment locality, unexpanded and expanded arguments, active
characters, control-sequence identity, errors, and invalid or unsupported
variants. Run the existing expl3 and bundled-package fixtures unchanged, and
exercise the new package through both interpreter and JIT tiers. Test scoped
resource denial and precise-rooting behavior across calls. Measure cold and
warm loading and document throughput in a release build, recording whether the
script operations actually reached the intended tier. Keep the current expl3
path until compatibility and selection behavior are established.

## Status

2026-11-11: this earlier bridge proposal has been superseded by the full script
port described above.


## Parser output content contract (2026-10-02)

**Spec linkage:** S2.6.1v2, S2.6.3, S2.6.4 and D3.3.1.
The 2026-10-06 package extension keeps that positional-content contract.
Generic commands now also expose `source_offset`, `starred` when present,
and `argument_groups`: ordered maps with `kind` (`required`/`optional`/
`parenthesized`),
`raw` group content, and `start`/`end` source offsets. `\item[label]` stores
the bracket group inside the item node; section commands retain `starred`,
and list environments retain `options_raw`. These attributes preserve syntax
for Lambda-script package adapters without changing content normalization
under **S2.6.1v2, S2.6.3–S2.6.5, D3.3.1**.

All parser-owned elements merge adjacent strings through the content append.
Consecutive string arguments of a command are collected in a non-spreading
array child: `\definecolor{accent}{HTML}{FF8000}` produces
`<definecolor ["accent", "HTML", "FF8000"]>`. A single string argument stays a
string; optional groups and non-string content break runs. Text fragments
inside one argument normalize before argument runs are collected.

The LaTeX package uses `util.command_args` to expose these array entries at
positional read sites (analysis, rendering, colors, boxes and macro calls).
Text extraction recursively reads arrays, and the LaTeX formatter emits each
array entry as a separate braced argument. MarkBuilder retains ordinary
arrays verbatim while normalizing element content; no format flag controls
merging.

**Additions (2026-10-12).** A parenthesized group counts as a command argument when a braced or bracketed group follows it, or when its content is a picture coordinate pair: two comma-separated numbers such as `(0,0)` or `(-1.2,-0.05)`. This captures `\multiput(0,0)(5,0){6}{…}` and `\qbezier(0,0)(5,5)(10,0)`, whose coordinates chain without a following brace, while prose such as `\LaTeX (2e)` stays text. The picture renderer reads every command's coordinates and body from these attributes. `\begin{multicols}{n}[preface][skip]` keeps its header out of the first column: the parser records the groups in `argument_groups`, stores the count as a `columns` attribute, and parses the preface as block content into a leading `multicols_preface` child, so the analysis pass numbers its headings in source order.

**Additions (2026-10-13).** Inside a `tabbing` environment, `\=`, `\'` and `` \` `` are tabbing commands, not text accents: the parser keeps them as elements named `=`, `'` and `` ` ``, as it already did for `\>`, so the script sees the tab stops. Outside `tabbing` they remain accents. The flag lives on the nested body parser and does not reach braced groups inside the body (Phase V M1, [Lambda_Impl_Latex_Packages.md](../impl/Lambda_Impl_Latex_Packages.md)).

**Additions (2026-10-13, Phase V M3).** fancyvrb's `\Verb` and minted's `\mint` and `\mintinline` are inline code like `\verb` and `\lstinline`. All but `\verb` take an `[options]` group, kept as `options_raw`. The minted commands then take a `{language}` group, kept as a `language` attribute. All but `\verb` accept the code either between a repeated delimiter or in braces, where balanced braces stay in the code. The engine passes the same commands through unexpanded (`P_RAW_GROUP_CMD`). The text-style commands (`\textbf`, `\textit`, `\texttt`, `\textrm`, `\textsf`, `\emph`, `\underline`) now carry `source_offset` like generic commands, so a source-order setting before them, such as ulem's `\normalem`, can be found.

**Additions (2026-10-13, Phase V M6).** titling's `\pretitle`, `\posttitle`, `\preauthor`, `\postauthor`, `\predate` and `\postdate` store TeX code in which an environment opens in one argument and closes in another (`\pretitle{\begin{center}\LARGE}`). Their braced arguments stay raw text, as a `\newenvironment` body does (`is_code_fragment_command` in `input-latex-tables.cpp`); the parser rejected them before as unclosed environments.

The unused Tree-sitter-era `latex/normalize.ls` phase, its import and the
unused reconstruction helper `util.rebuild_with_children` are retired. The
direct parser supplies normalized content; the package reads the current
argument model without an AST cleanup pass.

Math content uses the same array representation for consecutive text atoms,
including groups and delimiter bodies. The math package unpacks these token
runs when rendering sequences and extracting text; the math formatters read
them as logical atom sequences. Thus `2x` keeps a numeric atom and a variable
atom without storing adjacent strings under an element.

Verification: `make test-lambda-baseline` passes 6,151/6,151 tests, including
921/921 MathLive cases and the LaTeX argument-array and math token-array
regressions. The MarkBuilder and deep-copy suites pass 121/121 tests; the new
script regressions also match their goldens with both JIT and interpreter
tiers pinned.

The retired native-parser roadmap is retained in the [parser enhancement history](<Input_Latex_Parser_Enhancement_History (retired).md>).
