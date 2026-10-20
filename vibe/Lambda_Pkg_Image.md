# Lambda Image Package — Proposal

**Date:** 2026-10-16

**Status:** proposal requested by the user. On 2026-10-16 the user accepted
the revised schema in general: still images contain a `stack`, animations
contain `frame` children with a `stack` each, and nested stacks represent
groups. The shared asset container is named `assets`. The selected scope
also includes `lambda.image`, a `<raster>` virtual element, PSD input/output,
and image/photo editing. Detailed contracts, API names, and delivery profiles
below remain proposed, not implemented claims.

**Scope:** a common working model for still, animated, and layered raster
images, shared by scripts, document processing, and an eventual image editor.
Extend the existing image subsystem rather than establish another codec or
pixel-processing pipeline. A complete Photoshop implementation, video editor,
RAW developer, and GPU processing framework are outside the initial scope.

**Specification linkage:** **D2.1.4** (extensibility), **D7.4.4 / D7.4.5v2**
(declared members and virtual carriers), **S5.4.3** (element equality),
**S9.1.1–S9.1.3 / S9.2.2** (value semantics and views), **S12.1.1v3 /
S12.2.2** (effects and element writes), **D4.1.1v2 / D4.1.3 / D4.1.4v5 /
D4.2.6 / D5.3.3** (ownership and rooting), **D7.1.1–D7.1.3 /
D7.2.1–D7.2.4v3** (layering and packages), and **S7.4.1–S7.4.5 /
S17.8.1** (errors and options). See [Formal Design](../doc/Lambda_Formal_Design.md),
[Formal Semantics](../doc/Lambda_Formal_Semantics.md), and
[Doc Convention](../doc/Doc_Convention.md).

This document proposes image-domain contracts within those rulings. It does
not revise a formal ruling or ratify unspecified language behavior. The
existing [Common Image I/O Library](Lib_Image.md) remains the codec foundation;
this proposal adds the editing model that its initial scope deferred.

## 1. Selected model

An image is an editable document, a frame gives a composition its timing,
a stack orders its layers, a layer places content, and a raster holds pixel
samples. Stacks describe spatial composition; frames describe time. These
five concepts have separate lifetimes and responsibilities:

| Entity | Responsibility | Content |
|---|---|---|
| `image` | Document extent, resolution, working color description, playback and metadata | Optional `assets`, `metadata`, `preview`; one `stack` or ordered `frame` children |
| `frame` | One composition and its display duration | Exactly one root `stack` |
| `stack` | Ordered composition; nested stacks represent groups | Interleaved `layer` and `stack` children, plus optional masks/effects |
| `layer` | Placement, visibility, opacity, blending, masks and effects | One primary raster, text, vector, fill, embedded object or adjustment description; optional masks/effects |
| `raster` | Sample dimensions, channels, precision, color identity and pixels | Virtual attributes over a native buffer; no per-pixel child elements |

A still PNG/JPEG imports as `image → stack → layer → raster`. A PSD imports
as an image with one root stack and its layer/group hierarchy. A GIF imports
as `image → frame → stack → layer → raster`, initially with one displayed
raster layer per frame. Groups are nested stacks, not another kind of layer.

Layer/group order is **foreground to background** within each stack: index
zero is uppermost, matching OpenRaster and the editor's layer list. The
compositor traverses composition children in reverse to build the backdrop.
Frame order is temporal. Importers translate source ordering explicitly and
retain source record indices for export. Supporting asset, metadata, preview,
mask and effect children are not layer-order entries.
Editor IDs are document data, not addresses (**D2.6.8 / S9.1.5v2**).

### 1.1 Complete structural schema

The following notation describes the working schema, not new Lambda syntax.
`?` means optional, `*` means zero or more, and `+` means one or more.

```text
image
  assets?
    (profile | font | palette | asset)*
  metadata?
  preview?
  composition: stack | frame+

frame
  stack

stack
  (layer | stack)*
  mask*
  effect*

layer
  primary: raster | text | vector | fill | object | adjustment
  (kind: empty permits no primary content)
  mask*
  effect*

mask
  raster | vector

raster
  canonical descriptor + typed pixel samples, through Velmt attributes
```

An image chooses exactly one composition form; it does not mix a direct
root stack with frame children. Stacks never contain frames. Converting a
still image to animation explicitly wraps its root stack in a timed frame.
A single-frame animation may retain that frame and its duration; it is not
automatically collapsed into a still image. Each frame/root composition
starts from a transparent backdrop. Nested stack isolation is explicit.
The primary content alternatives above define the schema's target vocabulary;
the initial editable/renderable profile remains raster-based (§4.2). Empty
paint layers retain their metadata as `kind: empty` without inventing pixel
extents; they render transparently and preserve PSD zero-sized records.

### 1.2 Example working document

The example is schematic: `photo` and `mask` stand for Raster values built
by the proposed package, and `font_bytes` is a captured font binary. It shows
a still image with direct stack layers, a nested group, and a text layer;
text rendering belongs to a later profile than the initial raster compositor.

```lambda no-run
// no-run: lambda.image is proposed; photo/mask/font_bytes are supplied values.
import img: lambda.image

<image width: 1920, height: 1080, working_space: 'srgb', plays: 1,
    <assets
        <font id: "heading-font", family: "Example Sans",
              format: 'otf', subset: false, data: font_bytes>
    >
    <stack
        <stack id: "overlay", name: "Overlay", opacity: 1.0,
               isolation: 'isolated',
            <layer id: "heading", kind: 'text', position: [120.0, 40.0],
                <text font: "heading-font", size: 48.0, "Title">
            >
            <layer id: "accent", kind: 'raster', position: [120.0, 80.0],
                img.blank({width: 320, height: 160, color: [1.0, 0.0, 0.0, 0.5]})
            >
        >
        <layer id: "background", kind: 'raster', name: "Photo",
               position: [0.0, 0.0], opacity: 1.0, blend: 'normal',
            photo;
            <mask role: 'alpha', position: [0.0, 0.0], default: 1.0,
                mask
            >
            <effect op: 'exposure', stops: 0.5, enabled: true>
        >
    >
>
```

An animated image instead contains frame children, each wrapping the same
kind of root stack; no `frames` container is introduced.

### 1.3 Coordinates, timing and other multi-image formats

Raster indexing uses `[y, x, channel]`, top-down rows, with `(0,0)` at the
upper left. Raster dimensions are integral sample counts. Layer placement
uses document-space pixel coordinates, allows negative offsets, and may be
fractional after a transform. Rectangular regions use half-open
`[left, top, right, bottom]` bounds. Layers need not fill the document extent.

An animated frame has `duration: {ticks: n, rate: d}`, meaning `n/d` seconds,
with nonnegative integral ticks and a positive integral rate. This preserves
source timing without rounding everything to milliseconds. A still image
uses a direct root stack and has no frame duration. `plays` is the total
play count: `1` plays once, `0` repeats
indefinitely. Importers normalize format-specific repeat conventions.
Zero-delay source frames remain zero-delay data; a viewer's minimum-delay
policy does not rewrite the document. Sampling takes explicit time and a
documented boundary policy; rendering never consults the wall clock.

Pages and alternative resolutions remain distinct from animation. The
package can select a TIFF page or ICO representation as an image, or return
a collection of image values carrying source indices. It must not turn
pages or icon sizes into timed frames merely because they share a file.
This extends the distinctions already recorded in [Lib_Image §3.2](Lib_Image.md#32-image-and-option-contracts).

### 1.4 Assets and fonts

`<assets>` is the optional image-scoped collection of shared data. It
contains named `profile`, `font`, `palette` and generic `asset` entries.
An entry has a unique document-scoped ID and either captured `data` bytes or
an explicit external requirement/reference, plus format and descriptive
metadata. Text runs, color descriptions and embedded-object layers refer to
asset IDs; references remain data under **S9.1.5v2**, not parent pointers or
mutable aliases. Asset IDs share the image's documented ID namespace.

Rendering detached text/vector content receives the required asset scope
explicitly. A detached Raster retains its resolved color description and
necessary profile bytes/owner; pixel kernels never look up an implicit
parent document or fetch a missing profile. Resolving external assets is an
I/O operation before pure rendering, not a hidden font/network lookup.

A font entry records format, family/face identity, optional embedded binary,
and whether its data is subsetted. Embedded fonts are document-scoped,
not globally installed. An external font requirement preserves the source's
font identity even when no binary is available. Missing fonts produce a
diagnostic; explicit substitution or an available cached appearance is a
separate rendering policy. A glyph subset may reproduce current text while
omitting glyphs needed for later edits.

PSD native text normally references fonts needed on the editing system;
asset support does not imply that PSD embeds font binaries. OpenRaster's
baseline file layout defines no font-packaging contract. SVG supports web
font resources, and PDF supports embedded/subset fonts. Preserve fonts when
available and embedding permits it, while each exporter follows its own
format's actual capabilities.
[Adobe missing fonts](https://helpx.adobe.com/photoshop/desktop/text-typography/select-manage-fonts/replace-missing-fonts.html),
[OpenRaster file layout](https://www.openraster.org/baseline/file-layout-spec.html),
[SVG font resources](https://www.w3.org/TR/SVG2/text.html#FontsAndGlyphs), and
[Adobe PDF font settings](https://developer.adobe.com/acrobat-pdfl-sdk/guides/pdfcreation/pdf-create-usingsettings).

## 2. Raster virtual element

`<raster>` uses the existing **Velmt** carrier, with public element semantics
under **D7.4.5v2**. `RasterBuffer` is its native implementation, not a new
top-level language TypeId (**D2.1.4**). `image`, `frame`, `stack`, and `layer` are
ordinary composition elements; pixel-heavy behavior belongs in Raster.

Its canonical materialized structure is an element with these attributes:

| Attribute | Meaning |
|---|---|
| `width`, `height` | Positive sample dimensions |
| `sample_type` | Component type: initially `u8`; target `u16`, `f16`, `f32` |
| `channels` | Ordered unique names, e.g. `['r', 'g', 'b', 'a']` or `['gray']` |
| `color` | Color model, transfer/working-space identity, and optional ICC bytes |
| `alpha_mode` | `straight`, `premultiplied`, or `none` |
| `pixels` | Logical `(height,width,channel_count)` typed numeric sample array |

The channel axis and descriptor must agree. Width, height and sample type
are derived from one authoritative descriptor, not separately mutable
copies of array shape. A raster has zero structural children. Empty
compositions are valid; an empty crop returns a checked operation error
rather than constructing an inconsistent zero-size Raster.

### 2.1 Channel properties and array access

The proposed interface exposes:

| Access | Result |
|---|---|
| `raster.pixels` | Dense typed sample snapshot in `(H,W,C)` order |
| `raster.r`, `.g`, `.b`, `.a` | `(H,W)` snapshot of that named channel, when present |
| `img.channel(raster, name)` | Checked named-channel access, including non-RGB channels |
| `img.read_region(raster, rect, options)` | Dense snapshot of a requested region and channels |
| `img.from_array(samples, descriptor)` | Raster value constructed from typed samples |
| `img.materialize(raster)` | Ordinary element carrying the canonical descriptor and pixels |

Aliases are declared projection members under **D7.4.4 / S12.3.3v2**,
not extra enumerable copies of the pixel data. A missing property follows
normal missing-member behavior (**S7.1.1v3**); the checked channel function
returns a descriptive error for an absent channel. A grayscale/CMYK raster
does not silently become RGB when `.r` is requested. Alpha is a physical
channel when present; an opaque input may have `alpha_mode: 'none'`.

Contiguous storage can supply read-only strided channel snapshots over
shared storage. Tiled storage materializes a dense snapshot when a dense
array is requested; region access avoids forcing the whole image. A VArray
does not automatically acquire ArrayNum's N-D kernel interface. The initial
bridge returns actual ArrayNum values, while native raster kernels use the
region interface. Materialization/decode failures return explicit errors;
they never appear as successful blank pixels.

`img.raster_of(layer)` selects the primary content Raster, excluding masks.
Convenience `layer.r` forwarding is a later declared-interface option, not
magic property resolution based only on an ordinary element's tag. A text
layer or group stack is rendered explicitly before pixel access.

### 2.2 Equality, formatting and persistence

Under **S5.4.3**, Raster equality includes its canonical attributes and
therefore its samples. Equal descriptors with different pixels are unequal.
The Velmt and its ordinary materialized form have the same structural value;
comparison does not depend on tile partition, compressed bytes, native
address, decode cache, revision counter, or GPU texture. Different precision
or color descriptions remain different data even if a preview looks alike.
Any later nominal Raster type must preserve the same nominal descriptor on
materialization, as required by **S5.4.2v3**.

Generic formatting follows the canonical structure and may materialize
pixels. Cheap inspection uses `img.info`, rather than silently truncating
serialization. Derived channel aliases and implementation caches are not
serialized attributes. Persisting a working document must store descriptors
and actual samples, never process pointers or decoder handles. The initial
editable disk format is the supported layered PSD profile; a separate native
project/archive format can follow if PSD cannot retain the selected features.

## 3. Storage and value semantics

### 3.1 One pixel owner, several physical backends

Use a native RasterBuffer descriptor with explicit extent, channel layout,
sample format, byte capacity/strides, color description and storage owner.
The first backend is contiguous interleaved `u8` storage for gray,
gray-plus-alpha, RGB and RGBA; single-channel masks use the same owner.
The target backend uses sparse tiles with reference-counted, immutable
shared tile payloads.
Reference counting here owns native bytes through the existing allocation
substrate; it introduces neither another GC nor another allocator mechanism
(**D4.1.4v5**).

Decoded samples are authoritative for edited regions. Encoded PSD planes,
GIF patches and other original payloads are immutable source records used
for decoding or compatible export. There is no permanently synchronized
second set of independently writable R/G/B/A buffers. Planar and interleaved
layouts are backend choices; named channels are the logical interface.

Prefer ArrayNum over ArrayInt or packed native-endian color integers.
Compact component types express precision and channel operations directly.
The existing `(H,W,4)` `u8` bridge remains supported. Later high-depth
codecs must expose native samples; upconverting an already quantized RGBA8
decode cannot recover source precision.

### 3.2 Copy-on-write, snapshots and editing

**S9.1.1–S9.1.3** require observable value copying and exclusive `var`
borrows. **S9.2.2** requires read views to be snapshots and writable views
to be non-escaping borrows. Apply those rules to both the Velmt descriptor
and its native payload:

1. Binding a raster or document shares immutable storage internally.
2. A procedural edit detaches the Raster descriptor and affected storage
   before writing; a captured raster/channel snapshot retains its old value.
3. A contiguous backend may copy the full buffer on its first shared write.
   A tiled backend copies only affected tiles and its persistent tile index.
4. Channel replacement validates shape/type, snapshots any overlapping
   source, and updates the same pixel owner. Assigning a copied channel
   variable never writes through to its former raster.
5. Undo/redo retains document values; unrelated frame/layer/tile storage is
   shared. A native handle must not bypass document COW by mutating a shared
   Raster in place.

Writable channel access initially uses checked region/channel operations on
a `var` Raster. Escaping writable `.r` arrays are not part of the API.
Direct nested property writes require the existing place/store-back
machinery to detach the owner chain (**D4.4.3 / D4.4.4v4**); a setter callback
alone does not establish that guarantee.

Native decode caches may change internally only when the change is
unobservable. Pure processing cannot later reopen a source filename and
observe changed bytes. Import captures/retains an immutable source snapshot
or finishes decoding before releasing the source.

### 3.3 Color and alpha

Separate sample encoding from color identity: RGB bytes are not proof of
sRGB. Retain supported source profiles, precision and transfer information.
Color conversion is an explicit operation; unavailable transforms return
`unsupported_feature`. Do not approximate tagged CMYK/Lab as ordinary RGB.

Canonical editable RGB rasters initially use straight alpha, preserving RGB
under zero alpha. Composite/filter intermediates use an explicit common
working space and premultiplied alpha where mathematically required; RGB
is converted to linear light for the normal compositing/resampling profile.
Alpha is coverage, not a gamma-encoded color component. Output converts
back explicitly. Later blend modes may require a different declared blend
space to match their source format; linear-light processing alone is not a
promise of Photoshop appearance.

The precision target is `u8/u16/f16/f32` storage and `f32` photo-processing
intermediates, with quantization at an explicit output boundary. The first
milestone bridges color processing through the existing RGBA8 profile and
reports unsupported high-depth operations honestly. Masks use a
single-channel raster and retain their default coverage outside stored bounds.

## 4. Composition and image editing

### 4.1 Layer and stack contracts

A raster layer contains exactly one primary Raster. Other layer kinds carry
one specialized content description. Nested stacks contain ordered layers
and other stacks. Both layers and non-root stacks admit `id`, `name`,
`visible`, `opacity` in `[0,1]`, `blend`, and `composite`; a layer additionally
admits `position`, optional affine `transform`, and clipping configuration.
`blend` selects the color blending function; `composite` selects the alpha
compositing operator. Defaults are `normal` and `source_over`.

Nested stacks declare `isolation` as `isolated` or `pass_through`. Root
stacks are always isolated over the transparent backdrop and omit group
opacity/blending controls. Stack grouping does not implicitly translate its
descendants: layer coordinates remain in document space. Moving a group
updates its descendant layer transforms/positions; a retained group transform
would require an explicit future extension. Children carrying masks/effects
are ordered data with explicit roles, excluded from the layer-order sequence.

PSD opacity bytes map to normalized opacity while source bytes remain in
the export provenance. Masks retain their own offsets/bounds, enabled and
inversion flags, outside coverage and any retained feather/density settings.
Mask coverage, layer opacity and source alpha remain separate until
composition. An optional precomputed preview never becomes the editable
source of a text, vector, adjustment or smart-object layer without an
explicit rasterization operation.

### 4.2 Rendering order and supported core

For a supported raster layer: apply enabled content effects in declared
order, transform/sample into the requested document region, evaluate masks,
apply layer opacity/clipping, then composite over the accumulated backdrop.
Effect descriptors declare their coordinate space; initially they operate
on layer-local samples before the layer transform. Group isolation and
group-level masks/effects are evaluated at the group's declared boundary.

The initial compositor supports normal source-over, visibility, opacity,
translation, alpha masks and isolated nested stacks. Pass-through groups, clipping,
affine transforms and common blend modes follow as explicit profiles.
Unsupported visible operations fail strict rendering. A caller may request
a saved preview or a diagnosed approximation explicitly; a successful
strict render never silently skips an adjustment or effect.

Rendering a still image selects its direct root stack. A frame-selector API
can treat that composition as index zero without adding a frame node to the
stored value; an animation selects an actual frame and renders its root
stack. Preview selection is explicit in both cases.

### 4.3 Editing operations

The package exposes pure functions returning new values for crop, resize,
rotate/transform, color conversion, adjustments, mask construction,
composition, flattening, and layer/stack/frame insertion, removal or reordering.
Edits that preserve source pixels can instead add/modify ordinary `effect`
and `mask` descriptors. Baking effects or flattening is explicit.

Procedural operations on `var` values support repeated brush strokes,
region writes, fills, clearing, stamping and channel replacement without
unnecessary intermediate full-image copies (**S12.1.1v3**). Brushes are
bounded raster/drawing operations; an entire professional brush engine is
not required for the first editable profile. A stroke commits one history
transaction rather than one undo item per sampled dab.

`canvas` names a drawing command API over a borrowed Raster, not another
pixel store. A drawing context has explicit transform, clip and brush state
and cannot retain an escaping writable alias. Rendering UI, selections,
zoom/pan, history and tool state belong in an editor session outside the
image's exportable content. Reuse the existing editor transaction/history
mechanisms where their value ownership contracts fit.

### 4.4 Region evaluation and derived graphs

Compile the composition tree/effect descriptors into an internal acyclic
evaluation graph. The authored value remains a tree (**S9.1.5v2**); derived
graph nodes may share immutable inputs without exposing mutable aliases.
An operation declares output extent, required input region, neighborhood
halo, supported sample/color formats and edge policy. Point adjustments,
neighborhood filters and two-input composition share parameterized kernel
shapes instead of duplicating each per-format path.

Start with eager bounded evaluation and the existing image-processing
kernels. Add demand evaluation when tiles arrive: request a frame region,
pull its dependencies, and cache immutable results. Key caches by source
storage identity plus content version, parameters, working color/alpha
description, frame, region and scale. Revision numbers alone are not
globally unique identities. Edits invalidate all dependent previews and
composites; source pixels and saved history remain valid.

## 5. Format import/export

### 5.1 Shared codec integration

Reuse `lib/image.h`, `lambda/io/image_io.*`, and the existing input/format
dispatch. The document importer translates codec frames/layers into the
common model; the codec core does not depend on Lambda elements. Existing
`load`, `save`, and `image_process` array contracts remain valid. The new
package bridges arrays and Raster explicitly and reuses their kernels.

All formats use a capability manifest that distinguishes import, editable
preservation, rendering, static export and animated/layered export.
Recognizing a container is not a claim to edit or render every feature.
Initially import still PNG/JPEG and animated GIF through existing readers;
other established codecs follow their current supported subsets. Existing
still-image writer support does not establish animated GIF/WebP writing.
Animated export is a separately tested increment.

### 5.2 GIF and animated inputs

GIF images can be offset patches with palettes, transparency and disposal
rules, rather than independently displayed full frames. Normalize imports
to complete displayed frame rasters with exact timing and loop information;
wrap each displayed frame's layer in its own root stack, and keep original
patch/palette/disposal records as provenance. The existing
GIF decoder already provides composited frame pixels, so reuse that logic.
[GIF89a specification](https://www.w3.org/Graphics/GIF/spec-gif89a.txt)

Decode frames on demand with bounded caching where the decoder permits it.
Document logical independence does not imply eagerly allocating every full
frame. Tiles and checkpoints can share unchanged regions, while seeking
still accounts for disposal dependencies. Export recomputes palettes,
patches and disposal for edited frames; original source patches must not be
replayed after their displayed content changes.

### 5.3 PSD reader

Implement a bounded PSD version-1 reader and structural writer using the
[Adobe Photoshop file specification](https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/).
The reader maps source sections as follows:

| PSD data | Working representation |
|---|---|
| Header and document resources | Image extent, color/depth description, metadata and `assets` containing profiles and retained source records |
| Layer records and section dividers | One direct root stack with ordered layers and nested group stacks |
| Color and transparency channels | Source plane records feeding each layer's Raster |
| User/real masks | Separate mask Raster plus mask settings/bounds |
| Text/vector/adjustment/smart-object data | Specialized layer content, parsed where supported, otherwise preserved opaque |
| Merged image | Optional image-level source preview, separate from the editable root stack |
| Unknown resources/tagged blocks | Source records/opaque assets retaining scope, signature/key, order, payload and required source framing |

The first editable PSD profile is RGB/grayscale, 8 bits per component,
raster layers, Unicode names, offsets, normal blending, opacity, visibility,
alpha masks and isolated groups. Decode raw, PackBits RLE, ZIP and ZIP with
prediction through shared channel-decode helpers. Preserve other recognized
features without claiming editable rendering. Higher-depth and other color
modes follow with their actual native sample decoders.

Keep channel IDs, encoded compression, layer/source indices, original
flags, and mask rectangles in source records. Negative coordinates and
mask extents different from the layer are valid. RLE row tables and
compressed payloads have explicit lengths; binary data never uses text
length assumptions. Channel payload boundaries are represented by records,
not adjacent bare binaries (**S2.6.4**).

A merged preview may be absent or not represent real merged content.
Validate its source flags and data before use. An unedited source preview
can be shown explicitly, but it is never proof that Lambda reproduced the
layer composition. After an edit, invalidate the source composite and any
cached dependent result. Photoshop-specific rendering remains a separately
qualified compatibility profile.

### 5.4 PSD writer and preservation policy

PSD I/O includes writing edited layers, not just extracting them. The
initial writer emits a valid version-1 file for the editable profile:
header/resources, flattened source layer records with group markers,
channel data, mask metadata, Unicode names, and a freshly generated merged
preview. Start with raw/RLE output; ZIP output is an independent capability.
Translate foreground-first layer order, opacity, blend/composite settings
and nested stack/group structure back to PSD. Reject alpha compositing
operators the target PSD profile cannot represent rather than treating
every operator as a Photoshop blend mode.

Three explicit outcomes govern source preservation:

1. **Unchanged original:** an exact original-byte export is available only
   while the whole document remains unchanged and the captured bytes exist.
2. **Supported edited document:** write from current values and regenerate
   affected channels, section sizes and composite data. Reuse source blocks
   only when their dependencies and framing are known compatible.
3. **Unsupported preserved content:** strict editable export refuses edits
   whose consequences cannot be represented or whose opaque dependencies
   cannot be proven safe. An explicit rasterize/flatten export can produce a
   supported document, with a report of the editing information discarded.

Retaining opaque bytes alone is not lossless round-trip support. Reordering,
resizing, color conversion or modifying IDs can invalidate unknown blocks.
The export report identifies preserved, regenerated, rasterized and rejected
features. Failure is preferable to silently exporting stale previews or
damaged structures. Multiframe images require an explicit frame selection
for this PSD profile; no implicit animation-to-layers reinterpretation.

PSB is later scope. Its wider section lengths, large dimensions and the
current 32-bit Binary length bound require deliberate streaming/chunked
ownership design. Do not truncate lengths or label PSD-v1 support as PSB.

## 6. Proposed lambda.image surface

Ship the public entry module as `lmd/package/image.ls`, imported with
`import img: lambda.image`. Supporting source modules live under
`lmd/package/image/`. This explicit entry follows **D7.2.4v3**; no implicit
directory-index import or new namespace rule is needed.

| API family | Proposed operations | Effect/result |
|---|---|---|
| Inspection/construction | `info`, `capabilities`, `blank`, `from_array`, `validate` | Pure; values or explicit errors |
| Buffer access | `raster_of`, `channel`, `read_region`, `materialize` | Pure snapshots; decode failures are explicit errors |
| Composition | `render_frame`, `render_region`, `flatten`, `sample` | Pure, explicit frame/time/region/options |
| Transform/adjust | `crop`, `resize`, `transform`, `convert_color`, `adjust` | Pure new Raster/document values |
| Model editing | layer/stack/frame insertion, deletion, duplication, reordering; mask/effect and asset helpers | Pure replacement values |
| Repeated pixel editing | `write_region`, `replace_channel`, `fill`, `clear`, brush/stamp commands | `pn` operations over exclusive `var` Raster borrows |
| Byte interchange | `decode(bytes, options)`, `encode(image, options)` | Pure over captured bytes; error values |
| Resource I/O | `read(target, options)`, `write(image, target, options)` and native input/format integration | Existing explicit I/O/effect and raised-error contracts |

Pure native computations use **S7.4.1 / S7.4.5** error values. Effectful
readers follow **S7.4.2 / S7.4.5**. Package wrappers can explicitly declare
and engage raised contracts; names and convenience syntax must not conceal
filesystem access inside a pure operation. Unknown options fail under
**S17.8.1**. Optional conversion, frame selection, flattening and background
policy are explicit, particularly for destinations that cannot retain alpha
or multiple layers/frames.

Existing `input(path, 'psd')` gains structural PSD parsing. Its initial
static-data path returns ordinary Input-owned composition/raster elements
with materialized typed samples and retained source records. Runtime adoption
constructs Raster Velmts by copying samples or acquiring a supported owner
lease. The package's `read` can instead capture bytes through existing I/O
and pass them to runtime `decode`, retaining an immutable source snapshot
for deferred decoding. Both routes produce the same logical image model.
Do not place an untraced GC Velmt inside an Input-owned document. Package
decoding and `input` share the same PSD byte parser and document mapping;
only the allocation/materialization destination differs.

## 7. Prior art and adopted ideas

These are design references, not new required dependencies or promises of
API compatibility. No upstream code import or modification is authorized by
this proposal. Research checked against the linked primary documentation on
2026-10-16.

| System | Relevant existing design | Adaptation for Lambda |
|---|---|---|
| [GEGL](https://gegl.org/features.html) | Sparse/linear buffers, tiled mipmaps, subregion processing and subgraph caches | Raster storage abstraction, region evaluation, preview scale and bounded derived caches |
| [GEGL operations](https://gegl.org/operation-api.html) | Separate point filters, area filters and composers; area operations describe required neighborhoods | A small kernel taxonomy with explicit input regions/halos, rather than separate hand-written paths per codec/type |
| [babl](https://www.gegl.org/babl/) | Pixel formats distinguish sample types, models, spaces and component ordering | Explicit color/sample/alpha descriptors and conversion boundaries; do not equate RGBA8 with sRGB |
| [libvips](https://www.libvips.org/API/8.16/How-it-works.html) | Partial images generate rectangular regions on demand; processing produces new images | Pure operation descriptors and demand evaluation; preserve Lambda snapshot semantics instead of exposing mutable host graphs |
| [Krita](https://docs.krita.org/en/user_manual/layers_and_masks.html) | Layers and masks carry non-destructive filters/transforms, including effects on groups | Ordinary editable mask/effect nodes, explicit bake/flatten, and editor state separate from image content |
| [OpenImageIO ImageBuf](https://openimageio.readthedocs.io/en/latest/imagebuf.html) and [ImageCache](https://openimageio.readthedocs.io/en/latest/imagecache.html) | Distinguish local, application-owned and cache-backed pixels; cache requested tiles within resource limits | State storage ownership explicitly; bounded tile caches and pinned region borrows, without assuming every raster has a permanent flat pointer |
| [OpenRaster](https://www.openraster.org/baseline/layer-stack-spec.html) | Interchange separates layer order, visibility, blending/compositing and group isolation | Adopt foreground-first layer order and nested stacks for groups; extend the image model with timed frames and image-scoped assets |
| [psd-tools](https://psd-tools.readthedocs.io/en/latest/usage.html) | Layer/group inspection, source pixels versus composition, PSD saving and regenerated previews; documented rendering limits | Reference fixtures and API distinctions; track preview invalidation and qualify supported PSD rendering separately from parsing |

### 7.1 GEGL lessons worth carrying into the core

GEGL's most useful boundary is between a buffer and the operations that
produce or consume its regions. Adopt that boundary before implementing
tiles: a kernel requests an extent and format, not a global pixel pointer.
This lets the same compositor process a small dense image and later a tiled
photo. GEGL's aligned copies can share tiles with copy-on-write; Lambda uses
that mechanism to implement its existing immutable snapshot semantics.
[Gegl.Buffer.copy](https://developer.gimp.org/api/gegl/method.Buffer.copy.html)

Make out-of-extent sampling explicit. GEGL calls this the abyss policy and
provides zero, clamp and wrapping behavior among its choices. Lambda should
start with transparent/zero extension for composition and declared clamp
behavior for filters; masks retain their own outside-coverage default. A
border rule is part of an operation's value and cache key, not a global knob.
[GEGL abyss policy](https://gegl.org/abyss_policy.html)

Do not copy GEGL's object framework, runtime plugin surface or complete
operation catalogue. Lambda already owns values, modules, effects and GC.
Its composition tree is the authored representation; any native operation
graph is a derived, reproducible implementation detail.

### 7.2 Additional references and limits

libvips also distinguishes sequential from random-access loading. Follow
that capability distinction: region-based processing cannot make a ZIP
stream or GIF dependency chain randomly seekable for free. A decoder may
need bounded full decode or checkpoints; report the actual strategy rather
than promising constant memory from a lazy API alone.
[libvips file loading](https://www.libvips.org/API/8.16/How-it-opens-files.html)

Krita is a useful animation/editor reference as well as a layer reference,
but its documented in-memory frame workflow is not the proposed storage
contract. Lambda separates displayed frame values from their physical
tile sharing and cache lifetime. [Krita animation](https://docs.krita.org/en/user_manual/animation.html)

OpenRaster's `image → stack → (layer | stack)*` structure maps directly to
Lambda's still-image composition, including foreground-first order and
group isolation. An optional future ORA adapter translates layer `src`
members to captured assets/Rasters and maps attribute/operator spellings.
It retains resolution and source bounds, and exports the supported baseline
profile explicitly. Animation, image-scoped assets and sample precision are
Lambda extensions, not claims about OpenRaster's baseline. psd-tools is a
development comparison tool; its results, especially advanced effects, are
not an oracle for Photoshop conformance.

## Appendix A. Integration and ownership notes

Implementation notes are collected here so the design body stays about
behavior rather than a file-by-file implementation plan.

- Context-free RasterBuffer, region processing, compositing and PSD byte
  parsing/encoding belong below Lambda in the existing image/library layer.
  Resource acquisition/publication stays in `lambda/io`; document translation
  uses MarkBuilder/MarkReader; runtime bridges own Velmt and ArrayNum handling
  (**D7.1.1–D7.1.3**). Search/promote existing helpers rather than copying them.
- The native Raster Velmt traces every retained Item and finalizes its native
  storage once. Immutable native storage/source snapshots have explicit
  owners. C/C++ bridges use `RootFrame`/`Rooted`, refreshing borrowed pointers
  after safepoints (**D5.3.3**). No conservative native-stack scanning.
- Static input elements and byte payloads remain Input-owned
  (**D4.1.1v2 / D4.1.3**). Runtime adoption copies or acquires an explicit
  supported owner lease; it never relies on a bare pointer into an Input
  that may be released. Pool resource cleanup uses **D4.2.6** where appropriate.
- Radiant receives native pixel snapshots or leases through existing image
  upload contracts, never a retained GC pointer (**D4.5.2**). A GPU texture is
  a derived rendering resource, not the authoritative editing store.
- Reuse `lib/endian.h`, checked arithmetic, compression and image row kernels.
  All binary sections, dimensions, strides, expanded sizes and frame counts
  are validated before use. Apply per-operation source, decoded, scratch,
  metadata and cache budgets; compressed size is not an expanded-memory bound.
- Pixel kernels are context-free and share declared sample/alpha semantics.
  Worker jobs retain stable native snapshots and publish completed values
  through the existing scheduler, not naked pointers to GC arrays.
- Build declarations go through `build_lambda_config.json`; generated Lua
  and vendored sources are not edited. Any test/capture scratch stays under
  `./temp/`. Performance evidence uses release binaries; worktree builds
  follow Developer Guide §7.
- Existing anchors: `lib/image.h`, `lib/image_core.cpp`, `lib/image_gif.c`,
  `lib/image_process.h`, `lib/image_process.cpp`, `lambda/io/image_io.*`,
  `lambda/runtime/image_process.*`, `lambda/runtime/lambda-vector.cpp`,
  `lambda/runtime/vmap.cpp`, `lambda/input/input.cpp`, and
  `lambda/io/mark_builder.*`. Exact implementation placement follows provider
  ownership; filenames listed here are starting points, not duplicate APIs.

## Appendix B. Delivery milestones and acceptance

These are finite delivery boundaries. A full implementation plan and
progress record should live in `vibe/impl/` when implementation is requested.

| Milestone | Deliverable | Acceptance behavior |
|---|---|---|
| 1. Working Raster | Public package entry, contiguous `u8` Raster Velmt including gray masks and RGBA8 images, array/channel snapshots, region access and value-safe writes | PNG/JPEG import produces a direct root stack with a raster layer; captured raster/channel/document values survive edits and forced GC; materialized and virtual Raster equality agree |
| 2. Editable composition | Foreground-first stacks, nested group stacks, asset descriptors, normal compositing, placement, visibility, opacity, alpha masks, crop/resize, fill/stamp, history transactions | Layer movement/masking yields specified pixels; the top child renders uppermost; one stroke undoes/redoes as one edit; failures do not publish partially edited values |
| 3. Animation | Frame-owned root stacks, GIF normalization, exact durations/plays, frame editing, bounded frame access, explicit animated export profile | Offset patches and each disposal mode render correctly; editing one frame leaves others unchanged; exported animation preserves specified appearance and timing within declared palette limits |
| 4. PSD I/O | RGB/grayscale 8-bit layered reader and writer, groups/masks, four decode compression modes, regenerated preview and export report | Import → edit → export → reimport preserves supported hierarchy, names, geometry and pixels; an independent reader opens output; missing composites and unsupported edits produce defined outcomes |
| 5. Photo scale/precision | Tiled COW, region-driven effects, native high-depth adapters, explicit color conversion, broader transforms/blending | Shared snapshots copy affected tiles; tiled/eager reference renders agree within declared numerical tolerances; region/halo boundaries have no seams and source precision survives supported round trips |

Qualification uses small deterministic pixel/alpha fixtures plus representative
real files: channel snapshot ownership; padded/strided arrays; negative layer
offsets; independent mask bounds/defaults; group isolation; timing boundaries;
GIF palettes/disposal; PSD truncation and Unicode; preview invalidation; and
unsupported-feature export. Check source preservation separately from visual
comparison, and image export separately from layered editability.

Use focused native/runtime/package checks during each increment. At engine
milestone boundaries run `make test-lambda-baseline`; run the Radiant baseline
only when its integration changes. Every added golden-driven `.ls` fixture
gets its `.txt` golden. Documentation-only proposal changes need document
checks, not engine builds. Add independent PSD-reader comparisons to the
writer gate; Photoshop appearance claims need separately recorded reference
captures and tolerances.

Keep professional brush behavior, complete Photoshop effects/adjustments,
smart-object editing, all color models, PSB, disk-backed tile spill, arbitrary
GPU graphs and a dedicated native project format in later scope. Record
partial support by behavior rather than treating that standards tail as a
prerequisite to useful image editing.

## Appendix C. Superseded schema choices

The schema revision accepted on 2026-10-16 replaces these earlier choices;
the current contracts are in §§1 and 4.

- ~~`image → frame → layer → raster`; frames contain layers directly and
  there is no `stack` node.~~ Still images now contain a root stack directly;
  animated frames each contain a root stack.
- ~~A group is a `<layer kind: 'group'>` whose children are layers.~~ Nested
  stacks now represent groups.
- ~~Layer order is background to foreground; index zero is the bottom
  layer.~~ Foreground-first order now matches OpenRaster and the editor list.
- ~~`resources` names the shared image-level container.~~ The user selected
  `assets`; PSD's original Image Resources Section retains its format name
  when discussing on-disk PSD data.


## Implementation checkpoint — 2026-10-20

The working composition profile now evaluates editable primary text/vector
content and ordered shadow/glow/color-overlay styles, alongside the earlier
raster/blend/affine/effect profile. Embedded fonts remain image-scoped captured
assets; pure rendering neither reads font files nor substitutes system fonts
(**S12.1.1v3**). Empty text uses the normalized zero-child form (**S2.6.2**).
All paths still publish immutable owned Raster results (**S9.1.2 / S9.2.2**).
Native sample comparison preserves the element relation (**S5.4.3**) without
materializing pixel projections for two native u8 rasters.

The rich project format retains authored content, font bytes and editor coverage
channels. The strict PSD profile refuses these unsupported editable extensions;
no new native PSD text/vector/style preservation is implied. Text shaping/rich
runs, additional style families, high depth/ICC and larger-scene/background-job
qualification remain later profiles. The actual supported surface and acceptance
results are in [Lambda Image](../doc/Lambda_Image.md),
[image implementation](impl/Lambda_Impl_Image.md), and
[Photos implementation](impl/Lambda_Impl_Photos.md).
