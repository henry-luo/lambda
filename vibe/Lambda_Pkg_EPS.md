# Lambda EPS Package Proposal

> **Status:** authorized for implementation on 2026-10-14. The initial vector
> profile, local VM saves and CLI integration are implemented; text, images and
> resource expansion remain proposed. See the
> [implementation record](impl/Lambda_Impl_EPS.md) for the tested boundary.
>
> **Date:** 2026-10-13
>
> **Scope:** a C+ EPS input parser under `lambda/input/`, followed by a
> PostScript interpreter and SVG converter written in Lambda Script.
>
> **Native parser:** `lambda/input/input-eps.cpp` (proposed).
>
> **Script package:** `lmd/package/doc/eps/` (proposed), imported as
> `lambda.doc.eps.eps`.
>
> **Precedent:** the existing parser in `lambda/input/input-pdf.cpp` and
> renderer in `lmd/package/pdf/`; their implementation was inspected for this
> proposal. Historical paths in `Lambda_Pkg_PDF.md` are not the current tree.

## 1. Goal and governing constraints

Support Encapsulated PostScript as a document input format and convert its
artwork to a standalone SVG element tree. Follow the PDF architecture:

```text
EPS file bytes
    → C+ input parser: envelope, DSC metadata, bounded PostScript scanning
    → Lambda Script: PostScript execution, resources, graphics state
    → ordered drawing records
    → Lambda Script: SVG elements
    → existing format / Radiant pipelines
```

The native layer handles bytes, syntax, compression and existing font/image
codecs. The script layer owns the PostScript machine and drawing semantics.
Conversion does not require Ghostscript, an EPS-to-PDF intermediate, a new
Radiant layout engine, or a native PostScript evaluator.

This package does not change Lambda's language semantics. Undelivered profile
expansions remain proposals. The following formal rulings constrain
implementation:

| Formal ruling | Application here |
|---|---|
| **D7.2.1**, **D7.2.3** | Ship ordinary Lambda source modules with explicit exports and existing import caching. |
| **D7.2.4** | Place the new document package under `lambda.doc.eps.*`, resolving to `lmd/package/doc/eps/`. Reuse existing `lambda.pdf.*` modules at their current paths. |
| **D4.1.1v2**, **D4.1.2**, **D4.1.3** | Parsed source and objects belong to the Input arena/pool; runtime state has a separate lifetime. |
| **D1.5v2**, **D5.1.1v3** | Any native runtime bridge retaining GC Items uses precise roots; the native stack is not a GC root source. |
| **S1.4**, **S12.2.1** | Represent PostScript object sharing explicitly inside the guest machine; do not assume Lambda values alias. |
| **S12.1.1v2**, **S12.1.2** | A `var`/`while` execution driver is a `pn`; pure helpers are `fn`. An `fn` cannot call that driver. |
| **S7.4.2**, **S7.4.4**, **S7.5.1** | Public conversion failures use an explicit `^` channel with structured diagnostics and immediate propagation/handling. |
| **S2.2.2v2** | A zero-length PostScript byte string needs an explicit empty representation; it cannot be an empty Lambda `binary`. |
| **S1.8** | EPS enters as a source document and becomes guest object data; never generate or evaluate Lambda source from EPS text. |

Formal specifications: [semantics](../doc/Lambda_Formal_Semantics.md) and
[design](../doc/Lambda_Formal_Design.md). No existing ruling is revised here.

## 2. Format scope

EPS combines an interchange envelope with an executable PostScript program.
Support should be advertised as a tested **EPS rendering profile**, with an
operator and resource inventory, rather than full PostScript conformance.

The EPS specification establishes a single-page interchange format, required
header/bounding-box comments, optional previews, and an importing environment
that suppresses `showpage`. The proposal accepts plain EPS and the DOS binary
wrapper; preview images remain optional metadata. See Adobe's
[EPSF 3.0 specification, §§2–3 and §§5–6](https://printtechnologies.org/standards/files/epsf_spec_v3_0.pdf).

| Delivery | Proposed coverage |
|---|---|
| Vector MVP | EPS header/DSC metadata; names, numbers, byte strings and procedures; dictionaries, arrays, stack operations, arithmetic and control flow; graphics save/restore, matrices, paths, clipping, line styles and DeviceGray/RGB/CMYK. |
| Practical EPS | Base fonts and encoding/metrics; text operators; local VM save/restore; common in-memory image sources and supported filters. |
| Broader compatibility | Embedded Type 1 fonts, resource dictionaries, additional text/image forms, patterns and shadings where existing PDF helpers provide a sound basis. |
| Deferred | Full LanguageLevel 2/3 conformance; global VM; arbitrary executable strings; PostScript binary object encodings; Type 3/CID font execution; printer control, halftone/overprint fidelity, separations and vendor extensions. |

Do not identify a producer and substitute a handwritten rendering path for it.
Run its prolog through the supported machine. Unsupported executed features
must fail with their operator and source position; an unsupported feature in an
unexecuted branch does not itself prevent conversion.

## 3. Native parser and scanner boundary

### 3.1 EPS document input

Add a length-aware entry point alongside `parse_pdf`:

```cpp
void parse_eps(Input* input, const char* bytes, size_t length);
```

`input(path, 'eps')` should return a Mark-built document containing:

| Field | Meaning |
|---|---|
| `format`, `schema_version` | `"eps"` and the version of this parsed-data contract. |
| `eps_version`, `language_level` | Declared versions, without claiming complete interpreter support. |
| `source` | Input-owned original bytes; never a borrowed file buffer or a UTF-8 conversion. |
| `program` | Validated offset/length of the PostScript section within `source`. |
| `bbox`, `hires_bbox` | Four-coordinate boxes, or a pending trailer declaration. |
| `metadata` | Title, creator, creation date and declared needed/supplied resources. |
| `sections`, `previews` | Source spans for known DSC sections, nested documents and previews. |
| `diagnostics` | Nonfatal metadata diagnostics with stable codes and source spans. |

This is a proposed schema, not an existing API. Keep byte offsets relative to
the original file, including any wrapper; preserve source spans on scanned
objects. Avoid eagerly duplicating the file into strings and token arrays.

The envelope parser should:

- Validate the EPS header within the selected PostScript section. Ordinary
  `%!PS` printer jobs are not automatically EPS.
- Recognize the DOS `C5 D0 D3 C6` signature and its 30-byte header, read the
  little-endian fields, validate its specified checksum convention, and check
  each section with overflow-safe offset/length arithmetic. Never execute
  TIFF/WMF preview bytes. These fields are defined in
  [EPSF §5.2](https://printtechnologies.org/standards/files/epsf_spec_v3_0.pdf).
- Track header, body, trailer and nested-document scope; a child's metadata
  must not overwrite the enclosing artwork's bounding box.
- Resolve `(atend)` metadata from the matching outer trailer. Prefer a valid
  `%%HiResBoundingBox` for output geometry while retaining the integer box.
  Conversion rejects a box still unresolved at finalization, or nonfinite or
  nonpositive output extents. Never invent a Letter/A4 page when the box is
  missing.
- Preserve prolog/setup/resource code for execution. DSC section boundaries
  organize source; they do not make those sections optional.
- Treat previews as optional spans. Preview extraction does not replace
  interpretation of the artwork.

DSC `%%BeginData`/`%%BeginBinary` counts govern the metadata walk, including
byte-versus-line counting. The counted region can contain an executable reader
before its payload: skipping it for metadata does **not** mean deleting it from
the execution source. See Adobe's
[DSC 3.0 specification, §5.2](https://web.mit.edu/PostScript/Adobe/Documents/5001.DSC_Spec.pdf).

### 3.2 Demand-driven PostScript scanning

Use one native scanner for the input parser's lexical needs and the script
interpreter's source reads. Its core belongs under `lambda/input/`, with a
small IO-facing declaration and a runtime adapter following the existing
`pdf_parse_content_stream_io` / `pdf_runtime.cpp` separation.

Conceptually, a scan request takes `(document, offset, limit)` and returns one
object plus its next offset, source span, or a diagnostic. All scanner state is
explicit; there is no global cursor. Procedure bodies are structured object
data, not Lambda source. Native scanning performs no dictionary lookup or
operator execution.

Recognize decimal/exponent and radix numeric forms, literal/executable names,
`//name` events, nested parenthesized strings, octal escapes, line continuations,
hex and ASCII85 strings, comments, and procedure delimiters. Enforce the
PostScript whitespace/delimiter set with bounded reads. Preserve byte strings
including embedded NUL and non-UTF-8 bytes; use `{length: 0, data: null}` for an
empty byte payload and a binary-backed payload for nonempty data. Preserve
name bytes with a canonical byte-key representation; diagnostic text is a
separate escaped rendering.

Do not turn every name into a PDF-style `{op, operands}` record. Keep `[`/`]`
and `<<`/`>>` as executable construction operations, so expressions inside them
can run. `true`, `false` and `null` follow name lookup. Resolve `//name` events
in Lambda against the dictionary environment at the time that source object
is scanned, including events nested inside a procedure being collected.

The scanner must stop at the requested object. Keep the guest numeric type
explicit: reusing Lambda's exact parsing machinery does not adopt Lambda's
integer range, overflow promotion or decimal arithmetic as PostScript rules.
The profile must specify and test its guest integer/real representation.

`currentfile`, `readstring`
and filtered image sources may consume subsequent bytes before lexical
scanning resumes. Eagerly tokenizing the whole body would misinterpret raw
image/font data and lose the correct source cursor. A DSC metadata index is
advisory: where unmarked data prevents safe indexing, leave later metadata
pending and resolve it as execution advances. Accumulate drawing records in
EPS space so trailer geometry can be applied at finalization. Device-bound
queries that require unavailable geometry remain outside that profile.

## 4. Reuse existing parsing and PDF code

Extraction should follow concrete shared behavior. Promote useful `static`
functions into a module header and implementation; do not copy them into an
EPS parser. Keep PDF entry points and parsed-data contracts stable.

### 4.1 Native reuse map

| Existing code | Reuse and required adaptation |
|---|---|
| `input-context.hpp`, `source_tracker.hpp`, `parse_error.hpp` | Reuse `InputContext`, explicit source lengths, `SourceTracker`, `addErrorCode` and diagnostic formatting. Byte offsets are authoritative for binary spans; text context must be escaped safely. |
| `lambda/io/mark_builder.hpp` | Reuse `MarkBuilder`, map/array builders and `createBinary`; copy or retain bytes in the owning Input before file buffers and parser contexts expire. |
| `input-utils.hpp` | Reuse `parse_scanned_decimal_number` and exact integer helpers after validating the guest token grammar; add radix handling through existing numeric primitives where compatible. Do not use `parse_typed_value`, whose boolean/null coercions are wrong for PostScript names. |
| `lib/str.h`, `lib/byte_builder.h`, `lib/strbuf.h` | Reuse bounded scans, character classification and byte/text builders. A generic HTML whitespace predicate is not the PostScript lexical contract. |
| `input-pdf.cpp`: `pdf_cs_read_lit_string`, `pdf_cs_octal_char` | Extract the common bounded string/escape mechanism. Add explicit termination/error status, byte-string output and conformance cases before making both callers use it. |
| `input-pdf.cpp`: `pdf_cs_read_hex_string` | Share hex scanning/decoding machinery, with explicit invalid-digit, odd-nibble and terminator behavior. Preserve the PDF adapter's current `kind: "hex"` representation. |
| `input-pdf.cpp`: `pdf_cs_skip_ws`, `pdf_cs_skip_comment` | Share bounded traversal where the lexical policies agree. Delimiter/whitespace differences remain explicit parameters or separate thin predicates. |
| `input-pdf.cpp`: `pdf_cs_read_number`, `pdf_cs_read_name`, `pdf_cs_read_array`, `pdf_cs_skip_dict` | Audit rather than call blindly. The current content scanner has a fixed numeric scratch buffer, a PDF operand schema and a dictionary-skipping path; it is not a general PostScript reader. |
| `input-pdf-postprocess.cpp`: `osp_parse_*` | Inspect the object-stream number/string/dictionary parsing before extracting another variant. PDF indirect references and PDF name escaping remain PDF-specific. |
| `pdf_decompress.h/.cpp` | Reuse ASCIIHex, ASCII85, Flate, LZW, RunLength and predictor implementations. Add consumed-input reporting, incremental state and output budgets where the PostScript file/filter contract needs them. |
| `input-pdf-type1.hpp/.cpp`: `pdf_type1_glyph_paths` | Reuse bounded Type 1 decryption/charstring machinery through a generalized font-program API after auditing transport assumptions, encoding, FontMatrix and errors. The current PDF-shaped `String*`/clear-length/byte-code API is not a complete EPS font loader. |
| `lib/image.*`, `lib/base64.*` | Reuse image codecs and data-URI encoding once an image source has been interpreted. No second JPEG/PNG implementation. |

The current `pdf_parse_content_stream_io` groups operands until an operator
and clears that stack afterward. It remains a PDF adapter; it must not become
the EPS execution engine. Likewise, PDF xref/page-tree resolution has no role
in ordinary EPS parsing.

The decompressor functions currently return whole decoded buffers and lengths.
An EPS filter adapter must also know exactly how much source was consumed and
enforce a decoded-byte cap **during** allocation. Checking size after an
unbounded decode is insufficient. Implement this in Lambda-owned code, retain
existing PDF wrappers, and never patch zlib or other vendor sources in place.

### 4.2 Script reuse map

| Existing module | Proposed use |
|---|---|
| `lambda.pdf.util` | Reuse `fmt_num`, `fmt_matrix` and `matrix_mul`; verify multiplication order with noncommuting transforms. |
| `lambda.pdf.color` | Reuse `gray`, `rgb`, `cmyk` and compatible conversion helpers; separate approximations from color-managed output. |
| `lambda.pdf.svg` | Reuse `svg_root`, `group`, `path` and `text_run` where their contracts fit. EPS uses the portable root, not `svg_pdf_root` and its PDF image-resolver registry. |
| `lambda.pdf.path` | Reuse segment serialization and line-style emission. `current_path_d` and the extracted `paint` primitive are public; EPS retains separate path and graphics-state transitions. |
| `lambda.pdf.font` | Reuse base-font family/style mapping and compatible encoding data. EPS resource lookup and font dictionaries are new script logic. |
| `lambda.pdf.text` | Extract useful metrics/glyph-placement helpers. Its PDF text-state dispatcher and default-width fallback are not an implementation of PostScript `show`/`stringwidth`. |
| `lambda.pdf.image` | Reuse portable image emission after separating PDF object/resource lookup and native-only image references. |
| `lambda.pdf.interp`, `.resolve`, `.coords` | Reference and selective primitive reuse only. EPS has its own execution machine, resource environment and bounding-box policy. |

Initially import compatible public helpers directly. Extract a neutral shared
module only when both packages actually need an incompatible or private
primitive, and migrate both callers together. Avoid a wholesale PDF rewrite
as a prerequisite for the first vector EPS.

## 5. PostScript execution in Lambda Script

### 5.1 Machine representation

Use explicit machine data:

```text
Machine {
    operand_stack,
    dictionary_stack,
    execution_stack,
    object_store, next_object_id,
    graphics_state, graphics_stack,
    vm_saves,
    source_files,
    resources,
    draws, definitions,
    diagnostics, budget
}
```

The execution stack holds procedure/program counters, loop continuations,
data-source callbacks and error-control boundaries. Dispatch through a table
of builtin operator identifiers with operand validation and capability status.
The same stack helpers serve every operator family; never duplicate pop/type
checks in each handler.

Advance this machine in a `pn` loop. Procedure calls and guest loops push
execution frames instead of recursing through the native/Lambda call stack.
Keep state transitions, matrix/color calculations and SVG construction in
pure `fn` helpers. This deliberately makes the initial conversion entry
points procedural under **S12.1.1v2/S12.1.2**; do not copy historical PDF
comments suggesting that `var` inside `fn` is supported.

### 5.2 Object identity and mutation

PostScript composites require guest identity. Assign arrays, dictionaries and
strings object IDs; stack values hold typed descriptors with ID, executable
attribute, access permissions and optional interval. Store contents in a
machine-owned table. `dup` copies the descriptor; `put` updates the table entry
seen by all descriptors. Interval views share that entry.

This implements guest sharing without contradicting Lambda's value/COW
semantics (**S1.4**, **S12.2.1**). Equality of guest composites uses their
identity rules, not Lambda structural map equality. Names use canonical byte
keys; dictionary key coercion and comparison belong to the guest layer.

Procedure literals are executable-array objects collected by the scanner.
Distinguish encountering a procedure object as data from executing one reached
through a name or `exec`; do not automatically run every executable array.
`bind` resolves eligible builtin operator references at binding time, without
freezing all dynamic names. Access attributes are enforced, not treated as
cosmetic flags. These distinctions follow Adobe's
[PostScript Language Reference, §§3.3, 3.5 and 3.12](https://pdfa.org/norm-refs/PLRM.pdf).

### 5.3 Operator profile

Deliver operator families together with their validation and error cases:

| Family | Initial inventory / follow-on |
|---|---|
| Stack | `pop dup exch index roll copy clear count mark cleartomark counttomark`. |
| Objects/dictionaries | `array string dict begin end def load store get put length known where currentdict systemdict userdict`; array/dictionary construction; `aload astore getinterval putinterval`; `cvx cvlit type`, access checks. |
| Numbers/logic | Arithmetic, division/remainder, comparisons, boolean/integer logical forms, rounding/conversions, `sqrt sin cos atan`; preserve guest type/range behavior. |
| Execution | `exec if ifelse for repeat loop exit forall bind`; add `stopped`/`stop` only with explicit unwind frames and tests. |
| Graphics | Save/restore, transforms, path construction, paint, clips, colors and line styles described below. |
| Resources/text | Base fonts first; `findfont scalefont makefont setfont currentfont show stringwidth`, encoding and metrics; then `ashow widthshow awidthshow`, callback-driven `kshow`, resource operators and embedded fonts. |
| VM | Local `save restore` as a separate milestone; `readonly noaccess executeonly` where applicable. Global VM operators remain unsupported. |
| Data sources | In-memory `currentfile read readstring readhexstring filter`, image-source procedures and supported image forms after cursor/filter tests pass. |

Maintain an exact, versioned inventory during implementation. The table is a
delivery outline, not a claim that every form of every listed operator is MVP.
Do not report `languagelevel 3` merely because an EPS header requests it.

`save`/`restore` needs a local-VM journal or equivalent object-table snapshots,
allocation generations, and invalid-reference checks. Restore composite
contents according to guest rules, including the exception for string bytes;
restore graphics state and handle affected open streams separately. Operand,
dictionary and execution stacks are not rewound. A generic deep-copy snapshot
is insufficient. Emitted drawing records survive VM restoration.
Do not implement this as an alias for `gsave`/`grestore`. See
[PLRM §3.7.3](https://pdfa.org/norm-refs/PLRM.pdf).

## 6. Drawing model and SVG conversion

### 6.1 Ordered, portable output

Emit one ordered sequence of path, text and image drawing records with their
paint state and clip references. Keep reusable clip/font/image definitions in
a separate registry. Preserve execution order across primitive types: grouping
all paths before all text can change overpainting.

Use the shared PDF primitives at this drawing boundary. Do not synthesize a
PDF document and feed PostScript names into `pdf.interp`. That would couple
EPS execution to PDF resources, text state, and clipping conventions.

### 6.2 Geometry and transforms

For bounding box `[llx, lly, urx, ury]`, let `W = urx - llx`, `H = ury - lly`.
Emit `viewBox="0 0 W H"`; the artwork-to-SVG transform is:

```text
x_svg = x_eps - llx
y_svg = ury - y_eps
matrix(1 0 0 -1 -llx ury)
```

Keep this viewport transform separate from the guest CTM. Apply the root
bounding-box clip, preserve transparency by default, and allow an explicit
background. Express natural dimensions in points, or apply an explicit
points-to-CSS-pixels conversion; do not label one PostScript point as one CSS
pixel accidentally. A scale option changes display dimensions consistently.

Store path points in a stable EPS device space at construction time. For
painting, inverse-map the path through the paint-time CTM and emit that CTM
on the SVG node; this preserves stroke width/dashes under nonuniform scale
and shear without moving previously constructed points. Handle singular
matrices explicitly. The matrix convention needs algebraic and visual tests.
Give zero-width PostScript hairlines an explicit output-resolution policy;
SVG `stroke-width="0"` would make them disappear.

Graphics snapshots include the current path and clipping state. `currentpoint`
is computed through the current CTM; relative paths and `closepath` preserve
the proper subpath origin. Implement arcs as cubic segments through shared
geometry helpers where available. Verify `gsave … fill grestore stroke`, CTM
changes within a path, and `clip`/`eoclip` path retention against
[PLRM §§4.2–4.4](https://pdfa.org/norm-refs/PLRM.pdf).

Clipping is an intersection accumulated in the machine. Use nested SVG groups
or equivalent clip definitions with explicit coordinate units. Restoring a
graphics state restores the active clip. Resource IDs use a caller-supplied
prefix plus deterministic local counters so multiple EPS fragments can coexist.

### 6.3 Fonts and text

Font dictionaries and encoding vectors belong to the script resource model.
Reuse PDF family/style mappings, but obtain verified glyph advances from
existing font/AFM facilities or bundled metrics. The current PDF text helper's
`500.0` missing-width fallback is not adequate for `stringwidth`, which affects
subsequent program geometry. Missing metrics/resources should be explicit.

Share one glyph-layout result between text drawing, `stringwidth` and current
point updates. Keep original bytes and glyph codes until font encoding has
been applied; byte count and Unicode character count are different concerns.
Emit positioned SVG text where portable metrics/encoding are available, or
glyph paths where font programs provide outlines. Keep text upright under the
root y-flip and preserve arbitrary font matrices and per-glyph adjustments.

Embedded Type 1 support should reuse the existing native bounded decoder
after validating its EPS/PFA/PFB and `eexec` inputs. Native code decodes font
program primitives; it does not execute the EPS prolog. Defer unsupported font
formats explicitly rather than silently choosing a visually unrelated font.

### 6.4 Images and resources

Interpret image dictionaries, source procedures, ImageMatrix, Decode arrays,
color spaces and image masks in Lambda. Native helpers handle byte reads,
supported filter codecs, pixel packing and PNG/JPEG encoding. Filter execution
must update the shared source cursor using actual consumed byte counts.

The final SVG uses embedded data URIs or reusable embedded definitions. It must
render in a browser without Lambda's PDF image resolver. Test row padding,
sample depth, orientation, polarity and decode-array inversion independently.

`showpage` resolves to an importing-environment no-op; finalize the one artwork
at source completion. It does not create another SVG or erase accumulated
marks. Reject device-changing operations outside the profile. An explicit
trusted resource map may supply fonts/procsets; the EPS program cannot search
host files or execute external programs to satisfy resource requests.

## 7. Public API and integration

Proposed entry points in `lambda.doc.eps.eps`:

| API | Contract |
|---|---|
| `pub fn eps_metadata(doc)` | Return declared/resolved metadata without executing artwork. Pending trailer values remain visible. |
| `pub pn interpret(doc, opts) map^` | Execute within budgets; return `{draws, definitions, bbox, metadata, diagnostics, stats}`. |
| `pub fn display_to_svg(display, opts) element^` | Build portable SVG from a completed display result. |
| `pub pn eps_to_svg(doc, opts) element^` | Convenience composition of interpretation and SVG emission. |

Example proposed usage, once implemented:

```lambda
import eps: lambda.doc.eps.eps

pn main() {
    let doc = input("artwork.eps", 'eps')^
    let picture = eps.eps_to_svg(doc, {id_prefix: "artwork"})^
    output(format(picture, 'xml')^, "artwork.svg")^
}
```

Options include `id_prefix`, output scale/background, an explicit resource map,
and execution/allocation budgets. Defaults should be documented from corpus
measurements rather than scattered magic constants. `interpret` exposes
diagnostics for callers needing the full report. Conversion fails on executed
unsupported semantics; it never returns a successful partial picture after
dropping an unknown operator.

Native input integration uses `input_from_source_n_with_name_parent` and the
existing explicit-length path, adding `parse_eps` to `input-parsers.h`. Extend
the MIME/extension detector carefully: `.eps`/`.epsi` and EPS signatures select
EPS, while generic `application/postscript` requires EPS validation and must
not relabel all `.ps` printer jobs. Preserve binary lengths on local, HTTP and
`parse` entry paths.

First expose conversion through the explicit script API. Subsequent
`convert -t svg`, `view`, `render` and `layout` integration should call the
package through the existing module invocation machinery and an appropriate
procedural boundary. A pure document-transform `fn` cannot call this `pn`
(**S12.1.1v2**). Do not claim automatic CLI support before that caller audit.
Radiant consumes the generated SVG through its existing renderer.

## 8. Errors, ownership and bounded execution

Use parser diagnostics for malformed envelopes and lexical objects. Use guest
error names such as `undefined`, `typecheck`, `rangecheck`, `stackunderflow`,
`invalidaccess`, `invalidrestore` and `limitcheck` inside a structured Lambda
error. Include original file offset/span, operator, message, guest call/loop
context and a bounded operand summary. Public errors follow **S7.4.2/S7.4.4**;
do not invent numeric codes outside the repository's error convention.

Guest `stopped`, when supported, handles guest errors within its defined
boundary. Host allocation failure and conversion-budget exhaustion must still
terminate conversion; a guest handler cannot reset or swallow the outer budget.

Budget source bytes, token/string length, procedure nesting, instructions,
operand/dictionary/execution/graphics/save depth, guest allocations, decoded
bytes, image dimensions, path segments and emitted SVG size. Charge costs for
scans, composite copies, binding and codec work as well as opcode dispatch.
Every loop backedge and source-procedure callback checks the common budget.

Parsed source and structural objects retain Input ownership
(**D4.1.1v2/D4.1.3**). The runtime machine stores values and explicit guest IDs,
never raw native pointers into expired parser scratch buffers. Native bridge
results follow existing Item ownership/return conventions. Precisely root GC
Items retained across allocating runtime calls (**D1.5v2**, **D5.1.1v3**); do
not add roots for Input-arena storage or revive conservative stack scanning.

Keep all implementation in Lambda-owned sources, use C++17 and the `lib/`
containers/builders, and give nontrivial lifetime/parser changes concise
root-cause comments. Diagnostics use `log_*` with distinct EPS prefixes.

## 9. Verification and acceptance

Test lexical parsing, guest execution and SVG emission independently, then
exercise their composition. Shared-parser extraction must preserve PDF behavior
and pass the PDF parser/package suites alongside the new EPS tests.

| Area | Required evidence |
|---|---|
| Envelope/DSC | Plain/DOS EPS; truncated headers; checksum and section bounds; CR/LF/CRLF; previews; `(atend)`; negative/fractional boxes; nested documents; fake trailer comments inside strings/data. |
| Scanner | Delimiters/comments, exponent/radix numbers, long tokens, octal escapes, escaped line ends, nested/unterminated strings/procedures, odd hex, invalid ASCII85, empty/high-byte/NUL strings, malformed nesting. |
| Machine | Dictionary shadowing, bound operators versus dynamic names, `//name` scan timing, evaluated array/dictionary construction, procedure dispatch, object aliasing/intervals, access attributes, loop exit and bounded recursion. |
| VM/data | Local save/restore and invalid references, string mutation across restore, source cursor resumption, exact filter consumption, data-source callbacks and decoded-byte limits. |
| Graphics | Ordered overpainting, nonzero-origin box, root crop, noncommuting/mid-path CTMs, nonuniform strokes/dashes, singular cases, saved paths, winding rules, intersecting clips, arc geometry. |
| Text/images | Known advances/currentpoint, reencoding, font matrices, outlined text, missing resources, image orientation/masks/sample packing, standalone embedded assets. |
| Termination | Infinite `loop`, recursive procedures, excessive stacks/allocations, decompression expansion and output growth produce stable failures without hangs or partial success. |
| Portability | Identical structural results across interp/jit/auto; browser and Radiant display of the same standalone SVG; deterministic ID prefixes. |

Use small independently authored EPS fixtures first. Add licensed real files
from producers such as gnuplot, Matplotlib, dvips and Illustrator as a coverage
corpus, recording producer/version and required capabilities. This corpus does
not authorize producer-specific workarounds.

For visual references, use a pinned external PostScript renderer as an offline
test oracle, then compare its raster output with browser/Radiant rasterization
of the generated SVG at matching crop, resolution and fonts. Record tolerances
for antialiasing and separate font substitution from geometry defects. The
reference renderer is a test dependency, not a runtime conversion path.

Acceptance for each released profile requires all supported inventory cases,
negative cases and applicable Lambda baseline checks to pass. Report corpus
coverage and remaining unsupported features explicitly. Performance claims
require a release build and representative parsing/execution/output workloads;
do not estimate speed from a debug build or promise parity with PDF parsing.

## Appendix A. Implementation outline

This appendix identifies the concrete work and sequencing; a full execution
plan/progress ledger belongs in `vibe/impl/` when implementation is authorized.

### A.1 Proposed source layout

```text
lambda/input/input-eps.cpp             EPS envelope and DSC document builder
lambda/input/input-postscript.hpp     shared bounded scanner contract
lambda/input/input-postscript.cpp     extracted/adapted PDF/PS lexical helpers
lambda/io/postscript_scan.h           IO scan/byte-data entry declarations
lambda/runtime/postscript_runtime.cpp thin Item/ownership bridge; no PS evaluator

lmd/package/doc/eps/eps.ls             public package API
lmd/package/doc/eps/objects.ls         guest IDs, composites, permissions and keys
lmd/package/doc/eps/interp.ls          explicit execution stack and driver
lmd/package/doc/eps/operators.ls       shared operator dispatch/validation
lmd/package/doc/eps/graphics.ls        PS graphics state and ordered draws
lmd/package/doc/eps/resources.ls       fonts/procsets/resource environment
lmd/package/doc/eps/text.ls            font/encoding/advance semantics
lmd/package/doc/eps/image.ls           data sources and image semantics
lmd/package/doc/eps/svg.ls             display records → portable SVG
```

Split operator families further only as the implementation needs it. Import
existing public PDF helpers; introduce shared modules through extraction rather
than adding parallel copies. Register any new native system entry through the
existing function metadata and MIR/runtime registration path; no new back end.

### A.2 Delivery gates

| Gate | Reviewable result |
|---|---|
| Shared-reader foundation | Bounded scanner/decoder contracts, extracted common helpers, regression coverage for both PDF and PS dialects. |
| Native EPS input | `input(..., 'eps')`, validated source spans/metadata and parser negatives; no claim of rendering yet. |
| Vector interpreter | Explicit machine/object identity, real prologs, vector operators and SVG; procedure/loop/cursor limits verified. |
| Practical EPS profile | Local VM semantics, base-font metrics/text and supported image/filter sources; standalone visual corpus report. |
| Resource expansion | Embedded Type 1 and additional supported resources, each with positive/negative fixtures. |
| CLI integration | Native caller/effect audit, automatic EPS routing, baseline/visual checks and published support inventory. |

Update `build_lambda_config.json` for source/test registration and run `make`;
never edit generated Lua build files. The scanner needs no Tree-sitter grammar
or generated parser edits.

### A.3 Test placement and development checks

- Add a native `test/test_input_eps_gtest.cpp` following the PDF parser suite,
  with fixtures under a dedicated tracked EPS fixture directory.
- Put script cases in `test/lambda/eps/` and register it in
  `PROCEDURAL_TEST_DIRECTORIES`, since the public execution driver is `pn`.
  Every new `.ls` case needs its sibling `.txt` golden; helper naming follows
  [test/README.md](../test/README.md).
- Add SVG/reference cases to the appropriate existing visual harness rather
  than changing expected results to conceal interpreter defects. Capture
  intermediate files and comparison artifacts only under `./temp/`.
- Run the new suites, affected PDF suites and `make test-lambda-baseline`;
  include `make test-radiant-baseline` if native Radiant integration changes.
  Read [Developer Guide §7](../doc/dev/Developer_Guide.md#7-worktrees-and-agent-gotchas)
  before worktree builds; performance verification uses `make release` in the
  main checkout, never in a worktree.

## Appendix B. Questions to settle from implementation evidence

- Which real EPS corpus defines the first advertised profile, and which font
  resources/metrics can ship with appropriate licenses?
- Which existing image/font utilities expose enough portable data, and which
  need a narrow native bridge rather than reuse of a PDF resource resolver?
- What measured execution and allocation limits cover the target corpus, and
  where does the explicit object store need optimization?
- Which existing CLI invocation sites can call the procedural converter
  directly, and which need an explicit procedural wrapper?
- Do demand-scanned trailer metadata and supported device queries need a
  narrower initial contract? Resolve this with binary-source fixtures before
  claiming broad `currentfile` compatibility.

These are implementation/profile questions, not silently adopted language
rulings. Any required change to a formal ruling must be proposed and recorded
in both formal and working design documents before implementation.

## Appendix C. Initial delivered profile (2026-10-14)

The native parser and demand scanner live under `lambda/input/`; `ps_scan` is
a thin runtime bridge using an Input owner (**D4.1.3**). Literal-string parsing,
bounded ASCIIHex/ASCII85 decoding and SVG path painting are shared with PDF.
The script package implements explicit object IDs, procedure/control-flow
frames, local `save`/`restore`, and ordered vector draws, including clips,
paint-time transforms and bounding-box normalization. Standard 14 font lookup,
matrix derivation and graphics-state selection are also implemented, reusing
the PDF font-name mapping; text painting and metrics remain a later gate.

`lambda convert artwork.eps -t svg -o artwork.svg`, `render`, `layout` and
`view` route through the package. The conversion API is procedural
(**S12.1.2**); a `pn` failure is caught with a statement handler
(**S7.6.7v4**), for example:

```lambda
eps.eps_to_svg(doc) ^ { print(^.message) }
```

The scanner represents decoded PostScript string bytes as byte arrays for the
guest object store; its original source remains binary. Empty byte arrays
represent empty guest strings without introducing an empty Lambda binary
(**S2.2.2v2**). Valid UTF-8 DSC metadata remains text; undecodable metadata is
retained as binary. Names use escaped byte keys.

The initial profile supports name dictionary keys and boolean logical
operators. It rejects unsupported executed text/image/resource/file operators,
global VM, executable strings, and binary object encodings. It does not claim
LanguageLevel conformance. The current T0 module loader cannot activate this
import graph; execution is verified on auto/JIT, including forced GC. Broader
profile and interpreter-tier work remains explicit rather than silently falling
back to an external PostScript renderer.
