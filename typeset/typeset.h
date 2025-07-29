#ifndef TYPESET_H
#define TYPESET_H

#include "../lambda/lambda.h"
#include "../lib/strbuf.h"
#include <stdbool.h>
#include <stdint.h>

// Forward declarations
typedef struct TypesetEngine TypesetEngine;
typedef struct ViewTree ViewTree;
typedef struct ViewNode ViewNode;
typedef struct ViewRenderer ViewRenderer;

// Include core module headers (only implemented ones)
#include "view/view_tree.h"
#include "output/renderer.h"

// Typesetting options
typedef struct TypesetOptions {
    // Page settings
    double page_width;          // Page width in points (default: 612)
    double page_height;         // Page height in points (default: 792)
    double margin_left;         // Left margin in points (default: 72)
    double margin_right;        // Right margin in points (default: 72)
    double margin_top;          // Top margin in points (default: 72)
    double margin_bottom;       // Bottom margin in points (default: 72)
    
    // Typography
    char* default_font_family;  // Default font family
    double default_font_size;   // Default font size (12pt default)
    double line_height;         // Line height multiplier (1.2 default)
    double paragraph_spacing;   // Paragraph spacing
    
    // Quality settings
    bool optimize_layout;       // Optimize layout for performance
    bool show_debug_info;       // Show debug information
} TypesetOptions;

// Main typesetting engine
struct TypesetEngine {
    Context* lambda_context;    // Lambda context
    TypesetOptions* options;    // Default options
    
    // Statistics
    struct {
        int documents_processed;
        int pages_generated;
        double total_layout_time;
        size_t memory_usage;
    } stats;
};

// Core API functions
TypesetEngine* typeset_engine_create(Context* ctx);
void typeset_engine_destroy(TypesetEngine* engine);

// Main typesetting functions - produces device-independent view tree
ViewTree* typeset_create_view_tree(TypesetEngine* engine, Item content, TypesetOptions* options);

// View tree manipulation (basic functions)
ViewNode* view_tree_find_node_by_id(ViewTree* tree, const char* id);
ViewRect view_tree_get_bounding_box(ViewTree* tree);

// Options management
TypesetOptions* typeset_options_create_default(void);
void typeset_options_destroy(TypesetOptions* options);

// Lambda function integration (basic)
Item fn_typeset(Context* ctx, Item* args, int arg_count);

// Common constants
#define TYPESET_DEFAULT_PAGE_WIDTH 612.0        // Letter width in points
#define TYPESET_DEFAULT_PAGE_HEIGHT 792.0       // Letter height in points
#define TYPESET_DEFAULT_MARGIN 72.0             // 1 inch in points
#define TYPESET_DEFAULT_FONT_SIZE 12.0
#define TYPESET_DEFAULT_LINE_HEIGHT 1.2

// Units conversion (everything internal is in points)
#define POINTS_PER_INCH 72.0
#define POINTS_PER_MM 2.834645669
#define POINTS_PER_CM 28.34645669

#endif // TYPESET_H
