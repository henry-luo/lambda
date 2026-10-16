# Common image library implementation

Date: 2026-10-15

Design: [Common Image I/O Library](../Lib_Image.md). Scope and direct source
modification were authorized by the user; all imported code is under
`lib/image/<library>/`, with no patch replay step.

Spec linkage: **D7.1.1–D7.1.3** (codec mechanisms versus resource acquisition),
**D4.1.4v5** (tracked native allocation), **D5.3.3** (precise runtime roots),
**S7.4.3–S7.4.4** (faults and rich errors), **S17.8.1** (option validation).
No formal ruling changes are introduced.

The subsequent [utility consolidation analysis](Lib_Image_Refactor.md) records
shared pixel preparation, orientation, warnings, output buffering, endian
helpers, and measured release source/binary changes.

## Implementation

`lib/image.h` exposes a C ABI for memory/random-access sources, probing,
decoding, encoding, decoder sessions, owned result disposal, capabilities,
limits, cancellation, warnings, and bounded structured errors. The static
registry is shared by native consumers. Compatibility memory entry points
remain in `lib/image.c`; the GIF compositor is retained in `lib/image_gif.c`.

`lib/image_core.cpp` and `lib/image/image_internal.h` provide checked pixel
layouts, operation-owned allocations, exact reads, one growable output buffer,
alpha conversion, all eight orientation transforms, thumbnail planning, and
alpha-correct area reduction. The former Radiant memory admission helper is
promoted to `memtrack_allow_allocation`, so codec buffers share process-budget
and cache-reclamation checks with render surfaces. The reducer accumulates premultiplied samples
and returns straight RGBA8. It filters in sample space, without color management.

`lambda/io/image_io.*` owns files and atomic publication using the existing
file utility. A decode opens one source for both probe and decode. Encoded
output is completed in bounded memory before publication; encode/cancel
failure preserves an existing destination. Memory and file failures use the
same status vocabulary, with OS errors attached by the resource adapter. The
legacy worker save with a `created` flag preserves its exclusive-create
contract using `file_open_regular_write(..., false)`: it never replaces an
existing path and removes only a failed output it created. Ordinary saves
continue to publish by atomic replacement.

`fn_load` and `fn_save` retain their array result and PNG default. Request maps
add thumbnails, TIFF page/GIF frame selection, explicit output formats and
encoding options. Roots retain requests, binary snapshots, converted arrays,
and destinations across allocations. Actual OOM publishes the existing system
fault; quota/type/codec failures retain rich ordinary diagnostics.

Radiant probes by content, preserves intrinsic/display dimensions independently
of decoded pixels, and delegates memory/file decoding and PNG/JPEG export.
Its raster format/MIME acceptance uses the registry alongside SVG. Tiled PNG
export supplies rows through the same PNG encoder, preserving strip rendering
without a full-page pixel allocation or a second libpng error path.

## Imported sources and build

| Source directory | Pinned release | Role |
|---|---|---|
| `lib/image/libpng` | 1.6.55 | PNG read/write |
| `lib/image/libjpeg-turbo` | 3.1.2 | JPEG read/write; BMP writer basis |
| `lib/image/giflib` | 5.2.2 | GIF read/compositing |
| `lib/image/libwebp` | 1.6.0 | WebP read/write |
| `lib/image/libavif` | 1.3.0 | AVIF container/color conversion |
| `lib/image/libaom` | 3.12.1 | One decoder-only AV1 backend |
| `lib/image/libtiff` | 4.7.1 | TIFF read/write |
| `lib/image/libnsbmp` | 0.1.7 | BMP/ICO DIB reading |

Each directory retains upstream licenses and a `VENDOR.md` provenance record.
Unused corpora, applications, optional dependencies, and conversion tools are
trimmed or disabled. libnsbmp uses operation-owned bitmap/palette/directory
allocation and metadata-only probing; its DIB/palette/AND-mask bounds are
corrected in place. Optional libavif applications and backends are omitted.
The BMP writer adapts the imported IJG header/BGR layout;
ICO writing is directory glue around shared PNG output.

`build_lambda_config.json` now owns all image build inputs: the explicit
`lambda-image-codecs` source target, generated feature/version headers,
platform/CPU source sets, per-file intrinsic flags, and archive dependencies.
The ordinary Premake build produces `build/lib/liblambda-image-codecs.a`.
Consumers name the `image-codecs` provider; its `build_target` dependency makes
the host, modules, CLI, and tests build the archive before linking. The generic
generator also supports target includes and JSON source/flag groups.

`utils/generate_image_config.py`, invoked through `code_generators`, renders
the JSON-owned headers and runs the upstream AOM dispatch DSL generator. It
does not compile or link code and preserves unchanged output timestamps.
Generated files stay in `build_temp/image-config/`. Public source/generated
headers precede system codec headers in every variant, preventing accidental
ABI/configuration mixing. Upstream CMake recipes, templates, manifests,
staging, and `utils/build_image_codecs.py` are removed. There is no image CMake
invocation or dependency on the former codec prefix. Existing zlib is reused.

The source target excludes the GIF writer, WebP mux/demux/animation providers,
TurboJPEG packed-image file converters and lossless transforms, disabled TIFF
compression backends and C++ streams, standalone AOM rate control, and AOM's
optional libyuv/SVT-AV1/WebM providers. Native ARM64 retains PNG/JPEG/WebP NEON
and AOM's per-file SIMD/runtime dispatch. x86_64 selects PNG SSE2 and WebP
SSE2/SSE4.1, with portable JPEG and AV1 routines; that profile's runtime
performance has not been measured. Minimal AVIF libyuv scaling remains as a
required internal fallback. All eight formats and the thumbnail APIs remain.

### Decoder-only AVIF profile (2026-10-16)

The byte codec remains context-free in `lib`, as required by **D7.1.2v2**.
The reduced profile is declared in `build_lambda_config.json`; no additional
dependency or upstream source edit is required. The pinned libaom 3.12.1 decoder source lists were independently
checked against upstream configuration: 125 files on ARM64, including 56 ARM
SIMD files, and 68 portable files for the generic profile. Removing 158
encoder-side libaom files and libavif's writer reduces the complete ARM64 image
target from 562 to 403 translation units.

Both generated AOM profiles set `CONFIG_AV1_ENCODER=0` and retain
`CONFIG_AV1_DECODER=1` and high-bit-depth decoding. ARM64 retains runtime CPU
dispatch. libavif defines only `AVIF_CODEC_AOM_DECODE`; its encode macro is
absent because the upstream adapter tests whether that macro is defined.
The libaom/libavif file rules use `-Os`, while other codecs retain their speed
optimization. The normal build still uses the existing static image archive.

The common registry advertises AVIF as readable and not writable. Its existing
read-only dispatch returns `codec_unavailable` for AVIF output, preserving the
ordinary codec-error channel (**S7.4.4**). The unused native AVIF writer and
libavif writer translation unit are excluded. Stored lossless AVIF fixtures
retain alpha, 10-bit input, and all 12 rotation/mirror cases without linking an
encoder into tests. Thumbnail fallback and chunked-reader coverage still
include AVIF.

The original image suite passed 27/27 before the change; the decoder-only suite
passes 28/28. The Lambda image I/O fixture matches its updated golden, and the
Premake generator self-test passes. The macOS ARM64 release decreases from
30,052,632 to 25,853,448 bytes (28.66 to 24.66 MiB), saving 4,199,184 bytes
(4.00 MiB). The eight-format gallery renders to a byte-identical PNG before
and after the change. A minimal static libaom/libavif decoder executable is
1,243,640 bytes (1.19 MiB) and produces the same sample pixels as the measured
dav1d decoder; its only dynamic dependency is the macOS system library.
The generic source list was checked against upstream, but Linux
and Windows binaries have not been built for this change. Evidence is under
`temp/aom-decode-only/`.

The earlier source cleanup removed 805 files / roughly 17 MB from `lib/image/`.
Build recipes have since been deleted entirely rather than relocated. Licenses,
patent notices, copyright attributions, and provenance records remain, including
JPEG's required `README.ijg`. No formal ruling changes are introduced
(**D7.1.1–D7.1.3**, **D7.1.6–D7.1.7v6**).

## Finder thumbnail integration (2026-10-15)

Finder consumes the shared decoder through the document-owned `radiant.image_job`
thumbnail channel. The existing worker/cancellation lifecycle now admits copied
file paths and CSS bounds, converting bounds through the document's device scale.
Workers call `image_io_decode_path`; codecs remain in `lib` (**D7.1.2v2**).
The channel owns a 128-entry LRU cache, including decoder failures. Its 32 MiB
retained-pixel budget reserves the consumer's reported canvas bytes and pending
upload before admitting cached pixels; transient results and codec scratch
remain separately admitted by the shared image limits.
`FileRevision` adds subsecond modification time and file identity without changing
the existing `FileStat` ABI. Revisions are checked around decoding; explicit refresh
invalidates the cache even when publication metadata was preserved.

The app measures viewport geometry before mutations, prioritizes selected previews,
and uploads transient arrays into retained canvases (**S12.1.3**, **D4.5.2**).
Offscreen and retiring canvases release their pixels. Folder generations and job
tokens reject stale completions; document teardown cancels and joins native work
(**D4.5.1v4**). SVG remains on Radiant's vector path. No formal ruling changes.

Validation: 84 assertions across five Finder UI replays and 14 assertions in the
real-folder scrolling replay pass. Two Finder goldens pass on interp/auto/JIT;
18 file metadata tests pass, including a same-size, same-second revision change.
The forced-GC native worker replay passes with zero live tracked allocations at
shutdown. The complete Radiant View runner passes 203/203.

The full Radiant baseline is not green: document previews, timeline scripts,
table-caption view reuse, two recorded layout cases and one render reference fail.
Document/timeline failures reproduce with the original layout code; original-code
layout runs retain the same `tab-bidi-001` and `md_nuxt-readme` failures, and the
background-image render is byte-identical. The view-reuse caption crash remains
unresolved. Diagnostic reports are under `temp/finder/` and `test_output/`;
successful-run verbose logs were removed after the run exhausted disk space.

The large-directory follow-up isolates the flex-sized file scrollport with CSS
size/layout containment, initializes selection/focus in markup, and compares
listing inputs before formatting it again (**S12.1.3**). Thumbnail polling retains
geometry until publication, scroll or resize; node matches use explicit
`dom.same_node` identity (**S5.1.4**). All 102 assertions across six Finder UI
fixtures and the 14-assertion image scrolling replay pass; the thumbnail UI also
passes with forced GC and poisoning. The large-directory fixture checks that
opening a folder does not publish individual row-attribute mutations.

Three paired release runs against `476e9075f`, with 463 visible `./test` entries,
measure median navigation-through-next-frame latency of 2,944 ms before and
558 ms after (5.3×). Selection falls from 1,579 ms to 722 ms. Viewer startup is
excluded. `test/demo/finder/benchmark.py` reproduces the comparison, rejects debug
hosts and records raw runs under `temp/finder/performance/benchmark/`.
Host load affects these timings; no timing threshold is imposed on UI tests.

## Supported subsets and thumbnail behavior

| Format | Reading/writing | Thumbnail path |
|---|---|---|
| PNG | Palette/gray/RGB/RGBA, transparency, Adam7; 16-bit reduced to 8-bit; write gray/RGB/RGBA | Noninterlaced rows reduced incrementally; Adam7 full fallback |
| JPEG | 8-bit baseline/progressive gray/RGB, EXIF; write gray/RGB with explicit alpha background | Reported native scaling factors; progressive coefficients still require source-size storage |
| GIF | First display frame or composited selected frames; legacy all-frame timing/disposal/loops; read-only | Noninterlaced first frame reduced by rows; interlace/selected-frame full fallback |
| WebP | Lossy/lossless/alpha stills; write lossy/lossless | Native output scaling; lossless history/alpha require source-size planes |
| AVIF | SDR stills/alpha, irot/imir, high-depth quantization; read-only | Full AV1 planes and common resize; strict shrinking rejected |
| BMP | Upstream Windows palette/RGB/bitfield/RLE variants; write uncompressed 24-bit with explicit alpha background | Full upstream bitmap then common resize |
| TIFF | Classic/BigTIFF, explicit pages, strips/tiles, gray/palette/RGB/RGBA; write single-page DEFLATE | Compatible direct reduced-image SubIFDs, else strip/tile reduction |
| ICO | Validated PNG/DIB entries and AND masks; write one PNG entry | Smallest sufficient matching-aspect entry, then shared PNG/DIB decode |

Thumbnail bounds are physical pixels, preserve aspect ratio, and never upscale.
Results expose intrinsic metadata, actual dimensions, representation, strategy,
and a full-storage fallback flag. `allow_full_decode=false` rejects plans that
need source-size pixels/planes; it does not promise less entropy work or fewer
input bytes. The legacy at-least mode remains separate for Radiant promotion.
TIFF selection checks the reduced-image flag, aspect ratio, orientation, bit
depth, samples, photometric mode, alpha presence, and ICC bytes. It visits only
direct SubIFDs; ordinary pages never become implicit previews.

ICC presence and AVIF CICP are exposed, but profiles are not converted or
round-tripped. CMYK JPEG, animated WebP/AVIF, HDR PQ/HLG tone mapping, AVIF clean
apertures, GIF writing, and scientific TIFF preservation are unsupported.
Native libjpeg/WebP/libavif allocations are not all routed through the tracked
allocator: source/dimension limits and working-plane admission supplement
tracked output/scratch quotas, which are not a total process-memory ceiling.
Cancellation occurs at reads, rows, sink writes, and before/after native calls;
a single opaque native call may delay it. PNG rows are streamed into a bounded
compressed buffer, rather than directly publishing a partial destination.

## Verification

The Lambda-owned source build succeeds with the former CMake codec prefix
unavailable. `make build` and `make build-cli` compile/link the source archive;
the image/GIF suites pass **23/23 each**, and the renderer suite passes
**159/159**. The release CLI image fixture matches its golden. Generator
regressions verify ARM64/x86_64 source selection, per-file ISA flags, Windows
TIFF source selection, and transitive build dependencies for hosts and tests.
Build/test logs are under `temp/image-native-build/`. Full Linux/Windows
compilation and execution are not claimed.

The native host build and runtime-only release CLI build succeed. The
release CLI also passes the new seven-format Lambda image fixture, without
Radiant linked. Focused validation in this working tree:

- `test_image_io_gtest`: **23/23 passed**.
- Existing `test_gif_player_gtest`: **23/23 passed**.
- `test_rdt_vector_gtest`: **159/159 passed**, including PNG alpha, padded rows,
  process-budget rejection/recovery, SVG image decoding, and decode promotion.
- `RenderOutputParity.NormalPngMatchesForcedTiledPng`: **passed** with exact
  decoded pixel equality between normal export and forced 64-row strips.
- Native PNG/JPEG view loading, PNG preview promotion, and the image-worker
  forced-GC/close/ownership replay: **passed**. HTTP unsupported-image placeholder
  replay: **passed** outside the sandbox, whose loopback bind was prohibited.
- The new Lambda image fixture matches its golden on `interp`, `auto`, and
  `jit`, each with `LAMBDA_GC_FORCE_EVERY=1` and freed-memory poisoning.
- Image-related logging/layout-cast lint filters and `git diff --check`: passed.

`make test-lambda-baseline` reports **7,140/7,141 passed**, including all
**1,567/1,567 Lambda script tests**. The forced-GC sweep times out with no output
for 180 seconds in `regression_music_layout`; that script exercises music
layout/SVG generation, rather than the image `load`/`save` path. The timeout
is retained, not suppressed.

`make test-radiant-baseline` was attempted and remains red. It reported secondary
view/table-caption failures, three document-scroll UI failures, layout results
`3143 passed / 149 partial / 2 failed`, and a page-layout average regression.
Repeated GUI/3D watchdog timeouts made the run incomplete; only this run’s
process tree was stopped. macOS XPC connection errors and the HTTP loopback
bind failure were recorded in sandboxed tests. These broader failures are not
claimed as resolved. Two image regressions found during that run were fixed
and verified independently: the process-memory admission check and worker
exclusive-create behavior. The full baselines are not claimed green.

Native image tests cover
all seven writers and eight readers, exact lossless/alpha pixels, JPEG tolerance,
thumbnail bounds and strategies, file/memory/chunked sources, wrong extensions,
quotas, cancellation, failing sinks, existing-destination preservation, GIF
compatibility, all EXIF orientations, ICO DIB masks/selection/invalid offsets,
TIFF pages/tiles/SubIFDs, premultiplied input, row encoding, independent result
ownership, and operation allocation-failure cleanup.

Logs and generated fixtures are under `temp/image-import/` and
`temp/image-io-tests/`. This checkout's unrelated concurrent edits are retained.
Native macOS is the executed platform; Linux/Windows builds and performance
measurements have not been run. Codec dependencies and the CLI were built in
release mode; host regression checks use the debug build. No speedup or memory measurement
is claimed from debug functional tests.
