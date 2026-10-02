# PDF output virtual list MVP

> **Status:** MVP implemented and verified, 2026-10-02; follow-up issues remain open.
> **Authority:** S2.6.6v2, D2.6.12v2; remaining contracts SO48/DO32.
> **Design:** [PDF Output](../Lambda_Design_PDF_Output.md).

The MVP constructs a PDF file's bytes through an extended Element/List and
writes them with existing `output(file, path)`. It leaves EvalContext and
allocation contexts unchanged. Radiant's existing PDF exporter is not
replaced in this step.

## Supported path

- Both interpreter and MIR Direct select the builder after evaluating the
  file attributes, accepting symbol `'pdf'` or string `"pdf"`.
- File candidates reserve an extended allocation. Only the selected PDF
  destination sets `is_virtual`; ordinary file elements use normal content.
- `list_push` and `list_push_spread` dispatch through a common
  `MarkOutputBuilder` interface to `PDFBuilder`. It copies length-delimited
  string/binary spans, drops null/empty text, and recursively spreads lists.
- Map contributions encode directly into the buffer as dictionaries with
  surrounding spaces. Nested maps and arrays preserve object slots, including
  null and empty text; typed multidimensional arrays use existing slab readers.
  Map spreads retain first key order and last value under S2.3.1. Temporary
  dictionary metadata holds borrowed field descriptors, never copied Item
  arrays or serialized object strings, and is released before append returns.
- Names use length-aware `#XX` byte escaping and reject NUL. ASCII strings
  escape delimiters and controls; other text uses UTF-16BE hex with a BOM and
  rejects malformed UTF-8. Object binaries use hex. Integers keep their exact
  carriers; finite floats use the existing shortest round-trip formatter with
  exponent expansion (USER, 2026-10-02). Decimals remain deferred.
- ASCII literal escaping is shared with the existing C PDF writer through a
  length-aware `lib/escape` helper, including NUL and checked allocation. The
  existing writer keeps its byte-string policy; object Unicode conversion is
  supplied by PDFBuilder.
- The file has no output Item array. Its builder owns a growing byte buffer;
  the existing GC external-payload cleanup destroys it on collection or
  runtime teardown. The builder retains no Item edges.
- Finish returns the file or an error. Unsupported values reject the whole
  construction; output cannot write a partially failed buffer. Empty output,
  repeated output, append mode, and atomic output reuse the existing I/O path.

Use the file value for output. Retained-child operations are outside the MVP;
mutable cloning/COW reject the virtual destination rather than copy a
base-sized object without its builder. Public observation and copy semantics
remain SO48. The internal zero Item count does not mean that serialized bytes
are an empty logical content sequence.

## Deferred work

All §5.6 design issues remain open: producer forwarding, public retained-file
reads and updates, nested-file contribution, remaining structured PDF encoders,
general byte-offset/stream accounting, and recovery within streaming
expressions. Ordinary functions still build their return values before the
file consumes them. Native font/image/codec helpers and the Radiant Velmt
export bridge remain future work.

Direct array/scalar contributions, virtual source maps/arrays, decimal values,
qualified/private/JS identity keys, references, streams, and PDF wrapper values remain
unsupported. Dictionary and object nesting is bounded at 64 levels; cycles and
excessive depth reject the whole file. Reader-specific numeric range policy is
still open even though the writer preserves the selected numeric spelling.

The dictionary follow-up exposed a pre-existing D5.3.3 ownership defect in
map contract admission: shape rebuild rooted a wide scalar borrowed from the
old map data buffer, but GC moved that buffer before the store. The rebuild
now uses the existing scalar-home adoption helper before allocating, preserving
the original value and type. The nullable `i64` case is exercised by
`proc_pdf_maps.ls` with forced GC. A quoted Lambda map-key probe also exposed
existing truncation of an embedded NUL before encoding; the serializer's
length-aware rejection is checked through an Input-owned JSON map, and that
frontend key issue remains separate work.

The demonstration in `test/lambda/pdf/pdf_file_mvp.ls` authors a complete
one-page PDF with an ASCII content stream. It computes offsets from those
ASCII fragments in Lambda; this is a test of the byte destination, not a
general Unicode byte-position API or a shipped PDF package.

## Verification

- `proc_pdf_file.ls` checks mixed bytes, dynamic format selection, producer
  list spreading, empty files, reuse, rejection, and ordinary file behavior.
- `proc_pdf_maps.ls` checks dictionaries, names, escaped strings, Unicode,
  binary values, nested/typed arrays, spreads, nullable native lanes, exact
  wide integers, exponent-free floats, separators, and rejected values.
- `PdfFileBuilder.CopiesBorrowedBytesWithoutItemsAndSurvivesCollection`
  verifies borrowed-byte copying, no Item array, and exact GC destruction.
- Three more native builder tests check destruction of the source Input,
  cyclic dictionaries, and dictionary metadata allocation failure. Neither a
  failed object nor a failed allocation publishes partial output.
- The document fixture checks an end-to-end PDF write; byte-level validation
  verifies object offsets, stream length, and `startxref`.
- The initial dictionary baseline passed **6,176/6,177**; a MIR fixture's
  bare `1535` count also matched that substring in an ASLR pointer constant.
  Both affected sidecars now match the actual `and` guard instruction under
  D4.4.4v4, preserving their exact guard counts. The final baseline passed
  **6,177/6,177** (2,104 input and 4,073 runtime), including all four native
  builder tests. The existing C PDF writer suite passed **42/42** after sharing
  its literal-string escaping helper with PDFBuilder.
- All three committed scripts pass on interpreter and JIT with
  `LAMBDA_GC_FORCE_EVERY=1` and `LAMBDA_GC_POISON_FREED=1`. A supplemental
  function-return/handled-rejection probe also passes on both tiers.
- Exact mixed payload: `41 00 FF C3 A9 4C 00 FF 52 0A`, including reuse via
  atomic output. The demonstration PDF is 459 bytes; all four object offsets,
  its stream length, and `startxref` match the emitted bytes.
- Lambda's PDF reader parses the generated document. Independent `pdfinfo`
  validation was unavailable: the installed executable could not load its
  Fontconfig dynamic library.
- A supplemental full-document probe uses a map for its stream dictionary:
  **461 bytes**, four verified xref offsets, `/Length 28`, and matching
  `startxref`. Lambda's PDF reader parses that document too. This probe measures
  an ASCII header through `output` and reads it back; it does not close SO48's
  general byte-position API.

Run the complete example with:

```sh
./lambda.exe run test/lambda/pdf/pdf_file_mvp.ls
```

It writes `temp/lambda_pdf_mvp.pdf`. Construction buffers the final bytes in
memory; this MVP does not stream to disk or eliminate producer-return values.
