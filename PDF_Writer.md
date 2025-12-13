# Lambda PDF Writer Library

## Overview

This document outlines the implementation plan for a custom PDF writer library (`hpdf.c/h`) to replace the external libharu (HPDF) dependency. The library will be located in the `./lib` directory and maintain API compatibility with libharu for seamless migration.

## Goals

1. **API Compatibility**: Match libharu's C API for functions currently used by radiant
2. **Minimal Dependencies**: Use only standard C libraries and existing Lambda lib utilities
3. **Lightweight**: Focus on PDF generation features actually needed, not full PDF spec
4. **Cross-Platform**: Support macOS, Linux, and Windows (MSYS2/MinGW)

## Current libharu Usage Analysis

### Types Used in radiant/render_pdf.cpp

| Type | Description |
|------|-------------|
| `HPDF_Doc` | PDF document handle (opaque pointer) |
| `HPDF_Page` | Page handle (opaque pointer) |
| `HPDF_Font` | Font handle (opaque pointer) |
| `HPDF_STATUS` | Status/error code (unsigned long) |

### Functions Used (Priority 1 - Required)

| Function | Purpose |
|----------|---------|
| `HPDF_New(error_handler, user_data)` | Create new PDF document |
| `HPDF_Free(doc)` | Free PDF document and all resources |
| `HPDF_AddPage(doc)` | Add a new page to document |
| `HPDF_SaveToFile(doc, filename)` | Save PDF to file |
| `HPDF_GetFont(doc, font_name, encoding)` | Get built-in font handle |
| `HPDF_SetCompressionMode(doc, mode)` | Set compression (can be no-op initially) |
| `HPDF_SetInfoAttr(doc, type, value)` | Set document metadata |
| `HPDF_Page_SetWidth(page, width)` | Set page width |
| `HPDF_Page_SetHeight(page, height)` | Set page height |
| `HPDF_Page_SetFontAndSize(page, font, size)` | Set current font and size |
| `HPDF_Page_SetRGBFill(page, r, g, b)` | Set fill color (RGB 0.0-1.0) |
| `HPDF_Page_SetRGBStroke(page, r, g, b)` | Set stroke color (RGB 0.0-1.0) |
| `HPDF_Page_Rectangle(page, x, y, w, h)` | Add rectangle to path |
| `HPDF_Page_Fill(page)` | Fill current path |
| `HPDF_Page_Stroke(page)` | Stroke current path |
| `HPDF_Page_BeginText(page)` | Begin text object |
| `HPDF_Page_EndText(page)` | End text object |
| `HPDF_Page_TextOut(page, x, y, text)` | Draw text at position |

### Constants Used

| Constant | Value | Description |
|----------|-------|-------------|
| `HPDF_OK` | 0 | Success status |
| `HPDF_COMP_ALL` | 0x0F | Enable all compression |
| `HPDF_INFO_CREATOR` | 0 | Creator metadata field |
| `HPDF_INFO_PRODUCER` | 1 | Producer metadata field |

## PDF File Format Basics

A PDF file consists of:
1. **Header**: `%PDF-1.4` followed by binary marker
2. **Body**: Objects (dictionaries, streams, etc.)
3. **Cross-reference table**: Object locations
4. **Trailer**: Document root reference and xref offset

### Minimal Object Types Needed

1. **Catalog** (root object) - document structure
2. **Pages** (page tree) - page collection
3. **Page** - individual page with content stream
4. **Font** - font resource (Type1 base14 fonts)
5. **Content Stream** - page drawing operators
6. **Info** - document metadata

### PDF Operators for Graphics

| Operator | Syntax | Description |
|----------|--------|-------------|
| `q` | `q` | Save graphics state |
| `Q` | `Q` | Restore graphics state |
| `rg` | `r g b rg` | Set fill color (RGB) |
| `RG` | `r g b RG` | Set stroke color (RGB) |
| `re` | `x y w h re` | Rectangle path |
| `f` | `f` | Fill path |
| `S` | `S` | Stroke path |
| `BT` | `BT` | Begin text |
| `ET` | `ET` | End text |
| `Tf` | `/FontName size Tf` | Set font and size |
| `Td` | `x y Td` | Move text position |
| `Tj` | `(text) Tj` | Show text |

## Architecture Design

### Data Structures

```c
// Main document structure
typedef struct HPDF_Doc_Rec {
    Pool* pool;                 // memory pool (owns the arena)
    Arena* arena;               // arena allocator for all allocations
    
    // Objects
    int next_obj_id;            // next object ID to assign
    ArrayList* objects;         // list of all objects
    
    // Document structure
    int catalog_id;             // catalog object ID
    int pages_id;               // pages tree object ID
    int info_id;                // info dictionary object ID
    
    // Pages
    ArrayList* pages;           // list of HPDF_Page_Rec*
    
    // Fonts
    ArrayList* fonts;           // list of HPDF_Font_Rec*
    
    // Metadata
    char* creator;
    char* producer;
    char* title;
    char* author;
    
    // Error handling
    HPDF_ErrorHandler error_fn;
    void* error_user_data;
    HPDF_STATUS last_error;
    
    // Options
    unsigned int compression_mode;
} HPDF_Doc_Rec;

// Page structure
typedef struct HPDF_Page_Rec {
    HPDF_Doc_Rec* doc;          // parent document
    int obj_id;                 // object ID for this page
    int contents_id;            // content stream object ID
    int resources_id;           // resources object ID
    
    float width;
    float height;
    
    // Content stream
    StrBuf* content;            // PDF operators
    
    // Graphics state
    float fill_r, fill_g, fill_b;
    float stroke_r, stroke_g, stroke_b;
    float line_width;
    
    // Text state
    HPDF_Font_Rec* current_font;
    float font_size;
    bool in_text_object;
    
    // Used fonts on this page
    ArrayList* used_fonts;
} HPDF_Page_Rec;

// Font structure (for base14 fonts)
typedef struct HPDF_Font_Rec {
    HPDF_Doc_Rec* doc;
    int obj_id;
    const char* name;           // PDF font name (e.g., "Helvetica")
    const char* resource_name;  // Resource name (e.g., "F1")
} HPDF_Font_Rec;
```

### Implementation Phases

#### Phase 1: Core Infrastructure (Required)
- [ ] Document creation and destruction
- [ ] Object ID management
- [ ] Basic PDF structure (header, catalog, pages)
- [ ] Page creation with dimensions
- [ ] Save to file (uncompressed)

#### Phase 2: Graphics (Required)
- [ ] Graphics state management
- [ ] RGB fill/stroke colors
- [ ] Rectangle path
- [ ] Fill and stroke operations

#### Phase 3: Text (Required)
- [ ] Base14 font support (Helvetica, Times, Courier)
- [ ] Text objects (BT/ET)
- [ ] Text positioning (Td)
- [ ] Text output (Tj)
- [ ] Font size setting

#### Phase 4: Metadata & Cleanup (Required)
- [ ] Info dictionary (Creator, Producer)
- [ ] Cross-reference table generation
- [ ] Trailer generation

#### Phase 5: Optional Enhancements
- [ ] Compression (deflate/zlib) - optional, can start uncompressed
- [ ] Additional fonts
- [ ] Additional graphics operators (lines, curves)
- [ ] Images (JPEG, PNG)
- [ ] Links and annotations

## File Organization

```
lib/
├── pdf_writer.h    # Public API header (libharu compatible)
├── pdf_writer.c    # Implementation

test/
├── test_pdf_writer_gtest.cpp  # Unit tests
```

## Dependencies

The implementation will use existing Lambda lib utilities:
- `strbuf.h/c` - String buffer for content generation
- `arena.h/c` - Arena allocator for fast sequential allocation with bulk deallocation
- `arraylist.h/c` - Dynamic arrays for object/page lists

## API Reference

### Document Functions

```c
// Create new PDF document
// error_handler: callback for errors (can be NULL)
// user_data: passed to error handler
HPDF_Doc HPDF_New(HPDF_ErrorHandler error_handler, void* user_data);

// Free document and all resources
void HPDF_Free(HPDF_Doc doc);

// Save document to file
HPDF_STATUS HPDF_SaveToFile(HPDF_Doc doc, const char* filename);

// Set compression mode (no-op in initial implementation)
HPDF_STATUS HPDF_SetCompressionMode(HPDF_Doc doc, unsigned int mode);

// Set document info attribute
HPDF_STATUS HPDF_SetInfoAttr(HPDF_Doc doc, HPDF_InfoType type, const char* value);
```

### Page Functions

```c
// Add new page to document
HPDF_Page HPDF_AddPage(HPDF_Doc doc);

// Set page dimensions
HPDF_STATUS HPDF_Page_SetWidth(HPDF_Page page, float width);
HPDF_STATUS HPDF_Page_SetHeight(HPDF_Page page, float height);
```

### Font Functions

```c
// Get font by name (base14 fonts only initially)
HPDF_Font HPDF_GetFont(HPDF_Doc doc, const char* font_name, const char* encoding);

// Set page font and size
HPDF_STATUS HPDF_Page_SetFontAndSize(HPDF_Page page, HPDF_Font font, float size);
```

### Graphics Functions

```c
// Colors (RGB values 0.0 to 1.0)
HPDF_STATUS HPDF_Page_SetRGBFill(HPDF_Page page, float r, float g, float b);
HPDF_STATUS HPDF_Page_SetRGBStroke(HPDF_Page page, float r, float g, float b);

// Path construction
HPDF_STATUS HPDF_Page_Rectangle(HPDF_Page page, float x, float y, float width, float height);

// Path painting
HPDF_STATUS HPDF_Page_Fill(HPDF_Page page);
HPDF_STATUS HPDF_Page_Stroke(HPDF_Page page);
```

### Text Functions

```c
// Text object control
HPDF_STATUS HPDF_Page_BeginText(HPDF_Page page);
HPDF_STATUS HPDF_Page_EndText(HPDF_Page page);

// Text output
HPDF_STATUS HPDF_Page_TextOut(HPDF_Page page, float x, float y, const char* text);
```

## Testing Strategy

1. **Unit Tests**: Create `test_hpdf_gtest.cpp` with tests for each function
2. **Integration Test**: Generate a test PDF and verify with PDF readers
3. **Comparison Test**: Ensure radiant produces identical output with new library
4. **Visual Validation**: Open generated PDFs in multiple viewers

## Migration Steps

1. Implement `lib/pdf_writer.h` and `lib/pdf_writer.c`
2. Update `radiant/render_pdf.cpp` to include `"../lib/pdf_writer.h"` instead of `<hpdf.h>`
3. Update build system to compile `lib/pdf_writer.c` and remove libharu linking
4. Run tests to verify output matches
5. Remove libharu from dependencies

## Timeline Estimate

- Phase 1-4 (Core functionality): 2-3 days
- Phase 5 (Enhancements): As needed
- Testing and integration: 1 day

## References

- [PDF Reference 1.7](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf)
- [libharu documentation](http://libharu.org/wiki/Documentation)
- [PDF Operators Quick Reference](https://www.adobe.com/content/dam/acom/en/devnet/pdf/pdf_reference_archive/PDFReference.pdf)
