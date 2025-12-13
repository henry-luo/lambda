/**
 * Lambda PDF Writer Library
 * 
 * A lightweight PDF generation library compatible with libharu's C API.
 * Implements the subset of functions used by radiant for HTML-to-PDF rendering.
 * 
 * Copyright (c) 2024 Lambda Script Project
 * License: Same as Lambda Script project
 */

#include "pdf_writer.h"
#include "strbuf.h"
#include "arraylist.h"
#include "mempool.h"
#include "arena.h"
#include "log.h"
#include <string.h>
#include <math.h>

/*---------------------------------------------------------------------------*/
/*  Internal Structures                                                      */
/*---------------------------------------------------------------------------*/

// PDF object types for internal tracking
typedef enum {
    PDF_OBJ_CATALOG,
    PDF_OBJ_PAGES,
    PDF_OBJ_PAGE,
    PDF_OBJ_FONT,
    PDF_OBJ_CONTENT,
    PDF_OBJ_RESOURCES,
    PDF_OBJ_INFO
} PdfObjType;

// Base14 fonts mapping
typedef struct {
    const char* name;           // font name as passed by user
    const char* pdf_name;       // actual PDF font name
} Base14Font;

static const Base14Font base14_fonts[] = {
    { "Helvetica",           "Helvetica" },
    { "Helvetica-Bold",      "Helvetica-Bold" },
    { "Helvetica-Oblique",   "Helvetica-Oblique" },
    { "Helvetica-BoldOblique", "Helvetica-BoldOblique" },
    { "Times-Roman",         "Times-Roman" },
    { "Times-Bold",          "Times-Bold" },
    { "Times-Italic",        "Times-Italic" },
    { "Times-BoldItalic",    "Times-BoldItalic" },
    { "Courier",             "Courier" },
    { "Courier-Bold",        "Courier-Bold" },
    { "Courier-Oblique",     "Courier-Oblique" },
    { "Courier-BoldOblique", "Courier-BoldOblique" },
    { "Symbol",              "Symbol" },
    { "ZapfDingbats",        "ZapfDingbats" },
    { NULL, NULL }
};

// PDF object info (for xref table)
typedef struct {
    int id;
    long offset;    // byte offset in file
    PdfObjType type;
} PdfObject;

// Font structure
struct HPDF_Font_Rec {
    struct HPDF_Doc_Rec* doc;
    int obj_id;
    const char* name;           // pdf font name (e.g., "Helvetica")
    char resource_name[16];     // resource name (e.g., "F1")
    int resource_index;         // font index in document
};

// Page structure
struct HPDF_Page_Rec {
    struct HPDF_Doc_Rec* doc;
    int obj_id;
    int contents_id;
    int resources_id;
    
    float width;
    float height;
    
    // Content stream buffer
    StrBuf* content;
    
    // Graphics state
    float fill_r, fill_g, fill_b;
    float stroke_r, stroke_g, stroke_b;
    float line_width;
    
    // Text state
    struct HPDF_Font_Rec* current_font;
    float font_size;
    bool in_text_object;
    
    // Fonts used on this page
    ArrayList* used_fonts;      // list of HPDF_Font_Rec*
};

// Document structure
struct HPDF_Doc_Rec {
    // Memory management
    Pool* pool;                 // memory pool (owns the arena)
    Arena* arena;               // arena allocator for all allocations
    
    // Object management
    int next_obj_id;
    ArrayList* objects;         // list of PdfObject
    
    // Document structure
    int catalog_id;
    int pages_id;
    int info_id;
    
    // Pages
    ArrayList* pages;           // list of HPDF_Page_Rec*
    
    // Fonts
    ArrayList* fonts;           // list of HPDF_Font_Rec*
    int next_font_index;
    
    // Metadata
    char* creator;
    char* producer;
    char* title;
    char* author;
    char* subject;
    char* keywords;
    
    // Error handling
    HPDF_ErrorHandler error_fn;
    void* error_user_data;
    HPDF_STATUS last_error;
    
    // Options
    unsigned int compression_mode;
};

/*---------------------------------------------------------------------------*/
/*  Internal Helper Functions                                                */
/*---------------------------------------------------------------------------*/

// allocate a new object id
static int alloc_obj_id(HPDF_Doc doc) {
    return doc->next_obj_id++;
}

// record object offset for xref
static void record_obj_offset(HPDF_Doc doc, int obj_id, long offset, PdfObjType type) {
    PdfObject* obj = (PdfObject*)arena_alloc(doc->arena, sizeof(PdfObject));
    if (!obj) return;
    
    obj->id = obj_id;
    obj->offset = offset;
    obj->type = type;
    arraylist_append(doc->objects, obj);
}

// escape text for pdf string
static void pdf_escape_text(StrBuf* buf, const char* text) {
    strbuf_append_char(buf, '(');
    for (const char* p = text; *p; p++) {
        switch (*p) {
            case '(':
            case ')':
            case '\\':
                strbuf_append_char(buf, '\\');
                strbuf_append_char(buf, *p);
                break;
            default:
                if ((unsigned char)*p < 32 || (unsigned char)*p > 126) {
                    // escape as octal
                    char octal[8];
                    snprintf(octal, sizeof(octal), "\\%03o", (unsigned char)*p);
                    strbuf_append_str(buf, octal);
                } else {
                    strbuf_append_char(buf, *p);
                }
                break;
        }
    }
    strbuf_append_char(buf, ')');
}

// format float for pdf (avoid unnecessary precision)
static void pdf_format_float(StrBuf* buf, float value) {
    // check if value is essentially an integer
    if (fabsf(value - roundf(value)) < 0.001f) {
        strbuf_append_int(buf, (int)roundf(value));
    } else {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "%.3f", value);
        // remove trailing zeros
        size_t len = strlen(tmp);
        while (len > 0 && tmp[len-1] == '0') len--;
        if (len > 0 && tmp[len-1] == '.') len--;
        tmp[len] = '\0';
        strbuf_append_str(buf, tmp);
    }
}

// check if font is already in used_fonts list
static bool page_has_font(HPDF_Page page, HPDF_Font font) {
    for (int i = 0; i < page->used_fonts->length; i++) {
        if (page->used_fonts->data[i] == font) {
            return true;
        }
    }
    return false;
}

// find or create base14 font
static const char* find_base14_font(const char* name) {
    for (int i = 0; base14_fonts[i].name != NULL; i++) {
        if (strcmp(base14_fonts[i].name, name) == 0) {
            return base14_fonts[i].pdf_name;
        }
    }
    return NULL;
}

/*---------------------------------------------------------------------------*/
/*  Document Functions                                                       */
/*---------------------------------------------------------------------------*/

HPDF_Doc HPDF_New(HPDF_ErrorHandler error_fn, void* user_data) {
    // create pool and arena first
    Pool* pool = pool_create();
    if (!pool) return NULL;
    
    Arena* arena = arena_create_default(pool);
    if (!arena) {
        pool_destroy(pool);
        return NULL;
    }
    
    HPDF_Doc doc = (HPDF_Doc)arena_calloc(arena, sizeof(struct HPDF_Doc_Rec));
    if (!doc) {
        arena_destroy(arena);
        pool_destroy(pool);
        return NULL;
    }
    
    doc->pool = pool;
    doc->arena = arena;
    doc->error_fn = error_fn;
    doc->error_user_data = user_data;
    doc->next_obj_id = 1;  // pdf objects start at 1
    doc->next_font_index = 1;
    
    doc->objects = arraylist_new(32);
    doc->pages = arraylist_new(8);
    doc->fonts = arraylist_new(8);
    
    if (!doc->objects || !doc->pages || !doc->fonts) {
        // arraylist uses malloc, so we need to free them
        if (doc->objects) arraylist_free(doc->objects);
        if (doc->pages) arraylist_free(doc->pages);
        if (doc->fonts) arraylist_free(doc->fonts);
        arena_destroy(arena);
        pool_destroy(pool);
        return NULL;
    }
    
    // pre-allocate object ids for document structure
    doc->catalog_id = alloc_obj_id(doc);
    doc->pages_id = alloc_obj_id(doc);
    doc->info_id = alloc_obj_id(doc);
    
    log_debug("hpdf: created document, catalog=%d pages=%d info=%d",
              doc->catalog_id, doc->pages_id, doc->info_id);
    
    return doc;
}

void HPDF_Free(HPDF_Doc doc) {
    if (!doc) return;
    
    // free strbuf content buffers (they use malloc internally)
    for (int i = 0; i < doc->pages->length; i++) {
        HPDF_Page page = (HPDF_Page)doc->pages->data[i];
        if (page && page->content) {
            strbuf_free(page->content);
        }
        // used_fonts arraylist needs to be freed
        if (page && page->used_fonts) {
            arraylist_free(page->used_fonts);
        }
    }
    
    // free arraylists (they use malloc internally)
    arraylist_free(doc->pages);
    arraylist_free(doc->fonts);
    arraylist_free(doc->objects);
    
    // arena and pool cleanup - this frees all arena-allocated memory
    Arena* arena = doc->arena;
    Pool* pool = doc->pool;
    
    arena_destroy(arena);
    pool_destroy(pool);
}

HPDF_STATUS HPDF_SetCompressionMode(HPDF_Doc doc, unsigned int mode) {
    if (!doc) return HPDF_ERROR_INVALID_PARAM;
    
    doc->compression_mode = mode;
    // note: compression not implemented yet, stored for future use
    
    return HPDF_OK;
}

HPDF_STATUS HPDF_SetInfoAttr(HPDF_Doc doc, HPDF_InfoType type, const char* value) {
    if (!doc || !value) return HPDF_ERROR_INVALID_PARAM;
    
    char** target = NULL;
    
    switch (type) {
        case HPDF_INFO_CREATOR:   target = &doc->creator; break;
        case HPDF_INFO_PRODUCER:  target = &doc->producer; break;
        case HPDF_INFO_TITLE:     target = &doc->title; break;
        case HPDF_INFO_AUTHOR:    target = &doc->author; break;
        case HPDF_INFO_SUBJECT:   target = &doc->subject; break;
        case HPDF_INFO_KEYWORDS:  target = &doc->keywords; break;
        default:
            return HPDF_OK;  // ignore unknown types
    }
    
    // use arena_strdup - previous value is in arena and will be freed with doc
    *target = arena_strdup(doc->arena, value);
    
    return HPDF_OK;
}

/*---------------------------------------------------------------------------*/
/*  Page Functions                                                           */
/*---------------------------------------------------------------------------*/

HPDF_Page HPDF_AddPage(HPDF_Doc doc) {
    if (!doc) return NULL;
    
    HPDF_Page page = (HPDF_Page)arena_calloc(doc->arena, sizeof(struct HPDF_Page_Rec));
    if (!page) return NULL;
    
    page->doc = doc;
    page->obj_id = alloc_obj_id(doc);
    page->contents_id = alloc_obj_id(doc);
    
    // default page size (letter)
    page->width = HPDF_PAGE_SIZE_LETTER_WIDTH;
    page->height = HPDF_PAGE_SIZE_LETTER_HEIGHT;
    
    // initialize content stream (uses malloc internally)
    page->content = strbuf_new_cap(4096);
    if (!page->content) {
        return NULL;
    }
    
    // initialize fonts list (uses malloc internally)
    page->used_fonts = arraylist_new(4);
    if (!page->used_fonts) {
        strbuf_free(page->content);
        return NULL;
    }
    
    // default graphics state
    page->fill_r = page->fill_g = page->fill_b = 0.0f;    // black fill
    page->stroke_r = page->stroke_g = page->stroke_b = 0.0f; // black stroke
    page->line_width = 1.0f;
    page->font_size = 12.0f;
    page->in_text_object = false;
    
    arraylist_append(doc->pages, page);
    
    log_debug("hpdf: added page %d, contents=%d", page->obj_id, page->contents_id);
    
    return page;
}

HPDF_STATUS HPDF_Page_SetWidth(HPDF_Page page, float width) {
    if (!page) return HPDF_ERROR_INVALID_PARAM;
    page->width = width;
    return HPDF_OK;
}

HPDF_STATUS HPDF_Page_SetHeight(HPDF_Page page, float height) {
    if (!page) return HPDF_ERROR_INVALID_PARAM;
    page->height = height;
    return HPDF_OK;
}

/*---------------------------------------------------------------------------*/
/*  Font Functions                                                           */
/*---------------------------------------------------------------------------*/

HPDF_Font HPDF_GetFont(HPDF_Doc doc, const char* font_name, const char* encoding) {
    if (!doc || !font_name) return NULL;
    
    // check if we already have this font
    for (int i = 0; i < doc->fonts->length; i++) {
        HPDF_Font font = (HPDF_Font)doc->fonts->data[i];
        if (strcmp(font->name, font_name) == 0) {
            return font;
        }
    }
    
    // find base14 font
    const char* pdf_name = find_base14_font(font_name);
    if (!pdf_name) {
        // try using the name directly
        pdf_name = font_name;
    }
    
    // create new font using arena
    HPDF_Font font = (HPDF_Font)arena_calloc(doc->arena, sizeof(struct HPDF_Font_Rec));
    if (!font) return NULL;
    
    font->doc = doc;
    font->obj_id = alloc_obj_id(doc);
    font->name = pdf_name;
    font->resource_index = doc->next_font_index++;
    snprintf(font->resource_name, sizeof(font->resource_name), "F%d", font->resource_index);
    
    arraylist_append(doc->fonts, font);
    
    log_debug("hpdf: created font '%s' as %s (obj %d)", font_name, font->resource_name, font->obj_id);
    
    return font;
}

HPDF_STATUS HPDF_Page_SetFontAndSize(HPDF_Page page, HPDF_Font font, float size) {
    if (!page || !font) return HPDF_ERROR_INVALID_PARAM;
    
    page->current_font = font;
    page->font_size = size;
    
    // add font to page's used fonts if not already there
    if (!page_has_font(page, font)) {
        arraylist_append(page->used_fonts, font);
    }
    
    // if we're in a text object, emit font change operator
    if (page->in_text_object) {
        strbuf_append_str(page->content, "/");
        strbuf_append_str(page->content, font->resource_name);
        strbuf_append_str(page->content, " ");
        pdf_format_float(page->content, size);
        strbuf_append_str(page->content, " Tf\n");
    }
    
    return HPDF_OK;
}

/*---------------------------------------------------------------------------*/
/*  Graphics State Functions                                                 */
/*---------------------------------------------------------------------------*/

HPDF_STATUS HPDF_Page_SetRGBFill(HPDF_Page page, float r, float g, float b) {
    if (!page) return HPDF_ERROR_INVALID_PARAM;
    
    page->fill_r = r;
    page->fill_g = g;
    page->fill_b = b;
    
    // emit color operator
    pdf_format_float(page->content, r);
    strbuf_append_char(page->content, ' ');
    pdf_format_float(page->content, g);
    strbuf_append_char(page->content, ' ');
    pdf_format_float(page->content, b);
    strbuf_append_str(page->content, " rg\n");
    
    return HPDF_OK;
}

HPDF_STATUS HPDF_Page_SetRGBStroke(HPDF_Page page, float r, float g, float b) {
    if (!page) return HPDF_ERROR_INVALID_PARAM;
    
    page->stroke_r = r;
    page->stroke_g = g;
    page->stroke_b = b;
    
    // emit color operator
    pdf_format_float(page->content, r);
    strbuf_append_char(page->content, ' ');
    pdf_format_float(page->content, g);
    strbuf_append_char(page->content, ' ');
    pdf_format_float(page->content, b);
    strbuf_append_str(page->content, " RG\n");
    
    return HPDF_OK;
}

HPDF_STATUS HPDF_Page_SetLineWidth(HPDF_Page page, float width) {
    if (!page) return HPDF_ERROR_INVALID_PARAM;
    
    page->line_width = width;
    
    pdf_format_float(page->content, width);
    strbuf_append_str(page->content, " w\n");
    
    return HPDF_OK;
}

/*---------------------------------------------------------------------------*/
/*  Path Construction Functions                                              */
/*---------------------------------------------------------------------------*/

HPDF_STATUS HPDF_Page_Rectangle(HPDF_Page page, float x, float y, float width, float height) {
    if (!page) return HPDF_ERROR_INVALID_PARAM;
    
    pdf_format_float(page->content, x);
    strbuf_append_char(page->content, ' ');
    pdf_format_float(page->content, y);
    strbuf_append_char(page->content, ' ');
    pdf_format_float(page->content, width);
    strbuf_append_char(page->content, ' ');
    pdf_format_float(page->content, height);
    strbuf_append_str(page->content, " re\n");
    
    return HPDF_OK;
}

HPDF_STATUS HPDF_Page_MoveTo(HPDF_Page page, float x, float y) {
    if (!page) return HPDF_ERROR_INVALID_PARAM;
    
    pdf_format_float(page->content, x);
    strbuf_append_char(page->content, ' ');
    pdf_format_float(page->content, y);
    strbuf_append_str(page->content, " m\n");
    
    return HPDF_OK;
}

HPDF_STATUS HPDF_Page_LineTo(HPDF_Page page, float x, float y) {
    if (!page) return HPDF_ERROR_INVALID_PARAM;
    
    pdf_format_float(page->content, x);
    strbuf_append_char(page->content, ' ');
    pdf_format_float(page->content, y);
    strbuf_append_str(page->content, " l\n");
    
    return HPDF_OK;
}

/*---------------------------------------------------------------------------*/
/*  Path Painting Functions                                                  */
/*---------------------------------------------------------------------------*/

HPDF_STATUS HPDF_Page_Fill(HPDF_Page page) {
    if (!page) return HPDF_ERROR_INVALID_PARAM;
    
    strbuf_append_str(page->content, "f\n");
    
    return HPDF_OK;
}

HPDF_STATUS HPDF_Page_Stroke(HPDF_Page page) {
    if (!page) return HPDF_ERROR_INVALID_PARAM;
    
    strbuf_append_str(page->content, "S\n");
    
    return HPDF_OK;
}

HPDF_STATUS HPDF_Page_ClosePathFillStroke(HPDF_Page page) {
    if (!page) return HPDF_ERROR_INVALID_PARAM;
    
    strbuf_append_str(page->content, "b\n");
    
    return HPDF_OK;
}

/*---------------------------------------------------------------------------*/
/*  Text Functions                                                           */
/*---------------------------------------------------------------------------*/

HPDF_STATUS HPDF_Page_BeginText(HPDF_Page page) {
    if (!page) return HPDF_ERROR_INVALID_PARAM;
    if (page->in_text_object) return HPDF_ERROR_INVALID_STATE;
    
    strbuf_append_str(page->content, "BT\n");
    page->in_text_object = true;
    
    // set font if one is selected
    if (page->current_font) {
        strbuf_append_str(page->content, "/");
        strbuf_append_str(page->content, page->current_font->resource_name);
        strbuf_append_str(page->content, " ");
        pdf_format_float(page->content, page->font_size);
        strbuf_append_str(page->content, " Tf\n");
    }
    
    return HPDF_OK;
}

HPDF_STATUS HPDF_Page_EndText(HPDF_Page page) {
    if (!page) return HPDF_ERROR_INVALID_PARAM;
    if (!page->in_text_object) return HPDF_ERROR_INVALID_STATE;
    
    strbuf_append_str(page->content, "ET\n");
    page->in_text_object = false;
    
    return HPDF_OK;
}

HPDF_STATUS HPDF_Page_TextOut(HPDF_Page page, float x, float y, const char* text) {
    if (!page || !text) return HPDF_ERROR_INVALID_PARAM;
    
    bool need_end = false;
    if (!page->in_text_object) {
        HPDF_Page_BeginText(page);
        need_end = true;
    }
    
    // move to position
    pdf_format_float(page->content, x);
    strbuf_append_char(page->content, ' ');
    pdf_format_float(page->content, y);
    strbuf_append_str(page->content, " Td\n");
    
    // show text
    pdf_escape_text(page->content, text);
    strbuf_append_str(page->content, " Tj\n");
    
    if (need_end) {
        HPDF_Page_EndText(page);
    }
    
    return HPDF_OK;
}

HPDF_STATUS HPDF_Page_MoveTextPos(HPDF_Page page, float x, float y) {
    if (!page) return HPDF_ERROR_INVALID_PARAM;
    if (!page->in_text_object) return HPDF_ERROR_INVALID_STATE;
    
    pdf_format_float(page->content, x);
    strbuf_append_char(page->content, ' ');
    pdf_format_float(page->content, y);
    strbuf_append_str(page->content, " Td\n");
    
    return HPDF_OK;
}

HPDF_STATUS HPDF_Page_ShowText(HPDF_Page page, const char* text) {
    if (!page || !text) return HPDF_ERROR_INVALID_PARAM;
    if (!page->in_text_object) return HPDF_ERROR_INVALID_STATE;
    
    pdf_escape_text(page->content, text);
    strbuf_append_str(page->content, " Tj\n");
    
    return HPDF_OK;
}

/*---------------------------------------------------------------------------*/
/*  Graphics State Stack                                                     */
/*---------------------------------------------------------------------------*/

HPDF_STATUS HPDF_Page_GSave(HPDF_Page page) {
    if (!page) return HPDF_ERROR_INVALID_PARAM;
    
    strbuf_append_str(page->content, "q\n");
    
    return HPDF_OK;
}

HPDF_STATUS HPDF_Page_GRestore(HPDF_Page page) {
    if (!page) return HPDF_ERROR_INVALID_PARAM;
    
    strbuf_append_str(page->content, "Q\n");
    
    return HPDF_OK;
}

/*---------------------------------------------------------------------------*/
/*  PDF Output Generation                                                    */
/*---------------------------------------------------------------------------*/

// comparison function for sorting objects by id
static int compare_objects(const void* a, const void* b) {
    const PdfObject* obj_a = *(const PdfObject**)a;
    const PdfObject* obj_b = *(const PdfObject**)b;
    return obj_a->id - obj_b->id;
}

HPDF_STATUS HPDF_SaveToFile(HPDF_Doc doc, const char* filename) {
    if (!doc || !filename) return HPDF_ERROR_INVALID_PARAM;
    
    FILE* file = fopen(filename, "wb");
    if (!file) {
        if (doc->error_fn) {
            doc->error_fn(HPDF_ERROR_FILE_IO, 0, doc->error_user_data);
        }
        return HPDF_ERROR_FILE_IO;
    }
    
    long offset;
    
    // clear object list for fresh offsets (objects are in arena, just clear the list)
    arraylist_clear(doc->objects);
    
    // --- Header ---
    fprintf(file, "%%PDF-1.4\n");
    fprintf(file, "%%\xE2\xE3\xCF\xD3\n");  // binary marker
    
    // --- Info Dictionary ---
    offset = ftell(file);
    record_obj_offset(doc, doc->info_id, offset, PDF_OBJ_INFO);
    fprintf(file, "%d 0 obj\n<<\n", doc->info_id);
    if (doc->creator) {
        fprintf(file, "/Creator ");
        StrBuf* escaped = strbuf_new();
        pdf_escape_text(escaped, doc->creator);
        fprintf(file, "%s\n", escaped->str);
        strbuf_free(escaped);
    }
    if (doc->producer) {
        fprintf(file, "/Producer ");
        StrBuf* escaped = strbuf_new();
        pdf_escape_text(escaped, doc->producer);
        fprintf(file, "%s\n", escaped->str);
        strbuf_free(escaped);
    }
    if (doc->title) {
        fprintf(file, "/Title ");
        StrBuf* escaped = strbuf_new();
        pdf_escape_text(escaped, doc->title);
        fprintf(file, "%s\n", escaped->str);
        strbuf_free(escaped);
    }
    if (doc->author) {
        fprintf(file, "/Author ");
        StrBuf* escaped = strbuf_new();
        pdf_escape_text(escaped, doc->author);
        fprintf(file, "%s\n", escaped->str);
        strbuf_free(escaped);
    }
    fprintf(file, ">>\nendobj\n\n");
    
    // --- Font Objects ---
    for (int i = 0; i < doc->fonts->length; i++) {
        HPDF_Font font = (HPDF_Font)doc->fonts->data[i];
        
        offset = ftell(file);
        record_obj_offset(doc, font->obj_id, offset, PDF_OBJ_FONT);
        
        fprintf(file, "%d 0 obj\n<<\n", font->obj_id);
        fprintf(file, "/Type /Font\n");
        fprintf(file, "/Subtype /Type1\n");
        fprintf(file, "/BaseFont /%s\n", font->name);
        fprintf(file, ">>\nendobj\n\n");
    }
    
    // --- Page Content Streams ---
    for (int i = 0; i < doc->pages->length; i++) {
        HPDF_Page page = (HPDF_Page)doc->pages->data[i];
        
        offset = ftell(file);
        record_obj_offset(doc, page->contents_id, offset, PDF_OBJ_CONTENT);
        
        // content stream (uncompressed)
        size_t content_len = page->content->length;
        fprintf(file, "%d 0 obj\n<<\n/Length %zu\n>>\nstream\n", 
                page->contents_id, content_len);
        fwrite(page->content->str, 1, content_len, file);
        fprintf(file, "\nendstream\nendobj\n\n");
    }
    
    // --- Page Objects ---
    for (int i = 0; i < doc->pages->length; i++) {
        HPDF_Page page = (HPDF_Page)doc->pages->data[i];
        
        offset = ftell(file);
        record_obj_offset(doc, page->obj_id, offset, PDF_OBJ_PAGE);
        
        fprintf(file, "%d 0 obj\n<<\n", page->obj_id);
        fprintf(file, "/Type /Page\n");
        fprintf(file, "/Parent %d 0 R\n", doc->pages_id);
        fprintf(file, "/MediaBox [0 0 %.2f %.2f]\n", page->width, page->height);
        fprintf(file, "/Contents %d 0 R\n", page->contents_id);
        
        // resources - fonts
        if (page->used_fonts->length > 0) {
            fprintf(file, "/Resources <<\n/Font <<\n");
            for (int j = 0; j < page->used_fonts->length; j++) {
                HPDF_Font font = (HPDF_Font)page->used_fonts->data[j];
                fprintf(file, "/%s %d 0 R\n", font->resource_name, font->obj_id);
            }
            fprintf(file, ">>\n>>\n");
        }
        
        fprintf(file, ">>\nendobj\n\n");
    }
    
    // --- Pages Tree ---
    offset = ftell(file);
    record_obj_offset(doc, doc->pages_id, offset, PDF_OBJ_PAGES);
    
    fprintf(file, "%d 0 obj\n<<\n", doc->pages_id);
    fprintf(file, "/Type /Pages\n");
    fprintf(file, "/Kids [");
    for (int i = 0; i < doc->pages->length; i++) {
        HPDF_Page page = (HPDF_Page)doc->pages->data[i];
        fprintf(file, "%d 0 R ", page->obj_id);
    }
    fprintf(file, "]\n");
    fprintf(file, "/Count %d\n", doc->pages->length);
    fprintf(file, ">>\nendobj\n\n");
    
    // --- Catalog ---
    offset = ftell(file);
    record_obj_offset(doc, doc->catalog_id, offset, PDF_OBJ_CATALOG);
    
    fprintf(file, "%d 0 obj\n<<\n", doc->catalog_id);
    fprintf(file, "/Type /Catalog\n");
    fprintf(file, "/Pages %d 0 R\n", doc->pages_id);
    fprintf(file, ">>\nendobj\n\n");
    
    // --- Cross-Reference Table ---
    long xref_offset = ftell(file);
    
    // sort objects by id
    int num_objects = doc->objects->length;
    PdfObject** sorted = (PdfObject**)malloc(num_objects * sizeof(PdfObject*));
    for (int i = 0; i < num_objects; i++) {
        sorted[i] = (PdfObject*)doc->objects->data[i];
    }
    qsort(sorted, num_objects, sizeof(PdfObject*), compare_objects);
    
    // find max object id
    int max_obj_id = 0;
    for (int i = 0; i < num_objects; i++) {
        if (sorted[i]->id > max_obj_id) {
            max_obj_id = sorted[i]->id;
        }
    }
    
    fprintf(file, "xref\n");
    fprintf(file, "0 %d\n", max_obj_id + 1);
    
    // entry for object 0 (free list head)
    fprintf(file, "0000000000 65535 f \n");
    
    // create offset lookup
    long* offsets = (long*)calloc(max_obj_id + 1, sizeof(long));
    for (int i = 0; i < num_objects; i++) {
        offsets[sorted[i]->id] = sorted[i]->offset;
    }
    
    // write xref entries
    for (int i = 1; i <= max_obj_id; i++) {
        if (offsets[i] > 0) {
            fprintf(file, "%010ld 00000 n \n", offsets[i]);
        } else {
            fprintf(file, "0000000000 65535 f \n");
        }
    }
    
    free(offsets);
    free(sorted);
    
    // --- Trailer ---
    fprintf(file, "trailer\n<<\n");
    fprintf(file, "/Size %d\n", max_obj_id + 1);
    fprintf(file, "/Root %d 0 R\n", doc->catalog_id);
    fprintf(file, "/Info %d 0 R\n", doc->info_id);
    fprintf(file, ">>\n");
    fprintf(file, "startxref\n");
    fprintf(file, "%ld\n", xref_offset);
    fprintf(file, "%%%%EOF\n");
    
    fclose(file);
    
    log_info("hpdf: saved pdf to %s (%d pages, %d fonts)", 
             filename, doc->pages->length, doc->fonts->length);
    
    return HPDF_OK;
}
