#pragma once

#include "view.hpp"
#include "../lib/log.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct LayoutContext;

// Maximum number of src entries in a single @font-face rule
#define FONT_FACE_MAX_SRC 8

// Individual src entry with path and format
typedef struct FontFaceSrc {
    char* path;                  // Resolved local path
    char* format;                // Format string: "woff", "truetype", "opentype", etc.
} FontFaceSrc;

// Font face descriptor for @font-face support
// Descriptors are registered with the unified font module (lib/font) via
// font_face_register(). Actual font loading is handled entirely by the
// unified module; this struct only stores the CSS @font-face metadata.
typedef struct FontFaceDescriptor {
    char* family_name;           // font-family value
    char* src_local_path;        // local font file path (no web URLs) - first/fallback
    char* src_local_name;        // src: local() font name value
    FontFaceSrc* src_entries;    // Array of all src entries with formats
    int src_count;               // Number of entries in src_entries array
    CssEnum font_style;         // normal, italic, oblique
    CssEnum font_weight;        // 100-900, normal, bold
    CssEnum font_display;       // auto, block, swap, fallback, optional
    bool is_loaded;              // loading state
} FontFaceDescriptor;

// ============================================================================
// Text flow logging categories
// ============================================================================

extern log_category_t* font_log;
extern log_category_t* text_log;
extern log_category_t* layout_log;

// Logging initialization
void init_text_flow_logging(void);
void setup_text_flow_log_categories(void);

// Structured logging for font operations
void log_font_loading_attempt(const char* family_name, const char* path);
void log_font_loading_result(const char* family_name, bool success, const char* error);
void log_font_fallback_triggered(const char* requested, const char* fallback);

// ============================================================================
// CSS @font-face parsing and registration
// ============================================================================

// Forward declarations for CSS types
struct CssStylesheet;

// Parse and register @font-face rules from a CSS rule node
void parse_font_face_rule(struct LayoutContext* lycon, void* rule);

// Register a font face descriptor with UiContext (and bridge to unified FontContext)
void register_font_face(UiContext* uicon, FontFaceDescriptor* descriptor);

// Process all @font-face rules from a stylesheet
void process_font_face_rules_from_stylesheet(UiContext* uicon, struct CssStylesheet* stylesheet, const char* base_path);

// Process all @font-face rules from all stylesheets in a document
void process_document_font_faces(UiContext* uicon, struct DomDocument* doc);

#ifdef __cplusplus
}
#endif
