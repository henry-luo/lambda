# PDF output phase 2 — schema-backed Radiant views

> **Status:** implementation in progress, 2026-10-15. The fixed-record/native-reference foundation and an experimental continuous-page export are implemented; the complete migration is not qualified. See §10 for verified scope and remaining gates.
> **Confirmed direction:** retain shared native tree walking; pass schema-backed views and traversal context to Lambda callbacks in rendering order; Lambda reads view properties and emits PDF directly. Native helpers supply glyph outlines, image bytes and effect rasterization as needed (USER, 2026-10-14). Traversal links stay native; schema pointer access serves property/resource references.
> **Design basis:** [PDF Output](../Lambda_Design_PDF_Output.md), [Binary Schema](../Lambda_Type_Binary_Schema.md), [Virtual Containers](../Lambda_Design_Type_Virtual.md).
> **Formal linkage:** S11.5.1v2, S11.5.2, S11.5.3v2 / D3.5.1–D3.5.2 (binary records/native references); S2.6.6v2 / D2.6.12v2 (PDF byte destination); D3.1.1v5 (type descriptors); D7.4.1v2 / D7.4.4 / D7.4.5v2 (host boundary and virtual carriers); D2.6.8 (node identity); D4.1.4v5 / D4.5.1v4 (native storage lifetime); D5.3.3–D5.3.4 (precise roots); S12.1.1v2 (effects). PDF follow-ups remain SO48 / DO32.
> **Scope:** the next implementation after the [PDF byte-output MVP](Lambda_Impl_PDF_Output_MVP.md), including the unfinished export bridge from the earlier design's phase 1 and the resource/helper work assigned there to phase 2. This numbering is independent of the binary-schema proposal's own phases.

## 1. Outcome and current starting point

Lambda code should read final Radiant geometry and paint properties through
ordinary element field access, plan PDF objects and resources, and emit a PDF
file using the existing byte destination. Radiant continues to perform document
loading, CSS resolution, layout, shaping, and shared paint traversal.

```text
committed Radiant view + export session
    → shared native tree walker
    → ordered Lambda callbacks(schema-backed view, traversal context)
    → view/property reads + declared native resource helpers
    → direct Lambda PDF drawing and object/resource emission
    → PDFBuilder byte destination → explicit output
```

The existing MVP implements raw string/binary contributions and dictionary
encoding in `<file format:'pdf', …>` on interpreter and MIR Direct
(S2.6.6v2 / D2.6.12v2). It does not implement a Radiant export package,
destination forwarding through producer functions, or general byte accounting.

Repository inspection on 2026-10-14, at HEAD `f79e675ef` with local changes
present, establishes the following starting points. These are source findings,
not a new verification run:

| Area | Existing implementation | Work needed |
|---|---|---|
| Virtual containers | `Velmt`, `VArray`, status-bearing access callbacks, tracing and destruction in `lambda/lambda.hpp` and `lambda/runtime/vmap.cpp` | Add a schema backend using these carriers. |
| Binary storage | `lambda/core/binary.{h,cpp}` and `heap_binary_slice` already provide storage/slice operations | Audit and reuse their ownership contracts for buffer schemas; native object graphs need their own owner contract. |
| Binary schemas | `Lambda_Type_Binary_Schema.md` describes the feature; no `BinarySchema` / `TypeBinarySchema` implementation was found | Implement the required schema/compiler/runtime subset as an explicit prerequisite. |
| Continuous views | `View` aliases `DomNode`; `DomElement` / `DomText` hold view-property links | Bind actual layouts, subtype checks, and property lifetimes. |
| Secondary/paged views | `LayoutViewNode`, `ViewNodeState`, `ViewPageBox`, `LayoutViewRef` in `radiant/view_tree_model.hpp` | Bind the selected view and occurrence, including generation validation. |
| PDF rendering | `render_export_session_to_pdf`, `render_secondary_view_to_pdf`, shared walker, native `HPDF_*` writer | Add a Lambda callback backend; factor the paged traversal into a shared visitor where needed; qualify both export modes. |

The older binary-schema sketch proposes a new `VElmt` layout, an element
virtuality flag, a new schema TypeId, and the removed C-text transpiler. Those
implementation sketches are superseded by D7.4.5v2, D3.1.1v5, and the current
MIR-only architecture. The backend belongs in `Velmt::data`; it must not
replace the common carrier header or confuse it with `VirtualOutputElement`.
The latter is the separate writable PDF destination under D2.6.12v2.

## 2. Kaitai's pointer model and the additional native requirement

Kaitai models file references with a stored numeric offset and a lazy parse
instance whose `pos` uses that offset. `io` chooses the addressed stream;
substreams give relative origins. A target is parsed on first access and
cached. See the official [instances](https://doc.kaitai.io/user_guide.html#_instances_data_beyond_the_sequence)
and [absolute positioning](https://doc.kaitai.io/user_guide.html#_absolute_positioning)
sections.

```yaml
meta:
  id: pointed_record
  endian: le
seq:
  - id: body_offset
    type: u4
  - id: body_size
    type: u4
instances:
  body:
    io: _root._io
    pos: body_offset
    size: body_size
    if: body_offset != 0
```

Here zero is a sentinel chosen by this format. It is not a universal Kaitai
null-pointer rule. The target is a byte span; a user-defined `type` can instead
describe its contents. The instance does not consume a second inline slot in
the sequential record.

This is pointer-like access within streams. The documented abstraction does
not directly dereference a C/C++ address into a live object graph. Even
[memory-mapped input](https://doc.kaitai.io/faq.html#does-it-support-mmap-memory-mapped-to-access-files)
is wrapped as a stream. The architectural inference for Lambda is to reuse
lazy typed references while adding an explicit, host-owned native-memory
address domain.

## 3. Proposed pointer contract

The fixed-record and native-reference subset below is implemented under
S11.5.1v2, S11.5.2, S11.5.3v2 / D3.5.1–D3.5.2. Examples using `layout: 'native'`
and `pointer: 'native'` are runnable. The proposed offset-pointer and counted
span extensions remain design notation, and have no parser/runtime support.

### 3.1 Keep the logical type separate from physical indirection

Use the field's type for the value visible to Lambda. Use its layout annotation
to say that the field stores a reference to that value:

```text
// Native binding names refer to host-registered layouts.
type RadiantElement : binary & {layout: 'native', binding: 'radiant.DomElement'} {
    x: f32
    y: f32
    width: f32
    height: f32
    bound: RadiantBoundary? & {pointer: 'native'}
    font: RadiantFont? & {pointer: 'native'}
}

type RadiantTextRect : binary & {layout: 'native', binding: 'radiant.TextRect'} {
    x: f32
    y: f32
    width: f32
    height: f32
    start_index: i32
    length: i32
}
```

These are selected fields from the bound native types. Declaration order does
not pack a new C structure or determine its offsets. A host binding supplies
the physical location and representation of every declared field.
`RadiantBoundary` and `RadiantFont` stand for separately declared schemas for
the admitted fields of `BoundaryProp` and `FontProp`; they are not opaque
third-party font objects.

Reading `view.bound` returns `null` or another lazy `Velmt`; further property
access follows its schema after the caller handles null. Scalars behind
references decode to scalar Items.
No script-visible address, pointer arithmetic, or new general pointer value is
introduced (D7.4.1v2). A pointer field occupies its storage slot, not the size
of its pointee. Reject infinitely recursive inline layouts during schema
compilation. General recursive reference graphs can be added independently;
the PDF schema surface needs no parent/sibling navigation.

Prefer this annotation spelling over `T*`: `*` already denotes repetition in
Lambda types (S11.1.6v3). The user selected trailing `& {…}` metadata on
2026-10-15 (S11.5.1v2, S11.5.3v2). This is scoped to binary declaration
slots; ordinary intersections and `that` predicates retain their meaning
(S10.1.1v4). Metadata has a separate AST channel from predicate constraints.

### 3.2 Two address domains, one typed-reference descriptor

| Property | Buffer offset | Native reference |
|---|---|---|
| Source | A bounded immutable `binary` / slice | An object admitted by a live host export session |
| Reference storage | Explicit fixed-width integer and endianness | Compiled host pointer representation or declared native handle resolver |
| Target location | Checked base plus decoded offset | Typed reference obtained from a registered native field |
| Base | Explicit root buffer, current bounded record/substream, or reference-slot end | Host object graph; never a file offset |
| Null | Explicit format sentinel; zero can be a valid offset | Native null, interpreted with logical nullability |
| Bounds | Contained in the selected source span | Valid host object/region and extent under the session's ownership contract |
| Serialization | Requires an explicit offset/relocation policy | Native addresses cannot be serialized or reconstructed from arbitrary bytes |

An illustrative wire reference would be:

```text
header: Header? & {
    pointer: {kind: 'offset', storage: u32, base: 'root', null: 0}
}
```

The field's own `offset` locates the reference slot. `base` locates the target's
origin; it must not silently inherit the current decoder cursor. Offset zero
is valid when no sentinel is declared. Resolve signedness, subtraction, and
addition without overflow before reading target bytes. Following a reference
does not advance the containing record's inline cursor.

Implement native references for PDF through the common typed-reference
descriptor. Buffer-offset support remains the corresponding binary-schema
extension, with its own fixtures; it need not block PDF integration. General relocatable serialization, arbitrary
process-memory inspection, and cross-process pointers are outside this phase.
Ordinary `binary.decode` must reject native-pointer schemas: interpreting bytes
as an address cannot grant a host memory capability.

### 3.3 Arrays, strings, subtypes, and links

The compiled field descriptor distinguishes inline storage and one reference
to an object or contiguous span. Traversal chains stay behind native visitors.

| Shape | Proposed access behavior |
|---|---|
| `T?` with native pointer annotation | Null or a lazily decoded `T`. Null in a nonnullable scalar/struct reference is an error. |
| `T[count]` with native pointer annotation | One pointer to `count` contiguous `T` records; a lazy `VArray`. It does not mean an array of pointers. |
| `binary[length]` with native pointer annotation | A bounded byte span with an explicit owner. Never infer a length from an address. |
| Pointer plus zero count | Allow native null to represent an empty nonnullable span; null plus positive count fails. Pin down nullable-span distinctions in P0. |
| Native text | Use the existing length where available; a NUL-terminated field needs a bounded scan within an admitted span and an explicit encoding. |
| View/tree or text-fragment chain | Native traversal follows its links and supplies the current record to a callback; no link field is required in the Lambda schema. |
| Tagged native union/subtype | Validate the discriminator and admitted object type before selecting a target schema or union arm. |

The schema compiler must support a dependency on a count field even when that
field occurs later in the C layout. Logical schema order can put the count
first; native physical offsets still come from the binding. Validate negative
counts and `count × stride` overflow before constructing an array view. A
pointer-to-pointer table needs a separate explicit descriptor and is deferred
until an audited export consumer needs it.

Use `DomNode::node_type` for DOM subclass admission and `view_type` for layout
role; the binder must never read element-only fields from a text node.
Discriminated property/resource unions require the same admission checks.
Opaque font, image, vector-backend, and
function-pointer fields remain declared resource interfaces; the schema must
not guess their private structures or call a decoded function pointer.

### 3.4 PDF callbacks do not need traversal pointers

The native walker owns `first_child`, `last_child`, `next_sibling`,
`prev_sibling`, and `parent` navigation. Omit these fields from the PDF view
schemas. No recursive child projection or parent-chain traversal is required
for PDF emission. The walker supplies accumulated coordinates, inherited
font/color, page/fragment identity and balanced enter/leave events, so callbacks
do not reconstruct those facts by following ancestors.

Apply the same principle to `TextRect::next`: use the existing native text
fragment iterator to deliver the selected fragment to Lambda. A dedicated
resource collection may expose a bounded `VArray` when a callback needs indexed
data, but its internal links remain hidden. Other clients can add a traversal
interface later without making it a PDF prerequisite.

Pointer support is still needed for fields such as `DomElement::bound`,
`font`, `transform`, property subrecords, and pointer-plus-count resource
arrays. These expose logical property values through the generic schema
reader. The distinction is purpose: native code navigates the tree, while
Lambda reads the current view's data. D7.4.1v2 still keeps native addresses
out of script values.

## 4. Generic schema backend and native binding

### 4.1 Compile a shared access plan

Extend the binary-schema descriptor with the following information:

- Logical field type, schema/name identity, and attribute/content mapping.
- Physical field locator, scalar storage width, alignment and endianness.
- Inline/reference mode, target schema, address domain and null policy.
- Count/extent dependency, stride, discriminator, and reference-base rule.
- Native binding identity and ABI version; no handwritten numeric offsets.

Schema types remain `LMD_TYPE_TYPE` descriptors under D3.1.1v5. Select an
appropriate internal type kind/extension during P0; do not introduce the
standalone `LMD_TYPE_BINARY_SCHEMA` tag from the old sketch. Resolve named
schemas before binding fields, then reject impossible inline cycles and
cyclic layout/count dependencies. General recursive-reference schema support
is separate from the bounded property graphs used by PDF.

A backend payload conceptually carries `{schema, source, location, owner,
generation, cache}`. `source` is either a bounded byte store or a native
capability. The common accessor validates the owner, resolves the field,
checks its representation and extent, then returns a scalar, subview, or
array. No PDF code is embedded in that accessor.

Use length-aware field lookup and the existing virtual callbacks. Generate
the declared host member records from the same schema manifest under D7.4.4;
do not add a second per-property dispatch table. Every entry path—including
enumeration, indexed access, validation, and formatters—must observe the same
field and failure contracts.

### 4.2 Let the host compiler bind C/C++ layouts

Lambda declarations name fields and their interpretation. A build-generated
C++ binding, compiled with the actual headers and flags, supplies storage
sizes, permitted fields, subtype operations, and field locations. Use compiler
`sizeof`/`alignof` and valid `offsetof` expressions for eligible layouts.
Validate field type/width and the complete binding version when loading the
schema. Reject a mismatch before publishing an export view.

Radiant uses inheritance and pointer wrappers such as `lam::Own`, `lam::Up`,
`lam::ViewProp`, and `lam::ViewRef`. Do not assume a wrapper is a raw pointer
or apply portable `offsetof` claims to non-standard-layout derived types.
Generate typed field-locator/unwrap operations where necessary, using the
existing wrapper accessors and checked base/subclass conversions. Bitfields
need a generated typed read, not an invented byte offset. The implementation
should share these binding templates; it must not accumulate handwritten
Radiant getters for ordinary fields.

This preserves one generic schema access engine while letting the C++ compiler
handle representation details that a portable packed-buffer schema cannot
infer. Binding metadata exposes the approved fields only; it is not a memory
dump, and padding and unrelated pointers are not visible.

### 4.3 Expose schema records, with native traversal

The callback argument is an explicitly tagged export record backed by native
view storage. Its geometry and selected properties are schema attributes.
It is not the existing DOM wrapper with its children silently removed; choose
export-record tags and expose source tag/namespace as metadata when needed.
The PDF record schema declares no tree children. This fits the binary-schema
proposal's attribute mapping (§9, decisions 2–3) and the Velmt structural
contract (D7.4.5v2).

Use `VArray` for declared counted resource fields and native helpers for
fragment iteration. Fallible preparation must propagate through the existing
virtual interface; an invalid resource cannot appear to be an empty one.
The native walker retains full-depth traversal independently of this record
surface and the custom-layout wrapper's historical depth allowance.

The PDF binding admits a finite, selected property graph and omits ownership
backlinks. Shared targets remain valid and lazy; reject any cyclic descriptor
expansion in this profile. General cyclic schema projection/equality is a
separate feature, not a dependency of this export path. Wrapper reuse is keyed
internally by owner/generation, location and schema. Script-visible identity
and equality continue to follow D2.6.8 and S5.4.1–S5.4.3, not native addresses.

## 5. Lifetime and selected-view contract

An export session freezes a committed view generation and owns or retains its
document, view storage, resource owners, and schema metadata for the complete
export. A GC root for a wrapper does not keep a native arena alive
(D4.1.4v5 / D4.5.1v4). No schema pointer transfers ownership of its pointee.

These native projections are read-only. Reject writes through fields or
arrays and attempts to encode native addresses. Byte-schema write-through
and general re-encoding from the older proposal remain separate work; PDF
construction reads these views and writes a different destination.

Reuse `RenderExportSession` and the selected-view lifecycle. A wrapper must
hold a stable control object, not a pointer to the stack-local session struct.
On close, invalidate that control object before releasing any native data.
Escaped wrappers may keep the control object alive; their subsequent reads
must report invalidation before touching native storage. Resource arrays, cached
fields, text spans, and resource wrappers all obey that check.

Native references are admitted only through registered bindings rooted in
this session. There is no portable test that makes an arbitrary numeric
address safe to dereference: correctness relies on the native ownership graph
and typed binding. Add region/type checks where the host can provide them,
but do not mistake an in-range address for proof of a live object of type T.
Prevent relayout, destruction, and resource replacement during synchronous
reads; do not rely solely on checking a generation before a racing access.

For default views, bind `DomNode` geometry and the appropriate element/text
properties. For a selected secondary view, bind `LayoutViewNode` occurrence
geometry and `ViewNodeState` / per-fragment computed properties. Resolve
`LayoutViewRef` and `DomNodeRef` through their existing validators. Reading the
default DOM geometry while exporting a secondary page would select the wrong
layout even if every pointer were valid.

Traversal-context and helper-result records may have shorter lifetimes than
the session. Borrow them only for the synchronous callback, invalidate their
generation before reusing scratch storage, and copy or retain an explicitly
selected resource if it must survive. Session-backed view records can live
until session close. A borrowed byte span must not escape as an ordinary immutable
`Binary` whose owner can disappear; use existing retainable byte storage or
copy that leaf when required by its public lifetime. This does not require
materializing the view tree.

Trace every retained Item edge through the virtual vtable and root C++ local
Items with `RootFrame` / `Rooted` (D5.3.3–D5.3.4). Cache admission, schema
resolution, nested reads, callback errors and partial initialization all need
failure-safe cleanup. Session close and wrapper destruction must each be
idempotent with respect to the native resources they release.

## 6. From schema views to PDF output

### 6.1 Native callbacks and direct PDF drawing

Implement a `RenderBackend` adapter that invokes Lambda callbacks while the
shared native walker visits the committed view. The callbacks receive the
current schema-backed view and explicit traversal context. Lambda decides
how to draw that view from its resolved properties and emits PDF operators
directly. `PaintCmd`/`PaintList` is not the script-side drawing interface.
This is the selected approach in
[PDF Output §4.2](../Lambda_Design_PDF_Output.md#42-preserve-paint-order-and-rendering-semantics).

The callback contract must cover the following events; names are provisional:

| Event | Data supplied by native code | Lambda work |
|---|---|---|
| Begin/end page | Selected page geometry, boxes, labels and output transform | Page objects and page-level graphics state. |
| Draw boundary/marker/column rule | Current view, resolved properties and absolute placement | Background, border, marker and rule PDF drawing. |
| Draw text fragment | Current text view/fragment, baseline, inherited font/color and resource access | Text/font or outline emission using selected positions. |
| Draw image/vector content | Current view and declared resource helpers | Image/path objects and PDF placement. |
| Begin/end transform, clip, effect | Matched scope identity and resolved scope data | Balanced graphics state and the admitted direct/helper treatment. |
| Visit semantic element | Source identity, link/destination data and current placement | Annotations, anchors and other document semantics. |

A generic node-entry callback alone is insufficient: the walker must preserve
the relative placement of self paint, descendants, stacking phases, and scope
boundaries. Supply the coordinate space for each event. Parent-relative view
fields and accumulated absolute positions must not be combined twice. Use
existing inherited-style and text-fragment helpers rather than asking Lambda
to walk parent or `TextRect::next` links.

Callbacks are synchronous and run through the sanctioned runtime invocation
path, with rooted callable, arguments, result and emission state
(D5.3.3–D5.3.4 / D5.4.1). They cannot suspend beneath a native callback barrier
(D7.4.2v2). Carry the PDF destination explicitly through the adapter; never
install a global or EvalContext output destination (D2.6.12v2). P0 settles the
callback effect signatures and producer-demand ABI under S12.1.1v2 / DO32.

The current `RenderBackend` has several `void` callbacks. Add a shared checked
abort path or adapt existing propagation so the first Lambda error stops
further emission, returns the original error, and releases native scopes.
Do not continue a failed callback as a successful missing drawing operation.
For an effect/subscene handled by rasterization, an explicit handled-content
contract suppresses duplicate descendant drawing while preserving required
semantic visits. Native helpers may use PaintIR internally; the Lambda
callback consumes their declared result, never a general paint-command stream.

The continuous path already has shared walker callbacks. The paged path
currently produces a `PaintList` through `layout_secondary_paint_page()`;
factor its ordered fragment traversal into a shared visitor so native painting
and the Lambda callback backend can both use it. Do not claim this adapter
already exists, duplicate pagination, or reconstruct view callbacks from
paint commands.

Export the selected physical page sequence through the existing page visitor.
Preserve page boxes, source rotations, labels, fragment ranges, annotations,
destinations, outlines and metadata already supported by the current route.
Preview grids, shadows and presentation scale stay outside physical PDF
content. Declare each coordinate space and apply its PDF-point conversion
once, following RSC1 / RSC6–RSC8 in [Radiant Scale](../radiant/Radiant_Scale.md).
This phase consumes existing pagination; it does not add a second paginator.

### 6.2 Lambda document construction and byte accounting

Extend `lmd/package/pdf/` with output modules and expose them through the
existing package facade under D7.2.4. Reuse matching coordinate/color helpers
and the MVP's dictionary encoder rather than writing another encoder for the
same admitted values. Keep PDF object/reference/stream construction and
cross-reference/trailer policy in Lambda.

Complete the remaining producer-destination work from PDF Output §5.5 and
DO32 before claiming that nested emitters avoid fragment Item arrays. Schema
views and output builders are separate mechanisms; improving one does not
implement the other. Preserve ordinary function/list behavior outside an
explicit PDF destination, with interpreter/MIR parity.

Include the native-walker-to-Lambda callback edge in that destination-forwarding
work. A callback's output must append exactly once, in callback order, to the
selected measuring or emitting destination. Do not collect all callback
returns into a document-sized list as a substitute for forwarding.

Use the existing design's deterministic measurement proposal as the baseline:
freeze the resource/object plan, measure with the same encoding rules, compute
byte offsets, then emit. Count encoded bytes, never Unicode character counts.
Prepare image/font/compressed payloads once, and count their final bytes.
Run resource discovery and any effectful preparation once; measurement and
emission may revisit the frozen tree only with deterministic callbacks over
that frozen resource plan. Cache prepared fallbacks so the later passes do
not repeat external work or change the callback sequence.
Effectful work executes once before measurement; do not replay arbitrary
procedures (S12.1.1v2). Any single-pass destination-position API requires its
own explicit contract under SO48 / DO32.

Continue to buffer final bytes under S2.6.6v2. The completed file is written
through explicit I/O only after successful construction. A failed pointer
read, resource conversion or encoder must not publish a partial PDF or
overwrite an existing output. General native-pointer serialization and
retained-file mutation are not prerequisites for producing this file.

### 6.3 Define the phase 2 helper boundary

The first integrated fixture may use basic vectors and positioned text. Full
phase 2 qualification also needs an audited treatment for every operation
admitted by the current exporter:

| Resource/feature | Native responsibility | Lambda responsibility / gate |
|---|---|---|
| Paths, strokes, transforms, clips | Existing path visitation and resolved geometry | Operators, graphics-state balance, fill rules and stroke semantics. |
| Text and fonts | Existing shaping, selected faces, glyph IDs/positions, outline or font-byte access | PDF font/resource policy; match current visual coverage. Searchable Unicode requires separate embedding/subsetting and ToUnicode acceptance, not merely escaped Unicode strings. |
| Images and alpha | Owned decoded pixels or admitted encoded image bytes, dimensions, stride and color description | Image objects, color spaces, soft masks and references; preserve alpha and color interpretation. |
| SVG, gradients and effects | Shared vector lowering where available; explicit bounded raster helper for supported fallbacks | Select and encode the admitted representation; preserve density, clip and group semantics. |
| Compression | Reuse an existing codec behind a declared byte-to-byte helper | Stream filters, encoded byte lengths and deterministic resource reuse. |
| Links, page labels, metadata and outlines | Resolved page/source identities and geometry | PDF dictionaries and references across the selected page sequence. |

Do not inspect third-party font/renderer objects with binary schemas. Expose
those capabilities through declared, owner-aware helpers (D7.4.4), sharing
existing Lambda-side routines. Promote a needed static helper into its module
header instead of copying it. New font capabilities, PDF/A, tagged PDF,
encryption and signatures require separately scoped acceptance; none follows
automatically from this migration.

## 7. Implementation milestones

The milestone table retains the full acceptance scope. Implementation status
is recorded in §10; completion of a fixed-record backend or one continuous
export fixture does not mark this whole migration complete.

| Milestone | Work | Exit gate |
|---|---|---|
| **P0 — ratify contracts and inventory** | Reconcile binary-schema metadata with current grammar/type representation; settle property-pointer/null/count notation, native binding, export-record tags, callback scopes/effects and output-demand ABI. Inventory current continuous/paged export features and fields. | Record remaining language decisions in the working/formal specs together, with semver/ruling revisions and `make formal-index` when those rulings change. The native-walker/direct-emission architecture is already selected. |
| **P1 — schema foundation** | Implement the required binary type/annotation representation, fixed fields, nested schemas, counted arrays and lazy reads. Reuse current binary storage and virtual carriers. | Native tests plus Lambda fixtures demonstrate fixed-layout decoding, laziness, bounds failures and interpreter/JIT/auto parity. General buffer-reference/recursive-graph features remain separate. |
| **P2 — native reference bindings** | Compile host bindings, add native source/owner controls, typed pointer loads and shared wrapper adapters. Implement generic Velmt/VArray access and reference/extent validation. | Two unrelated fixture structures use the same backend; ABI mismatch, null/count errors, stale owners and forged native decoding fail safely; no field-specific PDF getters. |
| **P3 — Radiant schema views** | Declare export-record schemas for default and secondary views, selected properties, text fragments and pages; integrate session ownership and subtype selection. Omit tree and fragment-chain links. | Values match direct native reads for real layouts; shared properties, multiple occurrences and simultaneous view selection are correct; the PDF surface requires no navigation pointers. |
| **P4 — native walker callbacks and helpers** | Add the Lambda `RenderBackend` adapter, text-fragment callbacks, checked abort propagation and declared resource helpers. Share ordered paged-fragment traversal with its native painter. | Callback order, placement and scope pairs match native traversal; deep trees need no Lambda recursion; failure and handled-subtree paths never omit errors or double-paint descendants. |
| **P5 — Lambda PDF emitter** | Move view-to-PDF drawing rules into Lambda callbacks; finish destination forwarding across native callback invocation, byte accounting and object/resource modules. | A real laid-out document produces independently readable PDF directly from callbacks, with correct offsets/lengths and no PaintCmd consumption or full fragment Item array. |
| **P6 — phase 2 coverage** | Implement image/alpha, text/font coverage, compression and admitted SVG/effect handling; carry metadata and selected physical pages. | Feature matrix passes against independent references, including existing paged output. Any incomplete row remains visible and blocks whole-export replacement. |
| **P7 — integrate and qualify** | Connect CLI/session entry points, run lifecycle/correctness/platform gates and release measurements, then switch production export. | Supported behavior meets agreed gates; remove obsolete native PDF code only after a caller audit. Retain evidence for known pre-existing defects. |

Dependency order is P0 → P1 → P2 → P3 → P4 → P5 → P6 → P7.
After P0, byte-accounting/producer work can proceed independently of the
Radiant bindings, but both must meet at P5. Any temporary development selector
must choose a path explicitly; unsupported features must not silently fall
back to the old exporter and make the new path appear complete.

## 8. Code touch map

| Area | Entry points / intended changes |
|---|---|
| Syntax and AST | `lambda/runtime/parser/`, `lambda/runtime/build_ast.cpp`, `lambda/runtime/parse_type_pattern.cpp`; update the Lambda-owned CST grammar and regenerate it. Current `that` constraint parsing must be preserved. |
| Schema representation | `lambda/lambda-data.hpp`, `lambda/lambda.hpp`; add internal schema descriptors and source/binding metadata within the current type/carrier model. |
| Generic reads | Proposed `lambda/runtime/binary_schema.{hpp,cpp}`; reuse `lambda/core/binary.{h,cpp}`, existing field-name helpers and virtual dispatch. |
| Execution | `lambda/runtime/interp.cpp`, `lambda/runtime/transpile-mir.cpp`, shared runtime helpers; schema constants, access parity and destination forwarding. |
| Host binding | `lambda/module/radiant/radiant_module.cpp`, `radiant_dom_bridge.{hpp,cpp}`, Jube interface records; proposed schema declarations under `lmd/package/` and one build-generated binding manifest. |
| Native data | Read layouts from `lambda/input/css/dom_node.hpp`, `dom_element.hpp`, `radiant/view.hpp`, `view_tree_model.hpp` and `render.hpp`; extend only for a demonstrated missing export contract. |
| Export | `radiant/render_output.cpp`, `render_walk.cpp`, `layout_paged.cpp`, `render.hpp` and `render_pdf.cpp`; add the callback backend and share ordered fragment traversal, selected-page and resource lifecycle APIs. |
| PDF package/builder | `lmd/package/pdf/`, `lambda/runtime/pdf_builder.cpp`, `lambda/io/mark_output_builder.hpp`; share encoders and add the agreed accounting/producer integration. |
| Build and tests | `build_lambda_config.json`, existing test harnesses, and matching script goldens. Never hand-edit generated Lua or Tree-sitter parser output. |

## 9. Verification and completion criteria

### 9.1 Schema/reference correctness

- Exercise native null, absent optional property, shared target and a tree
  deeper than the current custom-layout projection limit. Confirm lazy field
  reads; traversal completeness must not depend on script-visible links.
- Test contiguous arrays, zero/nonzero count with null, negative/overflowing
  lengths, out-of-range field spans and bounded strings containing non-ASCII
  or embedded NUL. Test offset-reference bases/sentinels with that independent
  binary-schema extension when it is implemented.
- Cross-check native scalar/property reads against direct compiled access,
  including `f32` geometry, padding, inherited fields, pointer wrappers,
  union discriminators, field-width mismatches and stale ABI manifests.
- Verify attribute enumeration, export-record tags, declared empty content,
  validation, equality and formatting through the virtual interface. Reject
  cyclic descriptor expansion in the PDF profile; errors must not become
  missing fields or empty pages.
- Cover document/view generation changes, selected secondary views, retained
  resource arrays, callback-context reuse, failed allocation, and escaped
  handles after callback/session close. Force collection during callback entry,
  field reads, boxing/cache insertion and nested helper invocation.

### 9.2 PDF and resource correctness

- Validate object references, stream lengths, xref offsets, `startxref`,
  resources and page geometry with an independent PDF reader. Include UTF-8
  source text, binary zero bytes and compressed payloads in byte accounting.
- Rasterize small exported fixtures independently and compare geometry,
  interior colors, text placement, stacking, clips, images, alpha and effects.
  Compare with the existing exporter as additional evidence; existing renderer
  defects are not golden behavior to preserve blindly.
- Trace callback ordering for stacking contexts, generated content, text
  fragments and nested scopes on both continuous and paged views. Test a
  callback error, forbidden suspension and helper failure inside nested scopes;
  verify cleanup, error propagation and no further output. Rasterized effect
  groups must appear once, with their links/destinations preserved.
- Check selected-page count/order/boxes/rotation/labels, annotations and
  metadata. Export the same selected view under different preview arrangements
  and confirm that presentation does not affect physical PDF content.
- Verify no output file is replaced on constructor/helper/invalidation failure.
  Inspect allocations to distinguish legitimate object/resource tables from
  a duplicated view tree or materialized output-fragment arrays. Confirm the
  Lambda PDF package has no dependency on PaintCmd layouts and receives no
  `parent`, child/sibling or text-fragment `next` fields for traversal.
- Measure release time and peak memory on small, large, image-heavy and paged
  documents. Record host/compiler/build provenance and compare the same inputs
  with the current exporter. Use release builds in the primary checkout;
  follow Developer Guide §7 before worktree builds. Do not invent a performance
  threshold before establishing the paired baseline.

### 9.3 Repository gates

Follow [test/README.md](../../test/README.md). Extend the existing PDF builder,
virtual-container, Lambda script, layout and PDF-render harnesses. Every new
test `.ls` gets its matching `.txt`; register any new directory or executable.
Run focused tests first, then `make test-lambda-baseline` for runtime/schema
changes and `make test-radiant-baseline` for the export bridge. Include
`make test-pdf-render` with separately reported export and input coverage;
run `make test262-baseline` if shared JS/Jube behavior changes. Run dimension
lint for Radiant layout changes. Forced-GC checks use the current documented
knobs; temporary artifacts belong under `./temp/`.

The phase is complete only when schema-backed native access, complete
selected-view traversal, declared resource handling, Lambda PDF construction,
failure/lifetime behavior and the agreed export feature matrix pass together.
Until then, report the completed milestones and exact remaining rows.


## 10. Implementation progress (2026-10-15)

The experimental route is executable through `lambda.pdf.output.render` /
`.export`, and through `lambda.pdf.pdf.render_source` / `.export_source`. It
loads a private print-media `RenderExportSession`, keeps the shared native
walker, and invokes a rooted pure callback with `(event, record, context)`.
It does not change the production CLI PDF writer.

| Milestone | Current state | Remaining acceptance work |
|---|---|---|
| P0 | Fixed-record/native-reference metadata and lifetime contracts recorded in S11.5 and D3.5. | Counted-span and complete output-demand contracts. |
| P1 | Fixed-width byte fields, explicit offsets, nesting, endianness, lazy Velmt access and `binary_decode`. | Counted arrays and full compiler rejection corpus. |
| P2 | Generic compiler-derived member locators and typed raw/ownership-wrapper references; shared owner invalidation and precise tracing. | Counted spans, generated binding manifest/version checks and full virtual-operation invalidation audit. |
| P3 | Default element geometry, boundary/font/color/matrix properties, projected text fragments and traversal contexts; no navigation links. | Secondary-view occurrences and page schemas. |
| P4 | Shared continuous walker callbacks, checked abort, glyph and clip path helpers, copied image RGB/alpha bytes. | Shared paged visitor, remaining helper features, handled-subtree effects. |
| P5 | Lambda drawing operators, resource plan, object numbers, image soft masks, graphics-state alpha, stream byte lengths, xref/trailer and atomic output. | General producer destination forwarding and measuring destinations; multiple physical pages. |
| P6 | Rectangular fills/borders, positioned glyph outlines, image alpha, transforms and clips. | Rounded/decorated boundaries, decorated text, gradients, markers, SVG/vector scenes, effects, compression, semantics/metadata and physical pages. |
| P7 | Public package entry points and regression qualification started. | Production CLI integration/switch, remaining platform and release measurement gates. |

### Implementation details

- `lambda/runtime/binary_schema.{hpp,cpp}` contains the reusable descriptor,
  typed member bindings and byte/native Velmt backend. A native view owns a
  control reference, not its pointee. Selected Item fields and the binary
  source are precise GC edges (D5.3.3–D5.3.4).
- The direct parser and binder recognize `: binary & {…}` root metadata and
  trailing field `& {…}` maps in a separate AST channel (S11.5.1v2). Unknown
  metadata, invalid address domains, native byte offsets, nullable unboxed
  fields, methods and content/default fields are rejected. The reference
  grammar was updated and regenerated. General constraints are unchanged.
- `radiant_pdf_walk.cpp` binds the Lambda-selected fields to native layouts
  through `sizeof` and typed member locators, including inherited members.
  Session views invalidate before session destruction; scratch context and
  projected TextRect views invalidate after each callback. Escaped wrappers
  retain only the control object.
- A callback returns byte content, or `{content, resources: [...]}`. Content
  appends immediately to the explicitly owned PDF builder; only resource
  descriptors are retained as Items. A monotonic callback sequence gives
  Lambda stable resource names. Glyph and clip helpers return normalized
  path coordinates; images return owned RGB/alpha bytes plus placement.
  These results are capabilities, not a general PaintCmd stream.
- `lmd/package/pdf/output.ls` owns page drawing and classic PDF object/xref
  construction. The current document profile has exactly one continuous
  page; object/resource counts are dynamic. Numeric formatting rejects
  nonfinite values and expands exponent notation without rounding away
  runtime round-trip digits. Final bytes are buffered under S2.6.6v2 and
  explicit atomic output happens only after complete construction.
- Imported type declarations previously reused the index last assigned by
  another importing module. Interpreter and MIR type-value materialization
  now use the declaration identity / existing declaration resolver, which
  makes imported schema values retain their own descriptor (D3.1.1v5).

### Verification

Focused native tests cover inherited field reads, live lazy property reads,
nullable versus required references, read-only writes, width mismatch, byte
endianness/truncation/source retention, owner invalidation, escaped callback
contexts and exact callback-error preservation. A further native test runs
the resource fixture on interpreter and MIR and invokes
`test/pdf_output_check.py` to check every object offset and stream length.

`binary_schema.ls`, `pdf/output_views.ls` and `pdf/output_resources.ls` have
matching goldens. Resource output includes an RGBA image, translucent fills
and text, clipping, a transformed rectangle, rejection of unsupported
drawing, preservation of an existing destination, and finite-number checks.
Poppler independently opens and rasterizes the generated files; the images
were visually inspected. The resource fixture has eight indirect objects,
with all xref offsets and stream lengths checked directly against its bytes.

| Check | Result |
|---|---|
| `make build-test` | Pass; final native/API changes compile. |
| `BinarySchemaTest.*:PdfFileBuilder.*` | 12/12 pass after the `& {…}` migration, including native boxed-Item publication under forced collection, logical field contracts, callback purity, invalidation and original-error preservation. |
| Three new Lambda fixtures | 3/3 pass on interpreter and MIR with forced collection and freed-memory poisoning; includes i64 minimum, u64 maximum and inherited/explicit byte order. |
| Independent PDF byte check | Eight resource-fixture objects have correct xref offsets; every stream length addresses its exact terminator. |
| Poppler | Both fixture PDFs open and rasterize; rendered pixels visually inspected. |
| Compiler rejection probes | Six rejected: unknown metadata, invalid endian, buffer/native reference mixing, native offsets, nullable unboxed fields and field defaults. |
| Formal index / Radiant cast lint / diff whitespace | Pass. Formal-index tooling reports the existing duplicate DO25 definition; generated index is current. |
| `make test-lambda-baseline` | 6,668/6,669 pass. Its only failure is the MIR GC stress runner's 180-second idle timeout at `regression_music_layout`. The same case passes in isolation in 241 seconds, confirming the idle-limit cause for that failure. This aggregate run preceded the final boxed-field/inherited-endian tightening and the metadata syntax migration; focused tests above cover those changes. |
| `make test-radiant-baseline` | Not green: native suites mostly passed, UI 576/580, layout 3,143/3,294 and page snapshot 51/52. Repeated photo/view and scene-case timeouts continued; the run was interrupted after roughly ten minutes. No baselines or unrelated code were changed to hide these failures. |
| Native PDF visual suite | Reports failures in emphasis, background-layer blending, spacing, RTL overflow and group opacity; the run was interrupted after these failures and stalled progress. It is a separate legacy-render qualification run, not a passed migration gate. |

The metadata syntax migration (USER, 2026-10-15) also passes 39/39 native
lexer/parser tests, 404/404 reference grammar cases and 418/418 production
syntax cases. Thirteen schema/constraint/type-operator/PDF goldens pass on
interpreter and MIR under forced GC with freed-memory poisoning. All 106
checked `Lambda_Type.md` code units meet their declared compile contracts.
Seven new compiler rejection cases cover legacy `that` metadata, non-map
metadata, unsupported/duplicate keys and nested intersections. The parser
regression also preserves ordinary field/default reduction shapes consumed
by Jube interfaces.

Syntax migration logs are under `temp/pdf_schema_*`.
Earlier logs and generated artifacts are under `temp/pdf_phase2_*`; baseline details
are also in `test_output/`. Whole-phase completion still requires the feature
and integration rows above. The completed increment is a usable, explicitly
bounded export API, not a production-writer replacement.
