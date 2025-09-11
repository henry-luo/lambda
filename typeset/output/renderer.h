#ifndef RENDERER_H
#define RENDERER_H

#include "../view/view_tree.h"
#include "../../lib/strbuf.h"
#include <stdbool.h>

// Forward declarations
typedef struct ViewRenderer ViewRenderer;
typedef struct ViewRenderOptions ViewRenderOptions;

// Output format types
typedef enum {
    VIEW_FORMAT_SVG,
    VIEW_FORMAT_HTML,
    VIEW_FORMAT_PDF,
    VIEW_FORMAT_PNG,
    VIEW_FORMAT_TEX
} ViewFormat;

// Color space definitions
typedef enum {
    VIEW_COLOR_SPACE_RGB,
    VIEW_COLOR_SPACE_SRGB,
    VIEW_COLOR_SPACE_CMYK,
    VIEW_COLOR_SPACE_GRAY
} ViewColorSpace;

// Render quality settings
typedef enum {
    VIEW_RENDER_QUALITY_DRAFT,
    VIEW_RENDER_QUALITY_NORMAL,
    VIEW_RENDER_QUALITY_HIGH,
    VIEW_RENDER_QUALITY_PRINT
} ViewRenderQuality;

// Base render options
struct ViewRenderOptions {
    // Output format
    ViewFormat format;          // Output format type
    
    // Output format specific options
    double dpi;                 // For raster formats (72.0 default)
    bool embed_fonts;           // Embed font data
    bool optimize_output;       // Optimize output size
    ViewColorSpace color_space; // Color space
    
    // Quality settings
    ViewRenderQuality quality;  // Render quality
    bool anti_alias;            // Anti-aliasing
    double scale_factor;        // Scale factor (1.0 default)
    
    // Metadata
    bool include_metadata;      // Include metadata
    bool include_accessibility; // Include accessibility info
    
    // Clipping and viewport
    ViewRect* viewport;         // Viewport (NULL for full document)
    bool clip_to_viewport;      // Clip content to viewport
};

// Base renderer interface
struct ViewRenderer {
    // Renderer information
    char* name;                 // Renderer name ("HTML Renderer")
    char* format_name;          // Output format name ("HTML")
    char* mime_type;            // MIME type ("text/html")
    char* file_extension;       // File extension (".html")
    ViewFormat format;          // Format type enum
    
    // Renderer functions
    bool (*initialize)(ViewRenderer* renderer, ViewRenderOptions* options);
    bool (*render_tree)(ViewRenderer* renderer, ViewTree* tree, StrBuf* output);
    bool (*render_node)(ViewRenderer* renderer, ViewNode* node);
    void (*finalize)(ViewRenderer* renderer);
    void (*cleanup)(ViewRenderer* renderer);
    
    // Renderer-specific data
    void* renderer_data;
    
    // Options
    ViewRenderOptions* options;
};

// Renderer creation and destruction
ViewRenderer* view_renderer_create(const char* format_name);
void view_renderer_destroy(ViewRenderer* renderer);

// Generic rendering function
bool view_render_tree(ViewRenderer* renderer, ViewTree* tree, StrBuf* output, ViewRenderOptions* options);

// Convenience functions for common formats
StrBuf* render_view_tree_to_html(ViewTree* tree, ViewRenderOptions* options);
StrBuf* render_view_tree_to_svg(ViewTree* tree, ViewRenderOptions* options);
StrBuf* render_view_tree_to_tex(ViewTree* tree, ViewRenderOptions* options);
bool render_view_tree_to_pdf_file(ViewTree* tree, const char* filename, ViewRenderOptions* options);
bool render_view_tree_to_png_file(ViewTree* tree, const char* filename, ViewRenderOptions* options);

// Render options management
ViewRenderOptions* view_render_options_create_default(void);
void view_render_options_destroy(ViewRenderOptions* options);
ViewRenderOptions* view_render_options_copy(ViewRenderOptions* options);

// HTML renderer specific
typedef enum {
    HTML_VERSION_HTML4,
    HTML_VERSION_XHTML,
    HTML_VERSION_HTML5
} HTMLVersion;

typedef struct HTMLRenderOptions {
    ViewRenderOptions base;     // Base options
    
    // HTML-specific options
    bool use_semantic_html;     // Use semantic HTML5 elements
    bool inline_css;            // Inline CSS vs external stylesheet
    bool generate_toc;          // Generate table of contents
    HTMLVersion html_version;   // HTML version to generate
    bool pretty_print;          // Pretty print HTML
    int indent_size;            // Indentation size
    
    // CSS options
    bool use_css_grid;          // Use CSS Grid for layout
    bool use_flexbox;           // Use Flexbox for layout
    bool include_print_styles;  // Include print stylesheets
} HTMLRenderOptions;

HTMLRenderOptions* html_render_options_create_default(void);

// SVG renderer specific
typedef struct SVGRenderOptions {
    ViewRenderOptions base;     // Base options
    
    // SVG-specific options
    bool embed_fonts;           // Embed font data in SVG
    bool optimize_paths;        // Optimize SVG paths
    int decimal_precision;      // Decimal precision (2 default)
    bool use_viewbox;           // Use viewBox attribute
    
    // Text rendering
    bool convert_text_to_paths; // Convert text to paths
    bool use_css_fonts;         // Use CSS font declarations
} SVGRenderOptions;

SVGRenderOptions* svg_render_options_create_default(void);

// PDF renderer specific
typedef struct PDFRenderOptions {
    ViewRenderOptions base;     // Base options
    
    // PDF-specific options
    bool subset_fonts;          // Subset fonts
    bool enable_bookmarks;      // Generate bookmarks
    bool enable_links;          // Generate hyperlinks
    bool enable_annotations;    // Generate annotations
    
    // PDF version
    enum PDFVersion {
        PDF_VERSION_1_4,
        PDF_VERSION_1_7,
        PDF_VERSION_2_0
    } pdf_version;
    
    // Compression
    bool compress_streams;      // Compress PDF streams
    bool compress_images;       // Compress embedded images
} PDFRenderOptions;

PDFRenderOptions* pdf_render_options_create_default(void);

// TeX/LaTeX renderer specific
typedef enum {
    TEX_DOC_CLASS_ARTICLE,
    TEX_DOC_CLASS_BOOK,
    TEX_DOC_CLASS_REPORT,
    TEX_DOC_CLASS_MEMOIR
} TeXDocumentClass;

typedef enum {
    TEX_MATH_MODE_LATEX,
    TEX_MATH_MODE_AMSMATH,
    TEX_MATH_MODE_MATHTOOLS
} TeXMathMode;

typedef struct TeXRenderOptions {
    ViewRenderOptions base;     // Base options
    
    // TeX-specific options
    TeXDocumentClass doc_class; // Document class
    bool use_packages;          // Include required packages
    bool generate_preamble;     // Generate document preamble
    TeXMathMode math_mode;      // Math rendering mode
    
    // Output format
    bool output_xelatex;        // XeLaTeX compatible output
    bool output_lualatex;       // LuaLaTeX compatible output
    
    // Font handling
    bool use_fontspec;          // Use fontspec package
    bool convert_unicode;       // Convert Unicode to LaTeX commands
} TeXRenderOptions;

TeXRenderOptions* tex_render_options_create_default(void);

// PNG renderer specific
typedef struct PNGRenderOptions {
    ViewRenderOptions base;     // Base options
    
    // PNG-specific options
    int compression_level;      // PNG compression level (0-9)
    bool use_transparency;      // Use transparency
    struct ViewColor background_color; // Background color
    
    // Rasterization
    double pixel_density;       // Pixel density (DPI)
    bool smooth_scaling;        // Use smooth scaling
} PNGRenderOptions;

PNGRenderOptions* png_render_options_create_default(void);

#endif // RENDERER_H
