#ifndef FORMAT_LATEX_HTML_H
#define FORMAT_LATEX_HTML_H

#include "../lambda-data.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/mempool.h"

#ifdef __cplusplus
extern "C" {
#endif

// Main API function for LaTeX to HTML conversion
void format_latex_to_html(StringBuf* html_buf, StringBuf* css_buf, Item latex_ast, Pool* pool);

#ifdef __cplusplus
}

// =============================================================================
// C++ Structures for Internal Use
// =============================================================================

#include <map>
#include <string>
#include <vector>

// Forward declarations
struct RenderContext;
struct Element;

// Font context for tracking font declarations
typedef enum {
    FONT_SERIES_NORMAL,
    FONT_SERIES_BOLD
} FontSeries;

typedef enum {
    FONT_SHAPE_UPRIGHT,
    FONT_SHAPE_ITALIC,
    FONT_SHAPE_SLANTED,
    FONT_SHAPE_SMALL_CAPS
} FontShape;

typedef enum {
    FONT_FAMILY_ROMAN,
    FONT_FAMILY_SANS_SERIF,
    FONT_FAMILY_TYPEWRITER
} FontFamily;

typedef enum {
    FONT_SIZE_TINY,
    FONT_SIZE_SCRIPTSIZE,
    FONT_SIZE_FOOTNOTESIZE,
    FONT_SIZE_SMALL,
    FONT_SIZE_NORMALSIZE,
    FONT_SIZE_LARGE,
    FONT_SIZE_LARGE_CAP,
    FONT_SIZE_LARGE_2,
    FONT_SIZE_HUGE,
    FONT_SIZE_HUGE_CAP
} FontSize;

typedef struct {
    FontSeries series;
    FontShape shape;
    FontFamily family;
    FontSize size;
    bool em_active;
} FontContext;

// Alignment context for tracking paragraph alignment
typedef enum {
    ALIGN_NORMAL,
    ALIGN_CENTERING,
    ALIGN_RAGGEDRIGHT,
    ALIGN_RAGGEDLEFT
} AlignmentMode;

// Document metadata storage
typedef struct {
    char* title;
    char* author;
    char* date;
    bool in_document;
    int section_counter;
} DocumentState;

// Paragraph state context for CSS class management
typedef struct {
    bool after_block_element;
    bool noindent_next;
} ParagraphState;

// Counter information
struct Counter {
    int value = 0;
    std::string parent;
    std::vector<std::string> children;
};

// Label information
struct LabelInfo {
    std::string anchor_id;
    std::string ref_text;
};

// Macro definition
struct MacroDefinition {
    std::string name;
    int num_params;
    std::vector<std::string> default_values;
    Element* definition;
    bool is_environment;
};

// Unified render context - consolidates all global state
struct RenderContext {
    // Document state
    DocumentState doc_state;
    
    // Font context
    FontContext font_ctx;
    
    // Paragraph state
    ParagraphState para_state;
    
    // Alignment state
    AlignmentMode alignment;
    
    // Counter system
    std::map<std::string, Counter> counters;
    bool counters_initialized;
    
    // Label/reference system
    std::map<std::string, LabelInfo> labels;
    bool labels_initialized;
    std::string current_label_anchor;
    std::string current_label_text;
    int label_counter;
    
    // Macro system
    std::map<std::string, MacroDefinition> macros;
    bool macros_initialized;
    
    // Chapter/section tracking
    int chapter_counter;
    int section_counter;
    int global_section_id;
    
    // Memory pool
    Pool* pool;
    
    // Constructor - initialize all state
    RenderContext(Pool* p);
    
    // Helper methods for counter system
    void init_counters();
    void new_counter(const std::string& name, const std::string& parent = "");
    void set_counter(const std::string& name, int value);
    void add_to_counter(const std::string& name, int delta);
    void step_counter(const std::string& name);
    int get_counter_value(const std::string& name);
    bool counter_exists(const std::string& name) const;
    
    // Helper methods for label system
    void init_labels();
    void set_current_label(const std::string& anchor, const std::string& text);
    void register_label(const std::string& name);
    LabelInfo get_label_info(const std::string& name);
    std::string generate_anchor_id(const std::string& prefix = "ref");
    
    // Convenience methods for label/ref commands
    void define_label(const char* name, const char* value, int page);
    const char* get_label_value(const char* name);
    int get_label_page(const char* name);
    
    // Helper methods for macro system
    void init_macros();
    void register_macro(const std::string& name, int num_params, Element* definition, bool is_environment = false);
    bool is_macro(const std::string& name);
    MacroDefinition* get_macro(const std::string& name);
    
    // Convenience method for macro definition command
    void define_macro(const char* name, int num_args, const char* definition);
    
    // Convenience method for counter definition
    void define_counter(const char* name);
};

#endif // __cplusplus

#endif // FORMAT_LATEX_HTML_H
