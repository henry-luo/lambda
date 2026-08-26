# Lambda vs. Nushell — Positioning Analysis

- **Status:** Informational comparison, not a roadmap proposal (2026-08-22).
- **Lambda authorities:** `doc/Lambda_Formal_Semantics.md` (S#) and
  `doc/Lambda_Formal_Design.md` (D#).
- **Nushell authorities:** the [Nushell Book](https://www.nushell.sh/book/)
  and the [Nushell repository](https://github.com/nushell/nushell).

## Executive conclusion

Lambda and Nushell overlap in the attractive middle ground between a shell and
a data language: both have a REPL, structured values, pipelines, immutable
bindings by default, and cross-platform ambitions. They are nevertheless
**adjacent tools, not substitutes**.

- **Nushell is a structured-data operating-system shell.** Its centre of
  gravity is interactive terminal work, filesystem/environment state, external
  programs, streamed pipelines, completions, and command discovery.
- **Lambda is a functional language and document-processing runtime.** Its
  centre of gravity is expressive transformations over values and document
  trees, schemas, multi-format conversion, and ultimately layout and rendering.

The useful positioning is therefore: *use Nu to orchestrate the machine; use
Lambda to understand and transform the data or document.* Trying to make native
Lambda a drop-in replacement for Nu would pull the project into a very mature
shell-UX and process-integration problem while weakening its distinctive
document model.

---

## 1. High-level comparison

| Dimension | Lambda | Nushell |
|---|---|---|
| Primary identity | Functional scripting language and document engine | Interactive shell and command-oriented scripting language |
| Fundamental unit of composition | Expression over Lambda values, collections, and elements | Command pipeline over typed values and streams |
| Native structured data | Arrays, maps, elements, ranges, rich numeric types, types and errors | Lists, records, tables, ranges, paths, dates, durations, filesizes, binary |
| Documents | First-class markup elements; parse, validate, transform, layout, render | Structured configuration/tabular data first; documents are generally text or plugin input |
| Effects | Declared `fn` (pure) / `pn` (effectful), compiler checked | Commands and closures may perform effects; immutability is encouraged but not an effect system |
| Failure | Typed soft errors (`T | error`) and declared raised errors (`T^E`) | Propagating errors, handled by `try` / `catch` |
| External programs | `cmd()` plus a separately hosted Bash dialect | A core shell operation; external programs, stdin/stdout/stderr, exit statuses, and `extern` signatures |
| Streaming | Stream plans are specified but not yet shipped | Central, shipped pipeline behaviour; custom commands and many filters stream |
| Large tables | Grouping and joins exist; DataFrame/verb plan is pending | Polars plugin provides eager/lazy DataFrames and columnar execution |
| Execution | T0 interpreter by default with a MIR T1 JIT tier | Parser produces IR, then Nu's evaluation engine interprets it |
| Terminal UX and ecosystem | REPL, but not a complete daily login-shell experience | Dedicated editor, history, contextual completion, configuration, modules, plugins, prompt and environment integration |

The two systems can represent many of the same mundane jobs—read JSON, filter
records, select fields—but the *boundary* is different: Nu's boundary is the
operating system; Lambda's is the data/document pipeline.

## 2. Pipelines: similar surface, different contract

Nushell extends the Unix pipeline from strings to typed values. Nu commands
exchange structured data, while transitions to external programs explicitly
serialize to stdin and accept text or binary output on the way back. This makes
the following a natural interactive command rather than a special integration:

```nu
open users.json | where age >= 18 | get name
```

This is not merely display syntax: Nushell exposes input/output signatures in
`help`, can stream custom-command input/output, and treats external-command
interoperation as a first-class part of pipeline semantics. See the Nushell
[pipeline documentation](https://www.nushell.sh/book/pipelines.html),
[custom-command documentation](https://www.nushell.sh/book/custom_commands.html),
and [external-command documentation](https://www.nushell.sh/book/running_externals.html).

Lambda's current native pipe is an expression operator, `|>`; `|` is reserved
for union/alternative everywhere by **S10.1.1**. It maps with a free `~` and
applies to the whole value otherwise. That makes it a concise language feature
for collections and elements, rather than a general process-composition model.
It pairs naturally with `for`, queries, vector arithmetic, and the element
tree.

There is an important status distinction here. **S14.3** specifies lazy streams
as plans: `stream()` would use the same `|>` and `for` surface, pure stages
would be fusible, and effectful stages would form barriers. That is a strong and
coherent design, but the implementation-status appendix states that streams,
the full stream/plan system, relational verbs, windows, and DataFrames are
pending; only grouping and joins from **S14.1** are landed. It would be
misleading to compare that specified future to Nu's current streamed pipelines.

## 3. Data processing versus document processing

Nushell's canonical structure is the table—a list of records—which is ideal
for files, directory listings, command output, configuration, and operational
reporting. It directly loads common operational formats such as CSV, JSON,
TOML, YAML, XML, SQLite, and spreadsheets, and `open` is extensible with `from
...` commands. Its Polars plugin adds Arrow-backed, lazy columnar DataFrames
for large tabular workloads. See [Loading Data](https://www.nushell.sh/book/loading_data.html)
and [DataFrames](https://www.nushell.sh/book/dataframes.html).

Lambda has all of the ordinary collection shapes but also has `element`: a
document node that is simultaneously a child sequence and an attribute map
(**S2.1.1**). That is a qualitatively different centre of gravity. Markdown,
HTML/XML, JSON/YAML/TOML/CSV, LaTeX, PDF, and related formats can be normalized
into a unified Lambda/Mark tree, transformed, schema-validated, and passed into
Radiant for layout or rendering. This is the pipeline described in the
[project overview](../../README.md) and is not an area Nu attempts to own.

The resulting design opportunity for Lambda is unusually strong: the same
language can describe a table-like transformation and construct the resulting
document, without changing to a template language, DOM library, or renderer.
Nu remains the better environment for inspecting a directory full of data;
Lambda is the more natural environment for turning that data into a validated,
rendered report.

## 4. Programming semantics

Both languages deliberately encourage a functional style, but Lambda makes
stronger semantic commitments.

### Mutation and aliases

Nu has immutable `let`, parse-time `const`, and opt-in mutable `mut` variables.
It prohibits closures from capturing a mutable outer variable, which supports
the functional and parallel-friendly style documented in its
[Variables chapter](https://www.nushell.sh/book/variables.html).

Lambda's **S9.1** goes further: values never alias, `let` is final, and
assignment/construction observe copying even when copy-on-write implements it.
`var` is the only mutability marker; its mutation is specified as a functional
update of the old value. This makes Lambda's immutability a value-semantics
guarantee, rather than only a binding convention. The inout-borrow aspects of
that model are still partly pending, so the distinction is normative design as
well as a shipped runtime direction.

### Effects and failures

Lambda's **S12.1** and **D6.1.1** define `fn`/`pn` as a declared,
compiler-checked one-bit effect system: an `fn` is pure and deterministic;
only a `pn` may perform effects; an `fn` cannot call a `pn`. This is valuable
for reusable transformations and is intended to licence sound fusion and
parallelization under **D6.1.2**.

Nushell's command vocabulary is its effect surface: `ls`, `cd`, `save`, an
external program, and a user-defined `def` all compose in the pipeline. It has
good local type checking for command parameters and pipeline signatures, but it
does not make purity a function-level contract. This is the correct trade for a
shell, where changing directory or invoking `git` should be ordinary.

Lambda also records failure in the type and value model: **S7.4** distinguishes
soft `T | error`, declared raised `T^E`, and non-typeable system faults. Nu
propagates an error through the current command/expression and handles it with
`try` / `catch`; caught error information is a record rather than a regular
error value. See [Nu error handling](https://www.nushell.sh/book/control_flow.html#errors)
and [Nu's error type](https://www.nushell.sh/lang-guide/chapters/types/other_types/error.html).

Neither approach is universally better. Lambda's contracts are more useful for
library-like transformations; Nu's error model is more direct for a command
session in which the user decides recovery at the prompt.

## 5. Shell and process integration

This is Nu's decisive advantage.

Nushell is designed to be the user's shell. It owns environment scoping and
working-directory changes, keeps exit-code state, invokes executables, offers
external-command signatures/completions via `extern`, and has a dedicated
Reedline editor for history, hints, validation, multi-line editing, and
contextual completion. See [Environment](https://www.nushell.sh/book/environment.html),
[Externs](https://www.nushell.sh/book/externs.html), and
[Reedline](https://www.nushell.sh/book/line_editor.html).

### Nu's command vocabulary and signature system

In Nu, the command vocabulary is effectively the shell's standard-library
surface. Names such as `ls`, `open`, `where`, `get`, `select`, `each`, `save`,
and `help` work alongside multiword command families such as `path parse`,
`str trim`, and `date now`. The multiword form gives the shell useful,
discoverable namespaces instead of forcing every operation into one global set
of function names.

That vocabulary is open. A user can add an alias, define a custom command with
`def`, group commands in a module and import them with `use`, or declare the
interface of an external executable with `extern`. An external command can be
made unambiguous with `^` when it shares its name with a Nu command. Thus a
workflow can gradually replace a private shell snippet with a documented,
typed command without leaving the shell's normal composition model.

Every Nu command has an inspectable signature. `help <command>` reports its
positional parameters, optional parameters, flags, rest arguments, accepted
types, and permitted pipeline input/output types; `help commands` exposes the
catalogue. A custom command can state the same contract in source:

```nu
def summarize [
  --top (-t): int = 10
]: table -> table {
  $in | sort-by score -r | first $top
}
```

The signature says that `summarize` consumes a table from the pipeline and
returns a table, and that `--top` accepts an integer. Nu can use this
information before execution for diagnostics, show it in help, and surface
relevant flags/arguments in the REPL completion UI. Multiple input/output
alternatives can be declared when a command is genuinely polymorphic. This is
not a full purity or effect system, but it is highly effective shell-interface
metadata. Nu's parser also uses signatures to catch invalid typed command
arguments while the command line is being entered. See [Custom
Commands](https://www.nushell.sh/book/custom_commands.html) and
[Pipelines](https://www.nushell.sh/book/pipelines.html).

`extern` extends that experience to programs Nu did not implement. For
example, an `extern ssh [...]` declaration can document and type a destination,
port, identity-file flag, and custom completions. Nu still runs the system
executable, but a user receives shell-native help and completion rather than an
opaque argument string. This is a major ergonomics advantage over the usual
Unix convention of memorizing each tool's `--help` output.

### Detailed catalogue of Nushell commands

The catalogue below covers the principal built-in command families in Nu 0.115
as of 2026-08-22. It is deliberately more detailed than a quick-start list but
is not a frozen release manifest: OS-dependent commands, enabled features, and
plugins alter the available set. `help commands` is the authoritative catalogue
for a running Nu installation, and `help <command>` is authoritative for its
signature. The online [command reference](https://www.nushell.sh/commands/)
and its [Core](https://www.nushell.sh/commands/categories/core.html),
[Filters](https://www.nushell.sh/commands/categories/filters.html),
[Formats](https://www.nushell.sh/commands/categories/formats.html),
[Filesystem](https://www.nushell.sh/commands/categories/filesystem.html),
[Strings](https://www.nushell.sh/commands/categories/strings.html), and
[Network](https://www.nushell.sh/commands/categories/network.html) categories
are the source for this summary.

#### Language, scope, and discovery

- `help`: Shows documentation for a command, alias, module, operator, or
  external declaration; `help commands` emits the command catalogue as a
  structured table and `help --find TERM` searches it.
- `scope`: Returns structured metadata for definitions currently in scope;
  subcommands list commands, aliases, modules, variables, and externs.
- `describe`: Reports the type and structural shape of pipeline data; it is the
  quickest way to learn what a command actually produced.
- `def`: Defines a custom command, including its parameters, flags, pipeline
  signature, examples, descriptions, and optional `--wrapped` argument
  forwarding behaviour.
- `alias`: Defines a command alias, optionally with flags, for concise or
  compatibility-oriented command spelling.
- `extern`: Declares a signature for an external executable so Nu can offer
  argument checking, help, and completion without implementing the program.
- `attr`: Attaches metadata to a custom command, including category, examples,
  search terms, deprecation notices, and completion providers.
- `module`: Defines a module that can group private and exported definitions.
- `use`: Imports commands, aliases, constants, and other exported definitions
  from a module.
- `export`: Makes a `def`, `alias`, `extern`, `const`, module, or imported name
  public from the current module.
- `overlay`: Activates, lists, creates, or hides swappable layers of module
  definitions—useful for a project environment or toolchain profile.
- `hide` / `hide-env`: Removes definitions or environment bindings from the
  current scope.
- `let`, `mut`, and `const`: Create immutable runtime bindings, mutable runtime
  bindings, and parse-time constants respectively.
- `if` and `match`: Express conditional and structural-pattern control flow.
- `for`, `while`, and `loop`: Provide imperative iteration when a filter or
  reduction is not the right pipeline form.
- `do`: Invokes a closure, optionally feeding it the current pipeline input.
- `break`, `continue`, and `return`: Control a loop or return early from a
  custom command.
- `try` and `error make`: Catch a failing block (with optional `catch` and
  `finally`) or construct a structured diagnostic error.
- `source`, `source-env`, and `run`: Load a script into the current scope,
  load its environment changes, or execute it in an isolated scope.
- `version`: Returns Nu's version and build configuration.

#### Pipeline filters and table transformation

- `where`: Filters rows with Nu's concise row-condition syntax, for example
  `ls | where size > 10kb`.
- `filter`: Filters a list, table, or range using an explicit predicate closure.
- `each`: Maps a closure over input values, preserving serial pipeline order.
- `par-each`: Maps work across a list/table concurrently; `--keep-order` is
  available when completion order must not change the result order.
- `reduce`: Folds a list, table, or range to one value with an accumulator
  closure; `--fold` supplies an initial value.
- `all` and `any`: Test whether every or any input value satisfies a predicate.
- `enumerate`: Adds the current index to each pipeline item.
- `get`: Extracts a value using a cell path, such as `get name` or
  `get package.version`.
- `select` / `reject`: Keeps only named columns/rows or removes them.
- `columns`, `values`, and `items`: Exposes record/table field names, field
  values, or name/value pairs as ordinary pipeline values.
- `insert`: Adds a field or column, calculating per-row values with an
  expression or closure.
- `update`: Replaces an existing field or column.
- `upsert`: Updates a field when present and inserts it when absent.
- `rename`: Renames table or record columns.
- `move`: Reorders columns or moves them to the beginning/end of a table.
- `default`: Replaces a missing or `null` field with a supplied default.
- `compact`: Removes empty rows/values from structured input.
- `flatten`: Extracts nested collection values into the surrounding stream.
- `transpose`: Swaps table rows and columns.
- `wrap`: Turns a value/stream into a named record or table column.
- `merge` / `merge deep`: Combines records or tables, with `merge deep`
  recursively combining nested records.
- `group-by`: Partitions rows by a key into a record of groups.
- `join`: Joins two tables on their common or explicitly selected columns.
- `sort` / `sort-by`: Sorts values or sorts rows by cell paths/closures.
- `uniq` / `uniq-by`: Removes duplicate values or rows, optionally by selected
  columns.
- `reverse` and `shuffle`: Reverse input order or randomize it.
- `first`, `last`, `take`, `skip`, `drop`, and `slice`: Select an input prefix,
  suffix, range, or complement of a range.
- `chunks`, `chunk-by`, and `window`: Partition input by size/key or create
  overlapping sliding windows.
- `append` / `prepend`: Adds rows or values to the end/beginning of a stream.
- `zip`: Combines an input stream with another stream position by position.
- `interleave`: Reads streams concurrently and combines their arriving values.
- `tee`: Sends a copy of a stream to a side command while preserving the main
  pipeline.
- `find`: Searches strings, lists, and tables; supports regex, column-restricted,
  case-insensitive, and inverted search.
- `length`, `is-empty`, and `is-not-empty`: Counts input elements/bytes or tests
  whether a value is empty.

#### Reading, writing, and converting formats

- `open`: Reads a file and, where a parser is available, converts it to Nu
  values based on its extension; `--raw` preserves raw content.
- `save`: Writes pipeline data to a file, choosing an encoder from the target
  extension when possible.
- `from`: The parser namespace; its subcommands turn text/binary data into Nu
  records, tables, lists, or scalar values.
- `from csv`, `from tsv`, and `from ssv`: Parse delimited text into tables.
- `from json`, `from toml`, `from yaml`/`from yml`, `from ini`, and `from kdl`:
  Parse common configuration/data serializations.
- `from xml`, `from md`, `from eml`, `from ics`, and `from vcf`: Parse markup,
  Markdown, email, calendar, and contact sources into structured values.
- `from xlsx`, `from ods`, and `from plist`: Import spreadsheet or property-list
  data.
- `from nuon`: Parses Nuon, Nu's literal-oriented interchange notation.
- `from msgpack` / `from msgpackz`: Imports MessagePack or brotli-compressed
  MessagePack values.
- `to`: The serializer namespace; its subcommands turn structured Nu values
  into an interchange or display representation.
- `to json`, `to csv`, `to tsv`, `to toml`, and `to yaml`/`to yml`: Serialize
  common data/configuration formats.
- `to xml`, `to html`, and `to md`: Render supported record/table structures as
  simple markup, HTML, or Markdown.
- `to nuon`, `to msgpack`, and `to msgpackz`: Produce Nuon's literal notation
  or compact binary interchange.
- `to text`: Produces a plain-text representation when an external program or
  text-only target is required.
- `encode` / `decode`: Encode/decode strings and binary data, including Base32,
  Base32hex, Base64, and hex.

#### Strings, text extraction, and terminal output

- `str`: The string-operation namespace, including case conversion, containment,
  prefix/suffix tests, trim, replacement, substring, index lookup, joining,
  reversal, statistics, and edit distance.
- `str contains`, `str starts-with`, and `str ends-with`: Test common string
  relationships without manually writing a regular expression.
- `str replace`: Replaces literal or regex matches in strings or selected table
  columns.
- `str trim`: Removes whitespace or a selected character from either/both ends.
- `str substring` and `str index-of`: Extract a range or find a character/string
  position.
- `str join` and `str length`: Join text with a separator or measure string
  length.
- `split row`, `split column`, `split chars`, and `split words`: Split text into
  list rows, table columns, Unicode characters, or words.
- `lines`: Converts text into line-oriented pipeline values.
- `parse`: Extracts columns from text using a pattern or regular expression.
- `detect columns`: Guesses column boundaries in human-formatted text.
- `format`: Formats a record/table using a pattern; `format date`,
  `format duration`, and `format filesize` target common scalar types.
- `char`: Produces named special characters or Unicode code points.
- `ansi`: Builds, strips, or applies ANSI color/style escape sequences.
- `print`: Writes values to stdout or stderr as a sink, without adding a value
  back to the pipeline.
- `echo`: Emits its arguments as values and ignores preceding pipeline input;
  unlike `print`, it remains composable.
- `grid` and `table`: Render data for terminal display rather than changing the
  underlying data-processing model.

#### Paths, filesystem, and local system inspection

- `cd` and `pwd`: Changes the shell working directory or returns the current
  directory; `cd` participates in Nu's scoped environment model.
- `ls`: Returns a table of file metadata—name, type, size, and modification
  time—rather than terminal-formatted text.
- `glob`: Expands a pattern into paths that can enter an ordinary pipeline.
- `path`: The path-operation namespace; `path parse`, `path join`, and
  `path expand` decompose, combine, and normalize filesystem paths.
- `cp`, `mv`, `rm`, `mkdir`, and `touch`: Perform core file-management actions
  with cross-platform Nu command interfaces.
- `mktemp`: Creates a temporary file or directory.
- `du`: Computes disk-usage sizes for specified paths.
- `start`: Opens a file, folder, URL, or application through the platform's
  default handler.
- `watch`: Watches filesystem changes and runs Nu code when a change occurs.
- `which`: Resolves a command name to a Nu command, alias, or executable path.
- `idx`: Manages and searches Nu's in-memory file index, including import/export
  and fuzzy file/directory/content searching.
- `ps`: Returns information about local processes as structured rows.
- `sys`: Provides subcommands for host, CPU, disk, memory, network-interface,
  temperature, and user information.
- `uname`: Prints selected platform/system-identification data.
- `kill`: Sends a signal to a process identified by PID.

#### Environment, configuration, and interactive session control

- `$env`: A special case-insensitive environment record; assignment such as
  `$env.PATH = ...` changes the scoped Nu environment rather than an ordinary
  local variable.
- `load-env`: Merges a record of environment updates into the current scope.
- `with-env`: Runs a closure with a temporary environment update.
- `config`: Opens, edits, resets, or inspects Nu configuration; `config nu` and
  `config env` target its principal startup files.
- `history`: Returns or imports command-history records; `history session`
  exposes the active history session.
- `keybindings`: Inspects available/default bindings and configures interactive
  keybinding behaviour.
- `commandline`: Reads or modifies the current line-editor buffer, cursor, and
  completion state—useful for custom keybindings.
- `input`: Reads a line of interactive user input.
- `input list`: Presents a selectable or fuzzy-filtered interactive list/table.
- `clear`: Clears terminal output.
- `banner`: Prints Nu's startup/version banner.

#### Processes, external commands, and background work

- `^program`: Syntax that invokes a PATH-resolved external executable instead
  of a same-named Nu command, for example `^ls`.
- `run-external`: Invokes an external program from a command position when a
  direct external call is inconvenient.
- `complete`: Captures an external command's stdout, stderr, and exit code into
  a Nu record/table for structured error handling.
- `exec`: Replaces or exits the current process with a command, subject to
  platform behaviour.
- `extern`: Declares external-command flags, parameters, types, and custom
  completion providers; it is metadata, not a process launcher.
- `$env.LAST_EXIT_CODE`: Records the latest external-program exit status and
  is Nu's counterpart to POSIX `$?`.
- `job`: The experimental background-job namespace; it can spawn, list,
  describe, message, receive from, kill, and manage Nu jobs.

#### Date/time, mathematics, conversion, hashing, and network work

- `date`: The date/time namespace, including `date now`, parsing, formatting,
  arithmetic, and duration-related operations.
- `math`: Provides aggregate and element-wise numeric functions such as
  `math sum`, `math avg`, `math min`, `math max`, `math median`, `math stddev`,
  `math sqrt`, `math round`, and trigonometric functions.
- `matrix`: A current matrix-value namespace for high-performance matrix math;
  it was added in Nu 0.115 and is backed by Rust's `ndarray` library.
- `bits`: Performs bitwise operations on integer or binary values.
- `into`: Converts a value to a selected Nu type, for example string, integer,
  float, date, binary, record, or cell path where the conversion is defined.
- `hash`: Hashes values; `hash md5` and `hash sha256` are common subcommands.
- `random`: Generates random values such as integers, booleans, dice rolls,
  UUIDs, characters, and binary data.
- `http`: The HTTP namespace: `http get`, `head`, `post`, `put`, `patch`,
  `delete`, and `options` issue typed network requests.
- `url`: Parses URLs into records, joins record fields into URLs, and
  builds/splits percent-encoded query strings.
- `query`: Queries JSON, XML, HTML/web content, and webpage metadata where the
  corresponding built-in or plugin support is present.
- `port`: Obtains an available local TCP port for a process or development
  workflow.

#### Plugins, exploration, and optional high-performance dataframes

- `plugin`: Adds, lists, uses, stops, and manages the registry of external Nu
  plugins; installed plugins become internal typed commands for pipeline use.
- `polars`: An optional maintained plugin namespace that creates eager/lazy
  DataFrames and exposes column expressions, joins, grouping, aggregation,
  SQL-like transformations, and collection to a concrete result.
- `query`: An example of functionality that may arrive through a maintained
  plugin rather than the minimal core; check `help query` in the target Nu
  installation before depending on it.
- `explore`: Opens an interactive TUI for browsing structured data rather than
  printing a fixed table representation.
- `histogram` and chart/viewer commands: Produce terminal-oriented summaries
  and visualizations for structured values where their relevant features are
  enabled.

Lambda has a different kind of callable metadata. **D6.1.1** requires every
callable surface—including system-function registry entries and Jube-module
signatures—to carry the `fn`/`pn` effect bit; **D6.4.1** requires public system
functions to publish a narrow successful `return_type`. **S7.10** further
specifies the cardinality/error contract for those functions. Lambda therefore
has the ingredients for semantically reliable callable contracts, but its
current user-facing organisation is a language built-in/function catalogue plus
CLI subcommands, not Nu's broad command namespace with per-command interactive
help, argument schemas, external-command declarations, and completion hooks.
The CLI does provide `lambda <command> --help`, while the system-function
reference documents builtins such as `cmd()`, I/O, data, and math
([CLI reference](../../doc/Lambda_CLI.md), [system functions](../../doc/Lambda_Sys_Func.md)).

Native Lambda has IO and `cmd()`, but those are facilities for an effectful
Lambda program—not a terminal command model. Lambda also hosts a Bash dialect,
which can resolve PATH executables and passes a pipeline through captured
stdout. This is useful compatibility work, but it should not be conflated with
Nu's native shell experience: its pipeline is currently sequential with stdout
capture, and job control, process substitution, coprocesses, and `exec` remain
unsupported ([Bash support](../../doc/Bash_Support.md)).

The practical consequence is simple: Lambda should not recommend itself as a
login shell today. Its Bash guest expands interoperability; it does not erase
the shell-functionality gap.

## 6. Performance and execution model

No direct Lambda-versus-Nu benchmark is currently meaningful: their hot paths
and workloads differ too much.

Nushell is intentionally interpreted: source is parsed into IR and evaluated;
the parsed IR is not retained as a compiled artifact after evaluation. Its
native commands are nevertheless Rust implementations, and its Polars plugin
is an excellent answer for large columnar datasets. See [How Nushell Code Gets
Run](https://www.nushell.sh/book/how_nushell_code_gets_run.html).

Under **D8.1.1v3**, Lambda uses a tiered design: a boxed AST interpreter (T0)
is the default; MIR Direct can produce a native JIT tier (T1) under the retained
tier policy. This makes Lambda better positioned for compute-heavy language
code, vector operations, and reusable transformations after JIT promotion. It
does **not** establish that Lambda is faster than Nu for shell work: startup,
filesystem latency, serialization, external programs, and Polars all dominate
many real Nu workflows.

## 7. Maturity and extension model

Nushell has the more developed day-to-day shell ecosystem: stable installation
channels, a current release cadence (0.115.0 was released on 2026-08-15),
configuration/autoload conventions, a standard library, external definitions,
and a plugin protocol with maintained Polars, formats, query, and Git-status
plugins. Its own README still calls the project MVP-quality and allows that
some commands may be unstable, so “mature” must not be read as “feature
complete.” See [Nushell releases](https://github.com/nushell/nushell/releases)
and [plugins](https://www.nushell.sh/book/plugins.html).

Lambda's published overview correctly describes it as evolving. Its advantage
is architectural cohesion rather than ecosystem breadth: one data model across
parsers, scripts, schemas, document operations, layout, and rendering. Its
formal specifications are unusually explicit about which capabilities are
normative, shipped, or pending; this comparison follows those marks rather
than treating every design ruling as a release feature.

## 8. Strategic implication for Lambda

Nushell is evidence that structured values make a radically better shell than
text-only Unix composition. Lambda shares that insight, but its differentiator
is more specific and should remain so:

> **Lambda is not “Nu with a JIT.” It is a value-semantic,
> document-native functional language whose command-line surface exposes an
> end-to-end document pipeline.**

If Lambda chooses to improve shell-adjacent workflows, the highest-value work
is the work that strengthens that identity:

1. Ship **S14.3** streams so large document/data inputs can remain lazy while
   retaining Lambda's verified-pure pipeline stages.
2. Make `input`/`output`, paths, process invocation, and structured failure
   pleasant enough for automation around document pipelines—without trying to
   reproduce every shell builtin and interactive feature.
3. Complete relational verbs/DataFrames only where one algebra over tables and
   element children makes document processing substantially better (**S14.2**),
   not merely to match Nu's Polars surface.
4. Preserve the `fn`/`pn` and value-semantics distinctions; they are the parts
   that let Lambda's eventual streaming/fusion story be semantically stronger
   than an arbitrary command pipeline.

The ideal coexistence story is straightforward:

```nu
# Nu: filesystem discovery, command orchestration, interactive inspection
ls reports | where name =~ '\\.md$' | each { |f|
  ^lambda convert $f.name -t html -o ($f.name | path parse | get stem | $"($in).html")
}
```

Nu selects and manages the files; Lambda performs the typed, format-aware
transformation. That composition treats each tool as the thing it is best at.

## References

- Lambda formal semantics: **S2.1.1**, **S7.4**, **S9.1**, **S10.1.1**,
  **S12.1**, **S14.1–S14.3** — `doc/Lambda_Formal_Semantics.md`.
- Lambda formal design: **D6.1.1–D6.1.2**, **D8.1.1v3** —
  `doc/Lambda_Formal_Design.md`.
- Lambda implementation status: formal-semantics Appendix A, especially
  `S14.2, S14.3`.
- Lambda product scope: `README.md`; Bash guest scope:
  `doc/Bash_Support.md`.
- Nushell primary documentation: [Book](https://www.nushell.sh/book/),
  [Repository](https://github.com/nushell/nushell), and links inline above.
