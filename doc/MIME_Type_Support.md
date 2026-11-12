# MIME Type and Format Support

## Summary

Lambda has **99 supported format profiles** with at least one full or partial
route for input, output, display, playback, editing, or execution: 16 data
formats, 31 markup and document formats, 49 image/graph/media formats, and 3
scripting formats. Each counted row is one format profile; MIME aliases for
that profile share a row. The count is
not the number of MIME strings: some formats have multiple aliases, and some
Lambda-specific or suffix-selected formats have no standard MIME type.

This inventory counts a format only when Lambda has a format-aware route in at
least one of the four columns. MIME database recognition by itself does not
count. A format can be selected by filename, an explicit `input()` type, a MIME
mapping, or a package-specific route; MIME names below do not imply that every
API dispatches directly on an HTTP `Content-Type` header. Explicit format
selection takes precedence over filenames (**D10.1.2**); domain documents such
as `.slides` retain their own format identity (**D10.1.3**).

| Mark | Meaning |
|---|---|
| ✅ | Supported route documented for this action. |
| ◐ | Partial, bounded, experimental, platform-dependent, or route-specific support. |
| — | No format-specific route identified. |

“Input / parse” includes format-aware import, structured parsing, archive
inspection, or media decoding. “Output / save” includes format writers and
explicit export routes. “Display / play” includes Lambda view/render and
document or media players. “Edit / run” includes an editor or executable
runtime, not merely opening a read-only preview.

## 1. Data files

| Format / MIME type | Input / parse | Output / save | Display / play | Edit / run |
|---|---|---|---|---|
| **Plain text** — `text/plain` | ✅ | ✅ | ✅ | — |
| **JSON** — `application/json` | ✅ | ✅ | ✅ | — |
| **XML** — `application/xml` | ✅ | ✅ | ✅ | — |
| **YAML** — `application/x-yaml` | ✅ | ✅ | ✅ | — |
| **TOML** — `application/toml` | ✅ | ✅ | ✅ | — |
| **CSV** — `text/csv` | ✅ | — no CSV writer | ✅ table preview | — |
| **TSV** — `text/tab-separated-values` | ✅ | — no TSV writer | ✅ table preview | — |
| **INI** — `.ini` / `.conf` / `.cfg` (no single registered MIME) | ✅ | ✅ | ◐ text/property preview | — |
| **Java properties** — `text/x-java-properties`, `application/x-java-properties` | ✅ | ✅ | ◐ property preview | — |
| **SQLite** — `application/x-sqlite3` | ✅ database input/API | ◐ database writes use SQLite APIs | — | ◐ query/transaction APIs |
| **Mark** — `text/x-mark`, `application/x-mark`; `.mark` | ✅ | ✅ | ✅ | — |
| **XLSX** — `application/vnd.openxmlformats-officedocument.spreadsheetml.sheet` | ◐ ZIP package inspection; no workbook reader | ◐ chart/spreadsheet export | ◐ package inspection | — |
| **ZIP / JAR / WAR** — `application/zip`, `application/java-archive` | ✅ archive tree | ✅ ZIP writer | ◐ file-browser inspection | — |
| **7z** — `application/x-7z-compressed` | ✅ archive tree | — no 7z writer | ◐ file-browser inspection | — |
| **TAR and compressed TAR** — `application/x-tar`; `.tgz`, `.tbz2`, `.txz`, `.tzst` | ✅ archive tree | — no TAR writer | ◐ file-browser inspection | — |
| **RAR / RAR5** — `application/vnd.rar`, `application/x-rar-compressed` | ✅ archive tree | — no RAR writer | ◐ file-browser inspection | — |

CSV and TSV have readers but no writers. ZIP output is supported; other
recognized archive inputs can be inspected or converted to ZIP, subject to
format and platform limits. See [System Functions](Lambda_Sys_Func.md) and the
[document format guide](Markup_Formats_Support.md).

## 2. Markup and docs

| Format / MIME type | Input / parse | Output / save | Display / play | Edit / run |
|---|---|---|---|---|
| **Markdown** — `text/markdown`; `.md`, `.markdown` | ✅ | ✅ Markdown/HTML output | ✅ rendered document | ✅ `lambda edit` |
| **HTML5** — `text/html` | ✅ | ✅ HTML output | ✅ view/render | ✅ DOM/editor and page scripts |
| **XHTML** — `application/xhtml+xml` | ✅ strict XML input | ◐ serialize through XML/HTML routes | ✅ | ◐ DOM editor |
| **reStructuredText** — `text/x-rst` | ✅ | ✅ RST writer | ✅ rendered document | — |
| **Wiki family** — `text/x-wiki`; MediaWiki, DokuWiki, Creole, Jira, Confluence, TikiWiki, TWiki, Vimwiki, XWiki, ZimWiki | ✅ selected flavor | ✅ wiki writers | ✅ rendered document | — |
| **AsciiDoc** — `text/x-asciidoc`; `.adoc` | ✅ | ✅ | ✅ | — |
| **Org** — `text/x-org`; `.org` | ✅ | ✅ Org writer | ✅ | — |
| **Textile** — `text/textile`, `application/textile` | ✅ | ✅ Textile writer | ✅ | — |
| **troff / man** — `text/troff`; `.man`, `.1`–`.8` | ✅ | ✅ | ✅ | — |
| **Typst** — `text/typst`, `application/typst` | ✅ | ✅ | ✅ | — |
| **MDX** — `text/mdx` | ✅ parser | ✅ MDX writer | ◐ source/AST route; no general MDX renderer | — |
| **JSX markup** — explicit `jsx` type; no registered MIME | ◐ JSX tree parser; expressions remain source | ✅ JSX writer | — no direct viewer route | — not executable by LambdaJS |
| **LaTeX / TeX** — `application/x-tex`, `application/x-latex` | ✅ bounded TeX expansion and document parse | ✅ LaTeX writer | ◐ document rendering; this is not TeX typesetting | — |
| **TikZ / PGF** — `text/x-pgf`; `.pgf`, `.tikz` | ✅ parser/renderer | — | ✅ rendered graphic | — |
| **BibTeX / BibLaTeX resources** — `.bib` (read as a local TeX resource; MIME is not registered) | ✅ bounded resource parser | — no BibTeX writer | ✅ bibliography in rendered TeX documents | — |
| **Math expressions** — explicit `math` or `math-ascii` type; plain-text source, no dedicated MIME | ✅ LaTeX/ASCII math parser | ✅ LaTeX/ASCII math writer | ◐ documented MathML rendering subset | — |
| **PDF** — `application/pdf` | ✅ page/content input | ◐ render-to-PDF export | ✅ view/render | — |
| **RTF** — `application/rtf` | ◐ experimental raw control-group parser | — | — | — |
| **EML / RFC 822 email** — `message/rfc822`, `application/eml`, `message/eml` | ✅ message/MIME tree | ✅ EML writer | ✅ header/body preview | — |
| **vCard** — `text/vcard`; `.vcf` | ✅ | ✅ VCF writer | ◐ property preview | — |
| **iCalendar** — `text/calendar`, `application/ics`; `.ics` | ✅ | ✅ ICS writer | ◐ property preview | — |
| **DocBook 5** — `application/docbook+xml`; `.xml` with explicit DocBook route | ✅ bounded profile | — no DocBook serializer | ✅ partial HTML/CSS publication | — |
| **DOCX** — `application/vnd.openxmlformats-officedocument.wordprocessingml.document` | ◐ bounded semantic import | ◐ bounded Word package export with loss diagnostics | ◐ viewer preview | ◐ Word editing workflow |
| **PPTX** — `application/vnd.openxmlformats-officedocument.presentationml.presentation` | ◐ bounded slide import | — no PPTX writer | ✅ slide player/preview | ◐ Slides Open saves an editable `.slides` copy |
| **EPUB** — `application/epub+zip` | ◐ bounded EPUB 2/3 reader | — no EPUB writer | ✅ ebook reader | — |
| **OpenDocument Text (ODT)** — `application/vnd.oasis.opendocument.text` | ◐ ZIP package inspection only | — no ODT writer | ◐ package inspection | — |
| **OpenDocument Spreadsheet (ODS)** — `application/vnd.oasis.opendocument.spreadsheet` | ◐ ZIP package inspection only | — no ODS writer | ◐ package inspection | — |
| **OpenDocument Presentation (ODP)** — `application/vnd.oasis.opendocument.presentation` | ◐ ZIP package inspection only | — no ODP writer | ◐ package inspection | — |
| **CSS** — `text/css`, `application/css` | ✅ stylesheet parser | ✅ CSS formatter | ◐ applied during HTML layout/render | — |
| **Lambda slides** — `.slides` (`<presentation>` Mark document; no registered MIME) | ✅ explicit `slides` input | ✅ Mark output | ✅ presenter/player | ✅ slide editor |
| **Unified diff / patch** — `text/x-diff`, `text/x-patch`; `.diff`, `.patch` | ✅ patch parser/review model | — no patch writer | ✅ interactive review and rendered report | ◐ review actions, not source editing |

The format guide documents parser and writer limits for [markup/data](Markup_Formats_Support.md), [source formatters](Script_Formats_Support.md), and [PDF, DOCX, EPUB and PPTX packages](Lambda_Packages.md). DOCX import is semantic and bounded; it does not promise Word-faithful pagination or general DOCX round-tripping (**D9.3.4** package boundary; see also the [DOCX profile](Lambda_Packages.md#15-docx-word-docx-import-and-export)). LaTeX uses TeX expansion, not TeX typesetting (**D10.2.2**).

## 3. Image, graph, media

| Format / MIME type | Input / parse | Output / save | Display / play | Edit / run |
|---|---|---|---|---|
| **PNG** — `image/png` | ✅ decode | ✅ encode | ✅ image view/render | ◐ painting/image API |
| **JPEG** — `image/jpeg` | ✅ decode | ✅ encode | ✅ image view/render | ◐ painting/image API |
| **GIF** — `image/gif` | ✅ still and animated input | ✅ GIF writer | ✅ animation playback | ◐ frame/image API |
| **WebP** — `image/webp` | ✅ decode | ✅ encode | ◐ codec/resource route | ◐ image API |
| **AVIF** — `image/avif` | ◐ decoder/API route | — decoder only | ◐ image resource route; HTML image loading is unsupported | — |
| **BMP** — `image/bmp` | ✅ decode | ✅ encode | ◐ codec/API route; HTML image loading is unsupported | ◐ image API |
| **TIFF** — `image/tiff` | ✅ decode | ✅ encode | ◐ codec/API route; HTML image loading is unsupported | ◐ image API |
| **ICO** — `image/x-icon`, `image/vnd.microsoft.icon` | ✅ decode | ✅ encode | ◐ codec/API route; HTML image loading is unsupported | ◐ image API |
| **Photoshop PSD** — `image/vnd.adobe.photoshop` | ✅ layer-aware input | ✅ PSD output route | ✅ view/render | ◐ layer/image API |
| **SVG** — `image/svg+xml` | ✅ HTML5 SVG parsing | ✅ DOM SVG export | ✅ view/render | ✅ DOM editing |
| **EPS** — `application/postscript`; `.eps`, `.epsi` | ◐ EPS render input | — no EPS writer | ✅ render/view | — |
| **Lottie animation** — `.json`, `.lottie` (`application/json` for JSON form) | ◐ viewer animation load | — no Lottie writer | ◐ plays in `lambda view`; static layout/render is blank | — |
| **Lambda painting** — `application/x-lambda-painting`; `.painting` | ✅ | ✅ painting save | ✅ | ✅ painting editor |
| **TrueType fonts** — `font/ttf` | — font resource, not a document parser | — | ◐ used as a text/font resource | — |
| **OpenType fonts** — `font/otf` | — font resource, not a document parser | — | ◐ used as a text/font resource | — |
| **WOFF fonts** — `font/woff` | — font resource, not a document parser | — | ◐ used as a text/font resource | — |
| **WOFF2 fonts** — `font/woff2` | — font resource, not a document parser | — | ◐ used as a text/font resource | — |
| **Graphviz DOT** — `text/vnd.graphviz`; `.dot` | ✅ graph parser | ◐ SVG/HTML rendering | ✅ graph view/render | — |
| **D2** — `.d2` (suffix-selected) | ✅ graph parser | ◐ SVG/HTML rendering | ✅ graph view/render | — |
| **Mermaid** — `.mmd` (suffix/flavor-selected) | ✅ graph parser | ◐ SVG/HTML rendering | ✅ graph view/render | — |
| **Structurizr DSL** — `.dsl` (suffix-selected) | ✅ graph parser | ◐ SVG/HTML rendering | ✅ graph view/render | — |
| **glTF JSON** — `model/gltf+json`; `.gltf` | ✅ | ◐ glTF JSON/GLB export for supported scene constructs | ✅ scene3d load/render | ◐ scene authoring API |
| **glTF binary (GLB)** — `model/gltf-binary`; `.glb` | ✅ explicit scene asset import | ◐ GLB export for supported scene constructs | ◐ scene3d load/render | ◐ scene authoring API |
| **Wavefront OBJ** — `model/obj` | ✅ | — no OBJ writer | ◐ scene3d asset loading | ◐ scene authoring API |
| **Wavefront MTL** — `model/mtl` | ✅ | — no MTL writer | ◐ scene3d material loading | ◐ scene authoring API |
| **ASCII Model3D (A3D)** — `text/x-3d-model`; `.a3d` | ✅ | — no A3D writer | ◐ character/scene loading | ◐ character authoring API |
| **STL** — `model/stl` | ✅ | — no STL writer | ◐ scene3d asset loading | ◐ scene authoring API |
| **PLY** — `model/x-ply` | ✅ | — no PLY writer | ◐ scene3d asset loading | ◐ scene authoring API |
| **FBX** — `model/x-fbx` | ✅ bounded character/asset import | — no FBX writer | ◐ scene3d loading/playback | ◐ character authoring API |
| **BVH** — `model/x-bvh` | ✅ | — no BVH writer | ◐ character animation playback | ◐ character authoring API |
| **MusicXML** — `application/vnd.recordare.musicxml+xml`; `.musicxml`, `.xml` | ✅ | — no MusicXML writer | ✅ sheet music rendering and MIDI playback | ◐ score/player API |
| **Compressed MusicXML (MXL)** — `application/vnd.recordare.musicxml`; `.mxl` | ✅ | — no MXL writer | ✅ sheet music rendering and MIDI playback | ◐ score/player API |
| **Standard MIDI File** — `audio/midi`, `audio/x-midi`, `application/x-midi`; `.mid`, `.midi` | ✅ Type 0/1 input | ✅ Type 1 output | ✅ MIDI player/synth playback | ◐ performance/player API |
| **WAV audio** — `audio/wav` | ✅ PCM decode | ✅ WAV/PCM output | ✅ audio playback | — |
| **MP3 audio** — `audio/mpeg` | ✅ decode | — no MP3 encoder | ✅ audio playback | — |
| **FLAC audio** — `audio/flac` | ✅ decode | — no FLAC encoder | ✅ audio playback | — |
| **AAC audio** — `audio/aac` | ◐ platform decoder (macOS Core Audio) | — no AAC encoder | ◐ platform-dependent playback | — |
| **M4A / MPEG-4 audio** — `audio/mp4` | ◐ platform decoder (macOS Core Audio) | — no M4A encoder | ◐ platform-dependent playback | — |
| **HLS playlist** — `application/vnd.apple.mpegurl`, `application/x-mpegurl`, `audio/mpegurl`, `audio/x-mpegurl`; `.m3u8` | ◐ archive/capture route | ◐ stored in archive output | ◐ archived stream viewing; not a standalone HLS player | — |
| **HLS media segments** — `video/iso.segment` (fMP4), `video/mp2t` (MPEG-TS) | ◐ consumed by HLS archive/capture | ◐ included in archived stream output | ◐ through archived media route | — |
| **WebVTT captions** — `text/vtt`; `.vtt` | ◐ HLS subtitle/caption resource | — no standalone VTT writer | ◐ used with supported media routes | — |
| **MP4 video** — `video/mp4` | ◐ native media route | — no video encoder | ◐ local HTML media on macOS; codec-dependent | — |
| **AVI video** — `video/x-msvideo` | ◐ native media route | — no video encoder | ◐ local HTML media on macOS; codec-dependent | — |
| **QuickTime video** — `video/quicktime` | ◐ native media route | — no video encoder | ◐ local HTML media on macOS; codec-dependent | — |
| **WMV video** — `video/x-ms-wmv` | ◐ native media route | — no video encoder | ◐ local HTML media on macOS; codec-dependent | — |
| **FLV video** — `video/x-flv` | ◐ native media route | — no video encoder | ◐ local HTML media on macOS; codec-dependent | — |
| **WebM video** — `video/webm` | ◐ native media route | — no video encoder | ◐ local HTML media on macOS; codec-dependent | — |
| **Matroska video** — `video/x-matroska` | ◐ native media route | — no video encoder | ◐ local HTML media on macOS; codec-dependent | — |
| **3GPP video** — `video/3gpp` | ◐ native media route | — no video encoder | ◐ local HTML media on macOS; codec-dependent | — |

The image rows describe Lambda's image codecs and file-view routes; Radiant's
HTML image pipeline has narrower format coverage. In particular, AVIF, BMP,
TIFF, and ICO are not supported as HTML image resources. SVG uses HTML5 SVG
parsing and namespace-aware DOM export (**D10.1.5–D10.1.6**). Audio decoding
supports PCM WAV, MP3, and FLAC portably; AAC/M4A decoding uses macOS Core
Audio. Video playback is local and host-dependent; it is not a general media
transcoding feature. See [HTML/CSS/SVG support](HTML_CSS_SVG_Support.md),
[MusicXML and MIDI](Lambda_Music.md), and [media package notes](Lambda_Packages.md#shared-decoded-audio-foundation).

## 4. Scripting

| Format / MIME type | Input / parse | Output / save | Display / play | Edit / run |
|---|---|---|---|---|
| **Lambda Script** — `text/x-lambda`; `.ls` | ✅ | — no general script serializer | ✅ viewer/REPL | ✅ parse, interpret and JIT run |
| **JavaScript** — `application/javascript`, `text/javascript`; `.js`, `.mjs`, `.cjs` | ✅ | — no JavaScript source formatter | ✅ page scripts | ✅ experimental `lambda js` runtime |
| **TypeScript** — `application/typescript`; `.ts` | ◐ annotations stripped; no type checking | — no TypeScript source formatter | — | ◐ `lambda ts` execution after annotation stripping |

JavaScript and TypeScript support belongs to LambdaJS (**D8.4.1–D8.4.4**); TypeScript
mode strips annotations and does not type-check. JSX has a bounded parser and
writer in the markup pipeline, but LambdaJS does not execute or render JSX.

## Sources and boundaries

- [Markup and data format support](Markup_Formats_Support.md) and [script format support](Script_Formats_Support.md) cover parsers and writers.
- [System function reference](Lambda_Sys_Func.md) documents `input()`, archive reading, and output routes.
- [Lambda package reference](Lambda_Packages.md) covers DOCX, PPTX, EPUB, graph, painting, music, media, and export packages.
- [CLI reference](Lambda_CLI.md) and [LambdaJS support](JS_DOM_Support.md) describe the viewers and script runtimes.
- Input and MIME service ownership belongs to `lambda-input` (**D9.1.2**, **D9.1.4**); this document records observed Lambda-facing routes and does not redefine those service contracts.

Examples of MIME-database entries not counted here because no Lambda
format-aware route was found include legacy binary Office (`.doc`, `.xls`,
`.ppt`), MOBI/AZW ebooks, Access databases, DWG/DXF CAD, and 3MF models.
OpenDocument files are counted above only for ZIP package inspection, not as
semantic document readers.

This is a capability inventory, not a claim of complete conformance to every
format specification. ✅ marks a usable feature route; it does not claim full
conformance to the format standard. Partial markers identify bounded profiles,
missing writers, rendering limits, package inspection only, or platform
dependencies. Format names and MIME labels should be revisited as the parser,
formatter, viewer, and package routes change.
