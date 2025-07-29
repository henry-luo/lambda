#ifndef LAMBDA_SERIALIZER_H
#define LAMBDA_SERIALIZER_H

#include "../view/view_tree.h"
#include "../../lambda/lambda.h"
#include "../../lib/strbuf.h"
#include <stdbool.h>

// Forward declarations
typedef struct LambdaSerializer LambdaSerializer;
typedef struct SerializationOptions SerializationOptions;
typedef struct MarkdownSerializer MarkdownSerializer;
typedef struct MarkdownSerializationOptions MarkdownSerializationOptions;

// Lambda serialization options
struct SerializationOptions {
    bool pretty_print;          // Pretty print output
    int indent_size;            // Indentation size (2 default)
    bool include_metadata;      // Include metadata
    bool include_positioning;   // Include positioning info
    bool include_styling;       // Include style information
    bool include_source_refs;   // Include source references
    
    // Content options
    bool serialize_text_runs;   // Serialize individual text runs
    bool serialize_glyphs;      // Serialize individual glyphs
    bool merge_adjacent_text;   // Merge adjacent text runs
    
    // Math options
    bool expand_math_elements;  // Expand math element structure
    bool include_math_metrics;  // Include mathematical metrics
    
    // Geometric options
    bool include_geometry;      // Include geometric elements
    bool simplify_paths;        // Simplify geometric paths
};

// Lambda serializer structure
struct LambdaSerializer {
    Context* lambda_context;    // Lambda context
    SerializationOptions* options; // Serialization options
    
    // State
    int current_indent;         // Current indentation level
    StrBuf* output_buffer;      // Output buffer
    
    // Statistics
    int nodes_serialized;       // Number of nodes serialized
    int warnings_generated;     // Number of warnings generated
};

// Lambda serializer creation and destruction
LambdaSerializer* lambda_serializer_create(Context* ctx, SerializationOptions* options);
void lambda_serializer_destroy(LambdaSerializer* serializer);

// Serialization options management
SerializationOptions* serialization_options_create_default(void);
void serialization_options_destroy(SerializationOptions* options);

// Main serialization functions
Item serialize_view_tree_to_lambda(LambdaSerializer* serializer, ViewTree* tree);
Item serialize_view_node_to_lambda(LambdaSerializer* serializer, ViewNode* node);
Item serialize_view_page_to_lambda(LambdaSerializer* serializer, ViewPage* page);

// Specialized serialization functions
Item serialize_view_text_run_to_lambda(LambdaSerializer* serializer, ViewTextRun* text_run);
Item serialize_view_math_element_to_lambda(LambdaSerializer* serializer, ViewMathElement* math_elem);
Item serialize_view_geometry_to_lambda(LambdaSerializer* serializer, ViewGeometry* geometry);
Item serialize_view_image_to_lambda(LambdaSerializer* serializer, ViewImage* image);
Item serialize_view_group_to_lambda(LambdaSerializer* serializer, ViewGroup* group);

// Style and geometry serialization
Item serialize_view_style_to_lambda(LambdaSerializer* serializer, ViewStyle* style);
Item serialize_view_rect_to_lambda(LambdaSerializer* serializer, ViewRect rect);
Item serialize_view_point_to_lambda(LambdaSerializer* serializer, ViewPoint point);
Item serialize_view_transform_to_lambda(LambdaSerializer* serializer, ViewTransform transform);
Item serialize_view_color_to_lambda(LambdaSerializer* serializer, struct ViewColor color);

// Deserialization functions (Lambda element tree -> View tree)
ViewTree* deserialize_lambda_to_view_tree(Context* ctx, Item lambda_tree);
ViewNode* deserialize_lambda_to_view_node(Context* ctx, Item lambda_node);
ViewPage* deserialize_lambda_to_view_page(Context* ctx, Item lambda_page);

// Utility functions
Item create_lambda_attribute_map(LambdaSerializer* serializer, ...); // NULL-terminated pairs
void add_lambda_attribute(Context* ctx, Item element, const char* name, Item value);
void add_lambda_string_attribute(Context* ctx, Item element, const char* name, const char* value);
void add_lambda_number_attribute(Context* ctx, Item element, const char* name, double value);
void add_lambda_bool_attribute(Context* ctx, Item element, const char* name, bool value);

// Markdown serialization
typedef enum {
    MARKDOWN_FLAVOR_COMMONMARK,
    MARKDOWN_FLAVOR_GITHUB,
    MARKDOWN_FLAVOR_PANDOC,
    MARKDOWN_FLAVOR_MULTIMARKDOWN
} MarkdownFlavor;

struct MarkdownSerializationOptions {
    MarkdownFlavor flavor;      // Markdown flavor
    bool preserve_formatting;   // Preserve original formatting
    bool include_math;          // Include math expressions
    bool use_tables;            // Use table syntax
    bool use_strikethrough;     // Use strikethrough syntax
    bool use_task_lists;        // Use task list syntax
    
    // Math rendering
    bool inline_math_dollars;   // Use $...$ for inline math
    bool display_math_dollars;  // Use $$...$$ for display math
    bool use_latex_commands;    // Use LaTeX commands
    
    // Formatting
    int line_width;             // Maximum line width (0 for no limit)
    bool hard_wrap;             // Use hard line wrapping
    char* line_ending;          // Line ending ("\n" default)
};

struct MarkdownSerializer {
    MarkdownSerializationOptions* options; // Serialization options
    StrBuf* output_buffer;      // Output buffer
    
    // State
    int current_list_depth;     // Current list nesting depth
    int current_quote_depth;    // Current blockquote nesting depth
    bool in_table;              // Currently in table
    bool in_code_block;         // Currently in code block
    
    // Statistics
    int nodes_processed;        // Number of nodes processed
    int warnings;               // Number of warnings
};

// Markdown serializer creation and destruction
MarkdownSerializer* markdown_serializer_create(MarkdownSerializationOptions* options);
void markdown_serializer_destroy(MarkdownSerializer* serializer);

// Markdown serialization options management
MarkdownSerializationOptions* markdown_serialization_options_create_default(void);
void markdown_serialization_options_destroy(MarkdownSerializationOptions* options);

// Main markdown serialization functions
StrBuf* serialize_view_tree_to_markdown(MarkdownSerializer* serializer, ViewTree* tree);
bool serialize_view_node_to_markdown(MarkdownSerializer* serializer, ViewNode* node);

// Specialized markdown serialization
bool serialize_markdown_heading(MarkdownSerializer* serializer, ViewNode* node);
bool serialize_markdown_paragraph(MarkdownSerializer* serializer, ViewNode* node);
bool serialize_markdown_list(MarkdownSerializer* serializer, ViewNode* node);
bool serialize_markdown_blockquote(MarkdownSerializer* serializer, ViewNode* node);
bool serialize_markdown_code_block(MarkdownSerializer* serializer, ViewNode* node);
bool serialize_markdown_table(MarkdownSerializer* serializer, ViewNode* node);
bool serialize_markdown_math(MarkdownSerializer* serializer, ViewNode* node);
bool serialize_markdown_text(MarkdownSerializer* serializer, ViewNode* node);

// Utility functions for markdown
void markdown_write_line(MarkdownSerializer* serializer, const char* text);
void markdown_write_text(MarkdownSerializer* serializer, const char* text);
void markdown_write_escaped_text(MarkdownSerializer* serializer, const char* text);
void markdown_write_indented_text(MarkdownSerializer* serializer, const char* text, int indent);
char* markdown_escape_text(const char* text);
char* markdown_format_math(const char* math_text, bool is_display);

// Debug and validation functions
void validate_lambda_serialization(ViewTree* original_tree, Item serialized);
void print_serialization_stats(LambdaSerializer* serializer);
char* view_tree_to_debug_string(ViewTree* tree);

// Example Lambda element tree structure:
/*
<view-tree 
    title:"My Document" 
    pages:3
    creator:"Lambda Typesetting System"
    creation-date:"2025-07-29T12:00:00Z">
  
  <page number:1 width:595.276 height:841.89>
    <block x:72 y:72 width:451.276 height:20 role:"heading" level:1>
      <text-run font:"Times-Bold" size:16 color:[0,0,0,1]>
        "Introduction"
      </text-run>
    </block>
    
    <block x:72 y:110 width:451.276 height:60 role:"paragraph">
      <text-run font:"Times-Roman" size:12 color:[0,0,0,1]>
        "This is the first paragraph of the document. It contains "
      </text-run>
      <text-run font:"Times-Italic" size:12 color:[0,0,0,1]>
        "emphasized text"
      </text-run>
      <text-run font:"Times-Roman" size:12 color:[0,0,0,1]>
        " and continues with normal text."
      </text-run>
    </block>
    
    <math-block x:72 y:190 width:451.276 height:40 style:"display">
      <math-fraction axis-height:6.8>
        <math-numerator width:12 height:14>
          <math-symbol class:"variable" font:"STIX-Math">x</math-symbol>
        </math-numerator>
        <math-denominator width:12 height:14>
          <math-number font:"STIX-Math">2</math-number>
        </math-denominator>
      </math-fraction>
    </math-block>
    
    <geometry type:"line" x1:72 y1:250 x2:523.276 y2:250 
              stroke-width:1 color:[0,0,0,1]/>
  </page>
  
  <page number:2 width:595.276 height:841.89>
    <!-- Additional pages... -->
  </page>
</view-tree>
*/

#endif // LAMBDA_SERIALIZER_H
