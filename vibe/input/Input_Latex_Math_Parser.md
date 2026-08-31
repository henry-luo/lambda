# LaTeX and Math C Parser Proposal

> **Status:** proposal (2026-08-31)
>
> **Scope:** replace the production Tree-sitter LaTeX and LaTeX-math input
> parsers with direct C parsers.  Reuse the existing Input parsing, diagnostics,
> and Mark-construction framework.  Retain the two Tree-sitter grammars only
> in the `lambda-cst` differential/cross-reference build.  This retires the
> Tree-sitter parser library from the Lambda runtime; see
> [Runtime retirement boundary](#runtime-retirement-boundary).
>
> **Formal-spec linkage:** D1.1 defines hosted-language support as a selected
> grammar and AST builder, not a 100% compatibility promise; D1.2 requires
> `Item` as the sole script-visible value currency; D1.9 requires malformed
> input to fail as an error rather than reach undefined behaviour; D4.1.1v2
> and D4.1.3 require parsed documents to remain Input-arena owned and outside
> GC rooting; and D7.1.1 separates the static Lambda runtime from dynamic Jube
> modules.  This proposal changes none of those rulings.

## Summary

Replace the current pipeline:

```text
source -> Tree-sitter CST -> CST-to-Mark conversion -> Input root
```

with a single direct pipeline:

```text
source -> InputContext + C parser -> streaming Mark construction -> Input root
```

The C parser consumes the source once and emits the existing Mark structure as
it recognizes it.  It does not create a token list, a CST, a private AST, or a
second document representation.  This removes the generated LaTeX parsers,
their runtime library, and the large CST-to-Mark dispatch from normal Lambda
builds while retaining the public `input(..., {type: "latex"})` and
`input(..., {type: "math"})` behavior.

`Input_Latex.md` and `Input_Math.md` remain useful feature and grammar
references.  This document supersedes their production-parser direction: the
runtime target is a direct C parser, not a smaller Tree-sitter grammar.

## Goals and boundaries

### Goals

- Parse LaTeX documents and standalone math directly into the existing Mark
  AST, preserving the output consumed by the formatters and `lambda/package`
  renderers.
- Share one small LaTeX scanner between document mode and math mode.
- Use the established `Input` dispatch, `InputContext`, `SourceTracker`,
  `ParseErrorList`, `MarkBuilder`, `Input` arena, and command/environment
  tables rather than create parallel ownership or diagnostic facilities.
- Remove `tree-sitter-latex` and `tree-sitter-latex-math` from every normal
  runtime/build profile, together with every LaTeX/Math Tree-sitter API use.
- Remove the shared `tree-sitter` runtime library, includes, build targets,
  and linker dependencies from every normal Lambda runtime profile.
- Keep the two grammar projects available only to `lambda-cst` for corpus
  comparison and differential testing.

### Non-goals

- Implementing TeX execution: macro expansion, category-code mutation,
  assignments, file inclusion, package loading, and typesetting remain out of
  scope.  Per D1.1, the input profile supports the selected structural
  language, not arbitrary TeX programs.
- Redesigning the Mark AST, formatters, LaTeX package, or math renderer during
  the parser replacement.
- Retaining a production Tree-sitter fallback, dual parse, or source rewrite.
  A direct parser must handle its supported syntax itself; a fallback would
  hide regressions and retain the dependency this work removes.
- Promising a fixed percentage size or throughput change.  The result will be
  measured in a release build against the current implementation.

## Existing seam to preserve

The current runtime has a good integration seam even though its parser is
expensive:

- `input.cpp` dispatches `latex` input to `parse_latex_ts()` and `math` input
  to `parse_math()`.
- `InputContext` already owns an input copy of the source, UTF-8-aware source
  tracking, bounded diagnostics, and a `MarkBuilder`.
- `MarkBuilder` creates arena-owned strings, elements, maps, and arrays.  The
  final root belongs to the Input arena, exactly as D4.1.1v2 and D4.1.3
  require.
- `input-latex-tables.{h,cpp}` already centralizes common command and
  environment classification.

The replacement changes only the parsing implementation beneath these seams:

```text
input.cpp
  |- parse_latex(input, source)
  |    |- InputContext
  |    `- LaTeX C parser -> Mark sink -> input->root
  `- parse_math(input, source, flavor)
       |- InputContext
       `- Math C parser  -> Mark sink -> input->root
```

The thin Mark sink is C++ solely because `InputContext` and `MarkBuilder` are
C++ APIs.  The scanner and both grammars are C source and use only explicit
cursor state, fixed data tables, and callback operations.  Keeping that bridge
thin avoids duplicating `MarkBuilder` in C or adding a second allocation API.

## Minimal architecture

### Source layout

Use a small, purpose-specific layout under `lambda/input/latex/`:

```text
latex_scan.h / latex_scan.c       shared cursor, control sequence, comment,
                                   UTF-8, balanced-delimiter helpers
latex_parser.h / latex_parser.c   document-mode recursive-descent parser
math_parser.c                     LaTeX and ASCII math recursive-descent parser
```

`input-latex.cpp` is the one C++ adapter.  It creates an `InputContext`, owns
the Mark sink stack, maps byte offsets to `SourceTracker` diagnostics, and
sets `input->root`.  It contains no grammar decisions.  `input-math.cpp`
becomes a small flavor dispatcher to the same adapter.

Keep and extend `input-latex-tables` only when a command category changes
structure.  Unknown commands must use the existing generic-command Mark form;
they do not justify a new special parser branch.  Command tables are immutable
data, not a registry that executes handlers or implements TeX semantics.

### The parser state

Each parser has a bounded, stack-local state:

```c
typedef struct LatexParser {
    const char* begin;
    const char* cursor;
    const char* end;
    LatexMode mode;
    MathFlavor math_flavor;
    const LatexMarkSink* sink;
} LatexParser;
```

`LatexMarkSink` is a narrow callback interface for opening/closing an element,
adding text or an attribute, and recording an error at a byte offset.  It is a
streaming construction interface, not a node model: the C parser never owns a
Lambda value or retains a parse tree.  Nesting is represented only by the
adapter's open-element stack.

All cursor movement is range checked.  Delimiter scans take an explicit end
pointer, never rely on a NUL terminator, and report a diagnostic before
recovering.  This is the parser-level application of D1.9.

### Shared scanner rules

The scanner owns only syntax common to document and math modes:

- control words (`\\` followed by letters) and control symbols (`\\,`,
  `\\%`, `\\ `, and similar);
- UTF-8 code points, escaped special characters, and line endings;
- `%` comments, including their consumed physical newline;
- balanced `{...}` and `[...]` spans, with nesting and escaped delimiters;
- text runs, without copying until the Mark sink creates the final string.

It must not normalize or pre-process the complete source.  In particular, the
current comment-environment stripping and `\\frame{...picture...}` unwrapping
exist to compensate for CST grammar limitations.  A direct parser handles
those constructs at their real source locations, preserving trustworthy
diagnostics and avoiding an extra full-source allocation.

## Parsing flow

### Document mode

`latex_parser.c` is recursive descent over a sequence with an explicit stop
condition: end of source, a closing group, or a matching `\\end{name}`.

1. Coalesce ordinary text into one Mark string.
2. Skip comments; emit a paragraph-break symbol for a real blank paragraph.
3. Parse a group in place, preserving the existing empty-group marker where
   the formatter needs to distinguish `\\cmd` from `\\cmd{}`.
4. Parse a control sequence.  Its known category determines only argument
   shape; otherwise emit the existing generic command representation.
5. Treat `\\begin{name}` as an environment parse.  Raw-text environments scan
   directly to their matching end token; ordinary environments recursively
   parse their body.
6. On `$...$`, `$$...$$`, `\\(...\\)`, `\\[...\\]`, or a math environment,
   pass the exact inner range to the shared math parser and attach its AST.

Section commands, list-like environments, verbatim forms, command optional
arguments, and diacritics are normal branches in this flow; they do not need
a global mode stack or grammar backtracking.  This keeps the initial design
small while covering the existing structural output.

### Math mode

`math_parser.c` uses the same cursor and has one sequence parser parameterized
by a stop set (`}`, `]`, `&`, row break, environment end, or input end).  It
recognizes, in order:

- groups and delimiters;
- atoms (symbols, numbers, text, operators, relations, punctuation);
- superscript and subscript postfixes;
- LaTeX commands with table-selected argument shapes (`\\frac`, roots,
  delimiters, styles, accents, spacing, over/under, color/box, and generic
  commands);
- matrix/alignment environments, splitting rows and cells only while in that
  environment;
- ASCII words, quoted text, and ASCII operators when `MathFlavor` is ASCII.

Both `latex` and `ascii` flavors therefore use this one parser, but the flavor
is an explicit option rather than an inferred side effect.  This preserves the
current entry-point unification without carrying the Tree-sitter grammar.

### Mark compatibility contract

The first implementation preserves the output contract already used by the
runtime.  Representative required shapes are:

| Source construct | Mark result |
| --- | --- |
| LaTeX document | `<latex_document>` containing strings, symbols, and elements |
| ordinary command | command-named element with its current argument shape |
| `\\cmd[opt]{arg}` | command element containing a `<brack_group>` and argument content |
| inline/display math | `<inline_math>` / `<display_math>` with `source` and `ast` |
| standalone math | `<math>` root |
| math environment | environment-named element with `source` and `ast` |

The exact tags, attributes, empty-group behavior, and whitespace semantics are
captured as golden Mark snapshots before switching the default parser.  A
Tree-sitter node name, error recovery artefact, or source-preprocessing
workaround is not part of the contract.  If an existing fixture differs for
one of those reasons, its intended Mark result must be resolved explicitly in
a test before implementation; the direct parser must not copy the workaround.

## Diagnostics and recovery

The adapter sends every parser error through `InputContext` at the original
source offset.  It reuses the existing 100-error bound, `SourceTracker`, and
`ParseErrorList`; it creates no LaTeX-specific parallel diagnostic store.

Recovery is deliberately local and deterministic:

- An unterminated group or math delimiter records an error and returns the
  parsed prefix at the enclosing boundary/end of input.
- A mismatched `\\end{name}` records an error and returns to the nearest
  caller; it never scans arbitrarily far looking for a plausible tree.
- An unknown command is represented generically when its lexical form is
  valid.  A malformed control sequence is an error, not a guessed command.
- Raw/verbatim environments are opaque until their matching end token; a
  missing end token is a single unterminated-environment error.

This gives useful partial documents where the existing Input contract permits
them, but never treats malformed source as silently valid.  It follows D1.9
without importing Tree-sitter's error-node heuristics.

## Migration plan

### 1. Freeze the observable contract

- Add structural golden tests for the present LaTeX and math Mark output,
  including nested groups, comments, optional arguments, raw environments,
  math delimiters, scripts, fractions, matrices, and malformed input.
- Keep the existing round-trip, typesetting, and fixture tests.  Replace the
  current print-only Tree-sitter tests with assertions on Mark shape and
  diagnostics.
- Add a matching expected `.txt` file for every new Lambda `*.ls` test, as
  required by the repository test convention.

### 2. Land the direct math parser

- Add the shared scanner and direct math parser behind the existing
  `parse_math()` dispatch.
- Verify LaTeX and ASCII golden ASTs plus formatter round trips.
- Route embedded LaTeX math through the direct parser while the outer LaTeX
  document parser remains unchanged.  This is an integration checkpoint, not
  a permanent mixed production path.

### 3. Land the direct document parser

- Implement document commands, groups, environments, raw environments, and
  math delimiters using the shared scanner.
- Make it the only `latex` input implementation after the frozen contract
  passes.  Delete the Tree-sitter production entry point rather than retaining
  a flavor switch.

### 4. Retire runtime Tree-sitter dependencies

- Delete `input-latex-ts.cpp` and its Tree-sitter-specific declarations;
  rename the production entry points to `parse_latex()` and
  `parse_math_to_ast()`.
- Add the C source files explicitly to `build_lambda_config.json` (the current
  input source pattern selects `*.cpp`), then regenerate build files with
  `make`; never edit generated Lua files.
- Remove `tree-sitter-latex` and `tree-sitter-latex-math` from normal
  `lambda-data`, executable, debug, release, and test link dependencies.
  Remove the shared `tree-sitter` dependency from all normal Lambda runtime
  targets as well, including `lambda-rt` and `lambda-runtime-full`.
- Change normal Make targets so they do not build the Tree-sitter runtime or
  the LaTeX grammar libraries.
- Add both LaTeX grammar libraries to the `lambda-cst`-only dependency set.
  The grammar repositories may remain source-controlled at their current
  paths, but their generated libraries and Tree-sitter runtime are built and
  linked only by the `lambda-cst` profile.  The cross-reference harness must
  not be linked into `lambda.exe`.

### 5. Keep a reference verifier, not a fallback

Extend `lambda-cst` with a LaTeX/math differential command that compares the
direct parser's canonical Mark snapshot with the reference grammar's
normalized observation for supported fixtures.  It is a development and CI
tool only.  The authority order remains formal specs, then vibe design, then
the defined Mark contract; a Tree-sitter divergence is evidence to inspect,
not a reason to copy a grammar quirk.

## Runtime retirement boundary

The requested end state is no Tree-sitter parser library in the Lambda runtime.
Under D7.1.1, the Lambda runtime is its static-library/executable layering;
Jube language modules are dynamic modules and are outside that boundary.

| Consumer | Required result |
| --- | --- |
| LaTeX document input | direct C parser; no runtime Tree-sitter API |
| LaTeX/ASCII math input | direct C parser; no runtime Tree-sitter API |
| Lambda runtime build/link closure | no Tree-sitter runtime, headers, generated parser, or linker dependency |
| LaTeX and math reference grammars | `lambda-cst` only |
| Bash, Python, and Ruby Jube modules | separate direct-C-parser migrations; not a Lambda-runtime retirement blocker |
| Lambda/JS/TS reference grammars | existing `lambda-cst` model |

The build transition is therefore direct: remove the LaTeX/Math grammar
libraries and the shared Tree-sitter core from every normal runtime target,
and put the LaTeX/Math references in the `lambda-cst` dependency closure.  The
Jube migrations proceed in parallel and must not reintroduce Tree-sitter to the
runtime boundary.

## Completion gates

The migration is complete only when all of the following hold:

- LaTeX, standalone LaTeX math, ASCII math, embedded math, format round trips,
  package rendering, and malformed-input tests pass with the direct parser.
- The direct parser's golden Mark outputs and diagnostics cover every supported
  structure currently asserted by the runtime tests.
- `make release` is used for performance and binary-size comparison.  Record
  source size, release binary size, parse throughput, and allocation counts
  for the existing representative `.tex` and math fixtures before and after.
- A normal build and its link map have no `tree_sitter_latex*` or
  `tree_sitter_latex_math*` symbol, and no Tree-sitter parser-library
  dependency.  The normal Lambda runtime has no `TSParser`, `TSTree`,
  Tree-sitter include, generated parser, or `tree_sitter_*` symbol.
- `make lambda-cst` still builds the two grammars and runs the differential
  corpus independently of the runtime binary.
- No source preprocessor, generated parser, CST-to-Mark visitor, or runtime
  fallback remains in the production LaTeX/math input path.

## Why this is the smallest viable design

The current implementation pays for a generated parser, a runtime CST, and a
large conversion layer before creating the Mark tree that Lambda actually
uses.  The proposed parser replaces those three layers with one cursor and one
direct construction pass.  It reuses the framework that already owns lifetime,
names, document data, source locations, and errors, so the new code is limited
to LaTeX and math syntax rather than reimplementing Input infrastructure.

Sharing the scanner and the math parser avoids the two common growth paths:
one parser per math flavor, and one ad-hoc delimiter scanner per embedding
site.  Keeping Tree-sitter exclusively in `lambda-cst` preserves a useful
cross-reference without making it a runtime requirement.
