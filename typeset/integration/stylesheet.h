#ifndef STYLESHEET_H
#define STYLESHEET_H

#include "../typeset.h"

// CSS-like selector types
typedef enum {
    SELECTOR_TYPE_ELEMENT,      // element name (e.g., "p", "h1")
    SELECTOR_TYPE_CLASS,        // class name (e.g., ".highlight")
    SELECTOR_TYPE_ID,           // element ID (e.g., "#title")
    SELECTOR_TYPE_ATTRIBUTE,    // attribute selector (e.g., "[type=math]")
    SELECTOR_TYPE_DESCENDANT,   // descendant combinator (e.g., "div p")
    SELECTOR_TYPE_CHILD,        // child combinator (e.g., "div > p")
    SELECTOR_TYPE_SIBLING,      // sibling combinator (e.g., "h1 + p")
    SELECTOR_TYPE_UNIVERSAL,    // universal selector (*)
    SELECTOR_TYPE_PSEUDO_CLASS, // pseudo-class (e.g., ":first-child")
    SELECTOR_TYPE_COMPOUND      // compound selector (multiple conditions)
} SelectorType;

// Style rule selector
typedef struct StyleSelector {
    SelectorType type;
    char* element_name;         // Element name for element selectors
    char* class_name;           // Class name for class selectors
    char* id_name;              // ID for ID selectors
    char* attribute_name;       // Attribute name
    char* attribute_value;      // Attribute value
    char* pseudo_class;         // Pseudo-class name
    
    // Selector specificity (for CSS cascade)
    int specificity_a;          // ID selectors
    int specificity_b;          // Class selectors, attributes, pseudo-classes
    int specificity_c;          // Element selectors
    
    // Compound selector components
    struct StyleSelector** components; // Components of compound selector
    int component_count;        // Number of components
    
    // Next selector in chain (for complex selectors)
    struct StyleSelector* next;
    SelectorType combinator;    // Combinator to next selector
} StyleSelector;

// Style rule structure
typedef struct StyleRule {
    StyleSelector* selector;    // Selector for this rule
    TextStyle* text_style;      // Text style properties
    LayoutStyle* layout_style;  // Layout style properties
    
    // Rule metadata
    int line_number;            // Line number in stylesheet
    char* source_file;          // Source file name
    bool important;             // !important flag
    
    // Cascade information
    int specificity;            // Calculated specificity
    int source_order;           // Order in stylesheet
    
    struct StyleRule* next;     // Next rule in stylesheet
} StyleRule;

// Stylesheet structure
struct StyleSheet {
    StyleRule* first_rule;      // First rule in stylesheet
    StyleRule* last_rule;       // Last rule in stylesheet
    int rule_count;             // Total number of rules
    
    // Default styles
    StyleRule* default_text_rule;       // Default text style
    StyleRule* default_heading_rules[6]; // Default heading styles (h1-h6)
    StyleRule* default_paragraph_rule;   // Default paragraph style
    StyleRule* default_list_rule;        // Default list style
    StyleRule* default_table_rule;       // Default table style
    StyleRule* default_math_rule;        // Default math style
    StyleRule* default_code_rule;        // Default code style
    
    // Stylesheet metadata
    char* title;                // Stylesheet title
    char* description;          // Stylesheet description
    char* author;               // Stylesheet author
    char* version;              // Stylesheet version
    
    // Media queries and conditions
    char* media_query;          // Media query string
    bool screen_media;          // For screen media
    bool print_media;           // For print media
    
    // Performance optimizations
    void* selector_cache;       // Cache for selector matching
    bool cache_enabled;         // Caching enabled
};

// Stylesheet creation and management
StyleSheet* stylesheet_create(void);
void stylesheet_destroy(StyleSheet* sheet);
StyleSheet* stylesheet_create_default(FontManager* font_manager);
StyleSheet* stylesheet_copy(StyleSheet* source);

// Rule management
void stylesheet_add_rule(StyleSheet* sheet, StyleRule* rule);
void stylesheet_remove_rule(StyleSheet* sheet, StyleRule* rule);
void stylesheet_insert_rule_at(StyleSheet* sheet, StyleRule* rule, int index);
StyleRule* stylesheet_get_rule_at(StyleSheet* sheet, int index);

// Style rule creation
StyleRule* style_rule_create(void);
void style_rule_destroy(StyleRule* rule);
StyleRule* style_rule_create_simple(const char* selector_text, TextStyle* text_style, LayoutStyle* layout_style);

// Selector creation and management
StyleSelector* style_selector_create(SelectorType type);
void style_selector_destroy(StyleSelector* selector);
StyleSelector* style_selector_parse(const char* selector_text);
StyleSelector* style_selector_create_element(const char* element_name);
StyleSelector* style_selector_create_class(const char* class_name);
StyleSelector* style_selector_create_id(const char* id_name);
StyleSelector* style_selector_create_attribute(const char* attr_name, const char* attr_value);

// Selector matching
bool selector_matches_node(StyleSelector* selector, DocNode* node);
bool selector_matches_element(StyleSelector* selector, const char* element_name);
bool selector_matches_class(StyleSelector* selector, DocNode* node);
bool selector_matches_id(StyleSelector* selector, DocNode* node);
bool selector_matches_attribute(StyleSelector* selector, DocNode* node);

// Style application
void apply_stylesheet_to_document(Document* doc, StyleSheet* sheet);
void apply_stylesheet_to_node(DocNode* node, StyleSheet* sheet);
StyleRule** find_matching_rules(StyleSheet* sheet, DocNode* node, int* rule_count);
StyleRule* find_best_matching_rule(StyleSheet* sheet, DocNode* node);

// Style computation and cascade
TextStyle* compute_text_style(DocNode* node, StyleRule** matching_rules, int rule_count);
LayoutStyle* compute_layout_style(DocNode* node, StyleRule** matching_rules, int rule_count);
void cascade_styles(DocNode* root, StyleSheet* sheet);
void inherit_computed_styles(DocNode* child, DocNode* parent);

// Specificity calculation
int calculate_selector_specificity(StyleSelector* selector);
int compare_rule_specificity(StyleRule* rule1, StyleRule* rule2);
void sort_rules_by_specificity(StyleRule** rules, int rule_count);

// CSS parsing (simplified CSS-like syntax)
StyleSheet* parse_css_stylesheet(const char* css_text);
StyleRule* parse_css_rule(const char* rule_text);
StyleSelector* parse_css_selector(const char* selector_text);
TextStyle* parse_css_text_properties(const char* properties_text, FontManager* font_manager);
LayoutStyle* parse_css_layout_properties(const char* properties_text);

// Property parsing
bool parse_font_family_property(const char* value, char** font_family);
bool parse_font_size_property(const char* value, float* font_size);
bool parse_font_weight_property(const char* value, uint32_t* font_weight);
bool parse_color_property(const char* value, Color* color);
bool parse_margin_property(const char* value, float margins[4]);
bool parse_padding_property(const char* value, float padding[4]);
bool parse_border_property(const char* value, float* border_width, Color* border_color);
bool parse_text_align_property(const char* value, TextAlign* alignment);
bool parse_display_property(const char* value, DisplayType* display);

// Default stylesheets
StyleSheet* create_default_document_stylesheet(FontManager* font_manager);
StyleSheet* create_minimal_stylesheet(FontManager* font_manager);
StyleSheet* create_academic_paper_stylesheet(FontManager* font_manager);
StyleSheet* create_book_stylesheet(FontManager* font_manager);
StyleSheet* create_web_article_stylesheet(FontManager* font_manager);
StyleSheet* create_presentation_stylesheet(FontManager* font_manager);

// Stylesheet utilities
void stylesheet_merge(StyleSheet* target, StyleSheet* source);
StyleSheet* stylesheet_filter_by_media(StyleSheet* source, const char* media_type);
void stylesheet_optimize(StyleSheet* sheet);
void stylesheet_remove_unused_rules(StyleSheet* sheet, Document* doc);

// Debugging and inspection
void print_stylesheet_debug(StyleSheet* sheet);
void print_style_rule_debug(StyleRule* rule);
void print_style_selector_debug(StyleSelector* selector);
char* stylesheet_to_css_string(StyleSheet* sheet);
char* style_rule_to_css_string(StyleRule* rule);

// Node attribute helpers (for selector matching)
const char* get_node_element_name(DocNode* node);
const char* get_node_class_name(DocNode* node);
const char* get_node_id(DocNode* node);
const char* get_node_attribute(DocNode* node, const char* attr_name);
bool node_has_class(DocNode* node, const char* class_name);
bool node_has_attribute(DocNode* node, const char* attr_name);

// Pseudo-class support
bool matches_first_child_pseudo(DocNode* node);
bool matches_last_child_pseudo(DocNode* node);
bool matches_nth_child_pseudo(DocNode* node, const char* nth_expression);
bool matches_only_child_pseudo(DocNode* node);
bool matches_first_of_type_pseudo(DocNode* node);
bool matches_last_of_type_pseudo(DocNode* node);

// CSS units and value parsing
typedef enum {
    CSS_UNIT_PX,        // Pixels
    CSS_UNIT_PT,        // Points
    CSS_UNIT_IN,        // Inches
    CSS_UNIT_CM,        // Centimeters
    CSS_UNIT_MM,        // Millimeters
    CSS_UNIT_EM,        // Em units
    CSS_UNIT_REM,       // Root em units
    CSS_UNIT_PERCENT,   // Percentage
    CSS_UNIT_NONE       // No unit
} CSSUnit;

typedef struct CSSValue {
    float value;
    CSSUnit unit;
} CSSValue;

CSSValue parse_css_length(const char* value_text);
float css_value_to_points(CSSValue css_value, float base_size);
Color parse_css_color(const char* color_text);

// Stylesheet loading and saving
StyleSheet* load_stylesheet_from_file(const char* filename, FontManager* font_manager);
bool save_stylesheet_to_file(StyleSheet* sheet, const char* filename);
StyleSheet* load_stylesheet_from_string(const char* css_content, FontManager* font_manager);

// Template stylesheets
StyleSheet* load_template_stylesheet(const char* template_name, FontManager* font_manager);
void register_template_stylesheet(const char* name, StyleSheet* sheet);
char** list_available_templates(int* template_count);

// Error handling
typedef struct CSSParseError {
    char* message;
    int line_number;
    int column_number;
    char* problematic_text;
    struct CSSParseError* next;
} CSSParseError;

CSSParseError* css_parse_error_create(const char* message, int line, int column, const char* text);
void css_parse_error_destroy(CSSParseError* error);
void css_parse_error_chain_destroy(CSSParseError* first_error);

// Style inheritance utilities
void compute_inherited_text_style(DocNode* node);
void compute_inherited_layout_style(DocNode* node);
bool text_style_property_inherits(const char* property_name);
bool layout_style_property_inherits(const char* property_name);

// Performance optimization
void stylesheet_build_selector_index(StyleSheet* sheet);
void stylesheet_enable_caching(StyleSheet* sheet);
void stylesheet_disable_caching(StyleSheet* sheet);
void stylesheet_clear_cache(StyleSheet* sheet);

// Media query support
typedef struct MediaQuery {
    char* media_type;           // "screen", "print", etc.
    char* conditions;           // Media query conditions
    float min_width;            // Minimum width
    float max_width;            // Maximum width
    float min_height;           // Minimum height
    float max_height;           // Maximum height
    bool color;                 // Color capability
    bool monochrome;            // Monochrome capability
} MediaQuery;

MediaQuery* parse_media_query(const char* query_text);
bool evaluate_media_query(MediaQuery* query, float page_width, float page_height, bool color_capable);
void media_query_destroy(MediaQuery* query);

// Advanced selectors
bool matches_not_pseudo(StyleSelector* selector, DocNode* node);
bool matches_has_pseudo(StyleSelector* selector, DocNode* node);
bool matches_where_pseudo(StyleSelector* selector, DocNode* node);
bool matches_is_pseudo(StyleSelector* selector, DocNode* node);

#endif // STYLESHEET_H
