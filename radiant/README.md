# Radiant

Radiant is Lambda’s HTML/CSS/SVG layout and rendering subsystem.

It is built from scratch in C/C++ and compiled into `lambda.exe` by default, providing:
- browser-style layout (block/inline, flexbox, grid, table)
- render targets (SVG/PDF/PNG/JPEG)
- an interactive viewer that can open multiple document formats.

## Overview

Radiant takes a DOM-like tree with computed styles, performs layout in CSS pixels (with pixel-ratio aware rendering), and produces a view tree that can be rendered to a window or exported to files.

Within this repo, Radiant is the engine behind the Lambda CLI commands:
- `lambda.exe layout` (layout analysis)
- `lambda.exe render` (render to SVG/PDF/PNG/JPEG)
- `lambda.exe view` (interactive viewer).

## Key Features

### Layout engine
- **Block/inline formatting contexts** with text measurement and line breaking.
- **Flexbox** multipass algorithm and baseline handling.
- **CSS Grid** track sizing + item placement.
- **Tables** with table structure building and cell layout.

### Styling + computed values
- **CSS cascade and computed style resolution** (including unit resolution and inheritance).
- **Pixel ratio support**: CSS pixels are resolved to physical pixels for accurate HiDPI output.

### Rendering + export
- **SVG export** via `render_html_to_svg()`.
- **PDF export** via `render_html_to_pdf()`.
- **Raster export** via `render_html_to_png()` / `render_html_to_jpeg()`.
- **Interactive window viewer** (scrolling, input events) via `view_doc_in_window()`.

### Multi-format viewing (via Lambda)
The viewer supports a broader set of document types through Lambda’s parsing/conversion pipeline, including:
- HTML (`.html`, `.htm`)
- XML (`.xml`, treated as HTML)
- Markdown (`.md`, `.markdown`)
- Wiki (`.wiki`)
- LaTeX (`.tex`, converted to HTML)
- PDF (`.pdf`)
- Lambda script output (`.ls`, evaluated and rendered).

## Using Radiant (recommended)

Build once, then use the Lambda CLI:

```bash
make build

./lambda.exe view test/html/index.html
./lambda.exe layout test/html/index.html
./lambda.exe render test/html/index.html -o out.svg
./lambda.exe render test/html/index.html -o out.pdf
./lambda.exe render test/html/index.html -o out.png
```

See `./lambda.exe <command> --help` for detailed options.

## Developer entry points

If you’re working inside the C/C++ codebase, the key functions are:
- `layout_html_doc()` — performs layout and builds the view tree
- `render_html_doc()` — renders a laid-out view tree
- `render_html_to_svg()` / `render_html_to_pdf()` / `render_html_to_png()` / `render_html_to_jpeg()` — headless exports
- `view_doc_in_window()` — interactive viewer window

## File map (current)

Radiant’s implementation in this repo lives under `radiant/`:

- `view.hpp` — view tree types and core rendering structs
- `layout*.cpp`, `intrinsic_sizing*.{cpp,hpp}` — layout algorithms (block/inline/flex/grid/table)
- `resolve_css_style.cpp` — cascade + computed style resolution
- `render*.cpp` — rendering backends (SVG/PDF/raster)
- `window.cpp`, `event.cpp` — interactive viewer and input handling
- `pdf/` — PDF parsing/rendering helpers

## Dependencies (high level)

Radiant relies on a small set of native libraries (installed via the repo’s setup scripts):
- **GLFW** (window + OpenGL context)
- **FreeType** (font metrics and text measurement)
- **FontConfig** (font discovery/matching)
- **ThorVG** (vector drawing, including SVG-related rendering paths)

For the broader “open many formats” story, the Lambda side provides parsing/conversion.
