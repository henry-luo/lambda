# Case study: refactoring the TeX reader for structured error handling

**Date:** 2026-11-17

**Scope:** [`reader.ls`](../lmd/package/doc/latex/tex_engine/reader.ls), its immediate callers, and the Lambda behavior needed by their contracts. This is an implementation case study, not a new language ruling. The normative rules are in the [formal semantics](../doc/Lambda_Formal_Semantics.md), especially S7 and S11.4.

The comparison uses the reader at the repository's pre-refactor `HEAD` as “before” and the current worktree as “after”. It covers the error-handling and state-design changes, not a claim that the whole TeX engine has been refactored. The [implementation checkpoint](impl/Lambda_Impl_Latex_Script_Engine.md) records the work and qualification in more detail.

## The problem

The reader turns source text into TeX tokens one at a time. Each read returns a new reader snapshot so that the caller can change category codes or end-of-line behavior before reading the next token. It also captures raw spans for commands such as `\verb`, where input must be examined before ordinary tokenization.

Before the refactor, many helpers returned `any^` and intermediate calls used postfix `^`, even when the helper's meaningful result was a character, position, or scanner record. This made an unrestricted error a possible outcome throughout the call graph. It also left the actual shapes of successful results implicit. EOF, a missing delimiter, malformed source, and low-level access failures were easy to confuse because the reason for each value was not consistently expressed by its type or result shape.

`any^` is legal, but it is a poor default here. **S7.4.2v4** makes `T^` shorthand for `T | error`, while **S11.4.3** says explicit `any` already includes errors. A broad signature can be appropriate at a truly generic boundary; in a scanner that knows its data, it hides information the caller needs. Under **S7.5.1v6**, an error must be acknowledged by an appropriate receiver, handler, classification or propagation, but simply propagating a broad error through every helper makes it hard to see where recovery actually belongs.

## Contracts before and after

| Concern | Before | After | Reason |
| --- | --- | --- | --- |
| Reader and token | Untyped maps passed between helpers. | Named `Reader`, `Token` and `ReadStep` types describe state and token fields. | Make the successful path and state transition visible. |
| Source access | `ch(s, i) any^` and propagation through scanning loops. | `ch(s: string, i: int) string`; `text_span` normalizes missing slice results. | Convert a low-level query outcome into the reader's local convention once. |
| End of input | A result record could contain `token: null`. | `next(r: Reader?) ReadStep?` returns a step with a token, or `null` at EOF. | EOF is an expected absence of a next step; a successful step always contains a token. |
| Delimiter scan | A position or `null`, interpreted separately at each call site. | `DelimiterScan` carries `end`, `present`, and `closed`. | Preserve the position and distinguish absent syntax from unclosed syntax where the caller needs both. |
| Raw capture | Success and failure were maps distinguished by an `issue` field, with broad `any^` signatures. | `SourceSpan | CaptureError` at fallible capture entry points; a filtered handler converts known capture errors into diagnostics. | Make exceptional malformed capture visible and put policy at the layer that reports it. |
| Scanner loops | Several recursive helpers propagated broad outcomes. | Typed iterative scanners return positions or scan records. | Keep local scanning state and successful outcomes explicit; avoid growing the native call stack for long input. |

These are design choices for this reader. They do not imply that every parser should use the same sentinel, or that every invalid input should be silently replaced by a default.

## Normalize at the source boundary

The earlier character accessor passed a potentially broad result outward:

```lambda
fn ch(s, i) any^ => if (i >= len(s)) "" else slice(s, i, i + 1)
```

Its callers then used `ch(...)^` in loops that otherwise only needed characters. The current boundary is:

```lambda
fn ch(s: string, i: int) string =>
    if (i < 0 or i >= len(s)) "" else text_span(s, i, i + 1)

fn text_span(s: string, start: int, end: int) string =>
    slice(s, start, end) or ""
```

`""` is a **private reader convention** for no character at that position. The rest of the scanner can compare characters and advance positions without carrying an unrestricted error. The convention is safe only because a source character read by this scanner is not itself an empty string; it must not be generalized into a public rule that an empty string always means EOF.

The change relies on **S7.10.7**: `slice` returns `null` for invalid *non-error* inputs, while valid empty text remains `""`. Query functions still reject an incoming error operand (**S7.10.4**). Here `s`, `start`, and `end` have concrete non-error parameter contracts; `or ""` handles the resulting absence or empty text. It does **not** recover an error: **S7.5.3v3** preserves errors through `or`. An upstream operation that can actually fail still needs an explicit handler or a return contract that exposes the error.

Similarly, `catcode` interprets the external override map at its boundary. It computes the standard category, accepts an override only if it is an integer in the TeX category range 0–15, and otherwise returns the standard category. Its conversion is explicitly recovered:

```lambda
if (custom is int) {
    let code = int(custom) ^ { standard };
    if (code >= 0 and code <= 15) code else standard
} else standard
```

This is a deliberate fallback for optional category-code configuration, including malformed values; it should not be copied to places where bad configuration must be reported. The remaining conversion after `is int` is also a language usability issue: the current flow analysis does not always narrow the binding enough for the typed use. See the [open narrowing issue](Lambda_Issue_Ledger.md#reader-is-int-narrowing).

At the resource boundary in [`expansion.ls`](../lmd/package/doc/latex/tex_engine/expansion.ls), UTF-8 decoding has a different policy. `decode_source` handles the decoder's error and returns `{valid: bool, text: string}`. Its caller can distinguish invalid bytes from valid empty text and emit the appropriate TeX diagnostic. Returning only `""` there would erase that distinction.

## Represent normal absence and scan progress explicitly

Before, `done` could construct `{reader: r, token: null}`. A caller checked `next.token == null`, even though no token had been read. Afterward:

```lambda
pub type ReadStep = {reader: Reader, token: Token}
pub fn next(r: Reader?) ReadStep? { /* ... */ }
```

A caller checks whether the **whole result** is `null`; otherwise, it has both a token and the state to use for the next read. EOF is ordinary completion, not a failure. `next(null)` also returns `null`. The internal `read_line` uses `{reader, eof}` to retain its snapshot and make line exhaustion explicit, while the public `load_line` exposes `Reader?`.

A delimiter scan needs more than a bare optional position. The private result is:

```lambda
type DelimiterScan = {end: int, present: bool, closed: bool}
```

For optional raw groups, the reader can distinguish “no opening delimiter” from “an opening delimiter without a close”, and it retains a scan position. The caller still owns the recovery policy: an unclosed raw group can fall back to the source end; an unterminated inline delimiter can recover at the line boundary. Those choices preserve the reader's existing behavior rather than inventing a new diagnostic for every incomplete construct.

The private `hex_digit` uses `-1` as a local adapter for a missing hex digit. This is not a public system-function contract: **S7.10.3** requires system accessors such as `index_of` to use `null` for absence. The adapter is valid only inside this narrow scanner, where negative numbers cannot be hex digit values. A structured result or `int?` would be preferable if the sentinel escaped or became ambiguous.

## Report actual capture failures at the right layer

Previously, `verb_line` encoded malformed input as a map such as `{issue: "verb delimiter", reader: r}`, while success returned a map with `issue: null`. The current reader names both outcomes:

```lambda
pub type SourceSpan = {reader: Reader, end: int}
pub type CaptureError = error that ~.message in [
    VERB_DELIMITER_ERROR, VERB_UNTERMINATED_ERROR, RAW_INLINE_DELIMITER_ERROR
]

pub fn verb_line(r: Reader) SourceSpan | CaptureError {
    // ...
    if (delimiter == "" or code_point(delimiter) >= 128)
        raise error(VERB_DELIMITER_ERROR)
    // ...
}
```

A successful capture has the updated reader and byte endpoint. A malformed delimiter or unterminated `\verb` is an error rather than a success-shaped map. At the expansion boundary, where the engine knows how to report an unsupported construct, the matching error is converted to a diagnostic-shaped result:

```lambda
let scanned = reader.verb_line(source.reader)
    ^reader.CaptureError { {issue: ^.message} }^;
```

The filtered handler deals with the known capture errors. The final postfix `^` propagates a residual error to the enclosing error-admitting contract; it does not turn an unrelated failure into a capture diagnostic. **S7.6.1v9**, **S7.6.3v3** and **S7.6.6v6** define these distinct actions. The surrounding expansion code still has broad `any^` functions and diagnostic maps; this refactor improves the reader boundary without claiming the whole expansion layer has precise contracts.

There is an important limitation to `CaptureError`. Under **S11.4.6v4**, admission to a constrained type currently enforces its **base** only. Thus the return annotation `SourceSpan | CaptureError` admits an `error` even when its message does not satisfy the predicate. A named constrained type's predicate is checked by the filtered handler, as well as in the applicable `is` and `match` tests. The annotation communicates intent and exposes an error channel, but it is not a proof that every error crossing that return has one of those three messages. The [fixed admission issue](<Lambda_Issue_Ledger (fixed).md#reader-constrained-error-return-fixed>) describes the compiler and shared-core fixes and this limitation. The message predicate also couples classification to message text; the constants keep the current producer and classifier aligned, but a richer stable error identity could be considered separately.

## Why snapshots instead of one mutable reader closure?

A closure enclosing scanners and mutable position can be natural for a procedural parser. This reader's existing contract has a different need: callers receive a snapshot after each token, can derive a modified snapshot with different category codes, and can retain or reuse an earlier one. The current `next` returns a new `Reader` together with its token, so those transitions remain explicit and independently testable.

Local mutable variables are still useful *inside* an `fn`: loops keep positions, nesting depth, or the current snapshot while constructing a result. That is local implementation state, not an escaping shared reader. Confined `pn` helpers cannot simply be called from the existing `fn` API under **S12.1.1v3**. A closure-based procedural API would therefore be an interface and ownership change, not a mechanical cleanup; the snapshot form fits the current callers and category-code changes. It does mean passing and updating sizable state records, so this is a tradeoff rather than a general prohibition on closures.

## A practical Lambda error-handling pattern

The case suggests the following design process, subject to the domain's actual recovery policy:

1. **Describe the normal data precisely.** Use a concrete result type for a successful computation. Use `T?` when absence is an expected result; use a record when the caller needs both status and data. Do not make EOF an error merely because it stops a loop (**S11.4.2v3**, **S7.10.1v3**).
2. **Handle input uncertainty at a boundary that understands it.** Validate or decode external input and either return structured data, choose an intentional fallback, or expose a specific failure. Keep a fallback distinguishable from a valid value whenever the caller needs to know the difference.
3. **Expose failure when the caller must decide.** Use `T | error` / `T^`, or a more specific error union where supported, for a genuinely fallible operation. Avoid declaring `any^` simply to quiet a call graph. Declared returns must admit the actual error outcomes (**S11.4.2v3**); do not assume a constrained predicate is enforced at a boundary (**S11.4.6v4**).
4. **Acknowledge each fallible outcome deliberately.** `e ^ { fallback }` recovers errors, `e ^E { ... }` recovers matching errors, and `e^` propagates to an error-admitting return. Receiving an error in an appropriate union or classifying it can also acknowledge it; immediate recovery is not mandatory (**S7.5.1v6**, **S7.5.6**, **S7.6.3v3**). A handler's own new error still needs an appropriate receiver.
5. **Keep absence and failure distinct.** `or` handles `null`, `false`, and `""`, not errors (**S7.5.3v3**). Use it only when all relevant falsy values share the desired default. A silent default is wrong when it discards information needed for a diagnostic or changes valid empty input into failure.
6. **Let downstream helpers work on admitted data.** Once an input boundary has produced a `string`, position, or structured result, ordinary scanners can return concrete types. This reduces repeated defensive propagation without hiding errors that arise later at a different boundary.

Centralizing recovery means centralizing each *decision* where its context is known; it does not mean swallowing all errors in one catch-all or forbidding errors above the input layer. The right boundary differs for a missing character, invalid UTF-8, and a malformed TeX capture.

## Evidence and remaining work

The refactor was checked with reader contract tests for EOF, category defaults, Unicode byte positions, scanner snapshots, malformed and incomplete captures, long scans, and filtered capture errors: [`test_tex_script_reader_contracts.ls`](../test/lambda/latex/test_tex_script_reader_contracts.ls). The constrained error admission and propagation behavior has a separate [regression fixture](../test/lambda/constrained_error_return.ls). Interpreter, JIT and AUTO focused runs passed, including forced-GC checks. Differential checks against the earlier reader matched 782 token/state and raw-capture cases across interpreter and JIT, and all 285 LaTeX entry scripts compiled. These checks do not establish complete TeX or LaTeX feature parity.

At the last qualification, the input baseline passed 5,312 tests with three skips and Test262 passed 40,261/40,261. The Lambda baseline was not fully green: its script suite passed 2,445/2,446, with a WebRTC dependency failure, and the aggregate also had a PDF allocation-test abort and an editor assertion. See the [fixed issue record](<Lambda_Issue_Ledger (fixed).md#reader-constrained-error-return-fixed>) for scope and evidence.

Further work includes narrowing the remaining broad expansion-layer contracts, deciding whether error identity should be independent of message strings, and addressing the open `is int` flow-narrowing issue. Predicate enforcement at typed boundaries is a separate language-design question governed by S11.4.6v4; this case does not propose changing that ruling implicitly.
