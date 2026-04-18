# WebGL Support for Radiant — Analysis

There are three distinct interpretations of "WebGL support." Here's what each would require.

## Current Rendering Architecture

Radiant uses a **CPU-based multi-backend architecture**:

| Backend | Purpose | Key Files |
|---------|---------|-----------|
| **ThorVG (Software Rasterizer)** | Primary raster output (PNG, JPEG) | `radiant/rdt_vector_tvg.cpp` |
| **Core Graphics (macOS)** | Native macOS rendering | `radiant/rdt_vector_cg.mm` |
| **SVG Backend** | Vector SVG output | `radiant/render_svg.cpp` |
| **PDF Backend** | PDF document output (libharu) | `radiant/render_pdf.cpp` |
| **DVI Backend** | TeX output format | `radiant/render_dvi.cpp` |

All rendering is CPU-based. ThorVG ships with an **OpenGL engine (~8.6k lines)** and **WebGPU engine (~6.6k lines)** but both are currently **excluded** from the build. The `RdtVector` abstraction (`radiant/rdt_vector.hpp`) cleanly isolates the rendering backend. GLFW already creates an OpenGL context in `radiant/ui_context.cpp`.

Current release binary: **~8 MB**.

---

## Option A: GPU-Accelerated Rendering Backend (OpenGL ES / WebGL-compatible)

Replace or complement the CPU-based ThorVG software rasterizer with GPU rendering.

### Work Needed

| Component | Estimated Code | Notes |
|-----------|---------------|-------|
| Enable ThorVG GL engine | ~0 new lines | Already exists, add to build config |
| New `rdt_vector_gl.cpp` backend | ~1.5–2.5k lines | Map `rdt_*` API to ThorVG GL canvas |
| Shader programs (if custom) | ~500–1k lines | 2D fill/stroke/gradient/text shaders |
| GPU glyph atlas | ~800–1.2k lines | Upload FreeType glyphs to GPU textures |
| Framebuffer management | ~500–800 lines | For clipping, opacity layers, filters |
| Display list GPU replay | ~600–1k lines | Parallel to existing CPU replay path |
| **Total new code** | **~4–6.5k lines** | |

### Binary Size Impact

+200–400 KB (ThorVG GL engine + shaders + GL loader). The OpenGL driver itself is OS-provided.

### Advantages

- 10–50x faster raster rendering for interactive `view` command
- `RdtVector` abstraction makes this a clean slot-in
- ThorVG GL engine already exists in the source tree
- GLFW already sets up the GL context

---

## Option B: WebGL API in Lambda's JS Runtime (`<canvas>` + `getContext('webgl')`)

Support WebGL draw calls from JavaScript inside `<canvas>` elements — i.e., behave like a browser.

### Work Needed

| Component | Estimated Code | Notes |
|-----------|---------------|-------|
| `<canvas>` DOM element support | ~800–1.2k lines | Sizing, pixel buffer, compositing into view tree |
| Canvas 2D context (`getContext('2d')`) | ~3–5k lines | Would likely come first; drawRect, fillText, etc. |
| WebGL 1.0 API bindings | ~6–10k lines | ~300 functions: bindBuffer, bindTexture, drawArrays, etc. |
| GLSL ES shader compiler integration | ~500 lines (wrapper) | Use ANGLE or Mesa's GLSL compiler (~2–5 MB dep) |
| Texture/buffer/framebuffer management | ~2–3k lines | Resource lifecycle, format conversion |
| JS↔GPU data marshalling | ~1–2k lines | TypedArray to GL buffer uploads |
| WebGL 2.0 extensions (optional) | ~3–5k lines | Additional API surface |
| **Total new code** | **~15–25k lines** | Excluding shader compiler dependency |

### Binary Size Impact

+500 KB–1 MB for bindings alone. If bundling ANGLE for shader compilation: **+3–6 MB**. If relying on native GL driver's GLSL support: +500 KB–1 MB total.

### Note

This is essentially reimplementing a browser subsystem — very high effort for limited payoff given Lambda already has its own rendering pipeline.

---

## Option C: WebGL via WASM (Browser Target)

When `lambda.exe` is compiled to WASM (`lambda/lambda-wasm-main.c` exists), render to a WebGL canvas in the browser.

### Work Needed

| Component | Estimated Code | Notes |
|-----------|---------------|-------|
| Emscripten GL target for ThorVG | ~500–1k lines | ThorVG's GL engine compiles under Emscripten |
| WASM↔WebGL bridge | ~300–600 lines | Emscripten auto-maps GL→WebGL |
| Canvas/surface management | ~400–800 lines | Handle resize, DPI, compositing |
| Event forwarding (browser→WASM) | ~500–1k lines | Mouse, keyboard, touch |
| **Total new code** | **~2–3.5k lines** | |

### Binary Size Impact

WASM binary grows ~150–300 KB (GL state + shaders). Browser provides WebGL runtime.

---

## Summary Comparison

| Option | Code Growth | Binary Growth | Effort | Value |
|--------|-------------|---------------|--------|-------|
| **A. GPU backend** | ~4–6.5k lines | +200–400 KB | Medium | High — 10–50x faster raster rendering |
| **B. WebGL JS API** | ~15–25k lines | +0.5–6 MB | Very High | Low — niche use case |
| **C. WASM+WebGL** | ~2–3.5k lines | +150–300 KB | Low-Medium | Medium — enables browser deployment |

## Recommendation

**Option A is the best ROI.** The `RdtVector` abstraction makes it straightforward, ThorVG's GL engine already exists in the source tree, and GLFW already sets up the GL context. GPU-accelerated rendering for the interactive `view` command with minimal binary growth (~3–5% of current 8 MB).

**Option C is the easiest** if browser deployment matters — Emscripten handles most of the GL→WebGL translation automatically.

**Option B (full WebGL API) is essentially building a browser feature** — avoid unless there's a specific need for user-authored WebGL shaders in Lambda pages.
