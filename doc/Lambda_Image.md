# Lambda Image Package

**Verified against tree:** 2026-10-20. This describes the bounded native
`u8` editing profile and shared Photos extensions. Design and remaining milestones:
[proposal](../vibe/Lambda_Pkg_Image.md),
[implementation record](../vibe/impl/Lambda_Impl_Image.md).

`import img: lambda.image` provides raster values, layered composition,
animation frames, PSD interchange and image editing. The package lives under
the bundled package root (**D7.2.4v3**).

```lambda
import img: lambda.image

pn main() {
    let photo = img.read("photo.png")^
    let foreground = img.blank({width: 32, height: 32,
        color: [1.0, 0.0, 0.0, 0.5]})^
    let composition = img.insert(img.composition(photo), 0,
        img.layer(foreground, {name: "Overlay", position: [10, 20]}))^
    let edited = img.with_children(photo, [img.assets(photo), composition])^
    img.write(edited, "edited.psd", {format: 'psd'})^
}
```

## Document values

A still image contains one `stack`; an animation contains timed `frame`
elements, each owning one stack. Stacks contain layers or nested stacks.
Their first child renders uppermost. Raster layers own a Raster and optional
gray alpha mask. Image-scoped `assets` retain source bytes and any extracted
ICC profile; `preview` is derived content. Empty PSD layers retain metadata
as `kind: empty`.

Raster attributes are `width`, `height`, `sample_type`, `channels`, `color`,
`alpha_mode` and `pixels`. Native storage is immutable `u8` with dense inputs
and sparse copy-on-write tiles for region/paint edits, with
one through four channels: gray, gray/alpha, RGB or RGBA. The canonical
pixel snapshot has shape `[height,width,channels]`; `r`, `g`, `b`, `a` are
declared, non-enumerable channel projections (**D7.4.4 / D7.4.5v2**).
Absent RGB projections on gray storage return null; `channel` checks names.

Construction, replacement, channel reads and pixel reads preserve value
snapshots (**S9.1.2 / S9.2.2**). Editing a returned sample array changes that
array. Use the checked replacement or procedural APIs to publish pixels
back into a Raster. Region and paint writes share untouched backing and detach
only edited tiles. Shared native byte storage is GC-accounted once until its
last owner releases it (**D4.1.4v5**).

## API

All computational APIs are pure. Checked functions declare raised contracts
and use postfix `^`; callers can catch failures with `expr ^ { handler }`.
`read` and `write` are procedures because they perform I/O. `storage_info` is a
procedure because allocation/refcount observations can change after GC
(**S12.1.1v3**).
Unknown options fail (**S17.8.1**); arbitrary layer properties are document
data rather than codec options.

`./lambda.exe view picture.psd` opens a supported PSD as an image document.
The `lambda.doc.image` adapter renders the current layers and masks through
`lambda.image` and embeds an in-memory PNG; it does not depend on the file's
merged preview. Unsupported composition/color features retain the package's
strict error behavior. The same document route supports `layout` and `render`.

| Functions | Behavior |
|---|---|
| `blank(options)`, `from_array(samples, options={})` | Construct independent Raster storage. Blank dimensions are positive integers; normalized fill components are in `[0,1]`. Arrays must be `u8` of rank two or three. |
| `channel(raster,name)`, `read_region(raster,rect,options={})` | Dense sample snapshots. Rectangles are integral, half-open `[left,top,right,bottom]` within the Raster. Optional `channel` selects a plane. |
| `materialize(raster)`, `adopt(node)` | Materialize the canonical element; recursively adopt materialized image trees into runtime Raster owners. |
| `layer`, `stack`, `frame`, `document`, `of_raster`, `mask` | Construct composition values; the last optional map supplies attributes. |
| `frames`, `layers`, `composition`, `assets`, `raster_of`, `info` | Inspect the document. Frame indices are zero-based. |
| `with_properties(node,map)`, `with_children(node,array)` | Preserve the element tag/attributes while replacing specified data. Image previews are invalidated. |
| `node_at(node,path)`, `replace_at(node,path,value)` | Navigate or replace by actual content indices, including an image's `assets` child. |
| `insert(node,index,child)`, `remove`, `duplicate`, `reorder` | Edit layer, group, frame or asset child lists. Reorder destinations index the final list. |
| `to_animation(image,duration)` | Wrap a still root stack in a timed frame; retain existing animations. |
| `with_region(raster,rect,samples,options={})`, `with_channel(raster,name,samples)` | Validate sample type and shape, then return a replacement. |
| `write_region(var raster,rect,samples,options={})`, `replace_channel(var raster,name,samples)` | Publish a checked replacement through an exclusive borrow (**S9.1.3**). Failure leaves the caller's value unchanged. |
| `fill(raster,color)`, `clear(raster)`, `stamp(raster,brush,position,options={})` | Return filled, cleared or source-over stamped storage. Stamp accepts normalized `opacity`. |
| `crop(raster,rect)`, `resize(raster,width,height)`, `adjust(raster,options)` | Return transformed samples using the shared numeric/image kernels. |
| `transform(raster,options)`, `rotate(raster,quarter_turns)`, `flip(raster,horizontal=true,vertical=false)` | Explicitly bake geometry; image-process adjustments/transforms require RGBA8. |
| `render_frame(image,options={})`, `render_region(image,rect,options={})`, `sample(raster,x,y)` | Render linear-light source-over composition with the supported blends, or read a sample. Region rendering limits composite/group buffers to the requested canvas rectangle. Enabled filters currently evaluate their full source extent for neighborhood correctness. |
| `selection(width,height,options)`, `wand(raster,point,options={})` | Gray coverage from rectangle, ellipse, even-odd polygon or all; color selection with normalized tolerance and contiguous/global matching. Shape options use `start`/`end` or polygon `points`. |
| `combine_coverage(a,b,mode)`, `invert_coverage(a)`, `feather_coverage(a,radius)`, `coverage_bounds(a)` | Replace/add/subtract/intersect, finite inversion, bounded box-filter feather and nonzero half-open bounds (`null` for empty). |
| `paint_stroke(raster,options)`, `paint_fill(raster,options)`, `gradient(raster,options)` | Gray/RGBA painting with color, opacity and optional document-space selection/origin. Strokes accept points, size/hardness/spacing/flow and paint/erase mode; gradients accept linear/radial endpoints and endpoint colors. |
| `reframe(raster,rect,options={})`, `mix_samples(a,b,options={})`, `extract_samples(raster,options={})` | Extend/crop samples with optional fill; interpolate through coverage; retain selected alpha. Unchanged snapshots remain intact. |
| `coverage_overlay(coverage)` | Produce a separate translucent blue RGBA preview; overlays are not authored image content. |
| `pn storage_info(raster)` | Observe logical/index bytes, resident/shared backing references and currently private payload bytes. Shared references are counted as references, not summed as unique resident bytes. |
| `validate(image)` | Check all frame compositions using this render profile. |
| `decode(binary,options={})`, `encode(image,options={})`, `encode_report(image,options={})` | Interchange captured bytes. Reports identify regenerated/rasterized/omitted content. |
| `read(target,options={})`, `write(image,target,options={})` | Read binary input or write encoded bytes through existing target I/O. Specify output `format`; the default is PNG. |
| `capabilities()` | Query actual codec availability and declared editing/animation profiles. |
| `checked_options(options,allowed)` | Shared validation of option maps and allowed field names (**S17.8.1**). |

### Editable artwork and styles

`text_layer(text,options,layer_options={})` and
`vector_layer(options,layer_options={})` construct primary authored content.
Both require integral local `width`/`height` within the 16-million-pixel limit.
The shared compositor evaluates that content for preview, region rendering and
flat export; a cached raster never replaces its editable source (**S9.1.2**).
Layer masks, effects, opacity, blends and affine transforms apply normally.

Text options include `font` (an image-scoped asset ID), `size` (1–1024 pixels),
`line_height` (0.5–8 times the size), `align` (`left`, `center`, `right`) and
normalized RGBA `fill`. Embed the captured font as
`<assets <font id:"heading",data:font_bytes>>`; rendering performs no implicit
file/network acquisition or system-font substitution (**S12.1.1v3**). Missing
fonts, missing glyphs and unsupported outlines raise errors. This initial profile
supports explicit newlines and horizontal glyph placement with kerning, without
automatic wrapping, rich runs, shaping, bidirectional layout or color emoji.
U+0300–U+036F combining diacritics and contextual/RTL scripts are refused.
Text is capped at 16,384 UTF-8 bytes and 262,144 outline commands.

Vector options include `shape` (`rectangle`, `ellipse`, `path`), rectangle
`radius`, RGBA `fill`/`stroke`, and `stroke_width`. Paths are arrays such as
`[["M",0,0],["L",40,0],["Q",60,20,40,40],["Z"]]`; supported absolute operators
are M/L/Q/C/Z, with even-odd filling, round joins/caps and at most 4096 commands.
Local dimensions clip the artwork, including strokes extending outside its box.
Optional `gradient` supplies a linear `start`/`end`, `color`/`end_color` pair.

Layer children may contain ordered `<style>` descriptions with `kind` of
`shadow`, `glow` or `overlay`, normalized RGBA `color`, `opacity`, and `enabled`.
Shadow/glow use a bounded integer box-blur `radius` (0–128); shadow also accepts
integral `dx`/`dy` (±256). `outset` (0–512) explicitly extends and clips the local
filter region on each side. Overlay preserves source alpha and interpolates RGB
in linear light. Styles evaluate after adjustments and source masks, before the
retained affine. Style order is significant: each consumes the preceding result.
Group styles require explicit rasterization. The editor chooses a sufficient
outset by default and includes it in target/rasterization bounds.

`.photos` projects preserve artwork, embedded fonts and enabled/disabled style
parameters. This PSD profile refuses native text/vector/style export. Explicit
layer rasterization or document flattening makes the appearance a pixel source;
neither operation promises native Photoshop editability.

`render_frame` options are `frame`, `width`, `height` and `rect`; dimensions describe
the output viewport. Decode options are `page_index` and
`normalize_orientation`. Encode options are `format`, `frame`, `quality`
and `lossless`, subject to the selected writer's profile.

## Sessions and history

`import edits: lambda.image.edit` exposes `session(image)`,
`record(session,replacement,group=null)`, `undo`, `redo`, `can_undo` and
`can_redo`. It reuses the editor's snapshot transaction history. Give
adjacent stroke updates the same non-null group to undo them together.
History and tool state stay outside the exportable image.

`original_bytes(session)` returns captured bytes only while the current
document equals the original snapshot. A changed document fails this check;
retaining its source asset alone does not authorize byte passthrough.

## Qualified profiles and limits

- Composition supports straight-alpha sRGB, integral placement, visibility,
  normalized opacity, isolated/pass-through groups and one gray alpha mask per node,
  including independent mask bounds/default coverage and inversion. It
  composites in linear light with the eight blend modes listed below.
  Clipping chains preserve their base alpha; retained layer affine transforms move samples and masks together.
  Ordered enabled `effect` children apply extent-preserving image-process adjustments and filters.
  Unsupported blend names, singular transforms and geometry options in effects fail explicitly.
  Pass-through group effects require explicit isolation, as do pass-through clipping bases.
  Specialized content requires another profile
  or explicit baking.
- Shared codecs import their supported still representations as RGBA8;
  PNG/JPEG and GIF are covered by package fixtures. GIF preserves rational
  durations, encoded zero delays and total play counts (`0` means infinite).
  Export writes full display frames with local exact palettes of at most
  255 opaque colors plus transparency. Partial alpha and times not exactly
  representable in centiseconds fail; there is no implicit quantizer.
- PSD version 1 supports RGB/grayscale, eight-bit channels, Unicode names,
  layers/groups and user raster masks. Reading supports raw, PackBits RLE,
  ZIP and ZIP prediction; writing emits RGB8 raw planes and regenerates layer IDs
  and the merged composite. Opacity/default coverage round to PSD bytes.
  Inverted mask writing and group mask/effect writing are not yet qualified.
  An animation needs explicit `frame` selection for PSD/still output.
- `input(path,'psd')` and automatic PSD detection use the same byte parser
  and produce Input-owned materialized Raster elements. Call `adopt` before
  runtime rendering/editing. No GC Velmt is embedded in static Input data
  (**D4.1.2 / D5.3.3**).
- ICC-tagged source samples are marked `unknown` and retained with original
  bytes. This profile does not perform ICC conversion. Strict composition
  refuses unknown working color identities and preserved unsupported visual
  features. Photoshop effects, text/smart-object editing, PSB, 16/32-bit
  samples, sparse tiled storage and GPU/demand evaluation remain outstanding.

Individual Rasters/render outputs are bounded to 16 million pixels and
64 MiB. PSD source/output is bounded to 128 MiB, its decoded storage to
256 MiB, layer count to 4096, and composition depth to 64. GIF import/export
also uses the shared codec budgets. These are correctness limits, not
photo-scale performance claims.

## Retained composition and projects

Layers accept `transform:[a,b,c,d,tx,ty]` in source-local coordinates, followed by
integral `position`. Sampling interpolates premultiplied encoded sRGB; composition
uses linear-light RGB. Blend names are `normal`, `multiply`, `screen`, `overlay`,
`darken`, `lighten`, `difference`, `exclusion`. This is an explicit working-space
choice, not a claim of Photoshop appearance compatibility. `clipping:true` joins
an immediately lower base sibling's clipping chain. The chain retains base alpha,
including partial coverage. Stacks accept `isolation:'isolated'` or `'pass_through'`;
pass-through group opacity/masks interpolate the before/after backdrop.

An `<effect kind:'adjust',enabled:true,options:{...}>` retains shared image-process
color/filter parameters. Effects apply in child order before the layer mask and
affine transform. Geometry options are rejected. Gray/tiled inputs materialize
only for a filter evaluation. Group/filter/clipping buffers share a bounded
256-MiB composition scratch admission limit.

`histogram(raster)` returns one 256-bin count array per native channel. Transparent
samples contribute to alpha bins and are excluded from color bins. `to_bytes` and
`from_bytes(bytes,{width,height,channels:4,color_space:'srgb'})` copy raw interleaved
samples and preserve hidden RGB and gray coverage (**S9.1.2**).

`lambda.image.project.encode/decode` serializes a version-1 `.photos` Mark data
container. `read/write` acquire and atomically publish it (**S12.1.1v3**). Raw binary
samples preserve shape/channel/working-space identity; tree attributes preserve
IDs, locks, masks, effects, transforms and source assets. Loading parses data,
never executes code. Encoded text and decoded raster payloads are capped at
128 MiB with a conservative preflight before packing; nesting is limited to 64.
The initial format uses hexadecimal Mark binary data and has no compression or
content deduplication. PSD export explicitly refuses retained transforms,
non-normal blends and pass-through/clipping metadata it cannot retain. Retained
effects, including disabled ones, require explicit rasterization or a rich project.

## Bounded retouch kernels

`retouch(raster,options)` reuses brush points, size/hardness/spacing/flow/opacity,
selection/origin and immutable tile replacement. `kind` selects `clone`, `heal`,
`dodge`, `burn`, `sponge`, `blur` or `sharpen`. Targets and sampled/context sources
are RGBA8. Clone/heal require `sample` and `sample_offset`; optional `context` and
`context_offset` supply the destination neighborhood. Each stroke reads frozen
inputs, so earlier dabs cannot feed back into sampling (**S9.1.2**).

`strength` is in `[0,1]`; `radius` is an integer in `[1,32]`. Healing color-matches
source texture with alpha-weighted linear-light neighborhood means; it is not a
Poisson solver. Local dodge/burn apply up to one stop, sponge reduces saturation,
and blur/sharpen use the bounded neighborhood mean. Tonal/neighborhood tools
preserve destination alpha. Clone/heal source-over their sampled alpha. Stroke
work and neighborhood work are bounded at admission; unsupported kinds/options
fail without publishing a replacement (**S9.1.3**).
