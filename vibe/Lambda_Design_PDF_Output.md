# Migrate Radiant PDF output to Lambda

> **Status:** Virtual-list MVP authorized, 2026-10-02; remaining migration
> details are open. Confirmed direction: expose Radiant's
> view tree through `Velmt`; generate PDF in Lambda; consume a sequence of
> string/binary contributions through a special MarkBuilder; skip null and
> empty text; activate only for an explicitly PDF-formatted file. Its content
> destination extends List with a builder pointer and an `is_virtual` flag;
> `list_push` dispatches through that destination. EvalContext and allocation
> contexts are unchanged. Native-helper
> cases are deferred to phase 2. Map support is authorized, including nested
> dictionary/array values. Finite floats use shortest round-trip digits;
> decimals remain deferred (USER, 2026-10-02). The raw/dictionary subset and
> virtual-list mechanism are recorded in S2.6.6v2/D2.6.12v2; SO48/DO32 retain
> the remaining mappings and API issues.
> **Scope:** PDF export, the view projection it needs, and a reusable file
> content builder. PDF input and CSS layout remain separate subsystems.
> **Spec linkage:** S2.2.2v2/S2.2.4, S2.6.1v2–S2.6.5, S12.1.1v2,
> S16.7.1, S2.6.6v2; D2.6.5v4/D2.6.6v3/D2.6.12v2, D7.4.4/D7.4.5v2,
> D5.3.3–D5.3.4, D5.4.1, D8.2.6.

## 1. Proposed outcome

Move PDF document construction and serialization into the shipped
`lambda.pdf` package. Radiant continues to load documents, resolve CSS, and
lay out views. An export session exposes the resulting tree as Lambda
elements through `Velmt`, allowing Lambda code to inspect final geometry,
text, styles, and resources without copying the complete view tree into
materialized elements.

Lambda functions produce PDF syntax as strings and stream payloads as binary
values. A specialized MarkBuilder consumes that content while it is
evaluated, appending bytes to the destination. It must not first accumulate
the entire output as an Item array and serialize it afterwards.

```text
Document loading and CSS layout in Radiant
    → export session with a stable view tree
    → Velmt root and virtual children
    → Lambda PDF resource planning and drawing emission
    → virtual content list → PDFBuilder
    → completed output buffer for the file
    → explicit output operation or CLI file write
```

The requested source shape is:

```text
<file format:'pdf', pdf_output(); ...>
```

Here `pdf_output()` represents a producer of strings and binaries. It does
not return an HPDF handle. The final API will also pass the view root and
export options. This is a design sketch, not an executable example or a
promise that the new constructor semantics already exist.

## 2. Confirmed direction and format naming

| Topic | Direction |
|---|---|
| File content | An ordered sequence of contributions, principally strings and binaries, written in order. Null and empty text contribute no bytes. Logical content and serialized buffer storage are distinct. |
| Activation | Only an explicit PDF file constructor. Ordinary `<file>` elements and script top-level content retain their current behavior. |
| Output destination | Allocate an extended content list with `is_virtual` set and a pointer to its PDFBuilder. `list_push` dispatches through that pointer; ordinary lists retain their array storage. No output context is installed or changed. |
| Native helpers | Do not design or require font/image/codec/raster-fallback helpers in phase 1; define their treatment in phase 2. |

### 2.1 The existing I/O APIs use both names

Source inspection gives the following result:

| API | Direct format argument | Options-map selector | Evidence |
|---|---|---|---|
| `input` | `input(path, 'pdf')` | `type` | `lambda/runtime/lambda-eval.cpp:5486`, `fn_input2` |
| `parse` | `parse(text, 'json')` | `type` | `lambda/runtime/lambda-eval.cpp:5880`, `fn_parse2` |
| `format` | `format(value, 'json')` | `type` | `lambda/runtime/lambda-eval.cpp:6083`, `fn_format2` |
| `output` | `output(value, path, 'json')` | `format` | `lambda/runtime/lambda-proc.cpp:478`, `pn_output3` |

The output options form also appears in
`test/lambda/proc/test_pipe_file.ls:69` and the working design
[Lambda Shell](Lambda_Shell.md). The example at
`doc/Lambda_Sys_Func.md:1213` instead uses `{type: 'json'}` for `output`,
which disagrees with that implementation and working record. This proposal
records the inconsistency; it does not silently unify or rename the I/O APIs.
`pn_output_internal` already writes string and binary sources as raw bytes
(`lambda/runtime/lambda-proc.cpp:234` and `:251`); that behavior does not yet
provide a streaming file-element constructor.

**Recommendation: keep `format` for the new file constructor**, matching the
output operation and the user's spelling:

```text
<file format:'pdf',
    "%PDF-1.4\n"
    pdf_output(view)
>
```

The attribute/content boundary comma is required by **S16.9.3**. Proposed
activation checks the evaluated file tag and `format` value before content
evaluation. Accept symbol `'pdf'` and string `"pdf"`, matching the I/O
selector convention; a dynamic selector must behave like a literal one.
`type:'pdf'` is not a second implicit activation spelling. No filename or
ambient script context activates the special builder.

Construction is proposed to remain an in-memory value operation. Actual file
I/O stays at the procedural boundary, consistent with **S12.1.1v2**. The
requested output buffer does not by itself imply incremental disk writes or
constant memory independent of final file size.

**S2.6.4** continues to distinguish string and binary content contributions.
Writing both to a byte destination does not make them one semantic child.
**S16.7.1** continues to govern script top level. The new format conversion
belongs to an explicitly selected destination, not ordinary content append.
Observation of retained file content and mutation remain an API design item
in §5.3; the draft must not infer that contract from buffer storage alone.

## 3. Current implementation and migration boundary

The current file export dispatch calls `render_html_to_pdf()` or its document
transform counterpart. A `RenderExportSession` loads and lays out the
document; `render_pdf.cpp` walks the view tree through the shared
`RenderBackend`, lowers PaintIR to `HPDF_*` calls, and saves the document.
Those calls are implemented by Lambda's own `lib/pdf_writer.c`, whose
header describes a libharu-compatible subset.

The existing exporter creates one content-sized page. It writes text with
standard PDF fonts and uses raster fallbacks for gradients, SVG subscenes,
and effects. The writer emits PDF 1.4 and currently records compression
settings without compressing streams. These are implementation observations,
not language rulings or a target specification for future PDF features.

| Responsibility | Proposed owner |
|---|---|
| Load input, resolve CSS, compute layout and export bounds | Radiant |
| Expose views, resolved geometry, and resource access | Native Radiant module through virtual carriers |
| Shared paint ordering and generic painting mechanisms | Existing Radiant rendering infrastructure |
| Choose PDF representation, allocate object IDs, plan resources and pages | Lambda PDF package |
| Escape PDF names/text, format numbers, emit operators and dictionaries | Lambda PDF package |
| Assemble stream objects, cross-reference data, trailer and metadata | Lambda PDF package |
| Append length-delimited bytes and finalize the file value | PDFBuilder through a reusable virtual-list builder interface |
| Write completed bytes to a destination | Existing procedural I/O or CLI boundary |

The eventual migration succeeds when the production PDF export path no longer
calls `HPDF_*` or depends on `lib/pdf_writer.c`. Phase 1 proves the new path on
its declared subset. Phase 2 decides helper-dependent cases before full
replacement; their absence is not a reason to silently omit rendered content.

## 4. Exporting the view tree through Velmt

### 4.1 Reuse the carrier and declare the export interface

**D7.4.5v2** already establishes `Velmt` as the virtual carrier for
element-shaped host data. **D7.4.4** requires declared interfaces and
record-owned hooks. Extend that infrastructure with an export view interface;
do not introduce a second container taxonomy or expose native addresses as
Lambda values.

The current `RadiantVelmt` is a native custom-layout snapshot. Its Lambda
wrapper is guarded by an active layout-pass ID, and its recursive child
projection has a depth allowance. That wrapper is useful precedent but is
not an export-lifetime contract. A finished PDF export must cover the complete
rendered tree without inheriting a custom-layout depth cutoff.

Proposed exposed data includes:

- View kind, element tag and attributes, and virtual child access.
- Final content/border bounds and the declared coordinate space of each field.
- Resolved paint properties, transforms, clips, and effect groups.
- Text fragments with final positions, baselines, font identity, and available
  glyph/advance data; original source text alone is insufficient.
- Paths and document-scoped resource identifiers. Image, font-resource, and
  SVG-subscene helper interfaces are deferred to phase 2.

Expose computed values needed by the exporter instead of having Lambda
reimplement CSS resolution or infer geometry from DOM attributes. Use
document-scoped keys for resource deduplication; pointer identity must remain
an implementation detail.

### 4.2 Preserve paint order and rendering semantics

DOM sibling order is not the complete paint order. Stacking contexts,
positioned descendants, generated content, clipping, and effect boundaries
must match the existing renderer.

The proposed bridge therefore provides generic ordered paint access from the
Velmt root, derived from the shared walker and PaintIR. Lambda owns the
translation from those operations and view fields to PDF. Existing native
helpers that are needed outside their current file should be promoted and
shared, rather than copied into the bridge or rewritten as a second CSS
painting algorithm.

The bridge can expose operation elements in bounded batches or through an
export-session iterator; it must specify who owns a yielded record and how
long it remains valid. A consumer may retain a record only if its payload is
pinned or copied into owned storage. A full duplicated tree or a complete
Lambda array of paint commands is not required. The precise traversal API is
an implementation design item to settle against the existing walker.

### 4.3 Hold one stable export session

The session pins the document, view storage, and borrowed resources from
projection through completion or failure. Layout mutation is excluded during
the synchronous export. Escaped wrappers become invalid on session close;
they must never retain an unchecked pointer into reclaimed view storage.
The existing host-invalidation convention governs generic wrapper reads;
export itself must report invalidation as failure, not silently emit a
truncated tree.

Use precise `RootFrame`/`Rooted` ownership for retained Lambda values and
trace callbacks for retained Item edges, following **D5.3.3–D5.3.4**. A GC
root for a wrapper alone does not keep a separate native arena alive: the
session must own or pin that lifetime explicitly.

## 5. The file content MarkBuilder

### 5.1 Separate file contributions from PDF object values

There are two encoding contexts. At the file-content level, strings and
binaries are raw fragments. Within a PDF array, dictionary, or explicit
value wrapper, each value is a PDF object. The format encoder must carry this
context explicitly rather than use content normalization recursively.

The current subset implements map contributions and physical map/array object
values under **S2.6.6v2/D2.6.12v2**. The table also describes future direct
scalar/array contributions, virtual source containers, and explicit wrappers;
those remain SO48. Ordinary lists still spread at the file boundary.

The PDF object forms below follow Adobe's
[PDF Reference, sections 3.2 and 3.8.1](https://printtechnologies.org/standards/files/pdfrefman_v1-3.pdf).
The choice of which Lambda kind maps to each form is this proposal's design.

| Lambda value | Direct file contribution | Inside a PDF array or dictionary |
|---|---|---|
| String | Its UTF-8 bytes, unchanged | An escaped PDF string; `"Hello"` becomes `(Hello)` |
| Binary | Its bytes, unchanged, including NUL | A hexadecimal PDF byte string; `b'\x00FF'` becomes `<00FF>` |
| Null | Skip | Emit `null`; do not remove an array slot |
| Empty string | Skip | Emit the empty PDF string `()` |
| Bool | A PDF bool token | `true` or `false` |
| Integer | A PDF integer token | Decimal integer token |
| Finite float or decimal | A PDF real token | Decimal notation under an explicit precision/range policy |
| Symbol | A PDF name token | `'Catalog'` becomes `/Catalog` |
| Array | One PDF array | Recurse between `[` and `]`, preserving order and nesting |
| Map | One PDF dictionary | Recurse between `<<` and `>>`; keys encode as PDF names |
| List | Spread contributions under **S2.6.3** | No special PDF list type; apply Lambda's slot/array-image rules (**S2.5.6**) before encoding |
| Explicit PDF wrapper | The wrapper's specified representation | Allowed only where its PDF object kind is valid |
| Other element, datetime, path, function, type, or unsupported value | Error unless explicitly converted | Same |
| Error value | Propagate failure | Propagate failure |

An array stays an array, consistent with **S2.5.1v2/S2.6.3**. Typed numeric
arrays and admitted virtual arrays use the same mapping; admitted virtual
maps use the map mapping under **D7.4.5v2**. Range materialization must be
explicit in phase 1 to avoid accidentally expanding an unbounded value.

Example mappings, showing the object encoding without surrounding
file-content separator spaces:

```text
Lambda: [1, null, "", "hello", 'World', [2, 3]]
PDF:    [1 null () (hello) /World [2 3]]

Lambda: {Type: 'Catalog', Pages: pdf.ref(2)}
PDF:    << /Type /Catalog /Pages 2 0 R >>

Lambda: {Title: "Report", MediaBox: [0, 0, 595, 842]}
PDF:    << /Title (Report) /MediaBox [0 0 595 842] >>
```

`pdf.ref` and the helpers below are proposed package APIs, not existing
functions. PDF `/Type` is a dictionary key; it has no relationship to the
file's `format` selector or Lambda's `type()` function.

### 5.2 Encoding details and explicit wrappers

Raw file fragments receive no inserted spaces, escaping, line endings, or
BOM. Automatically encoded structured/scalar contributions own a leading
and trailing ASCII space in file-content context, so adjacent numeric tokens
cannot accidentally join. Inside arrays and dictionaries the object encoder
owns the separators. Raw PDF snippets remain responsible for their own token
boundaries and grammar. Document/stream wrappers own their exact framing;
`pdf.object` starts at its object number without extra wrapper padding, so an
offset recorded for it addresses the object header itself.

Map keys accept strings/symbols and retain case. Iterate in Lambda's defined
storage order (**S2.3.1**) for deterministic output. Reject unsupported keys
and duplicate names after key encoding. Symbols and keys use one name encoder;
for example, `'A/B'` becomes `/A#2FB`. Encode names from UTF-8 bytes, escape
delimiters and non-printable bytes with `#XX`, and reject NUL. Do not treat a string
beginning with `/` as a name. A present null field emits `/Key null`; a missing
field is absent. A PDF reader treats a null-valued dictionary entry as absent,
so the format cannot preserve Lambda's present-null distinction on round-trip.

The selected string policy is ASCII through escaped literal strings (with
three-digit octal escapes for controls),
and other Unicode text through UTF-16BE with a BOM, emitted as a hex string.
The same encoder covers metadata and structured string values. It is not a
font character-code encoder: text-show operators need explicitly encoded
bytes. Binary object values preserve their bytes through hex encoding.
Numeric emission rejects nonfinite values; exact signed/unsigned integer
carriers never pass through a float. Finite float object values use Lambda's
existing shortest round-trip formatter and expand exponent notation to PDF
decimal notation without rounding those digits. Decimal encoding remains
deferred (USER, 2026-10-02). Reader-specific numeric limits remain open;
formatting a wide token does not guarantee every PDF consumer supports its range.

Use explicit wrapper values for PDF concepts that have no plain Lambda
equivalent:

| Proposed helper | Meaning |
|---|---|
| `pdf.value(v)` | Encode `v` in object context even as direct file content; `pdf.value("")` emits `()`, and `pdf.value(null)` emits `null`. |
| `pdf.ref(object_num, gen_num)` | Emit an indirect reference; offer a one-argument form with generation zero. Validate its target in the document plan. |
| `pdf.object(object_num, value)` | Define one numbered indirect object with generation zero in phase 1; produce object framing and an object-encoded body. |
| `pdf.stream(dictionary, payload)` | A stream value allowed as an indirect object's body; compute `/Length` from its actual payload bytes. |

Represent wrappers as explicit, validated package element forms with agreed
tags/fields, not ordinary maps guessed by a `type` key. Thus a map with keys
`type`, `object_num`, or `data` is still a dictionary. The existing input parser
uses marker maps such as `{type: "indirect_ref", ...}` and stringifies PDF
names; those are an input representation to adapt explicitly, not an automatic
output dispatch protocol. Do not assume input parse/output encoding preserves
every representation detail.

Stream payloads accept strings, binaries, spreading lists, null, and empty
text under the raw-byte rules. Reject structured values there unless the
producer has explicitly encoded them: an array as payload must not silently
become raw image bytes or a PDF array. Reject conflicting caller `/Length`;
the package owns it. Streams do not occur directly inside arrays/dictionaries;
use a reference to their indirect object. Phase 1 emits uncompressed streams
without defining a native codec API.

### 5.3 Content sequence and storage ownership

The author's content is a logical sequence of contributions, not a required
in-memory Item array and not a single binary child. **S2.6.2–S2.6.4** still
govern null/empty dropping, list spreading, and same-kind normalization.
The output encoder consumes the sequence and produces bytes; normalization
of adjacent raw fragments does not change those bytes.

Do not infer indexing, iteration, or mutation of a retained file value from
its serialized buffer. In particular, PDF bytes cannot reconstruct the
original map or string/binary boundaries. The remaining representation choice
is whether to retain compact source-span/value metadata for such observation,
or use destination lowering only when the file is consumed for output and
materialize ordinary content when it is observed. That choice must preserve
the sequence-facing contract and **S2.6.5**, and state its memory cost before
the constructor is exposed as a general reusable value. Phase 1 must prove
the direct export path without inventing one-binary child semantics.

The special MarkBuilder needs begin, append-text, append-binary, finish, and
abort operations. It should share common construction and error handling with
existing builders while selecting a byte destination for file content. A
`PDFBuilder` can provide the format-specific append policy through that
common builder interface. This does not require making every current method
of the Input-owned `MarkBuilder` virtual or duplicating its allocation code.

For the authorized map MVP, **D2.6.12v2** places the object encoder inside
`PDFBuilder`, with document construction in Lambda. It reads existing map
fields and array slabs and appends their encoding directly, without a complete
serialized object string or fragment array. Whether the full migration moves
these encoders into the Lambda PDF package remains DO32; native resource
helpers remain phase 2. The original Lambda-encoder recommendation is retained
in Appendix S as a superseded recommendation for this MVP boundary.

Today `MarkBuilder` serves Input-owned data, while evaluated elements are
runtime values. Reuse the append mechanism, but keep the ownership adapters
explicit: borrowing an Input arena cannot make a runtime file survive that
Input's destruction. Buffer growth, final ownership transfer or copying, and
GC tracing must be specified for both entry paths before implementation.

Use existing length-aware library buffers and checked geometric growth where
suitable. Finalization should transfer owned storage when the representation
permits it, or perform one documented final copy. Memory remains proportional
to final output bytes plus resource data and metadata. There must be no
additional per-fragment Item array or repeated copying of the accumulated
prefix on every append.

### 5.4 Virtual content list and builder dispatch

Heap allocation and output destination are separate concerns. Existing maps,
arrays, strings, and binaries remain ordinary Lambda values. When a value
becomes a contribution to the PDF file, evaluation passes its `Item` to the
file's virtual content list. Its PDFBuilder consumes that value without
storing it in an output Item array.

Allocate a specialized List extension for the PDF content destination. Keep
the existing List prefix and add a pointer to its PDFBuilder. A new
`is_virtual` representation flag, readable through `List*`, selects builder
dispatch. Ordinary lists initialize it to false. Never set the flag on a
plain List allocation: the extension and builder must already be valid.

For future formats, the recommended pointer type is a small common builder
interface whose concrete instance here is PDFBuilder. This still stores the
PDFBuilder pointer in the extension, while letting `list_push` dispatch
without recognizing PDF tags or growing a switch for each format. The
interface needs content append, finish/abort, and ownership hooks; it need
not make every existing MarkBuilder operation polymorphic. Its name and
exact C/C++ interface remain implementation choices.

No output state is stored in EvalContext, TLS, or InputAllocationContext.
The receiver passed to `list_push` identifies the destination completely.
This follows **D2.6.5v4**, where append discipline belongs to the destination,
and preserves **D5.4.1**. Ordinary nested arrays and elements use their own
normal receivers. Nested PDF files have distinct receivers and builders;
there is no parent context to restore.

The word *virtual* here describes construction storage. It does not create
another Lambda list kind, change `is_spreadable`, or automatically implement
the readable host-container protocol of **D7.4.5v2**. Publication of a retained
file value still needs the contract in §5.3.

For example, this is a proposed source fragment, not a complete PDF:

```text
let body = {Count: 2, Values: [10, 20]}
<file format:'pdf',
    "1 0 obj\n"
    body
    "\nendobj\n"
>
```

`body` and its nested array are heap values before file construction. The
conceptual lowering is:

```text
attributes = evaluate_file_attributes()
file = allocate_virtual_pdf_file(attributes)
target = file_as_list(file)
try:
    list_push(target, "1 0 obj\n")
    list_push(target, body)
    list_push(target, "\nendobj\n")
    result = finish_file(target)
on failure:
    abort_file(target)
```

These names describe an internal protocol, not existing APIs. Construction
and finalization do not write to the filesystem. Evaluate attributes once,
preserving their order, and select the virtual destination before evaluating
any body content. A computed `format` selects the same representation as a
literal selector. `finish_file` does not settle the retained-value question.

The append dispatch is conceptually:

```text
list_push(list, item):
    if list.is_virtual:
        extended_list(list).builder.append_content(item)
        return
    perform the existing normalized content append to list
```

Check the flag before ordinary append accesses `items`, merges adjacent
values, grows capacity, or records concrete child ranges. The builder applies
§5.1, including null/empty dropping and list spreading; arrays remain single
PDF object values. Copying adjacent raw strings or binaries into the byte
buffer emits the same bytes as first merging them, without allocating the
merged heap value. Reader-visible normalization still depends on §5.3.

`list_push_spread`, `list_fill`, collection helpers that route element writes
to content append, and finish paths must honor the same receiver. Share one
virtual dispatch path rather than duplicating the rules. Concrete child-range
bookkeeping must not run for a destination with no child array. Only the
owning file construction finalizes its builder; completing a forwarded
producer must neither close it nor append its output twice.

`list_end` needs a virtual branch before ordinary zero/one/many collapse or
element logging. It must not mistake an unused `items` array for empty output.
Keep byte length in the builder; never repurpose List.length as a byte count
that ordinary readers could interpret as a number of Items. Empty PDF content
still requires explicit finalization.

| Layer | Responsibility |
|---|---|
| Interpreter/MIR element evaluation | Evaluate attributes once; allocate/select and root the extended destination; pass it to eligible producers; finalize or abort it. |
| `list_push` and builder dispatch | Test `is_virtual` on the receiver and invoke its builder; retain normal array storage for other receivers. |
| File/PDF builder | Apply the contribution policy, append bytes, track byte length/capacity, checkpoint/rollback, and finalize or release output. Under the recommended split, a Lambda encoder handles structured values. |

In the example, the Lambda dictionary encoder reads the existing `body` map
and emits its PDF representation directly into the receiver. Its array
encoder visits `[10, 20]` without building another array or a complete
serialized dictionary string. The input map remains alive while being read.

Under the recommended split, the native bridge passes the destination to an
internal Lambda encoder entry. Encoded fragments reach that receiver without
ambient output lookup or a user-visible mutable builder. Recursive encoding
carries its PDF-object context explicitly, so a dictionary's string value
cannot accidentally enter the raw-file path. Encoding must eventually append
raw fragments rather than recursively redispatch the same structured value.

Today script element evaluation calls runtime `list_*` helpers directly;
adding a MarkBuilder subclass alone does not connect it to either evaluator.
Both evaluators must select the extended allocation and support the finish
protocol. Existing ordinary element construction remains unchanged.

Root the receiver and input Item across allocating encoder calls under
**D5.3.3–D5.3.4**. Copy each borrowed byte span before its owner can expire;
retained Items need precise tracing. The extra pointer does not automatically
establish ownership: specify who owns and releases the builder and buffer
on success, failure, or abandonment. Emitting a value neither frees a live
binding nor changes its copy-on-write behavior.

Generic index/iteration, formatting, equality, copy/COW, and GC paths must
either understand the extended representation or be unable to receive the
private construction object. Merely keeping `items == nullptr` does not
implement a readable virtual list. Representation details are in Appendix B.

### 5.5 Producer functions must receive the destination

The virtual list removes the file's output Item array. It does not remove a
list already constructed by `pdf_output()` before that call returns. The
same applies to a large serialized string constructed inside a producer.

Pass the virtual destination explicitly through internal lowering for
output-demanded evaluation, extending **D8.2.6**. An eligible producer uses
that receiver for result contributions through blocks, branches,
for-expressions, and nested producer calls. A specialized emission entry may
take a hidden `List*` destination; ordinary calls retain their value-returning
entry. The interpreter carries the same receiver through evaluator calls
without installing it in EvalContext. Calls used to initialize variables or
compute arguments still construct ordinary values.

Current function/block results may use `array_push_spread`, and small or
single-result forms can bypass `list_push`. MIR's small-element `list_fill`
path also evaluates all child arguments before appending them. These paths
need destination-aware lowering to avoid holding all contributions at once.
Only eligible result paths share the virtual destination; do not change
ordinary local list/array allocation inside a producer. Completion must be
represented separately so the caller does not append already-emitted output.

The interpreter and MIR Direct must preserve the same semantics. Arrays
returned by calls encode one PDF array and must not be flattened. Generic
calls can encode an already-produced value for correctness; direct emission
must be demonstrated through the migrated PDF producer call graph. The
optimization may avoid materialization but must preserve ordinary evaluation,
including list kind and zero/one/many result behavior (**D8.2.6**).

Preserve evaluation order, effects, and error handling. Private output is
discarded on failure. If a handler recovers within an expression, roll back
partially emitted bytes and builder metadata from that expression before
appending its replacement value. Do not roll back external procedural effects.
Current `list_push` returns void; the bridge needs a checked failure path so
encoding/allocation errors propagate to the right handler and a partial PDF
cannot be published as success. A recorded builder failure alone is not enough
if evaluation continues past a point where an error should have propagated.

Nested files own separate receivers. Propagated errors, early exits,
cancellation, and native recovery must release construction-owned resources;
C++ destructors alone are insufficient across recovery that bypasses them
(**D5.3.6**). No output-frame restoration is needed. Phase 1 stays synchronous;
no builder is implicitly inherited by another task.

### 5.6 What the virtual list resolves and what remains

The agreed architecture resolves builder lookup, destination isolation, and
append dispatch without changing context state. Future builders can reuse
the extension and dispatch interface. These remaining contracts are deferred under **SO48/DO32**, as requested on
2026-10-02; they do not block the raw-fragment/dictionary MVP:

1. **Producer propagation.** Specify the emission entry/demand path that
   passes this receiver through nested calls and control flow; merely
   extending the final list does not make those calls stream.
2. **Completed file value.** Settle indexing, iteration, equality, copying,
   and mutation of retained file content, or constrain destination lowering
   to output consumption as in §5.3. Serialized PDF bytes cannot recover the
   original values. A nested file is not automatically inlined into its parent.
3. **Ownership and failure.** Define extended allocation, flag layout,
   tracing/destruction, checked append failure, finish-once behavior, and
   expression rollback. None follows automatically from the extra pointer.
4. **PDF encoding boundary and accounting.** The map MVP uses native object
   encoding under D2.6.12v2; choose the full package migration boundary and
   remaining mappings separately. Byte-offset/stream-length rules remain
   necessary. The counting destination in §6.1 can use the same
   virtual-list interface; a future single-pass offset API must be explicitly
   tied to a destination and preserve the pure/procedural boundary.

## 6. PDF construction in Lambda

Extend `lmd/package/pdf/` with output modules and export an entry through its
existing package facade, following **D7.2.4**. Keep shared coordinate/color
helpers where their contracts match; input interpretation and output
serialization have different responsibilities.

The package plans stable object IDs and resource references, walks the export
projection, and emits PDF syntax directly into file content. Resource maps,
object metadata, and offset tables are legitimate working data. The design
removes the array of output fragments, not every array used by the algorithm.

### 6.1 Byte positions and stream lengths

Cross-reference offsets must describe actual file byte positions, and stream
lengths must describe emitted payload bytes. They cannot be derived from
Unicode character counts. Treat the file as binary throughout, including its
textual syntax. The relevant format constraints are described in the
[PDF Association syntax specification corrections, stream extent and file structure](https://pdf-issues.pdfa.org/32000-2-2020/clause07.html).

A proposed reference strategy avoids exposing mutable builder position to
ordinary pure functions:

1. Freeze a resource/object plan and all external inputs for the export.
2. Measure each object's emitter through a counting destination that applies
   exactly the same text/binary conversion without retaining output bytes.
3. Compute object offsets from the header byte length and measured sizes.
4. Emit the same planned objects through the byte destination, followed by
   the Lambda-generated cross-reference table, trailer, and end marker.

Stream payloads can likewise be measured, with their lengths emitted through
separate planned length objects to avoid length fields changing the payload
being measured. Expensive image/font processing is prepared once in the
resource plan only if such processing is admitted in phase 2; phase 1 does
not need it. A later compressed-stream design must count encoded bytes, but
its helper and cache policy are deferred with compression itself.

The counting pass is a proposed correctness-first choice, with additional
CPU cost to measure. It is valid only for deterministic, effect-free
producers over the frozen export session. An effectful producer is evaluated
once and materialized or requires a different explicitly designed API; the
runtime must never duplicate arbitrary `pn` execution. A future single-pass
mark/offset facility can replace counting after its scope and effect contract
are settled. Either strategy leaves PDF accounting in Lambda.

### 6.2 Text and rendering parity

The file builder's UTF-8 conversion does not define PDF font encoding. The
PDF package must encode text for the chosen font resource, escape PDF syntax,
and consume the layout engine's positions. Font embedding and Unicode text
mapping are separate features from byte output; the initial migration should
state its supported text coverage explicitly.

Define one coordinate conversion from Radiant geometry to PDF page space.
Apply page dimensions, output scale, and the vertical-axis conversion
consistently to shapes, text, images, and clips. Preserve current visual
behavior where valid, and record confirmed defects separately instead of
copying them as a specification.

Phase 1 covers one content-sized page, vector shapes, transforms, basic clips,
and simple positioned text using standard PDF fonts where existing layout
data suffices. It requires no new native font, image, compression, or raster
helper. Unsupported content must be identified and reported, not silently
dropped or replaced with an unapproved fallback.

Image/alpha processing, SVG and effect raster fallbacks, font embedding and
shaping-dependent text, and compression move to phase 2. Their helper
contracts and implementation language are deliberately undecided. Full
rendering parity is a later migration gate. Pagination, tagged PDF,
encryption, and signatures are separate extensions; the object model can
already represent multiple page objects without promising pagination.

## 7. Migration and acceptance

The CLI retains `lambda render input -o output.pdf`. Its export adapter opens
the session, invokes the Lambda package with the Velmt root and options,
obtains the completed file's bytes, writes them, and closes the session on
both success and failure. It must write raw payload bytes, not format the
file element as Mark or XML. The existing PDF-to-PDF copy shortcut is outside
the rendering migration and must remain an explicit CLI policy.

### 7.1 Phase 1 proves byte construction and PDF mappings

The authorized [virtual-list MVP](impl/Lambda_Impl_PDF_Output_MVP.md) is a
subset of this phase: it proves raw byte construction and explicit output.
The producer-forwarding and structured-encoding requirements below remain
later work, not MVP acceptance gates.

1. Settle the mapping recommendations and observable-file representation;
   record new or changed rulings in the formal and working specs together.
2. Build the extended virtual content list, byte builder, and PDF encoder
   dispatch, with interpreter/MIR parity and precise lifetime coverage.
3. Implement value encoding, explicit object/reference/stream helpers, and
   cross-reference/trailer construction entirely in the Lambda PDF package.
4. Add the export-session Velmt interface and prove a complete basic document
   export through nested producer functions without a fragment Item array.

### 7.2 Phase 2 defines native helpers and closes coverage

Define the deferred font, image, compression, SVG, and effect treatment in a
separate follow-up design. Extend visual coverage against the current
exporter, then switch production export after the agreed structural, visual,
lifetime, and release performance gates pass. Retire the native PDF backend
and writer only after checking their remaining callers. Do not retire the
old path merely because phase 1 can emit a subset of current documents.

### 7.3 Verification

Verification must cover mixed string/binary data, embedded NUL, multibyte
text, empty files, list spreading versus arrays, nested producers, recovered
and propagated errors, allocation failure, and escaped-view invalidation.
Pin raw fragments versus object-context strings/binaries; nested arrays/maps;
null and empty text in array slots and map values; empty arrays/dictionaries;
symbol/name escaping and key case; map iteration order; explicit PDF wrappers;
nonfinite/numeric overflow rejection; duplicate object IDs and unresolved
references; computed format selectors; and non-PDF `<file>` behavior.
Every new Lambda test script requires its expected `.txt` file. Allocation
instrumentation or MIR inspection must demonstrate that large producer calls
do not construct a retained Item array of output fragments.

Verify the virtual flag on ordinary and extended allocations, dynamic format
selection, finish of empty content, and the small batched/single-result paths.
Exercise nested files and ordinary local collections to establish receiver
isolation. Force GC during structured encoding and failure handling; check
builder destruction, buffer ownership transfer, and rollback of recovered
expressions. Retained-file tests must match the observation contract selected
in §5.3, including copy/COW and reads that would otherwise access `items`.

For PDF correctness, validate offsets and stream lengths against emitted
bytes, parse with the existing reader, and use an independent PDF reader or
validator as an additional check. Phase 1 compares basic text, paths, borders,
transforms, and clips; phase 2 adds images, SVG, gradients, and complex effects.
Byte-for-byte identity with the old writer is not required: deterministic
output and correct rendering are the acceptance criteria.

Run Lambda and Radiant baselines for their corresponding implementation
changes. Measure elapsed export time, peak memory, allocations, and bytes
copied using a release build on the same document corpus. Include the cost
of any counting pass and resource preparation; no performance improvement is
claimed by this proposal. Keep temporary artifacts under `./temp/`.

## 8. Formal specification impact

| Existing authority | Required treatment on ratification |
|---|---|
| S2.2.2v2 and S2.2.4 | Preserve the childless empty file and absence of empty binary values. |
| S2.3.1 | Preserve source/insertion order when emitting dictionary entries. |
| S2.5.1v2 and S2.6.1v2–S2.6.5 | Keep lists spreading and arrays structured; apply content normalization only at content boundaries, not inside object slots; settle observation/update behavior. |
| S16.7.1 and S16.9.3 | Activate only the explicit PDF file constructor; preserve script-top-level semantics and the attribute/content boundary comma. |
| S12.1.1v2 | Preserve the boundary between value construction and external I/O, including measurement eligibility. |
| D2.6.5v4 and D2.6.6v3 | Document the extended List destination and representation flag; preserve the shared container prefix and content/sequence/slot distinction. |
| D7.4.4 and D7.4.5v2 | Reuse the declared host interface and structural virtual carriers. |
| D5.3.3–D5.3.4 and D8.2.6 | Document ownership/rooting and content-destination lowering without weakening semantic equivalence. |
| D5.4.1 and D5.3.6 | Leave context state unchanged; preserve precise recovery and ensure construction-owned resources are released on all exits. |

The authorized MVP subset is recorded as **S2.6.6v2/D2.6.12v2**. **SO48/DO32**
keep the remaining decisions open. Direct scalar/array contributions, decimal
encoding, virtual sources, retained-content contracts, producer-forwarding ABI,
and Radiant migration remain deferred. Future rulings must update the formal and working records
together under [Doc Convention](../doc/Doc_Convention.md).

## Appendix A Code entry points

Verified by source inspection on 2026-10-02. The MVP implementation record is
[PDF Output MVP](impl/Lambda_Impl_PDF_Output_MVP.md). Line anchors may drift;
symbol names identify the intended code.

| Area | Current source and symbols |
|---|---|
| Export setup and dispatch | `radiant/render_output.cpp:231`, `render_export_session_begin_internal`; `:755`, PDF dispatch |
| PDF rendering and save | `radiant/render_pdf.cpp:1772`, `render_view_tree_to_pdf`; `:1928`, `render_html_to_pdf` |
| Current byte writer | `lib/pdf_writer.c:1013`, `HPDF_SaveToFile`; `:386`, `HPDF_SetCompressionMode` |
| Shared paint walk | `radiant/render_walk.cpp:267`, `render_walk_block`; `:382`, `render_walk_children` |
| Existing view projection | `lambda/module/radiant/radiant_module.cpp:689`, `radiant_layout_velmt_vtable`; `:879`, `radiant_layout_view_children_item` |
| Input-owned construction | `lambda/io/mark_builder.hpp:43`, `MarkBuilder`; `lambda/io/mark_builder.cpp:499`, `ElementBuilder::child` |
| Runtime element construction | `lambda/runtime/transpile-mir.cpp:25527`, `emit_element_storage`; `lambda/runtime/interp.cpp:3849`, `eval_element` |
| Shared container layout | `lambda/lambda.hpp:436`, `List`; `:910`, `Element`; `lambda/lambda.h:1031`, `Container` |
| Content append and finish | `lambda/runtime/collection_runtime.cpp:433`, `list_push`; `:559`, `list_push_spread`; `lambda/runtime/lambda-data-runtime.cpp:1527`, `list_end`; `:2567`, `list_fill` |
| Existing PDF package facade | `lmd/package/pdf/pdf.ls`, `pdf_to_svg`, `pdf_to_html` |
| Input/parse/format selectors | `lambda/runtime/lambda-eval.cpp`, `fn_input2`, `fn_parse2`, `fn_format2` |
| Output selector and raw byte writing | `lambda/runtime/lambda-proc.cpp:468`, `pn_output3`; `:169`, `pn_output_internal` |

Related records: [Virtual Container Types](Lambda_Design_Type_Virtual.md),
[String Builder Followup](impl/Lambda_Impl_String_Builder_Followup.md),
[PDF Input Package](Lambda_Pkg_PDF.md), and the historical
[Native PDF Writer](pdf/PDF_Writer.md).

## Appendix B Virtual list integration notes

The MVP implements extended allocation, flag dispatch, byte ownership, and
finish. Readable publication and the other SO48/DO32 contracts remain open.

- **Element allocation.** There is no separately allocated content-list
  object in an ordinary Element: `Element : List` carries the content fields
  directly, and both evaluators append through `(List*)element`
  (**D2.6.6v3**). The PDF file therefore needs an Element extension, inheriting
  the List prefix and adding the builder pointer. Use the actual extended
  subtype in the downcast; sharing a prefix does not make unrelated C++
  subclasses interchangeable. A plain List must never be read beyond its
  allocation to discover the pointer.
- **Dynamic selection.** Existing element allocation can precede attribute
  filling. Adapt the constructor so the evaluated format selects an extended
  allocation before body appends, or reserve extension space for candidate
  files and enable it after selection. Preserve attribute order and precise
  roots across this step; changing a flag cannot enlarge an existing object.
- **Flag ABI.** The ordinary `flags` and `array_flags` bytes already have
  assigned or reserved bits. Allocate the `is_virtual` state deliberately,
  preferably without shifting the shared prefix; audit `reserved_state`
  against its boxed-list and native-lane uses before choosing a bit. Preserve
  C/C++/GC/MIR layout assertions, `is_spreadable`, and existing nominal/JS
  state. Default initialization and cloning must establish a valid flag and
  allocation pair.
- **Storage and lifetime.** `items` does not hold the builder pointer or the
  output bytes. Builder byte counts are separate from list fields. Current
  GC and copy routines follow the ordinary container layouts; teach the
  relevant ownership paths about the extension, or keep construction private
  and publish a separately supported representation. Raw native pointers are
  not traced automatically. Never copy an active builder pointer into an
  independently mutable file value.
- **Finalize and fail.** Add virtual handling before ordinary `list_end`
  collapse/logging and ensure empty content takes that path. The current
  evaluators return the element pointer after calling `list_end`, so a finish
  operation that returns a different artifact also requires changing result
  publication. Define checked error propagation around the void append ABI;
  finalize exactly once, release on abort, and prevent writes after finish.

## Appendix S Superseded proposal

Superseded on 2026-10-02 by the user's virtual-list direction in §5.4:

~~Entering the file body pushes an output frame with its own builder. Body
output appends use that frame; leaving the file restores the parent frame.~~

~~Introduce an output-state capsule owned by the existing EvalContext, with
a stack of frames identifying the builder and rooted output target. Producer
output forwards into the scoped output destination, and native recovery
restores that frame alongside roots.~~

The receiver now carries the builder pointer. This removes ambient builder
lookup and output-frame installation/restoration. Producer destination
propagation, resource cleanup, and expression rollback remain necessary;
they now operate on explicit construction-owned destinations.

Superseded for the map MVP on 2026-10-02 by **D2.6.12v2**; the full package
migration boundary remains DO32:

~~The recommendation is that PDFBuilder handles byte storage and invokes a
Lambda PDF encoder for structured values. A native object encoder inside the
specialized builder is an alternative requiring an explicit boundary choice.~~
