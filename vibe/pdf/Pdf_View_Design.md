# Radiant PDF Viewing/Rendering Design

## Overview

Radiant supports PDF viewing and rendering by converting PDF pages into the same ViewTree structure used for HTML/CSS rendering. This design maximizes code reuse and provides a unified rendering pipeline for both HTML documents and PDF files.

## Design Principles

### 1. Reuse HTML/CSS View Tree Architecture

PDF rendering reuses the existing Radiant view tree infrastructure:

| Component | HTML/CSS | PDF | Shared Code |
|-----------|----------|-----|-------------|
| View Tree | ViewTree, ViewBlock, ViewText, ViewSpan | Same structures | `radiant/view.hpp` |
| Rendering | Canvas drawing, text rendering | Same rendering | `radiant/render.cpp` |
| Font handling | FontProp, FontBox | Same font system | `radiant/font_face.h` |
| Color handling | RGBA colors | Same color model | `radiant/view.hpp` |
| Output | SVG, PNG, PDF | Same output pipeline | `radiant/render_*.cpp` |

### 2. Coordinate System Alignment

PDF uses a bottom-left origin coordinate system, while CSS uses top-left. The PDF loader performs coordinate transformation:

```
PDF coordinates:        Screen coordinates:
  ▲ y                     ┌───────────► x
  │                       │
  │                       │
  └───────────► x         ▼ y

Transformation: screen_y = page_height - pdf_y
```

### 3. Direct View Tree Creation

Unlike HTML documents which require:
1. DOM tree construction
2. CSS cascade resolution  
3. Layout computation

PDF documents create the ViewTree directly:
1. Parse PDF structure → Extract page content
2. Interpret PDF operators → Create View nodes with absolute positions
3. No CSS layout needed (positions from PDF)

---

## Pipeline: PDF to View on Screen

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         PDF VIEWING PIPELINE                                 │
└─────────────────────────────────────────────────────────────────────────────┘

┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│  PDF File    │───►│ PDF Parser   │───►│ Content      │───►│ View Tree    │
│  (.pdf)      │    │ input-pdf.cpp│    │ Interpreter  │    │ Creation     │
└──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘
                           │                   │                   │
                           ▼                   ▼                   ▼
                    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
                    │ Lambda Item  │    │ PDF Operators│    │ ViewTree     │
                    │ Structure    │    │ (Tj, TJ, re) │    │ ViewBlock    │
                    │ (Map/Array)  │    │ Graphics ops │    │ ViewText     │
                    └──────────────┘    └──────────────┘    └──────────────┘
                                                                   │
                    ┌──────────────────────────────────────────────┘
                    ▼
┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│ DomDocument  │───►│ UI Context   │───►│ Render       │───►│ Screen/File  │
│ (view_tree)  │    │ Setup        │    │ Pipeline     │    │ Output       │
└──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘
```

### Stage 1: PDF File Loading

**File**: `radiant/cmd_layout.cpp` - `load_pdf_doc()` (line 1407)

```cpp
DomDocument* load_pdf_doc(Url* pdf_url, int viewport_width, int viewport_height, 
                          Pool* pool, float pixel_ratio);
```

- Reads PDF file as binary data
- Creates Input structure for Lambda parsing

### Stage 2: PDF Parsing

**File**: `lambda/input/input-pdf.cpp` - `parse_pdf()`

- Parses PDF header, trailer, cross-reference table
- Resolves indirect object references
- Decompresses content streams (FlateDecode, LZWDecode, etc.)
- Returns Lambda Item structure (Map) representing PDF document

**Output Structure**:
```
pdf_root: Map
├── pages: Array[Map]
│   └── page: Map
│       ├── MediaBox: [0, 0, 612, 792]
│       ├── Contents: Stream (or Array of Streams)
│       └── Resources: Map (fonts, color spaces, images)
├── page_count: Int
└── metadata: Map (optional)
```

### Stage 3: Content Stream Interpretation

**File**: `radiant/pdf/pdf_to_view.cpp` - `process_pdf_stream()` (line 826)

Interprets PDF content stream operators:

| Category | Operators | Purpose |
|----------|-----------|---------|
| **Text** | BT, ET, Tj, TJ, Tf, Tm, Td | Text objects, show text, font, position |
| **Graphics** | q, Q, cm, re, m, l, c | State save/restore, transform, paths |
| **Color** | g, G, rg, RG, k, K, cs, CS | Grayscale, RGB, CMYK colors |
| **Path** | f, F, S, s, B, b | Fill, stroke, close path |
| **Image** | Do | XObject (image/form) rendering |

**Operator Dispatch** (lines 1101-1400):
```cpp
switch (op->type) {
    case PDF_OP_Tj:  // Show text
        create_text_view(input, view_pool, parent, parser, op->operands.show_text.text);
        break;
    case PDF_OP_TJ:  // Show text with positioning
        create_text_array_views(input, view_pool, parent, parser, op->operands.text_array.array);
        break;
    case PDF_OP_re:  // Rectangle
        pdf_add_rect_path(...);
        break;
    // ... more operators
}
```

### Stage 4: View Tree Creation

**File**: `radiant/pdf/pdf_to_view.cpp`

#### Text Views (lines 1977-2068)

```cpp
static void create_text_view(Input* input, Pool* view_pool, ViewBlock* parent,
                            PDFStreamParser* parser, String* text) {
    // Calculate position from text matrix
    double x = parser->state.tm[4];  // x translation
    double y = parser->state.tm[5];  // y translation
    
    // Coordinate transformation (PDF → screen)
    double screen_y = parent->height - y;
    
    // Create ViewText
    ViewText* text_view = (ViewText*)pool_calloc(view_pool, sizeof(ViewText));
    text_view->view_type = RDT_VIEW_TEXT;
    text_view->text = text->chars;
    text_view->length = text->len;
    
    // Create TextRect for position
    TextRect* rect = (TextRect*)pool_calloc(view_pool, sizeof(TextRect));
    rect->x = (float)x;
    rect->y = (float)screen_y;
    rect->height = (float)effective_font_size;
    
    // Add to parent
    append_child_view((View*)parent, (View*)text_view);
}
```

#### Graphics Views

- Rectangles → `ViewBlock` with background color
- Paths → `ViewBlock` with border/fill
- Images → `ViewBlock` with image data

### Stage 5: DomDocument Creation

**File**: `radiant/cmd_layout.cpp` (lines 1475-1505)

```cpp
DomDocument* dom_doc = dom_document_create(input);
dom_doc->root = nullptr;           // No DomElement tree for PDF
dom_doc->html_root = nullptr;      // No HTML tree (skips layout_html_doc)
dom_doc->view_tree = view_tree;    // Pre-created ViewTree from PDF
```

Key: `html_root = nullptr` signals that layout computation should be skipped.

### Stage 6: Layout Skip for PDF

**File**: `radiant/cmd_layout.cpp` (lines 3215-3229)

```cpp
if (doc->view_tree && doc->view_tree->root) {
    // Document already has view tree (PDF, SVG, image) - skip CSS layout
    log_info("[Layout] Document already has view_tree, skipping CSS layout");
} else {
    // HTML document - perform CSS layout
    layout_html_doc(&ui_context, doc, false);
}
```

### Stage 7: Rendering

Uses the shared rendering pipeline:

**File**: `radiant/render.cpp`

- ViewTree traversal
- Text rendering with FreeType
- Shape rendering with ThorVG
- Output to canvas (screen) or file (SVG/PNG/PDF)

---

## File Organization

```
radiant/
├── cmd_layout.cpp          # load_pdf_doc(), PDF extension detection
├── pdf/
│   ├── pdf_to_view.cpp     # PDF → ViewTree conversion
│   ├── pdf_to_view.hpp     # Public API
│   ├── operators.cpp       # PDF operator parsing
│   ├── operators.hpp       # Operator definitions
│   └── cmd_view_pdf.cpp    # PDF viewing command
├── view.hpp                # ViewTree, ViewBlock, ViewText (shared)
├── render.cpp              # Rendering pipeline (shared)
└── view_pool.cpp           # View tree output (JSON)

lambda/input/
├── input-pdf.cpp           # PDF binary parsing
└── input.cpp               # Input dispatcher
```

---

## Test Framework

### Reference: pdf.js

The test framework uses [pdf.js](https://mozilla.github.io/pdf.js/) as the reference implementation for comparison.

### Test Directory Structure

```
test/pdf/
├── package.json              # npm dependencies (pdfjs-dist, glob)
├── export_pdfjs_oplist.js    # Export pdf.js operator list as JSON
├── test_radiant_pdf.js       # Test runner comparing Radiant vs pdf.js
├── data/
│   └── basic/                # Test PDF files
│       ├── simple_test.pdf
│       ├── sample.pdf
│       └── advanced_test.pdf
├── reference/                # pdf.js reference outputs (JSON)
│   ├── simple_test.json
│   └── sample.json
└── output/                   # Radiant outputs for comparison
```

### pdf.js Reference Export

**File**: `test/pdf/export_pdfjs_oplist.js`

Exports:
- Operator list (`getOperatorList()`)
- Text items with positions (`getTextContent()`)
- Page dimensions and metadata

```javascript
const page = await doc.getPage(pageNum);
const opList = await page.getOperatorList();
const textContent = await page.getTextContent();

result.pages.push({
    pageNum,
    operations: opList.fnArray.map((fn, idx) => ({
        op: OPS_NAMES[fn],
        args: opList.argsArray[idx]
    })),
    textItems: textContent.items.map(item => ({
        str: item.str,
        transform: item.transform,
        fontName: item.fontName
    }))
});
```

### Test Runner

**File**: `test/pdf/test_radiant_pdf.js`

Comparison approach:
1. Run `lambda layout *.pdf --view-output` to generate Radiant view tree
2. Load pdf.js reference JSON
3. Compare text items:
   - Text content matching
   - Position tolerance (within threshold)
   - Font size comparison

```javascript
class RadiantPdfTester {
    async runTest(pdfName) {
        // Generate Radiant output
        await this.runRadiantLayout(pdfPath, outputPath);
        
        // Load both outputs
        const radiantData = this.loadRadiantOutput(outputPath);
        const pdfjsData = this.loadPdfjsReference(referencePath);
        
        // Compare text items
        const results = this.compareTextItems(
            this.extractTextItems(radiantData),
            pdfjsData.pages[0].textItems
        );
        
        return results;
    }
}
```

### Running Tests

```bash
# Setup
cd test/pdf && npm install

# Export pdf.js references
make test-pdf-export

# Run comparison tests
make test-pdf
# or
cd test/pdf && npm test
```

### Current Test Status

| PDF | pdf.js Items | Radiant Match | Issue |
|-----|--------------|---------------|-------|
| simple_test | 3 | 1/3 (33%) | Text combination |
| sample | 48 | ~0/48 (0%) | Text combination, encoding |
| advanced_test | 26 | ~0/26 (0%) | Text combination |

### Known Issues

1. **Text Combination**: Consecutive ViewText nodes are merged during JSON output (view_pool.cpp lines 872-960)
2. **Font Encoding**: Some characters not decoded (fi ligature → `�`)
3. **Word Boundaries**: No space inserted between combined text nodes

---

## Key APIs

### Loading PDF

```cpp
// Full document load
DomDocument* load_pdf_doc(Url* pdf_url, int viewport_width, 
                          int viewport_height, Pool* pool, float pixel_ratio);

// Page-only conversion
ViewTree* pdf_page_to_view_tree(Input* input, Item pdf_root, 
                                int page_index, float pixel_ratio);

// Page count
int pdf_get_page_count(Item pdf_root);
```

### Command Line

```bash
# Layout (generates view tree JSON)
./lambda.exe layout document.pdf --view-output output.json

# View (opens in window)
./lambda.exe view document.pdf

# Render (to file)
./lambda.exe render document.pdf -o output.svg
./lambda.exe render document.pdf -o output.png
./lambda.exe render document.pdf -o output.pdf  # PDF-to-PDF, re-rendered
```

---

## Comparison with pdf.js

| Feature | Radiant | pdf.js |
|---------|---------|--------|
| Text extraction | ✅ Basic | ✅ Full (ToUnicode, CMap) |
| Text positioning | ✅ | ✅ |
| Graphics (shapes) | ✅ Basic | ✅ Full |
| Images | ⚠️ Partial | ✅ Full |
| Font embedding | ❌ | ✅ |
| Annotations | ❌ | ✅ |
| Forms | ❌ | ✅ |
| Encryption | ❌ | ✅ |

---

## Future Improvements

1. **Fix Text Combination**: Add flag to preserve individual text nodes for comparison
2. **Font Encoding**: Implement ToUnicode CMap parsing for proper character decoding
3. **Ligature Handling**: Decode fi, fl, ff ligatures to individual characters
4. **Image Support**: Full image extraction and rendering
5. **Form XObjects**: Recursive form rendering
