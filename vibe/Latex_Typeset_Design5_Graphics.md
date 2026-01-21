# Lambda LaTeX Graphics Support Proposal

**Date**: January 2026  
**Status**: Proposal  
**Previous**: [Latex_Typeset_Design4.md](./Latex_Typeset_Design4.md)

---

## 1. Executive Summary

This document proposes enhancements to Lambda's LaTeX pipeline to support graphics packages:

1. **LaTeX `picture` environment** - Native vector graphics with lines, circles, boxes
2. **pict2e package** - Extended picture with arbitrary slopes and curves
3. **TikZ/PGF** - Full-featured graphics with paths, nodes, transformations
4. **graphicx** - Image inclusion and transformation

**Output Strategy**: All graphics output to **SVG** for HTML, which can be further converted to PDF/PNG via the existing Radiant engine.

---

## 2. Analysis of LaTeXML's Approach

### 2.1 How LaTeXML Handles Graphics

LaTeXML uses a **two-phase approach**:

#### Phase 1: Digestion (Package Bindings)

LaTeXML defines package bindings in Perl (`.ltxml` files) that convert LaTeX commands to an **intermediate XML representation**:

```perl
# From latex_constructs.pool.ltxml - picture environment
DefEnvironment('{picture} Pair OptionalPair',
  "<ltx:picture width='#width' height='#height' origin-x='#origin-x' origin-y='#origin-y'"
    . " fill='none' stroke='none' unitlength='#unitlength'>"
    . "?#transform(<ltx:g transform='#transform'>#body</ltx:g>)(#body)"
    . "</ltx:picture>",
  ...);

# Line command -> ltx:line element
DefConstructor('\line Pair:Number {Float}',
  "<ltx:line points='#points' stroke='#color' stroke-width='#thick'/>",
  ...);
```

Key elements in LaTeXML's intermediate format:
- `<ltx:picture>` - Container with width/height/origin
- `<ltx:g>` - Group with transform attribute
- `<ltx:line>` - Line with points
- `<ltx:circle>` - Circle with center and radius
- `<ltx:rect>` - Rectangle
- `<ltx:bezier>` - Bezier curves
- `<ltx:polygon>` - Polygons

#### Phase 2: Post-Processing (SVG Generation)

The `LaTeXML::Post::SVG` module converts the intermediate XML to SVG:

```perl
# From Post/SVG.pm
my %converters = (
  'ltx:picture' => \&convertPicture,
  'ltx:path'    => \&convertPath,
  'ltx:g'       => \&convertG,
  'ltx:line'    => \&convertLine,
  'ltx:circle'  => \&convertCircle,
  'ltx:rect'    => \&convertRect,
  'ltx:bezier'  => \&convertBezier,
  ...
);
```

### 2.2 How LaTeXML Handles TikZ

TikZ is handled through a **custom PGF system driver** (`pgfsys-latexml.def.ltxml`):

```perl
# From pgf.sty.ltxml
DefMacro('\pgfsysdriver', 'pgfsys-latexml.def');  # Use LaTeXML's driver

# From pgfsys-latexml.def.ltxml - path operations
DefConstructor('\pgfsys@moveto{Dimension}{Dimension}', '',
  afterDigest => sub { addToSVGPath('M', $_[1]->getArgs); });

DefConstructor('\pgfsys@lineto{Dimension}{Dimension}', '',
  afterDigest => sub { addToSVGPath('L', $_[1]->getArgs); });

DefConstructor('\pgfsys@curveto{Dimension}{Dimension}{Dimension}'
    . '{Dimension}{Dimension}{Dimension}', '',
  afterDigest => sub { addToSVGPath('C', $_[1]->getArgs); });
```

The strategy:
1. Load the **actual PGF TeX macros** (`noltxml => 1` flag)
2. Override only the **low-level driver commands** (`\pgfsys@*`)
3. Driver commands build SVG path data incrementally
4. Final output embeds `<svg:svg>` directly in the document

### 2.3 Output Format

LaTeXML outputs graphics as **inline SVG** in HTML:

```xml
<!-- Picture environment output -->
<picture width="100.0pt" height="100.0pt" xml:id="S1.p1.pic1">
  <g transform="translate(0,0)">
    <line points="0,0 138.37,0" stroke="#000000" stroke-width="0.4"/>
  </g>
</picture>

<!-- TikZ output - embedded SVG -->
<picture xml:id="p1.pic1">
  <svg:svg height="249.53" overflow="visible" version="1.1" width="253.54">
    <svg:g fill="#000000" stroke="#000000" stroke-width="0.4pt" 
           transform="translate(0,249.53) matrix(1 0 0 -1 0 0)">
      <svg:path d="M 130.27 0 C 130.27 6.71 ..." style="fill:none"/>
      <!-- nodes with foreignObject for text -->
      <svg:foreignObject>
        <Math mode="inline" tex="1" text="1">...</Math>
      </svg:foreignObject>
    </svg:g>
  </svg:svg>
</picture>
```

---

## 3. Proposed Lambda Architecture

### 3.1 Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    Lambda Graphics Pipeline                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   LaTeX Source                                                              │
│       │                                                                     │
│       ▼                                                                     │
│   ┌───────────────────────────────────────────────────────────────────┐     │
│   │  Tree-sitter Parser                                               │     │
│   │  - Parse picture/tikzpicture environments                         │     │
│   │  - Capture coordinate pairs, options, paths                       │     │
│   └───────────────────────────────────────────────────────────────────┘     │
│       │                                                                     │
│       ▼                                                                     │
│   ┌───────────────────────────────────────────────────────────────────┐     │
│   │  Graphics Builder (tex_graphics.cpp)                              │     │
│   │  - Convert picture commands → GraphicsElement IR                  │     │
│   │  - Process TikZ paths → SVG path data                             │     │
│   │  - Handle coordinate transforms                                   │     │
│   └───────────────────────────────────────────────────────────────────┘     │
│       │                                                                     │
│       ▼                                                                     │
│   ┌───────────────────────────────────────────────────────────────────┐     │
│   │  GraphicsElement IR (tex_graphics.hpp)                            │     │
│   │  - Canvas (picture/tikzpicture container)                         │     │
│   │  - Primitives: Line, Circle, Rect, Path, Bezier, Text             │     │
│   │  - Groups with transforms                                         │     │
│   │  - Style state (stroke, fill, line width)                         │     │
│   └───────────────────────────────────────────────────────────────────┘     │
│       │                                                                     │
│       ├────────────────────────┬─────────────────────┐                      │
│       ▼                        ▼                     ▼                      │
│   ┌──────────────┐      ┌──────────────┐      ┌──────────────┐              │
│   │ HTML/SVG     │      │ PDF          │      │ PNG          │              │
│   │ (inline SVG  │      │ (via Radiant │      │ (via Radiant │              │
│   │  in HTML)    │      │  engine)     │      │  engine)     │              │
│   └──────────────┘      └──────────────┘      └──────────────┘              │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 GraphicsElement Intermediate Representation

```cpp
// tex_graphics.hpp

namespace tex {

// Graphics primitive types
enum class GraphicsType {
    CANVAS,     // Picture/TikZ container
    GROUP,      // Group with transform
    LINE,       // Line segment or polyline
    CIRCLE,     // Circle (filled or stroked)
    ELLIPSE,    // Ellipse
    RECT,       // Rectangle
    PATH,       // SVG-style path (M, L, C, Z commands)
    BEZIER,     // Quadratic or cubic Bezier
    POLYGON,    // Closed polygon
    ARC,        // Circular arc
    TEXT,       // Text node
    IMAGE,      // External image
};

// Style properties
struct GraphicsStyle {
    const char* stroke_color;      // e.g., "#000000", "none"
    const char* fill_color;        // e.g., "#FF0000", "none"
    float stroke_width;            // Line width in pt
    const char* stroke_dasharray;  // e.g., "5,3" for dashed
    const char* stroke_linecap;    // "butt", "round", "square"
    const char* stroke_linejoin;   // "miter", "round", "bevel"
    float miter_limit;
    
    // Arrow/terminator markers
    const char* marker_start;
    const char* marker_end;
};

// 2D point
struct Point2D {
    float x;
    float y;
};

// Transform matrix (2D affine)
struct Transform2D {
    float a, b, c, d, e, f;  // matrix(a,b,c,d,e,f)
    
    static Transform2D identity();
    static Transform2D translate(float tx, float ty);
    static Transform2D scale(float sx, float sy);
    static Transform2D rotate(float angle);  // degrees
    
    Transform2D multiply(const Transform2D& other) const;
};

// Base graphics element
struct GraphicsElement {
    GraphicsType type;
    GraphicsStyle style;
    Transform2D transform;
    GraphicsElement* next;     // Sibling list
    GraphicsElement* children; // For GROUP/CANVAS
    
    union {
        // CANVAS
        struct {
            float width;
            float height;
            float origin_x;
            float origin_y;
            float unitlength;
        } canvas;
        
        // LINE
        struct {
            Point2D* points;
            int point_count;
            bool has_arrow;
        } line;
        
        // CIRCLE
        struct {
            Point2D center;
            float radius;
            bool filled;
        } circle;
        
        // RECT
        struct {
            Point2D corner;
            float width;
            float height;
            float rx, ry;  // Rounded corners
        } rect;
        
        // PATH
        struct {
            const char* d;  // SVG path data string
        } path;
        
        // BEZIER
        struct {
            Point2D p0, p1, p2, p3;  // Control points
            bool is_quadratic;       // true = quadratic (p3 unused)
        } bezier;
        
        // ARC
        struct {
            Point2D center;
            float radius;
            float start_angle;
            float end_angle;
            bool filled;
        } arc;
        
        // TEXT
        struct {
            Point2D pos;
            const char* text;
            const char* anchor;  // "start", "middle", "end"
            DocElement* content; // For formatted content
        } text;
        
        // IMAGE
        struct {
            Point2D pos;
            float width;
            float height;
            const char* src;
        } image;
    };
};

// Arena-based allocation
GraphicsElement* graphics_alloc(Arena* arena, GraphicsType type);

// Builder functions
GraphicsElement* graphics_build_picture(const ElementReader& elem, Arena* arena, TexDocumentModel* doc);
GraphicsElement* graphics_build_tikz(const ElementReader& elem, Arena* arena, TexDocumentModel* doc);

// Output functions
void graphics_to_svg(GraphicsElement* root, StrBuf* out);
void graphics_to_html(GraphicsElement* root, StrBuf* out);  // Inline SVG

} // namespace tex
```

### 3.3 SVG Output Generator

```cpp
// tex_graphics_svg.cpp

namespace tex {

// Convert GraphicsElement tree to SVG string
void graphics_to_svg(GraphicsElement* root, StrBuf* out) {
    if (!root) return;
    
    switch (root->type) {
        case GraphicsType::CANVAS:
            emit_svg_open(root, out);
            // Emit with coordinate flip (LaTeX y-axis is bottom-up)
            strbuf_printf(out, "<g transform=\"translate(0,%.2f) scale(1,-1)\">",
                          root->canvas.height);
            for (auto* child = root->children; child; child = child->next) {
                graphics_to_svg(child, out);
            }
            strbuf_append(out, "</g></svg>");
            break;
            
        case GraphicsType::GROUP:
            emit_group_open(root, out);
            for (auto* child = root->children; child; child = child->next) {
                graphics_to_svg(child, out);
            }
            strbuf_append(out, "</g>");
            break;
            
        case GraphicsType::LINE:
            emit_line(root, out);
            break;
            
        case GraphicsType::CIRCLE:
            emit_circle(root, out);
            break;
            
        case GraphicsType::PATH:
            emit_path(root, out);
            break;
            
        case GraphicsType::TEXT:
            emit_text(root, out);
            break;
            
        // ... other types
    }
}

static void emit_svg_open(GraphicsElement* canvas, StrBuf* out) {
    strbuf_printf(out, 
        "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        "version=\"1.1\" width=\"%.2f\" height=\"%.2f\" "
        "overflow=\"visible\">",
        canvas->canvas.width, canvas->canvas.height);
}

static void emit_line(GraphicsElement* line, StrBuf* out) {
    if (line->line.point_count == 2) {
        // Simple line
        strbuf_printf(out, "<line x1=\"%.2f\" y1=\"%.2f\" x2=\"%.2f\" y2=\"%.2f\"",
                      line->line.points[0].x, line->line.points[0].y,
                      line->line.points[1].x, line->line.points[1].y);
    } else {
        // Polyline
        strbuf_append(out, "<polyline points=\"");
        for (int i = 0; i < line->line.point_count; i++) {
            if (i > 0) strbuf_append(out, " ");
            strbuf_printf(out, "%.2f,%.2f", 
                          line->line.points[i].x, line->line.points[i].y);
        }
        strbuf_append(out, "\"");
    }
    emit_style(line, out);
    
    // Arrow markers
    if (line->line.has_arrow) {
        strbuf_append(out, " marker-end=\"url(#arrow)\"");
    }
    strbuf_append(out, "/>");
}

static void emit_circle(GraphicsElement* circle, StrBuf* out) {
    strbuf_printf(out, "<circle cx=\"%.2f\" cy=\"%.2f\" r=\"%.2f\"",
                  circle->circle.center.x, circle->circle.center.y,
                  circle->circle.radius);
    emit_style(circle, out);
    strbuf_append(out, "/>");
}

static void emit_path(GraphicsElement* path, StrBuf* out) {
    strbuf_printf(out, "<path d=\"%s\"", path->path.d);
    emit_style(path, out);
    strbuf_append(out, "/>");
}

static void emit_style(GraphicsElement* elem, StrBuf* out) {
    const GraphicsStyle& s = elem->style;
    
    if (s.stroke_color && strcmp(s.stroke_color, "none") != 0) {
        strbuf_printf(out, " stroke=\"%s\"", s.stroke_color);
    } else {
        strbuf_append(out, " stroke=\"none\"");
    }
    
    if (s.fill_color && strcmp(s.fill_color, "none") != 0) {
        strbuf_printf(out, " fill=\"%s\"", s.fill_color);
    } else {
        strbuf_append(out, " fill=\"none\"");
    }
    
    if (s.stroke_width > 0) {
        strbuf_printf(out, " stroke-width=\"%.2f\"", s.stroke_width);
    }
    
    if (s.stroke_dasharray) {
        strbuf_printf(out, " stroke-dasharray=\"%s\"", s.stroke_dasharray);
    }
}

} // namespace tex
```

---

## 4. Integration with Existing Lambda LaTeX Pipeline

This section explains how the proposed graphics support integrates with Lambda's existing LaTeX processing pipeline documented in [Latex_Typeset_Design4.md](./Latex_Typeset_Design4.md).

### 4.1 Pipeline Overview with Graphics

The graphics system inserts into the existing pipeline at multiple layers:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                  Complete Lambda LaTeX Pipeline                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   LaTeX Source (.tex file)                                                  │
│       │                                                                     │
│       ▼                                                                     │
│   ┌───────────────────────────────────────────────────────────────────┐     │
│   │  Layer 1: Tree-sitter LaTeX Parser                                │     │
│   │  - Parses \begin{picture}, \begin{tikzpicture}                    │     │
│   │  - Captures coordinate pairs, options, nested commands            │     │
│   │  - Output: Concrete Syntax Tree (CST)                             │     │
│   └───────────────────────────────────────────────────────────────────┘     │
│       │                                                                     │
│       ▼                                                                     │
│   ┌───────────────────────────────────────────────────────────────────┐     │
│   │  Layer 2: tex_latex_bridge (build_latex_ast)                      │     │
│   │  - Converts CST → Lambda Element tree                             │     │
│   │  - Picture/TikZ environments become Element nodes                 │     │
│   │  - Commands like \put, \draw become nested elements               │     │
│   └───────────────────────────────────────────────────────────────────┘     │
│       │                                                                     │
│       ▼                                                                     │
│   ┌───────────────────────────────────────────────────────────────────┐     │
│   │  Layer 3: TexDocumentModel + CommandRegistry + PackageLoader      │     │
│   │  ┌─────────────────────────────────────────────────────────────┐  │     │
│   │  │  PackageLoader: loads picture.pkg.json, tikz.pkg.json       │  │     │
│   │  │  - Registers graphics commands in CommandRegistry           │  │     │
│   │  │  - Sets up PGF driver callbacks for TikZ                    │  │     │
│   │  └─────────────────────────────────────────────────────────────┘  │     │
│   │  ┌─────────────────────────────────────────────────────────────┐  │     │
│   │  │  CommandRegistry: dispatches \put → put_handler callback    │  │     │
│   │  │  - Graphics commands build GraphicsElement IR               │  │     │
│   │  │  - TikZ \pgfsys@* commands → PgfDriverState                 │  │     │
│   │  └─────────────────────────────────────────────────────────────┘  │     │
│   │  ┌─────────────────────────────────────────────────────────────┐  │     │
│   │  │  process_element(): traverses Element tree                  │  │     │
│   │  │  - On picture/tikzpicture → graphics_build_*() functions    │  │     │
│   │  │  - Returns DocElement with type=GRAPHICS                    │  │     │
│   │  └─────────────────────────────────────────────────────────────┘  │     │
│   └───────────────────────────────────────────────────────────────────┘     │
│       │                                                                     │
│       ▼                                                                     │
│   ┌───────────────────────────────────────────────────────────────────┐     │
│   │  Layer 4: DocElement Tree (Document Model)                        │     │
│   │  - DocElemType::GRAPHICS contains GraphicsElement* root           │     │
│   │  - Embedded within PARAGRAPH, FIGURE, etc.                        │     │
│   │  - Mixed content: text + math + graphics in same paragraph        │     │
│   └───────────────────────────────────────────────────────────────────┘     │
│       │                                                                     │
│       ├────────────────────────┬─────────────────────┐                      │
│       ▼                        ▼                     ▼                      │
│   ┌──────────────┐      ┌──────────────┐      ┌──────────────┐              │
│   │ HTML Output  │      │ PDF Output   │      │ PNG Output   │              │
│   │ emit_html()  │      │ Radiant      │      │ Radiant      │              │
│   │ ↓            │      │ engine       │      │ engine       │              │
│   │ graphics_to_ │      │              │      │              │              │
│   │ svg()        │      │              │      │              │              │
│   │ ↓            │      │              │      │              │              │
│   │ Inline SVG   │      │              │      │              │              │
│   │ in HTML      │      │              │      │              │              │
│   └──────────────┘      └──────────────┘      └──────────────┘              │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 4.2 Key Integration Points

#### 4.2.1 Package Loading (PackageLoader)

When `\usepackage{tikz}` is encountered:

```cpp
// In tex_package_loader.cpp
void PackageLoader::load_package(const char* name, TexDocumentModel* doc) {
    // Load tikz.pkg.json
    const char* json_path = find_package_json(name);  // "lambda/tex/packages/tikz.pkg.json"
    Package* pkg = parse_package_json(json_path);
    
    // Register commands in CommandRegistry
    for (auto& cmd : pkg->commands) {
        doc->registry->register_command(cmd.name, cmd.handler_callback);
    }
    
    // For TikZ: initialize PGF driver state
    if (pkg->has_driver) {
        doc->pgf_state = pgf_driver_init(doc->arena);
    }
    
    // Load dependencies (pgf, xcolor, etc.)
    for (auto& dep : pkg->requires) {
        load_package(dep, doc);
    }
}
```

#### 4.2.2 Command Dispatch (CommandRegistry)

```cpp
// In tex_command_registry.cpp
DocElement* CommandRegistry::dispatch(const char* cmd_name, const ElementReader& elem,
                                       Arena* arena, TexDocumentModel* doc) {
    Handler* handler = find_handler(cmd_name);
    if (handler && handler->callback) {
        return handler->callback(elem, arena, doc);
    }
    
    // Fall through to default macro expansion
    return expand_macro(cmd_name, elem, arena, doc);
}

// Graphics command handler example
DocElement* put_handler(const ElementReader& elem, Arena* arena, TexDocumentModel* doc) {
    // Build GraphicsElement from \put command
    GraphicsElement* ge = build_put_command(elem, arena, doc);
    
    // Append to current graphics context (picture environment's GraphicsElement tree)
    append_graphics_element(doc->current_graphics_context, ge);
    
    return nullptr;  // Graphics elements collected separately, not as DocElements
}
```

#### 4.2.3 Environment Processing

```cpp
// In tex_document_model.cpp
DocElement* process_environment(const ElementReader& env, Arena* arena, 
                                 TexDocumentModel* doc) {
    const char* env_name = env.get_env_name();
    
    if (tag_eq(env_name, "picture")) {
        // Build entire picture as GraphicsElement IR
        GraphicsElement* gfx = graphics_build_picture(env, arena, doc);
        
        // Wrap in DocElement
        DocElement* elem = doc_elem_alloc(arena, DocElemType::GRAPHICS);
        elem->graphics.root = gfx;
        return elem;
    }
    else if (tag_eq(env_name, "tikzpicture")) {
        // Build TikZ picture via PGF driver
        GraphicsElement* gfx = graphics_build_tikz(env, arena, doc);
        
        DocElement* elem = doc_elem_alloc(arena, DocElemType::GRAPHICS);
        elem->graphics.root = gfx;
        return elem;
    }
    // ... other environments
}
```

#### 4.2.4 HTML Output with Inline SVG

```cpp
// In tex_doc_model_html.cpp
void emit_html(DocElement* root, StrBuf* out, HtmlContext* ctx) {
    switch (root->type) {
        case DocElemType::PARAGRAPH:
            emit_paragraph(root, out, ctx);
            break;
            
        case DocElemType::MATH_DISPLAY:
            emit_math_display(root, out, ctx);
            break;
            
        case DocElemType::GRAPHICS:
            // Convert GraphicsElement tree → inline SVG
            strbuf_append(out, "<span class=\"graphics\">");
            graphics_to_svg(root->graphics.root, out);
            strbuf_append(out, "</span>");
            break;
            
        // ... other types
    }
}
```

### 4.3 Data Flow Example: TikZ Processing

For a TikZ diagram like:
```latex
\begin{tikzpicture}
  \draw (0,0) -- (1,1);
  \node at (0.5, 0.5) {A};
\end{tikzpicture}
```

The data flows as:

1. **Tree-sitter** parses `\begin{tikzpicture}...\end{tikzpicture}` into CST nodes
2. **tex_latex_bridge** builds Element tree with `tikzpicture` element containing `\draw` and `\node` children
3. **PackageLoader** has already loaded `tikz.pkg.json`, registering `\draw`, `\node` handlers
4. **process_environment("tikzpicture")** calls `graphics_build_tikz()`:
   - Initializes PgfDriverState
   - Processes children: `\draw` expands to `\pgfsys@moveto`, `\pgfsys@lineto`, `\pgfsys@stroke`
   - Each `\pgfsys@*` command appends to path buffer or emits GraphicsElement
   - `\node` creates TEXT element with foreignObject for HTML content
5. **Result**: GraphicsElement tree:
   ```
   CANVAS (tikzpicture)
   └── GROUP
       ├── PATH (d="M 0 0 L 28.35 28.35")
       └── TEXT (pos=(14.17, 14.17), content="A")
   ```
6. **emit_html()** encounters DocElemType::GRAPHICS, calls `graphics_to_svg()`:
   ```html
   <span class="graphics">
     <svg xmlns="http://www.w3.org/2000/svg" width="28.35" height="28.35">
       <g transform="translate(0,28.35) scale(1,-1)">
         <path d="M 0 0 L 28.35 28.35" stroke="#000" fill="none"/>
         <foreignObject x="14.17" y="14.17">A</foreignObject>
       </g>
     </svg>
   </span>
   ```

### 4.4 Existing Components Reused

| Component | Location | Role in Graphics Support |
|-----------|----------|--------------------------|
| `PackageLoader` | `tex_package_loader.cpp` | Loads `picture.pkg.json`, `tikz.pkg.json` |
| `CommandRegistry` | `tex_command_registry.cpp` | Dispatches `\put`, `\draw`, `\pgfsys@*` |
| `tex_latex_bridge` | `tex_latex_bridge.cpp` | Parses CST → Element tree (unchanged) |
| `TexDocumentModel` | `tex_document_model.cpp` | Extends with `DocElemType::GRAPHICS` |
| `emit_html()` | `tex_doc_model_html.cpp` | Adds graphics → SVG output case |
| `Arena` allocator | `lib/arena.h` | Allocates GraphicsElement nodes |

### 4.5 New Components Required

| Component | Location | Purpose |
|-----------|----------|---------|
| `GraphicsElement` IR | `tex_graphics.hpp` | Intermediate representation for all graphics |
| `graphics_build_picture()` | `tex_graphics_picture.cpp` | Build IR from picture environment |
| `graphics_build_tikz()` | `tex_graphics_tikz.cpp` | Build IR from TikZ via PGF driver |
| `PgfDriverState` | `tex_pgf_driver.hpp` | State machine for PGF system commands |
| `graphics_to_svg()` | `tex_graphics_svg.cpp` | Convert IR to SVG output |
| `picture.pkg.json` | `lambda/tex/packages/` | Picture command definitions |
| `tikz.pkg.json` | `lambda/tex/packages/` | TikZ/PGF command definitions |

---

## 5. Picture Environment Implementation

### 5.1 Tree-sitter Grammar Additions

The Tree-sitter LaTeX grammar needs extensions for picture commands:

```javascript
// In tree-sitter-latex/grammar.js - additions for picture

picture_environment: $ => seq(
  '\\begin{picture}',
  $.picture_size,
  optional($.picture_offset),
  repeat($.picture_command),
  '\\end{picture}'
),

picture_size: $ => seq('(', $.dimension_pair, ')'),
picture_offset: $ => seq('(', $.dimension_pair, ')'),

picture_command: $ => choice(
  $.put_command,
  $.multiput_command,
  $.line_command,
  $.vector_command,
  $.circle_command,
  $.oval_command,
  $.qbezier_command,
  $.framebox_command,
  $.makebox_command,
  // ... more commands
),

put_command: $ => seq(
  '\\put',
  '(', $.coordinate_pair, ')',
  $.brace_group
),

line_command: $ => seq(
  '\\line',
  '(', $.slope_pair, ')',
  '{', $.dimension, '}'
),

// ... additional rules
```

### 5.2 Picture Builder Implementation

```cpp
// tex_graphics_picture.cpp

namespace tex {

// Build GraphicsElement from picture environment
GraphicsElement* graphics_build_picture(const ElementReader& elem, Arena* arena, 
                                        TexDocumentModel* doc) {
    GraphicsElement* canvas = graphics_alloc(arena, GraphicsType::CANVAS);
    
    // Parse size and offset
    if (auto size = elem.get_arg(0)) {
        auto pair = parse_coordinate_pair(size);
        canvas->canvas.width = pair.x * doc->unitlength;
        canvas->canvas.height = pair.y * doc->unitlength;
    }
    
    if (auto offset = elem.get_arg(1)) {
        auto pair = parse_coordinate_pair(offset);
        canvas->canvas.origin_x = pair.x * doc->unitlength;
        canvas->canvas.origin_y = pair.y * doc->unitlength;
    }
    
    canvas->canvas.unitlength = doc->unitlength;
    
    // Process child commands
    GraphicsElement** tail = &canvas->children;
    for (auto child : elem.children()) {
        GraphicsElement* ge = build_picture_command(child, arena, doc);
        if (ge) {
            *tail = ge;
            tail = &ge->next;
        }
    }
    
    return canvas;
}

static GraphicsElement* build_picture_command(const ElementReader& cmd, Arena* arena,
                                              TexDocumentModel* doc) {
    const char* name = cmd.tag_name();
    
    if (tag_eq(name, "put")) {
        return build_put_command(cmd, arena, doc);
    } else if (tag_eq(name, "line")) {
        return build_line_command(cmd, arena, doc);
    } else if (tag_eq(name, "vector")) {
        return build_vector_command(cmd, arena, doc);
    } else if (tag_eq(name, "circle")) {
        return build_circle_command(cmd, arena, doc);
    } else if (tag_eq(name, "qbezier")) {
        return build_qbezier_command(cmd, arena, doc);
    }
    // ... more commands
    
    return nullptr;
}

static GraphicsElement* build_put_command(const ElementReader& cmd, Arena* arena,
                                          TexDocumentModel* doc) {
    GraphicsElement* group = graphics_alloc(arena, GraphicsType::GROUP);
    
    // Parse position
    auto pos = parse_coordinate_pair(cmd.get_arg(0));
    float x = pos.x * doc->unitlength;
    float y = pos.y * doc->unitlength;
    
    group->transform = Transform2D::translate(x, y);
    
    // Build content
    auto content = cmd.get_arg(1);
    group->children = build_picture_content(content, arena, doc);
    
    return group;
}

static GraphicsElement* build_line_command(const ElementReader& cmd, Arena* arena,
                                           TexDocumentModel* doc) {
    GraphicsElement* line = graphics_alloc(arena, GraphicsType::LINE);
    
    // Parse slope (x, y)
    auto slope = parse_slope_pair(cmd.get_arg(0));
    
    // Parse length
    float length = parse_dimension(cmd.get_arg(1)) * doc->unitlength;
    
    // Calculate endpoint
    float dx = slope.x;
    float dy = slope.y;
    float mag = sqrtf(dx*dx + dy*dy);
    if (mag > 0) {
        dx = dx / mag * length;
        dy = dy / mag * length;
    }
    
    line->line.points = (Point2D*)arena_alloc(arena, 2 * sizeof(Point2D));
    line->line.point_count = 2;
    line->line.points[0] = {0, 0};
    line->line.points[1] = {dx, dy};
    line->line.has_arrow = false;
    
    // Default style
    line->style.stroke_color = "#000000";
    line->style.fill_color = "none";
    line->style.stroke_width = doc->line_thickness;
    
    return line;
}

static GraphicsElement* build_circle_command(const ElementReader& cmd, Arena* arena,
                                             TexDocumentModel* doc) {
    GraphicsElement* circle = graphics_alloc(arena, GraphicsType::CIRCLE);
    
    bool filled = cmd.has_star();
    float diameter = parse_dimension(cmd.get_arg(0)) * doc->unitlength;
    
    circle->circle.center = {0, 0};
    circle->circle.radius = diameter / 2;
    circle->circle.filled = filled;
    
    if (filled) {
        circle->style.fill_color = "#000000";
        circle->style.stroke_color = "none";
    } else {
        circle->style.fill_color = "none";
        circle->style.stroke_color = "#000000";
    }
    circle->style.stroke_width = doc->line_thickness;
    
    return circle;
}

} // namespace tex
```

---

## 6. TikZ Support Strategy

### 6.1 Approach Options

| Option | Description | Complexity | Coverage |
|--------|-------------|------------|----------|
| **A: Native TikZ parser** | Parse TikZ syntax directly | Very High | 60-70% |
| **B: PGF driver emulation** | Implement `\pgfsys@*` commands | High | 80-90% |
| **C: Macro expansion + driver** | Expand macros, then driver | Medium-High | 90%+ |
| **D: External dvisvgm** | Shell out to TeX + dvisvgm | Low | 100% |

**Recommended: Option B (PGF driver emulation)** with fallback to Option D for complex cases.

### 6.2 PGF Driver Implementation

TikZ/PGF has a layered architecture:
1. **TikZ layer** - High-level commands (`\draw`, `\node`, etc.)
2. **PGF basic layer** - Path operations (`\pgfpathmoveto`, etc.)
3. **PGF system layer** - Driver-specific output (`\pgfsys@*` commands)

We implement only the **system layer** to intercept all drawing commands:

```cpp
// tex_pgf_driver.hpp

namespace tex {

// PGF driver state
struct PgfDriverState {
    // Current path being built
    StrBuf path_data;
    
    // Graphics state stack
    struct State {
        GraphicsStyle style;
        Transform2D transform;
    };
    ArrayList<State> state_stack;
    
    // Output
    GraphicsElement* root;
    GraphicsElement* current_group;
    Arena* arena;
    
    // Clipping
    int clip_id_counter;
    bool clip_next;
};

// System layer commands
void pgfsys_moveto(PgfDriverState* state, float x, float y);
void pgfsys_lineto(PgfDriverState* state, float x, float y);
void pgfsys_curveto(PgfDriverState* state, float x1, float y1, 
                    float x2, float y2, float x3, float y3);
void pgfsys_rect(PgfDriverState* state, float x, float y, float w, float h);
void pgfsys_closepath(PgfDriverState* state);

void pgfsys_stroke(PgfDriverState* state);
void pgfsys_fill(PgfDriverState* state);
void pgfsys_fillstroke(PgfDriverState* state);
void pgfsys_discardpath(PgfDriverState* state);

void pgfsys_setlinewidth(PgfDriverState* state, float width);
void pgfsys_setdash(PgfDriverState* state, const char* pattern, float offset);
void pgfsys_setcolor_stroke(PgfDriverState* state, const char* color);
void pgfsys_setcolor_fill(PgfDriverState* state, const char* color);

void pgfsys_transformcm(PgfDriverState* state, float a, float b, 
                        float c, float d, float e, float f);
void pgfsys_beginscope(PgfDriverState* state);
void pgfsys_endscope(PgfDriverState* state);

void pgfsys_hbox(PgfDriverState* state, DocElement* content);

} // namespace tex
```

### 6.3 TikZ Command Registration

```cpp
// tex_tikz_commands.cpp

namespace tex {

// Register TikZ/PGF commands in the command registry
void register_tikz_commands(CommandRegistry* registry) {
    // System layer - these build SVG directly
    registry->register_callback("pgfsys@moveto", pgfsys_moveto_handler);
    registry->register_callback("pgfsys@lineto", pgfsys_lineto_handler);
    registry->register_callback("pgfsys@curveto", pgfsys_curveto_handler);
    registry->register_callback("pgfsys@stroke", pgfsys_stroke_handler);
    registry->register_callback("pgfsys@fill", pgfsys_fill_handler);
    registry->register_callback("pgfsys@beginscope", pgfsys_beginscope_handler);
    registry->register_callback("pgfsys@endscope", pgfsys_endscope_handler);
    // ... ~50 more pgfsys commands
    
    // Basic layer - expand to system layer
    registry->register_macro("pgfpathmoveto", "\\pgfsys@moveto{#1}");
    registry->register_macro("pgfpathlineto", "\\pgfsys@lineto{#1}");
    // ...
    
    // TikZ layer - complex expansions
    // These are handled by macro expansion in the TeX engine
}

// Handler example
static DocElement* pgfsys_stroke_handler(const ElementReader& elem, Arena* arena,
                                         TexDocumentModel* doc) {
    PgfDriverState* pgf = doc->pgf_state;
    
    // Create path element from accumulated path data
    GraphicsElement* path = graphics_alloc(arena, GraphicsType::PATH);
    path->path.d = strbuf_to_string(&pgf->path_data, arena);
    path->style = pgf->state_stack.back().style;
    path->style.fill_color = "none";  // stroke only
    
    // Append to current group
    append_to_group(pgf->current_group, path);
    
    // Clear path
    strbuf_clear(&pgf->path_data);
    
    return nullptr;  // Handled internally
}

} // namespace tex
```

### 6.4 TikZ Package JSON Definition

Enhance `tikz.pkg.json` with callback registrations:

```json
{
  "name": "tikz",
  "version": "3.1.10",
  "requires": ["latex_base", "xcolor", "pgf"],
  
  "driver": {
    "type": "pgf_system_driver",
    "implementation": "tex_pgf_driver.cpp",
    "init_callback": "pgf_driver_init"
  },
  
  "system_commands": {
    "pgfsys@moveto": {"callback": "pgfsys_moveto_handler", "params": "{{}}{{}}"},
    "pgfsys@lineto": {"callback": "pgfsys_lineto_handler", "params": "{{}}{{}}"},
    "pgfsys@curveto": {"callback": "pgfsys_curveto_handler", "params": "{{}}{{}}{{}}{{}}{{}}{{}}"},
    "pgfsys@rect": {"callback": "pgfsys_rect_handler", "params": "{{}}{{}}{{}}{{}}"},
    "pgfsys@closepath": {"callback": "pgfsys_closepath_handler"},
    "pgfsys@stroke": {"callback": "pgfsys_stroke_handler"},
    "pgfsys@fill": {"callback": "pgfsys_fill_handler"},
    "pgfsys@clipnext": {"callback": "pgfsys_clipnext_handler"},
    "pgfsys@discardpath": {"callback": "pgfsys_discardpath_handler"},
    "pgfsys@setlinewidth": {"callback": "pgfsys_setlinewidth_handler", "params": "{{}}"},
    "pgfsys@setdash": {"callback": "pgfsys_setdash_handler", "params": "{{}}{{}}"},
    "pgfsys@color@rgb@stroke": {"callback": "pgfsys_color_rgb_stroke_handler", "params": "{{}}{{}}{{}}"},
    "pgfsys@color@rgb@fill": {"callback": "pgfsys_color_rgb_fill_handler", "params": "{{}}{{}}{{}}"},
    "pgfsys@transformcm": {"callback": "pgfsys_transformcm_handler", "params": "{{}}{{}}{{}}{{}}{{}}{{}}"},
    "pgfsys@beginscope": {"callback": "pgfsys_beginscope_handler"},
    "pgfsys@endscope": {"callback": "pgfsys_endscope_handler"},
    "pgfsys@hbox": {"callback": "pgfsys_hbox_handler", "params": "{{}}"}
  },
  
  "environments": {
    "tikzpicture": {
      "description": "TikZ drawing environment",
      "callback": "tikzpicture_handler",
      "params": "[]"
    },
    "scope": {
      "description": "TikZ scope for local options",
      "callback": "tikz_scope_handler", 
      "params": "[]"
    }
  }
}
```

---

## 7. Integration with Document Model

### 7.1 DocElement Extensions

Add graphics support to the document model:

```cpp
// In tex_document_model.hpp - additions

enum class DocElemType {
    // ... existing types ...
    GRAPHICS,    // Graphics container (picture, tikzpicture)
};

struct DocElement {
    // ... existing fields ...
    
    // For GRAPHICS type
    struct {
        GraphicsElement* root;  // Graphics IR root
        const char* svg_cache;  // Pre-rendered SVG (optional)
    } graphics;
};
```

### 7.2 HTML Output Integration

```cpp
// In tex_doc_model_html.cpp - additions

static void emit_graphics_element(DocElement* elem, StrBuf* out, HtmlContext* ctx) {
    GraphicsElement* gfx = elem->graphics.root;
    
    if (!gfx) return;
    
    // Wrap in figure-like container if needed
    strbuf_append(out, "<span class=\"graphics\">");
    
    // Output as inline SVG
    graphics_to_svg(gfx, out);
    
    strbuf_append(out, "</span>");
}
```

---

## 8. Test Fixtures

Test fixtures have been copied from LaTeXML:

### 8.1 Graphics Tests (`test/latex/fixtures/graphics/`)

| File | Tests |
|------|-------|
| `picture.tex` | Basic picture environment, lines, circles, boxes |
| `graphrot.tex` | Image rotation and scaling |
| `colors.tex` | Color handling |
| `xcolors.tex` | xcolor package |
| `framed.tex` | Framed boxes |
| `calc.tex` | Calculation expressions |
| `xytest.tex` | XY-pic diagrams |

### 8.2 TikZ Tests (`test/latex/fixtures/tikz/`)

| File | Tests |
|------|-------|
| `cycle.tex` | Circular graph with nodes |
| `3d-cone.tex` | 3D cone drawing |
| `atoms-and-orbitals.tex` | Chemistry diagram |
| `consort-flowchart.tex` | Flowchart |
| `dominoes.tex` | Pattern drawing |
| `tikz_figure.tex` | TikZ with includegraphics |
| `unit_tests_by_silviu.tex` | Comprehensive unit tests |
| `various_colors.tex` | Color handling |

---

## 9. Implementation Roadmap

### Phase 1: Picture Environment (3 weeks)

| Week | Task | Deliverable |
|------|------|-------------|
| 1 | GraphicsElement IR design | `tex_graphics.hpp` |
| 1 | SVG output generator | `tex_graphics_svg.cpp` |
| 2 | Picture parser & builder | `tex_graphics_picture.cpp` |
| 2 | Basic commands: put, line, circle | Tests passing |
| 3 | Boxes, bezier, multiput | Full picture.tex passing |
| 3 | Integration with DocModel | HTML output working |

### Phase 2: pict2e Package (1 week)

| Task | Deliverable |
|------|-------------|
| Arbitrary slopes | Line/vector without restrictions |
| Bezier curves | cbezier, path commands |
| Enhanced circles/ovals | Any size support |

### Phase 3: TikZ Core (4 weeks)

| Week | Task | Deliverable |
|------|------|-------------|
| 4 | PGF driver framework | `tex_pgf_driver.cpp` |
| 4 | Basic path operations | moveto, lineto, curveto |
| 5 | Stroke/fill operations | pgfsys@stroke, fill |
| 5 | Transforms and scoping | beginscope, transformcm |
| 6 | Color support | RGB/CMYK/Gray |
| 6 | Text nodes (foreignObject) | hbox handling |
| 7 | Testing and fixes | cycle.tex passing |

### Phase 4: TikZ Libraries (ongoing)

| Library | Priority | Complexity |
|---------|----------|------------|
| arrows | High | Low |
| positioning | High | Low |
| calc | High | Medium |
| shapes.geometric | Medium | Medium |
| decorations | Low | High |
| 3d | Low | High |

---

## 10. Success Criteria

| Criterion | Target |
|-----------|--------|
| Picture environment tests | 100% of picture.tex |
| Basic TikZ tests | 80% of tikz fixtures |
| SVG output validity | W3C validator passing |
| Performance | < 100ms for typical diagram |
| HTML output | Renders correctly in browsers |

---

## 11. Appendix: LaTeXML Reference Files

Key LaTeXML files for reference:

| File | Purpose |
|------|---------|
| `lib/LaTeXML/Engine/latex_constructs.pool.ltxml` | Picture environment bindings |
| `lib/LaTeXML/Package/pgf.sty.ltxml` | PGF package loading |
| `lib/LaTeXML/Package/pgfsys-latexml.def.ltxml` | PGF system driver (1023 lines) |
| `lib/LaTeXML/Package/tikz.sty.ltxml` | TikZ package bindings |
| `lib/LaTeXML/Post/SVG.pm` | SVG post-processor (523 lines) |
| `lib/LaTeXML/Post/Graphics.pm` | Graphics file handling |

---

## 12. Summary

This proposal outlines a comprehensive approach to graphics support in Lambda:

1. **Unified GraphicsElement IR** - Common representation for all graphics types
2. **SVG as primary output** - HTML-compatible, scalable, well-supported
3. **Picture environment** - Native implementation for basic LaTeX graphics
4. **TikZ via PGF driver** - Intercept low-level commands for maximum compatibility
5. **Incremental rollout** - Picture first, then TikZ core, then libraries

The approach follows LaTeXML's proven strategy while adapting to Lambda's architecture (Tree-sitter parsing, C++ implementation, DocElement model).
