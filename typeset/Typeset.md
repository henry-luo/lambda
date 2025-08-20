# Lambda Typesetting System

## Overview

The Lambda Typesetting System is a modern typesetting engine integrated into the Lambda language that produces high-quality document layouts with precise mathematical typography. The system follows a device-independent architecture where layout computation produces a view tree that can be rendered to multiple output formats.

## Architecture

The typesetting system is built around a **device-independent view tree** that serves as an intermediate representation between document structure and final output. This design provides:

- **Single Source of Truth**: Layout is computed once and can be rendered to multiple formats
- **Device Independence**: All coordinates use typographical points (1/72 inch) for consistency
- **Programmability**: View trees are accessible as Lambda data structures for manipulation
- **High Precision**: Exact positioning for professional-quality mathematical typography

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Input Layer   │───▶│  Typeset Engine  │───▶│   View Tree     │───▶│  Output Layer   │
│ (Lambda/MD/TeX) │    │     (Core)       │    │ (Device-Indep.) │    │   (Multiple)    │
└─────────────────┘    └──────────────────┘    └─────────────────┘    └─────────────────┘
         │                       │                       │                       │
    ┌────▼─────┐         ┌───────▼────────┐      ┌───────▼────────┐      ┌───────▼────────┐
    │ Parsers  │         │  Layout Engine │      │  View Nodes    │      │   Renderers    │
    │ - Math   │         │  - Typography  │      │  - Positioned  │      │  - HTML        │
    │ - MD/LaTeX│        │  - Page Layout │      │  - Styled      │      │  - SVG         │
    │ - HTML   │         │  - Math Layout │      │  - Measured    │      │  - PDF         │
    └──────────┘         └────────────────┘      └────────────────┘      │  - PNG         │
                                                                         │  - TeX/LaTeX   │
                                                                         └────────────────┘
```

## Core Components

### View Tree System

The view tree is the central data structure representing a document's layout in device-independent coordinates.

#### View Node Types

```c
typedef enum {
    VIEW_NODE_DOCUMENT,         // Root document node
    VIEW_NODE_PAGE,             // Page node
    VIEW_NODE_BLOCK,            // Block-level element
    VIEW_NODE_INLINE,           // Inline element  
    VIEW_NODE_TEXT_RUN,         // Positioned text with specific font/style
    VIEW_NODE_MATH_ELEMENT,     // Mathematical element with precise positioning
    VIEW_NODE_GLYPH,            // Individual glyph with exact positioning
    VIEW_NODE_LINE,             // Geometric line
    VIEW_NODE_RECTANGLE,        // Geometric rectangle
    VIEW_NODE_PATH,             // Complex geometric path
    VIEW_NODE_GROUP,            // Grouping container
    VIEW_NODE_TRANSFORM,        // Transform container
    VIEW_NODE_CLIPPING          // Clipping region
} ViewNodeType;
```

#### Device-Independent Coordinates

All measurements use typographical points (1/72 inch) with double precision:

```c
typedef struct ViewPoint {
    double x, y;                // Double precision coordinates
} ViewPoint;

typedef struct ViewSize {
    double width, height;       // Double precision dimensions
} ViewSize;

typedef struct ViewRect {
    ViewPoint origin;           // Top-left corner
    ViewSize size;              // Width and height
} ViewRect;
```

### Multi-Format Output System

The system supports multiple output formats through a unified renderer interface:

- **HTML**: Semantic HTML5 with CSS styling for web display
- **SVG**: High-quality vector graphics for scalable output
- **PDF**: Print-ready documents with embedded fonts
- **PNG**: Raster images with configurable DPI
- **TeX/LaTeX**: LaTeX source code generation for further processing
- **Markdown**: Lightweight markup for content extraction

### Mathematical Typography

Advanced mathematical typesetting with:

- Precise positioning and spacing according to mathematical rules
- Support for fractions, superscripts, subscripts, radicals, matrices
- Multiple math input formats (TeX, ASCII, Lambda expressions)
- Device-independent mathematical metrics

### Lambda Integration

View trees can be serialized as Lambda element trees for inspection and manipulation:

```lambda
<view-tree title:"My Document" pages:3 creator:"Lambda Typesetting System">
  <page number:1 width:595.276 height:841.89>
    <block x:72 y:72 width:451.276 height:20 role:"heading">
      <text-run font:"Times-Bold" size:16 color:[0,0,0,1]>
        "Introduction"
      </text-run>
    </block>
    
    <math-block x:72 y:190 width:451.276 height:40 style:"display">
      <math-fraction axis-height:6.8>
        <math-numerator width:12 height:14>
          <math-symbol class:"variable">x</math-symbol>
        </math-numerator>
        <math-denominator width:12 height:14>
          <math-number>2</math-number>
        </math-denominator>
      </math-fraction>
    </math-block>
  </page>
</view-tree>
```

## API Reference

### Core Typesetting Functions

```c
// Main typesetting function - produces device-independent view tree
ViewTree* typeset_create_view_tree(TypesetEngine* engine, Item content, TypesetOptions* options);

// Convenience functions for different input types
ViewTree* typeset_markdown_to_view_tree(TypesetEngine* engine, const char* markdown, TypesetOptions* options);
ViewTree* typeset_latex_to_view_tree(TypesetEngine* engine, const char* latex, TypesetOptions* options);
ViewTree* typeset_math_to_view_tree(TypesetEngine* engine, const char* math, TypesetOptions* options);

// View tree serialization
Item view_tree_to_lambda_element(Context* ctx, ViewTree* tree, SerializationOptions* options);
StrBuf* view_tree_to_markdown(ViewTree* tree, MarkdownSerializationOptions* options);

// Multi-format rendering
StrBuf* render_view_tree_to_html(ViewTree* tree, HTMLRenderOptions* options);
StrBuf* render_view_tree_to_svg(ViewTree* tree, SVGRenderOptions* options);
StrBuf* render_view_tree_to_tex(ViewTree* tree, TeXRenderOptions* options);
bool render_view_tree_to_pdf_file(ViewTree* tree, const char* filename, PDFRenderOptions* options);
bool render_view_tree_to_png_file(ViewTree* tree, const char* filename, PNGRenderOptions* options);

// View tree manipulation
ViewNode* view_tree_find_node_by_id(ViewTree* tree, const char* id);
ViewNode* view_tree_find_node_by_role(ViewTree* tree, const char* role);
void view_tree_apply_transform(ViewTree* tree, ViewTransform* transform);
ViewTree* view_tree_extract_pages(ViewTree* tree, int start_page, int end_page);
ViewTree* view_tree_merge(ViewTree* tree1, ViewTree* tree2);
```

### Lambda Function Integration

```lambda
# Core typesetting function
view_tree = typeset(content, options)

# Serialization
lambda_tree = serialize_to_lambda(view_tree, serialization_options)

# Multi-format rendering
html_output = render(view_tree, 'html', html_options)
svg_output = render(view_tree, 'svg', svg_options)
pdf_file = render_to_file(view_tree, 'pdf', 'document.pdf')
tex_output = render(view_tree, 'tex', tex_options)

# View tree manipulation
heading = view_tree_find_by_role(view_tree, 'heading')
modify_node_style(heading, {'color': 'blue', 'font_size': 18})
```

## Usage Examples

### Basic Document Processing

```lambda
# Create document content
content = <document title:"Research Paper">
    <heading level:1>"Introduction"</heading>
    <paragraph>
        "This paper discusses advanced mathematical concepts..."
    </paragraph>
    <math display:true>
        "\\int_{-\\infty}^{\\infty} e^{-x^2} dx = \\sqrt{\\pi}"
    </math>
</document>

# Typeset to view tree
view_tree = typeset(content, {
    'page_size': 'A4',
    'font_family': 'Times New Roman',
    'font_size': 12
})

# Render to different formats
html_output = render(view_tree, 'html', {'semantic': true})
svg_output = render(view_tree, 'svg', {'embed_fonts': true})
pdf_file = render_to_file(view_tree, 'pdf', 'paper.pdf')
latex_output = render(view_tree, 'tex', {'document_class': 'article'})

# Inspect and manipulate
print(serialize_to_lambda(view_tree, {'pretty_print': true}))
title_node = view_tree_find_by_role(view_tree, 'heading')
modify_node_style(title_node, {'color': 'blue', 'font_size': 18})
```

### Mathematical Expression Processing

```lambda
# Process mathematical content
math_expr = input('complex_equation.math', {'type': 'math', 'flavor': 'latex'})

# Create view tree
math_view = typeset(math_expr, {
    'math_style': 'display',
    'font_size': 14
})

# Render to multiple formats
svg_math = render(math_view, 'svg')
html_math = render(math_view, 'html', {'math_renderer': 'mathjax'})
tex_math = render(math_view, 'tex')

# Analyze positioning
print('Math element bounds:', math_view.pages[0].blocks[0].bounds)
print('Total width:', math_view.pages[0].blocks[0].width)
```

### Advanced Document Manipulation

```lambda
# Load and combine documents
doc1 = typeset_from_file('chapter1.md')
doc2 = typeset_from_file('chapter2.tex')
combined = view_tree_merge(doc1, doc2)

# Extract specific pages
pages_2_to_5 = view_tree_extract_pages(combined, 2, 5)

# Apply transformations
scale_transform = create_scale_transform(1.2, 1.2)
view_tree_apply_transform(pages_2_to_5, scale_transform)

# Generate outputs
render_to_file(pages_2_to_5, 'pdf', 'selected_pages.pdf')
render_to_file(pages_2_to_5, 'html', 'selected_pages.html')
```

## Configuration Options

### Typesetting Options

```c
typedef struct TypesetOptions {
    // Page settings
    ViewSize page_size;         // Page size (A4 default: 595.276 x 841.89 points)
    ViewRect margins;           // Page margins
    bool landscape;             // Landscape orientation
    
    // Typography
    char* default_font_family;  // Default font family
    double default_font_size;   // Default font size (12pt default)
    double line_height;         // Line height multiplier (1.2 default)
    double paragraph_spacing;   // Paragraph spacing
    
    // Layout
    double column_width;        // Column width (0 for auto)
    int column_count;           // Number of columns (1 default)
    double column_gap;          // Gap between columns
    
    // Math settings
    double math_scale;          // Math scale factor (1.0 default)
    bool inline_simple_math;    // Render simple math inline
    
    // Quality settings
    double text_quality;        // Text rendering quality (0.0-1.0)
    bool optimize_layout;       // Optimize layout for performance
    bool enable_hyphenation;    // Enable hyphenation
} TypesetOptions;
```

### Output Format Options

Each output format supports specific options for fine-tuning:

- **HTML**: Semantic markup, CSS options, accessibility features
- **SVG**: Font embedding, path optimization, precision control
- **PDF**: Font subsetting, bookmarks, links, annotations
- **TeX**: Document class, package selection, Unicode handling
- **PNG**: DPI, compression, transparency, scaling

## File Structure

```
lambda/typeset/
├── Typeset.md                    # This documentation
├── typeset.h                     # Main API header
├── typeset.c                     # Main engine implementation
├── view/
│   └── view_tree.h              # View tree data structures
│   └── view_tree.c              # View tree implementation
├── document/
│   ├── document.h               # Document structure
│   ├── document.c
│   ├── page.h                   # Page management
│   └── page.c
├── style/
│   ├── font.h                   # Font management
│   ├── font.c
│   ├── style.h                  # Style system
│   └── style.c
├── layout/
│   ├── layout.h                 # Layout engine
│   └── layout.c
├── math/
│   ├── math_layout.h            # Mathematical layout
│   ├── math_layout.c
│   ├── math_metrics.h           # Math metrics
│   └── math_metrics.c
├── output/
│   ├── renderer.h               # Base renderer interface
│   ├── renderer.c
│   ├── html_renderer.h          # HTML output
│   ├── html_renderer.c
│   ├── svg_renderer.h           # SVG output
│   ├── svg_renderer.c
│   ├── pdf_renderer.h           # PDF output
│   ├── pdf_renderer.c
│   ├── tex_renderer.h           # TeX/LaTeX output
│   ├── tex_renderer.c
│   ├── png_renderer.h           # PNG output
│   └── png_renderer.c
├── serialization/
│   ├── lambda_serializer.h      # Lambda element serialization
│   ├── lambda_serializer.c
│   ├── markdown_serializer.h    # Markdown serialization
│   └── markdown_serializer.c
└── integration/
    ├── lambda_bridge.h          # Lambda AST integration
    ├── lambda_bridge.c
    ├── stylesheet.h             # Stylesheet system
    └── stylesheet.c
```

## Building and Testing

The typesetting system is integrated into the main Lambda build system:

```bash
# Build with typesetting support
make build

# Test typesetting functionality
make test-typeset

# Test refined view tree architecture
make test-typeset-refined

# Test mathematical typesetting
make test-typeset-math

# Test Markdown processing
make test-typeset-markdown
```

## Benefits

### Device Independence
- All measurements in typographical points ensure consistent output across devices
- No device-specific rendering code in the core system
- High precision for professional typography

### Multiple Output Formats
- Single view tree can be rendered to any supported format
- Consistent appearance across all output formats
- Easy to add new output formats through the renderer interface

### Programmability
- View tree is accessible as Lambda data structure
- Can be manipulated programmatically for advanced workflows
- Enables document processing pipelines and automation

### Performance
- Layout computed once, rendered multiple times
- View tree can be cached and reused
- Efficient for generating multiple format outputs

### Mathematical Typography
- Precise mathematical layout according to typographical rules
- Support for complex mathematical expressions
- Integration with multiple math input formats

### Extensibility
- Modular architecture allows easy extension
- New node types can be added to the view tree
- Renderer interface supports custom output formats
- Style system is fully extensible

This typesetting system provides a modern, flexible foundation for high-quality document processing within the Lambda language ecosystem, suitable for everything from simple text documents to complex mathematical papers and technical documentation.
