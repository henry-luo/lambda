# Source Format Writers in Lambda Script

**Status (2026-10-15):** initial implementation of the six requested writers in
**lambda.doc.misc**: AsciiDoc, troff/man, Typst, EML, vCard, and iCalendar.
Native build and package compilation pass. Dedicated round-trip tests pass
for EML/VCF/ICS and expose existing structural readback gaps in the three prose
readers. External tool conformance remains unverified.

**Scope:** source serialization of existing Mark Doc and record values. This
does not add an AsciiDoc, groff, or Typst execution engine, send email, or acquire
external resources during formatting.

**Formal linkage:** normalized document content follows **S2.6.1v2,
S2.6.2–S2.6.5**. Parser-owned data construction follows **D7.1.5**. Shipped
source modules and the runtime/IO boundary follow **D7.1.1, D7.2.1–D7.2.4**;
package invocation retains precise roots under **D5.3.3**. No formal ruling is
changed. Writer syntax and the record precedence below are the implementation
contract for these initial profiles, not new language semantics.

## API and routing

```lambda
import writers: lambda.doc.misc
let document = parse("# Title\n\nSome **text**.", "markdown")^
writers.format_report(document, {type:"asciidoc"})^
writers.man(document, {title:"LAMBDA", section:"1"})^
writers.typst(document)^
```

The facade exports **format_report**, **format**, **format_file**, and convenience
functions **asciidoc**, **man**, **typst**, **eml**, **vcf**, and **ics**.
**format_native** is the runtime bridge. Default target is **asciidoc**.
**format_report** returns **{text, diagnostics}**; each diagnostic contains
**code**, **severity**, **element**, and **message**. **format** rejects the first
diagnosed loss by default; **loss:"allow"** returns degraded output. Invalid
structures, control characters in wire fields, and unsupported encodings remain
errors in either mode.

Native **format(value, "asciidoc")**, formatted file **output**, and
**lambda convert -t asciidoc** use the same source package. Output aliases are
**adoc**, **troff**, **vcard**, **ical**, and **icalendar**. Man title arguments are
**title**, **section**, **date**, **source**, and **manual**. No clock-derived
metadata is invented. **format_file** additionally accepts **input_type** and
**input_flavor**. CLI **--loss error|allow** now covers these writers and wiki
writers. The runtime-free **format_data** API refuses package-backed output.

File output recognizes **.adoc/.asciidoc/.asc**, **.man/.1–.9**,
**.typ/.typst**, **.eml**, **.vcf/.vcard**, and **.ics/.ical**.
For permissive file output, format to text with package options and then call
**output(text, path, "text")**; file output otherwise uses strict defaults.

## Mark Doc profiles

One traversal in **misc/markup.ls** implements paragraphs, headings, nested
ordered/unordered lists, definition lists, code, emphasis, links, images,
quotes, rules, breaks, and tables. **misc/common.ls** owns shared tree/report
helpers; the existing wiki writers now reuse them. Wiki tasks retain their
existing shared task model. **misc/grid.ls** shares column reservation and
row-span advancement with the existing DocBook CALS importer. Prose table
writers account for cells reserved by earlier rows and reject overlapping or
out-of-table spans.

| Target | Mapping |
|---|---|
| AsciiDoc | **meta.title** uses **=**, body h1 uses **==**; inline markup, listing/source blocks with collision-free fences, list continuation, native checkboxes, table spans, block anchors, title/author header |
| troff/man | **.TH/.SH/.SS**, **.PP**, nested **.RS/.RE**, **.IP/.TP**, font escapes, **.nf/.fi**, and **tbl** tables; source text is protected against request/escape interpretation |
| Typst | Equals headings and explicit **text**, **strong**, **emph**, **raw**, **link**, **image**, **list/enum**, **quote**, and **table** calls, including cell spans and heading/table labels; strings are quoted without evaluating source content |

Limit diagnostics cover unsupported elements/attributes, figures and advanced
layout, footnotes/citations/math, target text effects, richer task workflows,
man images/table spans/deep headings, AsciiDoc body headings deeper than five
levels and nested list starts, and raw
source from another syntax. Typst is source output; its existing input parser
does not interpret every generated function call. Native tool conformance is
therefore a separate pending check, not an asserted reader/writer round trip.

## Record precedence and wire representation

### vCard and iCalendar

Both formats use the existing parser representation:

1. If **entries** is present, its ordered **{name, value, parameters?}** records
   are authoritative. Repeated properties and nested calendar component maps
   stay in source order. Existing parser values are already wire-escaped;
   entries default to **raw:true**. **parameter_source**, when present, retains
   original parameter quoting and repeated parameter names; remove it before
   editing the convenience parameter map. A newly authored semantic entry can set
   **raw:false** to request TEXT and parameter escaping.
2. Otherwise **properties** is authoritative. Its values are wire values;
   array values allow repeated properties.
3. Otherwise recognized convenience fields are serialized as semantic values.

Convenience fields are views of retained source and do not override entries.
Edit the entry array to preserve parameters/repeats, or remove **entries** and
**properties** to author from fields. Unknown top-level fields produce loss
diagnostics. A vCard input may be one contact, an array of contacts, or
**{contacts:[...]}**. Calendar components may contain **components** arrays when
not already represented in ordered entries.

vCard emits versions **3.0** and **4.0** (new contacts default to 4.0).
iCalendar emits **2.0**. VERSION is emitted immediately after BEGIN.
Missing required contact/component properties are diagnosed; no person name,
UID, event date, or email date is fabricated. A missing calendar PRODID uses
the formatter's own **-//Lambda//doc.misc//EN** product identifier.

Both writers use CRLF, escape semantic TEXT and structured contact fields,
quote parameters, and fold content lines within 75 UTF-8 octets without
splitting a scalar. vCard 4/iCalendar semantic parameters use RFC 6868 caret
escaping. Fresh date-part maps support DATE, floating/UTC DATE-TIME, and TZID;
duration maps support weeks or days/hours/minutes/seconds. Retained RRULE,
VTIMEZONE, extension fields, and alarm properties remain wire values; the
writer does not resolve recurrence or timezone databases.

### EML

The reader now retains **header_entries** alongside its convenience **headers**
map, and **raw_body** alongside its display **body**. This keeps repeated
headers and MIME source, including attachments that the display parser does
not model. **header_entries** takes precedence over **headers**; missing common
headers can be supplied through top-level **from**, **to**, **cc**, **bcc**,
**reply_to**, **subject**, **date**, and **message_id**.

**raw_body** is authoritative wire content. Remove it to rebuild a message
from semantic **body** and **attachments**. A string body with a retained
Content-Transfer-Encoding is also treated as encoded wire content. To replace
that body with plain text, remove the transfer header or supply a MIME part
map. Removing original multipart source and rebuilding only its display HTML
diagnoses loss of the original attachments.

New MIME parts accept **content** (string, binary, or HTML element),
**content_type**/**type**, **encoding**, **headers**, **filename**, **content_id**,
**disposition**, and nested **parts**. New content defaults to Base64;
quoted-printable and checked 7bit/8bit text are also supported. UTF-8 subjects
and structured address display names use encoded words. Header line breaks
are rejected, long headers fold at whitespace, encoded body lines are wrapped,
and multipart boundaries are chosen to avoid their serialized parts.

This is serialization, not complete RFC validation or transport negotiation.
Fresh text supports UTF-8/US-ASCII; SMTPUTF8 structured headers, other charset
transcoding, and RFC 2231 continuation of exceptionally long filenames remain
outside the initial profile. Original encoded source can still be retained.

## Reader corrections needed by the writers

RFC 6350 and RFC 5545 remove CRLF plus exactly one continuation whitespace
character. The previous shared reader inserted a space, corrupting values
when standard folding split a word. **input-rfc-text.h** now unfolds entire
VCF/ICS content lines before names and parameters are parsed and preserves
empty values. Email headers instead remove CRLF while retaining following
whitespace, as RFC 5322 requires.

## Verification and open readback gaps

- **make build** succeeds.
- **./lambda.exe --dry-run lmd/package/doc/misc.ls** compiles the facade and
  its imported writers; the same compilation check succeeds for
  **lmd/package/doc/wiki.ls** after shared-helper extraction.
- **test/lambda/doc_misc_roundtrip.ls** and its golden pass 31 checks for newly
  authored contacts, calendar events/alarms, MIME messages and attachments,
  built-in routing, validation, and loss diagnostics. Interpreter, JIT, and
  JIT forced-collection/poisoning runs pass (D5.3.3).
- **test/script_format_roundtrip.py**, registered through its Node runner in
  the **input** baseline, runs 43 checks across package, built-in **format**,
  automatic file **output**, and CLI boundaries: **31 pass, 12 fail**. All 16
  record round trips and 15 alias checks pass. Record comparisons retain
  ordered duplicate map entries (D2.6.5v3), and passing round trips also require
  byte-identical output after a second parse/write. UTF-8 scalar boundaries,
  CRLF and RFC physical line limits are checked independently.
- **make test-lambda-baseline** completed with **7138/7141** passing, including
  the new golden and all **2112/2112** input-parser checks. The broader gate
  was not clean: music-layout GC stress timed out, the MOBI script failed,
  and the LaTeX phase-3 corpus timed out. The LaTeX case passed in isolation
  on retry. These results do not establish a clean whole-repository baseline.
- The following existing reader gaps remain failing assertions. There are no
  skips or failure-shaped goldens. Their output syntax mappings are not new
  language semantics rulings.

| ID | Reader gap | Affected checks |
|---|---|---|
| MSF-RT1 | AsciiDoc counts title/section heading markers alike and mistakes listing attributes/fences for underline headings. | 4 prose paths |
| MSF-RT2 | Man treats **.TH** as body content, misses next-line section titles, and misreads roff font/text escapes and literal markup characters. | 4 prose paths |
| MSF-RT3 | Typst treats generated literal-content function calls as text. | 4 prose paths |

The first runtime probes also found and corrected writer validation
expressions contributing extra values to returns, and element attribute
checks iterating children instead of **map(element)**. MIME client
interoperability, native AsciiDoc/groff/Typst compilation, and a complete
formatter GC stress sweep remain unverified. Commands and fixture coverage are in
**doc/Script_Formats_Support.md**, **Verification**.

## Syntax references

- [AsciiDoc syntax](https://docs.asciidoctor.org/asciidoc/latest/syntax-quick-reference/)
- [GNU troff manual](https://www.gnu.org/software/groff/manual/groff.html)
- [Typst syntax](https://typst.app/docs/reference/syntax/)
- [RFC 5322: Internet Message Format](https://www.rfc-editor.org/rfc/rfc5322)
- [RFC 2045: MIME transfer encoding](https://www.rfc-editor.org/rfc/rfc2045)
- [RFC 2047: encoded header words](https://www.rfc-editor.org/rfc/rfc2047)
- [RFC 2231: MIME parameters](https://www.rfc-editor.org/rfc/rfc2231)
- [RFC 6350: vCard 4.0](https://www.rfc-editor.org/rfc/rfc6350)
- [RFC 2426: vCard 3.0](https://www.rfc-editor.org/rfc/rfc2426)
- [RFC 5545: iCalendar](https://www.rfc-editor.org/rfc/rfc5545)
- [RFC 6868: parameter caret encoding](https://www.rfc-editor.org/rfc/rfc6868)
