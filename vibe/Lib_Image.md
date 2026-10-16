# Common Image I/O Library

**Date:** 2026-10-15

**Status:** implementation authorized by the user on 2026-10-15. The common native API, Lambda/Radiant migration, eight readers, six writers, and thumbnail planner are implemented. The 2026-10-16 decoder-only AVIF profile retires AVIF encoding to reduce native binary size; validation and supported subsets are recorded in [the implementation record](impl/Lib_Image_Impl.md).

**Confirmed source policy (2026-10-15):** the user directs that official codec source be imported under `./lib/image/` and modified directly, following the local-source approach of `lib/re2/`. Separate vendor patch files are not required for this image-library work.

**Confirmed build policy (2026-10-15):** image sources, generated feature headers, CPU/platform selection, and linking are declared only in `build_lambda_config.json`. The ordinary Lambda build compiles the explicit image I/O source subset into one static archive. All upstream CMake and related build configurations are removed; no separate codec build system or installed-codec prefix is used (**D7.1.1**, **D7.1.6–D7.1.7v6**).

**Scope:** one shared raster image library for Lambda, Radiant, bitmap fonts, and document import/export; retain PNG, JPEG, GIF, and WebP, and add AVIF, BMP, TIFF, and ICO.

**Spec linkage:** D7.1.1–D7.1.3 (layering and resource I/O), D7.1.6–D7.1.7v6 (build profiles), D4.1.3–D4.1.4v5 and D4.2.6 (allocation and ownership), D5.3.3 (native rooting), S7.4.1–S7.4.5 (failure channels), S17.8.1 (option validation). See the [formal design](../doc/Lambda_Formal_Design.md), [formal semantics](../doc/Lambda_Formal_Semantics.md), and [documentation convention](../doc/Doc_Convention.md).

This records the approved image-library scope within the existing formal rulings. It does not change the language semantics or amend the formal specs.

## 1. Goals

Develop a consolidated image I/O subsystem with:

1. Uniform APIs for format detection, metadata inspection, decoding, encoding, and image/frame release, shared by file and memory callers.
2. Structured, consistent errors across formats and operations, with no dependency on parsing log text.
3. Continued support for existing formats plus AVIF, BMP, TIFF, and ICO, with explicit read/write capabilities and supported subsets.
4. Import official upstream C/C++ codec source directly under `./lib/image/` and modify it there for Lambda. Trim and consolidate allocation, byte access, pixel conversion, metadata handling, resizing, and diagnostics. Preserve upstream codec algorithms and track local changes through ordinary Git history; new Lambda code supplies the common API and integration.
5. A thumbnail loading mode that accepts a requested pixel size and avoids unnecessary decoding, allocation, and animation work where the format permits it.

The initial pixel interchange format is RGBA8. Scientific TIFF preservation, HDR processing, general image editing, SVG rasterization, and video decoding are separate concerns. Existing SVG rendering continues through the vector subsystem; the raster capability table must not imply that SVG is unsupported by Radiant.

## 2. Architecture and ownership

### 2.1 One codec core, one resource adapter

Use a context-free codec core in `lib/image/`, with the public native interface in `lib/image.h`. Put file/resource acquisition and output publication in `lambda/io/image_io.*`. Both expose the same image, option, status, and diagnostic types; the I/O adapter only acquires a source or sink and invokes the core.

Import the codec implementations under `lib/image/<library>/`, for example `lib/image/libpng/`, `lib/image/libavif/`, and `lib/image/libtiff/`. These checked-in sources are modified directly and built with Lambda. The shared core in `lib/image/` calls the imported implementations and supplies the common utilities they reuse.

```text
Lambda array adapters       Radiant surfaces / document export
          |                          |
          +---- lambda/io/image_io ---+   file/resource acquisition
                           |
Bitmap font decoder -------+------ lib/image   byte/stream codecs
                                      |
                   libpng / libjpeg-turbo / giflib / libwebp
                         libavif / libtiff / libnsbmp
```

This follows **D7.1.1** and **D7.1.2v2**: raw context-free mechanisms belong in `lib`; resource I/O belongs in `lambda-io`; Radiant consumes acquired sources. Keeping byte decoding below Lambda also lets `lib/font/font_rasterize_tvg.cpp` decode embedded PNG glyphs without an upward dependency. The project gets one implementation even though resource ownership requires two entry layers.

The codec core has no dependency on `Item`, `Input`, the GC, `ImageSurface`, DOM, GPU textures, URLs, or event loops. It reads a borrowed memory span or a caller-supplied reader and writes to a caller-supplied sink. HTTP/data-URI acquisition stays in the existing resource infrastructure; codecs never fetch.

### 2.2 Shared responsibilities

| Common component | Responsibility |
|---|---|
| Format registry | Content detection, format ID, MIME type, extension aliases, and compiled capabilities |
| Source/sink adapters | Bounded memory access, exact reads/writes, seeking or `read_at`, cancellation, and short-I/O reporting |
| Buffer/layout utilities | Checked dimensions, stride, allocation sizes, budgets, and owned-buffer disposal |
| Pixel utilities | Channel order, alpha conversion, grayscale expansion, orientation, and shared resampling |
| Metadata model | Encoded/display dimensions, orientation, color description, frame/page information, and resolution candidates |
| Diagnostic translation | Stable image status codes, operation context, vendor details, warnings, and fault classification |
| Codec adapter | Only format-specific parsing, codec configuration, native decode/encode calls, and native error translation |

Use a static registry rather than runtime codec plugins. Public headers expose Lambda types, never vendor handles or structs. Query read, write, animation, page selection, and thumbnail capabilities separately; registration of a decoder does not imply an encoder exists.

The implementations behind this interface are adapted upstream sources. Adapters alone are not the intended endpoint: source changes may replace duplicated utility paths with shared Lambda helpers, standardize diagnostic and allocation hooks, expose bounded thumbnail decoding, and remove features outside the supported subset. Retain the proven format and compression algorithms; the common API, integration, and extracted utilities are the main new code.

Content determines the input format. Extensions and supplied MIME types are hints, not authority. Detection must be bounded and able to request more header bytes: AVIF requires parsing bounded ISO-BMFF boxes/brands rather than assuming every `ftyp` file is AVIF. Recognized but disabled formats return a specific capability error. Corruption must not trigger an unrestricted sequence of unrelated decoders.

## 3. Common native API

### 3.1 Operations

The common operations are declared in [`lib/image.h`](../lib/image.h). The abbreviated signatures below describe that API; the header is authoritative for C types and const qualifiers. `ImageContext` carries operation limits, cancellation, and a diagnostic callback; `ImageSource` and `ImageSink` abstract byte access.

```c
ImageStatus image_probe(ImageContext* context, ImageSource* source,
    ImageInfo* info, ImageError* error);

ImageStatus image_decode(ImageContext* context, ImageSource* source,
    const ImageDecodeOptions* options, ImageDecoded* result, ImageError* error);

ImageStatus image_encode(ImageContext* context, const ImagePixelView* pixels,
    const ImageEncodeOptions* options, ImageSink* sink, ImageError* error);

ImageStatus image_decoder_open(ImageContext* context, ImageSource* source,
    const ImageDecodeOptions* options, ImageDecoder** decoder, ImageError* error);
ImageStatus image_decoder_read_frame(ImageDecoder* decoder, uint32_t frame_index,
    ImageDecoded* result, ImageError* error);
void image_decoder_close(ImageDecoder* decoder);

void image_decoded_dispose(ImageDecoded* image);
```

Provide memory-source, bounded subsource, file-reader, and memory-sink adapters. Path helpers such as `image_io_probe_path`, `image_io_decode_path`, and `image_io_save_path` live in `lambda/io/`, call these operations, and return the same status/error model. No separate codec implementation is allowed for file versus memory input, or for full versus thumbnail loading.

Declare source seek/random-access capabilities explicitly. A codec requiring them must use an admitted bounded spool or return a capability error for a sequential source; it must not silently buffer an unbounded stream. Probe and decode use the same acquired source identity, rather than reopening a path between validation and decoding. Readers must handle large metadata before image headers without a fixed small-header assumption.

`image_probe` reads metadata without allocating decoded pixels. It is not a promise that the complete payload is valid or that every metadata query is constant-time. Frame/page counts may be unknown until a bounded scan; expose a known/unknown flag rather than making zero mean both unknown and empty.

### 3.2 Image and option contracts

| Type | Contents |
|---|---|
| `ImageInfo` | Format; encoded width/height; oriented display width/height; source bit depth; alpha presence; orientation/transform; color description; frame/page counts with validity flags |
| `ImagePixelView` | Borrowed pixels, byte capacity, width/height, byte stride, channel count, and premultiplied/straight alpha |
| `ImageDecoded` | Owned pixels and metadata; actual output dimensions/stride; selected representation; GIF timing/loops; thumbnail strategy and fallback flag |
| `ImageDecodeOptions` | Full/thumbnail/at-least mode; target dimensions; orientation policy; page/frame selection; whether full-decode fallback is allowed |
| `ImageEncodeOptions` | Explicit format; quality/lossless options; JPEG/BMP alpha background where needed |
| `ImageLimits` | Encoded bytes, source dimensions/pixels, output bytes, scratch budget, metadata bytes, frames/pages, and worker-thread ceiling |

Decoded pixels are top-down, byte-addressed RGBA8 with straight alpha and an explicit stride. The default owned output is tightly packed; pixel views may have padding. Validate the final addressable row span against the supplied capacity, not just `width * height * channels`. No caller should infer channel order from a host-endian packed integer.

Keep color identity explicit. RGBA8 describes storage, not a guarantee of sRGB conversion. Initially preserve supported SDR RGB samples and the bounded source color description; do not silently label ICC-tagged pixels sRGB. A requested conversion without an available transform returns `UNSUPPORTED_FEATURE`. AVIF YUV-to-RGB uses the declared matrix/range; high-bit-depth SDR quantization must be explicit. HDR transfer functions require an explicit supported tone-map policy or return `UNSUPPORTED_FEATURE`, rather than being clipped into an apparently successful SDR image.

New thumbnail calls normalize display orientation by default and fit the oriented dimensions. Legacy `load` calls retain their documented raw-orientation default. Report encoded dimensions separately, clear the pending orientation after applying it, and never rotate the result twice. Carry AVIF container transforms through the same normalized transform model; do not blindly apply an additional EXIF transform.

For multi-image data, distinguish **animation frames**, **document pages**, and **alternative resolutions**. GIF frame selection returns a composited canvas with timing and loop metadata. TIFF defaults to page 0 and allows explicit page selection. ICO defaults to the largest usable representation for full loading and selects by requested size for thumbnails. An ordinary second TIFF page is never an implicit thumbnail of page 0.

### 3.3 Lifetime and allocation

Sources and sinks are borrowed for synchronous operations. An open decoder borrows its source and context until closed; the caller must retain both. Each successful decoded result owns its pixels and any retained metadata, and remains valid after closing the decoder. `image_decoded_dispose` is the single public release operation and is safe on an empty result.

Use existing tracked allocation for Lambda-owned native pixels and scratch; do not introduce another allocator mechanism (**D4.1.4v5**). Prefer codec APIs that write into provided buffers or accept allocation callbacks. Where a vendor owns internal allocations, close them with the vendor's matching release API and document the portion of memory the adapter cannot directly account for. Source limits, codec-native limits, and conservative admission estimates supplement tracked budgets; do not claim that a pixel quota bounds all codec memory.

Input-owned resources may register cleanup with the existing pool lifecycle (**D4.1.3**, **D4.2.6**). Runtime adapters root Lambda values with `RootFrame`/`Rooted` across allocations and reload borrowed GC pointers after safepoints (**D5.3.3**). A worker must hold a native snapshot or another sanctioned stable owner; it must not retain a bare pointer into a movable Lambda binary. No native-stack scanning is introduced.

## 4. Uniform errors and diagnostics

Every fallible native operation returns `ImageStatus`; the optional `ImageError` is caller-owned and contains a bounded, allocation-independent diagnostic. Initialize outputs to empty on failure and transfer ownership only on success. Reusing an output requires disposing its previous contents first.

| Status family | Meaning |
|---|---|
| `INVALID_ARGUMENT` | Bad dimensions/options, invalid pixel layout, or incompatible selections |
| `UNKNOWN_FORMAT` | No supported signature/container identification |
| `CODEC_UNAVAILABLE` | Recognized format or operation omitted from this build |
| `UNSUPPORTED_FEATURE` | Recognized codec, unsupported compression/layout/color feature |
| `TRUNCATED_INPUT`, `INVALID_DATA` | Missing bytes versus a malformed container or bitstream |
| `IO_ERROR` | Open/read/seek/write/flush/close/publication failed |
| `LIMIT_EXCEEDED` | An explicit input/output/scratch/frame/metadata quota was exceeded |
| `FAST_PATH_UNAVAILABLE` | Thumbnail requested with full-decode fallback prohibited |
| `CANCELLED` | Caller cancellation observed at a supported checkpoint |
| `OUT_OF_MEMORY`, `INTERNAL_ERROR` | Allocation failure or an internal invariant failure |

`ImageError` carries the stable code, operation (`probe`, `decode`, `encode`, `read`, `write`, etc.), format, bounded message, and optional native code, byte offset, page/frame index, and OS error. Each optional field has an explicit validity flag. The I/O layer adds resource identity where appropriate. Consumers branch on stable codes; vendor text is diagnostic detail. Preserve the original failure when cleanup also fails, with cleanup failure as secondary detail.

Warnings are separate records through a caller-supplied callback. A warning does not replace an error or turn a failed decode into success. Distinguish recoverable metadata loss from pixel corruption; unsupported mandatory interpretation is an error. There is no global or thread-local "last image error", and no unconditional logging or stderr output in the codec core. Host adapters log once where useful, using a searchable `IMAGE_IO:` prefix and `lib/log.h`.

Adapter requirements:

- **libpng:** install custom error/warning and I/O callbacks. Keep `setjmp`/`longjmp` inside a narrow C-compatible boundary with explicit cleanup state; never jump across C++ destructors. The upstream API provides these hooks; its default handlers print to stderr. [libpng manual](https://libpng.org/pub/png/libpng-manual.html)
- **libjpeg-turbo:** use handle-scoped error reporting supported by the pinned API; retain native severity/code and copy the message before destroying the handle.
- **giflib / libwebp / libavif / libnsbmp:** translate native return codes immediately, with operation context. Do not guess an `IO_ERROR` from every generic decode failure.
- **libtiff:** use `TIFFClientOpenExt` with per-handle error/warning callbacks through `TIFFOpenOptions`, rather than changing process-global handlers. Require a version exposing those APIs. [LibTIFF open options](https://libtiff.gitlab.io/libtiff/functions/TIFFOpenOptions.html)

The Lambda adapter respects existing failure channels: ordinary pure decoding failures are error values (**S7.4.1**, **S7.4.5**), effectful reader APIs use the existing raised channel (**S7.4.2**, **S7.4.5**), and actual allocation exhaustion stays a system fault (**S7.4.3**), not a recoverable image-format error. Quota rejection remains a deliberate checked error. Map diagnostics into existing rich error construction with code, message, and source location (**S7.4.4**). Any reclassification of legacy `load`/`save` effects needs separate review; this proposal does not silently change their language signatures.

For path output, success includes successful encoding, writing, flushing/closing, and publication. On failure or cancellation, remove only a partial output owned by the operation; never delete an unrelated pre-existing destination. Reuse existing atomic-write utilities where their contract fits. Caller-provided sinks may already contain partial bytes on failure; transactional publication belongs to the I/O adapter.

## 5. Codec basis and format scope

### 5.1 Recommended libraries

| Format | Upstream basis | Proposed read scope | Proposed write scope |
|---|---|---|---|
| PNG | [libpng](https://www.libpng.org/pub/png/libpng.html), existing dependency | Static PNG, palette/grayscale/RGB/RGBA, transparency, interlace, explicit 16-to-8 conversion | Shared file/memory PNG encoder |
| JPEG | [libjpeg-turbo](https://www.libjpeg-turbo.org/Documentation/Documentation), existing dependency | Baseline/progressive, grayscale/RGB, EXIF orientation; CMYK/YCCK only with an explicitly supported conversion | Move existing JPEG output behind the common encoder; quality/subsampling controls |
| GIF | [giflib](https://giflib.sourceforge.net/), existing dependency | Still and animated GIF; preserve compositing, disposal, delay, loop behavior | Deferred; capability reports read-only initially |
| WebP | [libwebp](https://developers.google.com/speed/webp/docs/api), existing dependency | Lossy/lossless/alpha stills; animation is a separate advertised capability | Still lossy/lossless encoding |
| AVIF | [AOMedia libavif](https://github.com/AOMediaCodec/libavif), with decoder-only libaom | Still SDR images, alpha, container transforms; explicit bit-depth/color handling | Read-only; reports `CODEC_UNAVAILABLE` |
| BMP | [NetSurf libnsbmp](https://www.netsurf-browser.org/projects/libnsbmp/) decoder | Common Windows palette/RGB/bitfield variants; RLE4/RLE8 gated by verified upstream support | Uncompressed 24-bit Windows BMP; header/BGR row layout adapted from the pinned libjpeg-turbo `wrbmp.c` |
| TIFF | [LibTIFF](https://libtiff.gitlab.io/libtiff/) | Classic TIFF/BigTIFF within quotas; selected pages; strips/tiles; common grayscale/palette/RGB/RGBA compression subsets | Single-page RGB/RGBA TIFF with a small explicit compression set initially |
| ICO | libnsbmp for DIB payloads; existing libpng adapter for PNG payloads | Bounded icon directory, size selection, DIB XOR/AND masks and PNG entries | Single-entry ICO container glue around the shared upstream PNG encoder |

NetSurf's library is a C decoder for both BMP and ICO under the MIT license. The imported 0.1.7 source has operation-owned allocation callbacks, metadata-only probing, shared endian reads, and corrected palette/mask bounds. PNG-bearing ICO entries route through the shared PNG adapter rather than depending on an upstream decoder to support every ICO payload type. [Libnsbmp project](https://www.netsurf-browser.org/projects/libnsbmp/)

libnsbmp supplies decoding. BMP writing adapts the uncompressed header and BGR row layout of libjpeg-turbo’s IJG writer, retaining its notices. ICO writing adds the small directory wrapper around shared PNG output; no new compression codec is implemented.

libavif exposes a C API and supports selectable AV1 codec dependencies. The implementation uses decoder-only libaom 3.12.1 to retain one AV1 backend with a smaller native binary; no comparative performance claim is made. `build_lambda_config.json` selects the upstream decoder source closure, disables `CONFIG_AV1_ENCODER`, and enables only libavif's AOM decoding adapter. Keep additional AV1 backends disabled. Its optional libyuv acceleration is a measured build choice, not a second image API. [libavif build documentation](https://github.com/AOMediaCodec/libavif/blob/main/README.md)

Read support for all eight formats is a required milestone. Write support is tracked independently: preserve existing PNG/JPEG output, then add WebP, BMP, TIFF, and ICO. AVIF and GIF encoding and new animation encoders are deferred. A single `image_encode` interface can report unsupported operations without every format having identical capabilities.

### 5.2 Format-specific boundaries

- **BMP:** validate DIB size, row padding, signed/top-down height, palette bounds, bit masks, and compression mode. Explicitly reject unsupported OS/2 or embedded JPEG/PNG variants. The initial writer need not implement every readable variant.
- **ICO:** directory dimensions encoded as zero mean 256; verify the selected payload's real dimensions and bounded offset/length. Handle DIB doubled height, row alignment, legacy AND masks, and 32-bit alpha conventions with reference fixtures. Reuse one selection helper and bounded-subsource mechanism; never decode every entry just to choose a size.
- **TIFF:** cap directory traversal, detect cycles, validate offset/byte-count pairs, and distinguish pages from reduced-resolution SubIFDs. Support a declared matrix of compression and photometric/sample layouts; a compiled LibTIFF feature does not automatically become a supported Lambda feature. Normalize its channel/alpha/row conventions explicitly. Strip/tile access is available through the upstream APIs. [LibTIFF API](https://libtiff.gitlab.io/libtiff/functions/libtiff.html)
- **AVIF:** identification must distinguish AVIF from generic HEIF/HEIC. Bound item/grid dimensions and metadata before pixel allocation. Sequence decoding, HDR tone mapping, and opportunistic progressive/thumbnail items are later capabilities unless verified in the selected library version.
- **PNG/JPEG/GIF/WebP:** retain the current accepted corpus and existing GIF animation contract during migration; additions must not reduce existing support.

### 5.3 Unification and trimming

Develop the library by adapting official upstream source, sharing common utilities across the adapted implementations, and removing unneeded code. Source modification is part of the approved approach. Each change needs a concrete integration, duplication, resource-use, or feature-trimming reason; preserve format-specific algorithms and optimized kernels unless evidence justifies changing them.

Prefer existing codec hooks where they provide the required behavior. Where they do not, make focused source changes to use common byte access, allocation, diagnostics, or row-processing helpers. Extract reusable utilities into the shared core and call them from the adapted codecs. The target is one file/memory reader, one PNG setup path, one pixel allocator, one orientation/resampling path, and one diagnostic contract, without repeated implementations inside the wrappers and sourced code.

Trim the imported source/build subset to the supported feature matrix, retaining license notices, provenance, build prerequisites, and upstream regression material needed for maintenance. Exclude CLI apps, examples, benchmarks, unused codecs, and unneeded optional dependencies from production artifacts. Dead-strip unused code where supported. Feature removal must preserve declared compatibility and malformed-input checks.

Record the upstream URL, imported release/commit, retained license notices, and a brief summary of local adaptations in each imported library's source record. Modify the checked-in source directly and use ordinary Git history for review and maintenance. No separate `patches/` files or patch-replay workflow is required. The user's explicit source policy above authorizes this approach for the image-library work when applying the general vendor instructions.

Build the imported sources through `build_lambda_config.json` and generated build machinery, with explicit dependency versions and feature checks. Avoid linking an additional system copy of a codec already built from the local source. Required release formats must not silently disappear when a package is absent. Deliberately reduced profiles expose their capability set. Image support remains excluded from the current WASM evaluation profile (**D7.1.7v6**); runtime-only native CLI builds can use the codec core without Radiant (**D7.1.6**).

## 6. Fast thumbnail loading

### 6.1 Size contract

Thumbnail mode takes a positive bounding box in **physical pixels**, preserves aspect ratio, and does not enlarge by default. A scalar size is shorthand for a square box. CSS callers convert CSS size and device scale before invoking it; the codec library has no device-scale policy.

For oriented source dimensions `w × h` and bounds `W × H`:

```text
s = min(1, W / w, H / h)
output_width  = max(1, floor(w * s))
output_height = max(1, floor(h * s))
```

Compute this with checked arithmetic and a shared rounding helper so boundary cases cannot exceed either limit. A `4032 × 3024` image requested at `256 × 256` returns `256 × 192`; its portrait counterpart returns `192 × 256`; a `100 × 80` source stays `100 × 80`. The returned dimensions describe the actual pixel buffer. There is no implicit crop, padding, or stretch.

Keep Radiant's existing **at-least** scaled-decode requirement as a distinct mode: it may retain a codec-sized intermediate at or above the target for later painting. Thumbnail mode always finishes at the computed bounded size. Both use the same planner and decoding machinery; changing the legacy scaled loader to thumbnail semantics would be a regression.

### 6.2 Decode planning

After bounded metadata inspection, choose the cheapest valid representation that can supply the requested output:

1. Select a suitable native resolution or verified embedded preview of the same image, if available and allowed.
2. Use reduced-resolution codec decoding where supported.
3. Decode rows/strips/tiles into a bounded reduction pipeline where possible.
4. Otherwise perform a budget-admitted full decode followed by shared resampling, only if fallback is permitted.

Representation selection is a policy: default to a sufficient representation, rather than enlarging an undersized preview of a larger source. Embedded previews must have a verified relationship to the selected image and compatible orientation/color interpretation. No unvalidated preview parser is required for the first release.

Return the selected strategy (`native_scaled`, `selected_resolution`, `streamed_reduce`, or `full_decode_resize`) and whether full-primary-resolution fallback occurred. `allow_full_decode=false` requires a verified plan avoiding a full-primary-resolution pixel/plane allocation; if none exists, return `FAST_PATH_UNAVAILABLE`. Decoding a suitable smaller ICO entry or equivalent TIFF level is permitted: that is resolution selection, not primary-resolution fallback. This does not promise sublinear CPU or byte consumption: a PNG may still require decompressing every row. Codec scratch and working planes count in plan admission, not just the returned RGBA buffer.

### 6.3 Per-format opportunities and limits

| Format | Thumbnail strategy | Important limit |
|---|---|---|
| JPEG | Select a supported TurboJPEG scaling factor just above the final target, then resize once | Use reported unordered factors and actual dimensions; progressive coefficients retain source-size storage |
| PNG | Non-interlaced row decode with rolling area reduction; share it for files and memory | Entropy/filter work still covers the image; Adam7 needs an explicit bounded implementation or reported full-decode fallback |
| WebP | Advanced decoder scaling into caller-provided output | Lossless history and alpha planes retain source-size storage; flag fallback and reject in strict mode |
| GIF | Decode the first display frame only for a thumbnail | Avoid slurping all animation frames; existing full GIF APIs retain all-frame behavior; unsupported streaming cases use explicit fallback |
| AVIF | Full AV1 decode followed by resize initially | Do not claim generic JPEG-like reduced decode; return a full-decode strategy, or fail when strict fast mode forbids it |
| BMP | Reduce rows directly if the selected upstream API permits incremental access | A full-bitmap-only decoder requires budgeted fallback; RLE cannot be treated as random-access rows |
| TIFF | Select a verified reduced-resolution SubIFD, else process bounded strips/tiles into reduction | Strip/tile decoding may still consume substantial source data and scratch; arbitrary pages are not previews |
| ICO | Select the smallest sufficient entry, then delegate to PNG/DIB decode | Directory selection saves decoding unrelated entries; a selected DIB decoder may still allocate that entry at full size |

TurboJPEG provides scaled decompression APIs, and WebP's advanced API explicitly supports rescaling during decoding. These are implementation bases, not measured speedup claims. [TurboJPEG API](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/src/turbojpeg.h), [WebP advanced decoding](https://developers.google.com/speed/webp/docs/api#advanced_decoding_api)

TIFF reduced-resolution selection is conditional on equivalent image content, aspect ratio, and metadata. For ordinary AVIF stills, full YUV working planes may dominate memory even when the final RGBA thumbnail is small. Future support for layered/progressive AVIF or embedded previews must be capability-tested against the pinned API and covered by fixtures before it is advertised.

Use one shared alpha-correct area reduction for shrinking and existing bilinear machinery where suitable. Filtering operates on premultiplied color and alpha; output returns to straight alpha, preventing dark or colored fringes. Reuse the relevant code in `lib/image_process.cpp`, extracting a shared helper where necessary instead of copying it. A color-managed/linear-light quality mode can be added separately; fast thumbnail mode must state its sample-space behavior.

Thumbnail caches remain in their existing owning subsystem. Keys include source revision, output size, selected page/frame, orientation/color policy, and quality mode. The library has no hidden global pixel cache. A cached thumbnail never replaces the intrinsic dimensions used for layout or the source needed for a later larger decode.

## 7. Integration direction

Preserve existing public `load`/`save` forms and RGBA array shape `(height, width, 4)`. First route their implementation through the new core and richer diagnostics. Add options without inventing another unrelated image representation.

The extension to the existing `load` request map is:

```text
load({
    data: encoded_snapshot,
    thumbnail: {width: 256, height: 256},
    normalize_orientation: true,
    allow_full_decode: true,
    max_pixels: 16000000
})
```

This request shape is implemented for both binary snapshots and paths in `data`. `thumbnail: 256` may abbreviate the square box. Keep `max_pixels` as a source-image admission limit; a separate output/scratch budget controls thumbnail resources. A small thumbnail must not bypass source-size limits. Unknown option names follow **S17.8.1**.

Decide metadata access separately from the pixel-array return: the native result already carries dimensions, strategy, and diagnostics; a future Lambda information API can expose them without changing `load` to return a map. Extend encoding with explicit format/options while preserving the existing PNG default of `save(img, path)`; do not silently switch legacy encoding according to a filename suffix. JPEG alpha flattening requires a defined background, rather than dropping alpha from premultiplied pixels.

Radiant adapters retain responsibility for surfaces, alpha-mode conversion, decode promotion, and rendering placeholders. Format/MIME acceptance should query the shared raster registry plus the existing SVG capability, rather than maintain another hard-coded format list. Document export and data-URI output use the same encoder/sink interface. Bitmap fonts consume memory decoding directly from `lib`, with no resource acquisition.

## Appendix A. Preimplementation inventory

Historical inventory captured before the migration on 2026-10-15. The replacement implementation is described in [the implementation record](impl/Lib_Image_Impl.md); this table preserves the consolidation rationale.

| Location / symbols | Current behavior and consolidation target |
|---|---|
| [`lib/image.h`](../lib/image.h), [`lib/image.c`](../lib/image.c): `image_load`, `image_load_from_memory`, `image_get_dimensions*` | PNG/JPEG/GIF/WebP dispatch, mostly null/boolean failures, file/memory duplication; consolidate sources, probing, decode setup, and errors |
| `lib/image.c`: `image_load_scaled`, `image_load_from_memory_scaled` | File JPEG has native scaling and file PNG has row reduction; memory JPEG scales, memory PNG falls back to full loading; other formats also fall back. The fallback returns full pixels rather than guaranteeing a final thumbnail resize |
| `lib/image.c`: `IMAGE_MAX_DECODED_BYTES`, `IMAGE_MAX_ENCODED_BYTES`, `IMAGE_MAX_GIF_FRAMES` | Existing limits are 512 MiB decoded storage, 128 MiB encoded bytes, and 4096 GIF frames; retain protection while making policies consistent and explicit |
| `lib/image.c`: `image_gif_load*`, `gif_preflight`, `image_jpeg_exif_orientation_from_memory` | Preserve GIF safety/compositing and JPEG orientation coverage; share bounded metadata utilities |
| [`radiant/surface.cpp`](../radiant/surface.cpp): `image_surface_ensure_decoded` | Retains intrinsic versus decoded dimensions and promotes decode size; preserve its at-least requirement |
| [`radiant/render_image_encode.cpp`](../radiant/render_image_encode.cpp): `render_encode_surface_png`; [`radiant/render_img.cpp`](../radiant/render_img.cpp): `save_surface_to_jpeg`; [`radiant/render_output.cpp`](../radiant/render_output.cpp) | Encoding exists outside the common image module; route codec work through shared pixel/sink interfaces |
| [`lambda/runtime/lambda-vector.cpp`](../lambda/runtime/lambda-vector.cpp): `fn_load`, `fn_save` | Existing path/binary loading, request-map pixel limit/orientation normalization, typed-array conversion, PNG saving; replace generic failures with common diagnostic translation |
| [`lib/font/font_rasterize_tvg.cpp`](../lib/font/font_rasterize_tvg.cpp): `render_cbdt_png` | Embedded glyph PNG decoding requires the codec implementation to remain available below Lambda |
| [`lib/image_process.h`](../lib/image_process.h), [`lib/image_process.cpp`](../lib/image_process.cpp) | Existing row callbacks, geometry, filtering, alpha-aware processing, and bilinear helpers are reuse candidates |
| [`lib/endian.h`](../lib/endian.h), [`lib/math_checked.hpp`](../lib/math_checked.hpp), [`lib/file.h`](../lib/file.h) | Reuse endian reads/writes, checked arithmetic, bounded file reads, and suitable atomic publication helpers |

## Appendix B. Adoption sequence and validation

The following was the adoption plan. Current completion and verification evidence live in [the implementation record](impl/Lib_Image_Impl.md):

1. **Common core and diagnostics:** import the existing four upstream codec bases under `./lib/image/`, record their versions/licenses, modify the source directly to use shared types/readers/sinks/diagnostics, and migrate the existing formats while preserving output. Move resource entry points to their proper provider. Do not leave a `lib` shim forwarding upward into Lambda (**D7.1.3**); memory compatibility wrappers may remain within `lib` temporarily.
2. **Thumbnail planner:** share file/memory JPEG and PNG paths, add WebP scaling and final bounded resize, expose strategy/fallback, and preserve Radiant's at-least mode. Establish honest per-format performance baselines.
3. **New readers:** import the BMP/ICO, TIFF, and AVIF implementations and required codec dependencies under `./lib/image/`, adapt their source directly, and integrate documented feature subsets with matching file/memory diagnostics. Reading all eight formats completes the format-expansion milestone.
4. **Common writers and cleanup:** migrate all existing PNG/JPEG writers; add WebP/BMP/TIFF/ICO output using the locally adapted upstream implementations and common sink. Keep AVIF read-only to bound the native codec footprint. Remove superseded paths and unused sourced features after callers migrate. Verify builds use the intended local source and preserve required license notices. Report binary/dependency changes and actual duplication removed, rather than reducing lines by reformatting.

Validation must cover:

- The same fixture through memory, file, and deliberately short/chunked readers; matching pixels, metadata, and stable error codes.
- All eight formats, sniffing with wrong extensions, disabled features, truncated data, invalid offsets/dimensions, arithmetic overflow, quotas, cancellation, and failing sinks including close/publication failure.
- PNG interlace/palette/transparency, JPEG progressive/grayscale/EXIF, WebP alpha, GIF disposal/loops, BMP padding/bitfields/RLE, ICO PNG/DIB/masks and selection, TIFF strips/tiles/SubIFDs/pages, and AVIF alpha/transforms/unsupported HDR.
- Thumbnail bounding and rounding, portrait orientation, small sources, one-pixel outputs, alpha edges, full-decode fallback flags, strict-fast rejection, and intrinsic-size preservation during Radiant promotion.
- Encode/decode round trips for every advertised writer: exact pixels for lossless subsets, stated tolerances for lossy encoders. Verify format signatures and metadata rather than comparing encoder bytes.
- Concurrent independent contexts with distinct diagnostics, allocation-failure cleanup, native buffer accounting, and forced-GC adapter lifetime checks.

Use existing GTest/golden conventions and add a dedicated native image suite if needed. Every added golden-driven `.ls` fixture needs its `.txt` expectation. Engine integration runs the applicable Lambda and Radiant baseline gates; dependency build checks cover native macOS, Linux, and Windows profiles and the existing layering gates.

Performance measurements use release builds only, following [Developer Guide §7](../doc/dev/Developer_Guide.md#7-worktrees-and-agent-gotchas); never run `make release` in a worktree. Keep generated artifacts under `./temp/`. Compare full-decode-plus-resize against thumbnail mode for small/large images and multiple target sizes; report wall time, peak tracked memory, process/codec working memory where measurable, bytes read, and selected strategy. Separate cold acquisition from warm decoding. Do not promise a universal speedup: JPEG/WebP scaling, ICO resolution selection, and pyramidal TIFF can save different kinds of work, while PNG and ordinary AVIF have different limits.

The implementation record pins the format subsets, RGBA8/SDR boundary, encoding options, thumbnail fallback policy, upstream versions, and source adaptations. The required outcome is one consistent image subsystem built from upstream code imported and modified directly under `./lib/image/`, explicit errors and capabilities, and measurable thumbnail behavior without duplicated codec paths.
