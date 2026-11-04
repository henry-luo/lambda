# Strict CSV input for reviewed imports

Status: implemented, 4 November 2026. The accounting bank-import design needs
lossless, bounded parsing of user-selected CSV. The existing reader heuristically
detects the separator and header, warns while dropping extra map fields, and
accepts a quoted field followed by non-delimiter text as another field. A
caller cannot distinguish those warnings from successful data. This is a reader
limitation, not a reason to alter source content or duplicate the tokenizer.

Add the opt-in `parse(text,'csv-strict')` and equivalent input format;
`{type:'csv',flavor:'strict'}` is an equivalent spelling. The distinct format
name fails on older runtimes that silently ignore unknown flavors. It treats the first row as a required header and comma as the delimiter,
requires UTF-8 and no NUL, distinct nonempty header names and exactly the header
field count in every data row, and returns the normal array of row maps. Quoted
fields may contain commas, CR/LF and doubled quotes; reject an unclosed quote,
a quote in an unquoted field and any text following a closing quote before a
comma, line ending or end of input. A trailing comma is an explicit empty last
field, represented as null like the existing reader. Blank physical rows are
ignored. The ordinary CSV/TSV reader retains its established behavior.

Parsing remains an ordinary pure data transformation where S12.1.3v2 applies;
input/file reads require D7.5.2/D7.5.5 authority. `flavor` is an existing input
option and this adds a format-specific value without changing a formal ruling
(S17.8.1). Tests must exercise `parse` and `input`, exact row widths, malformed
quotes and headers, trailing empty fields, UTF-8/NUL, multiline fields and
legacy behavior in interpreter and MIR; run the required Lambda baseline.
Track any environmental or cross-platform limitations rather than weakening
checks.

The focused strict CSV cases pass in interpreter and MIR, including forced-GC
runs. A later full unrestricted aggregate passed 8,754/8,754 tests, including
the strict CSV golden, input-parser suite and security runtime suite. Earlier
isolated media and chart failures did not recur in that final aggregate.
