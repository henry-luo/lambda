# Radiant SVG2: Unified External and Inline SVG Handling

## Goal

Radiant currently has two SVG personalities:

- **Inline SVG** enters through the HTML5 parser and is rendered from the HTML/DOM `Element` tree.
- **External SVG** enters through image/resource paths and is currently parsed through `parse_xml()` in `input-xml.cpp` before being wrapped as an `RdtPicture` / `ImageSurface`.

This document proposes one SVG pipeline for all SVG sources:

- `<svg>...</svg>` inline in HTML
- `<img src="foo.svg">`
- `background-image: url(foo.svg)`
- standalone `.svg` documents
- external `<svg><use href="external.svg#id">`

The key design principle is: **all SVG markup should be parsed by the same HTML/SVG parser path and rendered by the same SVG rendering backend**. External resources should not be special XML-only islands.

## Design Decisions

This proposal makes the following decisions explicit:

- SVG `<image href="...">` nested inside SVG must be supported for raster images and SVG images.
- Standalone `.svg` documents and `<img src="foo.svg">` should render as vector documents wherever the target backend can preserve vectors. Rasterization is a fallback, not the primary path.
- Inline SVG inherits page CSS according to the HTML/CSS/SVG specifications: inherited properties inherit across the embedding boundary; regular page selectors can match inline SVG elements because they are part of the document tree; external SVG image documents are isolated from page selectors.
- External `<use href="external.svg#id">` should use render-by-reference as the initial behavior.
- Embedded `<style>` in external SVG loaded as an image should be supported by reusing the same CSS parsing/style machinery used for HTML `<style>`, with SVG-document isolation rules.

## Implementation Status

Status as of 2026-05-07: **partially implemented**. The core parser/render unification path is now in place, but the full `SvgDocument` resource/cache architecture described below is not yet complete.

| Area | Status | Notes |
|------|--------|-------|
| Parse external SVG through HTML/SVG path | Done | `html5_parse_svg_document()` parses external SVG through the HTML5 SVG correction path. `rdt_picture_load()` now uses this instead of `parse_xml()` for SVG pictures. |
| Standalone `.svg` rendering | Done | Standalone SVG keeps its prebuilt SVG/image view tree and renders through `RdtPicture` / `render_svg_to_vec()`. |
| `<img src="foo.svg">` vector-first rendering | Done | `render_image_content()` draws SVG images as `RdtPicture` vector content instead of forcing `render_svg()` rasterization first. |
| `background-image: url(foo.svg)` vector path | Mostly done | Background SVG tiles draw through duplicated `RdtPicture` instances and `rc_draw_picture()`. It still uses the current `ImageSurface` compatibility wrapper. |
| Nested SVG `<image href="nested.svg">` | Done for local files/data URIs | SVG data URIs and local `.svg` hrefs route through `RdtPicture`; raster image hrefs still use the existing ThorVG image loader path. Relative file resolution is now source-path aware. |
| External `<use href="external.svg#id">` | Done for synchronous local files | The renderer splits file/fragment, loads the external SVG DOM, finds the referenced id, and renders by reference with the `<use>` transform/viewport behavior. It does not yet use a shared async resource cache. |
| Embedded `<style>` inside external SVG | Partial | A lightweight scoped style bridge handles simple selectors (`tag`, `.class`, `#id`, and simple combinations) for presentation properties used by the SVG renderer. It is not yet routed through the full HTML CSS parser/cascade engine. |
| Inline SVG host CSS inheritance/selectors | Existing/partial | Inline SVG remains in the host DOM path. Full spec-grade CSS/SVG cascade normalization is still future work. |
| `SvgDocument` / `SvgDocumentRef` API | Not yet | Current implementation extends `RdtPicture` as the compatibility document wrapper instead of introducing the formal structs. |
| Shared `SvgResourceManager`, URL cache, async/network integration | Not yet | External SVG loads are still synchronous in the render/resource paths that were touched. Cache-by-canonical-URL and async repaint/reflow are pending. |
| Central id table | Not yet | Id lookup is recursive or render-time defs based. A persistent document-level id table is still pending. |
| Renderer API rename/split (`svg_render_document`, `svg_render_element`) | Not yet | `render_svg_to_vec()` remains the shared lower-level renderer; `render_svg_inline.cpp` still contains general SVG rendering logic. |
| Cycle detection for recursive SVG references | Partial | Local file SVG recursion now has a thread-local render stack guard. Recursive external `<use>` and file-based nested SVG `<image>` references are skipped with a debug log; broader async/cache-cycle detection is still pending. |
| Full CSS parser integration for SVG `<style>` | Not yet | The current bridge is intentionally narrow; full selector/cascade support remains part of the style cleanup phase. |

So, no: **not every designed action item has been implemented**. The implemented slice covers the high-impact behavior requested first: parser unification for external SVG pictures, vector-first external image rendering, nested SVG image support, local external `<use>` render-by-reference, and basic scoped embedded styles.

## Current State

### Inline SVG

Inline SVG is parsed by the HTML5 parser. While inside SVG namespace, the parser applies SVG tag and attribute case corrections such as `clippath` -> `clipPath`, `lineargradient` -> `linearGradient`, and `viewbox` -> `viewBox`.

The layout engine treats `<svg>` as an inline replaced element by default. Rendering dispatch calls `render_inline_svg()`, which casts the `ViewBlock` back to a DOM element, retrieves its native SVG `Element*`, and calls `render_svg_to_vec()`.

This path is good because it keeps SVG elements and attributes consistent with HTML parsing and with Radiant's document font/style state.

### External SVG

External SVG has several entry points:

- `load_image()` for `<img src="foo.svg">`
- background image loading for `background-image: url(foo.svg)`
- `load_svg_doc()` for standalone `.svg` files
- `process_svg_resource()` for external `<use href="external.svg#id">`

The first three eventually load an `RdtPicture`. `rdt_picture_load()` reads the file and calls an SVG picture creation path that parses the bytes with `parse_xml()`. The parsed SVG root is then rendered with `render_svg_to_vec()`.

That means external SVG and inline SVG now share much of the drawing code, but **do not share the same parser path**. External `<use>` is even less complete: it discovers and downloads the external SVG, but the current handler is mostly a stub that reads the file and logs the target fragment.

## Problems to Fix

1. **Parser divergence**

   Inline SVG gets HTML5 foreign-content behavior and SVG tag/attribute correction. External SVG gets XML parser behavior. This can create subtle differences in tag names, attributes, namespace aliases, self-closing handling, and error recovery.

2. **Resource handling divergence**

   `<img>`, CSS backgrounds, standalone SVG, and external `<use>` all perform similar URL resolution and load work through different local paths. They should delegate to one SVG-capable resource backend.

3. **Rendering entry point divergence**

   `render_svg()` is image/raster-oriented, while `render_inline_svg()` is document-context-oriented. Both should call the same actual SVG renderer with different placement/rasterization options.

4. **Text/font behavior risk**

   Inline SVG has access to `RenderContext`, current color, inherited CSS, and `FontContext`. External SVG rasterization can happen off-screen through an image-style path and must be explicit about which Radiant font APIs it uses.

5. **External `<use>` is incomplete**

   External fragments need the same parsed SVG document cache, id lookup, cloning/reference rendering, and resource lifetime management as other external SVG uses.

## Proposed Architecture

```text
                         +-------------------------+
inline <svg> ------------> existing HTML Element*   |
                         |                         |
<img src=foo.svg> -------> SvgResourceManager ------> SvgDocument
background url(foo.svg) -> shared URL/load/cache    |  - Input*
standalone .svg ---------> parse via input-html ----|  - Element* svg_root
external <use> ----------> fragment/id lookup       |  - id table
                         +------------+------------+
                                      |
                                      v
                         +-------------------------+
                         | SvgRenderBackend        |
                         | render_svg_document()   |
                         +------------+------------+
                                      |
                    +-----------------+-----------------+
                    |                                   |
             direct document draw                 off-screen raster
             inline/background SVG                image fallback/export
```

## Unified SVG Document Model

Introduce a small parsed-resource wrapper:

```cpp
struct SvgDocument {
    Input* input;
    Pool* pool;
    Element* svg_root;
    Url* source_url;
    float intrinsic_width;
    float intrinsic_height;
    HashMap* id_table;          // id -> Element*
    bool owns_pool;
};
```

Inline SVG does not need to allocate a new `SvgDocument` permanently. It can create a lightweight borrowed view:

```cpp
struct SvgDocumentRef {
    Element* svg_root;
    Pool* pool;
    Url* source_url;
    HashMap* id_table;          // optional/lazy for <use>
    bool borrowed;
};
```

The renderer should operate on a document/ref abstraction instead of directly caring whether the source was inline, an image, a background, or a standalone file.

## Parse External SVG Through Input HTML

External SVG should be parsed through the HTML5/input-html stack, not `parse_xml()`.

Add an explicit SVG document parse helper near the HTML parser API:

```cpp
Element* html5_parse_svg_document(Input* input, const char* svg_source, Html5ParseOptions* opts);
```

This helper should reuse the same tokenizer/tree-builder code as `html5_parse()` and the same SVG foreign-content correction tables. It should differ only in setup and extraction:

- Treat the source as a document whose meaningful root is the first `<svg>` element.
- Preserve SVG namespace behavior and SVG tag/attribute correction.
- Ignore or safely retain XML declaration, doctype, comments, and processing instructions.
- Return the `<svg>` root directly, or return a document wrapper with a predictable accessor.
- Build the same Lambda `Element` data model that inline SVG uses.

Implementation options:

1. **Synthetic HTML wrapper**

   Wrap SVG bytes in a minimal HTML body before parsing:

   ```html
   <!doctype html><html><body>...svg source...</body></html>
   ```

   Then extract the first `<svg>` element from the body. This is simple and reuses current parser behavior, but XML declarations and leading doctypes should be stripped or tolerated.

2. **HTML fragment mode with SVG root extraction**

   Feed the source through a fragment parser and extract the `<svg>` root. This avoids creating full html/head/body wrappers if the fragment API is already reliable enough.

3. **Dedicated SVG mode in HTML5 parser**

   Add `Html5ParseOptions::svg_document_mode`. The parser can parse a root `<svg>` while keeping normal SVG foreign-content correction. This is the cleanest long-term option if synthetic wrapping exposes edge cases.

Preferred path: start with option 1 or 2 for low risk, then graduate to option 3 if SVG document parsing needs more precise error recovery.

### Remove XML Parser Dependency From SVG Image Loading

Replace the current `rdt_picture_load()` SVG parsing path:

```text
rdt_picture_load(path)
  -> read bytes
  -> parse_xml(input, bytes)
  -> find first <svg>
```

with:

```text
svg_resource_load(url)
  -> read/fetch bytes through resource backend
  -> html5_parse_svg_document(input, bytes)
  -> build SvgDocument
  -> cache by canonical URL
```

`rdt_picture_load()` should either disappear from external SVG paths or become a thin compatibility shim over `SvgResourceManager`.

## Unified Resource Backend

All external SVG consumers should delegate to the same resource backend:

```cpp
SvgDocument* svg_resource_load(UiContext* uicon, const char* url, DomElement* owner);
SvgDocument* svg_resource_load_data(UiContext* uicon, const char* data, size_t size, const char* base_url);
Element* svg_resource_find_id(SvgDocument* doc, const char* id);
```

This backend should handle:

- relative URL resolution against the document URL
- local files, HTTP(S), and data URIs
- cache/deduplication by canonical URL without fragment
- fragment extraction by `#id`
- async network completion and reflow/repaint scheduling
- lifetime ownership for parsed `Input`, `Pool`, and `Element` trees

### Consumer Mapping

| Consumer | New behavior |
|----------|--------------|
| `<img src="foo.svg">` | Loads `SvgDocument` through `svg_resource_load()`, stores a vector image handle in `EmbedProp` or `ImageSurface` compatibility wrapper. |
| `background-image: url(foo.svg)` | Loads the same `SvgDocument`, then asks the renderer to paint it into each background tile. |
| standalone `.svg` | Calls the same resource load path, then creates a root `ViewBlock` sized from `SvgDocument` intrinsic dimensions. |
| `<use href="external.svg#id">` | Loads the same `SvgDocument`, resolves `id`, and renders/clones the referenced element using the same renderer. |

### Resource Types

The existing network layer has `RESOURCE_IMAGE` and `RESOURCE_SVG`. Keep `RESOURCE_SVG`, but use it for all external SVG documents, including image and background consumers. Raster image formats stay under `RESOURCE_IMAGE`.

Do not let `process_image_resource()` try to decode `.svg` with raster image APIs. SVG detection should route to `process_svg_resource()` or the shared SVG loader before raster decoding.

## External `<use href="external.svg#id">`

External `<use>` should not parse the external file in the `<use>` handler itself. It should ask the shared SVG resource backend for the parsed document and fragment.

Proposed flow:

```text
render <use>
  -> href = xlink:href || href
  -> if href starts with '#': lookup local defs/id table
  -> else:
       split url and fragment
       doc = svg_resource_load(base_url + url)
       target = svg_resource_find_id(doc, fragment)
       render target with <use> x/y/width/height transform
```

Two implementation strategies are possible:

1. **Render by reference**

   Keep the external target in its source document and render it with a composed transform. This avoids cloning and is good for rendering-only use cases.

2. **Clone into a shadow tree**

   Clone the target subtree into the owner SVG's shadow/use tree. This is closer to browser semantics if later DOM inspection, CSS inheritance, or event targeting matters.

Preferred path: render by reference first, with a clean API boundary that allows cloning later.

Render-by-reference is the required initial implementation for external `<use>`. The referenced element stays owned by its source `SvgDocument`; the current document composes the `<use>` transform, viewport, inherited SVG state, and x/y/width/height adjustments at render time. Cloning can be added later only if DOM/event semantics require it.

## Unified Rendering Backend

Replace the conceptual split between `render_svg()` and `render_inline_svg()` with one lower-level renderer:

```cpp
struct SvgRenderOptions {
    float viewport_width;
    float viewport_height;
    float pixel_ratio;
    RdtMatrix base_transform;
    FontContext* font_ctx;
    Color current_color;
    Color initial_fill;
    bool has_current_color;
    bool has_initial_fill;
    DisplayList* display_list;
};

void svg_render_document(RdtVector* vec, SvgDocumentRef* doc, SvgRenderOptions* options);
void svg_render_element(RdtVector* vec, SvgDocumentRef* doc, Element* element, SvgRenderOptions* options);
```

Then the public entry points become wrappers:

```text
render_inline_svg(rdcon, block)
  -> build SvgDocumentRef from block native Element
  -> build options from RenderContext, CSS inherited color/fill, clip, transform
  -> svg_render_document()

render_svg_image(rdcon, image, rect)
  -> get SvgDocumentRef from image/resource handle
  -> build options from object-fit/object-position/background tile rect
  -> svg_render_document()

render_standalone_svg(rdcon, root_view)
  -> same as image/document wrapper, no special parser path
```

The old `render_svg(ImageSurface*)` should be renamed or reduced to an off-screen rasterization helper:

```cpp
bool svg_rasterize_to_surface(ImageSurface* surface, SvgDocumentRef* doc, float target_width, float target_height, SvgRenderOptions* options);
```

This makes it clear that rasterization is only one output mode, not the SVG renderer itself.

### Vector-First Output

Standalone `.svg` and `<img src="foo.svg">` should stay vector as long as possible:

- SVG/PDF/vector display-list outputs should emit vector draw commands from `svg_render_document()` directly.
- Tiled or immediate raster surfaces may rasterize only at the final paint target.
- Off-screen rasterization should be limited to backends that require pixels, cache thumbnails, or compatibility code that has not yet been moved to vector resources.
- `render_svg(ImageSurface*)` should not be the main path for SVG image rendering. It should become an explicitly named fallback such as `svg_rasterize_to_surface()`.

This avoids quality loss for external SVG images and makes inline/external SVG match more closely in SVG/PDF export paths.

## ThorVG and Radiant Font/Text Consistency

The renderer should use one policy for all SVG text, regardless of source:

- Use Radiant `FontContext` for font lookup and fallback.
- Resolve `font-family`, `font-size`, `font-weight`, `font-style`, `line-height`, `text-anchor`, and baseline consistently.
- Use the same text shaping path as Radiant text where practical.
- Keep ThorVG text/picture wrappers isolated behind `rdt_` APIs.

Do not let external SVG text depend on a process-wide implicit font context unless there is no active document. Prefer passing `FontContext*` through `SvgRenderOptions`.

If SVG is rasterized off-screen for `<img>`, pass the owning document's `FontContext` into `svg_rasterize_to_surface()` so text matches inline SVG in the same page.

## ImageSurface Compatibility

The current renderer expects `<img>` content to live in `EmbedProp::img` as an `ImageSurface*`. For migration, keep compatibility but separate raster and vector payloads:

```cpp
struct ImageSurface {
    ImageFormat format;
    int width;
    int height;
    void* pixels;
    RdtPicture* pic;        // legacy/compatibility
    SvgDocument* svg_doc;   // new preferred vector payload
};
```

Longer term, consider replacing image-specific storage with a clearer resource union:

```cpp
enum EmbeddedResourceKind {
    EMBED_RESOURCE_RASTER_IMAGE,
    EMBED_RESOURCE_SVG_DOCUMENT,
    EMBED_RESOURCE_LOTTIE,
    EMBED_RESOURCE_VIDEO,
};
```

That would avoid treating SVG as a raster image with delayed pixel generation.

## Intrinsic Sizing and Layout

Use a single intrinsic-size helper for inline and external SVG:

```cpp
SvgIntrinsicSize svg_calculate_intrinsic_size(Element* svg_root, const SvgSizingContext* ctx);
```

This helper should implement the CSS Images/SVG rules already approximated in the current code:

- `width`/`height` attributes define intrinsic dimensions when absolute.
- `viewBox` supplies intrinsic aspect ratio and fallback dimensions.
- missing size defaults to `300 x 150` where appropriate.
- percentage `width`/`height` needs containing-block context.
- `preserveAspectRatio` affects rendering, not intrinsic dimensions except through aspect ratio.

Use this helper from:

- inline SVG layout
- `<img src="foo.svg">` intrinsic sizing
- standalone `.svg` document setup
- background-size calculations for SVG backgrounds

## CSS and Style Resolution

SVG has presentation attributes (`fill`, `stroke`, `font-size`, etc.) and may contain `<style>` elements. The unified renderer should cleanly separate three layers:

1. CSS inherited from the HTML document into inline `<svg>`.
2. SVG presentation attributes on SVG elements.
3. Stylesheets embedded inside SVG resources.

External SVG loaded as an image should not accidentally inherit page CSS selectors, but it should still use document resources such as fonts. Inline SVG should inherit `color`, font family, and applicable inherited properties from the HTML tree.

Normative inheritance policy:

- Inline SVG is part of the host document tree. Follow normal CSS cascade and SVG presentation attribute rules. Page selectors may match inline SVG elements, and inherited CSS properties inherit from HTML ancestors into the `<svg>` subtree when the property is inherited by spec.
- External SVG used as an image (`<img>`, CSS background, standalone image document) is an isolated SVG document. Page CSS selectors do not match inside it. Its own embedded stylesheets, presentation attributes, and user-agent defaults define its internal style.
- External `<use href="external.svg#id">` renders the referenced subtree from the external document. The external subtree should keep its source-document styles. Context-sensitive values such as `currentColor` should be passed according to SVG use/reference semantics where applicable, but page selectors still do not directly match the external document.
- Presentation attributes participate at the author-origin level with CSS specificity behavior matching SVG/CSS rules.

Embedded `<style>` in SVG resources should be supported rather than deferred. The implementation should reuse the HTML style pipeline where possible:

```text
html5_parse_svg_document()
   -> collect <style> children from the SVG document
   -> parse CSS with the existing CSS parser
   -> attach stylesheet list to SvgDocument
   -> resolve SVG element style with the same selector/cascade primitives used by HTML
```

The important difference is scope: external SVG stylesheets apply only inside their own `SvgDocument`; inline SVG `<style>` elements are part of the host document style environment unless the existing HTML style implementation already scopes them differently.

Recommended cleanup:

- Add an `SvgStyleContext` used by both inline and external SVG.
- Convert presentation attributes into the same internal style state before drawing.
- Support embedded `<style>` in external SVG by routing it through the same CSS parser/cascade infrastructure used for HTML styles.
- Make `currentColor` handling identical for inline and external render paths, with external SVG defaulting to black unless referenced context provides another value.

## Nested SVG `<image>` Support

SVG `<image>` elements should be handled as first-class SVG resources, not as an afterthought in shape rendering.

Supported forms:

- `<image href="photo.png">`
- `<image xlink:href="photo.png">`
- data URI raster images
- `<image href="nested.svg">`
- `<image href="sprite.svg#id">` where the referenced SVG can be rendered by reference

Rendering behavior:

- Resolve `href`/`xlink:href` against the current SVG document URL.
- Use the shared resource backend so nested image loads deduplicate with HTML images and CSS backgrounds.
- Raster images produce a raster image resource and draw through `rdt_draw_image()` / image display-list commands.
- Nested SVG images produce a `SvgDocument` and draw through `svg_render_document()` with the `<image>` element's x/y/width/height viewport.
- Respect `preserveAspectRatio` on `<image>` for SVG image content and for raster image fitting.
- Support clipping/masking/opacity/transform from the `<image>` element like other SVG paint elements.

The resource resolver must include cycle detection. Recursive SVG images or recursive `<use>` chains should fail gracefully with a warning and skip the recursive paint.

## URL and Reference Resolution

SVG references appear in several attributes:

- `<use href="...">`
- `<image href="...">`
- `fill="url(#grad)"`
- `stroke="url(#grad)"`
- `clip-path="url(#clip)"`
- `mask="url(#mask)"`
- filter/pattern references later

The renderer should use one resolver:

```cpp
struct SvgResolvedReference {
    SvgDocument* document;
    Element* element;
    const char* fragment_id;
};

bool svg_resolve_reference(SvgDocumentRef* current_doc, const char* href, SvgResolvedReference* out);
```

Internal `#id` references use the current document id table. External references load via `svg_resource_load()` and then use that document's id table.

## Caching

Cache parsed SVG documents by canonical URL without fragment:

```text
foo.svg#icon-a -> cache key foo.svg
foo.svg#icon-b -> same SvgDocument, different target id
```

Cache should store:

- source bytes metadata for reload/debug if needed
- parsed `Input` / `Element` tree
- id table
- intrinsic size
- load state and errors

Do not cache rendered pixels as the primary representation. Rendered/rasterized surfaces are viewport-size dependent and should be secondary caches keyed by size, pixel ratio, and style-relevant options.

## Migration Plan

### Phase 1: Parser Unification

- Add `html5_parse_svg_document()`.
- Update external SVG load path to use input-html instead of `parse_xml()`.
- Keep `rdt_picture_load()` as a compatibility wrapper initially.
- Add regression tests comparing inline SVG and external SVG with the same markup.

### Phase 2: Shared SvgDocument Resource

- Add `SvgDocument` / `SvgDocumentRef`.
- Route `<img src="*.svg">`, backgrounds, standalone `.svg`, and external `<use>` through the shared resource loader.
- Fix `process_image_resource()` so SVG never goes through raster image decoding.
- Implement external `<use href="external.svg#id">` by id-table lookup and render-by-reference.
- Route standalone `.svg` and `<img src="foo.svg">` through vector document rendering, with rasterization only as backend fallback.

### Phase 3: Renderer API Cleanup

- Introduce `svg_render_document()` / `svg_render_element()` as the real backend.
- Convert `render_inline_svg()` into a wrapper.
- Convert image/background/standalone SVG drawing into wrappers.
- Rename `render_svg(ImageSurface*)` to `svg_rasterize_to_surface()` or remove it from the common path.

### Phase 4: Style and Text Consistency

- Pass `FontContext*` explicitly through render options.
- Consolidate SVG text rendering with Radiant font APIs.
- Normalize `currentColor`, inherited fill/stroke, and presentation attribute handling.
- Support embedded `<style>` in external SVG using the same CSS parser/style resolver concepts as HTML, scoped to the external SVG document.
- Make inline SVG inheritance follow the HTML/CSS/SVG spec: inherited properties and matching page selectors apply because inline SVG is in the host document tree.
- Add tests for inline vs external text/font matching.

### Phase 5: Reference and Asset Support

- Finish external `<use>`.
- Route SVG `<image href="...">` through the same resource manager for raster and nested SVG resources.
- Support external gradients/masks only if required by test cases; otherwise reject or defer clearly.
- Add cycle detection for recursive `<use>` and URL references.

## Testing Strategy

Add paired tests where the same SVG appears in multiple forms:

- Inline `<svg>` in HTML.
- `<img src="same.svg">`.
- `background-image: url(same.svg)`.
- standalone `same.svg`.
- `<use href="same.svg#shape">`.

Test dimensions and rendering:

- `width`/`height` only
- `viewBox` only
- both `width`/`height` and `viewBox`
- `preserveAspectRatio="none"`, default meet, and slice
- paths, rects, circles, gradients, masks, clips
- text with font family, weight, style, and `currentColor`
- embedded `<style>` in inline SVG and external SVG image documents
- SVG `<image>` with PNG/JPEG/GIF/data URI content
- SVG `<image>` with nested external SVG content
- vector output preservation for standalone `.svg` and `<img src="foo.svg">` when rendering to SVG/PDF backends
- data URI SVG
- relative URL resolution from nested directories

Useful assertions:

- view tree intrinsic dimensions match between inline and external forms
- output SVG/PNG pixel comparison for representative fixtures
- id lookup works for internal and external `<use>`
- external `<use>` renders by reference without cloning into the host DOM
- external SVG embedded styles do not leak into the page and page selectors do not match external SVG image documents
- inline SVG follows normal host-document selector matching and inherited-property behavior
- network async completion schedules reflow/repaint once

## Cleanup Opportunities

1. **Rename files and functions around current intent**

   `render_svg_inline.cpp` now contains general SVG rendering logic, not just inline SVG. Rename or split into:

   - `svg_render.cpp`
   - `svg_parse.cpp`
   - `svg_resource.cpp`
   - `svg_intrinsic.cpp`

2. **Remove process-wide SVG font globals**

   Prefer explicit `FontContext*` in render options.

3. **Separate vector image from raster image**

   Stop modeling SVG primarily as `ImageSurface` with `format = IMAGE_FORMAT_SVG`. Keep a temporary compatibility path only where old render code requires it.

4. **Centralize id/defs handling**

   The renderer currently builds defs during render. Build a document-level id table once, then let render-time state handle inherited style and transforms.

5. **Make resource loading sync/async behavior explicit**

   Local files may load synchronously, HTTP resources may be async, but both should produce the same `SvgDocument` state machine.

6. **Remove duplicate SVG detection code**

   MIME/content/extension detection should live in one helper used by image loading, resource loading, CLI document loading, and data URI handling.

7. **Normalize fragment URL handling**

   Split URLs into `{base_url, fragment}` once and cache by base URL. Avoid treating `foo.svg#id` as a separate downloaded resource from `foo.svg#other`.

8. **Document unsupported SVG features**

   Filters, animation, scripting, foreignObject, external CSS, and some text layout details should have explicit behavior: supported, ignored, or TODO with warnings.

## Resolved Questions

- External SVG loaded as an image should support embedded `<style>` now, using the HTML CSS parser/style resolver where possible and scoping those styles to the external SVG document.
- Standalone `.svg` should render as a vector document directly to vector-capable backends, avoiding intermediate rasterization wherever possible.
- `<img src="foo.svg">` should render as vector into display lists/vector backends where possible. Rasterization is only a fallback for pixel-only targets or compatibility paths.
- Inline SVG should follow the HTML/CSS/SVG specs: it is part of the host document tree, page selectors can match it, and inherited properties inherit normally. External SVG image documents remain style-isolated from page selectors.
- External `<use>` should render by reference as the first implementation.

## Remaining Open Questions

- Which SVG filter subset should be prioritized first, if any?
- Should `foreignObject` be ignored, rasterized through nested HTML layout, or delayed until a broader embedded-document model exists?
- How far should SVG animation support go before the unified static renderer is complete?

## Target End State

At the end of this refactor, the pipeline should be simple to reason about:

```text
All SVG bytes/elements
  -> HTML5/SVG Element tree
  -> SvgDocument/SvgDocumentRef
  -> shared resource/cache/id resolver
  -> shared SVG renderer
  -> direct vector draw or explicit off-screen rasterization
```

Inline SVG and external SVG should differ only in source ownership, resource isolation, and placement context. They should not differ in parser semantics, attribute normalization, text/font APIs, or drawing backend behavior.