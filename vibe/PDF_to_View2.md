# PDF to Radiant View Enhanced Rendering Proposal

## Executive Summary

This proposal outlines enhancements to achieve **full PDF rendering fidelity** in Radiant, comparable to Mozilla's PDF.js. Based on analysis of pdf.js architecture and the current Radiant implementation, we identify gaps and propose a phased implementation plan to achieve complete PDF rendering.

**Goal**: Full PDF document rendering under Radiant with pixel-perfect accuracy, matching pdf.js capabilities.

### Key Design Principles

1. **Reuse HTML/CSS View Infrastructure**: Maximize reuse of existing Radiant view types (`ViewBlock`, `ViewText`, `ViewImage`, etc.) and their associated rendering pipelines
2. **Unified Rendering Backend**: Use FreeType for text and ThorVG for vector graphics - the same backends used by HTML/CSS rendering. No direct OpenGL calls.
3. **View Tree Integration**: PDF content maps to the same view tree structures as HTML/CSS, enabling shared layout adjustments and rendering code

---

## Implementation Progress

**Last Updated**: 5 January 2026

### Phase 1: Stream & Compression ✅ COMPLETED

| Task | Status | Files Modified |
|------|--------|----------------|
| FlateDecode (zlib) | ✅ Done | `lambda/input/pdf_decompress.cpp` |
| LZWDecode | ✅ Done | `lambda/input/pdf_decompress.cpp` |
| ASCII85Decode | ✅ Done | `lambda/input/pdf_decompress.cpp` |
| ASCIIHexDecode | ✅ Done | `lambda/input/pdf_decompress.cpp` |
| RunLengthDecode | ✅ Done | `lambda/input/pdf_decompress.cpp` |
| PNG Predictor (10-15) | ✅ Done | `lambda/input/pdf_decompress.cpp` |
| TIFF Predictor (2) | ✅ Done | `lambda/input/pdf_decompress.cpp` |
| DecodeParms extraction | ✅ Done | `radiant/pdf/pdf_to_view.cpp` |

### Phase 2: Font System Enhancement ✅ COMPLETED

| Task | Status | Files Modified |
|------|--------|----------------|
| PDFFontCache structure | ✅ Done | `radiant/pdf/fonts.cpp` |
| FreeType initialization | ✅ Done | `radiant/pdf/fonts.cpp` |
| Embedded font extraction | ✅ Done | `radiant/pdf/fonts.cpp` |
| Type1/TrueType/CFF loading | ✅ Done | `radiant/pdf/fonts.cpp` |
| Font type detection | ✅ Done | `radiant/pdf/fonts.cpp` |
| Glyph width calculation | ✅ Done | `radiant/pdf/fonts.cpp` |
| ft_face in FontProp | ✅ Done | `radiant/view.hpp` |
| pdf_fonts.h header | ✅ Done | `radiant/pdf/pdf_fonts.h` |

### Phase 3: Image Handling ✅ COMPLETED

| Task | Status | Files Modified |
|------|--------|----------------|
| Do operator parsing | ✅ Done | `radiant/pdf/operators.cpp` |
| XObject lookup | ✅ Done | `radiant/pdf/pdf_to_view.cpp` |
| Image XObject handling | ✅ Done | `radiant/pdf/pdf_to_view.cpp` |
| Form XObject (recursive) | ✅ Done | `radiant/pdf/pdf_to_view.cpp` |
| JPEG decoding (DCTDecode) | ✅ Done | `lib/image.c` |
| PNG decoding | ✅ Done | `lib/image.c` |
| Raw image (RGB/Gray/CMYK) | ✅ Done | `radiant/pdf/pdf_to_view.cpp` |
| image_load_from_memory() | ✅ Done | `lib/image.h`, `lib/image.c` |
| 1-bit/4-bit/8-bit images | ✅ Done | `radiant/pdf/pdf_to_view.cpp` |

### Phase 4: Color Spaces ✅ COMPLETED

| Task | Status | Files Modified |
|------|--------|----------------|
| DeviceRGB | ✅ Done | `radiant/pdf/pdf_to_view.cpp` |
| DeviceGray | ✅ Done | `radiant/pdf/pdf_to_view.cpp` |
| DeviceCMYK | ✅ Done | `radiant/pdf/pdf_to_view.cpp` |
| Indexed color space | ✅ Done | `radiant/pdf/pdf_to_view.cpp`, `radiant/pdf/operators.h` |
| ICCBased profiles | ✅ Done | `radiant/pdf/pdf_to_view.cpp` |
| CalGray/CalRGB | ✅ Done | `radiant/pdf/pdf_to_view.cpp` |
| CS/cs operators | ✅ Done | `radiant/pdf/operators.cpp`, `radiant/pdf/pdf_to_view.cpp` |
| SC/sc/SCN/scn operators | ✅ Done | `radiant/pdf/operators.cpp`, `radiant/pdf/pdf_to_view.cpp` |
| PDFColorSpaceInfo struct | ✅ Done | `radiant/pdf/operators.h` |
| parse_color_space() | ✅ Done | `radiant/pdf/pdf_to_view.cpp` |
| apply_color_space_to_rgb() | ✅ Done | `radiant/pdf/pdf_to_view.cpp` |
| lookup_named_colorspace() | ✅ Done | `radiant/pdf/pdf_to_view.cpp` |
| Lab color space (basic) | ✅ Done | `radiant/pdf/pdf_to_view.cpp` |
| Separation/DeviceN (basic) | ✅ Done | `radiant/pdf/pdf_to_view.cpp` |

### Phase 5-8: Future Work

| Phase | Status | Description |
|-------|--------|-------------|
| Gradients/Shading | ⬜ Not started | Linear/radial gradients, mesh shading |
| Clipping & Transparency | ⬜ Not started | Clip paths, blend modes, soft masks |
| Annotations | ⬜ Not started | Links, comments, form fields |
| Text Selection | ⬜ Not started | ToUnicode CMap, text layer |

### Build Status

✅ **Build successful** - 0 errors, 369 warnings (as of 5 January 2026)

---

## Part 1: PDF.js Architecture Analysis

### 1.1 Overall Architecture

PDF.js is a 5-layer architecture:

```
┌─────────────────────────────────────────────────────────────────┐
│                      Web Viewer Layer                            │
│  (web/viewer.js, web/app.js, web/pdf_viewer.js)                 │
│  UI controls, zoom, navigation, search, thumbnails              │
├─────────────────────────────────────────────────────────────────┤
│                      Display Layer                               │
│  (src/display/api.js, canvas.js, text_layer.js)                 │
│  PDFDocumentProxy, PDFPageProxy, Canvas rendering               │
├─────────────────────────────────────────────────────────────────┤
│                      Scripting Layer                             │
│  (src/scripting_api/)                                            │
│  JavaScript execution for interactive PDFs                       │
├─────────────────────────────────────────────────────────────────┤
│                      Core Layer (Worker)                         │
│  (src/core/evaluator.js, parser.js, fonts.js)                   │
│  PDF parsing, operator evaluation, font handling                │
├─────────────────────────────────────────────────────────────────┤
│                      Shared Utilities                            │
│  (src/shared/util.js, message_handler.js)                       │
│  Constants, messaging, common utilities                          │
└─────────────────────────────────────────────────────────────────┘
```

### 1.2 Key pdf.js Components

#### 1.2.1 Core Evaluator (`src/core/evaluator.js`)
The heart of pdf.js - 5,400+ lines handling:

- **Operator Processing**: All 80+ PDF operators
- **Font Loading**: Type1, TrueType, CFF, Type3 fonts
- **Image Handling**: JPEG, JBIG2, JPX decoders
- **Pattern Processing**: Tiling and shading patterns
- **Graphics State**: Complete state machine with save/restore stack

Key classes:
```javascript
class PartialEvaluator {
  // Main evaluator handling all PDF operations
  async getOperatorList(stream, resources) {...}
  buildPaintImageXObject(resources, image, ...) {...}
  handleText(operator, args) {...}
  buildPath(operatorList, fn, args, parsingText) {...}
}
```

#### 1.2.2 Operator List (`src/core/operator_list.js`)
Efficient intermediate representation:

```javascript
const OPS = {
  // 80+ operations organized by category
  dependency: 1,
  setLineWidth: 2,
  setLineCap: 3,
  setLineJoin: 4,
  setMiterLimit: 5,
  setDash: 6,
  // ... transforms, paths, text, images, etc.
  paintImageXObject: 85,
  paintInlineImageXObject: 86,
  paintImageMaskXObject: 89,
  // ... patterns, shading, etc.
};
```

Features:
- Operator batching for performance
- Dependency tracking between operations
- Optimization passes (inline image grouping, mask grouping)

#### 1.2.3 Font System (`src/core/fonts.js`, `font_renderer.js`)
Complete font handling:

```javascript
class Font {
  // Font types: Type1, TrueType, OpenType, CFF, Type3
  constructor(properties) {
    this.name = properties.name;
    this.type = properties.type;
    this.widths = properties.widths;
    this.toUnicode = properties.toUnicode;
    // ... glyph mapping, encoding, etc.
  }
}

// Font renderer for Type3 and fallback rendering
class FontRendererFactory {
  static create(font, seacAnalysisEnabled) {
    // Creates TrueType or CFF renderer
  }
}
```

Key features:
- ToUnicode CMap parsing for text extraction
- Glyph width calculations
- Font substitution for missing fonts
- Embedded font extraction and conversion

#### 1.2.4 Canvas Graphics (`src/display/canvas.js`)
The rendering backend - 3,300+ lines:

```javascript
class CanvasGraphics {
  // Graphics state with full stack
  constructor(canvasCtx, pageWidth, pageHeight, ...) {
    this.current = new CanvasExtraState(width, height);
    this.stateStack = [];
  }
  
  // All 80+ operators implemented
  save() { this.stateStack.push(this.current.clone()); }
  restore() { this.current = this.stateStack.pop(); }
  transform(a, b, c, d, e, f) {...}
  
  // Text rendering
  setFont(fontRefName, size) {...}
  showText(glyphs) {...}
  
  // Path operations
  moveTo(x, y) {...}
  lineTo(x, y) {...}
  curveTo(x1, y1, x2, y2, x3, y3) {...}
  
  // Complex operations
  paintImageXObject(objId) {...}
  shadingFill(patternIR) {...}
  beginAnnotation(id, rect, transform, ...) {...}
}
```

#### 1.2.5 Color Spaces (`src/core/colorspace.js`)
Complete color space support:

- DeviceGray, DeviceRGB, DeviceCMYK
- CalGray, CalRGB, Lab
- ICCBased (with ICC profile parsing)
- Indexed (palette-based)
- Separation, DeviceN
- Pattern

#### 1.2.6 Pattern & Shading (`src/core/pattern.js`)
Gradient and pattern fills:

```javascript
const ShadingType = {
  FUNCTION_BASED: 1,
  AXIAL: 2,        // Linear gradients
  RADIAL: 3,       // Radial gradients
  FREE_FORM_MESH: 4,
  LATTICE_FORM_MESH: 5,
  COONS_PATCH_MESH: 6,
  TENSOR_PATCH_MESH: 7,
};
```

#### 1.2.7 Image Handling (`src/core/image.js`)
Multi-format image decoding:

- JPEG (native browser decode)
- JPEG2000/JPX (WebAssembly decoder)
- JBIG2 (WebAssembly decoder)
- Inline images (BI...EI operators)
- Image masks and soft masks
- ICC color profile application

#### 1.2.8 Text Layer (`src/display/text_layer.js`)
Invisible text overlay for selection:

```javascript
class TextLayer {
  // Creates invisible HTML divs matching text positions
  render() {
    // Process text items with positions
    // Create span elements with transforms
    // Apply font size and rotation
  }
}
```

---

## Part 2: Current Radiant Implementation Analysis

### 2.1 What Radiant Currently Supports

Based on `radiant/pdf/` analysis:

| Feature | Status | Files |
|---------|--------|-------|
| **PDF Parsing** | ✅ Basic | `lambda/input/input-pdf.cpp` |
| **Operator Parser** | ✅ 50+ operators | `operators.cpp`, `operators.h` |
| **Text Operators** | ✅ BT/ET, Tj, TJ, Tf, Tm, Td | `pdf_to_view.cpp` |
| **Path Operators** | ✅ m, l, c, re, h, S, f, B | `pdf_to_view.cpp` |
| **Color (RGB)** | ✅ rg/RG | `pdf_to_view.cpp` |
| **Graphics State** | ✅ q/Q, gs (partial) | `operators.cpp` |
| **Line Width** | ✅ w operator | `operators.cpp` |
| **Dash Patterns** | ✅ d operator | `operators.cpp` |
| **Font Mapping** | ✅ Standard 14 | `fonts.cpp` |
| **Multi-page** | ✅ Page navigation | `pages.cpp` |
| **Coordinate Transform** | ✅ PDF ↔ Screen | `pdf_to_view.cpp`, `coords.cpp` |
| **ThorVG Rendering** | ✅ Curves, dashed lines | `pdf_to_view.cpp` |

### 2.2 Major Gaps vs. pdf.js

| Gap | Priority | Complexity | pdf.js Location |
|-----|----------|------------|-----------------|
| **Stream Decompression** | High | Medium | `src/core/stream.js` |
| **Embedded Images** | High | High | `src/core/image.js` |
| **Embedded Fonts** | High | High | `src/core/fonts.js` |
| **ToUnicode CMap** | High | Medium | `src/core/cmap.js` |
| **Color Spaces (CMYK, ICC)** | Medium | Medium | `src/core/colorspace.js` |
| **Gradients/Shading** | Medium | High | `src/core/pattern.js` |
| **Form XObjects** | Medium | Medium | `src/core/evaluator.js` |
| **Clipping Paths** | Medium | Medium | `src/display/canvas.js` |
| **Transparency/Blending** | Medium | High | `src/display/canvas.js` |
| **Annotations** | Low | High | `src/core/annotation.js` |
| **Text Selection Layer** | Low | Medium | `src/display/text_layer.js` |
| **JavaScript Actions** | Low | Very High | `src/scripting_api/` |

---

## Part 3: Enhancement Proposal

### 3.1 Proposed Architecture

```
PDF Input (binary)
    ↓
Enhanced PDF Parser (decompress streams)
    ↓
PDF Object Graph (resolved references)
    ↓
┌─────────────────────────────────────────────────────────────┐
│              Content Stream Evaluator                        │
│  ┌─────────┬──────────┬──────────┬──────────┬────────────┐ │
│  │ Text    │ Graphics │ Image    │ Color    │ Pattern    │ │
│  │ Handler │ Handler  │ Handler  │ Handler  │ Handler    │ │
│  └─────────┴──────────┴──────────┴──────────┴────────────┘ │
│  ┌─────────────────────────────────────────────────────────┐│
│  │              Graphics State Stack                        ││
│  │  (CTM, colors, fonts, clip, blend, alpha)               ││
│  └─────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────────────────────────┐
│         Radiant View Tree (REUSE HTML/CSS VIEWS)            │
│  ┌───────────┬───────────┬───────────┬───────────────────┐ │
│  │ ViewBlock │ ViewText  │ ViewImage │ VectorPathProp    │ │
│  │ (rects,   │ (FreeType │ (raster   │ (curves, paths    │ │
│  │  boxes)   │  text)    │  images)  │  via ThorVG)      │ │
│  └───────────┴───────────┴───────────┴───────────────────┘ │
│  Properties: BoundaryProp, BackgroundProp, FontProp, etc.  │
└─────────────────────────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────────────────────────┐
│         Existing Radiant Renderer (REUSE)                   │
│  ┌─────────────────────┬───────────────────────────────────┐│
│  │ render_text.cpp     │ render_background.cpp             ││
│  │ (FreeType glyphs)   │ (ThorVG fills)                    ││
│  ├─────────────────────┼───────────────────────────────────┤│
│  │ render_border.cpp   │ render_svg.cpp                    ││
│  │ (ThorVG strokes)    │ (ThorVG paths)                    ││
│  ├─────────────────────┼───────────────────────────────────┤│
│  │ render_img.cpp      │ render_pdf.cpp (enhanced)         ││
│  │ (image blitting)    │ (VectorPath rendering)            ││
│  └─────────────────────┴───────────────────────────────────┘│
└─────────────────────────────────────────────────────────────┘
    ↓
Output: Screen (GLFW/ThorVG) | SVG | PDF | PNG
```

### 3.2 View Type Mapping (PDF → HTML/CSS Views)

The key insight is that PDF graphics primitives map naturally to existing Radiant view types:

| PDF Construct | Radiant View Type | Rendering Backend | Notes |
|---------------|-------------------|-------------------|-------|
| Text (Tj, TJ) | `ViewText` | FreeType | Reuse HTML text rendering |
| Rectangle (re) | `ViewBlock` + `BoundaryProp` | ThorVG | Reuse box model |
| Filled path | `ViewBlock` + `VectorPathProp` | ThorVG | Use existing VectorPath |
| Stroked path | `ViewBlock` + `VectorPathProp` | ThorVG | Use existing VectorPath |
| Bezier curves | `ViewBlock` + `VectorPathProp` | ThorVG | ThorVG cubic bezier |
| Image (Do) | `ViewImage` | ThorVG/stb | Reuse image rendering |
| Gradient fill | `ViewBlock` + `GradientProp` | ThorVG | New prop, ThorVG gradient |
| Clipping path | `ViewBlock` + `ClipProp` | ThorVG | New prop for clip mask |
| Transparency | Existing alpha support | ThorVG | Already supported |

### 3.3 Rendering Pipeline Integration

**Key Principle**: PDF rendering uses the SAME render functions as HTML/CSS:

```cpp
// In render.cpp - unified rendering for both HTML and PDF views
void render_view(View* view, RenderContext* ctx) {
    switch (view->view_type) {
        case RDT_VIEW_BLOCK:
            // Same for HTML divs AND PDF rectangles/paths
            render_block((ViewBlock*)view, ctx);
            break;
        case RDT_VIEW_TEXT:
            // Same for HTML text AND PDF text
            render_text((ViewText*)view, ctx);  // Uses FreeType
            break;
        case RDT_VIEW_IMAGE:
            // Same for HTML img AND PDF images
            render_image((ViewImage*)view, ctx);
            break;
    }
}

// In render_block.cpp - handles VectorPathProp for PDF curves
void render_block(ViewBlock* block, RenderContext* ctx) {
    // Background (HTML backgrounds AND PDF fills)
    if (block->bound && block->bound->background) {
        render_background(block, ctx);  // ThorVG
    }
    
    // Vector path (PDF curves, complex shapes)
    if (block->vpath) {
        render_vector_path(block->vpath, ctx);  // ThorVG - already exists!
    }
    
    // Border (HTML borders AND PDF strokes)
    if (block->bound && block->bound->border) {
        render_border(block, ctx);  // ThorVG
    }
}
```

### 3.4 Implementation Phases

---

## Phase 1: Stream & Compression (Foundation)

**Timeline**: 2 weeks  
**Priority**: Critical - blocks all subsequent work

### 1.1 Stream Decompression Pipeline

**New File**: `lambda/input/pdf_stream.cpp`

```cpp
// PDF Stream handler with filter chain support
class PDFStreamDecoder {
public:
    // Filter types (matching pdf.js)
    enum FilterType {
        FILTER_NONE,
        FILTER_FLATE,      // zlib/deflate
        FILTER_LZW,        // LZW compression
        FILTER_ASCII85,    // ASCII85 encoding
        FILTER_ASCIIHEX,   // Hex encoding
        FILTER_RUNLENGTH,  // Run-length encoding
        FILTER_CCITT,      // CCITT fax (for images)
        FILTER_JBIG2,      // JBIG2 (for images)
        FILTER_DCT,        // JPEG
        FILTER_JPX,        // JPEG2000
        FILTER_CRYPT,      // Encryption
    };
    
    // Decode stream with filter chain
    // pdf.js: src/core/stream.js DecodeStream hierarchy
    Buffer* decode(const Buffer* encoded, 
                   const std::vector<FilterType>& filters,
                   const std::vector<Dict*>& filterParams);
    
private:
    Buffer* decodeFlate(const Buffer* data, Dict* params);
    Buffer* decodeLZW(const Buffer* data, Dict* params);
    Buffer* decodeASCII85(const Buffer* data);
    Buffer* decodeASCIIHex(const Buffer* data);
    Buffer* decodePredictorData(Buffer* data, Dict* params);
};
```

**Key pdf.js References**:
- `src/core/stream.js` - Base stream classes
- `src/core/flate_stream.js` - FlateDecode implementation
- `src/core/lzw_stream.js` - LZWDecode implementation
- `src/core/predictor_stream.js` - PNG/TIFF predictor support

### 1.2 XRef & Object Resolution

**Enhance**: `lambda/input/input-pdf.cpp`

```cpp
// Cross-reference table for object lookup
// pdf.js: src/core/xref.js
class PDFXRef {
public:
    // Resolve indirect object reference
    Item resolveRef(int objNum, int genNum);
    
    // Handle object streams (compressed objects)
    // pdf.js: XRef.fetchCompressed()
    Item fetchFromObjectStream(int objStreamNum, int index);
    
private:
    // XRef entry types
    struct XRefEntry {
        enum Type { FREE, UNCOMPRESSED, COMPRESSED };
        Type type;
        uint32_t offset;     // For uncompressed: byte offset
        uint32_t objStream;  // For compressed: object stream number
        uint16_t index;      // For compressed: index in object stream
        uint16_t gen;
    };
    
    std::vector<XRefEntry> entries;
    std::map<int, Item> cache;  // Object cache
};
```

---

## Phase 2: Font System Enhancement

**Timeline**: 3 weeks  
**Priority**: High - critical for text rendering fidelity

### 2.1 Font Type Support

**Approach**: Leverage FreeType (already used for HTML text) for all PDF fonts.

**New File**: `radiant/pdf/pdf_fonts.cpp`

```cpp
// PDF Font types (matching pdf.js classification)
enum PDFFontType {
    FONT_TYPE1,           // PostScript Type 1
    FONT_TYPE1C,          // CFF-based Type 1
    FONT_TRUETYPE,        // TrueType
    FONT_OPENTYPE,        // OpenType (CFF or TrueType)
    FONT_TYPE3,           // Glyph streams (inline graphics)
    FONT_CID_TYPE0,       // CID-keyed Type 1
    FONT_CID_TYPE0C,      // CID-keyed CFF
    FONT_CID_TYPE2,       // CID-keyed TrueType
};

// Font descriptor from PDF
// pdf.js: src/core/fonts.js Font class
class PDFFont {
public:
    PDFFontType type;
    String* name;
    String* baseFont;
    
    // Metrics
    float* widths;          // Glyph widths
    float defaultWidth;
    float ascent, descent;
    float missingWidth;
    
    // Encoding
    Encoding* encoding;     // Maps char codes to glyph names
    ToUnicodeMap* toUnicode; // Maps char codes to Unicode
    
    // Embedded font data
    Buffer* fontFile;       // Type1 font program
    Buffer* fontFile2;      // TrueType/OpenType
    Buffer* fontFile3;      // CFF data
    
    // Font flags
    bool isSymbolic;
    bool isSerif;
    bool isScript;
    bool isItalic;
    bool isBold;
    bool hasEncoding;
    
    // Convert to FreeType face (REUSE existing font infrastructure)
    // Uses same FreeType loading as HTML fonts
    FT_Face toFreeTypeFace();
    
    // Create FontProp for ViewText (REUSE HTML font property)
    FontProp* toFontProp(Pool* pool, float size);
};
```

### 2.2 Integration with Existing Font System

**Key Insight**: Radiant already has complete font infrastructure for HTML/CSS. PDF fonts should integrate with it:

```cpp
// In radiant/font.cpp - EXTEND existing font loading
// Add support for loading embedded PDF fonts

// Existing function (used by HTML)
FT_Face load_font_by_name(const char* family, FontWeight weight, FontStyle style);

// NEW: Load embedded PDF font data into FreeType
FT_Face load_embedded_font(const uint8_t* data, size_t length, PDFFontType type) {
    FT_Face face;
    
    switch (type) {
        case FONT_TRUETYPE:
        case FONT_OPENTYPE:
            // Direct load - FreeType handles these natively
            FT_New_Memory_Face(ft_library, data, length, 0, &face);
            break;
            
        case FONT_TYPE1:
        case FONT_TYPE1C:
        case FONT_CID_TYPE0C:
            // CFF/Type1 - FreeType also handles these
            FT_New_Memory_Face(ft_library, data, length, 0, &face);
            break;
            
        case FONT_TYPE3:
            // Type3 fonts are graphics - handled separately
            // Render glyph streams to ThorVG paths
            return nullptr;
    }
    
    return face;
}

// Create FontProp that ViewText can use (same as HTML text)
FontProp* PDFFont::toFontProp(Pool* pool, float size) {
    FontProp* prop = (FontProp*)pool_calloc(pool, sizeof(FontProp));
    
    // Try embedded font first
    if (fontFile2 || fontFile3 || fontFile) {
        prop->ft_face = toFreeTypeFace();
        prop->is_embedded = true;
    } else {
        // Fall back to system font (existing font lookup)
        prop->family = map_pdf_font_to_system(baseFont->chars);
    }
    
    prop->font_size = size;
    prop->font_weight = isBold ? CSS_VALUE_BOLD : CSS_VALUE_NORMAL;
    prop->font_style = isItalic ? CSS_VALUE_ITALIC : CSS_VALUE_NORMAL;
    
    return prop;
}
```

### 2.3 ViewText Creation (Reusing HTML Text Rendering)

```cpp
// In pdf_to_view.cpp - create ViewText same as HTML text
ViewText* create_pdf_text_view(Pool* pool, PDFFont* font, 
                                const char* text, float x, float y, float size) {
    ViewText* view = (ViewText*)pool_calloc(pool, sizeof(ViewText));
    view->view_type = RDT_VIEW_TEXT;
    
    // Set text content (same as HTML DomText)
    view->text = text;
    view->length = strlen(text);
    
    // Create TextRect for positioning (same as HTML text layout)
    TextRect* rect = (TextRect*)pool_calloc(pool, sizeof(TextRect));
    rect->x = x;
    rect->y = y;
    rect->height = size;
    view->rect = rect;
    
    // Use FontProp (SAME structure as HTML fonts)
    view->font = font->toFontProp(pool, size);
    
    // Text color (same Color struct as HTML)
    view->color = current_graphics_state.fill_color;
    
    return view;
    // This ViewText will be rendered by existing render_text()
    // which uses FreeType - NO NEW RENDERING CODE NEEDED
}
```

### 2.2 CMap & ToUnicode Support

**New File**: `radiant/pdf/pdf_cmap.cpp`

```cpp
// Character code to CID/Unicode mapping
// pdf.js: src/core/cmap.js
class CMap {
public:
    enum Type {
        CMAP_IDENTITY,      // Identity mapping
        CMAP_STANDARD,      // Predefined CMap
        CMAP_EMBEDDED,      // Embedded in PDF
    };
    
    // Lookup character code
    uint32_t lookup(uint32_t charCode);
    
    // Parse CMap stream
    // pdf.js: CMapFactory.create()
    static CMap* parse(Stream* stream);
    
    // Load predefined CMap (CJK fonts)
    // pdf.js uses bcmaps/ directory
    static CMap* loadPredefined(const char* name);
};

// ToUnicode mapping for text extraction
// pdf.js: src/core/to_unicode_map.js
class ToUnicodeMap {
public:
    // Map char code to Unicode string (can be multi-char)
    String* lookup(uint32_t charCode);
    
    // Parse ToUnicode CMap
    static ToUnicodeMap* parse(Stream* stream);
};
```

### 2.3 FreeType Integration

**Enhance**: `radiant/font.cpp`

```cpp
// Load embedded PDF font into FreeType
// pdf.js: FontLoader, font_loader.js
FreeTypeFace* loadEmbeddedFont(PDFFont* pdfFont) {
    // Handle different font types
    switch (pdfFont->type) {
        case FONT_TYPE1:
        case FONT_TYPE1C:
            // Convert Type1/CFF to TrueType for FreeType
            // pdf.js: OpenTypeFileBuilder
            break;
            
        case FONT_TRUETYPE:
        case FONT_OPENTYPE:
            // Load directly
            FT_New_Memory_Face(library, 
                pdfFont->fontFile2->data,
                pdfFont->fontFile2->length,
                0, &face);
            break;
            
        case FONT_TYPE3:
            // Type3 fonts are inline graphics - special handling
            // pdf.js: charProcOperatorList
            break;
    }
}
```

---

## Phase 3: Image Handling

**Timeline**: 2 weeks  
**Priority**: High - most PDFs contain images

### 3.1 Image Decoder Pipeline

**Approach**: Decode images to RGBA, then use existing `ViewImage` and ThorVG rendering.

**New File**: `radiant/pdf/pdf_image.cpp`

```cpp
// PDF Image handling
// pdf.js: src/core/image.js PDFImage class
class PDFImage {
public:
    int width, height;
    int bitsPerComponent;
    int numComponents;
    bool interpolate;
    bool imageMask;
    
    ColorSpace* colorSpace;
    PDFImage* softMask;     // Transparency mask
    PDFImage* mask;         // Hard mask
    
    // Decode image data to RGBA
    // pdf.js: PDFImage.getData()
    Buffer* decode(const Buffer* encoded);
    
    // Convert to RGBA for rendering (same format as HTML images)
    // pdf.js: PDFImage.fillRgbaBuffer()
    uint8_t* toRGBA();
    
    // Create ViewImage for rendering (REUSE HTML image infrastructure)
    ViewImage* toViewImage(Pool* pool, float x, float y, float w, float h);
    
private:
    void applyDecode(uint8_t* data);
    void convertColorSpace(uint8_t* data);
    void applySoftMask(uint8_t* data);
};
```

### 3.2 Integration with Existing Image Rendering

```cpp
// Create ViewImage using existing infrastructure
ViewImage* PDFImage::toViewImage(Pool* pool, float x, float y, float w, float h) {
    // Decode to RGBA (same format as HTML images)
    uint8_t* rgba = toRGBA();
    
    // Create ViewImage (SAME type used for HTML <img>)
    ViewImage* view = (ViewImage*)pool_calloc(pool, sizeof(ViewImage));
    view->view_type = RDT_VIEW_IMAGE;
    view->x = x;
    view->y = y;
    view->width = w;
    view->height = h;
    
    // Store image data (same as HTML image loading)
    view->image_data = rgba;
    view->image_width = width;
    view->image_height = height;
    view->image_format = IMAGE_FORMAT_RGBA;
    
    return view;
    // Rendered by existing render_img.cpp using ThorVG
    // NO NEW RENDERING CODE NEEDED
}
```

### 3.3 Format-Specific Decoders

```cpp
// JPEG decoder - use stb_image (already in project)
// Same decoder used for HTML <img src="*.jpg">
class JPEGDecoder {
    Buffer* decode(const Buffer* data, ColorSpace* cs) {
        int w, h, channels;
        uint8_t* pixels = stbi_load_from_memory(
            data->data, data->length, &w, &h, &channels, 4);
        return new Buffer(pixels, w * h * 4);
    }
};

// JPEG2000 decoder 
// pdf.js: uses OpenJPEG via WebAssembly
// Consider: OpenJPEG C library
class JPXDecoder {
    Buffer* decode(const Buffer* data);
};

// JBIG2 decoder (bi-level images)
// pdf.js: src/core/jbig2.js + WebAssembly
// Consider: jbig2dec library
class JBIG2Decoder {
    Buffer* decode(const Buffer* data, const Buffer* globals);
};

// CCITT Fax decoder (Group 3/4 fax) - for scanned documents
// pdf.js: src/core/ccitt.js
class CCITTDecoder {
    Buffer* decode(const Buffer* data, int k, bool endOfLine);
};
```

### 3.4 Inline Image Support

```cpp
// Handle BI...ID...EI inline images
// pdf.js: evaluator.js makeInlineImage()
void processInlineImage(PDFStreamParser* parser, ViewBlock* parent) {
    // Parse BI dictionary
    Dict* params = parseInlineImageDict();
    
    // Read data until EI
    Buffer* data = readUntilEI();
    
    // Create PDFImage and decode
    PDFImage* img = new PDFImage(params);
    
    // Create ViewImage (reuses HTML image rendering)
    ViewImage* view = img->toViewImage(pool, x, y, w, h);
    
    // Add to parent (same as HTML images)
    append_child_view((View*)parent, (View*)view);
}
```

---

## Phase 4: Color Space System

**Timeline**: 2 weeks  
**Priority**: Medium - needed for accurate colors

### 4.1 Color Space Classes

**New File**: `radiant/pdf/pdf_colorspace.cpp`

```cpp
// Abstract color space (matches pdf.js ColorSpace class)
// pdf.js: src/core/colorspace.js
class ColorSpace {
public:
    virtual int numComponents() = 0;
    virtual void getRGB(const float* src, uint8_t* dest) = 0;
    
    // Parse color space from PDF
    // pdf.js: ColorSpace.parse()
    static ColorSpace* parse(Item csItem, Resources* resources);
};

// Device color spaces
class DeviceGray : public ColorSpace {
    int numComponents() { return 1; }
    void getRGB(const float* src, uint8_t* dest) {
        dest[0] = dest[1] = dest[2] = (uint8_t)(src[0] * 255);
    }
};

class DeviceRGB : public ColorSpace {
    int numComponents() { return 3; }
    void getRGB(const float* src, uint8_t* dest) {
        dest[0] = (uint8_t)(src[0] * 255);
        dest[1] = (uint8_t)(src[1] * 255);
        dest[2] = (uint8_t)(src[2] * 255);
    }
};

class DeviceCMYK : public ColorSpace {
    int numComponents() { return 4; }
    void getRGB(const float* src, uint8_t* dest) {
        // CMYK to RGB conversion
        // pdf.js: DeviceCmykCS.getRgbItem()
        float c = src[0], m = src[1], y = src[2], k = src[3];
        dest[0] = (uint8_t)(255 * (1 - c) * (1 - k));
        dest[1] = (uint8_t)(255 * (1 - m) * (1 - k));
        dest[2] = (uint8_t)(255 * (1 - y) * (1 - k));
    }
};

// CIE-based color spaces
class CalGray : public ColorSpace { /* Gamma-corrected gray */ };
class CalRGB : public ColorSpace { /* Gamma-corrected RGB with whitepoint */ };
class Lab : public ColorSpace { /* L*a*b* color space */ };

// ICC Profile-based
class ICCBased : public ColorSpace {
    // Use LittleCMS (lcms2) for ICC profiles
    // pdf.js: src/core/icc_colorspace.js
    cmsHTRANSFORM transform;
};

// Indexed (palette)
class Indexed : public ColorSpace {
    ColorSpace* base;
    uint8_t* palette;
    int hival;
};

// Separation (spot colors)
class Separation : public ColorSpace {
    String* name;
    ColorSpace* alternateSpace;
    PDFFunction* tintTransform;
};
```

---

## Phase 5: Pattern & Shading

**Timeline**: 2 weeks  
**Priority**: Medium - needed for gradients

### 5.1 Tiling Patterns

**Approach**: Render pattern content to a tile, then use ThorVG pattern fill.

```cpp
// Tiling pattern (repeated graphics)
// pdf.js: src/display/pattern_helper.js TilingPattern
class TilingPattern {
public:
    enum PaintType { COLORED = 1, UNCOLORED = 2 };
    enum TilingType { CONSTANT = 1, NO_DISTORTION = 2, FAST = 3 };
    
    PaintType paintType;
    TilingType tilingType;
    float bbox[4];
    float xStep, yStep;
    float matrix[6];
    Stream* contentStream;
    
    // Render pattern to tile surface (using existing rendering)
    // The tile itself is rendered using normal view tree rendering
    Tvg_Paint* toThorVGPattern(Resources* resources);
};

// Create pattern using ThorVG (same API as CSS background patterns)
Tvg_Paint* TilingPattern::toThorVGPattern(Resources* resources) {
    // 1. Render pattern content to a temporary view tree
    ViewBlock* tile = renderPatternContent(contentStream, resources);
    
    // 2. Render tile to a bitmap using existing renderer
    Surface* tileSurface = render_to_surface(tile, bbox[2], bbox[3]);
    
    // 3. Create ThorVG picture from bitmap
    Tvg_Paint* picture = tvg_picture_new();
    tvg_picture_load_raw(picture, tileSurface->data, 
                         tileSurface->width, tileSurface->height, true);
    
    // 4. Return for use as fill pattern
    return picture;
}
```

### 5.2 Shading Patterns (Gradients)

**Approach**: Map PDF gradients to ThorVG gradients (same as CSS gradients).

```cpp
// Gradient shading
// pdf.js: src/core/pattern.js
class Shading {
public:
    enum Type {
        AXIAL = 2,      // Linear gradient (like CSS linear-gradient)
        RADIAL = 3,     // Radial gradient (like CSS radial-gradient)
    };
    
    Type type;
    ColorSpace* colorSpace;
    float bbox[4];
    bool extend[2];     // Extend before/after
    
    // Axial-specific
    float coords[4];    // x0, y0, x1, y1
    
    // Radial-specific  
    float radialCoords[6]; // x0, y0, r0, x1, y1, r1
    
    // Color stops
    struct ColorStop {
        float t;
        float color[4];
    };
    std::vector<ColorStop> stops;
    
    // Convert to ThorVG gradient (SAME API as CSS gradients)
    Tvg_Gradient* toThorVGGradient();
};

// ThorVG gradient creation (matches CSS gradient rendering)
Tvg_Gradient* Shading::toThorVGGradient() {
    Tvg_Gradient* grad;
    
    if (type == AXIAL) {
        // Linear gradient - same as CSS linear-gradient
        grad = tvg_linear_gradient_new();
        tvg_linear_gradient_set(grad, coords[0], coords[1], coords[2], coords[3]);
    } else {
        // Radial gradient - same as CSS radial-gradient  
        grad = tvg_radial_gradient_new();
        tvg_radial_gradient_set(grad, radialCoords[3], radialCoords[4], 
                                 radialCoords[5]);
    }
    
    // Add color stops (same format as CSS gradient stops)
    Tvg_Color_Stop* tvgStops = new Tvg_Color_Stop[stops.size()];
    for (size_t i = 0; i < stops.size(); i++) {
        tvgStops[i].offset = stops[i].t;
        tvgStops[i].r = (uint8_t)(stops[i].color[0] * 255);
        tvgStops[i].g = (uint8_t)(stops[i].color[1] * 255);
        tvgStops[i].b = (uint8_t)(stops[i].color[2] * 255);
        tvgStops[i].a = 255;
    }
    tvg_gradient_set_color_stops(grad, tvgStops, stops.size());
    
    return grad;
}
```

### 5.3 Gradient Property for ViewBlock

```cpp
// Add gradient support to existing view properties
// Extends existing BackgroundProp structure

struct GradientProp {
    enum Type { LINEAR, RADIAL };
    Type type;
    float coords[6];  // Linear: x0,y0,x1,y1; Radial: x0,y0,r0,x1,y1,r1
    struct Stop {
        float offset;
        Color color;
    };
    Stop* stops;
    int stop_count;
};

// In render_background.cpp - extend existing gradient rendering
// CSS already supports gradients, so this may already be implemented!
void render_background(ViewBlock* block, RenderContext* ctx) {
    BackgroundProp* bg = block->bound->background;
    
    if (bg->gradient) {
        // Render gradient using ThorVG (same as CSS gradients)
        render_gradient(bg->gradient, block->x, block->y, 
                       block->width, block->height, ctx);
    } else if (bg->color.a > 0) {
        // Solid color fill
        render_solid_fill(bg->color, block, ctx);
    }
}
```

---

## Phase 6: Advanced Graphics

**Timeline**: 3 weeks  
**Priority**: Medium - needed for complex PDFs

### 6.1 Clipping Paths

**Approach**: Use ThorVG clip masks (same approach as CSS `clip-path`).

**Enhance**: `radiant/pdf/pdf_to_view.cpp`

```cpp
// Clipping path support using ThorVG masks
// Similar to CSS clip-path rendering

struct ClipProp {
    VectorPathProp* path;   // Reuse existing VectorPathProp
    bool evenOdd;           // true = even-odd rule, false = non-zero winding
};

// Add clip to ViewBlock (extends existing view properties)
struct ViewBlock {
    // ... existing fields ...
    ClipProp* clip;  // NEW: optional clip mask
};

// In render.cpp - apply clip before rendering children
void render_block_with_clip(ViewBlock* block, RenderContext* ctx) {
    if (block->clip) {
        // Save ThorVG canvas state
        tvg_canvas_push(ctx->canvas);
        
        // Create clip shape from VectorPathProp
        Tvg_Paint* clipShape = vector_path_to_thorvg(block->clip->path);
        
        // Apply as composite mask (ThorVG ClipPath)
        Tvg_Paint* composite = tvg_scene_new();
        tvg_scene_push(composite, clipShape);
        // Set composite method for clipping
        
        // Render children within clip
        render_block_children(block, ctx);
        
        // Restore state
        tvg_canvas_pop(ctx->canvas);
    } else {
        render_block_children(block, ctx);
    }
}
```

### 6.2 Transparency & Blending

**Approach**: Use ThorVG blend modes (same as CSS `mix-blend-mode`).

```cpp
// Blend modes (matches pdf.js AND CSS blend modes)
enum BlendMode {
    BLEND_NORMAL,       // source-over (CSS default)
    BLEND_MULTIPLY,     // CSS multiply
    BLEND_SCREEN,       // CSS screen
    BLEND_OVERLAY,      // CSS overlay
    BLEND_DARKEN,       // CSS darken
    BLEND_LIGHTEN,      // CSS lighten
    BLEND_COLOR_DODGE,  // CSS color-dodge
    BLEND_COLOR_BURN,   // CSS color-burn
    BLEND_HARD_LIGHT,   // CSS hard-light
    BLEND_SOFT_LIGHT,   // CSS soft-light
    BLEND_DIFFERENCE,   // CSS difference
    BLEND_EXCLUSION,    // CSS exclusion
    // ... more modes
};

// Map PDF blend mode to ThorVG composite method
Tvg_Composite_Method blend_to_thorvg(BlendMode mode) {
    switch (mode) {
        case BLEND_NORMAL:    return TVG_COMPOSITE_METHOD_NONE;
        case BLEND_MULTIPLY:  return TVG_COMPOSITE_METHOD_MULTIPLY;
        // ThorVG supports most CSS/PDF blend modes
        default: return TVG_COMPOSITE_METHOD_NONE;
    }
}

// Extended graphics state (used by both PDF and CSS)
struct GraphicsState {
    float fillAlpha;      // PDF: ca, CSS: opacity
    float strokeAlpha;    // PDF: CA
    BlendMode blendMode;  // PDF: BM, CSS: mix-blend-mode
};

// Apply transparency in rendering (same for PDF and CSS)
void apply_alpha_and_blend(Tvg_Paint* paint, float alpha, BlendMode blend) {
    tvg_paint_set_opacity(paint, (uint8_t)(alpha * 255));
    if (blend != BLEND_NORMAL) {
        tvg_paint_set_composite_method(paint, nullptr, blend_to_thorvg(blend));
    }
}
```

### 6.3 Form XObjects

**Approach**: Form XObjects become nested ViewBlocks.

```cpp
// Form XObject (reusable graphics)
// pdf.js: evaluator.js executeXObject()
class FormXObject {
public:
    Stream* stream;
    Resources* resources;
    float bbox[4];
    float matrix[6];
    TransparencyGroup* group;  // Optional transparency
    
    // Convert to ViewBlock (REUSE existing view infrastructure)
    ViewBlock* toViewBlock(Pool* pool) {
        ViewBlock* block = (ViewBlock*)pool_calloc(pool, sizeof(ViewBlock));
        block->view_type = RDT_VIEW_BLOCK;
        
        // Set dimensions from bbox
        block->x = bbox[0];
        block->y = bbox[1];
        block->width = bbox[2] - bbox[0];
        block->height = bbox[3] - bbox[1];
        
        // Store transform matrix
        block->transform = createTransformProp(pool, matrix);
        
        // Process content stream into child views
        processContentStream(stream, resources, block);
        
        return block;
    }
    
    // Execute form XObject (add to parent view tree)
    void execute(PDFStreamParser* parser, ViewBlock* parent) {
        // Save graphics state
        parser->state.save();
        
        // Apply matrix transform
        parser->state.concatenate(matrix);
        
        // Create ViewBlock for form content
        ViewBlock* formView = toViewBlock(parser->pool);
        
        // Add clip from bbox
        if (bbox) {
            formView->clip = createClipFromBbox(parser->pool, bbox);
        }
        
        // Add to parent
        append_child_view((View*)parent, (View*)formView);
        
        // Restore state
        parser->state.restore();
    }
};
```

### 6.4 Transform Support

```cpp
// Transform property (REUSE CSS transform infrastructure)
// CSS already supports transform: matrix()

struct TransformProp {
    float matrix[6];  // [a b c d e f] - same as CSS matrix()
};

// Apply transform during rendering (same as CSS transforms)
void apply_transform(Tvg_Paint* paint, TransformProp* transform) {
    if (transform) {
        Tvg_Matrix m = {
            transform->matrix[0], transform->matrix[2], transform->matrix[4],
            transform->matrix[1], transform->matrix[3], transform->matrix[5],
            0, 0, 1
        };
        tvg_paint_transform(paint, &m);
    }
}
```

---

## Phase 7: Text Layer & Selection

**Timeline**: 2 weeks  
**Priority**: Low - enhancement for interactivity

### 7.1 Text Content Extraction

```cpp
// Extract text with positions (for selection/search)
// pdf.js: src/core/evaluator.js getTextContent()
struct TextItem {
    String* str;           // Unicode text
    float transform[6];    // Text positioning matrix
    float width;           // Text width
    float height;          // Font size
    String* fontName;
    bool hasEOL;          // End of line
};

std::vector<TextItem> extractTextContent(Page* page);
```

### 7.2 Selection Layer

```cpp
// Invisible text layer for selection
// pdf.js: src/display/text_layer.js
class TextSelectionLayer {
public:
    // Build selection layer matching PDF text
    void build(const std::vector<TextItem>& items);
    
    // Get text in selection rectangle
    String* getTextInRect(float x, float y, float w, float h);
    
    // Search text
    std::vector<Rect> findText(const String* query);
};
```

---

## Phase 8: Annotations (Optional)

**Timeline**: 3 weeks  
**Priority**: Low - needed for interactive PDFs

### 8.1 Annotation Types

```cpp
// PDF Annotation support
// pdf.js: src/core/annotation.js, src/display/annotation_layer.js
class PDFAnnotation {
public:
    enum Type {
        ANNOT_TEXT,         // Note icon
        ANNOT_LINK,         // Hyperlink
        ANNOT_FREETEXT,     // Text box
        ANNOT_LINE,         // Line
        ANNOT_SQUARE,       // Rectangle
        ANNOT_CIRCLE,       // Ellipse
        ANNOT_POLYGON,      // Polygon
        ANNOT_POLYLINE,     // Polyline
        ANNOT_HIGHLIGHT,    // Text highlight
        ANNOT_UNDERLINE,    // Text underline
        ANNOT_STRIKEOUT,    // Text strikeout
        ANNOT_STAMP,        // Rubber stamp
        ANNOT_INK,          // Freehand drawing
        ANNOT_POPUP,        // Popup note
        ANNOT_FILEATTACH,   // File attachment
        ANNOT_WIDGET,       // Form field
    };
    
    Type type;
    float rect[4];
    float borderWidth;
    Color color;
    String* contents;
    
    // Create view for annotation
    View* toView(Pool* pool);
};
```

---

## Implementation Summary

### View Reuse Strategy

The key design principle is **maximum reuse of existing HTML/CSS view infrastructure**:

| PDF Feature | Reused View/Prop | Reused Renderer | New Code Needed |
|-------------|------------------|-----------------|-----------------|
| Text | `ViewText`, `FontProp`, `TextRect` | `render_text.cpp` (FreeType) | Font loading only |
| Rectangles | `ViewBlock`, `BoundaryProp` | `render_background.cpp`, `render_border.cpp` | None |
| Paths/Curves | `ViewBlock`, `VectorPathProp` | `render_pdf.cpp` (ThorVG) | Already exists |
| Images | `ViewImage` | `render_img.cpp` (ThorVG) | Decoder only |
| Fills | `BackgroundProp` | `render_background.cpp` | None |
| Strokes | `BorderProp` | `render_border.cpp` | None |
| Gradients | `GradientProp` | CSS gradient renderer | Extend if needed |
| Alpha | Existing alpha support | ThorVG opacity | None |
| Transforms | `TransformProp` | ThorVG matrix | Extend if needed |
| Clipping | New `ClipProp` | ThorVG composite | New prop |

### What's New vs. Reused

**New Code (PDF-specific)**:
- Stream decompression (`pdf_stream.cpp`)
- XRef/object resolution (`pdf_xref.cpp`) 
- Font embedding loader (`pdf_fonts.cpp`)
- CMap parser (`pdf_cmap.cpp`)
- Image decoders for JPX, JBIG2 (`pdf_image.cpp`)
- ColorSpace converters (`pdf_colorspace.cpp`)
- Pattern/shading processors (`pdf_pattern.cpp`)

**Reused Code (from HTML/CSS)**:
- All view types (`ViewBlock`, `ViewText`, `ViewImage`)
- All property types (`FontProp`, `BoundaryProp`, `BackgroundProp`)
- Text rendering (FreeType via `render_text.cpp`)
- Background rendering (ThorVG via `render_background.cpp`)
- Border rendering (ThorVG via `render_border.cpp`)
- Image rendering (ThorVG via `render_img.cpp`)
- Vector path rendering (ThorVG via existing `VectorPathProp`)
- Color handling (`Color` struct)
- Pool memory allocation

### File Structure

```
radiant/pdf/
├── pdf_to_view.cpp        # Main conversion (existing, enhance)
├── pdf_to_view.hpp        # Public API (existing, enhance)
├── operators.cpp          # Operator parser (existing, enhance)
├── operators.h            # Operator types (existing, enhance)
├── fonts.cpp             # Font mapping (existing)
├── pages.cpp             # Page handling (existing)
├── coords.cpp            # Coordinate transforms (existing)
│
├── pdf_stream.cpp        # NEW: Stream decompression
├── pdf_stream.hpp
├── pdf_xref.cpp          # NEW: Object resolution
├── pdf_xref.hpp
├── pdf_fonts.cpp         # NEW: Embedded font loading → FreeType
├── pdf_fonts.hpp
├── pdf_cmap.cpp          # NEW: CMap parsing
├── pdf_cmap.hpp
├── pdf_image.cpp         # NEW: Image decoding → ViewImage
├── pdf_image.hpp
├── pdf_colorspace.cpp    # NEW: Color space conversion
├── pdf_colorspace.hpp
├── pdf_pattern.cpp       # NEW: Patterns → ThorVG
├── pdf_pattern.hpp
└── pdf_annotation.cpp    # NEW: Annotations (optional)
    pdf_annotation.hpp

# Existing files that need EXTENSION (not replacement):
radiant/
├── view.hpp              # Add ClipProp, GradientProp if needed
├── render.cpp            # Add clip mask support
├── font.cpp              # Add embedded font loading
└── render_background.cpp # Verify gradient support
```

### Dependencies

| Feature | Library | License | Notes |
|---------|---------|---------|-------|
| zlib | zlib | zlib | Already have (FlateDecode) |
| JPEG | stb_image or libjpeg | Public domain / IJG | Already have stb |
| JPEG2000 | OpenJPEG | BSD-2 | New dependency |
| JBIG2 | jbig2dec | AGPL-3 | Or port pdf.js WASM |
| ICC | LittleCMS (lcms2) | MIT | For ICC profiles |
| Fonts | FreeType | FreeType License | Already have |

### Priority Matrix

| Phase | Features | Priority | LOC Est. | Reuses |
|-------|----------|----------|----------|--------|
| 1 | Streams, XRef | Critical | ~1500 | zlib |
| 2 | Fonts, CMap | High | ~2000 | FreeType (existing) |
| 3 | Images | High | ~1500 | ViewImage, stb (existing) |
| 4 | Color Spaces | Medium | ~1000 | Color struct (existing) |
| 5 | Patterns | Medium | ~1200 | ThorVG gradients (existing) |
| 6 | Advanced Graphics | Medium | ~1500 | ThorVG blend/clip |
| 7 | Text Layer | Low | ~800 | ViewText (existing) |
| 8 | Annotations | Low | ~2000 | ViewBlock (existing) |

**Total Estimated**: ~11,500 lines of new code (reduced from 13,000 due to reuse)

---

## Rendering Flow Comparison

### HTML/CSS Rendering Flow (Existing)
```
HTML Parser → DOM Tree → CSS Cascade → View Tree → Radiant Renderer
                                           ↓
                              ┌────────────┴────────────┐
                              ↓                         ↓
                         FreeType                   ThorVG
                        (text glyphs)         (shapes, images)
                              ↓                         ↓
                              └────────────┬────────────┘
                                           ↓
                                      GLFW Window
```

### PDF Rendering Flow (Proposed - Reuses Same Backend)
```
PDF Parser → Object Graph → Operator Eval → View Tree → Radiant Renderer
                                                ↓
                              ┌────────────┴────────────┐
                              ↓                         ↓
                         FreeType                   ThorVG
                        (text glyphs)         (shapes, images)
                              ↓                         ↓
                              └────────────┬────────────┘
                                           ↓
                                      GLFW Window
```

**Key Insight**: Both flows converge at the View Tree level and share the same rendering backend. This means:
- No new OpenGL code
- No duplicate rendering logic
- Consistent visual output
- Shared bug fixes and optimizations

---

## Testing Strategy

### Test Categories

1. **Unit Tests**: Individual components (stream decoder, CMap parser, etc.)
2. **Integration Tests**: Full PDF → View rendering pipeline
3. **Visual Regression**: Compare output to pdf.js/Acrobat renders
4. **Performance Tests**: Large document handling, memory usage

### Test PDFs

Use pdf.js test corpus (`pdf-js/test/pdfs/`):
- `tracemonkey.pdf` - Complex layout with text and images
- `alphatrans.pdf` - Transparency and blending
- `rotationNormal.pdf` - Page rotation
- `bug1020858.pdf` - CJK fonts with CMaps
- Various other edge cases

### Visual Comparison

```bash
# Compare Radiant output to reference
./lambda.exe render test.pdf -o output.png
compare output.png reference.png diff.png
```

---

## Conclusion

This proposal provides a comprehensive roadmap to achieve pdf.js-level PDF rendering in Radiant, with a strong emphasis on **reusing existing infrastructure**.

### Key Design Decisions

1. **Unified View Tree**: PDF content maps to the same view types as HTML/CSS (`ViewBlock`, `ViewText`, `ViewImage`), enabling shared rendering code.

2. **FreeType for All Text**: Both HTML and PDF text rendering use FreeType through the existing `render_text.cpp` pipeline. PDF fonts are loaded into FreeType faces.

3. **ThorVG for All Graphics**: All vector graphics (paths, curves, gradients, images) use ThorVG through existing render functions. No direct OpenGL calls.

4. **Property Reuse**: PDF styling maps to existing CSS-like properties (`BoundaryProp`, `BackgroundProp`, `FontProp`), minimizing new property types.

5. **Extend, Don't Replace**: New PDF features (clipping, gradients, transforms) extend existing view properties rather than creating parallel systems.

### Benefits of This Approach

| Benefit | Impact |
|---------|--------|
| **Reduced Code** | ~11,500 LOC vs. ~20,000+ if building separate renderer |
| **Shared Bug Fixes** | Fixes to text/graphics rendering benefit both HTML and PDF |
| **Consistent Output** | Same visual quality for HTML and PDF |
| **Easier Testing** | One rendering pipeline to test |
| **Future Features** | New ThorVG/FreeType features automatically benefit PDF |

### Success Factors

1. **Stream decompression** is the critical foundation - without it, most PDFs won't work
2. **Font embedding** determines text rendering quality (FreeType handles all font types)
3. **Image support** is needed for most real-world PDFs (reuse `ViewImage`)
4. **Color spaces** affect visual accuracy (converters produce standard RGB)
5. **Patterns/gradients** use ThorVG's native gradient support

The architecture follows pdf.js patterns for PDF processing while leveraging Radiant's existing HTML/CSS rendering infrastructure for output, achieving both correctness and maximum code reuse.
