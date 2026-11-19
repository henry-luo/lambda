# RTF input and script parser

## RTF script parser: direct and grammar readers (2026-11-13)

This record documents the RTF readers in `lambda.doc.rtf`. **D9.3.4** places
both script readers in the document package; **D9.1.1v4** and **D9.1.3v2**
retain the independent native input ownership boundary; the former native RTF
parser has been removed. Errors from the script API use **S7.4.2v4**.

### Public API and shared semantics

`rtf.parse(source)` and `rtf.parse_file(path)` use the direct Lambda reader.
`rtf.parse_grammar(source)` selects the grammar reader for differential tests
and performance analysis. Both return the same `doc` element and share the
semantic lowering in `lmd/package/doc/rtf.ls`: nested groups save and restore
formatting, font and color tables populate document state, text becomes styled
spans in paragraphs, and `par`, `row`, `page`, `line`, `tab` and `cell`
produce the corresponding document boundaries. This is the existing bounded
RTF subset, not full code-page, embedded-media or table-layout support. The
script parser rejects a visible group at nesting depth 512. Non-body
destinations and groups marked with `\*` are skipped.

The default reader in `lmd/package/doc/rtf/lexer.ls` scans characters on demand
and emits control, hex, symbol, brace and text records. Its integer conversion
saturates like the native parser; Unicode fallback counts escaped characters
as one. No grammar tree is allocated for the default path.

The experimental reader in `lmd/package/doc/rtf/grammar.ls` and
`grammar_lexer.ls` first uses a Lambda grammar to recognize the complete record
stream. It converts the resulting elements to records, computes cumulative
character offsets, and uses binary search to locate the record at each source
position; semantic lowering is shared with the direct reader. The grammar is
flat rather than recursively grouping braces: the grammar engine has a nesting
limit, whereas ignored destinations can contain deeper balanced groups without
hitting the visible-group limit. A 1,500-level ignored destination is covered
by the differential test. Character offsets, not grammar byte spans, match
Lambda string slicing, including Unicode text and fallback positions.

The native RTF parser and the generic `input(path, 'rtf')` dispatch have been
removed. The viewer uses `rtf.parse` and therefore the direct script reader;
call `rtf.parse_file(path)` for file input.

### Performance evidence

On 2026-11-13, ten interleaved process pairs used the same pinned release
executable (`48e95c7c4e26ae6689ef04f31097d00af02643d533c320e16ff5689a8c2bcfa8`),
automatic tier selection, identical inputs and ten untimed warmups per sample.
The timed region parses and constructs the document; file reading, process
startup and serialized-output checking are excluded. All compared serialized
documents and loop checksums matched. The expanded input wraps eight copies of
the existing fixture. Times are medians in milliseconds per parse:

| Input | Original direct control | Shared default direct | Grammar reader |
|---|---:|---:|---:|
| Existing fixture, 3,827 bytes | 3.450 | 3.637 | 6.467 |
| Expanded fixture, 30,624 bytes | 23.111 | 23.726 | 62.286 |

The grammar reader was approximately 1.78x and 2.63x slower than the current
default on these inputs. Timing spread and machine load limit conclusions
about the small difference between the two direct columns; these measurements
are not a causal breakdown of grammar costs. The grammar tree and offset index
are extra allocations and possible optimization targets, not independently
profiled causes. Per-sample times, source and executable hashes, and output
hashes are in `temp/rtf-grammar/dual-benchmark.json`.

The native-output oracle and its provenance are retained in
`test/input/rtf/`; the direct and grammar readers are compared against it in
the script tests. The input baseline recorded the pre-existing LI-001, LI-002
and LI-013 failures. Recheck both readers and the viewer after changes.
