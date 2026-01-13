# Radiant Engine: Inline SVG Support Proposal

## Executive Summary

This proposal outlines enhancements to the Radiant engine to properly support **inline SVG elements embedded within HTML documents**. Currently, Radiant can load standalone SVG files as images via ThorVG, but does not support SVG elements embedded directly in HTML (e.g., `<svg>...</svg>` within `<body>`). This enhancement will enable rendering of inline SVG graphics with proper layout integration by **directly constructing ThorVG scene graphs** from parsed SVG elements.

---

## 1. Current State Analysis

### 1.1 HTML Parser: SVG Element Support ✅ VERIFIED

The HTML5 parser **already supports SVG elements** with proper namespace handling:

**File**: [html5_parser.cpp](../lambda/input/html5/html5_parser.cpp)

| Feature | Status | Details |
|---------|--------|---------|
| SVG tag parsing | ✅ Supported | `<svg>`, `<path>`, `<circle>`, `<rect>`, etc. |
| SVG tag case correction | ✅ Supported | `clippath` → `clipPath`, `foreignobject` → `foreignObject` |
| SVG attribute case correction | ✅ Supported | `viewbox` → `viewBox`, `preserveaspectratio` → `preserveAspectRatio` |
| SVG namespace detection | ✅ Supported | `html5_is_in_svg_namespace()` walks parent chain |
| Foreign attributes | ✅ Supported | `xlink:href`, `xml:lang`, etc. preserved |

**Evidence from code**:
```cpp
// 55 SVG attribute replacements (viewBox, preserveAspectRatio, etc.)
static const char* svg_attribute_replacements[][2] = {
    {"viewbox", "viewBox"},
    {"preserveaspectratio", "preserveAspectRatio"},
    // ... 53 more
};

// 37 SVG tag replacements (clipPath, linearGradient, etc.)
static const char* svg_tag_replacements[][2] = {
    {"clippath", "clipPath"},
    {"lineargradient", "linearGradient"},
    // ... 35 more
};
```

### 1.2 Layout Engine: SVG Handling ⚠️ PARTIAL

**Current state** ([resolve_css_style.cpp#L815](../radiant/resolve_css_style.cpp#L815)):
```cpp
} else if (tag_id == HTM_TAG_SVG) {
    // SVG elements are inline replaced elements by default
    display.outer = CSS_VALUE_INLINE;
    display.inner = RDT_DISPLAY_REPLACED;
}
```

The SVG element is treated as a **replaced element** (like `<img>`), but:
- ❌ No viewBox parsing from SVG element attributes
- ❌ No intrinsic size calculation from `width`/`height` attributes or viewBox
- ❌ No coordinate system mapping for child elements
- ❌ Children of `<svg>` are not rendered

### 1.3 ThorVG Integration ✅ VERIFIED

ThorVG is already integrated and used extensively for rendering:

**Existing ThorVG usage patterns** (from [render_border.cpp](../radiant/render_border.cpp), [render_background.cpp](../radiant/render_background.cpp)):

```cpp
// Shape creation and path building
Tvg_Paint* shape = tvg_shape_new();
tvg_shape_move_to(shape, x, y);
tvg_shape_line_to(shape, x2, y2);
tvg_shape_cubic_to(shape, cp1_x, cp1_y, cp2_x, cp2_y, end_x, end_y);
tvg_shape_append_rect(shape, x, y, w, h, rx, ry);
tvg_shape_append_circle(shape, cx, cy, rx, ry);
tvg_shape_close(shape);

// Fill and stroke
tvg_shape_set_fill_color(shape, r, g, b, a);
tvg_shape_set_stroke_width(shape, width);
tvg_shape_set_stroke_color(shape, r, g, b, a);
tvg_shape_set_stroke_cap(shape, TVG_STROKE_CAP_ROUND);
tvg_shape_set_stroke_join(shape, TVG_STROKE_JOIN_MITER);
tvg_shape_set_stroke_dash(shape, pattern, count, offset);

// Gradients
Tvg_Gradient* grad = tvg_linear_gradient_new();  // or tvg_radial_gradient_new()
tvg_gradient_set_color_stops(grad, stops, count);
tvg_shape_set_fill_gradient(shape, grad);

// Scene composition
Tvg_Paint* scene = tvg_scene_new();
tvg_scene_push(scene, child_paint);

// Transforms and clipping
tvg_paint_translate(paint, x, y);
tvg_paint_scale(paint, factor);
tvg_paint_set_transform(paint, &matrix);
tvg_paint_set_mask_method(paint, clip_shape, TVG_MASK_METHOD_ALPHA);

// Render to canvas
tvg_canvas_push(canvas, paint);
tvg_canvas_draw(canvas, false);
tvg_canvas_sync(canvas);
```

---

## 2. Architecture: Direct ThorVG Scene Graph Construction

### 2.1 Design Rationale

**Why direct API over serialization:**
- **Performance**: No string allocation/parsing overhead
- **Consistency**: Uses same ThorVG instance as rest of Radiant
- **Control**: Direct access to ThorVG scene graph for future optimizations
- **Caching**: Can cache `Tvg_Paint*` objects directly

### 2.2 SVG Element → ThorVG Mapping

| SVG Element | ThorVG API | Notes |
|-------------|------------|-------|
| `<svg>` | `tvg_scene_new()` | Container with viewBox transform |
| `<g>` | `tvg_scene_new()` | Group container |
| `<rect>` | `tvg_shape_append_rect()` | Rounded corners via rx/ry |
| `<circle>` | `tvg_shape_append_circle()` | cx, cy, r |
| `<ellipse>` | `tvg_shape_append_circle()` | cx, cy, rx, ry |
| `<line>` | `tvg_shape_move_to()` + `line_to()` | x1,y1 → x2,y2 |
| `<polyline>` | `tvg_shape_move_to()` + `line_to()` | points array |
| `<polygon>` | polyline + `tvg_shape_close()` | closed path |
| `<path>` | `tvg_shape_append_path()` | Parse d= attribute |
| `<text>` | Custom text rendering | Use existing font system |
| `<linearGradient>` | `tvg_linear_gradient_new()` | Gradient definitions |
| `<radialGradient>` | `tvg_radial_gradient_new()` | Gradient definitions |
| `<clipPath>` | `tvg_paint_set_clip()` | Clipping paths |
| `<defs>` | (skip rendering) | Definition container |
| `<use>` | Clone referenced element | xlink:href resolution |

---

## 3. Implementation Plan

### Phase 1: Core SVG Renderer Module

**New files**:
- `radiant/render_svg.hpp` - SVG rendering declarations
- `radiant/render_svg_elements.cpp` - SVG element → ThorVG conversion

#### 3.1 Main Entry Point

```cpp
// radiant/render_svg.hpp
#pragma once
#include "view.hpp"

/**
 * Build ThorVG scene graph from inline SVG element
 * @param svg_element The <svg> Element from HTML5 parser
 * @param viewport_width Target rendering width
 * @param viewport_height Target rendering height
 * @return ThorVG scene containing all SVG content
 */
Tvg_Paint* build_svg_scene(Element* svg_element, float viewport_width, float viewport_height);

/**
 * Render inline SVG element using ThorVG
 */
void render_inline_svg(RenderContext* rdcon, ViewBlock* view);
```

#### 3.2 SVG Rendering Context

```cpp
// Internal context for SVG rendering
struct SvgRenderContext {
    Element* svg_root;           // Root <svg> element
    Pool* pool;                  // Memory pool
    
    // viewBox transform
    float viewbox_x, viewbox_y;
    float viewbox_width, viewbox_height;
    float scale_x, scale_y;      // viewport / viewBox ratio
    float translate_x, translate_y;
    
    // Current state (inherited)
    Color fill_color;
    Color stroke_color;
    float stroke_width;
    float opacity;
    
    // Gradient/pattern definitions (from <defs>)
    HashMap* defs;               // id → Tvg_Gradient* or Tvg_Paint*
};
```

#### 3.3 Element Dispatching

```cpp
Tvg_Paint* render_svg_element(SvgRenderContext* ctx, Element* elem) {
    const char* tag = element_tag_name(elem);
    
    if (strcmp(tag, "rect") == 0) {
        return render_svg_rect(ctx, elem);
    } else if (strcmp(tag, "circle") == 0) {
        return render_svg_circle(ctx, elem);
    } else if (strcmp(tag, "ellipse") == 0) {
        return render_svg_ellipse(ctx, elem);
    } else if (strcmp(tag, "line") == 0) {
        return render_svg_line(ctx, elem);
    } else if (strcmp(tag, "polyline") == 0) {
        return render_svg_polyline(ctx, elem);
    } else if (strcmp(tag, "polygon") == 0) {
        return render_svg_polygon(ctx, elem);
    } else if (strcmp(tag, "path") == 0) {
        return render_svg_path(ctx, elem);
    } else if (strcmp(tag, "g") == 0) {
        return render_svg_group(ctx, elem);
    } else if (strcmp(tag, "text") == 0) {
        return render_svg_text(ctx, elem);
    } else if (strcmp(tag, "use") == 0) {
        return render_svg_use(ctx, elem);
    } else if (strcmp(tag, "defs") == 0) {
        process_svg_defs(ctx, elem);  // Populate ctx->defs
        return nullptr;
    } else if (strcmp(tag, "linearGradient") == 0 ||
               strcmp(tag, "radialGradient") == 0) {
        // Gradients are processed via <defs>, skip direct rendering
        return nullptr;
    }
    
    // Unknown element - try rendering children as group
    return render_svg_children_as_scene(ctx, elem);
}
```

### Phase 2: Basic Shape Rendering

#### 2.1 Rectangle

```cpp
Tvg_Paint* render_svg_rect(SvgRenderContext* ctx, Element* elem) {
    float x = parse_svg_length(get_attr(elem, "x"), 0);
    float y = parse_svg_length(get_attr(elem, "y"), 0);
    float width = parse_svg_length(get_attr(elem, "width"), 0);
    float height = parse_svg_length(get_attr(elem, "height"), 0);
    float rx = parse_svg_length(get_attr(elem, "rx"), 0);
    float ry = parse_svg_length(get_attr(elem, "ry"), rx);  // defaults to rx
    
    Tvg_Paint* shape = tvg_shape_new();
    tvg_shape_append_rect(shape, x, y, width, height, rx, ry);
    
    apply_svg_fill_stroke(ctx, shape, elem);
    apply_svg_transform(ctx, shape, elem);
    
    return shape;
}
```

#### 2.2 Circle and Ellipse

```cpp
Tvg_Paint* render_svg_circle(SvgRenderContext* ctx, Element* elem) {
    float cx = parse_svg_length(get_attr(elem, "cx"), 0);
    float cy = parse_svg_length(get_attr(elem, "cy"), 0);
    float r = parse_svg_length(get_attr(elem, "r"), 0);
    
    Tvg_Paint* shape = tvg_shape_new();
    tvg_shape_append_circle(shape, cx, cy, r, r);
    
    apply_svg_fill_stroke(ctx, shape, elem);
    apply_svg_transform(ctx, shape, elem);
    
    return shape;
}

Tvg_Paint* render_svg_ellipse(SvgRenderContext* ctx, Element* elem) {
    float cx = parse_svg_length(get_attr(elem, "cx"), 0);
    float cy = parse_svg_length(get_attr(elem, "cy"), 0);
    float rx = parse_svg_length(get_attr(elem, "rx"), 0);
    float ry = parse_svg_length(get_attr(elem, "ry"), 0);
    
    Tvg_Paint* shape = tvg_shape_new();
    tvg_shape_append_circle(shape, cx, cy, rx, ry);
    
    apply_svg_fill_stroke(ctx, shape, elem);
    apply_svg_transform(ctx, shape, elem);
    
    return shape;
}
```

#### 2.3 Line, Polyline, Polygon

```cpp
Tvg_Paint* render_svg_line(SvgRenderContext* ctx, Element* elem) {
    float x1 = parse_svg_length(get_attr(elem, "x1"), 0);
    float y1 = parse_svg_length(get_attr(elem, "y1"), 0);
    float x2 = parse_svg_length(get_attr(elem, "x2"), 0);
    float y2 = parse_svg_length(get_attr(elem, "y2"), 0);
    
    Tvg_Paint* shape = tvg_shape_new();
    tvg_shape_move_to(shape, x1, y1);
    tvg_shape_line_to(shape, x2, y2);
    
    apply_svg_stroke(ctx, shape, elem);  // lines have no fill
    apply_svg_transform(ctx, shape, elem);
    
    return shape;
}

Tvg_Paint* render_svg_polyline(SvgRenderContext* ctx, Element* elem) {
    const char* points_str = get_attr(elem, "points");
    if (!points_str) return nullptr;
    
    Tvg_Paint* shape = tvg_shape_new();
    
    // Parse points: "x1,y1 x2,y2 x3,y3 ..."
    float x, y;
    bool first = true;
    const char* p = points_str;
    while (parse_point_pair(&p, &x, &y)) {
        if (first) {
            tvg_shape_move_to(shape, x, y);
            first = false;
        } else {
            tvg_shape_line_to(shape, x, y);
        }
    }
    
    apply_svg_fill_stroke(ctx, shape, elem);
    apply_svg_transform(ctx, shape, elem);
    
    return shape;
}

Tvg_Paint* render_svg_polygon(SvgRenderContext* ctx, Element* elem) {
    Tvg_Paint* shape = render_svg_polyline(ctx, elem);
    if (shape) {
        tvg_shape_close(shape);
    }
    return shape;
}
```

### Phase 3: Path Rendering (SVG `d` attribute)

#### 3.1 Path Command Parser

```cpp
// SVG path command types
enum SvgPathCmd {
    SVG_CMD_MOVE_TO,       // M/m
    SVG_CMD_LINE_TO,       // L/l
    SVG_CMD_HLINE_TO,      // H/h
    SVG_CMD_VLINE_TO,      // V/v
    SVG_CMD_CUBIC_TO,      // C/c
    SVG_CMD_SMOOTH_CUBIC,  // S/s
    SVG_CMD_QUAD_TO,       // Q/q
    SVG_CMD_SMOOTH_QUAD,   // T/t
    SVG_CMD_ARC_TO,        // A/a
    SVG_CMD_CLOSE,         // Z/z
};

Tvg_Paint* render_svg_path(SvgRenderContext* ctx, Element* elem) {
    const char* d = get_attr(elem, "d");
    if (!d) return nullptr;
    
    Tvg_Paint* shape = tvg_shape_new();
    
    float cur_x = 0, cur_y = 0;       // current position
    float start_x = 0, start_y = 0;   // subpath start
    float last_ctrl_x = 0, last_ctrl_y = 0;  // for smooth curves
    
    const char* p = d;
    while (*p) {
        skip_whitespace_comma(&p);
        if (!*p) break;
        
        char cmd = *p++;
        bool relative = islower(cmd);
        cmd = toupper(cmd);
        
        switch (cmd) {
            case 'M': {
                float x, y;
                parse_number(&p, &x);
                parse_number(&p, &y);
                if (relative) { x += cur_x; y += cur_y; }
                tvg_shape_move_to(shape, x, y);
                cur_x = start_x = x;
                cur_y = start_y = y;
                // Subsequent coords are implicit LineTo
                while (peek_number(p)) {
                    parse_number(&p, &x);
                    parse_number(&p, &y);
                    if (relative) { x += cur_x; y += cur_y; }
                    tvg_shape_line_to(shape, x, y);
                    cur_x = x; cur_y = y;
                }
                break;
            }
            case 'L': {
                float x, y;
                while (peek_number(p)) {
                    parse_number(&p, &x);
                    parse_number(&p, &y);
                    if (relative) { x += cur_x; y += cur_y; }
                    tvg_shape_line_to(shape, x, y);
                    cur_x = x; cur_y = y;
                }
                break;
            }
            case 'H': {
                float x;
                while (peek_number(p)) {
                    parse_number(&p, &x);
                    if (relative) { x += cur_x; }
                    tvg_shape_line_to(shape, x, cur_y);
                    cur_x = x;
                }
                break;
            }
            case 'V': {
                float y;
                while (peek_number(p)) {
                    parse_number(&p, &y);
                    if (relative) { y += cur_y; }
                    tvg_shape_line_to(shape, cur_x, y);
                    cur_y = y;
                }
                break;
            }
            case 'C': {
                float x1, y1, x2, y2, x, y;
                while (peek_number(p)) {
                    parse_number(&p, &x1);
                    parse_number(&p, &y1);
                    parse_number(&p, &x2);
                    parse_number(&p, &y2);
                    parse_number(&p, &x);
                    parse_number(&p, &y);
                    if (relative) {
                        x1 += cur_x; y1 += cur_y;
                        x2 += cur_x; y2 += cur_y;
                        x += cur_x; y += cur_y;
                    }
                    tvg_shape_cubic_to(shape, x1, y1, x2, y2, x, y);
                    last_ctrl_x = x2; last_ctrl_y = y2;
                    cur_x = x; cur_y = y;
                }
                break;
            }
            case 'S': {
                // Smooth cubic: reflect previous control point
                float x2, y2, x, y;
                while (peek_number(p)) {
                    float x1 = 2 * cur_x - last_ctrl_x;
                    float y1 = 2 * cur_y - last_ctrl_y;
                    parse_number(&p, &x2);
                    parse_number(&p, &y2);
                    parse_number(&p, &x);
                    parse_number(&p, &y);
                    if (relative) {
                        x2 += cur_x; y2 += cur_y;
                        x += cur_x; y += cur_y;
                    }
                    tvg_shape_cubic_to(shape, x1, y1, x2, y2, x, y);
                    last_ctrl_x = x2; last_ctrl_y = y2;
                    cur_x = x; cur_y = y;
                }
                break;
            }
            case 'Q': {
                // Quadratic Bezier → convert to cubic
                float qx, qy, x, y;
                while (peek_number(p)) {
                    parse_number(&p, &qx);
                    parse_number(&p, &qy);
                    parse_number(&p, &x);
                    parse_number(&p, &y);
                    if (relative) {
                        qx += cur_x; qy += cur_y;
                        x += cur_x; y += cur_y;
                    }
                    // Convert Q to C: control points at 2/3 along Q handles
                    float cx1 = cur_x + 2.0f/3.0f * (qx - cur_x);
                    float cy1 = cur_y + 2.0f/3.0f * (qy - cur_y);
                    float cx2 = x + 2.0f/3.0f * (qx - x);
                    float cy2 = y + 2.0f/3.0f * (qy - y);
                    tvg_shape_cubic_to(shape, cx1, cy1, cx2, cy2, x, y);
                    last_ctrl_x = qx; last_ctrl_y = qy;
                    cur_x = x; cur_y = y;
                }
                break;
            }
            case 'A': {
                // Arc: convert to cubic Bezier approximation
                float rx, ry, rotation, x, y;
                int large_arc, sweep;
                while (peek_number(p)) {
                    parse_number(&p, &rx);
                    parse_number(&p, &ry);
                    parse_number(&p, &rotation);
                    parse_flag(&p, &large_arc);
                    parse_flag(&p, &sweep);
                    parse_number(&p, &x);
                    parse_number(&p, &y);
                    if (relative) { x += cur_x; y += cur_y; }
                    
                    // Convert arc to cubic bezier segments
                    arc_to_beziers(shape, cur_x, cur_y, rx, ry, 
                                   rotation, large_arc, sweep, x, y);
                    cur_x = x; cur_y = y;
                }
                break;
            }
            case 'Z': {
                tvg_shape_close(shape);
                cur_x = start_x;
                cur_y = start_y;
                break;
            }
        }
    }
    
    apply_svg_fill_stroke(ctx, shape, elem);
    apply_svg_transform(ctx, shape, elem);
    
    return shape;
}
```

### Phase 4: Fill and Stroke Application

```cpp
void apply_svg_fill_stroke(SvgRenderContext* ctx, Tvg_Paint* shape, Element* elem) {
    // Get fill
    const char* fill = get_attr(elem, "fill");
    if (!fill) fill = "black";  // SVG default
    
    if (strcmp(fill, "none") != 0) {
        if (strncmp(fill, "url(#", 5) == 0) {
            // Reference to gradient/pattern
            char id[64];
            extract_url_id(fill, id, sizeof(id));
            Tvg_Gradient* grad = (Tvg_Gradient*)hashmap_get(ctx->defs, id);
            if (grad) {
                tvg_shape_set_fill_gradient(shape, tvg_gradient_duplicate(grad));
            }
        } else {
            // Solid color
            Color c = parse_svg_color(fill);
            float opacity = parse_float(get_attr(elem, "fill-opacity"), 1.0f);
            tvg_shape_set_fill_color(shape, c.r, c.g, c.b, (uint8_t)(c.a * opacity));
        }
    }
    
    // Get stroke
    const char* stroke = get_attr(elem, "stroke");
    if (stroke && strcmp(stroke, "none") != 0) {
        float stroke_width = parse_svg_length(get_attr(elem, "stroke-width"), 1.0f);
        tvg_shape_set_stroke_width(shape, stroke_width);
        
        if (strncmp(stroke, "url(#", 5) == 0) {
            char id[64];
            extract_url_id(stroke, id, sizeof(id));
            Tvg_Gradient* grad = (Tvg_Gradient*)hashmap_get(ctx->defs, id);
            if (grad) {
                tvg_shape_set_stroke_gradient(shape, tvg_gradient_duplicate(grad));
            }
        } else {
            Color c = parse_svg_color(stroke);
            float opacity = parse_float(get_attr(elem, "stroke-opacity"), 1.0f);
            tvg_shape_set_stroke_color(shape, c.r, c.g, c.b, (uint8_t)(c.a * opacity));
        }
        
        // Stroke properties
        const char* linecap = get_attr(elem, "stroke-linecap");
        if (linecap) {
            if (strcmp(linecap, "round") == 0) tvg_shape_set_stroke_cap(shape, TVG_STROKE_CAP_ROUND);
            else if (strcmp(linecap, "square") == 0) tvg_shape_set_stroke_cap(shape, TVG_STROKE_CAP_SQUARE);
            else tvg_shape_set_stroke_cap(shape, TVG_STROKE_CAP_BUTT);
        }
        
        const char* linejoin = get_attr(elem, "stroke-linejoin");
        if (linejoin) {
            if (strcmp(linejoin, "round") == 0) tvg_shape_set_stroke_join(shape, TVG_STROKE_JOIN_ROUND);
            else if (strcmp(linejoin, "bevel") == 0) tvg_shape_set_stroke_join(shape, TVG_STROKE_JOIN_BEVEL);
            else tvg_shape_set_stroke_join(shape, TVG_STROKE_JOIN_MITER);
        }
        
        // Dash array
        const char* dasharray = get_attr(elem, "stroke-dasharray");
        if (dasharray && strcmp(dasharray, "none") != 0) {
            float dashes[16];
            int count = parse_dash_array(dasharray, dashes, 16);
            float offset = parse_float(get_attr(elem, "stroke-dashoffset"), 0);
            tvg_shape_set_stroke_dash(shape, dashes, count, offset);
        }
    }
}
```

### Phase 5: Gradients and Definitions

```cpp
void process_svg_defs(SvgRenderContext* ctx, Element* defs) {
    for (int i = 0; i < defs->length; i++) {
        Element* child = get_child_element(defs, i);
        if (!child) continue;
        
        const char* tag = element_tag_name(child);
        const char* id = get_attr(child, "id");
        if (!id) continue;
        
        if (strcmp(tag, "linearGradient") == 0) {
            Tvg_Gradient* grad = create_linear_gradient(ctx, child);
            hashmap_set(ctx->defs, id, grad);
        } else if (strcmp(tag, "radialGradient") == 0) {
            Tvg_Gradient* grad = create_radial_gradient(ctx, child);
            hashmap_set(ctx->defs, id, grad);
        } else if (strcmp(tag, "clipPath") == 0) {
            Tvg_Paint* clip = render_svg_children_as_scene(ctx, child);
            hashmap_set(ctx->defs, id, clip);
        }
    }
}

Tvg_Gradient* create_linear_gradient(SvgRenderContext* ctx, Element* elem) {
    float x1 = parse_svg_percent(get_attr(elem, "x1"), 0);
    float y1 = parse_svg_percent(get_attr(elem, "y1"), 0);
    float x2 = parse_svg_percent(get_attr(elem, "x2"), 100);
    float y2 = parse_svg_percent(get_attr(elem, "y2"), 0);
    
    Tvg_Gradient* grad = tvg_linear_gradient_new();
    tvg_linear_gradient_set(grad, x1, y1, x2, y2);
    
    // Collect color stops from <stop> children
    Tvg_Color_Stop stops[32];
    int stop_count = 0;
    
    for (int i = 0; i < elem->length && stop_count < 32; i++) {
        Element* stop_elem = get_child_element(elem, i);
        if (!stop_elem || strcmp(element_tag_name(stop_elem), "stop") != 0) continue;
        
        float offset = parse_svg_percent(get_attr(stop_elem, "offset"), 0) / 100.0f;
        Color c = parse_svg_color(get_attr(stop_elem, "stop-color"));
        float opacity = parse_float(get_attr(stop_elem, "stop-opacity"), 1.0f);
        
        stops[stop_count].offset = offset;
        stops[stop_count].r = c.r;
        stops[stop_count].g = c.g;
        stops[stop_count].b = c.b;
        stops[stop_count].a = (uint8_t)(255 * opacity);
        stop_count++;
    }
    
    tvg_gradient_set_color_stops(grad, stops, stop_count);
    
    return grad;
}
```

### Phase 6: Layout Integration - viewBox and Sizing

**File**: `radiant/layout.cpp`

```cpp
struct SvgViewBox {
    float min_x, min_y;
    float width, height;
    bool has_viewbox;
};

SvgViewBox parse_svg_viewbox(const char* viewbox_attr) {
    SvgViewBox vb = {0, 0, 0, 0, false};
    if (!viewbox_attr) return vb;
    
    if (sscanf(viewbox_attr, "%f %f %f %f", 
               &vb.min_x, &vb.min_y, &vb.width, &vb.height) == 4) {
        vb.has_viewbox = true;
    }
    return vb;
}

struct SvgSize {
    float width;
    float height;
    float aspect_ratio;
    bool has_intrinsic_width;
    bool has_intrinsic_height;
};

SvgSize calculate_svg_intrinsic_size(Element* svg_element) {
    SvgSize size = {300, 150, 2.0f, false, false};  // HTML default
    
    const char* width_attr = get_attr(svg_element, "width");
    const char* height_attr = get_attr(svg_element, "height");
    const char* viewbox_attr = get_attr(svg_element, "viewBox");
    
    SvgViewBox vb = parse_svg_viewbox(viewbox_attr);
    
    if (width_attr) {
        size.width = parse_svg_length(width_attr, 300);
        size.has_intrinsic_width = true;
    } else if (vb.has_viewbox) {
        size.width = vb.width;
        size.has_intrinsic_width = true;
    }
    
    if (height_attr) {
        size.height = parse_svg_length(height_attr, 150);
        size.has_intrinsic_height = true;
    } else if (vb.has_viewbox) {
        size.height = vb.height;
        size.has_intrinsic_height = true;
    }
    
    if (size.height > 0) {
        size.aspect_ratio = size.width / size.height;
    }
    
    return size;
}
```

### Phase 7: Render Integration

```cpp
// In radiant/render.cpp - add to render_block_view()
void render_inline_svg(RenderContext* rdcon, ViewBlock* view) {
    if (!view->dom_elem || !view->dom_elem->html_element) return;
    
    Element* svg_elem = view->dom_elem->html_element;
    float scale = rdcon->scale;
    
    // Build ThorVG scene from SVG element tree
    Tvg_Paint* svg_scene = build_svg_scene(svg_elem, view->width, view->height);
    if (!svg_scene) return;
    
    // Position in document coordinates
    float x = rdcon->block.x + view->x * scale;
    float y = rdcon->block.y + view->y * scale;
    
    tvg_paint_translate(svg_scene, x, y);
    tvg_paint_scale(svg_scene, scale);
    
    // Apply clipping
    Tvg_Paint* clip_rect = tvg_shape_new();
    Bound* clip = &rdcon->block.clip;
    tvg_shape_append_rect(clip_rect, clip->left, clip->top,
        clip->right - clip->left, clip->bottom - clip->top, 0, 0);
    tvg_shape_set_fill_color(clip_rect, 0, 0, 0, 255);
    tvg_paint_set_mask_method(svg_scene, clip_rect, TVG_MASK_METHOD_ALPHA);
    
    // Render
    if (rdcon->has_transform) {
        tvg_paint_set_transform(svg_scene, &rdcon->transform);
    }
    tvg_canvas_push(rdcon->canvas, svg_scene);
}

Tvg_Paint* build_svg_scene(Element* svg_element, float viewport_width, float viewport_height) {
    SvgRenderContext ctx = {};
    ctx.svg_root = svg_element;
    ctx.defs = hashmap_new(sizeof(void*), 32, 0, 0, hash_string, cmp_string, nullptr, nullptr);
    
    // Parse viewBox and set up transform
    SvgViewBox vb = parse_svg_viewbox(get_attr(svg_element, "viewBox"));
    if (vb.has_viewbox && vb.width > 0 && vb.height > 0) {
        ctx.viewbox_x = vb.min_x;
        ctx.viewbox_y = vb.min_y;
        ctx.viewbox_width = vb.width;
        ctx.viewbox_height = vb.height;
        ctx.scale_x = viewport_width / vb.width;
        ctx.scale_y = viewport_height / vb.height;
        ctx.translate_x = -vb.min_x * ctx.scale_x;
        ctx.translate_y = -vb.min_y * ctx.scale_y;
    } else {
        ctx.scale_x = ctx.scale_y = 1.0f;
    }
    
    // Default styles
    ctx.fill_color = {0, 0, 0, 255};   // black
    ctx.stroke_color = {0, 0, 0, 0};   // none
    ctx.stroke_width = 1.0f;
    ctx.opacity = 1.0f;
    
    // Create root scene
    Tvg_Paint* scene = tvg_scene_new();
    
    // Apply viewBox transform to scene
    if (vb.has_viewbox) {
        tvg_paint_scale(scene, ctx.scale_x);  // Uniform scale for now
        tvg_paint_translate(scene, ctx.translate_x, ctx.translate_y);
    }
    
    // Render children
    for (int i = 0; i < svg_element->length; i++) {
        Element* child = get_child_element(svg_element, i);
        if (!child) continue;
        
        Tvg_Paint* child_paint = render_svg_element(&ctx, child);
        if (child_paint) {
            tvg_scene_push(scene, child_paint);
        }
    }
    
    hashmap_free(ctx.defs);
    return scene;
}
```

---

## 4. File Changes Summary

| File | Change Type | Description |
|------|-------------|-------------|
| `radiant/render_svg_inline.cpp` | NEW | SVG element → ThorVG conversion |
| `radiant/render_svg_inline.hpp` | NEW | SVG rendering declarations |
| `radiant/svg_path_parser.cpp` | NEW | SVG path `d` attribute parser |
| `radiant/svg_path_parser.hpp` | NEW | Path parser declarations |
| `radiant/layout.cpp` | MODIFY | SVG intrinsic size calculation |
| `radiant/render.cpp` | MODIFY | Call `render_inline_svg()` for SVG blocks |
| `radiant/view.hpp` | MODIFY | Add SvgViewBox struct |

---

## 5. Implementation Priority

| Priority | Feature | ThorVG API | Complexity |
|----------|---------|------------|------------|
| P0 | `<rect>`, `<circle>`, `<ellipse>` | `append_rect`, `append_circle` | Low |
| P0 | `<line>`, `<polyline>`, `<polygon>` | `move_to`, `line_to`, `close` | Low |
| P0 | Solid fill/stroke colors | `set_fill_color`, `set_stroke_*` | Low |
| P1 | `<path>` (M,L,C,Z commands) | `move_to`, `line_to`, `cubic_to` | Medium |
| P1 | `<g>` groups | `tvg_scene_new`, `scene_push` | Low |
| P1 | viewBox transform | `tvg_paint_scale`, `translate` | Medium |
| P2 | `<path>` (Q,S,T,A commands) | Bezier conversion, arc approx | High |
| P2 | `<linearGradient>` | `tvg_linear_gradient_*` | Medium |
| P2 | `<radialGradient>` | `tvg_radial_gradient_*` | Medium |
| P3 | `<text>` | Existing font system | Medium |
| P3 | `<clipPath>` | `tvg_paint_set_clip` | Medium |
| P3 | `<use>` references | Element cloning | Medium |
| P4 | `transform` attribute | Matrix parsing | Medium |
| P4 | `<pattern>` fills | Complex | High |

---

## 6. Test Plan

### 6.1 Unit Tests

```cpp
// test_svg_render.cpp
TEST(SvgRender, BasicRect) {
    Element* svg = parse_html("<svg><rect x='10' y='10' width='80' height='30' fill='red'/></svg>");
    Tvg_Paint* scene = build_svg_scene(svg, 100, 50);
    ASSERT_NE(scene, nullptr);
}

TEST(SvgRender, CircleWithStroke) {
    Element* svg = parse_html("<svg><circle cx='50' cy='50' r='40' fill='blue' stroke='black' stroke-width='2'/></svg>");
    Tvg_Paint* scene = build_svg_scene(svg, 100, 100);
    ASSERT_NE(scene, nullptr);
}

TEST(SvgRender, PathMLC) {
    Element* svg = parse_html("<svg><path d='M10 10 L90 10 C90 50 50 90 10 50 Z' fill='green'/></svg>");
    Tvg_Paint* scene = build_svg_scene(svg, 100, 100);
    ASSERT_NE(scene, nullptr);
}

TEST(SvgRender, ViewBoxScaling) {
    Element* svg = parse_html("<svg viewBox='0 0 100 100' width='200' height='200'><rect width='100' height='100'/></svg>");
    // Verify viewBox scaling transforms content correctly
}

TEST(SvgRender, LinearGradient) {
    const char* html = R"(
        <svg>
            <defs><linearGradient id='g1'><stop offset='0%' stop-color='red'/><stop offset='100%' stop-color='blue'/></linearGradient></defs>
            <rect fill='url(#g1)' width='100' height='100'/>
        </svg>
    )";
    Element* svg = parse_html(html);
    Tvg_Paint* scene = build_svg_scene(svg, 100, 100);
    ASSERT_NE(scene, nullptr);
}
```

### 6.2 Visual Regression Tests

Add to `test/layout/`:
- `test_inline_svg_shapes.html` - basic shapes
- `test_inline_svg_path.html` - path commands
- `test_inline_svg_gradient.html` - gradient fills
- `test_inline_svg_viewbox.html` - viewBox scaling

---

## 7. Timeline Estimate

| Phase | Duration | Dependencies |
|-------|----------|--------------|
| Phase 1-2: Basic shapes | 2-3 days | None |
| Phase 3: Path parser | 3-4 days | Phase 1 |
| Phase 4: Fill/stroke | 1-2 days | Phase 1 |
| Phase 5: Gradients | 2-3 days | Phase 4 |
| Phase 6: Layout/viewBox | 2 days | Phase 1 |
| Phase 7: Integration | 1-2 days | All above |
| Testing & Polish | 2-3 days | All phases |
| **Total** | **13-19 days** | |

---

## 8. Future Enhancements (Out of Scope)

- **SVG animations** (`<animate>`, `<animateTransform>`)
- **SVG filters** (`<filter>`, `<feGaussianBlur>`, etc.)
- **CSS styling of SVG** (external stylesheets)
- **`<foreignObject>`** (HTML inside SVG)
- **`<image>`** (external image references)
- **`<marker>`** (line markers)

---

## 9. References

- [WHATWG HTML5 Parsing - SVG](https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-inforeign)
- [SVG 2 Specification](https://www.w3.org/TR/SVG2/)
- [SVG Path Specification](https://www.w3.org/TR/SVG/paths.html)
- [ThorVG C API](https://www.thorvg.org/apis) (via `./mac-deps/thorvg/inc/thorvg.h`)
- [Existing ThorVG usage in Radiant](../radiant/render_border.cpp)
