# Radiant

Radiant is a native HTML/CSS/SVG renderer built from scratch in C, designed as a sub-project of the Lambda runtime. It provides a complete rendering engine for web content without relying on existing browser engines.

## Overview

Radiant implements a full-featured rendering pipeline that can parse HTML documents, apply CSS styling, layout content using modern CSS features including Flexbox, and render the final output with hardware-accelerated graphics.

## Key Features

### HTML/CSS Support
- **HTML Parsing**: Built on the Lexbor HTML parser for fast and compliant HTML document processing
- **CSS Engine**: Full CSS parsing and style resolution using Lexbor CSS
- **DOM Tree**: Complete Document Object Model implementation for HTML elements

### Layout Engine
- **Block Layout**: Traditional block-level element layout with margin collapsing
- **Inline Layout**: Text flow with proper line breaking and vertical alignment
- **Flexbox Layout**: CSS Flexbox implementation with:
  - Flex direction (row, column, reverse variants)
  - Flex wrapping and line distribution
  - Justify content (start, end, center, space-between, space-around, space-evenly)
  - Align items and align content
  - Flex grow/shrink calculations
  - Order property support
  - Percentage-based flex basis

### Text Rendering
- **Font Management**: FontConfig integration for system font discovery
- **Typography**: FreeType integration for high-quality text rendering
- **Text Layout**: Advanced text positioning with baseline alignment
- **Font Styling**: Support for font families, sizes, weights, and styles

### Graphics and Media
- **SVG Rendering**: ThorVG integration for scalable vector graphics
- **Image Support**: STB_image for loading PNG, JPEG, and other bitmap formats
- **Hardware Acceleration**: OpenGL-based rendering through GLFW
- **Color Management**: Full RGBA color support with alpha blending

### Interactive Features
- **Event System**: Mouse and keyboard event handling
- **Scrolling**: Custom scrollbar implementation with overflow management
- **UI State**: Cursor tracking, selection, and interaction state management

## Architecture

### Core Components

#### Document Model (`dom.h`)
- `Document`: Container for parsed HTML with DOM tree and view tree
- URL parsing and resolution for resource loading
- Document lifecycle management

#### View System (`view.h`)
- `View`: Base class for all renderable elements
- `ViewText`: Text content rendering
- `ViewSpan`: Inline element containers
- `ViewBlock`: Block-level element containers
- `ViewGroup`: Container for child views

#### Layout Engine (`layout.c`, `layout_*.c`)
- `LayoutContext`: Layout computation state
- Font metrics and text measurement
- Block flow layout algorithms
- Flexbox layout implementation in `layout_flex.c`

#### Rendering Pipeline (`render.c`)
- `RenderContext`: Rendering state and clipping
- Glyph rasterization and text drawing
- Background and border rendering
- SVG and image compositing

#### Memory Management
- Custom memory pools for efficient allocation
- View tree recycling and cleanup
- Image caching system

### Dependencies

- **Lexbor**: HTML/CSS parsing and DOM manipulation
- **FreeType**: Font rasterization and text metrics
- **FontConfig**: System font discovery and matching
- **ThorVG**: SVG rendering and vector graphics
- **GLFW**: Window management and OpenGL context
- **STB**: Image loading (stb_image)
- **zlog**: Logging system

## File Structure

```
radiant/
├── dom.h              # Document and DOM definitions
├── view.h             # View system and rendering structures
├── layout.h           # Layout engine interface
├── render.h           # Rendering context and functions
├── flex.h             # Flexbox layout definitions
├── event.h            # Event system definitions
├── handler.h          # Event handling interface
│
├── parse_html.c       # HTML document parsing
├── layout.c           # Main layout algorithms
├── layout_block.c     # Block layout implementation
├── layout_flex.c      # Flexbox layout engine
├── layout_flex_nodes.c # Flex node management
├── layout_text.c      # Text layout and line breaking
├── render.c           # Main rendering pipeline
├── resolve_style.c    # CSS style resolution
├── surface.c          # Image loading and caching
├── window.c           # Window management and main loop
├── ui_context.c       # UI state management
├── view_pool.c        # Memory pool for views
├── font.c             # Font loading and metrics
├── event.c            # Event processing
└── scroller.c         # Scrollbar implementation
```

## Usage

The main entry points are:

1. **Document Loading**: `load_html_doc()` to parse HTML files
2. **Layout**: `layout_html_doc()` to compute element positions
3. **Rendering**: `render_html_doc()` to draw the final output

The rendering pipeline follows these steps:
1. Parse HTML document into DOM tree
2. Resolve CSS styles for all elements
3. Build view tree from styled DOM
4. Perform layout calculations (block, inline, flex)
5. Render views to graphics surface
6. Handle user interaction events

## Performance Features

- **Memory Pools**: Efficient allocation for temporary layout data
- **View Recycling**: Reuse view objects during reflows
- **Image Caching**: Cache decoded images to avoid repeated loading
- **Clipping**: Optimize rendering by skipping out-of-bounds content
- **Hardware Acceleration**: Use GPU for final compositing

## CSS Support Status

### Implemented
- Box model (margin, padding, border)
- Display properties (block, inline, inline-block, flex)
- Positioning (static, absolute)
- Flexbox (complete implementation)
- Text styling (fonts, colors, alignment)
- Background colors and images
- List styling
- Overflow and scrolling

### In Development
- CSS Grid layout
- Transforms and animations
- Advanced selectors
- Media queries

Radiant represents a modern approach to web content rendering, built for performance and extensibility while maintaining standards compliance.