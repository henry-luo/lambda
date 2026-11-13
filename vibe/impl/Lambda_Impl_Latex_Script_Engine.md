# Lambda Script TeX and LaTeX engine implementation plan

**Status:** In progress; the experimental script engine is not the production
LaTeX input path. The records below include historical observations and test
results as of their dates; they do not imply full TeX compatibility.

**Scope and authority:** Port TeX expansion, LaTeX digestion and the selected
expl3 APIs to `lambda.doc.latex` in Lambda Script. TeX source remains data
(**S1.8**); the existing input boundary is governed by **D10.2.2** and the
input layer must not depend on the Lambda runtime (**D9.1.3v2**). The requested
production migration requires the coordinated formal and native-client work
described in [the input migration record](../../../lambda-input/vibe/input/Input_Latex.md).
The existing native engine design and historical Phase IV work are in
[TeX engine design](../latex/Lambda_Design_TeX_Engine.md) and
[LaTeX Phase IV implementation](Lambda_Impl_Latex_Phase4.md).

## Implementation priorities and acceptance

1. Complete core tokenization, expansion, scoped state, constructors, resources,
   diagnostics and TeX/expl3 operations, preserving native or separately ruled
   behavior. Record partial support and unsupported cases in the findings below.
2. Complete expanded-output reconstruction, source mapping and LaTeX digestion;
   compare text, diagnostics, ASTs and rendered documents against the existing
   native and TeX reference fixtures. Preserve existing golden results and fix
   root causes rather than hiding failing cases.
3. Qualify interpreter and JIT, representative forced-GC cases, the existing
   package/corpus tests and release-profile performance. Retest after each change
   that affects their results.
4. Specify and implement the production entry-point and native-client migration,
   including resource authority, lifetime and formal-design changes, before
   retiring the C++ path. The input-owned defects remain in the
   [Lib and Input issue ledger](../../../lambda-input/vibe/Lib_Input_Issue_Ledger.md).

General Lambda language, compiler and runtime defects found during this work
remain in the [Lambda issue ledger](../Lambda_Issue_Ledger.md). The package
findings below were moved from that ledger on 2026-11-12; their dated statuses,
reproductions, evidence and test history are retained.

## Lambda Script expl3 subset needs a TeX expansion boundary — 2026-11-11

**Status:** OPEN — feature integration boundary. **Dependent task blocked:** yes,
the requested expl3 subset in `lambda.doc.latex`. Authority: D9.1.3v2,
D10.2.2, D8.3.1 and D11.2.1.

`lmd/package/doc/latex/latex.ls` calls `parse` before its adapters run. Input's
`run_tex_engine` performs TeX expansion during that parse, and its current
`EngineOptions` and `InputParseOptions` carry adapter names and raw commands,
not a way to call Lambda Script during expansion. Consequently a script module
used by the existing post-expansion adapters cannot implement expandable expl3
operations, scoped TeX assignments or macro definitions at the point TeX needs
them. Reproduce by inspecting those entry points and the expl3 fixture at
`test/lambda/latex/fixtures/local_packages/lmdexpl.sty`.

The requested implementation needs Lambda-owned package/runtime work and an
agreed input/host boundary; the shape of that boundary is not yet decided.
Input must not import the runtime, values crossing the boundary require precise
rooting, and resource authority must be preserved. Input-side design and the
candidate first subset are recorded in `../lambda-input/vibe/input/Input_Latex.md`.

### Superseding request: full TeX and LaTeX script port — 2026-11-11

The user now requests porting the TeX engine to Lambda Script and moving the
entire LaTeX handling path to script, superseding the partial expl3 bridge
request above. **Dependent task blocked:** yes, pending Lambda-owned package,
runtime, formal-design and test work and a coordinated input migration.

Current evidence: `lambda.doc.latex.latex.parse_source` calls the built-in
`parse`; input's `parse_latex_direct` runs `tex::Engine` and then the C++
`DirectLatexParser`, while `parse_tex_expansion` exposes the engine directly.
Inspect `lmd/package/doc/latex/latex.ls`, input's `lambda/input/input-latex-c.cpp`
and `lambda/input/input.cpp`. A package-only engine leaves those public and
native entry points unchanged. D10.2.2 currently places every LaTeX parse at
the input boundary, so the requested migration requires coordinated revision
of that ruling. Preserve S1.8 (TeX remains data), D9.1.3v2 (no input runtime
imports), D11.2.1 authority and existing conformance and document fixtures.

At the time of this 2026-11-11 observation, no script engine had been shipped.
Input-side scope and candidate compatibility gates are documented in
`../lambda-input/vibe/input/Input_Latex.md`.

2026-11-11 follow-up: the user authorized coordinated edits to the Lambda and
input checkouts. `lmd/package/doc/latex/tex_engine/reader.ls` starts the script
port with incremental token reading, and `test/lambda/latex/test_tex_script_reader`
has a golden result passing in interpreter and JIT modes. The production parse
path is still the C++ implementation. Expansion, primitives, AST construction,
public-entry migration, formal ruling updates and full parity remain open;
the earlier authorization blocker is removed.


## Peer-sync qualification: TeX/expl3 package-echo golden mismatch — 2026-11-11

**Status:** FIXED for the script-handled package echo. The earlier aggregate
Lambda baseline was **7,475/7,476**, with the sole failure in
`test/lambda/latex/test_tex_script_expl3.ls`. Its `script expl3
package` golden expects `A` but receives `usepackage{expl3}A`; the duplicate
case expects `X ` but receives `usepackage{expl3}usepackage{expl3}X `. The
isolated unchanged test reproduces the same mismatch. These cases inspect the
script expansion's direct output-token values; this report does not decide
whether that projection, its golden or the expansion needs correction.
Evidence in the Lambda checkout: `temp/sync-peer/baseline.log`,
`expl3-recheck.log` and `expl3-recheck.json`.

The peer task changed neither the TeX modules nor their goldens; the mismatch
is outside its sync-module paths and does not block the focused peer acceptance
gates. This report does not assess the separately recorded package-echo selection
fix. S1.8 and D10.2.2 remain the applicable input boundary.


## Expanded parenthesized command argument source mapping — 2026-11-11

**Status:** OPEN — the source-provenance gap is confirmed; a complete mapping
strategy remains to be designed. **Full script TeX-port blocker:** yes, for AST
parity on expanded parenthesized arguments. The production native parser is
unaffected; the experimental digester reports these cases as unsupported.

Expand `\def\arg{(1,2)}\begin{document}\mystery\arg\end{document}` with
`profile.expand_with_kernel` and `echo_definitions: true`, then compare
`digest.digest(expanded).ast` to `parse(source, {type: "latex"})`. The native
AST puts `(1,2)` in a `parenthesized` argument of `mystery`, mapped to the
invocation; the expansion tokens point at the macro definition and have
`direct: false`. A second reproduction is `\def\arg{1,2}\begin{document}
\mystery(\arg)\end{document}`: only the contents are expanded, but native still
recognizes a coordinate argument and records its expanded raw value. Using only
original source slices misclassifies both examples as ordinary text and cannot
produce the native argument descriptor. The script digester must retain enough
invocation provenance and expanded text to reproduce native raw values and
source offsets without violating S1.8 or precise source ownership. The staged
port explicitly returns `latex-digest-unsupported` for these cases until then;
its regression tests retain these reproductions.


## Script spacing reconstruction lost separators and comment-adjacent whitespace — 2026-11-11

**Status:** FIXED for the tested spacing and control-space forms in the
experimental script digester. **Full TeX-port blocker:** no for these cases;
full source-provenance and LaTeX parity remain open.

With `\def\spaceout{\quad}\begin{document}A\spaceout B\end{document}`
and `echo_definitions: true`, native parsing yields `"A"`, `quad`, `" B"`;
the script digester previously yielded `"A"`, `quad`, `"B"`. The expanded
`quad` token is not source-adjacent to the following letter: native TeX
reconstruction inserts a separator to keep a control word and following ASCII
letter distinct. This is not simply copied invocation whitespace: two spaces
at the macro call still produce one separator, while following punctuation
needs none. The digester now follows that reconstruction rule.

The same investigation found that direct spacing commands and control spaces
followed by a comment could lose whitespace around the comment. The shared
source-gap scanner now retains that whitespace while matching native comment
line-ending handling, including CRLF. The full script digester goldens pass
interpreter and JIT, with native-AST comparisons for macro expansion, following
letters, punctuation and groups, and LF/CRLF comments. The separate expanded
parenthesized-argument source-mapping issue remains open.

## Script TeX control-space primitive was unsupported — 2026-11-11

**Status:** FIXED in the experimental script engine. **Full TeX-port blocker:**
no for the tested control-space forms; full TeX and LaTeX parity remains open.

Expanding `A\ B` with the script profile previously stopped with
`tex-unsupported-primitive` for control space, although native TeX keeps the
primitive in the expanded stream and native LaTeX parses it as a literal space.
The primitive was recognized in the registry but lacked an execution branch.
The script expansion step now preserves the control token, including `\let`
aliases; the digester consumes a literal control space and recovers direct
following whitespace from source. New TeX expansion token tests and
`test/lambda/latex/test_tex_script_control_space.ls` cover direct, repeated,
macro-expanded, and formatted cases. The staged expansion and digester
goldens pass interpreter and JIT; the control-space AST cases match native in
both backends.


## Macro-generated inline formatting uses definition offset — 2026-11-11

**Status:** FIXED for the tested inline-formatting cases. The stream records
fresh main-file reads and synthetic formatting tokens carry the current read
offset; source reconstruction for generic commands remains open separately.
The production native parser is unaffected.

Expand `\def\u{\textbf{Hi}}\begin{document}\u\end{document}` using
`profile.expand_with_kernel` with `echo_definitions: true`, then compare
`digest.digest(expanded).ast` against `parse(source, {type: "latex"})`. The
script digester previously returned a formatting element whose source offset
points at the command in the macro definition, whereas native reconstruction
maps the expanded command to its invocation. The affected synthetic token has
`direct: false` and retains its definition offset. The stream now records the
last fresh main-file read without resetting it when tokens are pushed back or
foreign files are read; the digester uses that position for synthetic inline
formatting. `test_tex_script_provenance` compares nested and repeated macros,
arguments, lookahead, case changes and non-ASCII text against native ASTs;
`test_tex_script_provenance_package` compares offsets from a local package.
The installed tests and existing TeX script suites pass interpreter and JIT,
including focused forced-GC checks. Expanded parenthesized and foreign generic
command source mappings remain open.


## Script TeX underline and overline primitives were unsupported — 2026-11-11

**Status:** FIXED for direct and tested macro-generated text underline, and
tested math underline/overline forms. **Full TeX-port blocker:** no for the
covered forms; generic source reconstruction remains open.

The experimental profile stopped with `tex-unsupported-primitive` for
`\underline{Hello}` and `$\overline{x}$`, although native TeX retains those
primitives for the LaTeX digester. They were recognized in the primitive
registry but lacked execution branches. The script engine now preserves the
primitive tokens and aliases; the digester builds native-compatible text
underline and math accent nodes. `test/lambda/latex/test_tex_script_underline`
covers nesting, empty and spaced groups, math expressions, primitive aliases,
and macro-generated formatting cases. Expansion and digester goldens pass
interpreter and JIT, and supported ASTs
match native parsing on both backends.


## Empty TeX macro argument inserts a null token — 2026-11-11

**Status:** FIXED for tested empty required and optional macro arguments.
**Full script TeX-port blocker:** no for those forms. Native TeX is unaffected.

Before the fix, expanding `\def\a#1{[#1]}\a{}` in the script engine failed with
E201, `map literal spread requires a map-like value or element`. The macro
substitution comprehension places a conditional around `*args[t.value]`; an
empty argument becomes a null item, which `stored_tokens` then tries to spread
as a map. A standalone reproduction is `let a = [[]]; [for (t in [0]) if
(true) *a[t] else t]`, which produces `[null]` on the observed interpreter.
The fix spreads the selected token array outside the conditional so that the
empty argument contributes zero tokens. The expression's behavior is consistent
with collapsed list values under S2.5.5v2; no runtime defect is asserted. Both
TeX and expl3 macro substitutions use this shared helper. The installed
`test_tex_script_provenance` covers empty, repeated and mixed arguments in both
backends; existing TeX and expl3 goldens also pass.

## TeX case shifting retains direct source flags — 2026-11-11

**Status:** FIXED for the tested case-shift source mappings. **Full script
TeX-port blocker:** no for those forms. Native TeX is unaffected.

For `\begin{document}\uppercase{\textbf{Hi}}\end{document}`, the native
AST reports the `textbf` source offset at the last main-file token read by
the case operation, while the script previously used the command's original
position. Native `case_shift` backs up a stored copy of the scanned token list,
and `copy_stored` clears direct-source flags; the script shifted the list and
pushed it without clearing them. The fix stores the resulting tokens before
pushing them, preserving TeX token values and category codes while marking their
source as synthetic. The stream also tracks the main-file read position used
for synthetic formatting tokens.
`test_tex_script_provenance` compares upper and lower case, nested formatting
and zero mappings to native in both backends.


## Script LaTeX profile cannot load lmdnotes counter declaration — 2026-11-11

**Status:** FIXED for counter expansion in the local-package fixture. The
fixture's synthesized class declaration still blocks script AST digestion in
the separate source-mapping issue below; the native production path remains
available.

Run `profile.expand_with_kernel` with the source from
`test/lambda/latex/fixtures/local_packages/document.tex`, authorized local
snapshots of `lmdreport.cls` and `lmdnotes.sty`, and an empty bundled snapshot.
The script result previously stopped at `newcounter` in `lmdnotes.sty`; see
`../lambda-input/temp/expl3-script/provenance-existing-fixture.out`. The script
engine now implements register-backed declarations, global set/add/step,
child resets, `value` and the tested counter formats. The installed
`test_tex_script_counters` passes interpreter and JIT and compares with native
and pdfLaTeX, keeping input LI-014's two intentional native divergences visible.
`test_tex_script_counters_fixture` confirms expansion reaches both exercise
numbers; AST digestion still reports the class source-mapping issue.
`refstepcounter` currently implements numeric step/reset only; reference-label
state remains unfinished.


## Foreign generic command offsets use package positions — 2026-11-11

**Status:** OPEN — foreign-source argument mapping is unimplemented.
**Full script TeX-port blocker:** yes, when loaded package files emit generic
commands. Native production parsing is unaffected.

A local package containing `\mystery{Package}` and loaded by
`\usepackage{foreign-output}\begin{document}Hi\end{document}` produces
a native `<mystery>` whose `source_offset` and required-argument boundaries
map to main-file offset 27. The staged script expansion/digestion instead
returned the package-file command and group offsets 54, 62 and 71. Reproduce
with `../lambda-input/temp/expl3-script/provenance-foreign.ls` and its captured
local files under `../lambda-input/temp/expl3-provenance/`; evidence is in
`provenance-foreign-generic.out` there. The command tokens are direct relative
to their package file but their emitted bytes are synthetic relative to the
main document. The staged digester now reports foreign generic commands as
unsupported until reconstructed output spans and main-file offset mapping are
available. This is related to the expanded parenthesized-argument issue.


## Script TeX writes bypass terminal line buffering — 2026-11-11

**Status:** FIXED for terminal writes and `\showtokens` in the script engine.
Output-file streams remain separately OPEN below.

Previously `\immediate\write16{}` recorded an empty message, while native
TeX flushes no line. With `\newlinechar=64\immediate\write16{A@B}`, native
records `A` and `B` separately while the script recorded `A@B`. The cause was
that `write_message` directly appended `token_text` instead of honoring
`newlinechar` and native flush rules. The script now buffers terminal output,
only splitting newly printed text using the current newline character; writes
and `\showtokens` flush around their output. The installed
`test_tex_script_message_pending` compares empty writes, line breaks, changes to
`newlinechar`, and interleaved message/write/show operations against native in
interpreter and JIT. The earlier reproduction is in
`../lambda-input/temp/expl3-script/message-old-interp.out`.


## Script TeX message primitive is unsupported — 2026-11-11

**Status:** FIXED for terminal output and string diagnostic codes; structured
native diagnostic metadata remains separately OPEN. **Full script TeX-port
blocker:** this primitive no longer blocks the tested message cases.

Previously `\message{hello}` returned `tex-unsupported-primitive`, while native
`parse("\\message{hello}", {type:"tex"})` appended `hello` to its messages.
The registered `\errmessage` primitive also had no execution branch. Both now
expand their arguments and use the shared terminal buffer, including native
spacing and flush behavior; `\errmessage` adds `tex-errmessage` to script
diagnostic codes. Installed native comparisons cover multiple messages, empty
messages, macro and counter expansion, newline changes, Unicode, and error
lines; they pass in interpreter and JIT. Forced-GC tests pass both tiers.
Structured diagnostics and output-stream state remain separately OPEN.


## Echoed TeX source spans lose token sequence boundaries — 2026-11-11

**Status:** FIXED for sequence-boundary retention. The general reconstruction
and source map remain OPEN; native production parsing is unaffected.

`\def\x{A}% comment\nB` emits an echoed `source-span` followed by `B`.
The original reader tokens have consecutive sequence numbers, but the span
only retains byte offsets and drops its first and last sequence. Native output
reconstruction uses those sequence boundaries to preserve intervening source
bytes; without them, the script cannot correctly tell whether the comment and
newline belong in the reconstructed output. `stream.end_trace` computed the
boundaries but did not return them. It now returns the first and last sequence,
and echoed definition spans retain them. The installed stream and trace-span
tests cover adjacent comments, lookahead, and macro output in interpreter and
JIT; forced-GC tests also pass. Evidence is in
`../lambda-input/temp/expl3-script/trace-span-interp.out`.


## Script TeX output does not retain emission origins for all tokens — 2026-11-11

**Status:** OPEN — the general emitted-output provenance contract is incomplete.
**Full script TeX-port blocker:** yes, for native-compatible reconstructed
source offsets. Native production parsing is unaffected.

For `\def\m{\quad}\begin{document}A\m B\end{document}`, native
reconstruction inserts a space between the expanded control word and `B`. Its
separator is mapped to the main-file read position recorded at emission. The
script's direct `B` output token has no emission-position field; synthetic
class declaration tokens also have no recorded main-file origin. Existing
`read_offset` covers selected synthetic tokens processed by `step`, not all
emission paths. A temporary reconstruction prototype therefore reports
`tex-reconstruct-missing-origin` at this separator rather than guessing.
Evidence: `../lambda-input/temp/expl3-script/reconstruct-tests-interp.out` and
`counter-fixture-prefix.out` in the same directory. Native `emit_token` and
`emit_text` record `last_main_offset` per output item; the script needs the
same contract, including output emitted after lookahead or by package helpers.


## Foreign RequirePackage declaration is echoed instead of synthesized — 2026-11-11

**Status:** FIXED for selecting the echoed or synthesized declaration.
Reconstructed source gaps and full offset mapping remain separately OPEN.
Native production parsing is unaffected.

In `test/lambda/latex/fixtures/local_packages/document.tex`, the captured
`lmdnotes.sty` contains `\RequirePackage{xcolor}`. Native input emits the
synthesized declaration `\usepackage{xcolor}`, while the script emits the
foreign direct `RequirePackage` token and its package-file arguments. Previously the
script's `latex_usepackage` emitted original tokens whenever the plan had no
local loads, without checking whether a main-file source echo was valid. Native
`do_usepackage` uses `echo_finish`, which rejects foreign source reads, and
otherwise emits a synthesized declaration. The script now traces and validates
the echo, synthesizing the declaration when tracing fails. Installed native
comparisons cover direct and macro-generated commands and names, options,
duplicates, empty names, and the local-package fixture; they pass interpreter
and JIT, as do focused forced-GC checks. The original reproduction is in
`../lambda-input/temp/expl3-script/reconstruct-fixture-interp.out`.

## Echoed package declarations lose their consumed source gap — 2026-11-11

**Status:** OPEN — the script emits the declaration's explicit tokens but
omits whitespace consumed by its optional-version lookahead. **Full script
TeX-port blocker:** yes, for native reconstructed text and offset parity.
Native production parsing is unaffected.

In `test/lambda/latex/fixtures/local_packages/document.tex`, native output
preserves the newline after the direct main-file `\usepackage{amsmath}` before
a subsequently synthesized declaration. The script emits the command and its
argument tokens, while `optional_argument` consumes the following newline;
the resulting output items no longer contain that source gap. Native
`echo_finish` emits one source span extending through the consumed bytes.
A temporary reconstruction with matching empty adapter configuration shows the
missing newline in `../lambda-input/temp/expl3-script/reconstruct-fixture-aligned.out`.
The same prototype also lacks the raw trailing source after `\end{document}`;
that trailing-source behavior needs to be carried into reconstruction.


## Script scantokens pseudo-source is unavailable to reconstruction — 2026-11-11

**Status:** FIXED for pseudo-source retention. General reconstructed output
and source mapping remain OPEN. Native production parsing is unaffected.

For `\begin{document}\scantokens{\textbf{Hi}}\end{document}`, native
expanded text contains `\textbf {Hi}` while a temporary script reconstruction
emits `\textbf{Hi}`. Native converts the scanned token list to a pseudo-file,
registers it in the engine's source list, and preserves the skipped space between
adjacent direct pseudo-file tokens. The script previously created its reader with file id -1 and did not retain the
source, preventing reconstruction from reading the gap. It now registers a
unique source for each `\scantokens` reader. Installed tests cover multiple and
nested calls and Unicode byte offsets; both backends and forced-GC checks pass.
The experimental reconstruction now matches native for this case and all 29
provenance text cases in interpreter and JIT. Evidence:
`../lambda-input/temp/expl3-script/reconstruct-scan-probe.out` and
`reconstruct-provenance-fixed-interp.out`.


## Script TeX inputlineno quantity is unsupported — 2026-11-11

**Status:** FIXED for the tested physical, pseudo-file, and local-package lines.
Structured diagnostic line metadata remains separately OPEN. Native production
parsing is unaffected.

Previously `Line 1\n\the\inputlineno/\number\inputlineno` stopped with
`tex-unsupported-primitive`, while native emitted `2/2`. The reader now tracks
physical lines per reader, and both `\the` and numeric scans read the active
reader's line number. Installed native comparisons cover CRLF, blank/comment
lines, signed scans, macro calls, multiline expansion, nested `\scantokens`,
and a local package returning to the main file. They pass interpreter and JIT,
as do affected regression and forced-GC checks. Original evidence:
`../lambda-input/temp/expl3-script/inputlineno-probe.out`.


## Script TeX character primitive is unsupported — 2026-11-11

**Status:** PARTIALLY FIXED — scalar values and invalid-range diagnostics are
implemented; surrogate code points remain explicitly unsupported. **Full script
TeX-port blocker:** yes, for surrogate values. Native parsing is unaffected.

Previously `\char65\char36` returned `tex-unsupported-primitive` for `char`,
while native reconstructed `A\$`. The script now scans and emits character
tokens with native categories, keeps the main-read position, and reports
`tex-bad-char` for numbers outside the native range. Installed tests cover
ASCII, Unicode scalars, zero, invalid ranges, registers, aliases, numeric forms,
macros, and special characters in interpreter and JIT; forced-GC checks pass.
Surrogate code points are explicitly rejected with `char surrogate`: native
retains such tokens while Lambda strings cannot encode them. The regression
test keeps this limit visible. Evidence:
`../lambda-input/temp/expl3-script/char-complete-interp.out`.


## Script TeX stops after an extra closing brace — 2026-11-11

**Status:** OPEN — script error recovery differs from native. **Full script
TeX-port blocker:** yes, for malformed documents where native continues after a
diagnostic. Native production parsing is unaffected.

Expand `A}B`: the script stops with `tex-extra-brace` after emitting `A`.
Native reports the same diagnostic and continues, producing `A}B ` (including
the reader's final space). The script treats this diagnostic as a terminal
`issue`; native `handle_right_brace` emits the extra token after reporting it.
Evidence: `../lambda-input/temp/expl3-script/brace-recovery-probe.out`.
The same difference occurs for an extra `\endgroup` and mismatched group
types. A staged fix matches native output and diagnostic codes on seven cases,
but the existing `test_tex_script_expansion.txt` explicitly expects a terminal
`tex-extra-endgroup` for `{\endgroup}`. The supplied checkout instructions
require preserving reference results; the staged change is not installed and
that failing comparison remains visible in
`../lambda-input/temp/expl3-script/group-recovery-existing-test.out`. Structured
diagnostic metadata remains a separate OPEN issue.


## Script digester rejects active-character definition targets — 2026-11-11

**Status:** OPEN — echoed definitions of active characters lack a digester
branch. **Full script TeX-port blocker:** yes, for documents defining active
characters when definition echo is enabled. Native parsing is unaffected.

For `\def~{XYZ}\begin{document}A~B\end{document}`, expansion succeeds, but
script digestion returns `latex-digest-unsupported` for `definition target`.
Native LaTeX retains the definition and expands the document text to `AXYZB`.
The staged active-token text fix does not handle definition targets. Evidence:
`../lambda-input/temp/expl3-script/active-digest-interp.out`. The existing
`test/lambda/latex/test_tex_script_active_digest.txt` explicitly expects the
unsupported result; preserving existing references blocks promoting a change
that alters that result without an owner ruling.


## Script LaTeX digester rejects emitted active characters — 2026-11-11

**Status:** PARTIALLY FIXED — ordinary emitted active text is supported;
active characters that reconstruct to LaTeX syntax remain explicitly unsupported.
**Full script TeX-port blocker:** yes for those syntax characters. Native parsing
is unaffected.

Previously `\begin{document}A~B\end{document}` produced
`latex-digest-unsupported` for an active token while native produced paragraph
text `A~B`. The script now accepts emitted active text and accounts for active
letters after spacing commands. Installed native-AST tests cover tildes,
macros, custom active punctuation and letters, and spacing in interpreter and
JIT; forced-GC checks pass. Active `%`, `$`, and other syntax characters are
explicitly rejected because treating them as plain text produces an incorrect
AST; `active-syntax-probe.out` in `../lambda-input/temp/expl3-script/` shows
the native error. Active definition targets remain separately OPEN.


## Script math digester lacks common style commands — 2026-11-11

**Status:** FIXED for the tested style-command arguments and `\in` symbol.
Broader math support and general reconstruction remain OPEN. Native parsing is
unaffected.

Previously the local-package fixture reached `latex-digest-unsupported` for
`math command mathbb`. The script now emits native-compatible `style_command`
nodes for 12 common style commands and handles `\in`. Sixteen installed
native-AST comparisons cover braced, unbraced, nested, and scripted arguments
in interpreter and JIT; forced-GC checks pass. An isolated reconstruction and
reparse of the fixture now matches native AST content and mapped offsets in
both tiers, while root diagnostics/package metadata remain separately OPEN.
Evidence: `../lambda-input/temp/expl3-script/reparse-fixture-remapped.out`.


## Script math digester rejects repeated scripts — 2026-11-11

**Status:** OPEN — repeated scripts are not represented by the script math
digester. **Full script TeX-port blocker:** yes for native AST parity on these
expressions. Native parsing is unaffected.

For `\begin{document}$x^2^3$\end{document}`, the script reports
`latex-digest-unsupported` for `repeated math script`; native produces a
superscript group containing `23`. The existing `test_tex_script_digest`
golden records this case as pending. Evidence:
`../lambda-input/temp/expl3-script/reparse-digest-v2-interp.out`. This remains
a visible unsupported case; no reference result has been altered.


## Script digester rejects bracket text after unknown environment start — 2026-11-11

**Status:** OPEN — the generic environment guard rejects a leading bracket.
**Full script TeX-port blocker:** yes for documents using this syntax. Native
parsing is unaffected.

For `\begin{document}\begin{strange}[x]Hello\end{strange}\end{document}`,
the script reports `latex-digest-unsupported` for `environment options`;
native places `[x]Hello` in the `strange` environment's body. The existing
`test_tex_script_digest` golden records this case as pending. Evidence:
`../lambda-input/temp/expl3-script/reparse-digest-v2-interp.out`. This remains
a visible unsupported case; no reference result has been altered.


## Script digester rejects leading bracket text in document — 2026-11-11

**Status:** FIXED for the document environment; generic environment brackets
remain separately OPEN. Native parsing is unaffected.

Previously `\begin{document}[A]\end{document}` returned
`latex-digest-unsupported` for `environment options`, while native treated
`[A]` as body text. The script now applies the option guard only to other
environments. Installed native comparisons cover direct, repeated, nested,
followed-by-group and macro-generated brackets in interpreter and JIT. The
full existing digest golden passes both tiers, and the reconstructed-path corpus
now matches 101 native ASTs with two explicitly unsupported cases. Original
evidence: `../lambda-input/temp/expl3-script/document-bracket-probe.out`.


## Experimental reconstructed-text digestion drops NUL characters — 2026-11-11

**Status:** FIXED in the isolated reconstruction prototype using the VMap
embedded-NUL key fix; general reconstruction remains uninstalled and OPEN.
**Full script TeX-port blocker:** yes, before that path can produce native ASTs
for documents containing character code zero. Production parsing is unaffected.

The prototype reconstructs `\begin{document}\char-1\end{document}` and
retokenizes the resulting text with the TeX reader. Native LaTeX digestion
retains the emitted NUL character, while the TeX reader's default category
for NUL ignores it; both `tex-bad-char` metadata and a successful prototype
AST are then present, but AST equality is false. The installed direct-token
script digester matches native on this example. Evidence:
`../lambda-input/temp/expl3-script/reparse-diagnostics-interp.out`. The
prototype must use LaTeX digester lexical semantics or explicitly reject cases
it cannot preserve; it must not be connected to production in its current form.


The tokenizer's explicit NUL category now survives the corrected map spread,
and the prototype matches native ASTs for invalid `\char` and mixed supported
diagnostics in interpreter and JIT with the isolated fixed executable. Evidence:
`../lambda-input/temp/expl3-script/nul-map-fix/reparse-diagnostics-interp.out`.


## Synthesized class declaration has no source span — 2026-11-11

**Status:** OPEN — expanded-output reconstruction is missing. **Full script
TeX-port blocker:** yes, for the existing local-class document fixture.
The native production parser is unaffected.

With the staged counter implementation, `profile.expand_with_kernel`
completes expansion of `test/lambda/latex/fixtures/local_packages/document.tex`
with authorized `lmdreport.cls` and `lmdnotes.sty` snapshots. Digestion stops
with `latex-digest-unsupported`, `declaration source mapping`. The class
loader emits a synthesized `\documentclass{article}` whose tokens have no
source file, span, or main-file read offset; `group_raw` therefore cannot
construct the native declaration's argument descriptor. Reproduce with
`../lambda-input/temp/expl3-script/counter-fixture-prefix.ls`; the observed
tokens and issue are in `counter-fixture-prefix.out`. The general output
reconstruction and source-segment mapping must cover generated declarations
as well as foreign generic commands and expanded parenthesized arguments.


## Generic argument end offsets ignore reconstructed segment boundaries — 2026-11-11

**Status:** FIXED for the tested direct-argument boundaries. The native
production path is unaffected; general expanded-output reconstruction remains
open.

In `\begin{document}\newcounter{p}\newcounter{c}[p]\stepcounter{p}\arabic{c}\end{document}`,
the direct `stepcounter` argument ends at source offset 62, but native maps
its reconstructed-output boundary to offset 71, the next synthesized token's
main-file read position. The script previously returned 62. An empty macro
between a direct command argument and the next direct token produces the same
class of mismatch: reconstruction maps the boundary to the next nonadjacent
source segment. Evidence and native AST comparisons are in
`../lambda-input/temp/expl3-script/counter-ast-detail.out` and
`counter-boundary-probe.out`. The digester maps a boundary to the next
synthetic token's read offset, or to the next nonadjacent direct token's
position, and keeps the original end for adjacent direct segments. The installed
`test_tex_script_counters` covers direct, empty-expansion, synthetic,
parenthesized and optional boundaries in both backends. More general
expanded-output source reconstruction remains open.


## Script digester omits native TeX diagnostic metadata — 2026-11-11

**Status:** OPEN — metadata representation and propagation are incomplete.
**Full script TeX-port blocker:** yes, for AST parity when TeX reports
diagnostics. Native production parsing is unaffected.

For `\begin{document}\newcounter{n}\setcounter{n}{1\relax A}\arabic{n}\end{document}`,
native LaTeX parsing attaches `tex_diagnostics` containing a
`latex-counter-value` diagnostic and a `tex_packages` attribute to the root
element. The staged script expansion records the diagnostic code in its
state, but `digest.digest` returns an AST without those attributes; full AST
equality fails even when the document children agree. Reproduce using
`../lambda-input/temp/expl3-script/counter-relax-detail.ls`; the output is
recorded in `counter-relax-detail.out`. The script engine currently keeps only
string diagnostic codes in several paths, so reproducing native structured
messages and source positions requires a general diagnostic representation
and mapping rather than adding empty attributes.

For clean documents that loaded local packages, the script digester now attaches
`tex_diagnostics: []` and the ordered local `tex_packages` list. The installed
`test_tex_script_package_metadata` compares a real package fixture and a document
without packages against native in interpreter and JIT, and focused forced-GC
checks pass. The experimental reconstructed path now achieves full native AST
equality on the real local-class fixture when diagnostics are empty. Structured
diagnostics and their file/line/offset metadata remain OPEN for other codes.

The script now also records complete native-compatible `tex-errmessage` entries
for messages up to 511 UTF-8 bytes without NUL, including main-file,
`\scantokens`, and local-package source locations. The digester attaches them
only when every diagnostic has a complete record. Installed tests compare
messages, offsets, filenames, lines, and ASTs in interpreter and JIT, with
forced-GC checks. Mixed diagnostic codes and oversized messages remain visibly
incomplete in `test_tex_script_errmessage_diagnostics`; see
`../lambda-input/temp/expl3-script/errmessage-structured-qualified-interp.out`.

Out-of-range `\char` now also records the native `tex-bad-char` message and
location. Installed comparisons cover negative and oversized values, multiple
errors, and mixed `tex-bad-char`/`tex-errmessage` ASTs; both backends and
forced-GC checks pass. Other diagnostic types remain OPEN.


## Script TeX output streams are unsupported — 2026-11-11

**Status:** FIXED for the existing native input engine's modeled stream behavior.
Both engines discard written file contents; actual filesystem output is not
implemented by this compatibility model.

Previously `\immediate\openout0=unused.txt \immediate\write0{secret}`
stopped with `tex-unsupported-primitive` for `openout`. The script now scans
filenames, tracks the 16 output stream slots, implements `openout` and
`closeout` (including their immediate and aliased forms), and suppresses
terminal output for writes to open streams. The installed
`test_tex_script_output_stream` compares 12 stream, grouping, number-wrapping,
filename, and terminal-buffer cases with native in interpreter and JIT. Focused
forced-GC tests pass in both backends. The original reproduction is in
`../lambda-input/temp/expl3-script/stream-output-probe.out`.

## Script class and counter echoes omit consumed source bytes — 2026-11-11

**Status:** OPEN — the installed script emits the explicit tokens of direct
main-file class and counter declarations, but does not retain native echo spans.
**Full script TeX-port blocker:** yes, for native-compatible reconstructed ASTs
and source offsets. Native production parsing is unaffected. S1.8 and D10.2.2
cover the data and input boundary.

In an isolated reconstruction prototype, `\documentclass{article}` followed
by a newline, comment or spaces produces a different AST from native because
optional-version lookahead consumed the gap. `\newcounter{n}` followed by a
newline similarly loses bytes consumed by optional-parent lookahead. The native
`echo_finish` emits the traced source span, including these consumed bytes.
The prototype also explicitly reports missing origins for macro-generated
`\stepcounter`, `\setcounter`, and `\chardef` output. Evidence, including
interpreter/JIT parity: `../lambda-input/temp/expl3-script/reparse-origin-interp.out`
and `reparse-origin-jit.out` in the same directory. These findings extend the
separately recorded package-gap and emission-origin issues; the prototype is
not installed as a production path.

## Reconstructed LaTeX comments can start an empty paragraph — 2026-11-11

**Status:** OPEN — the isolated script reconstruction and reparse prototype
cannot yet represent every comment preserved in emitted TeX source. **Full
script TeX-port blocker:** yes, for native-compatible ASTs. Native production
parsing is unaffected. S1.8 and D10.2.2 apply to the data and input boundary.

For `\documentclass{article}%x` followed by a newline and
`\begin{document}A\end{document}`, the native AST contains an empty paragraph
between the class declaration and document. The experimental reparse feeds
reconstructed bytes to the TeX reader, which discards `%x` and its line ending
and therefore silently omitted that paragraph. The prototype now explicitly
reports `latex-digest-unsupported` / `preserved comment` when skipped reader
gaps contain `%`; this conservative guard can reject cases whose AST would
otherwise match. Native examples in document content and command arguments are
recorded in `../lambda-input/temp/expl3-script/native-comment-tests.out`; the
original mismatch and guarded outcomes are in `reparse-origin-v2-interp.out`
and `reparse-origin-tests-guard-v2-interp.out` in that directory. Interpreter
and JIT agree on the focused outcome. A LaTeX-source-aware comment representation
is needed before this guard can be removed.

## Script LaTeX declarations omit trailing optional groups — 2026-11-11

**Status:** FIXED for adjacent brace and bracket groups on declarations.
Native production parsing is unaffected. S1.8 and D10.2.2 apply to the input
boundary; the broader script TeX migration remains incomplete.

For `\documentclass[11pt]{article}[2020/01/01]` followed by a document, native
places the trailing optional group in `documentclass.argument_groups` and a
`brack_group` child; the isolated reconstruction and script digest instead
emit it as paragraph text. Reproduced in both execution backends by
`../lambda-input/temp/expl3-script/reparse-origin-more.ls`, with results in
`reparse-origin-more-interp.out` and `reparse-origin-more-jit.out` beside it.
The declaration parsing branch in `lmd/package/doc/latex/tex_engine/digest.ls`
reads only the initial optional and required groups; the trailing group needs
native-compatible general argument handling.

The script digester now extends declarations through adjacent required and
optional groups, retaining group descriptors and native argument packing;
whitespace and parentheses end the declaration's group sequence. New
`test/lambda/latex/test_tex_script_declaration_groups.ls/.txt` compares ten
cases with the native AST. The new test and existing digest and provenance
goldens pass interpreter and JIT; the declaration suite also passes against
the installed package in both tiers. The separate reconstructed-comment
limitation and general emission provenance remain OPEN.

## Script LaTeX environment recognition ignores command-to-brace spaces — 2026-11-11

**Status:** PARTIALLY FIXED — ordinary whitespace between environment commands
and braces now follows native parsing; comment gaps and other malformed or
unmatched cases can still be explicitly unsupported. **Full script TeX-port
blocker:** yes, for remaining AST parity. Native production parsing is
unaffected. S1.8 and D10.2.2 govern the data and input boundary.

Reproduction `\begin {document}\begin {quote}A\end {quote}\end {document}`
produces nested `document` and `quote` nodes in the experimental reconstructed
script AST, but four paragraph nodes in native parsing. The script digester's
`blocks` branch recognizes a control token followed by a group without checking
the raw gap. Both backends reproduce the difference in
`../lambda-input/temp/expl3-script/reparse-environment-origin-interp.out` and
`reparse-environment-origin-jit.out`. The same audit found that macro-generated
`\end{quote}` and `\end{document}` are rejected by the prototype as missing
emission origins, consistent with the separately recorded general provenance
issue. Macro-generated starts and an unmatched math definition in the test
produce native parse errors, so those cases are not evidence of script parity.



The script digester now preserves whitespace before a spaced environment
argument as paragraph content, rather than treating the command as a boundary.
`test/lambda/latex/test_tex_script_environment_gaps.ls/.txt` covers spaces,
tabs, newlines, nesting, ordinary adjacent environments, and an unclosed
environment. The installed suite and existing digest, provenance and declaration
goldens pass interpreter and JIT. Unsupported comment gaps and unmatched
endings remain visible, and the separate general provenance issue remains OPEN.

## Script TeX digestion and reconstruction follow-up — 2026-11-12

The script inline digester now consumes the `\end` command itself while
retaining its following group and whitespace, matching the native direct
parser's `parse_command` handling. The installed
`test/lambda/latex/test_tex_script_definition_end.ls/.txt` compares seven
macro-definition examples, including nested and spaced forms, with native.
The new test passes installed in interpreter and JIT; existing digest,
provenance, declaration and environment goldens also pass both backends.

The separately recorded document-tail fix cannot yet be installed without changing the
existing `test_tex_script_expansion.txt` expectation: its `latex document hooks`
case asserts the old `TAIL` value. The checkout instructions require preserving
reference results; the native-matching change stays in the temporary prototype.

The isolated reconstruction prototype now represents preserved top-level
comments immediately before block commands, including empty paragraphs and
whitespace before a comment. Eight focused examples match native in interpreter
and JIT (`../lambda-input/temp/expl3-script/reparse-comment-tests-v2-interp.out`
and `reparse-comment-tests-v2-jit.out`). It still rejects comments in groups,
math, and argument gaps, and has not been installed. This narrows but does not
close the earlier reconstructed-comment issue.

## Script TeX passes unexpanded environment names through output — 2026-11-12

**Status:** FIXED for the tested computed environment names and emitted
closings. The full script TeX port remains incomplete; native production
parsing is unaffected.
S1.8 and D10.2.2 govern the data and input boundary.

For `\def\ename{quote}\begin{document}\begin{\ename}A\end{\ename}\end{document}`,
the script computes `quote` for its own environment state but emits the raw
`\ename` argument. Native expansion instead emits `\begin{quote}` and
`\end{quote}` when direct source echo cannot represent the expansion.
Macro-generated `\end` likewise requires synthetic output at its emission
position. Native emission uses source spans only for valid direct echoes.
The staged script records those spans and emits computed forms with the
current main-source offset. Evidence: six native text comparisons in
`../lambda-input/temp/expl3-script/envstage-env-output-interp.out` and its JIT
counterpart; existing expansion, profile and provenance goldens pass in both
backends. The separate main-file tail issue remains OPEN and its existing
golden is preserved.

The script now echoes an unchanged package declaration only when every named
package remains in the passthrough set. Script-handled `expl3` is consumed,
while mixed foreign, adapter and local packages retain their proper output.
`test/lambda/latex/test_tex_script_expl3_package_echo.ls/.txt` covers eight
cases. The previously failing `test_tex_script_expl3` golden and the new test
pass against the installed package in interpreter and JIT.

Environment echo spans and synthetic expanded names are now in the installed
script engine. Six native expanded-text comparisons are covered by
`test/lambda/latex/test_tex_script_environment_output.ls/.txt` and pass in both
backends. Existing expansion, profile, provenance, counters, package metadata
and related digest goldens pass in both backends. Small focused tests for both
changes pass with every-allocation GC and freed-memory poisoning in interpreter
and JIT (`../lambda-input/temp/expl3-script/env-package-gc-{interp,jit}.out`).
The independent trailing-source and full source-mapping issues remain OPEN.

## Script LaTeX digester rejects standalone math groups — 2026-11-12

**Status:** FIXED for supported standalone math groups. The full script TeX
port and other math constructs remain incomplete. Native production parsing is
unaffected. S1.8 and D10.2.2 apply to the document and input boundary.

For `\begin{document}${a+b}$\end{document}`, native creates a `group` inside
the `inline_math` AST, while the script digester returns
`latex-digest-unsupported` / `math atom`. Empty, nested, scripted and display
math groups fail similarly. `math_atoms` handles groups as arguments to
commands and scripts but has no branch for a group used as an ordinary atom.
Eight reproductions and native ASTs are in
`../lambda-input/temp/expl3-script/math-group-before-interp.out` and the JIT
counterpart. The independently tracked repeated-script issue is unaffected.

The script `math_atoms` now uses the existing group parser and applies any
following scripts to the resulting group. The installed
`test/lambda/latex/test_tex_script_math_groups.ls/.txt` compares eight empty,
nested, scripted, adjacent, radical and display groups with native; it passes
interpreter and JIT. Existing digest, underline, active-text and definition
goldens pass both tiers. The reconstruction prototype also matches four native
ASTs for comments inside those groups; it remains uninstalled.

The reconstructed-comment prototype now retains supported comment whitespace,
handles comments in tested math formulas, and explicitly rejects unqualified
argument/definition gaps. After these changes its prior 103-case corpus matches
100 native ASTs, with repeated scripts, unknown environment options and a
multiple-comment argument gap explicitly unsupported; see
`../lambda-input/temp/expl3-script/reparse-digest-tests-mathcomments-interp.out`
and its JIT counterpart. Further malformed-input, provenance and performance
qualification is required before production use.

## Script TeX rejects native pass-through and penalty primitives — 2026-11-12

**Status:** FIXED for the listed pass-through, penalty and unskip primitives.
Other TeX operations and downstream math digestion remain incomplete. Native
production is unaffected. S1.8 and D10.2.2 govern the document and input
boundary.

`\begin{document}\noindent!\end{document}` terminates script expansion with
`tex-unsupported-primitive` for `noindent`, while native emits the command
and continues. The same happens for `indent`, fill/filneg variants, alignment
markers, discretionary hyphen and italic correction; `penalty`, `unpenalty` and
`unkern` are also unsupported. Native `do_typesetting` passes the first group
as tokens, scans and discards a penalty's integer, and ignores the last two.
Eight reproductions in both backends are recorded in
`../lambda-input/temp/expl3-script/typesetting-primitives-before-interp.out`
and its JIT counterpart. This is about preserving native expansion behavior;
layout remains Radiant's responsibility under D10.2.2.

The same primitive audit found `\mathord`, `\displaystyle`, `\limits`,
`\left`, `\over`, and their native `P_MATH_EMIT` siblings are also rejected
by the installed script engine. Native expansion emits these commands as tokens;
it does not consume their math structure at this stage. The reproductions and
native expanded text are in `../lambda-input/temp/expl3-script/typeset-all-before-interp.out`
and the JIT counterpart. A staged implementation follows the shared native
pass-through branch, with downstream math AST support tracked separately.

The script now emits the same pass-through token families as native, scans and
discards penalties, and implements `\unpenalty`, `\unkern` and `\unskip`
with native output-token behavior. The new `test_tex_script_typesetting_tokens`
`.ls/.txt` compares 18 expansion cases with native, including
math commands, aliases, numeric expressions and whitespace removal. The new
suite passes installed in interpreter and JIT; existing expansion, profile,
expl3 and environment-output goldens pass both tiers. This does not claim
downstream LaTeX AST support for every emitted math command.

## Script math digester rejects style declarations and operatorname — 2026-11-12

**Status:** FIXED for the four style declarations and supported `operatorname`
forms. Other math syntax and source-provenance cases remain incomplete. Native
production parsing is unaffected; S1.8 and D10.2.2 apply.

After native-compatible math token emission, `\begin{document}$\displaystyle x+1$\end{document}`
reaches script digestion but returns `latex-digest-unsupported` /
`math command displaystyle`. Native emits a `style_command` node without an
argument. The other three math style declarations and `\operatorname` are
also unsupported, including the starred operator form. Six comparisons and
native ASTs are in `../lambda-input/temp/expl3-script/math-declarations-before-interp.out`
and its JIT counterpart. This is a digester issue independent of the newly
ported TeX token pass-through.

## Script math digester rejects ordinary punctuation atoms — 2026-11-12

**Status:** PARTIALLY FIXED for parentheses, brackets, comma, colon and vertical
bars. Other punctuation and multi-character relations remain incomplete. Native
production parsing is unaffected; S1.8 and D10.2.2 apply.

After staging support for `\operatorname`, the formula
`\begin{document}$\operatorname{foo}(x)$\end{document}` still returns
`latex-digest-unsupported` / `math atom` when the script digester encounters
`(`. Native creates `punctuation` nodes for the parentheses. The script's
`math_atoms` handles letters, numbers, a few operators and relations, but no
ordinary punctuation branch. Both backends reproduce this in
`../lambda-input/temp/expl3-script/math-declarations-after-interp.out` and
its JIT counterpart. Other punctuation and multi-character relation semantics
need individual validation rather than assuming all characters are alike.

## Script math digester rejects atom classes and mathchoice — 2026-11-12

**Status:** FIXED for supported atom-class bodies and four-branch mathchoice.
Malformed arguments and operator modifiers remain incomplete. Native production
is unaffected; S1.8 and D10.2.2 apply.

After the TeX token pass-through port, `\begin{document}$\mathord{x}$\end{document}`
still returns `latex-digest-unsupported` / `math command mathord`. Native
produces a `math_atom` with a `mord` class and grouped body. `\mathop` and
`\mathchoice` are similarly unsupported; native retains the operator body and
all four choice branches. Five examples and native ASTs are recorded in
`../lambda-input/temp/expl3-script/math-atom-commands-before-interp.out` and
the JIT counterpart. Additional malformed and modifier cases need separate
qualification.

The script math digester now emits native-style declaration nodes and
`operatorname` nodes, including direct adjacent starred forms; uncertain
star provenance remains explicitly unsupported. Parentheses, brackets, comma,
colon and vertical bars are parsed as punctuation atoms and can take scripts.
The installed `test_tex_script_math_declarations.ls/.txt` compares 13 cases
with native and passes interpreter and JIT. Existing digest, math-group,
math-style and underline goldens also pass both backends. Other punctuation,
relations and the independently recorded atom classes remain open.

The script math parser now retains the native atom class and body for
`\mathord`, `\mathbin`, `\mathrel`, `\mathopen`, `\mathclose`,
`\mathpunct`, and `\mathinner`, represents `\mathop`, and preserves all four
`\mathchoice` branches. A shared argument scanner also serves fractions.
The installed `test_tex_script_math_atom_classes.ls/.txt` compares nine native
ASTs and passes interpreter and JIT. Existing digest, math-group, declaration,
style and underline goldens pass both tiers. A focused native AST comparison
passes with forced GC and freed-memory poisoning in both backends; evidence is
`../lambda-input/temp/expl3-script/math-atoms-gc-{interp,jit}.out`.

## Script math digester rejects left/right delimiter groups — 2026-11-12

**Status:** PARTIALLY FIXED for supported scalable delimiter groups, including
nested and middle delimiters. Invalid middle delimiters and other malformed
forms remain explicitly unsupported. Native production parsing is unaffected;
S1.8 and D10.2.2 apply.

`\begin{document}$\left(x+1\right)$\end{document}` now passes script TeX
expansion but digestion returns `latex-digest-unsupported` / `math command left`.
Native builds a `delimiter_group` with its left/right spellings and parsed
children. Seven native AST comparisons, including nested, named, invisible,
scripted, middle and unclosed forms, are recorded in
`../lambda-input/temp/expl3-script/math-delimiters-before-interp.out` and its
JIT counterpart. Malformed and raw delimiter behavior requires qualification
before enabling a broad implementation.

The script now matches nested `\left`/`\right` pairs, parses supported
`\middle` delimiters, retains delimiter spellings and children, applies
following scripts, and covers tested native recovery and braced-right forms.
The installed `test_tex_script_math_delimiters.ls/.txt` compares 20 native ASTs
and asserts explicit rejection for an invalid middle delimiter. It passes both
interpreter and JIT; existing digest and math goldens pass both tiers. A focused
nested delimiter AST comparison also passes every-allocation GC and poisoning
in both backends (`../lambda-input/temp/expl3-script/math-delimiters-gc-*.out`).

## Script TeX rejects native mark and special handling — 2026-11-12

**Status:** FIXED for the tested native mark and special expansion behavior.
Malformed-input recovery and the broader script TeX port remain incomplete.
Native production is unaffected; S1.8 and D10.2.2 apply.

`\begin{document}A\special{color push}B\end{document}` stops script expansion
with `tex-unsupported-primitive` / `special`, while native expands and discards
the special's token list and continues with `AB`. `\mark`, indexed `\marks`,
and all five ordinary and indexed mark queries are likewise unsupported.
Native discards expanded mark contents, returns empty mark queries, and scans
an integer for indexed forms; it does not perform page building. Nine examples,
including nested expansion and aliases, are in
`../lambda-input/temp/expl3-script/mark-special-before-interp.out` and its JIT
counterpart. This concerns native expansion compatibility, not page layout.

The script now scans expanded mark/special text without executing its
unexpandable tokens, consumes the index for `\marks` and indexed queries,
and returns the native empty mark result. The installed
`test_tex_script_marks_special.ls/.txt` compares 12 native expanded outputs and
checks that discarded messages are not executed. It passes interpreter and JIT;
existing expansion, profile, expl3, token, environment and digest goldens pass
both tiers. A focused every-allocation-GC/poison check also passes both backends
(`../lambda-input/temp/expl3-script/marks-special-gc-{interp,jit}.out`).

## Script TeX rejects explicit glue and kern commands — 2026-11-12

**Status:** FIXED for the tested forms. Full script-port qualification remains
open; S1.8 and D10.2.2 apply.

`\begin{document}A\hskip1pt!B\end{document}` stops script expansion with
`tex-unsupported-primitive` / `hskip`. Native scans the glue and emits
`\hspace{1.0pt}` in this case. `\vskip`, `\kern`, `\mkern` and `\mskip`
are also unsupported; native uses math-sensitive command names, formatted
scanned values, and direct source echo when its trace is valid. Nine native
examples, including flexible glue, aliases, registers and expressions, are in
`../lambda-input/temp/expl3-script/spacing-command-before-interp.out`.
Successful implementation must preserve direct-echo spans and synthetic output
origins rather than replacing every case with a fixed spelling.


The script now scans and emits these glue and kern commands, retains captured
direct tokens and source spans when native echo is valid, and synthesizes
math-sensitive output otherwise. The installed
`test_tex_script_spacing_commands.ls/.txt` compares 23 native cases, including
comments, UTF-8 offsets, aliases, expressions, and a dimension longer than 64
tokens. It passes interpreter and JIT; focused forced-GC and freed-memory-poison
checks pass both. Existing expansion, stream, trace, profile, provenance, digest
and package tests pass with the staged candidate in both backends. Malformed
input recovery and release-profile performance qualification remain open.

## Script TeX dimension scanner lacks font-relative and pixel units — 2026-11-12

**Status:** FIXED for the tested units and supported font parameters. Full
script-port qualification remains open; S1.8 and D10.2.2 apply.

The script dimension scanner rejects `em`, `ex` and `px` as
`tex-illegal-unit`. For example, `\pdfpxdimen=2pt \dimen0=1.5px
\the\dimen0` yields `3.0pt` from the native TeX parser but script expansion
stops. A selected font's metrics likewise determine `em` and `ex`. The
reproduction and native comparisons are in
`../lambda-input/temp/expl3-script/font-unit-before-interp.out`. Native also
tries these units before physical units; partially matched keyword tokens are
restored as non-direct tokens, which can invalidate a source-echo trace. The
script currently omits those probes and preserves the direct flags on matched
prefixes. This additionally affects the spacing-command work recorded above.

The script scanner now reads the selected font's em/ex parameters and the
`pdfpxdimen` value, and restores partially matched keyword prefixes with the
native non-direct provenance. The installed `test_tex_script_font_units.ls/.txt`
compares ten native cases, including assigned and negative font metrics, pixel
values, case variants and math units. It passes interpreter and JIT, including
focused forced-GC and freed-memory-poison runs. The existing expansion golden also passes with the candidate in both backends.

## Script TeX keyword scanner rejects character category codes accepted by native — 2026-11-12

**Status:** FIXED for tested keyword characters. Full script-port
qualification remains open; S1.8 and D10.2.2 apply.

``\catcode`p=7 \dimen0=1pt \the\dimen0`` yields `1.0pt` from the native parser without a diagnostic, while the
script scanner returns `tex-illegal-unit`. The executable reproduction and output
are `../lambda-input/temp/expl3-script/unit-catcode-check.ls` and
`unit-catcode-check.out`. Native `scan_keyword` matches character values without
requiring letter/other categories; script `scan_word` currently requires those
categories. This also affects other keyword-using primitives, so changes need
regression checks beyond dimension scanning.


The script keyword scanner now matches character values without restricting
their category, in line with native `scan_keyword`. The installed
`test_tex_script_keyword_catcodes.ls/.txt` compares six native dimension, glue
and arithmetic cases and passes interpreter and JIT. Focused forced-GC and
freed-memory-poison tests, spacing comparisons, and existing expansion, stream,
trace, profile, digest, provenance and expl3 goldens pass in both backends.

## Script TeX rejects discretionary replacement text — 2026-11-12

**Status:** FIXED for tested well-formed groups. Full script-port qualification
remains open; S1.8 and D10.2.2 apply.

`\begin{document}A\discretionary{-}{z}{bc}D\end{document}` stops script
expansion at `tex-unsupported-primitive` / `discretionary`. Native expansion
consumes the first two unexpanded token groups and processes the third group,
producing `\begin{document}AbcD\end{document}`. A macro-containing case and
native output are in `../lambda-input/temp/expl3-script/discretionary-before.ls`
and `discretionary-before-interp.out`. The script implementation needs to
preserve token-list expansion and source provenance, and report unsupported
recovery where malformed input is not yet handled.


The script now scans three unexpanded token groups, discards the first two, and
executes stored replacement tokens with non-direct provenance. The installed
`test_tex_script_discretionary.ls/.txt` compares 13 native cases including
macros, nested groups, aliases, Unicode, assignments and messages. It passes
interpreter and JIT, as do focused forced-GC/poison and existing expansion,
stream, trace, profile, digest, provenance, expl3 and spacing checks. Missing
opening braces still return an explicit issue rather than native recovery.

## Script TeX rejects numeric accent primitive — 2026-11-12

**Status:** PARTIALLY FIXED for tested expansion and digest forms. Full script
TeX-port qualification remains blocked on unsupported accent arguments and
source reconstruction. Native production parsing is unaffected; S1.8 and
D10.2.2 apply.

`\begin{document}\accent19 e\end{document}` stops script expansion with
`tex-unsupported-primitive` / `accent`, while native emits `\'{e}`. The native
engine maps selected accent numbers to LaTeX commands, scans an optional
character or character definition, and emits the character unaccented for an
unknown accent number. Reproduction and examples are in
`../lambda-input/temp/expl3-script/accent-before.ls` and
`accent-before-interp.out`. Successful support needs native-compatible
character-number scanning, lookahead, and synthetic-output provenance.


The script now scans numeric accent and character arguments and emits the native
command mapping with synthetic origins. Installed `test_tex_script_accent.ls/
.txt` compares 26 native outputs and diagnostic records in interpreter and JIT;
focused forced-GC/poison checks and existing character, diagnostic, expansion,
profile, digest, provenance, stream and expl3 goldens pass both. The script digester now builds supported text accent nodes, including the
first example, and the installed `test_tex_script_accent_digest.ls/.txt`
compares 28 native ASTs and renders `é à č` to HTML. It passes interpreter and
JIT, as do focused forced-GC/poison and math, underline, digest, provenance and
package regressions. Comment-adjacent arguments, control-sequence arguments and
certain emitted syntax characters return explicit unsupported results; native
behavior for comment gaps is documented in
`../lambda-input/temp/expl3-script/accent-digest-probe-v3-interp.out`. General
emitted-source reconstruction and full accent coverage remain open.


A staged control-argument candidate matches native for `\'\i!`, `\'\%`,
and nested accents (`../lambda-input/temp/expl3-script/accent-args-probe-interp.out`).
The installed `test_tex_script_accent_digest` golden explicitly asserts that
the first two forms are unsupported, so the checkout rule preserving existing
assertions and reference results prevents installing that behavior without an
explicit user ruling. The candidate remains in
`../lambda-input/temp/expl3-script/accent-control-candidate-home`; nested accents
are handled independently.



Nested accents are now supported by a shared accent-node parser. The installed
`test_tex_script_nested_accents.ls/.txt` compares six native ASTs and checks the
remaining comment-gap rejection; it passes interpreter and JIT, as do the
existing accent golden, math, digest and package regressions and focused
forced-GC/poison checks. The separate control-argument limitation remains.

## Script TeX rejects insert and vertical adjustment groups — 2026-11-12

**Status:** PARTIALLY FIXED for tested groups. Full script-port qualification
remains open; S1.8 and D10.2.2 apply.

`\begin{document}A\insert0{hidden}B\end{document}` stops script expansion
with `tex-unsupported-primitive` / `insert`; `\vadjust` similarly stops. Native
runs the group with ordinary scoping and then discards its output, while global
assignments still take effect. Three native comparisons are in
`../lambda-input/temp/expl3-script/insert-before.ls` and
`insert-before-interp.out`. A correct port must execute the body, manage group
scope and aftergroup actions, and remove only the produced output; simply
skipping group tokens would lose side effects.


The script now executes these groups with normal scoping, preserves global and
aftergroup effects, and discards their emitted output at group close. Installed
`test_tex_script_insert.ls/.txt` compares 24 native outputs including nested
groups, macros, math, messages, and an early document end. It passes interpreter
and JIT, as do focused forced-GC/poison and existing expansion, discretionary,
spacing, digest, provenance, math and package regressions. Cross-boundary
`\unskip` returns an explicit unsupported issue, and malformed-opening-brace
recovery is incomplete (`../lambda-input/temp/expl3-script/insert-rejection-probe.out`).

## Script TeX rejects rule primitives — 2026-11-12

**Status:** PARTIALLY FIXED for tested rule forms. Full script-port
qualification remains open; S1.8 and D10.2.2 apply.

`\begin{document}A\hrule width1pt height2pt depth3pt!B\end{document}`
stops script expansion at `tex-unsupported-primitive` / `hrule`; native emits
`\rule{1.0pt}{5.0pt}`. A macro-generated `\vrule` emits the native default
`\rule{0.4pt}{1em}`. Native scans optional dimension keywords in any order,
uses source echo when its trace is valid, and otherwise synthesizes a rule.
Reproductions are in `../lambda-input/temp/expl3-script/rule-before.ls` and
`rule-before-interp.out`. The script port needs correct scanning, echo spans,
and synthetic source origins.


The script now scans and emits rules with native source echo or synthesized
output. Installed `test_tex_script_rules.ls/.txt` compares 19 native expanded
outputs, and `test_tex_script_rule_digest.ls/.txt` compares 17 native ASTs and
a rendered HTML rule. Both pass interpreter and JIT, as do focused forced-GC/
poison checks and existing insert, spacing, digest, math and package regressions.
An echoed rule containing a comment can lose whitespace in direct digestion;
the script explicitly reports `rule comment source gap` instead. The mismatch
and rejection are reproduced in
`../lambda-input/temp/expl3-script/rule-digest-stage-check-interp.out` and
`../lambda-input/temp/expl3-script/rule-comment-gap-probe.out`. General
comment reconstruction and malformed dimension recovery remain open.

## Script TeX rejects leader primitives — 2026-11-12

**Status:** PARTIALLY FIXED for rule and inline-box leaders. Full script-port
qualification remains open; S1.8 and D10.2.2 apply.

`\begin{document}A\leaders\hrule\hskip2cm!B\end{document}` stops script
expansion with `tex-unsupported-primitive` / `leaders`. Native consumes the
rule specification and then processes the following glue, emitting
`\hskip2cm`. A second case using `\cleaders\vrule` is in
`../lambda-input/temp/expl3-script/leaders-before.ls` and
`leaders-before-interp.out`. Native also permits box leaders, executing and
discarding their box contents before processing the following glue. The script
port needs both rule and box behavior, with scoped execution and source fidelity.


The script now consumes rule leaders and executes/discards `hbox`, `vbox` and
`vtop` leader bodies, including their hooks, scoping and global effects. The
installed `test_tex_script_leaders.ls/.txt` compares 24 native outputs and
passes interpreter and JIT; focused forced-GC/poison and existing rule, insert,
digest, math and package regressions pass both. Leader boxes obtained from
registers, copies or splits remain explicitly unsupported, as does malformed
box recovery. Box-leader side effects were reproduced in
`../lambda-input/temp/expl3-script/leader-box-before-interp.out`.

## Script TeX rejects inline box primitives — 2026-11-12

**Status:** PARTIALLY FIXED for tested inline boxes. Full script-port
qualification remains open; S1.8 and D10.2.2 apply.

`\begin{document}A\hbox{Hi}B\end{document}` stops script expansion at
`tex-unsupported-primitive` / `hbox`; native emits `\mbox{Hi}`. A vertical box
emits a grouped body. Reproductions are in
`../lambda-input/temp/expl3-script/inline-box-before.ls` and
`inline-box-before-interp.out`. Correct support must scan box specifications,
execute scoped bodies and box hooks, and preserve synthetic-output origins.
Box registers, copying, splitting and movement require additional work.


A staged inline-box expansion matches native output in the initial cases. Its
direct vertical-box digester currently loses a space after a skipped comment:
`\vbox{a%note\n b}` yields `ab` where native yields `a b` (reproduction in
`../lambda-input/temp/expl3-script/inline-box-digest-probe.out`). Synthetic
horizontal boxes also lack command source mapping. Both need explicit handling
or rejection before installing the expansion.


A category-code probe found an additional horizontal-box argument mismatch:
when `$` has category 12, native records escaped `\$` in the argument's raw
source but a first candidate recorded `$`. The staged digester now rejects
this source shape until token serialization is available. Before and after
evidence is `../lambda-input/temp/expl3-script/inline-box-catcode-probe.out`
and `inline-box-catcode-probe-v2.out`.


The script now expands inline boxes with scoped bodies and hooks, and digests
supported simple horizontal and vertical boxes. Installed
`test_tex_script_inline_box.ls/.txt` compares 17 native expanded outputs;
`test_tex_script_inline_box_digest.ls/.txt` compares 13 native ASTs and renders
HTML. Both pass interpreter and JIT, as do focused forced-GC/poison and existing
leader, rule, insert, math and package regressions. Comment-sensitive vertical
boxes, macro-expanded horizontal bodies and special category-code characters
return explicit unsupported results pending source reconstruction
(`../lambda-input/temp/expl3-script/inline-box-unsupported-probe.out`,
`inline-box-catcode-probe-v2.out`).

## Script TeX rejects box registers and retrieval — 2026-11-12

**Status:** OPEN. **Full script TeX-port blocker:** yes, for `\setbox`,
`\box`, `\copy` and related operations. Native production parsing is unaffected;
S1.8 and D10.2.2 apply.

`\setbox0=\hbox{Hi}\begin{document}A\copy0!\box0!\copy0 B\end{document}`
stops script expansion at `tex-unsupported-primitive` / `setbox`. Native stores
the box body, copies it once, consumes it once, and leaves the final copy empty.
A related `\ifvoid` example and output are in
`../lambda-input/temp/expl3-script/box-register-before.ls` and
`box-register-before-interp.out`. The port needs group-scoped box assignment,
copy versus consume semantics, box conditionals, hooks and source provenance;
splitting, unboxing and movement also remain to be qualified.


An initial register candidate matched the tested output and scope cases, but
replaying `\setbox0=\hbox{a%note\n b}` into the script digester produced
`ab` where the native AST contains `a b`. Evidence is
`../lambda-input/temp/expl3-script/box-register-digest-probe.out`. Box replay
needs to preserve or explicitly reject unqualified source gaps before the
register implementation can be installed.


Further staged AST comparisons found a definition argument ending at the wrong
source offset when a box assignment separates it from the next emitted input,
and a synthesized rule replay whose final argument end should map to the next
emitted source segment. The output text matches native; ASTs differ. Evidence is
`../lambda-input/temp/expl3-script/box-register-digest-probe-v3.out`. General
definition reconstruction is already incomplete; the candidate must reject
unqualified definition/replay combinations and fix or reject the rule mapping.


The definition-offset mismatch also occurs with an empty assigned box and no
replay: `../lambda-input/temp/expl3-script/box-assignment-definition-probe.out`
shows native mapping the definition argument end to the next emitted document
segment, while the candidate used the original closing brace. The staged
digester must account for assignments, not just replay markers, when it reports
this unqualified reconstruction.


A staged register candidate now matches 26 native expanded-output examples and
eight native AST examples in interpreter and JIT, with a focused forced-GC/
poison check. Existing `test_tex_script_expansion.ls:134` and
`test_tex_script_profile.ls:13` explicitly use `\setbox0=\hbox{}` as an
unsupported primitive and failed preload. Their unchanged `.txt` references
therefore fail in both backends when the candidate correctly accepts it. The
checkout rule requires preserving assertions and reference results, so this
blocks installation without an explicit user ruling; no workaround or golden
change was made. The candidate and failing outputs remain at
`../lambda-input/temp/expl3-script/box-register-stage-home` and
`box-register-regression-{expansion,profile}-{interp,jit}.out`. All other
focused regressions passed. Comment and definition source gaps are conservatively
rejected by the candidate; broader source reconstruction remains open.

## Script TeX rejects box dimension access — 2026-11-12

**Status:** OPEN. **Full script TeX-port blocker:** yes, for `\wd`, `\ht` and
`\dp`. Native production parsing is unaffected; S1.8 and D10.2.2 apply.

With the staged box-register implementation,
`\setbox0=\hbox{Hi}\setbox1=\copy0\wd1=2pt` stops at
`tex-unsupported-primitive` / `wd`. Native reports both `\wd0` and `\wd1` as
`2.0pt`, because the copied registers refer to the same box object. Evidence is
`../lambda-input/temp/expl3-script/box-dimensions-before.ls` and
`box-dimensions-before-interp.out`. Correct support needs native-compatible
shared-box mutation, scanning, scoped references, and dimension diagnostics.


## Script TeX rejects paragraph pattern and shape assignments — 2026-11-12

**Status:** PARTIALLY FIXED for well-formed expansion. AST source mapping,
some quantity-read ASTs and malformed-input recovery remain open; S1.8 and D10.2.2
apply.

The script profile stops with `tex-unsupported-primitive` for `\patterns`,
`\hyphenation`, `\parshape`, and `\interlinepenalties`. For example,
`\begin{document}A\patterns{a1b}B\end{document}` expands natively to
`\begin{document}AB\end{document}`. Reproduction and native outputs are in
`../lambda-input/temp/expl3-script/paragraph-assign-before.ls` and
`paragraph-assign-before.out`. The native engine consumes the pattern text
without expansion and scans shape dimensions or penalty integers without storing
layout state; parity requires preserving scanning and assignment side effects.

An experimental implementation matches 25 native expansion cases, but its
initial AST comparisons expose three source-mapping failures: a prior echoed
definition gets the wrong end offset, a nested formatting argument loses a
`\hyphenation` node, and math source includes a consumed penalty assignment.
See `../lambda-input/temp/expl3-script/paragraph-digest-probe-interp.out` for
actual and native trees. These AST mismatches block claiming complete support;
the digester must reject affected inputs until source reconstruction is fixed.
Malformed numeric and brace recovery remains separate from the well-formed
assignment subset.

The script implementation scans pattern and hyphenation text without executing
it and consumes paragraph-shape dimensions and the four penalty lists, including
prefixes and `\afterassignment`. The installed
`test_tex_script_paragraph_assignments.ls/.txt` compares 25 text and diagnostic
cases with native behavior and checks that pattern contents do not execute. Both
backends pass, as do 23 focused regressions (the fixture-dependent test was
rerun after correcting its isolated working directory) and sampled forced GC
with freed-memory poisoning. To avoid emitting incorrect ASTs, digestion now
explicitly rejects documents that execute these assignments until the mapping
gaps above are repaired. `\the\parshape` and `\the\clubpenalties1` were unsupported while native
returns zero; the original reproduction is
`../lambda-input/temp/expl3-script/paragraph-query-probe.ls` and `.out`.

The Script engine now reads those quantities in `\the`, numeric expressions,
conditionals and dimension factors, scanning the required penalty index. The
new `test_tex_script_paragraph_quantities.ls/.txt` compares 20 expansion and
diagnostic cases, and three supported ASTs, against native in interpreter
and JIT. Both backends also pass 24 focused regressions and sampled forced GC
with freed-memory poisoning. Math with a generated quantity and combinations
with echoed definitions report unsupported where source mapping remains
unqualified; assignment-containing documents retain the broader guard above.


## Script LaTeX definition offsets omit following consumed assignments — 2026-11-12

**Status:** OPEN. Blocks AST parity for affected documents; S1.8 and D10.2.2
apply.

With `\def\x{Hi}\count0=0\begin{document}A\x B\end{document}`, Script
digestion reports the definition body's argument end as byte 10, while native
reports byte 19, the start of the next emitted segment. `\dimen0=1pt` similarly
reports 10 versus 21. The Script result is marked successful despite the AST
mismatch. Reproduction and both trees are in
`../lambda-input/temp/expl3-script/definition-assignment-mapping-probe.ls` and
`.out`. A candidate for paragraph quantity reads also exposes this existing
problem; the issue appears to be incomplete reconstruction of the native source
map across consumed assignments. Exact general cause remains unconfirmed.


## Script TeX rejects font hyphen and skew characters — 2026-11-12

**Status:** PARTIALLY FIXED for direct font identifiers. Family identifiers,
AST source mapping and malformed-input recovery remain open; S1.8 and D10.2.2
apply.

`\the\hyphenchar\nullfont` and `\the\skewchar\nullfont` stop Script
expansion at `tex-unsupported-primitive`; native emits `45` and `-1`. Font
property assignments such as `\hyphenchar\f=63` likewise stop, whereas
native updates the selected font and reads back `63`. Reproduction is
`../lambda-input/temp/expl3-script/font-character-before.ls` and `.out`.
The implementation needs native font identity, defaults, property mutation,
scoping and quantity scanning rather than treating the properties as text.

A staged candidate matches 20 native expansion and diagnostic examples,
including shared font identity, defaults and group behavior. Its initial AST
probe produces incorrect successful trees for an assignment inside a formatting
argument and for an assignment following an echoed definition; see
`../lambda-input/temp/expl3-script/font-character-digest-probe.out`. The latter
also occurs for ordinary register assignments (the separate definition-offset
issue above). The candidate must report unsupported for unqualified AST source
mappings; full source reconstruction remains open.


Script now tracks these properties on shared font identities, preserves native
defaults for new fonts and nullfont, and supports assignment and reads through
`\the`, numeric expressions, conditions and dimension factors. The new
`test_tex_script_font_characters.ls/.txt` compares 20 expansion/diagnostic
cases and two supported ASTs to native in interpreter and JIT. Both backends
pass 26 focused regressions and sampled forced GC with poisoning. Digestion
explicitly rejects property assignments or unqualified definition combinations,
and math source mapping remains unsupported. Font-family identifiers are
tracked separately below.

## Script TeX rejects math font family assignments and identifiers — 2026-11-12

**Status:** PARTIALLY FIXED for family assignments and font-identifier scans.
Direct font-token `\the`, AST source mapping and malformed-input recovery
remain open; S1.8 and D10.2.2 apply.

`\textfont0=\f` and `\scriptfont2=\f` stop Script expansion at
`tex-unsupported-primitive`; native accepts them. With the staged font-property
work, `\the\hyphenchar\textfont0` reports `tex-missing-font`, while native
reads the default null font and emits `45`. Reproduction and native output are
`../lambda-input/temp/expl3-script/font-family-before.ls` and `.out`. Correct
support needs family-slot assignment, index scanning and font identifiers in
numeric and `\the` contexts, including font identity and source mapping.

A staged family-slot candidate matches 16 native expansion and diagnostic
examples, including index wrapping, aliases, shared font identity and global
effects. Removing its provisional AST guard reproduces incorrect successful
trees when a family assignment occurs inside formatting or after a definition;
see `../lambda-input/temp/expl3-script/font-family-digest-probe.out`. These
source-mapping cases must remain explicitly unsupported until reconstruction
is qualified. Direct `\the\textfont` font-token production is not yet ported.


Script now scans and assigns all three math font families, including wrapped
indices, aliases, shared font identity and native group behavior. It resolves
family identifiers for font properties and font dimensions. The new
`test_tex_script_font_families.ls/.txt` compares 16 expansion/diagnostic cases
and two supported ASTs with native in interpreter and JIT; 27 focused
regressions and sampled forced GC with poisoning pass both. Family assignments
explicitly reject AST digestion while source mapping remains unqualified.
Direct font-token production through `\the` remains the separate issue below.

## Script TeX cannot expand font identifiers through the — 2026-11-12

**Status:** PARTIALLY FIXED for expansion and experimental text reconstruction.
Font-token AST source mapping and production integration remain open; S1.8
and D10.2.2 apply.

`\the\font`, `\the\nullfont` and `\the\f` stop Script expansion as
unsupported, while native produces the font's original control-sequence token.
For example, after `\font\f=cmr10 at 13pt`, `\edef\x{\the\f}` stores
`\f`, not the literal font name. Redefining that control sequence affects
subsequent use of the returned token. Reproduction and native output are
`../lambda-input/temp/expl3-script/font-the-before.ls`, `font-the-before.out`,
and `font-family-the-probe.out`. Correct support needs original font identifier
identity and `\the` token-list expansion semantics, including `\edef`.

The staged candidate in `../lambda-input/temp/expl3-script/font-the-stage-home`
returns the original font token for direct use, but `\edef` cases expose two
additional mismatches: the echoed definition is lost when tracing sees the
generated font token, and in-document `\meaning` emits a raw backslash where
native serializes a text-backslash command. Sixteen comparisons are in
`test_tex_script_font_tokens.ls`, with actual/native output in
`font-token-mismatch-probe.out`. That earlier candidate remained uninstalled while the general source-trace and
text serialization causes were investigated.


The Script engine now preserves original font control-sequence identity for
`\the` of named, current, null and math-family fonts, including `\edef` and
later redefinitions. Generated tokens are distinguished in source tracing.
`test_tex_script_font_tokens.ls/.txt` compares 17 native text/diagnostic cases
and verifies safe AST handling in interpreter and JIT. The experimental
`reconstruct.ls` is used for those text comparisons; the existing digester
returns unsupported for font-token ASTs until mapping is qualified.

## Script LaTeX meaning output does not serialize backslash characters — 2026-11-12

**Status:** PARTIALLY FIXED in the experimental reconstructor. Production
integration and complete source-map qualification remain open; S1.8 and
D10.2.2 apply.

For `\def\x{\y}\begin{document}\meaning\x\end{document}`, Script
expansion emits a raw backslash before `y`, while native serializes it as
`\textbackslash{}y`. The Script result is marked successful. A macro meaning
with only ordinary characters matches. Reproduce with
`../lambda-input/temp/expl3-script/meaning-serialization-probe.ls` and `.out`.
The evidence points to generated character serialization in the LaTeX profile;
the exact general cause and all affected character categories are unconfirmed.


The experimental Script `reconstruct.ls` canonicalizes generated characters
and matches native output for the reproduction above. It is a separate API,
not yet the production LaTeX path; direct expanded tokens remain tokens.
The new `test_tex_script_reconstruction.ls/.txt` checks 91 native text cases
in both backends, including this one, comments, boxes, rules, accents and
assignments. Offset-map and full-corpus qualification remain open.

## Script TeX reconstruction loses numeric accent text segments — 2026-11-12

**Status:** FIXED for tested numeric accent reconstruction in the experimental
Script reconstructor. Full reconstruction qualification remains open; S1.8
and D10.2.2 apply.

Native `\accent19 \char123` and `\accent19 \char92` emit a single raw
text segment, including the character. The staged Script reconstructor treats
the characters as individual synthetic tokens and escapes them, producing
`\'{\{}` and `\'{\textbackslash{}}` rather than the native output.
Reproduction, all 51 comparisons and actual/native text are in
`../lambda-input/temp/expl3-script/reconstruct-conformance-b.ls` and
`reconstruct-conformance-b-interp.out`. The native `do_accent` calls
`emit_text`; the script must retain that segment boundary and origin.


The Script expansion output now retains native accent text-segment boundaries
and the experimental reconstructor emits them as one segment. Both examples
are included in the 91-case native comparison test.

## Script LaTeX font-token expansion loses nested and math source mapping — 2026-11-12

**Status:** OPEN in the staged font-token implementation. Blocks AST parity
for documents using font-valued `\the`; S1.8 and D10.2.2 apply.

The staged engine correctly reconstructs expansion text for direct
`\the\nullfont`, but its AST silently differs from native when that command
appears in math or a formatting argument. See
`../lambda-input/temp/expl3-script/font-token-digest-probe.ls` and `.out` for
actual and native trees. Source mapping for font-token expansion must report
unsupported until the native digestion behavior is ported or reconstructed.


## Script TeX executes assignments inside constructor arguments — 2026-11-12

**Status:** OPEN. Blocks script-port output, state and AST parity for affected
documents; S1.8 and D10.2.2 apply. Production native parsing is unaffected.

For `\count0=0\begin{document}\textbf{\global\count0=7 X}\the\count0\end{document}`,
the Script engine executes the assignment, removes it from the output and
emits `7`; native leaves the assignment in the constructor argument and emits
`0` afterwards. Optional constructor arguments have the same defect. The
`\hyphenation` nested formatting discrepancy and several font-related AST
mismatches arise from this difference. Actual/native text and controls are in
`../lambda-input/temp/expl3-script/constructor-arg-mode-before.ls` and `.out`.
Native `arg_mode` in `input-tex-main.cpp` expands macros in constructor
arguments while passing unexpandable primitives and assignments through, with
special handling for nested constructors, environments and raw arguments.
Correct support must preserve those semantics and source provenance.

A staged constructor-argument implementation in
`../lambda-input/temp/expl3-script/constructor-arg-stage-home` matches the four
reproductions above, including state and optional arguments. Its unchanged
regressions for `test_tex_script_paragraph_assignments`,
`test_tex_script_font_characters`, and `test_tex_script_font_families` now
fail because those existing references explicitly expect unsupported ASTs for
these nested cases. The corrected candidate returns native-matching ASTs for
all three; the paragraph AST was compared directly in interpreter and JIT in
`../lambda-input/temp/expl3-script/constructor-paragraph-ast.ls` and `.out`. Failing comparisons are
`constructor-arg-regression-{paragraph_assignments,font_characters,font_families}-interp.out`;
the paragraph failure also reproduces in JIT. Checkout rules require preserving
test assertions and reference results, so this blocks installation without a
user ruling. The candidate also still needs complete environment/raw-command
handling and qualification; no golden was changed.

The same state error also occurs in arguments of unknown passthrough commands:
`\unknown{B\global\count0=7 C}` executes the assignment in Script, while
native preserves it and leaves the register unchanged. Evidence is
`../lambda-input/temp/expl3-script/unknown-arg-before.ls/.out`. Native main
control applies argument mode to both undefined controls and constructors. The
existing staged candidate matches this example in the interpreter; see
`../lambda-input/temp/expl3-script/unknown-arg-staged.out`. It still needs
rebasing, JIT qualification, and environment and raw-argument handling.

The rebased prototype at `../lambda-input/temp/expl3-script/constructor-current-stage-home`
also handles tested passthrough environment options and unknown commands in
inline math; see `constructor-current-probe2.out`. It retains passthrough
nested environment names and explicitly rejects unqualified raw commands and
custom environments inside an argument. The latter is blocked by input issue
[LI-017](../../../lambda-input/vibe/Lib_Input_Issue_Ledger.md#li-017), which causes
native to emit its internal environment-end marker. Test evidence is
`constructor-env-probe.ls`, `.out`, and `constructor-env-probe2.out` in the
same temporary directory. Interpreter and JIT now agree on 17 of 20 combined
LaTeX probes; the other three explicitly reject raw commands or LI-017's custom
environment. Four plain-TeX cases also match in both backends; see
`constructor-pure-probe2.out` and `constructor-pure-probe2-jit.out`. The
candidate remains uninstalled, and the reference result conflict described
above remains unresolved.

The rebased stage initially lost main-file origin on a synthetic opening brace
read as a constructor argument from a `\read` stream, causing
`tex-reconstruct-missing-origin` in `script_read_scope`; evidence is
`constructor-read-detail2.out` in the same temporary directory. A shared
emission-origin helper now annotates such delimiters, and the existing nine-case
`test_tex_script_read` matches its preserved reference in both backends
(`constructor-read-fixed2-interp.out` and `constructor-read-fixed2-jit.out`).

A further rebase at `../lambda-input/temp/expl3-script/constructor-verb-stage-home`
uses the installed direct-source `\verb` reader inside constructor arguments.
Nine native comparisons cover braces, comments, optional arguments, Unicode,
starred and aliased forms in interpreter and JIT; evidence is
`constructor-verb-probe.ls` and `constructor-verb-probe-{interp,jit}.out`.
Nine focused existing references, including `\read` and `\verb`, pass in both
backends; the three paragraph/font references still fail only because they
expect unsupported results. Logs and preserved actual outputs are under
`../lambda-input/temp/expl3-script/constructor-verb-regressions-{interp,jit}.log`
and `constructor-verb-regression-{interp,jit}/`. The candidate still
explicitly rejects custom environments affected by LI-017 and remains
uninstalled because of the preserved-reference conflict.

The latest rebase at `../lambda-input/temp/expl3-script/constructor-random-stage-home`
retains the installed identity and deterministic-random features. Nine further
native comparisons cover random expansion and seed assignments, `\jobname`,
`\fontname`, `\the` and inline `\verb` inside arguments; all match in both
backends. Evidence is `constructor-random-probe.ls` and
`constructor-random-probe-random-{interp,jit}.out` in the same directory.
The prior command and inline-verb probes also continue to match.

The current rebase at `../lambda-input/temp/expl3-script/constructor-shipout-stage-home`
also matches six native cases in both backends where movement or shipout appears
inside arguments, including nested assignments and verbatim content. Evidence is
`constructor-shipout-probe.ls` and `constructor-shipout-probe-{interp,jit}.out`.
The staged fix remains subject to the preserved-reference and LI-017 limits.


## Script TeX rejects letterspacefont alias assignments — 2026-11-12

**Status:** PARTIALLY FIXED for well-formed assignments outside constructor
arguments. AST source mapping and malformed-input recovery remain open; S1.8
and D10.2.2 apply.

`\font\f=cmr10 at 13pt\letterspacefont\g\f 100` stops the Script
profile at `tex-unsupported-primitive`, while native defines `\g` as the
same font identity as `\f`. Native reads its sixth font dimension as
`13.0pt` and `\ifx\f\g` succeeds; nullfont aliases also work. Reproduction
and native outputs are in `../lambda-input/temp/expl3-script/letterspacefont-before.ls`
and `.out`. Native scans and discards the spacing integer, preserving font
identity; Script support needs that scanning, scope and source handling.


Script now scans the font and spacing expression and assigns a scoped alias to
the existing font identity. The new `test_tex_script_letterspacefont.ls/.txt`
compares 18 native text and diagnostic cases and checks safe AST outcomes in
both backends, including global definitions and `\afterassignment`. Thirteen
focused regressions and sampled forced GC with poisoning pass both. AST
digestion explicitly reports unsupported for assignments, because unguarded
nested and definition cases differ from native; see
`../lambda-input/temp/expl3-script/letterspacefont-digest-probe.out`. The
separate constructor-argument defect affects assignments inside arguments.

## Script TeX rejects interaction and page-state primitives — 2026-11-12

**Status:** PARTIALLY FIXED for well-formed assignments and queries outside
constructor arguments. AST source mapping and malformed-input recovery remain
open; S1.8 and D10.2.2 apply.

`\the\interactionmode`, `\interactionmode=3`, `\batchmode`,
`\the\pagegoal`, and `\pagegoal=1pt` stop Script expansion at
`tex-unsupported-primitive`. Native currently reports interaction mode `1` and
page dimensions `0.0pt` even after assignment, because its input engine does
not model interaction state or a page builder; it still scans assignment
values. Evidence is `../lambda-input/temp/expl3-script/page-state-before.ls`
and `.out`. Script parity must scan values and preserve assignment side effects
and diagnostics rather than omit the commands or their inputs.


Script now models the native engine's fixed interaction/page results and scans
assignment values, including prefixes, aliases and `\afterassignment`. The
new `test_tex_script_page_state.ls/.txt` compares 23 native text/diagnostic
cases and checks safe AST outcomes in both backends. Fourteen focused
regressions and sampled forced GC with poisoning pass both. Unguarded nested
and definition assignments produce incorrect successful ASTs; see
`../lambda-input/temp/expl3-script/page-state-unguarded-probe.out`. The
digester reports unsupported for those unqualified assignment mappings, and
the separate constructor-argument defect affects nested assignments.

## Script expl3 rejects token-list append and prepend APIs — 2026-11-12

**Status:** PARTIALLY FIXED for well-formed `Nn` and `Nx` variants outside
constructor arguments. AST source mapping and invalid-target recovery remain
open; S1.8 and D10.2.2 apply.

With script expl3 enabled, `\tl_put_right:Nn`, `\tl_put_left:Nn` and
`\tl_put_right:Nx` stop at `tex-unsupported-primitive`; native produces
`AB`, `BA`, and an expanded token-list result respectively. Reproduction
and native text are in `../lambda-input/temp/expl3-script/expl3-tl-put-before.ls`
and `.out`. Support needs token-level concatenation, local/global scope,
`n` versus `x` expansion and source/diagnostic behavior.


A staged implementation matches 20 native text and diagnostic cases in both
backends. Unguarded digestion still returns incorrect successful ASTs for
nested constructor arguments and assignments following definitions; see
`../lambda-input/temp/expl3-script/expl3-tl-put-digest-probe.out`. The staged
digester must report unsupported for these unqualified source mappings.

Script now supports local/global append and prepend, preserving token bodies
and `n` versus `x` expansion. `test_tex_script_expl3_tl_put.ls/.txt` compares
20 native text/diagnostic cases and checks safe AST handling in interpreter and
JIT. Thirteen focused regressions and sampled forced GC with poisoning pass
both. The digester explicitly rejects token-list mutations until source mapping
is qualified; constructor arguments remain a separate issue.

## Script expl3 omits standard scratch token lists — 2026-11-12

**Status:** FIXED for the four standard local/global scratch token lists.
Other expl3 initialization remains incomplete; S1.8 and D10.2.2 apply.

With script expl3 enabled, `\tl_use:N\l_tmpa_tl` and
`\tl_use:N\l_tmpb_tl` fail with `expl3-undefined-variable`; native expl3
provides these as initially empty token lists. Reproduction and native text
are in `../lambda-input/temp/expl3-script/expl3-scratch-before.ls` and `.out`.
Correct initialization must preserve standard scope and duplicate-declaration
behavior rather than treating every missing token list as empty.


Script expl3 initialization now provides `\l_tmpa_tl`, `\l_tmpb_tl`,
`\g_tmpa_tl` and `\g_tmpb_tl` as empty token lists, without treating other
undefined names as empty. The new token-list test covers all four and scoped
mutation; existing expl3 tests continue to pass in both backends.

## Script expl3 rejects token-list emptiness conditionals — 2026-11-12

**Status:** PARTIALLY FIXED for tested `N` and `n` branch variants. AST
source mapping and malformed arguments remain open; S1.8 and D10.2.2 apply.

`\tl_if_empty:NTF` and `\tl_if_empty:nTF` stop Script expansion at
`tex-unsupported-primitive`, while native selects the true branch for an empty
list and the false branch for a nonempty one. Reproduction and native output
are in `../lambda-input/temp/expl3-script/expl3-tl-empty-before.ls` and `.out`.
The implementation must scan both branches, expand only the selected branch,
and preserve token-list and grouping semantics.

The staged conditional initially chose correct branches but echoed an `\edef`
that native suppresses after expanding expl3's internal macro tokens. Both `N`
and `n` cases reproduce this in
`../lambda-input/temp/expl3-script/expl3-tl-empty-trace-probe.out`. Source
tracing must model that internal expansion origin; the uncorrected staged
result is not qualified.

A second staged probe found that checking only the token-list body is
insufficient for `N` conditionals: native compares the complete TeX meaning
to the empty token-list meaning. Empty long/protected macros, macros with
parameter prefixes, `\relax` and undefined controls select false. Evidence
is `../lambda-input/temp/expl3-script/expl3-tl-empty-meaning-probe.out` and
`expl3-tl-empty-meaning-native.out`. The candidate must compare meanings.

An unguarded staged AST comparison also returns an incorrect successful
definition-offset tree after a token-list conditional; see
`../lambda-input/temp/expl3-script/expl3-tl-empty-digest-probe.out`. The
conditional must remain guarded in the current digester until source mapping
is qualified.


Script now provides `NTF`/`NT`/`NF` and `nTF`/`nT`/`nF` variants, comparing
complete TeX meanings for `N` and unexpanded tokens for `n`. Internal
macro-expansion provenance is retained for definition tracing. The new
`test_tex_script_expl3_tl_empty.ls/.txt` compares 31 native text/diagnostic
cases and checks safe AST outcomes in both backends. Fourteen focused
regressions and sampled forced GC with poisoning pass both. AST digestion
explicitly reports unsupported until conditional source mapping is qualified.

## Script expl3 rejects token-list blankness conditionals — 2026-11-12

**Status:** PARTIALLY FIXED for `n` and `V` branch variants. AST source
mapping and malformed arguments remain open; S1.8 and D10.2.2 apply.

`\tl_if_blank:nTF` stops Script expansion at `tex-unsupported-primitive`.
Native selects true for a token list consisting only of spaces, false for a
letter or for an unexpanded control sequence even if that macro expands to
empty. Evidence is `../lambda-input/temp/expl3-script/expl3-tl-blank-before.ls`
and `.out`. Correct support requires token-category-aware whitespace tests and
normal conditional branch and source-trace behavior.

The staged `nTF`/`nT`/`nF` implementation matches 17 native text and
diagnostic examples. Without the existing conditional source-map guard, an
conditional following an echoed definition yields an incorrect successful AST;
see `../lambda-input/temp/expl3-script/expl3-tl-blank-digest-probe.out`. The
staged digester retains the explicit guard until mapping is qualified.

The common `VTF` variable variant is also unsupported in the staged `n`
implementation. Native checks the variable's stored tokens without expanding
control sequences inside it; evidence is
`../lambda-input/temp/expl3-script/expl3-tl-blank-v-before.ls` and `.out`.

Script now implements the `n` variants with category-aware space checks,
unexpanded token semantics and macro-origin tracing. The new
`test_tex_script_expl3_tl_blank.ls/.txt` compares 17 native text/diagnostic
cases and checks safe AST outcomes in both backends. Fifteen focused
regressions and sampled forced GC with poisoning pass both. The digester
continues to reject unqualified conditional source mappings.


The Script engine now also implements `VTF`/`VT`/`VF`, checking the stored
tokens without expanding embedded controls. `test_tex_script_expl3_tl_blank_v.ls/.txt`
compares 19 native text/diagnostic cases and checks safe AST outcomes in both
backends. Sixteen focused regressions and sampled forced GC with poisoning pass
both; unsupported variable types and conditional source mapping remain bounded.

## Script TeX rejects input-stream EOF conditionals — 2026-11-12

**Status:** PARTIALLY FIXED for opened and closed captured-resource streams.
Stream AST source mapping remains open; S1.8 and D10.2.2 apply.

`\ifeof0 Y\else N\fi` stops Script expansion at
`tex-unsupported-primitive`, while native selects `Y` for an initially closed
stream. Nested and `\unless` examples show the same gap; evidence is
`../lambda-input/temp/expl3-script/ifeof-before.ls` and `.out`. Native scans a
stream number, diagnoses out-of-range values and tests the stream's actual
open/EOF state; a partial port must not assume open streams are closed.

The staged closed-stream candidate matches 15 native text/diagnostic cases,
but unguarded AST comparisons still differ for math and a preceding echoed
definition. Evidence is `../lambda-input/temp/expl3-script/ifeof-digest-probe.out`.
The current digester must explicitly reject those unqualified conditional source
mappings until the native mapping is reproduced.


Script now tracks input-stream state and implements `\ifeof`, including
stream-number diagnostics, nested conditions and `\unless`. Captured-resource
opening, closing, `\readline` and `\read` now update this state.
`test_tex_script_ifeof.ls/.txt` compares 15 native text and diagnostic cases and checks safe AST outcomes in both backends. Seventeen
focused regressions and sampled forced GC with poisoning pass both. The
digester explicitly rejects EOF conditional source mapping for now.

## Script TeX rejects input stream opening and closing — 2026-11-12

**Status:** PARTIALLY FIXED for captured-resource opening and closing. Stream
reading is supported for tested valid UTF-8 resources; AST source mapping
remains open. S1.8 and D10.2.2 apply.

`\openin0=stream_fixture` stops the Script engine at
`tex-unsupported-primitive`. Native opening a fixture relative to the input
file changes the following `\ifeof0` result to false; a missing file leaves
the stream closed. The isolated reproduction uses
`../lambda-input/temp/expl3-script/openin-main.tex`, `stream_fixture.tex`,
`openin-file-native.ls/.out`, and `openin-before.ls/.out`. Native source
`do_open_stream` scans the stream number and file name, tries the exact name
and `.tex`, and keeps state until closed or read past EOF. Script must use the
authorized captured-resource provider rather than assuming file availability;
the initial candidate did not yet port `\read` and `\readline`.

A staged captured-resource implementation matches a file-backed native fixture
covering existing and missing files, `.tex` fallback, empty files, wrapped
stream numbers, grouping, closing and scanner lookahead. Without a digestion
guard, opening after a definition and closing inside a formatting argument
produce incorrect successful ASTs; see
`../lambda-input/temp/expl3-script/openin-digest-probe.out`. The separate
constructor-argument defect contributes to the nested case.


Script now opens and closes streams through the existing captured-resource
provider, trying exact and `.tex` names, tracking empty files and wrapped stream
numbers, and preserving native scan order. The new file-backed
`test_tex_script_openin.ls/.txt` compares native text/diagnostics across
opening, missing files, grouping, empty files and closing. Both backends pass
18 focused regressions and sampled forced GC with poisoning. The digester
reports unsupported for unqualified stream command mappings; `\read` remains
separate work.

## Script TeX rejects readline from input streams — 2026-11-12

**Status:** PARTIALLY FIXED for valid UTF-8 captured resources. Malformed
inputs, unsupported endline characters and AST mapping remain open; S1.8 and
D10.2.2 apply.

With the staged `\openin` implementation, `\readline0 to\first` stops
Script expansion at `tex-unsupported-primitive`. Native reads and trims a
line, stores other-category character tokens in a control sequence, and closes
the stream only after a subsequent read passes EOF. The fixture with two lines
and an EOF check is `../lambda-input/temp/expl3-script/openin-test-cwd/test/`
`lambda/latex/fixtures/local_packages/script_readline.tex` and
`script_stream_data.dat`; reproduction and native output are
`../lambda-input/temp/expl3-script/readline-before.ls` and `.out`. Support
must preserve input bytes, endline handling, stream position, scope,
`\afterassignment` and diagnostics. Ordinary `\read` remains separate.


Script now reads physical lines, trims trailing spaces, emits other-category
character tokens, tracks stream position and EOF, honors endline settings,
scopes assignments and preserves `\afterassignment`. Six file-backed native
fixtures compare text and diagnostics in interpreter and JIT, covering CRLF,
UTF-8, special characters, category behavior, scope and terminal reads.
Nineteen focused regressions and sampled forced GC with poisoning pass both.
Invalid UTF-8, surrogate endline characters, malformed headers and stream AST
mapping remain explicitly unsupported; tokenized `\read` is tracked below.

## Script TeX rejects tokenized input-stream reads — 2026-11-12

**Status:** PARTIALLY FIXED for tested valid UTF-8 captured resources. Invalid
UTF-8, surrogate endline characters, malformed headers and stream AST source
mapping remain unsupported; S1.8 and D10.2.2 apply.

With staged stream opening and `\readline`, `\read0 to\first` stops Script
expansion at `tex-unsupported-primitive`, while native reads a line, tokenizes
it using current category codes and stores it in a control sequence.
Reproduction is `../lambda-input/temp/expl3-script/read-before.ls/.out` with
`openin-test-cwd/test/lambda/latex/fixtures/local_packages/script_read.tex`
and `script_stream_data.dat`. Native continues across lines while braces remain
unbalanced, so a correct port must retain tokenization, balance, EOF, scope,
`\afterassignment`, source provenance and diagnostics.

Script now tokenizes lines with the current category codes and continues while
braces remain unbalanced. It preserves stream state, scope, `\afterassignment`,
reused synthetic source provenance and the tested native diagnostics. Nine
file-backed fixtures compare native text and diagnostics in interpreter and JIT,
including multiline and mixed `\read`/`\readline` input. Twenty focused
regressions and sampled forced GC with poisoning pass both backends. The
digester continues to reject unqualified stream AST mappings.


## Script TeX reconstruction errors on stale partial UTF-8 spans — 2026-11-12

**Status:** FIXED for tested stale partial UTF-8 spans. Full reconstruction
mapping remains experimental; S1.8 and D10.2.2 apply.

The staged `\read` implementation reproduces native reuse of one synthetic
read buffer. In `script_read_mixed.tex`, an earlier token span later points into
a changed UTF-8 line and slices through a multibyte character. The installed
Script `reconstruct.ls` propagates `E318` from UTF-8 decoding instead of
falling back to the token's canonical serialization as native does. The
file-backed fixture and failure are
`../lambda-input/temp/expl3-script/openin-test-cwd/test/lambda/latex/fixtures/local_packages/script_read_mixed.tex`
and `../lambda-input/temp/expl3-script/read-final4-interp.log`. The Script
reconstructor must treat an undecodable candidate span as non-faithful; a
required undecodable source gap still needs an explicit issue.

The reconstructor now falls back to canonical token serialization when a
candidate source span cannot be decoded; an undecodable required gap remains an
explicit `tex-reconstruct-missing-origin` issue. Two focused cases and all 91
existing native text comparisons pass in interpreter and JIT, including
sampled forced GC with poisoning for the focused cases.


## Script TeX rejects expl3 token-list equality conditionals — 2026-11-12

**Status:** PARTIALLY FIXED for `NN` and `nn` variants. Other variants and
conditional AST source mapping remain open; S1.8 and D10.2.2 apply.

`\tl_if_eq:NNTF\l_tmpa_tl\l_tmpb_tl{T}{F}` and
`\tl_if_eq:nnTF{A}{A}{T}{F}` stop Script expansion with
`tex-unsupported-primitive`, while native selects the true branch. Native
also distinguishes a braced group from its unbraced contents. The comparisons
and diagnostic evidence are `../lambda-input/temp/expl3-script/tl-eq-before.ls`
and `.out`. The implementation must preserve TeX token identity, category
codes, full meanings for N arguments, conditional branch selection and source
mapping; AST qualification remains separate.

Script now implements `\tl_if_eq:NNTF/NNT/NNF` and `nnTF/nnT/nnF`.
The N variants compare complete TeX meanings and the n variants compare token
identity without expansion. Thirty-seven native text and diagnostic cases and
three safe AST checks cover branch selection, scope, category codes and `\edef`
in interpreter and JIT, including forced GC with poisoning. Conditional AST
mapping remains explicitly guarded.


## Script TeX rejects expl3 token-list stringification — 2026-11-12

**Status:** PARTIALLY FIXED for tested `n` and token-list `V` inputs.
Unsupported variable types and stringification AST mapping remain open; S1.8
and D10.2.2 apply.

In Script expl3 mode both variants stop with `tex-unsupported-primitive`, while
native produces string tokens. Native output also serializes control sequences,
groups and spaces before emitting TeX-safe text. Evidence:
`../lambda-input/temp/expl3-script/tl-to-str-before.ls/.out`. A port needs
native token/category and serialization behavior, including variable lookup and
source mapping, not only ordinary text examples.

The Script port serializes tokens into other-category characters and spaces,
with variable lookup and the tested `\edef` source tracing. Forty-one native
text and diagnostic comparisons and three guarded AST checks pass interpreter
and JIT, as do 19 focused regressions and 13 sampled forced-GC/poison cases.
Tests include category-sensitive
characters, control sequences, scoping, escape characters, empty values and
nested definitions. The temporary trace exemption is cleared when a generated
token is stored in a definition, so its later macro expansion retains its
correct foreign origin.


## Script TeX rejects expl3 token-list counting — 2026-11-12

**Status:** PARTIALLY FIXED for tested `n` and token-list `N` inputs. Other
variable types and AST source mapping remain open; S1.8 and D10.2.2 apply.

Both variants stop Script expansion with `tex-unsupported-primitive`. Native
counts `ABC` as three items, ignores a top-level space, and counts a braced
group as one item. Reproduction and native results are
`../lambda-input/temp/expl3-script/tl-count-before.ls/.out`. A correct port
must preserve unexpanded TeX token and group semantics, variable lookup, and
source tracing; AST mapping needs independent qualification.

The Script port counts unexpanded items, skipping top-level space tokens and
treating balanced groups as one item. Thirty-seven native text/diagnostic
comparisons and three guarded AST checks pass in interpreter and JIT, including
nested groups, scope, macro meanings, `\edef` and a 256-token input. Thirteen
sampled GC/poison cases and ten focused regressions pass both backends.


## Script TeX rejects raw inline verbatim commands — 2026-11-12

**Status:** PARTIALLY FIXED for direct-source, terminated `\verb` with an
ASCII delimiter. Unterminated input, non-ASCII delimiters, macro-sourced
commands, `\lstinline` and other raw forms, and AST mapping remain open;
S1.8 and D10.2.2 apply.

Script expansion reports `tex-unsupported-primitive` for `\verb|a_% b|`,
`\verb*+x y+` and `\lstinline|a_% b|`; native retains the raw bytes and
continues after the closing delimiter. Evidence is
`../lambda-input/temp/expl3-script/verb-before.ls/.out`. A correct port must
read from the active physical source without interpreting TeX category codes,
respect direct-source eligibility, delimiters, reader position, source spans,
EOF diagnostics and constructor argument mode. Raw environments and command
variants need separate qualification.

The staged direct-source implementation found a reconstruction defect for raw
spans emitted from an included file: converting an echoed span to a temporary
source span dropped its main-file emission origin, so reconstruction returned
`tex-reconstruct-missing-origin`. The failing comparison is
`../lambda-input/temp/expl3-script/verb-final2-interp.out`. The current staged
reconstructor now retains the source token's emission origin. All 91 existing
native reconstruction comparisons pass in interpreter and JIT. Raw spans also
remain distinct from echoed definition spans.

The Script reader now captures the tested `\verb` spans from direct main and
included sources without tokenizing their contents, and advances the active
physical reader. Sixteen inline native text/diagnostic cases and one file-backed
case pass in interpreter and JIT, along with eight focused regressions and
sampled forced-GC/poison checks.
Tests cover Unicode content, special TeX characters, aliases, line endings,
source-span classification, and explicitly unsupported boundaries. The
digester rejects raw inline source mapping until it is qualified.

Native LaTeX AST comparison for valid verbatim containing `%` or an apparent
`\end{document}` is additionally blocked by input issue
[LI-018](../../../lambda-input/vibe/Lib_Input_Issue_Ledger.md#li-018): native TeX
preserves those spans, but its LaTeX environment scan interprets their contents.


## Script TeX rejects expandable engine identity primitives — 2026-11-12

**Status:** PARTIALLY FIXED for tested identity expansion. Font-name AST
source mapping and downstream malformed-input recovery remain open; S1.8 and
D10.2.2 apply.

Script expansion reports `tex-unsupported-primitive` for `\jobname`,
`\fontname\nullfont` and `\pdftexrevision`, while native expands them to
`texput`, `nullfont` and `27` respectively for string input. Reproduction and
native diagnostics are `../lambda-input/temp/expl3-script/fontname-before.ls`
and `.out`. A port must preserve source-dependent job names, font identity and
size formatting, expanded-token behavior and diagnostics. File-backed cases
and AST source mapping need separate qualification.

Script now expands `\jobname` from the explicit `base_path` option (or
`texput` when absent), `\fontname` from the selected font identity and size,
and `\pdftexrevision` to the native revision. Thirty-three string-input and
four file-backed native text/diagnostic comparisons pass in interpreter and JIT,
including included-file job names. Font-name AST source mapping remains guarded;
other tested AST outcomes match native. Sixteen focused regressions and sampled
forced-GC/poison checks pass both backends.


## Script TeX stops on missing font identifiers — 2026-11-12

**Status:** PARTIALLY FIXED for font-identifier scanning and tested
`\fontname` recovery. Downstream numeric and dimension recovery remains open;
S1.8 and D10.2.2 apply.

`\fontdimen2\bad` and `\hyphenchar\bad=3` stop Script expansion at
`tex-missing-font`, while native reports the diagnostic, pushes the offending
token back, uses `nullfont`, and continues with further diagnostics and
output. Native `scan_font_ident` in `lambda/input/input-tex-scan.cpp` implements
that recovery. `../lambda-input/temp/expl3-script/font-recovery-before.ls/.out`
contains the native and Script evidence, including a `\fontname` lookahead case
in the staged identity implementation. Correct support must preserve input
position, diagnostic metadata, downstream scans and group behavior.

The shared font scanner now records `tex-missing-font`, restores the offending
token and returns `nullfont`. Native comparisons cover undefined and character
identifiers, EOF, definition lookahead and included-file transitions in both
backends. Later numeric and dimension scans can still stop on their own errors;
those diagnostic recovery paths remain open.


## Script TeX retains a stale source line after main input EOF — 2026-11-12

**Status:** FIXED for tested main-input EOF diagnostics and active included
readers. Broader malformed-input recovery remains open; S1.8 and D10.2.2 apply.

With staged `\fontname` and missing-font recovery, the source `\fontname`
produces the correct `tex-missing-font` text and offset, but Script attaches
`line: 1` while native has no line attribute. Evidence is
`../lambda-input/temp/expl3-script/fontname-eof-detail.ls/.out` and
`fontname-eof-state.ls/.out`. `stream.next` retains the previous root reader
when `reader.next` returns null, and `record_diagnostic` consequently mistakes
that stale reader for an active source. Correct recovery must distinguish an
exhausted main reader from a still-active included reader and preserve native
metadata without suppressing the diagnostic.

The stream now records root EOF separately while retaining its source data;
active included readers remain visible. Diagnostic recording omits the stale
line at exhausted root EOF and preserves a line when a scan resumes in an
included or parent file. The identity tests cover these cases and raw-reader
eligibility in interpreter and JIT, including sampled forced GC with poisoning.


## Script TeX rejects deterministic pdfTeX random primitives — 2026-11-12

**Status:** PARTIALLY FIXED for tested deterministic random and seed behavior.
Malformed numeric recovery and AST source mapping remain open; S1.8, S12.1.1v2
and D10.2.2 apply.

The Script engine reports `tex-unsupported-primitive`, while native returns 27
for the default-seeded `\pdfuniformdeviate 100`, 95 after seed 1, zero for
`\pdfnormaldeviate`, and 12345 for the initial `\pdfrandomseed`. Evidence:
`../lambda-input/temp/expl3-script/random-before.ls/.out`. Native uses a
32-bit deterministic linear congruential sequence, an explicitly settable
seed, and a fixed zero normal deviate. Correct support must preserve state,
scanning, grouping, arithmetic, expansion and diagnostic behavior.

Script now keeps native's 32-bit state, using exact-range intermediate products,
including seed assignment, zero and negative bounds, grouped and nested scans,
and the native fixed-zero normal deviate. Sixty-one string-input comparisons,
one included-file comparison, five safe AST checks, 18 focused regressions and
sampled forced-GC/poison tests pass in interpreter and JIT. The digester
explicitly rejects unqualified random and timer source mappings.

## Script TeX rejects pdfTeX timer primitives — 2026-11-12

**Status:** FIXED for the native input engine's zero-valued timer model.
AST source mapping remains guarded; S1.8 and D10.2.2 apply.

`\pdfresettimer\the\pdfelapsedtime` stops Script at
`tex-unsupported-primitive`, while native emits zero. Reproduction is
`../lambda-input/temp/expl3-script/random-before.ls/.out`. Native's timer model
must be verified before porting its observable behavior.

The native input engine returns zero for elapsed time and ignores reset; Script
now reproduces that deterministic model. Numeric and `\the` uses are included
in the native comparison suite.


## Script TeX loses definition echo for the-expansion — 2026-11-12

**Status:** FIXED for tested numeric, dimension, glue and token-register
`\the` values in echoed definitions. Broader reconstruction remains open;
S1.8 and D10.2.2 apply.

For `\edef\x{\the\count0}\x`, Script reconstructs `0` while native
reconstructs the original definition followed by `0`. The same discrepancy
occurs for `\tolerance`, `\inputlineno`, `\eTeXversion` and `\dimen0`;
`\number\count0` correctly suppresses that echo. Evidence is
`../lambda-input/temp/expl3-script/the-edef-before.ls/.out`. Numeric and
dimension tokens produced by `\the` lack the internal-origin trace exemption
already used by font-token `\the`; a fix must cover relevant quantities and
clear the exemption when tokens enter a stored definition.

The same defect affects `\the\toks0`: characters, control sequences and
nested groups from a token register suppress an echo that native retains. See
`../lambda-input/temp/expl3-script/the-toks-detail.ls/.out`. The staged fix
also marks these directly inserted tokens as internal to the current trace;
`the-toks-detail2.out` matches native for four cases in the interpreter.

Numeric, dimension and glue text and token-register contents now retain their
internal trace origin only while scanning the current definition. Tests cover
nested definitions, `\number`, macros in register indices, and global, long and
protected prefixes in both backends. They are part of the same native
comparison and focused-regression runs above.


## Script TeX rejects box movement primitives — 2026-11-12

**Status:** PARTIALLY FIXED for inline `\hbox`, `\vbox` and `\vtop` sources.
Registered boxes and AST source mapping remain open; S1.8 and D10.2.2 apply.

Script expansion reports `tex-unsupported-primitive` for these commands.
Native turns `\raise1pt\hbox{B}` into `\raisebox{1.0pt}{B}`, negates the
dimension for `\lower`, and emits the moved inline box's contents for
`\moveleft` and `\moveright`. Evidence is
`../lambda-input/temp/expl3-script/move-before.ls/.out`. A correct port must
scan the dimension and box, preserve grouping, hooks and source origins, and
handle registered boxes without assuming they are empty; AST mapping requires
separate qualification.

Script now scans movement dimensions and executes inline box bodies with the
native wrapper or discarded shift, preserving group scope, hooks and source
origins. Thirty-five string-input and one included-file native comparisons,
three safe AST checks, 16 focused regressions and 16 sampled forced-GC/poison
cases pass in interpreter and JIT. Registered-box movement remains explicitly
unsupported; the digester guards unqualified movement source mapping.


## Script TeX rejects shipout of inline boxes — 2026-11-12

**Status:** PARTIALLY FIXED for inline `\hbox`, `\vbox` and `\vtop` sources.
Registered boxes, cross-boundary `\unskip` and AST source mapping remain open;
S1.8 and D10.2.2 apply.

`\shipout\hbox{B}` stops Script with `tex-unsupported-primitive`, while the
native input engine discards the box's emitted contents and continues; global
assignments within the box still take effect. Evidence is
`../lambda-input/temp/expl3-script/shipout-before.ls/.out`. A correct port
must preserve box grouping, hooks, assignment effects, diagnostics, and source
mapping; registered boxes require the separate box-register implementation.

Script now executes inline boxes and discards their emitted material, retaining
global assignments, grouping, hooks, terminal messages and diagnostics. Twenty-one
string-input native comparisons, four message/diagnostic comparisons, one
included-file comparison, three safe AST checks, 16 focused regressions and
sampled forced-GC/poison checks pass in interpreter and JIT. Cross-boundary
`\unskip` remains explicitly unsupported as tracked below and in LI-019.


## Script TeX rejects cross-boundary unskip in discarded material — 2026-11-12

**Status:** OPEN. Blocks output parity when `\unskip` inside discarded insert,
vadjust, leader or shipout material removes preceding output; S1.8 and D10.2.2
apply.

For `\begin{document}A \insert0{\unskip}B\end{document}`, Script explicitly
reports `tex-unsupported-primitive` / `insert cross-boundary unskip`, while
native emits `AB`. The same occurs for `\vadjust` and discarded leader boxes;
the inline `\shipout` slice has the same limit. Evidence is
`../lambda-input/temp/expl3-script/discard-unskip-before.ls/.out` and
`shipout-edge-probe.ls/.out`. The discard scope stores an output-length
boundary and cannot currently represent removal of pre-existing output without
corrupting its later truncation. A fix needs to preserve native mutation and
nested discard behavior rather than dropping the error.

A staged boundary-adjustment candidate matches 16 of 18 native text cases but
differs on two nested/echo cases; it also exposes unqualified AST mappings.
The native differences are caused by input issue
[LI-019](../../../lambda-input/vibe/Lib_Input_Issue_Ledger.md#li-019): native leaves
stale discard boundaries after the removal and can leak discarded output.
Evidence is `../lambda-input/temp/expl3-script/discard-unskip-detail.ls/.out`,
`discard-unskip-detail.log` and `discard-unskip-ast-probe.out`. The candidate
is uninstalled; the existing explicit unsupported result remains in place.


## Script TeX rejects vcenter boxes — 2026-11-12

**Status:** PARTIALLY FIXED for tested `\vcenter` expansion and grouping.
Math/document AST source mapping and broader group semantics remain open; S1.8
and D10.2.2 apply.

Script expansion stops with `tex-unsupported-primitive`, while native emits a
`\vcenter{...}` wrapper and processes its body; an optional `to` dimension is
scanned rather than emitted. Evidence is
`../lambda-input/temp/expl3-script/vcenter-before.ls/.out`. A correct port must
preserve box specification scanning, grouping, body effects, hooks and source
mapping. Math and document AST digestion require separate qualification.

An isolated digester experiment can recognize the native math `box_transform`
shape, but six probes remain explicitly unsupported because the synthetic
wrapper lacks qualified math or command source mapping. Evidence is
`../lambda-input/temp/expl3-script/vcenter-ast-probe.ls/.out`. The engine slice
keeps an explicit AST guard until reconstruction is qualified.

Script now scans optional size specifications and emits the native wrapper
while preserving tested scope and body effects without injecting `\everyvbox`.
Twenty-six string-input and one included-file native comparisons, three safe AST
checks, 17 focused regressions and sampled forced-GC/poison tests pass both
backends. The digester explicitly guards unqualified `\vcenter` source mapping.


## Script TeX stops on native-diagnosed numeric math commands — 2026-11-12

**Status:** PARTIALLY FIXED. Script now scans tested numeric operands for
`\mathchar`, `\delimiter`, `\radical` and `\mathaccent`, reports the native
diagnostic and continues. S1.8 and D10.2.2 apply. Numeric-math AST source mapping
and malformed operand recovery remain unsupported; the native input engine also
does not render these numeric math characters.

Previously Script reported `tex-unsupported-primitive` and stopped. Native scans the numeric
operand, reports `tex-unsupported-mathchar` with source metadata and continues
with subsequent output. Reproduction and diagnostic records are
`../lambda-input/temp/expl3-script/math-number-before.ls/.out`. A compatible
port must preserve operand expansion and scanning, diagnostics, source
locations and continued processing; native does not render these numeric math
characters, and broader math support remains separate. The port matches 32 string
cases and one included-file case, with three guarded AST checks, 17 focused
regressions and sampled forced-GC checks in both execution backends. See
`test/lambda/latex/test_tex_script_numeric_math.ls`.


## Script TeX rejects native inspection commands — 2026-11-12

**Status:** PARTIALLY FIXED. Script handles tested `\show`, `\showbox`,
`\showlists`, `\showgroups`, and `\showifs` cases; S1.8 and D10.2.2 apply.
Inspection AST source mapping, registered boxes, and malformed operand recovery
remain unsupported. The separate `\showthe` issue records its tested support.

Previously Script reported `tex-unsupported-primitive`. Native reports a token's meaning
for `\show`, prints void-box information for `\showbox`, and treats the three
list/group/conditional inspection commands as no-ops in this input model.
Reproduction, native messages, text and diagnostics are
`../lambda-input/temp/expl3-script/show-before.ls/.out`. A correct port needs
token identity, register scanning, terminal buffering and box-state behavior;
`\showthe` remains a separate inspection operation. Fifty native string
comparisons, one included-file comparison, three guarded AST checks, 21 focused
regressions and sampled forced-GC tests pass both execution backends; see
`test/lambda/latex/test_tex_script_inspection.ls`.


## Script TeX misreports dimension and glue register meanings — 2026-11-12

**Status:** FIXED for tested dimension and glue register aliases in `\meaning`
and `\show`; S1.8 and D10.2.2 apply.

For `\dimendef\d=3 \meaning\d`, Script previously output `undefined`, while the
native engine prints the meaning `\dimen3`. Similarly `\skipdef` and
`\muskipdef` aliases incorrectly reported `undefined` instead of `\skip5`
and `\muskip7`. Reproduction and output are
`../lambda-input/temp/expl3-script/meaning-probe.ls/.out`. The shared Script
`print_meaning` previously omitted dimension and glue register kinds; native
`print_meaning` handles all register kinds. The shared implementation now
handles them, covered by `test/lambda/latex/test_tex_script_inspection.ls`.


## Script TeX misreports LaTeX constructor meanings — 2026-11-12

**Status:** FIXED for tested LaTeX constructor meanings in `\meaning` and
`\show`; S1.8 and D10.2.2 apply.

For `\show\section`, Script previously printed `> \section=\section.` while native
prints `> \section=\protected macro:->\section .`. The same missing
protected-macro representation affects constructor symbols such as `\%`.
Evidence: `../lambda-input/temp/expl3-script/show-extra-probe.ls/.out`. The
Script `print_meaning` constructor branch previously omitted the native protected
macro wrapper and control-sequence spacing; it now reproduces both. Tests are
in `test/lambda/latex/test_tex_script_inspection.ls`.


## Script TeX rejects showthe — 2026-11-12

**Status:** PARTIALLY FIXED. Tested `\showthe` quantities and token lists are
supported; inspection AST source mapping and malformed operand recovery remain
unsupported. S1.8 and D10.2.2 apply.

Previously Script stopped with `tex-unsupported-primitive`. Native prints the
scanned internal quantity or token-list value and continues: `\count0=12
\showthe\count0` prints `> 12.`, while `\toks0={abc} \showthe\toks0`
prints `> abc.`. Evidence: `../lambda-input/temp/expl3-script/showthe-before.ls/.out`.
The shared Script scanner now captures the result without expanding token-list
contents, including empty lists and nested `\the` expressions. Forty-one string
cases, one included file, two guarded AST checks, 29 existing regressions and
sampled forced-GC tests pass both backends; see
`test/lambda/latex/test_tex_script_showthe.ls`.


## Script LaTeX digester misinterprets generated meaning text — 2026-11-12

**Status:** PARTIALLY FIXED. Generated `\meaning` text requiring escaped-output
source mapping now explicitly returns unsupported instead of a wrong successful
AST; S1.8 and D10.2.2 apply. Native AST mapping remains open.

For `\begin{document}\meaning\section\end{document}`, Script expansion
matches native text, but digestion returns successful paragraph text containing
literal backslashes. Native produces `textbackslash` nodes with source offsets.
The same mismatch occurs for dimension and glue register aliases and constructor
symbols. Four native comparisons are recorded in
`../lambda-input/temp/expl3-script/meaning-ast-probe.ls/.out`. The Script
digester consumes generated character tokens directly and does not reparse
native escaped output; the required source mapping remains unimplemented. The
guard is reset after kernel preload and preserves ordinary text digestion. See
`test/lambda/latex/test_tex_script_generated_text_digest.ls`.


## Script LaTeX digester misinterprets generated string text — 2026-11-12

**Status:** PARTIALLY FIXED. Generated `\string` text requiring escaped-output
source mapping now explicitly returns unsupported instead of a wrong successful
AST; S1.8 and D10.2.2 apply. Native AST mapping remains open.

For `\begin{document}\string\foo\end{document}`, Script returns a successful
AST with a literal `\foo` paragraph string, while native creates a
`textbackslash` node and text `foo`. `\string\%` has the same mismatch; a
plain `\string A` matches native. Evidence:
`../lambda-input/temp/expl3-script/string-ast-probe.ls/.out`. Generated
character tokens bypass the native text escaping and source-offset mapping.
The guard and ordinary text cases are covered by
`test/lambda/latex/test_tex_script_generated_text_digest.ls`; 12 focused
regressions and sampled forced-GC checks pass in both backends.


## Script expl3 integer variables and comparisons are unsupported — 2026-11-12

**Status:** PARTIALLY FIXED. Script now supports tested `\int_abs:n`,
`\int_min:nn`, `\int_max:nn`, `\int_compare:nNnTF/T/F` with direct
relations or zero-argument macros expanding to one relation, and chained
`\int_compare:nTF/T/F` expressions. Integer variables, predicate variants,
more complex `nNn` relations, delimiter-consuming operands, overflow recovery
and integer AST source mapping remain open; S1.8 and D10.2.2 apply.

With `script_expl3: true`, Script previously stopped with `tex-unsupported-primitive` for
`\int_new:N`, `\int_set:Nn`, `\int_zero:N`, `\int_incr:N`,
`\int_compare:nNnTF`, `\int_compare:nTF`, `\int_abs:n`, and
`\int_max:nn`. Native produces the expected values in seven comparisons,
including local scratch variables. Evidence:
`../lambda-input/temp/expl3-script/int-api-before.ls/.out`. A port needs
register identity and scoping, expression parsing and expansion behavior,
conditional branch semantics, and guarded AST source mapping. The installed
numeric helpers share the integer-expression scanner with `\int_eval:n` and
match 51 string cases, one included file, two guarded AST checks, 20 focused
regressions and sampled forced-GC checks in both backends. See
`test/lambda/latex/test_tex_script_expl3_int_math.ls`. The direct-relation
comparison forms match 38 string cases, one included file, two guarded AST
checks, 13 focused regressions and sampled forced-GC checks in both backends;
see `test/lambda/latex/test_tex_script_expl3_int_compare.ls`. Zero-argument
relation macros that expand to a single `<`, `=` or `>` token are now supported:
34 string cases, one included file, three bounded unsupported cases, three safe
AST checks, 19 focused regressions and sampled forced-GC checks pass both
backends; see `test/lambda/latex/test_tex_script_expl3_int_relation.ls`.
Some conditional and parameter-taking relation expansions remain unsupported; the
original mismatch and examples are in
`../lambda-input/temp/expl3-script/int-compare-relation-before.ls/.out` and
`int-relation-debug.ls/.out`.
The single-expression forms support `=`, `==`, `!=`, `<`, `>`, `<=`, `>=` and
short-circuited chains; 44 string cases, one included file, three guarded AST
checks, 18 focused regressions and sampled forced-GC checks pass both backends.
See `test/lambda/latex/test_tex_script_expl3_int_compare_expr.ls`. Operand
macros that consume internal comparison delimiters still report an issue where
native continues; evidence: `../lambda-input/temp/expl3-script/int-marker-malformed-probe.ls/.out`.


## Script expl3 preload lacks native count register allocations — 2026-11-12

**Status:** OPEN. Blocks observable count allocation and register-identity parity
for the Script expl3 profile; S1.8 and D10.2.2 apply.

With `script_expl3: true`, `\ExplSyntaxOn\number\count10\ExplSyntaxOff`
succeeds but prints `24`; native prints `124`. Native `\meaning\l_tmpa_int`
is `\count26`, while Script reports `undefined`, and the next native
`\int_new:N` allocation is `\count125`. The bundled expl3 source defines
`\int_new:N` through `\newcount` and allocates numerous internal and scratch
registers. Evidence: `../lambda-input/temp/expl3-script/int-allocation-before.ls/.out`
and `lmd/package/doc/latex/tex/expl3-code.tex` near `\int_new:N`. A port must
preserve the shared allocator, identity and scope; setting observed counts or
register numbers to constants would hide the missing initialization.


## Script expl3 preload omits standard integer constants — 2026-11-12

**Status:** PARTIALLY FIXED. The non-allocating `\c_zero_int` and
`\c_one_int` now retain their native TeX meanings; register-backed constants
such as `\c_max_int` and the broader allocation state remain open. S1.8 and
D10.2.2 apply.

Native `\meaning\c_zero_int` is `\char"0` and `\meaning\c_one_int` is
`\mathchar"1`; Script previously reported both as undefined. Native `\int_eval:n` can
also read `\c_max_int`, while Script reports `tex-missing-number`. Evidence:
`../lambda-input/temp/expl3-script/int-constants-before.ls/.out`; the bundled
expl3 source defines these values and their TeX meanings in
`lmd/package/doc/latex/tex/expl3-code.tex`. Some integer constants allocate
registers, so their initialization is coupled to the separately recorded
count-allocation mismatch. `\c_two_int` and `\c_minus_one_int` were also
tested and are undefined in the native profile; do not invent them. The port
matches 32 string cases, one included file and five safe AST cases; see
`test/lambda/latex/test_tex_script_expl3_int_constants.ls`.


## Script LaTeX digestion misparses emitted expl3 math character names — 2026-11-12

**Status:** PARTIALLY FIXED. Emitted `\mathchardef` control names that the
native LaTeX scanner would split are explicitly guarded; native AST source
mapping remains open. S1.8 and D10.2.2 apply.

With the Script expl3 profile, directly emitting `\c_one_int` inside a document
returns a successful generic `c_one_int` node, while native reconstruction
reparses its emitted command and produces an accent node followed by text. A
separate `\mathchardef\foo_bar` reproduction yields a generic `foo_bar` node
in Script and a `foo` node plus `_bar` text natively. Ordinary `\foo` and
`\alpha` math-character names match. Evidence:
`../lambda-input/temp/expl3-script/int-one-ast-probe.ls/.out`,
`mathchardef-expl-native-only.ls/.out`, and
`mathchardef-expl-script-only.ls/.out`. The direct-token digester does not
reparse those emitted names with the native LaTeX scanner's catcodes. The
guard preserves successful native-equal ASTs for ordinary control names;
coverage is in `test/lambda/latex/test_tex_script_chardef_origin.ls`.


## Script TeX profile exceeds budget time on expl3 math-character input — 2026-11-12

**Status:** PARTIALLY FIXED. The tested exact self-recursive package bootstrap
now returns an explicit unsupported issue promptly after the package is attempted.
Other recursive macro shapes remain subject to the expansion budget; S1.8 and
D10.2.2 apply.

With `script_expl3` omitted, expanding the source in
`../lambda-input/temp/expl3-script/mathchardef-expl-disabled-small-budget.ls`
did not finish within a 10-second process timeout even with expansion budget
5000; the subprocess was terminated by the test. The same source with
`script_expl3: true` finishes in under a second in this local run, and native
LaTeX parsing finishes. See `mathchardef-expl-disabled-small-budget.out`,
`mathchardef-expl-script-only.ls/.out`, and
`mathchardef-expl-native-only.ls/.out` in that directory. At initial isolation it was unconfirmed
whether the slowdown was a non-progressing loop, excessive work outside the
expansion counter, or another cause. Further isolation shows `\ExplSyntaxOn`
alone reproduces the issue when expl3 resources and adapters are unavailable.
Budgets 500, 1000 and 2000 terminate with `tex-budget`; 5000 exceeded five
seconds. At budget 1000 the engine had accumulated 8000 output items and one
unknown `expl3` package. The kernel stub recursively requires `expl3` and
re-enters `\ExplSyntaxOn`; an unknown passthrough package does not replace the
stub. Evidence: `explsyntax-budget-results.out`, `explsyntax-cycle-detail.ls/.out`
and `mathchardef-timeout-isolates.out` in the same directory. The repeated
bootstrap is confirmed; quadratic cost from growing output is a plausible but
unconfirmed contributor. A general check now detects zero-argument macros whose
body requires an already attempted package and then re-enters the same meaning.
It preserves direct unknown-package passthrough, redefined `\RequirePackage`,
and packages that replace the bootstrap. Eleven behavioral cases, 18 focused
regressions and forced-GC checks pass both execution backends; see
`test/lambda/latex/test_tex_script_bootstrap_cycle.ls`.


## Script TeX reconstruction cannot emit an expl3 zero character — 2026-11-12

**Status:** FIXED for tested direct and macro-sourced `\c_zero_int` output;
numeric use is also supported. S1.8 and D10.2.2 apply.

With the staged non-allocating expl3 integer constants, expanding
`\ExplSyntaxOn\c_zero_int\ExplSyntaxOff` succeeded, but reconstruction
previously returned `tex-reconstruct-missing-origin` and no bytes. Native TeX output has
one NUL byte. Numeric byte counts and equality, which avoid the separately
recorded CLI NUL truncation, are in
`../lambda-input/temp/expl3-script/int-zero-nul-probe.ls/.out`. The shared `\chardef` emission branch omitted the reconstruction origin; see
`int-zero-origin.ls/.out` and the separate chardef provenance issue. The fixed
port reconstructs the same one byte as native; numeric evidence is in
`int-zero-nul-after.out`, and coverage is in
`test/lambda/latex/test_tex_script_chardef_origin.ls`. The direct-token LaTeX
AST also matched native in `int-constants-ast-probe.ls/.out`.


## Script TeX chardef emission loses source origin — 2026-11-12

**Status:** PARTIALLY FIXED. Emitted `\chardef` characters now carry a
main-source origin for tested reconstruction cases; constructor-argument parity
and broader source mapping remain separate. S1.8 and D10.2.2 apply.

For `\chardef\c=65 \c`, Script expansion succeeded but reconstruction
previously returned `tex-reconstruct-missing-origin`; native emits `A`. The same occurred for
escaped punctuation, Unicode and macro-sourced uses. Evidence:
`../lambda-input/temp/expl3-script/chardef-origin-before.ls/.out`. The shared
`step` branch creates a character token with the control token's start/end
but omits file, read-offset and emission-origin fields. A staged
`\c_zero_int` token shows these fields are null in
`int-zero-origin.ls/.out`; the missing reconstruction origin is confirmed.
Constructor-argument behavior is separately blocked by the existing
constructor expansion issue and must not be inferred from the last probe. The
port matches 25 string cases, one included file, three safe AST checks, two
ordinary math-character AST cases, 24 focused regressions and sampled forced-GC
checks in both backends; see `test/lambda/latex/test_tex_script_chardef_origin.ls`.


## Script expl3 integer zero and parity conditionals are unsupported — 2026-11-12

**Status:** PARTIALLY FIXED. Tested `\int_if_zero:nTF/T/F`,
`\int_if_odd:nTF/T/F` and `\int_if_even:nTF/T/F` are supported. Predicate
variants, conditional evaluation order for nested diagnostics, malformed or
overflow recovery and integer AST source mapping remain open; S1.8 and D10.2.2
apply.

Previously Script stopped with `tex-unsupported-primitive` for these operations; native
selects the expected branches for zero, negative odd and positive even
expressions. Five native comparisons are in
`../lambda-input/temp/expl3-script/int-tests-before.ls/.out`. A port must
preserve expansion, signed parity, branch laziness, group behavior and guarded
AST source mapping; predicate variants remain separate. The port matches 44
string cases, one included file, three guarded AST checks, 20 focused
regressions and sampled forced-GC checks in both execution backends; see
`test/lambda/latex/test_tex_script_expl3_int_tests.ls`. Branch evaluation order
for nested diagnostic-producing expressions remains an open issue recorded in
`Script expl3 integer conditionals read branches before evaluation`.


## Script expl3 integer division and modulo are unsupported — 2026-11-12

**Status:** OPEN. Blocks `\int_div_truncate:nn`, `\int_div_round:nn` and
`\int_mod:nn` in the Script expl3 profile; S1.8 and D10.2.2 apply.

Script stops with `tex-unsupported-primitive`; native distinguishes truncation
toward zero from nearest rounding and preserves the dividend's sign in the
remainder for the tested cases. Six native comparisons are in
`../lambda-input/temp/expl3-script/int-div-before.ls/.out`. Division-by-zero,
overflow recovery, expansion order and AST source mapping must be qualified.
The staged implementation explicitly stops with `tex-arith-overflow` for a
zero divisor, while native emits `0` and continues with a diagnostic; evidence:
`../lambda-input/temp/expl3-script/int-div-zero-probe.ls/.out`.


## Script expl3 integer conditionals read branches before evaluation — 2026-11-12

**Status:** OPEN. Blocks native diagnostic source metadata and potentially
branch-tokenization parity for nested integer operations; S1.8 and D10.2.2
apply. The tested ordering cause is confirmed for Script; native's exact
internal sequencing is inferred from source offsets and remains to be fully
qualified.

In the staged division port, `\int_compare:nTF{\int_mod:nn{1}{0}=0}{Y}{N}`
emits the correct `YX` text and arithmetic-overflow diagnostic but reports
offset 55, while native reports offset 49. The analogous `\int_if_zero:nTF`
case reports 59 instead of 53. The Script comparison and zero-test functions
read both branch arguments before evaluating the expression, advancing the
main-source cursor; native offsets indicate evaluation before those branch
tokens are consumed. Evidence:
`../lambda-input/temp/expl3-script/int-div-zero-offset-mismatch.ls/.out`. The
port must preserve branch laziness, source trace, grouping and diagnostics.
Conversely, the Script binary integer helpers evaluate their first operand
before collecting the second. Nested division diagnostics report offset 48
instead of native 51 for `\int_max:nn`, and 51 instead of 54 for
`\int_div_truncate:nn`; `nNn` comparison also reads branches too early.
Evidence: `../lambda-input/temp/expl3-script/int-order-binary-before.ls/.out`.
The first staged ordering revision regressed existing `nNn` comparisons when
an operand ended in `\relax`: its synthetic expression terminator was read as
a branch. Both backends produced two failed text comparisons in
`int-order-regression-{interp,jit}/expl3_int_relation.out` under that directory.
The installed implementation is unaffected; a corrected revision is being
qualified before installation.


## Concurrent integer-port preflight processes terminated with status 137 — 2026-11-12

**Status:** OPEN, cause unconfirmed. This limits highly concurrent qualification;
it does not currently block smaller test runs.

Ten concurrent interpreter/JIT preflight processes against the staged integer
ordering change exited with status 137 before producing result output. Sampled
stderr contained no fatal runtime diagnostic. A subsequent isolated interpreter
run of the unchanged integer-helper golden passed in 31.33 seconds. Evidence:
`../lambda-input/temp/expl3-script/int-order-concurrent-termination.out` and
`int-order-isolated-math.out/.log` in that directory. Resource pressure, external
termination and a runtime defect have not been distinguished; no cause is claimed.


## Script expl3 integer helpers mishandle explicit relax — 2026-11-12

**Status:** OPEN. Blocks native parity for some explicit expression terminators
and blocks qualification of the staged integer-division port; S1.8 and
D10.2.2 apply.

With `script_expl3: true`, `\int_abs:n{-1\relax}` emits `1`, while native emits
`1 `; `\int_min:nn{1\relax}{2}` stops with `tex-missing-number`, while native
recovers with a diagnostic and emits `0 `. The installed profile also stops on a
nested `\int_max:nn{\int_abs:n{-1\relax}\relax}{2}` where native continues with
two diagnostics. Evidence: `../lambda-input/temp/expl3-script/int-order-relax-detail.ls`
and `int-order-relax-live.out` in that directory. The staged ordering change
separates synthetic terminators and fixes the existing `nNn` branch regression,
but still reports a successful, different result for some helper arguments; for
example staged `\int_div_round:nn{7}{2\relax}` emits `4` without a diagnostic,
while native emits `4)` with `tex-missing-paren`. Evidence: `int-order-relax-detail.out`
and `int-div-fixed-interp.out` / `int-div-fixed-jit.out` in that directory. The candidate has not
been installed. Correct behavior requires modeling each native macro's expression
and terminator flow, not discarding the terminator or changing existing goldens.


## Script expl3 sequence queries and reversal are unsupported — 2026-11-12

**Status:** PARTIALLY FIXED. `\seq_if_empty:NTF/NT/NF`, the standard
`\c_empty_seq` meaning, `\seq_reverse:N` / `\seq_greverse:N` and tested
`\seq_item:Nn` indexing are implemented in the experimental Script profile.
The predicate and other sequence operations remain open; S1.8 and D10.2.2 apply.

Before this change, `\seq_if_empty:NTF`, `\seq_item:Nn`, `\seq_reverse:N`
and `\seq_if_in:NnTF` stopped with `tex-unsupported-primitive`. The tested
emptiness conditionals compare TeX meanings against the current `\c_empty_seq`,
including aliases, redefinitions and grouping. Forty native text and diagnostic
comparisons, one included file, two guarded AST cases, 14 focused regressions
and sampled forced-GC checks pass in both backends; see
`test/lambda/latex/test_tex_script_expl3_seq_empty.ls`. The `p`/`c` variants,
malformed-input recovery and full AST source mapping remain open. Native returns
`b` for index 2
and `c` for index -1, reverses `a,b,c` to `c,b,a`, and finds `b` in the
sequence. Reproduction and results:
`../lambda-input/temp/expl3-script/seq-api-before.ls/.out`. The remaining port
must retain TeX meaning comparison, expandable/protected behavior, group scope,
token identity and source mapping.

`\seq_item:Nn` supports tested positive and negative integer positions,
out-of-range empty results, nested expressions, and unexpanded item tokens.
Forty-two native text and diagnostic comparisons, two guarded residual-token
cases, three safe or guarded AST cases, one included file, eleven focused
regressions and sampled forced-GC checks pass both backends; see
`test/lambda/latex/test_tex_script_expl3_seq_item.ls`. Alternate argument
variants, malformed-input recovery, expressions with remaining tokens after an
early `\relax`, and full AST mapping remain open.

The reversal port matches 34 native text and diagnostic comparisons, one
included file, three safe AST cases, nine focused regressions and sampled
forced-GC checks in both backends; see
`test/lambda/latex/test_tex_script_expl3_seq_reverse.ls`. It preserves local and
global assignment, `\globaldefs`, aliases and token order. It stops with
`expl3-invalid-sequence` for `\edef\rev{\seq_reverse:N\l_s_seq}\rev`:
the protected command is retained while the following sequence token expands,
so the executed target is not a sequence variable. Native diagnoses the resulting
malformed target and continues with the original sequence. Evidence:
`../lambda-input/temp/expl3-script/seq-reverse-probe.ls/.out`. Recovery is still
open; the explicit invalid-target error remains visible. Native AST comparison
when expl3 is first invoked inside a constructor argument is separately blocked
by [LI-020](../../../lambda-input/vibe/Lib_Input_Issue_Ledger.md#li-020).


## Staged sequence indexing leaks an expression terminator — 2026-11-12

**Status:** PARTIALLY FIXED. The installed `\seq_item:Nn` bounds integer
index evaluation and consumes its synthetic terminator. Residual tokens after
an early `\relax` are explicitly unsupported; complete native recovery and
expression behavior remain open. S1.8 and D10.2.2 apply.

For a sequence containing `10,20,30`, the staged expression
`\int_eval:n{\seq_item:Nn\l_s_seq{2\relax}+1}` returned `20+1` with no
issue while native returned `21`. Nested `\int_max:nn` also stopped with
`tex-missing-number` where native returned `20`. Evidence:
`../lambda-input/temp/expl3-script/seq-item-nested-probe.ls/.out`. The staged
indexer used the shared integer scanner, whose synthetic `\relax` remains in
the input after an explicit terminating `\relax`; the enclosing expression
then consumes it. The installed indexer delimits its token scope and matches the
tested nested cases. It reports `tex-unsupported-primitive` when additional
user tokens remain after an early `\relax`, instead of discarding them;
native can evaluate those tokens as part of its macro expression. Evidence:
`../lambda-input/temp/expl3-script/seq-item-residual-probe.ls/.out`.


## Script sequence operations reject valid macro bodies without cached items — 2026-11-12

**Status:** PARTIALLY FIXED. Tested sequence operations can decode ordinary
TeX macro bodies without a private cache. Protected macro mutation, broader
macro forms and full AST source mapping remain open; S1.8 and D10.2.2 apply.

For `\def\s{\s__seq\__seq_item:n{a}\__seq_item:n{b}}`, native
`\seq_use:Nn\s{|}` emits `a|b`; Script stops with
`expl3-invalid-sequence`. Native counting, indexing, appending and reversal
likewise work on the same macro body; Script currently requires a `seq_items`
array attached by its own sequence assignment functions. Evidence:
`../lambda-input/temp/expl3-script/seq-body-before.ls/.out`. Sequence identity
in TeX is the macro's token body, not a hidden allocation or a precomputed
cache. Decode well-formed sequence bodies while preserving token categories,
macro grouping, expansion behavior and bounded handling of malformed bodies.
The first staged decoder also rejects native-supported unbraced items and
protected sequence macros; its two initial macro-created-sequence AST examples
returned incorrect successful ASTs. Evidence:
`../lambda-input/temp/expl3-script/seq-body-probe.ls/.out`. The unbraced and
protected cases need qualification, and source mapping must remain guarded
until it can reproduce the native AST. An extended staged probe also found
that appending or prepending to a sequence with unbraced items reconstructs
those existing items with braces, changing `\meaning` from native. Reversal
and sorting do normalize those tested items natively. Evidence:
`../lambda-input/temp/expl3-script/seq-body-meaning-probe.ls/.out`. Updates must
preserve the existing macro body where native does, not only the item values.
The installed decoder handles tested braced and unbraced item bodies, and
append/prepend preserves the original body tokens. Forty native text and
diagnostic cases, two guarded protected mutations, three guarded AST cases,
one included file, thirteen focused regressions and sampled forced-GC checks
pass both execution backends; see
`test/lambda/latex/test_tex_script_expl3_seq_body.ls`. The tested protected
sequence macros can be read, but mutation remains explicitly unsupported until
its full native expansion behavior is modeled.


## Script-backed protected expl3 operations skip macro lookahead — 2026-11-12

**Status:** PARTIALLY FIXED for the installed local/global sequence and token-list
left- and right-insertion operations and sequence membership conditionals.
Other protected operations remain open; S1.8 and D10.2.2 apply.

With expl3 syntax active, store a letter-category `x` in a sequence and use
`\catcode`x=12 \seq_if_in:NnTF\l_s_seq{x}{Y}{N}`. Native emits `Y` and the
staged Script emits `N`. If a `\seq_put_right:Nn` immediately follows the
assignment before the membership test, native emits `N` and Script emits `Y`.
Both engines agree when the `\catcode` assignment is explicitly terminated by
`\relax`. Evidence: `../lambda-input/temp/expl3-script/seq-in-cats-probe.ls/.out`
and `seq-in-cat-detail.ls/.out`. A separate TeX Live 2025 pdfLaTeX
comparison agrees with the native `Y` and `N` results; see
`seq-in-cat-pdftex.tex/.out` in that directory.

Native `get_x_token` expands protected macros outside protected-expansion
contexts, so scanning the assignment's number starts expansion of the following
expl3 macro and captures its argument tokens before the category-code change.
Script-backed protected operations are modeled as unexpandable primitives; the
Script scanner does not start them until after the assignment, changing the
category codes of their arguments. The exact full macro-expansion sequence for
all affected operations remains to be qualified. This also affected
previously installed protected operations such as `\seq_put_right:Nn`; do not
alter existing goldens or substitute expected tokens. Other operations still need
protected-macro expansion that preserves argument timing and `\edef` protection.

An isolated prototype represents `\seq_put_right:Nn`, its global variant and
underlying token-list insertion as actual protected TeX macros, using the native
`\edef`/`\xdef`, `\unexpanded` and `\expandafter` aliases. It matches nine
initial native cases, including the category-code lookahead case, both macro
meanings, protected `\edef`, aliases and `\afterassignment`; evidence:
`../lambda-input/temp/expl3-script/seq-macro-right-probe.ls` and
`seq-macro-full-probe.out`. The implementation depends on sequence-body decoding. It covers only
right insertion and does not close the general protected-macro issue. The first
prototype also bypassed the installed token-list assignment source-mapping
guard: two existing `test_tex_script_expl3_tl_put` AST checks returned false.
Evidence: `../lambda-input/temp/expl3-script/seq-macro-tl-put-interp.out`.
The macro-backed path must preserve that guard until its reconstructed AST is
qualified; the existing golden remains unchanged. The installed implementation
attaches the guard to the underlying `\edef`/`\xdef` aliases, including aliases and
wrappers. Thirty-six native text, message and diagnostic comparisons, three
AST checks, an included file, fifteen existing regression suites and sampled
forced-GC checks pass on interpreter and JIT; see
`test/lambda/latex/test_tex_script_expl3_seq_macro.ls`.


## Staged sequence membership omits internal assignments and source mapping — 2026-11-12

**Status:** PARTIALLY FIXED. The installed `\seq_if_in:NnTF/T/F` uses
protected TeX macros for tested membership operations. Expanded-output source
mapping remains guarded; S1.8 and D10.2.2 apply.

A pending `\afterassignment X` produces `XY` or `XN` natively, while the
staged membership function emits `YX` or `NX`: native membership executes an
internal token-list assignment before choosing the branch. With `\globaldefs=1`,
its internal scratch token-list changes persist; one test yields `Y/b/b`
natively but `Y/BEFORE/OLD` in the staged version. Evidence:
`../lambda-input/temp/expl3-script/seq-in-probe.ls/.out` and
`seq-in-sideeffects.ls/.out`. The staged direct comparison skips those internal
assignments and therefore cannot currently reproduce the side effects. Two
other native AST comparisons produce incorrect successful Script ASTs; see the
final array in `seq-in-probe.out`. Correct support requires modeling the
internal assignment/group behavior and expanded-output source mapping, not
merely moving an output token or synthesizing the observed scratch values.


The installed implementation executes protected TeX macros and supporting
group, assignment and conditional macros. Forty-seven comparisons and an
included file match native text, messages and diagnostics on interpreter and
JIT, including category lookahead, `\afterassignment`, and local and global
scratch assignments; three AST examples are conservatively guarded. Sixteen
existing regression suites and sampled forced-GC tests pass both backends.
A changed-kernel-category initialization case is also covered. See
`test/lambda/latex/test_tex_script_expl3_seq_in.ls`. General malformed-input
recovery and source mapping remain open.


## Script expl3 operations expose primitive meanings — 2026-11-12

**Status:** PARTIALLY FIXED for the installed local/global sequence and token-list
left- and right-insertion macros and sequence membership conditionals.
Introspection parity remains open for other hand-ported expl3 operations; S1.8 and D10.2.2 apply.

With `script_expl3: true`, `\meaning\seq_if_empty:NTF` and
`\meaning\int_abs:n` return escaped primitive names with no issue, while native
returns their long macro meanings and bodies. The staged reversal operations
similarly report primitive names where native reports protected long macros.
The Script `meaning` and `print_meaning` paths classify script-backed operations
as primitives, so the TeX macro identity, prefixes and bodies are absent.
Evidence: `../lambda-input/temp/expl3-script/expl3-meaning-probe.ls`,
`expl3-meaning-live.out` and `expl3-meaning-probe.out` in that directory.
The staged `\ifx` checks for reversal versus global reversal and for a `\let`
alias happen to match native; this does not qualify general `\ifx`, `\show`,
or other reflection. Preserve real TeX meaning semantics and aliases when
implementing this; do not substitute observed strings for macro definitions.

The installed right-insertion operations use real protected macro bodies; their
`\meaning`, `\show`, tested `\ifx` and aliases match native. The installed
sequence membership conditionals also use real protected macro bodies and
match tested `\meaning` and `\show` results. Other expl3 operations remain
subject to the primitive-identity limitation above.


## Staged membership definitions inherit kernel category codes — 2026-11-12

**Status:** FIXED in the installed membership implementation; S1.8 and
D10.2.2 apply.

A supplied kernel ending with `\catcode35=12` caused the prototype to tokenize
its internal macro definitions with a non-parameter `#`, producing
`tex-macro-mismatch` even when the document restored the category before use.
Evidence: `../lambda-input/temp/expl3-script/seq-in-initial-catcode.ls` and
`seq-in-initial-catcode.out`. The definition reader now uses fixed TeX
group and parameter categories and expl3 syntax categories; the user document
keeps its own category state. The same reproducer now emits `Y`; see
`seq-in-initial-catcode-fixed.out`. Its assertion is included in
`test/lambda/latex/test_tex_script_expl3_seq_in.ls`.


## Token-list left insertion uses protected TeX macros — 2026-11-12

**Status:** IMPLEMENTED for `\tl_put_left:Nn` and `\tl_gput_left:Nn` in the
experimental Script profile. S1.8 and D10.2.2 apply.

The operations now execute their protected TeX definitions through the existing
`\edef`/`\xdef`, `\unexpanded` and `\expandafter` aliases. Their tested
argument timing, `\meaning`, `\show`, aliases and local/global assignment
behavior match native, while the existing token-list assignment source-mapping
guard remains in place. Twenty-eight native text, diagnostic and message
comparisons, two guarded AST cases, an included file, ten focused regressions
and sampled forced-GC checks pass interpreter and JIT; see
`test/lambda/latex/test_tex_script_expl3_tl_left_macro.ls`. Other token-list
variants and general expanded-output source mapping remain open.


## Macro-backed token-list assignment needs precise source mapping — 2026-11-12

**Status:** OPEN; `\tl_set:Nn` and `\tl_gset:Nn` remain on the installed
primitive implementation. S1.8 and D10.2.2 apply.

An isolated version using their real protected TeX macro bodies matches 19
native text, diagnostic and message comparisons. However, their shared
`\__kernel_tl_set:Nx` and `\__kernel_tl_gset:Nx` aliases carry the existing
token-list insertion source-mapping guard. For a direct assignment before a
document, the installed implementation produces an AST that matches native;
the staged version rejects it as `latex-digest-unsupported`. Reproduction:
`../lambda-input/temp/expl3-script/tl-set-ast-probe.ls`,
`tl-set-ast-live.out` and `tl-set-ast-stage.out`. Do not install the change by
regressing that supported document or weaken the guard globally: unguarded
insertion is already known to admit incorrect ASTs. Preserve expansion
provenance accurately enough to support direct assignments and safely handle
insertions and wrappers.

A further wrapper comparison currently differs between Script and native even
before the staged change. Native splits expl3 control names in the echoed
definition, and the two parsers also disagree on its source span. Native
scanner evidence and the still-unconfirmed offset cause are recorded as
[LI-021](../../../lambda-input/vibe/Lib_Input_Issue_Ledger.md#li-021); do not
copy the native scanner limitation into Script. Evidence:
`../lambda-input/temp/expl3-script/tl-set-ast-cases.ls`,
`tl-set-ast-cases-live.out`, `tl-set-ast-cases-unguarded.out` and
`tl-set-ast-detail.out`.


## Script expl3 profile lacks `\scan_stop:` — 2026-11-12

**Status:** FIXED in the installed Script profile; `\scan_stop:` is a real
TeX `\relax` alias. S1.8 and D10.2.2 apply.

`\ExplSyntaxOn\meaning\scan_stop:\ExplSyntaxOff` reports `undefined`
in Script while native reports `\relax`. This also affects meanings copied
by a staged `\tl_set_eq:NN`. The profile has not installed the expl3 alias
for TeX `\relax`; it must preserve primitive and `\ifx` behavior, rather
than substituting a printed value. Evidence:
`../lambda-input/temp/expl3-script/tl-set-eq-detail.ls` and `.out`.


## Token-list and sequence meaning-copy operations — 2026-11-12

**Status:** IMPLEMENTED for local and global `\tl_set_eq:NN` and
`\seq_set_eq:NN` in the experimental profile. S1.8 and D10.2.2 apply.

These operations execute protected TeX macros backed by `\let` and the
normal scoped assignment mechanism; aliases retain TeX meaning and assignments
respect `\globaldefs` and `\afterassignment`. The `\scan_stop:`,
`\tex_let:D` and `\tex_global:D` primitive aliases are installed. A
source-mapping guard follows aliased `\let` operations through direct and
prefixed assignments. Twenty-five native text, message and diagnostic
comparisons, four guarded AST cases, an included file, twelve focused
regressions and sampled forced-GC checks pass interpreter and JIT; see
`test/lambda/latex/test_tex_script_expl3_tl_set_eq.ls`. Other argument
variants and general source mapping remain open.


## Numeric scans ignore control sequences with space meaning — 2026-11-12

**Status:** FIXED for tested expanded leading-space and numeric-terminator
scans in the installed Script engine. S1.8 and D10.2.2 apply.

In the staged implementation of the native expl3 `f`-expansion helpers,
`\exp_stop_f:` has the meaning of a space token. Native consumes it both
before a number and as a terminator after ordinary or character numbers. The
Script scanner checks only the input token's literal category and therefore
leaves an extra space, or reports `tex-missing-number`. This also leaves
`\exp_stop_f:` in the resulting sequence macro body and causes a later
`expl3-invalid-sequence`. Evidence:
`../lambda-input/temp/expl3-script/seq-left-number-probe.ls/.out`,
`seq-left-detail.ls/.out`, and `seq-left-probe.ls/.out`.

The installed fix recognizes control sequences whose TeX meaning is a space in
expanded leading-space scans and numeric terminators; the three native numeric
cases and the initial sequence cases match after the change. Evidence:
`seq-left-number-fixed.out` and `seq-left-probe-fixed.out` in the same
directory. The tested numeric and aliased-space cases, nineteen focused regressions and
sampled GC pass both backends. General source mapping remains guarded. Native
recovery after a missing number is a separate issue; the
first two cases in the numeric probe demonstrate that it is not fixed here.


## Sequence left insertion uses protected TeX macros — 2026-11-12

**Status:** IMPLEMENTED for `\seq_put_left:Nn` and `\seq_gput_left:Nn` in
the experimental Script profile. S1.8 and D10.2.2 apply.

The operations execute the protected TeX definitions and their tested `f`
expansion helpers, preserving token bodies, argument timing, aliases, scope
and introspection. The existing token-list assignment source-mapping guard
remains in effect. Forty-six native text, diagnostic and message comparisons,
two guarded AST cases, an included file, nineteen focused regressions and
sampled forced-GC checks pass both backends; see
`test/lambda/latex/test_tex_script_expl3_seq_left_macro.ls`. Other variants,
general `f` expansion and source mapping still need qualification.


## Control-sequence existence conditionals need expanded-output mapping — 2026-11-12

**Status:** PARTIALLY IMPLEMENTED. The installed Script profile uses the real
expandable TeX conditional macros with an explicit source-mapping guard;
full AST support remains open. S1.8 and D10.2.2 apply.

Fifteen initial native text, message and diagnostic comparisons match, but
three AST examples whose branches define commands return successful Script
ASTs that differ from native. One direct document-body example matches and
one math example is already unsupported. Evidence:
`../lambda-input/temp/expl3-script/cs-exist-probe.ls/.out` and
`cs-exist-ast.ls/.out`. The exact contribution of native definition scanning
and source-span behavior (see [LI-021](../../../lambda-input/vibe/Lib_Input_Issue_Ledger.md#li-021))
versus Script mapping needs isolation. A staged guard marks actual conditional
macro expansions, including aliases, and returns `latex-digest-unsupported`;
see `cs-exist-ast-guarded.out`. Keep the mismatching cases visible and do not
claim tree support until the provenance is fixed and compared independently.


The installed `\cs_if_exist:NTF`, `:NT`, `:NF` and `\cs_if_exist_p:N`
macros preserve the tested distinction between undefined and `\relax`
meanings, expansion, aliases and nesting. Thirty native text, message and
diagnostic comparisons, four guarded AST cases, an included file, twenty-one
focused regressions and sampled forced-GC checks pass on interpreter and JIT;
see `test/lambda/latex/test_tex_script_expl3_cs_exist.ls`. Other variants and
expanded-output source mapping remain open.


The `N`-argument existence conditionals and predicates for token lists,
sequences, integers, booleans, comma lists, property lists, strings and
dimensions are real aliases of the corresponding control-sequence checks.
Their copied TeX meanings and the existing source-mapping guard follow aliases
and survive later redefinition of the original conditional. Thirty-two native
comparisons covering all 32 aliases, ten guarded AST cases, an included file,
twelve focused regression suites and sampled forced-GC checks pass on both
backends; see `test/lambda/latex/test_tex_script_expl3_exist_alias.ls`. Other
argument variants and expanded-output source mapping remain open (S1.8,
D10.2.2).


The `c`-argument control-sequence existence conditionals and predicates, plus
the corresponding token-list, sequence, integer, boolean, comma-list,
property-list, string and dimension aliases, now execute their native TeX
macro meanings. Dynamic names expand with `\ifcsname` semantics, including
the tested absence of control-sequence creation for missing names; the
existing AST source-mapping guard follows the conditional and its aliases.
Thirty-six native text, diagnostic and message comparisons, ten guarded AST
cases, an included file, fourteen focused regressions and sampled forced-GC
checks pass interpreter and JIT; see
`test/lambda/latex/test_tex_script_expl3_cs_exist_c.ls`. Other variants and
expanded-output source mapping remain open (S1.8, D10.2.2).


The `N`- and `c`-argument `\cs_if_free` conditionals and predicates now
execute their native TeX macro meanings, including the tested treatment of
undefined and `\relax` meanings and dynamic names. Their expanded output
uses the same explicit source-mapping guard. Thirty-two native text, message
and diagnostic comparisons, four guarded AST cases, an included file, fifteen
focused regressions and sampled forced-GC tests pass both backends; see
`test/lambda/latex/test_tex_script_expl3_cs_free.ls`. Other variants and
expanded-output source mapping remain open (S1.8, D10.2.2).


## Control-sequence meaning comparison — 2026-11-12

**Status:** IMPLEMENTED for `\cs_if_eq:NNTF`, `:NNT`, `:NNF` and
`\cs_if_eq_p:NN` in the experimental Script profile. S1.8 and D10.2.2 apply.

These expandable TeX macros compare actual meanings, including tested
undefined commands, aliases, macro prefixes, token category codes and register
identities. Expanded-output source mapping has a dedicated explicit guard.
Thirty native text, message and diagnostic comparisons, four guarded AST
cases, an included file, sixteen focused regression suites and sampled
forced-GC checks pass both backends; see
`test/lambda/latex/test_tex_script_expl3_cs_eq.ls`. Other argument variants
and expanded-output source mapping remain open.


The `cN`, `Nc` and `cc` meaning-comparison conditionals and predicates now
execute their native TeX wrapper and argument-expansion macro meanings. The
tested `\csname` behavior creates missing names as in TeX, and the existing
comparison source-mapping guard follows wrappers and aliases. Twenty-nine
native text, message and diagnostic comparisons, four guarded AST cases, an
included file, seventeen focused regressions and sampled forced-GC checks pass
both backends; see `test/lambda/latex/test_tex_script_expl3_cs_eq_c.ls`.
General expansion variants and expanded-output source mapping remain open
(S1.8, D10.2.2).


The local and global token-list and sequence meaning-copy operations also
support the `cN`, `Nc` and `cc` variants using native protected TeX wrapper
meanings and the shared name-expansion helpers. The assignment source-mapping
guard remains in effect. Thirty-six native text, message and diagnostic
comparisons, four guarded AST cases, an included file, eighteen focused
regressions and sampled forced-GC tests pass both backends; see
`test/lambda/latex/test_tex_script_expl3_tl_set_eq_c.ls`. Other argument
variants and expanded-output source mapping remain open (S1.8, D10.2.2).


The local and global token-list and sequence left/right insertion operations
also support the `cn` variants using the native protected TeX wrapper meanings
and shared name-expansion helper. The assignment source-mapping guard remains
in effect. Thirty-six native text, message and diagnostic comparisons, four
guarded AST cases, an included file, nineteen focused regressions and sampled
forced-GC tests pass both backends; see
`test/lambda/latex/test_tex_script_expl3_put_cn.ls`. Other argument variants
and expanded-output source mapping remain open (S1.8, D10.2.2).


## Duplicate expl3 declarations do not reproduce native diagnostics — 2026-11-12

**Status:** OPEN in the installed Script profile; the dynamic-name declaration
wrappers inherit this limitation. S1.8 and D10.2.2 apply.

Declaring the same token list or sequence twice through `\tl_new:c` or
`\seq_new:c` stops the Script prototype with `expl3-already-defined` and no
diagnostic records. Native TeX instead records a `tex-errmessage` with the
control sequence name and continues recovery. The root is the installed
`expl3_tl_assign`/`expl3_seq_assign` fresh-name check, which calls `fail`
instead of executing the native declaration error machinery. Evidence:
`../lambda-input/temp/expl3-script/decl-c-invalid.ls/.out`. This also limits
the existing `N` forms. Preserve the failing evidence and implement the
diagnostic and recovery path before claiming full declaration compatibility;
valid declarations can be qualified independently.


## Declaration-stage regression process terminated — 2026-11-12

**Status:** UNCONFIRMED cause; qualification continues serially. In the
staged dynamic declaration run, `test_tex_script_expl3_cs_free.ls` exited
with signal 9 and no stdout; see
`../lambda-input/temp/expl3-script/decl-c-regressions-jit.log`. The identical
staged suite subsequently passed against its unchanged golden when run alone,
with peak child RSS 853,114,880 bytes. The installed profile also passed the
same isolated suite with peak child RSS 940,867,584 bytes. These individual
debug-run measurements do not establish a performance comparison or a cause
for the termination. Scripts and captured outputs are `decl-c-one.py`,
`decl-c-live-one.py`, `decl-c-one-jit-expl3_cs_free.out` and
`decl-c-live-one-jit-expl3_cs_free.out` in that directory. Continue serial
qualification, recording exit codes; do not treat the empty output as a TeX
semantic mismatch without further evidence (S1.8, D10.2.2).


## Dynamic-name token-list and sequence declarations — 2026-11-12

**Status:** IMPLEMENTED for valid `\tl_new:c`, `\seq_new:c`, and local and
global `c` clearing forms in the experimental Script profile. S1.8 and
D10.2.2 apply.

The operations use protected TeX wrappers and the shared dynamic-name
expansion helper. Tested scope, `\globaldefs`, aliases and assignment ordering
match native. Thirty valid native text, message and diagnostic comparisons,
four guarded AST cases, an included file, twenty focused regressions and
sampled forced-GC checks pass both backends; see
`test/lambda/latex/test_tex_script_expl3_decl_c.ls`. Duplicate declarations
retain the diagnostic/recovery limitation above, and source mapping remains
guarded.


## Dynamic-name assignment wrappers still lose protected lookahead — 2026-11-12

**Status:** OPEN; the `cn` token-list assignment and sequence-from-clist
wrappers remain uninstalled. S1.8 and D10.2.2 apply.

An isolated port using the native `\exp_args:Nc` wrappers matches ordinary
assignment, scope, alias and included-file tests. However, if a `\catcode`
number scan is immediately followed by `\tl_set:cn` or
`\seq_set_from_clist:cn`, a saved `x` has the wrong category: native emits
`Y` and Script emits `N` in both tests. Evidence:
`../lambda-input/temp/expl3-script/assign-cn-detail.ls/.out` and the failing
comparisons in `test_tex_script_expl3_assign_cn.ls` /
`assign-cn-final-interp.out` in that directory.

The wrapper expands while the number is being scanned, but its underlying
`\tl_set:Nn` and `\seq_set_from_clist:Nn` are Script primitives instead of
the native protected macros, so their argument tokens are captured after the
category change. The earlier staged macro-backed token-list assignment is
also blocked by its source-mapping issue above. Preserve these failing tests,
implement the underlying protected expansion and source provenance, and
qualify both wrappers before installation; do not substitute expected tokens.


## Token-list use accepts a relax meaning without native diagnostics — 2026-11-12

**Status:** PARTIALLY FIXED: the installed precondition rejects the invalid
variable explicitly. Native diagnostic and recovery parity remain open;
S1.8 and D10.2.2 apply.

For `\ExplSyntaxOn\tl_use:c{missing}/\ifcsname missing\endcsname Y\else N\fi\ExplSyntaxOff`,
the staged `c` wrapper initially emitted `/Y` with no issue or diagnostics,
while native emitted `/Y` and recorded `tex-macro-mismatch`. Evidence:
`../lambda-input/temp/expl3-script/use-c-missing-detail.ls/.out`. The existing
`\tl_use:N` primitive rejects only an undefined meaning, but `\csname`
creates a `\relax` meaning and native `\tl_use:N` also treats that as a
missing variable. The installed fix applies the existing undefined-variable issue
to both meanings; see `use-c-missing-fixed.out`. It prevents silent success
but does not reproduce native diagnostics or recovery. Preserve the original
failing comparison, qualify valid reads independently, and implement the
expl3 message/recovery path before claiming full invalid-input compatibility.


The same staged dynamic-read probe found further missing-name limits:
`\tl_count:c{missing}` returns `tex-unsupported-primitive` in Script while
native returns `1`; `\seq_count:c{missing}` returns `expl3-invalid-sequence`
in Script while native returns `0`. Missing-name sequence use and indexing
also stop explicitly instead of reproducing native diagnostics and recovery.
Evidence: `../lambda-input/temp/expl3-script/use-c-invalids.ls/.out`. These
unsupported cases must remain visible and are outside the qualified valid-read
subset until the underlying token-list and sequence meaning semantics and
error recovery are implemented.


## Dynamic-name token-list and sequence reads — 2026-11-12

**Status:** IMPLEMENTED for valid `\tl_use:c`, `\tl_count:c`,
`\seq_use:cn`, `\seq_item:cn` and `\seq_count:c` in the experimental
Script profile. S1.8 and D10.2.2 apply.

The operations execute native expandable TeX wrappers and the shared
name-expansion helper. Thirty valid native text, message and diagnostic
comparisons, five guarded AST cases, four explicitly rejected invalid cases,
an included file, twenty-two focused regressions and sampled forced-GC tests
pass both backends; see `test/lambda/latex/test_tex_script_expl3_use_c.ls`.
Missing-name diagnostics, recovery and count semantics remain open as
described above; expanded-output source mapping remains guarded.


## Dynamic-name token-list and sequence emptiness — 2026-11-13

**Status:** IMPLEMENTED for `\tl_if_empty:cTF`/`:cT`/`:cF` and
`\seq_if_empty:cTF`/`:cT`/`:cF` in the experimental Script profile.
S1.8 and D10.2.2 apply.

These forms use expandable TeX wrappers and the shared name-expansion helper.
Thirty-three native text, message and diagnostic comparisons, seven guarded
AST cases, an included file, seven focused regressions and sampled
forced-GC checks pass both execution backends; see
`test/lambda/latex/test_tex_script_expl3_empty_c.ls`. Expanded-output source
mapping remains guarded.


## Inline formatting commands without a brace group — 2026-11-13

**Status:** PARTIALLY FIXED for the tested script digester cases. S1.8 and
D10.2.2 govern the document and input boundary.

For `\def\x{\textbf}\begin{document}A\end{document}`, the native parser
creates an empty `textbf` node in the definition body. The script digester
instead returned `latex-digest-expected-group`: it required a brace group for
every formatting command. The native parser consumes an adjacent star, skips
ordinary space only when a group follows, and leaves a comment before a group
outside the formatting command. The script digester now follows these rules,
retains trailing group whitespace, and restores separators on synthetic output.

`test/lambda/latex/test_tex_script_inline_without_group.ls/.txt` compares 47
native ASTs for the supported formatting commands, definitions, comments,
whitespace, stars, Unicode and generated text. Interpreter and JIT pass with
the explicit generated-star guard, affected digest and provenance regressions,
and sampled forced GC. Earlier five-suite digest regressions passed in both
tiers. Full reconstructed-output digestion and malformed-input parity remain
unqualified.

An expanded command followed by a star can have different token source files
or offsets; for example `\def\x{\textbf}\begin{document}\x*{B}\end{document}`.
The native AST associates the star with `textbf`, while the script cannot yet
prove their reconstructed adjacency. It returns `latex-digest-unsupported` /
`inline command star source mapping`, rather than emitting an incorrect tree.
This blocks parity for affected documents. Reproduce with the final guarded
case in the test above; implementing general reconstructed-output source
mapping remains open.


## Experimental reconstruction drops source-mapping guards — 2026-11-13

**Status:** OPEN in the uninstalled reconstruction/reparse prototype; full
script-port blocker for affected documents. S1.8 and D10.2.2 apply. Production
continues to use the native parser.

For `\begin{document}A\textbf{B\hyphenation{words}C}D\end{document}`,
the experimental expander sets `paragraph_data_seen`; direct script digestion
correctly reports `latex-digest-unsupported` / `paragraph assignment source
mapping`. The reconstruction prototype instead returns an AST with no issue,
but that AST differs from native because the assignment was executed inside a
constructor argument. The prototype builds a new input map containing only
the reconstructed tokens and package/diagnostic metadata; it drops the source
flags before calling `digest.digest`. Reproduce with
`../lambda-input/temp/expl3-script/reparse-guard-probe.ls` and
`reparse-guard-before.out`. Do not integrate this path until it preserves the
relevant guard decisions, with any relaxed guards justified by direct native
comparisons and the constructor-argument behavior fixed separately.


## Ordinary output omits its emission origin — 2026-11-13

**Status:** PARTIALLY FIXED for ordinary emitted tokens; other emission paths
still need auditing for reconstructed output. S1.8 and D10.2.2 apply.
Production continues to use the native engine.

For `\def\spaceout{\quad}\begin{document}A\spaceout B\end{document}`,
`reconstruct.reconstruct` returns `tex-reconstruct-missing-origin`. The `B`
is a direct main-file token and needs a synthesized separator after `\quad`,
but its output item does not carry the current main-reader offset. Native
`emit_token` records that offset even on direct tokens, and reconstruction
uses it for separator source mapping. Evidence is
`../lambda-input/temp/expl3-script/reparse-spacing-origin-detail.out`.

The ordinary output path now records the reader position at emission,
including when lookahead has advanced it. Thirty-two native text, document-message
and diagnostic cases, four direct AST comparisons, an included-file fixture,
six focused regressions and sampled forced GC pass both execution backends;
see `test/lambda/latex/test_tex_script_macro_argument_origin.ls`. Other
emission paths require separate audit.


## Macro substitution loses argument source identity — 2026-11-13

**Status:** PARTIALLY FIXED for tested macro argument substitution; other
source-map and constructor-argument gaps remain open. S1.8 and D10.2.2 apply. Production still
uses native parsing.

For `\def\q#1{\quad#1}\begin{document}\q{\textbf{B}}C\end{document}`,
the native AST maps `textbf` to byte 36, its main-file source location. The
reconstruction prototype maps it to byte 46 instead. The script's
`expand_macro` applies `stored_tokens` to the entire substituted list,
clearing `direct` on freshly scanned arguments. Native macro substitution
copies argument tokens with their existing flags while its stored replacement
body is non-direct. Evidence:
`../lambda-input/temp/expl3-script/reparse-macro-argument-detail.out`.

The expander now clears the replacement body's direct flags separately,
keeps scanned argument flags and clears stored optional defaults. Thirty-two
native text, document-message and diagnostic cases, four direct AST cases, an
included-file fixture, six focused regressions and sampled forced GC pass both
backends; see `test/lambda/latex/test_tex_script_macro_argument_origin.ls`.
The separate reconstruction prototype matches 16 text/AST cases in both
tiers and 29 provenance cases in the interpreter; it is still uninstalled.


## Kernel preload messages are omitted from script results — 2026-11-13

**Status:** OPEN for ordinary non-expl3 profile output; blocks full native
message parity. S1.8 and D10.2.2 apply. The production native path is unchanged.

With `X` as input, native `parse(..., {type: "tex"})` includes the kernel's
allocation messages, whereas `profile.expand_with_kernel` returns no
messages. Running `expansion.expand` on the script kernel directly produces
the same preload messages as native; `expansion.resume` explicitly resets
`messages` to `[]`. Native `run_preload` clears the emitted tokens but leaves
the messages. Reproduce with
`../lambda-input/temp/expl3-script/kernel-preload-message-detail.ls/.out`;
the pre-change and staged macro profiles behave identically. Existing
inspection goldens deliberately compare document messages after subtracting
the native empty-document baseline, so changing this behavior requires
resolving those preserved assertions and qualifying the separate expl3-format
preload path. The failing full-message comparisons in
`macro-origin-native-interp.out` remain visible.


## Reconstructed leading blank lines lose a paragraph — 2026-11-13

**Status:** OPEN in the uninstalled reconstruction/reparse prototype; blocks
AST parity for affected documents. S1.8 and D10.2.2 apply. Production still
uses native parsing.

The local-file fixture `../lambda-input/temp/expl3-script/macro-origin-fixture-cwd/`
loads a file defining two macros before `\begin{document}`. Script and native
reconstructed text match, starting with three newline bytes, but the prototype
omits the leading `<paragraph "\n">` present in the native AST. Its TeX
reader converts those newlines to three `par` tokens, and `blocks` drops them
at the top level. Evidence: `../lambda-input/temp/expl3-script/
macro-origin-fixture-interp.out`, produced by `macro-origin-fixture.ls`.
The precise blank-line and comment rules must be preserved when reparsing; do
not integrate this prototype while it silently accepts the mismatched case.


## Formatted optional defaults are not digested — 2026-11-13

**Status:** OPEN in the script digester; blocks AST parity for affected
command definitions. S1.8 and D10.2.2 apply. Production remains native.

For `\newcommand{\q}[2][\textbf{Z}]{#1/#2}\begin{document}\q{B}\end{document}`,
native parsing returns an AST while `digest.digest` returns
`latex-digest-unsupported` / `optional argument`. The script's `bracket_end`
accepts only simple character tokens and rejects the formatting command and
nested brace group in the default. Reproduce with the final case of
`../lambda-input/temp/expl3-script/macro-origin-direct-ast.ls`; interpreter
output is `macro-origin-direct-ast-interp.out` in that directory. Port balanced
optional-argument parsing and source mapping before claiming this case.


2026-11-13 prototype follow-up: reconstructed blank-line normalization
matched 12 focused cases and the included-file fixture in the interpreter;
its 103-case corpus matched 100 ASTs and explicitly rejected three. The
prototype and its shared source guards remain uninstalled pending broader
qualification and do not close the production migration.


## Constructor execution also bypasses reconstruction guards — 2026-11-13

**Status:** OPEN in the uninstalled reconstruction prototype; blocks safe
integration and full script-port parity. S1.8 and D10.2.2 apply. Production
still uses native parsing.

Forwarding the existing source flags is necessary but insufficient. For
`\count0=0\begin{document}\textbf{\global\count0=7 X}\the\count0\end{document}`,
the legacy script expander executes the assignment inside the constructor
argument and emits `7`; native passes the assignment through and emits `0`.
The reconstruction prototype reports no issue and returns a different AST.
The same happens for unknown commands and optional arguments; evidence:
`../lambda-input/temp/expl3-script/reparse-constructor-safety-interp.out`.

An isolated candidate adds opt-in constructor argument mode, passes expandable
tokens through the native-style argument scanner without executing assignments,
and makes reconstruction reject legacy-mode results. In the interpreter the
four cases above match native, and the 103-case corpus has 100 matches and
three explicitly unsupported forms. Raw argument commands and custom
environments still report unsupported, the latter also affected by LI-017.
The candidate is uninstalled; JIT, more argument forms, source mapping,
existing goldens and GC still need qualification.
