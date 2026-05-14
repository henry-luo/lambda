# Radiant-ThorVG Source Subtree Integration Proposal

**Date:** May 2026  
**Status:** Proposal  
**Depends on:** `Radiant_Design_ThorVG_Integration.md`

---

## 1. Executive Summary

Radiant has already isolated ThorVG behind `RdtVector` and moved SVG document rendering into Radiant's own view/render pipeline. The next step is to stop treating ThorVG as an external static library and instead vendor the ThorVG source under Radiant as a source subtree, then build only the parts Radiant actually needs.

The goal is not to make ThorVG another browser subsystem. The goal is narrower:

> **Radiant manages the SVG view tree and all resources. ThorVG is used only as a vector rasterization engine for Radiant-owned vector views.**

This proposal replaces the current `libthorvg.a` integration with a curated, source-level integration that:

1. Builds ThorVG from source inside the Radiant build.
2. Uses ThorVG's C++ API internally, not the C API.
3. Excludes ThorVG's SVG loader, CSS handling, image loading, text layout, and font resolution from Radiant's rendering path.
4. Keeps `RdtVector` as Radiant's public vector rendering boundary.
5. Allows us to patch, reduce, and eventually extract the software vector renderer with much less friction.

---

## 2. Current State

Stage 1 has already made the most important architectural move: Radiant rendering files no longer depend directly on ThorVG types or `tvg_*` calls. They render through `rdt_*` functions.

Current state after Stage 1 and Stage 1.5:

- Radiant rendering code calls `RdtVector` APIs.
- SVG DOM parsing, intrinsic sizing, viewBox handling, style inheritance, and SVG element dispatch are Radiant-owned.
- External SVG files and SVG data URIs are parsed by Radiant, not by ThorVG's SVG loader.
- Text font selection is Radiant-owned, using Radiant's `FontContext` and `font_find_best_match()` path.
- ThorVG remains linked as an external static library.
- The remaining ThorVG bridge exists mostly to rasterize vector shapes and to support temporary text/image paint paths.

That puts us in a good position to remove the static-library dependency. The remaining problem is that linking the full ThorVG static library still pulls in code and ownership assumptions Radiant no longer wants: SVG loading, image loading, CSS parsing, text handling, animation infrastructure, optional engines, saver code, and retained scene-graph behavior.

---

## 3. Design Decision

**Vendor ThorVG as a Radiant-owned source subtree and build a minimal ThorVG renderer target.**

The subtree should live under Radiant's source tree, for example:

```text
lambda/radiant/third_party/thorvg/
```

or, if we want a clearer ownership boundary:

```text
lambda/radiant/vector/thorvg/
```

The exact path can be decided during implementation, but the important rule is that ThorVG becomes source that Radiant builds directly, not a separately built static dependency copied into `mac-deps/`, `win-native-deps/`, or `/usr/local/lib`.

Radiant should compile a custom ThorVG subset target, tentatively named:

```text
radiant_thorvg_min
```

This target is private to Radiant. It is not a general ThorVG distribution and should not expose ThorVG headers to the rest of the application.

---

## 4. Ownership Boundaries

### 4.1 Radiant Owns Text

Radiant remains responsible for all text behavior:

- Font discovery and fallback
- CSS `font-family`, `font-weight`, `font-style`, and `font-size` resolution
- SVG text inheritance
- Unicode handling
- Text shaping path decisions
- Glyph positioning
- HTML document text rendering
- SVG `<text>` element layout semantics

ThorVG should not decide which font face to use. It should not parse CSS font lists. It should not resolve bold/italic variants. It should not own document text layout.

For Stage 2, there are two acceptable implementation paths:

1. **Short-term bridge:** Radiant resolves font paths and may still create ThorVG text paints internally for SVG `<text>` until a better vector-glyph path exists.
2. **Preferred direction:** Radiant converts SVG text into glyph outlines or rasterized glyph runs and passes those to `RdtVector` as paths, masks, or image spans.

The long-term target is clear: ThorVG should not own text semantics. If ThorVG text classes remain temporarily compiled, they are implementation helpers, not the architecture.

### 4.2 Radiant Owns Resource Loading

Radiant handles every external or embedded resource:

- SVG files
- SVG data URIs
- Raster images in SVG `<image>`
- CSS style blocks and inline style attributes
- `@font-face` rules
- Network fetching
- File path resolution
- MIME sniffing
- Data URI decoding
- Caching
- Security policy and sandboxing

ThorVG's loaders should not be used for Radiant document resources.

In particular, the custom ThorVG build should aim to exclude or disable:

- `loaders/svg/`
- CSS/style parsing used by ThorVG's SVG loader
- ThorVG image loaders unless temporarily needed by a bridge path
- Lottie and animation loaders
- saver modules

If SVG `<image>` still needs ThorVG image primitives temporarily, Radiant should load and decode the image first, then pass decoded pixels to `RdtVector`. ThorVG should not be given arbitrary URLs or document resource strings.

### 4.3 Radiant Owns the SVG View Tree

This is the central design rule.

Radiant parses SVG into its own DOM/view representation and walks that tree during rendering. ThorVG does not build or retain an SVG scene tree for Radiant documents.

Radiant owns:

- Element traversal
- `<svg>` viewport handling
- `viewBox` and `preserveAspectRatio`
- Group transforms
- `<use>` resolution
- Paint inheritance
- Fill/stroke style computation
- Gradient and pattern mapping
- Clip path and mask resolution
- Text/image element interpretation
- Dirty-region and invalidation decisions

ThorVG owns only low-level vector drawing once Radiant has already decided what to draw.

The render flow should be:

```text
SVG/XML input
  -> Radiant XML parser
  -> Radiant SVG element/view tree
  -> Radiant style/resource resolution
  -> Radiant render traversal
  -> RdtVector commands
  -> ThorVG C++ renderer backend
  -> pixels
```

The render flow should not be:

```text
SVG/XML input
  -> ThorVG SVG loader
  -> ThorVG scene tree
  -> ThorVG-owned text/image/style behavior
  -> pixels
```

---

## 5. API Direction: Use ThorVG C++ Internally

The current backend uses ThorVG's C API because Radiant previously linked ThorVG as an external static dependency and needed a stable C boundary.

Once ThorVG source is vendored into Radiant, that constraint goes away. The backend should integrate with ThorVG through the C++ API directly.

### 5.1 Public Boundary Remains `RdtVector`

The C++ API is an implementation detail. Radiant rendering code should still include only Radiant headers:

```cpp
#include "rdt_vector.hpp"
```

Rendering files should continue to call APIs like:

```cpp
rdt_fill_path(&ctx->vec, path, color, fillRule, transform);
rdt_stroke_path(&ctx->vec, path, color, width, cap, join, dash, dashCount, transform);
rdt_draw_image(&ctx->vec, pixels, w, h, stride, x, y, dstW, dstH, opacity, transform);
```

ThorVG C++ headers should be included only inside the ThorVG backend implementation, for example:

```text
radiant/rdt_vector_tvg.cpp
radiant/third_party/thorvg_min_build.cpp
```

or their eventual replacement files.

### 5.2 Why C++ API Instead of C API

Using the C++ API gives us:

- Direct access to ThorVG's native types without C wrapper overhead.
- Easier patching when ThorVG internals need small changes.
- Better control over build flags and feature exclusion.
- Fewer compatibility constraints from the public C API surface.
- A cleaner path toward bypassing ThorVG `Canvas`/`Scene` abstractions later if we choose to integrate closer to the software renderer.

This does not mean exposing ThorVG C++ types to Radiant. It means the backend can use ThorVG naturally while `RdtVector` protects the rest of the codebase.

---

## 6. Proposed Source Layout

A possible layout:

```text
lambda/radiant/
  rdt_vector.hpp
  rdt_vector_tvg.cpp
  render_svg_inline.cpp
  third_party/
    thorvg/
      upstream/
        src/
          common/
          renderer/
          renderer/sw_engine/
          ...
        inc/
        LICENSE
        README.md
      radiant/
        thorvg_config.h
        thorvg_min_sources.cmake
        patches/
```

The `upstream/` directory should be kept close to an upstream ThorVG snapshot. Radiant-specific build files and patches should live outside the upstream source tree when possible.

If we use `git subtree`, the upstream import can be managed as:

```text
lambda/radiant/third_party/thorvg/upstream
```

with Radiant build glue next to it.

---

## 7. Custom Build Scope

The first custom build should be conservative: include enough ThorVG source to preserve current rendering behavior, then reduce in measured passes.

### 7.1 Keep Initially

Likely required in the first source build:

- `src/common/`
- ThorVG renderer core
- ThorVG software engine
- shape/path/fill/stroke support
- transform and matrix utilities
- gradient support
- clipping/masking primitives currently used by `RdtVector`
- image paint support only if `RdtVector` still maps images through ThorVG picture/image internals
- text paint support only if SVG `<text>` still temporarily uses ThorVG text objects

### 7.2 Exclude Immediately If Possible

These should not be part of Radiant's normal rendering path:

- ThorVG SVG loader
- ThorVG CSS/style parser
- Lottie loader
- animation framework not needed for immediate rendering
- saver/export modules
- GL engine
- WGPU engine
- image file loaders once Radiant passes decoded pixels directly
- ThorVG C API wrapper files once `rdt_vector_tvg.cpp` is migrated to C++

### 7.3 Reduce in Stages

The initial source build should not attempt heroic surgery. After behavior matches the static-library build, remove unused ThorVG modules in small, testable steps.

Recommended order:

1. Build full required C++ renderer subset from source.
2. Switch `rdt_vector_tvg.cpp` from C API to C++ API.
3. Remove ThorVG C API compilation.
4. Remove ThorVG SVG loader compilation.
5. Replace SVG `<image>` bridge with Radiant-decoded pixels.
6. Replace SVG `<text>` bridge with Radiant-owned glyph rendering.
7. Remove ThorVG image loaders and text classes if no longer referenced.
8. Investigate bypassing ThorVG Canvas/Scene for a lower-level software renderer integration.

---

## 8. Backend Architecture

The intended architecture after this proposal:

```text
Radiant DOM / SVG View Tree
  |
  | render traversal, style resolution, resource resolution
  v
RdtVector API
  |
  | immediate-mode vector commands
  v
RdtVector ThorVG C++ backend
  |
  | private ThorVG C++ calls
  v
ThorVG minimal software renderer
  |
  v
Radiant pixel buffer
```

`RdtVector` remains immediate-mode from Radiant's perspective. Even if the ThorVG backend temporarily uses ThorVG `Canvas`, `Shape`, `Picture`, or `Scene` internally, that behavior stays hidden.

Eventually, we may replace the ThorVG canvas-level implementation with a thinner adapter around ThorVG's software renderer internals. That is a later optimization, not a requirement for the subtree migration.

---

## 9. SVG Rendering Model

Radiant's SVG rendering model should be explicit and stable:

```text
render_svg_to_vec()
  -> render_svg_element()
     -> render_svg_group()
     -> render_svg_rect()
     -> render_svg_circle()
     -> render_svg_path()
     -> render_svg_text()
     -> render_svg_image()
     -> ...
```

Each renderer computes Radiant-owned state and emits `RdtVector` commands.

Examples:

- A `<path>` becomes an `RdtPath` plus fill/stroke commands.
- A `<linearGradient>` becomes `rdt_fill_linear_gradient()` with Radiant-resolved stops and transform.
- A `<clipPath>` becomes `rdt_push_clip()` / `rdt_pop_clip()` around child rendering.
- A `<text>` becomes Radiant-resolved text/glyph drawing, not ThorVG font-family matching.
- An `<image>` becomes a Radiant-loaded decoded image passed to `rdt_draw_image()`.

ThorVG should never be asked to parse an SVG document, interpret CSS, load an image URL, or resolve a font family for an SVG element.

---

## 10. Resource Loading Model

Radiant's resource pipeline should normalize all external inputs before rendering.

For SVG `<image>`:

```text
href/src
  -> Radiant URL/data URI resolver
  -> Radiant MIME sniffing and policy checks
  -> Radiant image decoder
  -> ImageSurface / decoded pixel buffer
  -> rdt_draw_image()
```

For nested SVG images:

```text
href/src
  -> Radiant URL/data URI resolver
  -> Radiant XML parser
  -> Radiant SVG view tree
  -> render_svg_to_vec() into target or intermediate surface
```

For CSS:

```text
style attribute / style element / external sheet
  -> Radiant CSS parser
  -> Radiant cascade/inheritance
  -> computed SVG paint/text/layout properties
  -> RdtVector drawing commands
```

This keeps all browser-like behavior in Radiant, where it can share policy, caching, and semantics with HTML/CSS rendering.

---

## 11. Migration Plan

### Phase 1: Import ThorVG as a Source Subtree

1. Choose upstream ThorVG commit matching the current dependency.
2. Import ThorVG under `lambda/radiant/third_party/thorvg/upstream` using `git subtree` or an equivalent source-vendor process.
3. Preserve upstream license and notices.
4. Add a Radiant-local build manifest listing ThorVG source files.
5. Build the initial source target without changing rendering behavior.

Exit criteria:

- Radiant builds ThorVG from source.
- Existing `libthorvg.a` lookup is no longer required for the Radiant target.
- Rendering tests match the current static-library integration.

### Phase 2: Switch Backend to ThorVG C++ API

1. Replace `thorvg_capi.h` usage inside `rdt_vector_tvg.cpp` with ThorVG C++ headers.
2. Replace `Tvg_Canvas`, `Tvg_Paint`, and C wrapper calls with `tvg::Canvas`, `tvg::Shape`, `tvg::Picture`, or equivalent C++ types.
3. Remove C API bridge functions from `rdt_vector.hpp`.
4. Keep all ThorVG C++ includes private to the backend.

Exit criteria:

- No Radiant header includes ThorVG C or C++ public headers.
- No `tvg_*` C API calls are needed by Radiant.
- `RdtVector` callers are unchanged.

### Phase 3: Remove ThorVG SVG Loader from the Build

1. Confirm all SVG file/data paths use Radiant parsing.
2. Remove `loaders/svg/` from the ThorVG source build.
3. Remove any now-unused patch files that only targeted ThorVG's SVG loader.
4. Keep equivalent behavior covered in Radiant SVG tests.

Exit criteria:

- ThorVG SVG loader is not compiled.
- Radiant SVG tests still pass.
- Existing nested viewport, font inheritance, and text-anchor fixes are represented in Radiant code/tests, not ThorVG loader patches.

### Phase 4: Move SVG Image Handling Fully to Radiant

1. Ensure data URI decoding and MIME sniffing are Radiant-owned.
2. Decode raster images before reaching `RdtVector`.
3. Render nested SVG images through Radiant's SVG renderer.
4. Replace any ThorVG `Picture::load()` image bridge with `rdt_draw_image()` or a Radiant-owned image surface path.

Exit criteria:

- ThorVG image loaders are not required for SVG `<image>`.
- ThorVG receives decoded pixels or vector paths only.

### Phase 5: Move SVG Text Handling Fully to Radiant

1. Resolve SVG text properties through Radiant style code.
2. Convert text to Radiant glyph runs, glyph outlines, masks, or raster spans.
3. Draw the result through `RdtVector` primitives.
4. Remove dependency on ThorVG text/font APIs.

Exit criteria:

- ThorVG does not load fonts for SVG text.
- ThorVG text classes are not required by Radiant.
- SVG text rendering remains visually equivalent or improves against browser baselines.

### Phase 6: Minimize ThorVG Renderer Source

1. Remove unused loaders and optional engines.
2. Remove unused retained-scene features only after profiling and test coverage are adequate.
3. Consider a direct software-renderer adapter if Canvas/Scene overhead remains significant.

Exit criteria:

- The custom ThorVG subtree contains only the renderer functionality Radiant needs.
- The `RdtVector` API remains stable.

---

## 12. Build System Notes

Radiant's build should move from external library discovery to explicit source compilation.

Before:

```text
Radiant -> link libthorvg.a -> ThorVG C API
```

After:

```text
Radiant -> compile radiant_thorvg_min sources -> ThorVG C++ API inside backend
```

Recommended build properties:

- Static source target private to Radiant.
- No exported ThorVG include directories outside backend files.
- Feature flags disabling loaders and engines Radiant does not use.
- One source manifest per platform if necessary, but shared where possible.
- Reproducible upstream commit recorded in a small metadata file.
- Patches kept either as local commits in the subtree or as explicit patch files with a documented apply order.

The build should fail if a non-backend Radiant file includes ThorVG headers. This can be enforced later with a grep-based CI check.

---

## 13. Testing Strategy

The subtree migration is risky enough that it should be guarded by pixel and behavior tests.

Required checks:

- Existing Lambda tests.
- Existing Radiant layout/render baseline tests.
- SVG-specific render baselines covering:
  - nested `<svg>` viewport and viewBox behavior
  - `preserveAspectRatio`
  - grouped transforms
  - `<use>`
  - fill/stroke inheritance
  - gradients
  - clipping
  - SVG text font weight/style/family resolution
  - `text-anchor`
  - SVG `<image>` with raster data URI
  - nested SVG image resource

For each reduction phase, compare against the current static-library build before removing more ThorVG source.

---

## 14. Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| ThorVG internals are more coupled than expected | Medium | Start with a conservative source build, then reduce incrementally |
| C++ API changes across ThorVG updates | Medium | Vendor a known upstream commit and update deliberately |
| Accidentally reintroducing ThorVG SVG/resource ownership | High | Keep ThorVG headers private and add CI checks for loader/API usage |
| Text rendering regressions | High | Move text in a separate phase with focused SVG text baselines |
| Image decoding regressions | Medium | Route all image data through Radiant's existing surface/resource pipeline |
| Larger short-term source tree | Low | Accept during transition; reduction comes after behavioral parity |
| Patch drift from upstream | Medium | Keep upstream subtree clean and Radiant changes documented as small local patches |

---

## 15. Non-Goals

This proposal does not require:

- Replacing ThorVG with Core Graphics or Direct2D immediately.
- Extracting `sw_engine` directly in the first step.
- Rewriting all vector rasterization code.
- Changing the public `RdtVector` API for callers.
- Letting ThorVG become the SVG engine again.

Platform-native backends remain a future option. The subtree migration is useful either way because it makes the current ThorVG dependency smaller, patchable, and explicitly subordinate to Radiant's rendering model.

---

## 16. Final Target

The final architecture should look like this:

```text
Radiant
  owns: DOM, SVG tree, CSS, fonts, text, images, resources, layout, invalidation

RdtVector
  owns: Radiant's stable vector drawing API

ThorVG subtree
  owns: low-level vector rasterization implementation only
```

ThorVG should become a renderer component, not a document engine.

The most important invariant is:

> **Radiant decides what vector views exist and how they are styled. ThorVG only draws the already-decided vector geometry into pixels.**
