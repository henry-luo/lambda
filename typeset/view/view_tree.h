#ifndef VIEW_TREE_H
#define VIEW_TREE_H

#include "../../lambda/lambda.h"
#include "../../lib/strbuf.h"
#include <stdbool.h>
#include <stdint.h>

// Forward declarations
typedef struct ViewTree ViewTree;
typedef struct ViewNode ViewNode;
typedef struct ViewStyle ViewStyle;
typedef struct ViewFont ViewFont;
typedef struct ViewPage ViewPage;
typedef struct TypesetEngine TypesetEngine;

// Device-independent coordinate system (typographical points, 1/72 inch)
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

typedef struct ViewTransform {
    double matrix[6];           // 2D transformation matrix [a,b,c,d,tx,ty]
} ViewTransform;

// View node types
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

// Content-specific structures
typedef struct ViewTextRun {
    char* text;                 // UTF-8 text content
    int text_length;            // Text length in bytes
    int glyph_count;            // Number of glyphs
    
    ViewFont* font;             // Font for this run
    struct ViewColor {
        double r, g, b, a;      // RGBA components (0.0 - 1.0)
        char* name;             // Color name (optional)
    } color;                    // Text color
    double font_size;           // Font size
    
    // Glyph positioning
    struct ViewGlyphInfo {
        uint32_t glyph_id;      // Glyph ID in font
        uint32_t codepoint;     // Unicode codepoint
        double advance_width;   // Glyph advance width
        double advance_height;  // Glyph advance height
        ViewPoint offset;       // Glyph offset
    }* glyphs;                  // Array of glyph information
    ViewPoint* glyph_positions; // Array of glyph positions
    double total_width;         // Total width of text run
    double ascent;              // Ascent
    double descent;             // Descent
    
    // Text shaping information
    bool is_shaped;             // Text has been shaped
    enum ViewTextDirection {
        VIEW_TEXT_LTR,
        VIEW_TEXT_RTL
    } direction;                // Text direction
    enum ViewScript {
        VIEW_SCRIPT_LATIN,
        VIEW_SCRIPT_ARABIC,
        VIEW_SCRIPT_CHINESE,
        VIEW_SCRIPT_OTHER
    } script;                   // Unicode script
    char* language;             // Language code
} ViewTextRun;

typedef struct ViewMathElement {
    enum ViewMathElementType {
        VIEW_MATH_ATOM,
        VIEW_MATH_FRACTION,
        VIEW_MATH_SCRIPT,
        VIEW_MATH_RADICAL,
        VIEW_MATH_MATRIX,
        VIEW_MATH_DELIMITER
    } type;                     // Type of math element
    
    // Mathematical properties
    double width, height, depth; // Math dimensions
    double axis_height;         // Mathematical axis
    double italic_correction;   // Italic correction
    
    // Math style
    enum ViewMathStyle {
        VIEW_MATH_DISPLAY,
        VIEW_MATH_TEXT,
        VIEW_MATH_SCRIPT,
        VIEW_MATH_SCRIPTSCRIPT
    } math_style;               // Math style
    bool is_cramped;            // Cramped style
    
    enum ViewMathClass {
        VIEW_MATH_ORD,          // Ordinary
        VIEW_MATH_OP,           // Large operator
        VIEW_MATH_BIN,          // Binary operation
        VIEW_MATH_REL,          // Relation
        VIEW_MATH_OPEN,         // Opening delimiter
        VIEW_MATH_CLOSE,        // Closing delimiter
        VIEW_MATH_PUNCT,        // Punctuation
        VIEW_MATH_INNER         // Inner
    } math_class;               // Math class for spacing
    
    // Content (union based on type)
    void* math_content;         // Type-specific content
} ViewMathElement;

typedef struct ViewGeometry {
    enum ViewGeometryType {
        VIEW_GEOM_LINE,
        VIEW_GEOM_RECTANGLE,
        VIEW_GEOM_CIRCLE,
        VIEW_GEOM_PATH
    } type;                     // Geometry type
    
    struct ViewColor color;     // Color
    double stroke_width;        // Stroke width
    bool filled;                // Filled geometry
    
    // Geometry data (union based on type)
    void* geometry_data;        // Type-specific geometry data
} ViewGeometry;

typedef struct ViewImage {
    char* src;                  // Image source (path or data URI)
    char* alt_text;             // Alternative text
    char* mime_type;            // MIME type
    ViewSize natural_size;      // Natural image size
    double resolution;          // Image resolution (DPI)
    
    // Image data
    uint8_t* image_data;        // Raw image data
    size_t data_size;           // Data size in bytes
} ViewImage;

typedef struct ViewGroup {
    char* name;                 // Group name
    ViewTransform group_transform; // Group transformation
    struct ViewColor background_color; // Background color
    bool clip_children;         // Clip children to bounds
} ViewGroup;

// Clipping path
typedef struct ViewClipPath {
    enum ViewClipType {
        VIEW_CLIP_RECT,
        VIEW_CLIP_PATH
    } type;                     // Clip type
    
    union {
        ViewRect clip_rect;     // Rectangle clipping
        void* clip_path;        // Path clipping
    } clip_data;
} ViewClipPath;

// Main view node structure
struct ViewNode {
    ViewNodeType type;          // Node type
    
    // Hierarchy
    ViewNode* parent;           // Parent node
    ViewNode* first_child;      // First child
    ViewNode* last_child;       // Last child  
    ViewNode* next_sibling;     // Next sibling
    ViewNode* prev_sibling;     // Previous sibling
    int child_count;            // Number of children
    
    // Geometric properties (device-independent units)
    ViewRect bounds;            // Bounding rectangle
    ViewPoint position;         // Position relative to parent
    ViewSize size;              // Element size
    ViewTransform transform;    // Transformation matrix
    
    // Visual properties
    ViewStyle* style;           // Computed visual style
    ViewClipPath* clip_path;    // Clipping path
    double opacity;             // Opacity (0.0 - 1.0)
    bool visible;               // Visibility flag
    
    // Content data (union based on node type)
    union {
        ViewTextRun* text_run;      // For text runs
        ViewMathElement* math_elem; // For math elements
        ViewGeometry* geometry;     // For geometric shapes
        ViewImage* image;           // For images
        ViewGroup* group;           // For groups
    } content;
    
    // Metadata
    char* id;                   // Unique identifier
    char* class_name;           // CSS class
    char* semantic_role;        // Semantic role (heading, paragraph, etc.)
    
    // Source tracking
    Item source_lambda_item;    // Original Lambda item (if any)
    int source_line;            // Source line number
    int source_column;          // Source column number
    
    // Reference counting
    int ref_count;              // Reference count
};

// View page structure
struct ViewPage {
    int page_number;            // Page number (1-based)
    ViewSize page_size;         // Page size
    ViewRect content_area;      // Content area (excluding margins)
    ViewRect margin_area;       // Margin area
    
    ViewNode* page_node;        // Root node for this page
    
    // Page metadata
    char* page_label;           // Page label
    bool is_landscape;          // Landscape orientation
};

// View tree statistics
typedef struct ViewStats {
    int total_nodes;            // Total number of nodes
    int text_runs;              // Number of text runs
    int math_elements;          // Number of math elements
    int geometric_elements;     // Number of geometric elements
    float total_text_length;    // Total text length
    float layout_time;          // Time spent on layout (seconds)
    size_t memory_usage;        // Memory usage (bytes)
} ViewStats;

// Main view tree structure
struct ViewTree {
    ViewNode* root;             // Root node
    
    // Document properties
    ViewSize document_size;     // Total document size
    int page_count;             // Number of pages
    ViewPage** pages;           // Array of pages
    
    // Style information
    struct ViewStyleSheet* stylesheet; // Computed styles
    struct ViewFontRegistry* fonts;    // Font registry
    
    // Metadata
    char* title;                // Document title
    char* author;               // Document author
    char* subject;              // Document subject
    char* creator;              // Creator ("Lambda Typesetting System")
    char* creation_date;        // Creation date (ISO 8601)
    
    // Statistics
    ViewStats stats;            // Layout and rendering statistics
    
    // Reference counting
    int ref_count;              // Reference count
};

// View tree creation and destruction
ViewTree* view_tree_create(void);
ViewTree* view_tree_create_with_root(ViewNode* root);
void view_tree_retain(ViewTree* tree);
void view_tree_release(ViewTree* tree);

// View node creation and destruction
ViewNode* view_node_create(ViewNodeType type);
ViewNode* view_node_create_text_run(const char* text, ViewFont* font, double font_size);
ViewNode* view_node_create_math_element(ViewMathElement* math_elem);
ViewNode* view_node_create_geometry(ViewGeometry* geometry);
ViewNode* view_node_create_image(ViewImage* image);
ViewNode* view_node_create_group(const char* name);

void view_node_retain(ViewNode* node);
void view_node_release(ViewNode* node);

// View tree hierarchy management
void view_node_add_child(ViewNode* parent, ViewNode* child);
void view_node_insert_child(ViewNode* parent, ViewNode* child, int index);
void view_node_remove_child(ViewNode* parent, ViewNode* child);
void view_node_remove_from_parent(ViewNode* node);

// View tree traversal
typedef bool (*ViewNodeVisitor)(ViewNode* node, void* context);
void view_tree_walk(ViewTree* tree, ViewNodeVisitor visitor, void* context);
void view_node_walk(ViewNode* node, ViewNodeVisitor visitor, void* context);

// View tree queries
ViewNode* view_tree_find_node_by_id(ViewTree* tree, const char* id);
ViewNode* view_tree_find_node_by_role(ViewTree* tree, const char* role);
ViewNode** view_tree_find_nodes_by_type(ViewTree* tree, ViewNodeType type, int* count);

// View tree geometry and transforms
ViewRect view_tree_get_bounding_box(ViewTree* tree);
ViewRect view_node_get_absolute_bounds(ViewNode* node);
void view_tree_apply_transform(ViewTree* tree, ViewTransform* transform);
void view_node_apply_transform(ViewNode* node, ViewTransform* transform);

// View tree manipulation
ViewTree* view_tree_extract_pages(ViewTree* tree, int start_page, int end_page);
ViewTree* view_tree_merge(ViewTree* tree1, ViewTree* tree2);
void view_tree_optimize(ViewTree* tree);

// View tree analysis
ViewStats* view_tree_calculate_stats(ViewTree* tree);
double view_tree_get_total_text_length(ViewTree* tree);
ViewSize view_tree_get_total_size(ViewTree* tree);

// Utility functions
ViewTransform view_transform_identity(void);
ViewTransform view_transform_translate(double dx, double dy);
ViewTransform view_transform_scale(double sx, double sy);
ViewTransform view_transform_rotate(double angle);
ViewTransform view_transform_multiply(ViewTransform* a, ViewTransform* b);

ViewPoint view_point_transform(ViewPoint point, ViewTransform* transform);
ViewRect view_rect_transform(ViewRect rect, ViewTransform* transform);

bool view_rect_contains_point(ViewRect rect, ViewPoint point);
bool view_rect_intersects_rect(ViewRect rect1, ViewRect rect2);
ViewRect view_rect_union(ViewRect rect1, ViewRect rect2);

#endif // VIEW_TREE_H
