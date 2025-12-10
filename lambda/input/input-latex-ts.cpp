#include "input.hpp"
#include "../mark_builder.hpp"
#include "../lambda.h"  // For it2s() function
#include <tree_sitter/api.h>
#include "../ts-enum.h"
#include "../../lib/log.h"
#include "../../lib/strbuf.h"
// Tree-sitter LaTeX parser
extern "C" {
    const TSLanguage *tree_sitter_latex(void);
}
#include "input-context.hpp"
#include "source_tracker.hpp"
#include <unordered_map>
#include <string>
#include <cstring>

using namespace lambda;

// Tree-sitter language declaration
extern "C" {
    const TSLanguage* tree_sitter_latex(void);
}

// Grammar-based node type classification
// Determines whether a tree-sitter node type should become Element or Symbol
enum NodeCategory {
    NODE_CONTAINER,   // Has children -> Element
    NODE_LEAF,        // No children possible -> Symbol or String
    NODE_TEXT         // Text content -> String
};

// Grammar analysis: which node types can have children?
static const std::unordered_map<std::string, NodeCategory> node_classification = {
    // Container nodes (can have children in grammar)
    {"source_file", NODE_CONTAINER},
    {"generic_command", NODE_CONTAINER},
    {"generic_environment", NODE_CONTAINER},
    {"curly_group", NODE_CONTAINER},
    {"curly_group_text", NODE_CONTAINER},
    {"brack_group", NODE_CONTAINER},
    {"brack_group_text", NODE_CONTAINER},
    {"math_environment", NODE_CONTAINER},
    {"section", NODE_CONTAINER},
    {"subsection", NODE_CONTAINER},
    {"subsubsection", NODE_CONTAINER},
    {"chapter", NODE_CONTAINER},
    {"part", NODE_CONTAINER},
    {"paragraph", NODE_CONTAINER},
    {"subparagraph", NODE_CONTAINER},
    {"itemize", NODE_CONTAINER},
    {"enumerate", NODE_CONTAINER},
    {"description", NODE_CONTAINER},
    {"text", NODE_TEXT},  // Text content - return as string, not element
    {"enum_item", NODE_CONTAINER},
    {"diacritic_command", NODE_CONTAINER},  // Can have optional base
    {"linebreak_command", NODE_CONTAINER},  // Can have optional spacing
    {"verb_command", NODE_CONTAINER},       // Has content field
    {"begin", NODE_CONTAINER},
    {"end", NODE_CONTAINER},
    {"comment_environment", NODE_CONTAINER},
    {"verbatim_environment", NODE_CONTAINER},
    
    // Leaf nodes (cannot have children in grammar)
    {"command_name", NODE_LEAF},
    {"escape_sequence", NODE_LEAF},
    {"spacing_command", NODE_LEAF},
    {"symbol_command", NODE_LEAF},
    {"line_comment", NODE_LEAF},
    {"paragraph_break", NODE_LEAF},  // Blank line - creates paragraph boundary
    {"space", NODE_LEAF},             // Whitespace - collapses to single space
    {"operator", NODE_LEAF},
    {"delimiter", NODE_LEAF},
    {"value_literal", NODE_LEAF},
    {"label", NODE_LEAF},
    {"path", NODE_LEAF},
    {"uri", NODE_LEAF},
    {"{", NODE_LEAF},                 // Brace delimiters - skip these
    {"}", NODE_LEAF},
    {"[", NODE_LEAF},
    {"]", NODE_LEAF},
    
    // Text nodes (raw text content)
    {"word", NODE_TEXT},
    {"comment", NODE_TEXT},
};

static NodeCategory classify_node_type(const char* node_type) {
    auto it = node_classification.find(node_type);
    if (it != node_classification.end()) {
        return it->second;
    }
    
    // Default: assume container (safer, can hold children)
    log_debug("Unknown node type classification: %s, defaulting to NODE_CONTAINER", node_type);
    return NODE_CONTAINER;
}

// Forward declarations
static Item convert_latex_node(InputContext& ctx, TSNode node, const char* source);
static Item convert_command(InputContext& ctx, TSNode node, const char* source);
static Item convert_environment(InputContext& ctx, TSNode node, const char* source);
static Item convert_text_node(InputContext& ctx, TSNode node, const char* source);
static Item convert_leaf_node(InputContext& ctx, TSNode node, const char* source);
static String* extract_text(InputContext& ctx, TSNode node, const char* source);
static String* normalize_latex_text(InputContext& ctx, const char* text, size_t len);

// Extract text from a tree-sitter node
static String* extract_text(InputContext& ctx, TSNode node, const char* source) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    size_t len = end - start;
    
    if (len == 0) {
        return ctx.builder.createString("", 0);
    }
    
    const char* text = source + start;
    return ctx.builder.createString(text, len);
}

// Normalize LaTeX whitespace according to LaTeX rules
static String* normalize_latex_text(InputContext& ctx, const char* text, size_t len) {
    // Apply LaTeX whitespace rules:
    // 1. Multiple spaces/tabs → single space
    // 2. Newlines in text → spaces
    // 3. Preserve leading/trailing space significance (for now)
    // 4. Trim trailing newlines only
    
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);
    
    bool in_whitespace = false;
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!in_whitespace) {
                stringbuf_append_char(sb, ' ');
                in_whitespace = true;
            }
        } else {
            stringbuf_append_char(sb, c);
            in_whitespace = false;
        }
    }
    
    // Trim trailing newlines/spaces
    while (sb->length > 0 && (sb->str->chars[sb->length-1] == ' ' ||
                               sb->str->chars[sb->length-1] == '\n' || 
                               sb->str->chars[sb->length-1] == '\r')) {
        sb->length--;
    }
    
    return ctx.builder.createString(sb->str->chars, sb->length);
}

// Convert leaf node to Symbol
static Item convert_leaf_node(InputContext& ctx, TSNode node, const char* source) {
    const char* node_type = ts_node_type(node);
    
    // Extract node text
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    size_t len = end - start;
    
    MarkBuilder& builder = ctx.builder;
    
    // Special mapping for spacing commands
    if (strcmp(node_type, "spacing_command") == 0) {
        // Extract command name from source: \quad -> "quad"
        const char* cmd_start = source + start;
        if (*cmd_start == '\\') cmd_start++;  // skip backslash
        
        // Find command name length
        size_t cmd_len = 0;
        while (cmd_len < len && (isalpha(cmd_start[cmd_len]) || cmd_start[cmd_len] == ',' || 
                                  cmd_start[cmd_len] == '!' || cmd_start[cmd_len] == ';' ||
                                  cmd_start[cmd_len] == ':')) {
            cmd_len++;
        }
        
        // Map special characters to names
        if (cmd_len == 1) {
            if (cmd_start[0] == ',') return {.item = y2it(builder.createSymbol("thinspace"))};
            if (cmd_start[0] == '!') return {.item = y2it(builder.createSymbol("negthinspace"))};
            if (cmd_start[0] == ';') return {.item = y2it(builder.createSymbol("thickspace"))};
            if (cmd_start[0] == ':') return {.item = y2it(builder.createSymbol("medspace"))};
        }
        
        return {.item = y2it(builder.createSymbol(cmd_start, cmd_len))};
    }
    
    // Symbol commands: \ss -> Symbol("ss")
    if (strcmp(node_type, "symbol_command") == 0) {
        const char* cmd_start = source + start;
        if (*cmd_start == '\\') cmd_start++;
        size_t cmd_len = len - 1;  // subtract backslash
        return {.item = y2it(builder.createSymbol(cmd_start, cmd_len))};
    }
    
    // Escape sequence: \$ -> String("$")
    if (strcmp(node_type, "escape_sequence") == 0) {
        // Return the escaped character as a string
        const char* cmd_start = source + start;
        if (*cmd_start == '\\' && len > 1) {
            return {.item = s2it(builder.createString(cmd_start + 1, 1))};
        }
    }
    
    // Paragraph break -> Symbol("parbreak") 
    // (blank line in LaTeX creates paragraph boundary)
    if (strcmp(node_type, "paragraph_break") == 0) {
        return {.item = y2it(builder.createSymbol("parbreak"))};
    }
    
    // Space -> String(" ")
    // (LaTeX.js rule: multiple spaces/newlines collapse to single space)
    if (strcmp(node_type, "space") == 0) {
        return {.item = s2it(builder.createString(" ", 1))};
    }
    
    // Line comment -> skip for now
    if (strcmp(node_type, "line_comment") == 0) {
        return {.item = ITEM_NULL};
    }
    
    // Delimiters ({, }, [, ]) -> skip these, they're structural only
    if (strcmp(node_type, "{") == 0 || strcmp(node_type, "}") == 0 ||
        strcmp(node_type, "[") == 0 || strcmp(node_type, "]") == 0) {
        return {.item = ITEM_NULL};
    }
    
    // Operator nodes (-, --, ---, and potentially other punctuation)
    // Extract the actual text and decide how to handle it
    if (strcmp(node_type, "operator") == 0) {
        // Get the operator text
        String* op_text = extract_text(ctx, node, source);
        if (op_text && op_text->len > 0) {
            // Check if it's a dash sequence (for ligature processing)
            if (op_text->chars[0] == '-') {
                // Return as text string for ligature processing
                return {.item = s2it(op_text)};
            }
            // For other operators, also return as text
            return {.item = s2it(op_text)};
        }
    }
    
    // Default: use node type as symbol name
    return {.item = y2it(builder.createSymbol(node_type))};
}

// Convert text node to String
static Item convert_text_node(InputContext& ctx, TSNode node, const char* source) {
    const char* node_type = ts_node_type(node);
    
    // For "word" nodes, extract and return as string
    if (strcmp(node_type, "word") == 0) {
        return {.item = s2it(extract_text(ctx, node, source))};
    }
    
    // For "text" nodes, process children (words, spaces, commands, etc.)
    if (strcmp(node_type, "text") == 0) {
        uint32_t child_count = ts_node_child_count(node);
        if (child_count == 0) {
            return {.item = ITEM_NULL};
        }
        
        // If single child, unwrap it
        if (child_count == 1) {
            return convert_latex_node(ctx, ts_node_child(node, 0), source);
        }
        
        // Multiple children - need to handle mixed content (strings, commands, symbols)
        // Strategy: Try to concatenate consecutive strings, but preserve non-string items
        MarkBuilder& builder = ctx.builder;
        std::vector<Item> items;
        StrBuf* sb = strbuf_new();  // Buffer for concatenating strings
        
        auto flush_string_buffer = [&]() {
            if (sb->length > 0) {
                items.push_back({.item = s2it(builder.createString(sb->str, sb->length))});
                strbuf_reset(sb);
            }
        };
        
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            Item child_item = convert_latex_node(ctx, child, source);
            
            if (child_item.item != ITEM_NULL) {
                TypeId type = get_type_id(child_item);
                if (type == LMD_TYPE_STRING) {
                    // Append to string buffer
                    String* str = (String*)(child_item.item & ~7ULL);
                    strbuf_append_str_n(sb, str->chars, str->len);
                } else {
                    // Non-string item (command, symbol, element) - flush buffer and add item
                    flush_string_buffer();
                    items.push_back(child_item);
                }
            }
        }
        flush_string_buffer();  // Flush any remaining string
        strbuf_free(sb);
        
        // If we have a single item, unwrap it
        if (items.size() == 1) {
            return items[0];
        }
        
        // Multiple items - return as list
        ListBuilder lb = builder.list();
        for (const Item& item : items) {
            lb.push(item);
        }
        return lb.final();
    }
    
    // Default: extract as string
    return {.item = s2it(extract_text(ctx, node, source))};
}

// Main conversion dispatcher
static Item convert_latex_node(InputContext& ctx, TSNode node, const char* source) {
    const char* node_type = ts_node_type(node);
    
    // Skip certain node types
    if (strcmp(node_type, "ERROR") == 0) {
        log_error("Parse error at position %u", ts_node_start_byte(node));
        return {.item = ITEM_NULL};
    }
    
    NodeCategory category = classify_node_type(node_type);
    
    switch (category) {
        case NODE_TEXT:
            return convert_text_node(ctx, node, source);
            
        case NODE_LEAF:
            return convert_leaf_node(ctx, node, source);
            
        case NODE_CONTAINER:
            // Special handlers for specific container types
            if (strcmp(node_type, "source_file") == 0) {
                // Root node - create latex_document element with all children
                MarkBuilder& builder = ctx.builder;
                ElementBuilder root_builder = builder.element("latex_document");
                
                uint32_t child_count = ts_node_child_count(node);
                for (uint32_t i = 0; i < child_count; i++) {
                    TSNode child = ts_node_child(node, i);
                    Item child_item = convert_latex_node(ctx, child, source);
                    if (child_item.item != ITEM_NULL) {
                        root_builder.child(child_item);
                    }
                }
                
                return root_builder.final();
            }
            
            if (strcmp(node_type, "generic_command") == 0) {
                return convert_command(ctx, node, source);
            }
            
            if (strcmp(node_type, "generic_environment") == 0) {
                return convert_environment(ctx, node, source);
            }
            
            // TODO: Add more specific handlers
            // For now, create generic element with node type as tag
            {
                MarkBuilder& builder = ctx.builder;
                ElementBuilder elem_builder = builder.element(node_type);
                
                uint32_t child_count = ts_node_child_count(node);
                for (uint32_t i = 0; i < child_count; i++) {
                    TSNode child = ts_node_child(node, i);
                    Item child_item = convert_latex_node(ctx, child, source);
                    if (child_item.item != ITEM_NULL) {
                        elem_builder.child(child_item);
                    }
                }
                
                return elem_builder.final();
            }
    }
    
    return {.item = ITEM_NULL};
}

// Convert command node
static Item convert_command(InputContext& ctx, TSNode node, const char* source) {
    // Extract command name
    TSNode cmd_name_node = ts_node_child_by_field_name(node, "command", 7);
    if (ts_node_is_null(cmd_name_node)) {
        return {.item = ITEM_NULL};
    }
    
    String* cmd_name_str = extract_text(ctx, cmd_name_node, source);
    const char* cmd_name = cmd_name_str->chars;
    
    // Skip backslash if present
    if (cmd_name[0] == '\\') {
        cmd_name++;
    }
    
    // Create element with command name as tag
    MarkBuilder& builder = ctx.builder;
    ElementBuilder cmd_elem_builder = builder.element(cmd_name);
    
    // Extract arguments (curly_group children)
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char* child_type = ts_node_type(child);
        
        // Process argument groups
        if (strcmp(child_type, "curly_group") == 0) {
            // Unwrap curly group and add its contents
            uint32_t arg_child_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < arg_child_count; j++) {
                TSNode arg_child = ts_node_child(child, j);
                const char* arg_child_type = ts_node_type(arg_child);
                
                // Skip braces
                if (strcmp(arg_child_type, "{") == 0 || strcmp(arg_child_type, "}") == 0) {
                    continue;
                }
                
                Item arg_item = convert_latex_node(ctx, arg_child, source);
                if (arg_item.item != ITEM_NULL) {
                    cmd_elem_builder.child(arg_item);
                }
            }
        }
    }
    
    return cmd_elem_builder.final();
}

// Convert environment node
static Item convert_environment(InputContext& ctx, TSNode node, const char* source) {
    // Extract environment name from \begin{name}
    TSNode begin_node = ts_node_child_by_field_name(node, "begin", 5);
    if (ts_node_is_null(begin_node)) {
        return {.item = ITEM_NULL};
    }
    
    // Get the name field from begin node
    TSNode name_node = ts_node_child_by_field_name(begin_node, "name", 4);
    if (ts_node_is_null(name_node)) {
        return {.item = ITEM_NULL};
    }
    
    // Extract environment name from curly_group_text
    // Need to get the text field from curly_group_text
    TSNode text_node = ts_node_child_by_field_name(name_node, "text", 4);
    if (ts_node_is_null(text_node)) {
        // Fallback: extract from name_node directly
        text_node = name_node;
    }
    
    String* env_name = extract_text(ctx, text_node, source);
    
    // Create element with environment name as tag
    MarkBuilder& builder = ctx.builder;
    ElementBuilder env_elem_builder = builder.element(env_name->chars);
    
    // Process environment content (between \begin and \end)
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_child(node, i);
        const char* child_type = ts_node_type(child);
        
        // Skip begin/end nodes
        if (strcmp(child_type, "begin") == 0 || strcmp(child_type, "end") == 0) {
            continue;
        }
        
        Item child_item = convert_latex_node(ctx, child, source);
        if (child_item.item != ITEM_NULL) {
            env_elem_builder.child(child_item);
        }
    }
    
    return env_elem_builder.final();
}

// Main entry point - tree-sitter LaTeX parser
extern "C" void parse_latex_ts(Input* input, const char* latex_string) {
    log_info("Tree-sitter LaTeX parser starting...");
    
    // Create tree-sitter parser
    TSParser* parser = ts_parser_new();
    if (!parser) {
        log_error("Failed to create TSParser");
        input->root = {.item = ITEM_ERROR};
        return;
    }
    
    // Set language to LaTeX
    if (!ts_parser_set_language(parser, tree_sitter_latex())) {
        log_error("Failed to set LaTeX language");
        ts_parser_delete(parser);
        input->root = {.item = ITEM_ERROR};
        return;
    }
    
    // Parse the LaTeX string
    TSTree* tree = ts_parser_parse_string(parser, NULL, latex_string, strlen(latex_string));
    if (!tree) {
        log_error("Failed to parse LaTeX string");
        ts_parser_delete(parser);
        input->root = {.item = ITEM_ERROR};
        return;
    }
    
    TSNode root_node = ts_tree_root_node(tree);
    
    // Create input context for error tracking and source location
    InputContext ctx(input, latex_string, strlen(latex_string));
    
    // Convert tree-sitter CST to Lambda tree
    Item lambda_tree = convert_latex_node(ctx, root_node, latex_string);
    input->root = lambda_tree;
    
    // Check for parse errors
    if (ts_node_has_error(root_node)) {
        log_warn("Parse tree contains errors");
    }
    
    // Cleanup
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    
    log_info("Tree-sitter LaTeX parser completed");
}
