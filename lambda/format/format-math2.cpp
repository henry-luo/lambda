// format-math2.cpp - Formatter for new MathNode-based math trees
//
// This formatter converts MathNode trees (Map-based) to various output formats.
// MathNode trees are produced by the tree-sitter-based math parser (input-math2.cpp).

#include "format.h"
#include "../lambda-data.hpp"
#include "../math_node.hpp"
#include "../mark_reader.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/log.h"
#include <string.h>
#include <ctype.h>

using namespace lambda;

// ============================================================================
// Forward declarations
// ============================================================================

static void format_node_latex(StringBuf* sb, Item node, int depth);
static void format_node_typst(StringBuf* sb, Item node, int depth);
static void format_node_ascii(StringBuf* sb, Item node, int depth);
static void format_node_mathml(StringBuf* sb, Item node, int depth);

// ============================================================================
// Helper functions
// ============================================================================

// Get string value from a MathNode field
static const char* get_field_string(Map* map, const char* field_name) {
    if (!map) return nullptr;
    ConstItem val = map->get(field_name);
    if (val.item == ItemNull.item) return nullptr;
    
    TypeId type = val.type_id();
    Item val_item = *(Item*)&val;
    
    if (type == LMD_TYPE_STRING) {
        String* str = val_item.get_string();
        return str ? str->chars : nullptr;
    }
    if (type == LMD_TYPE_SYMBOL) {
        String* str = val_item.get_symbol();
        return str ? str->chars : nullptr;
    }
    return nullptr;
}

// Get an Item field from a MathNode
static Item get_field_item(Map* map, const char* field_name) {
    if (!map) return ItemNull;
    ConstItem val = map->get(field_name);
    return *(Item*)&val;
}

// Get the node type string from a MathNode
static const char* get_node_type_string(Map* map) {
    return get_field_string(map, "node");
}

// Check if an Item is a MathNode (Map with "node" field)
static bool is_math_node(Item item) {
    if (get_type_id(item) != LMD_TYPE_MAP) return false;
    Map* map = item.map;
    ConstItem node_field = map->get("node");
    return node_field.item != ItemNull.item;
}

// ============================================================================
// LaTeX formatting
// ============================================================================

static void format_symbol_latex(StringBuf* sb, Map* map) {
    const char* value = get_field_string(map, "value");
    if (value) {
        stringbuf_append_str(sb, value);
    }
}

static void format_number_latex(StringBuf* sb, Map* map) {
    const char* value = get_field_string(map, "value");
    if (value) {
        stringbuf_append_str(sb, value);
    }
}

static void format_command_latex(StringBuf* sb, Map* map) {
    const char* cmd = get_field_string(map, "cmd");
    if (cmd) {
        stringbuf_append_str(sb, cmd);
    }
}

// Check if an atom type needs spacing before/after
static bool needs_space_before(const char* atom_type) {
    if (!atom_type) return false;
    // Relations and binary operators need space before
    return strcmp(atom_type, "rel") == 0 || 
           strcmp(atom_type, "bin") == 0;
}

static bool needs_space_after(const char* atom_type) {
    if (!atom_type) return false;
    // Relations and binary operators need space after
    return strcmp(atom_type, "rel") == 0 || 
           strcmp(atom_type, "bin") == 0;
}

// Get atom type from a MathNode item
static const char* get_item_atom_type(Item item) {
    if (get_type_id(item) != LMD_TYPE_MAP) return nullptr;
    Map* map = item.map;
    return get_field_string(map, "atom");
}

// Check if item is a space command (like \quad, \,, \; etc.)
static bool is_space_command(Item item) {
    if (get_type_id(item) != LMD_TYPE_MAP) return false;
    Map* map = item.map;
    const char* node_type = get_node_type_string(map);
    return node_type && strcmp(node_type, "space") == 0;
}

// Check if item is a command (like \alpha, \sin, etc.)
static bool is_command_node(Item item) {
    if (get_type_id(item) != LMD_TYPE_MAP) return false;
    Map* map = item.map;
    const char* node_type = get_node_type_string(map);
    return node_type && strcmp(node_type, "command") == 0;
}

// Check if a command string ends with a letter (needs space before letter arg)
static bool command_ends_with_letter(const char* cmd) {
    if (!cmd) return false;
    size_t len = strlen(cmd);
    if (len == 0) return false;
    char last = cmd[len - 1];
    return (last >= 'a' && last <= 'z') || (last >= 'A' && last <= 'Z');
}

// Check if an item starts with a letter when formatted
static bool item_starts_with_letter(Item item) {
    if (get_type_id(item) != LMD_TYPE_MAP) return false;
    Map* map = item.map;
    const char* node_type = get_node_type_string(map);
    if (!node_type) return false;
    
    // Symbols and numbers: check their value
    if (strcmp(node_type, "symbol") == 0 || strcmp(node_type, "number") == 0) {
        const char* value = get_field_string(map, "value");
        if (value && strlen(value) > 0) {
            char first = value[0];
            return (first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z');
        }
    }
    // Commands start with backslash, not letter
    // Rows, groups, etc. - need to check recursively but for safety return false
    return false;
}

// Get the cmd field from a space or command node
static const char* get_node_cmd(Item item) {
    if (get_type_id(item) != LMD_TYPE_MAP) return nullptr;
    Map* map = item.map;
    return get_field_string(map, "cmd");
}

// Get the value of a symbol node
static const char* get_symbol_value(Item item) {
    if (get_type_id(item) != LMD_TYPE_MAP) return nullptr;
    Map* map = item.map;
    const char* node_type = get_node_type_string(map);
    if (!node_type || strcmp(node_type, "symbol") != 0) return nullptr;
    return get_field_string(map, "value");
}

// Check if a bin operator at given position is actually unary (at start of expression)
static bool is_unary_position(int position) {
    // At start of row = unary
    return position == 0;
}

static void format_row_latex(StringBuf* sb, Map* map, int depth) {
    Item items = get_field_item(map, "items");
    if (items.item == ItemNull.item) return;
    
    if (get_type_id(items) != LMD_TYPE_LIST) return;
    List* list = items.list;
    
    for (int i = 0; i < list->length; i++) {
        Item current = list->items[i];
        const char* atom_type = get_item_atom_type(current);
        
        // Check if this is a unary operator (at start of expression)
        bool is_unary = is_unary_position(i) && atom_type && strcmp(atom_type, "bin") == 0;
        
        // Add space before relation/binary operators (but not unary)
        if (i > 0 && !is_unary && needs_space_before(atom_type)) {
            stringbuf_append_char(sb, ' ');
        }
        
        format_node_latex(sb, current, depth + 1);
        
        // Add space after relation/binary operators (but not unary)
        if (!is_unary && needs_space_after(atom_type) && i < list->length - 1) {
            stringbuf_append_char(sb, ' ');
        }
        // Add space after commands that end with letter if followed by letter
        else if (i < list->length - 1) {
            bool is_space_or_cmd = is_space_command(current) || is_command_node(current);
            if (is_space_or_cmd) {
                const char* cmd = get_node_cmd(current);
                if (command_ends_with_letter(cmd) && item_starts_with_letter(list->items[i+1])) {
                    stringbuf_append_char(sb, ' ');
                }
            }
        }
    }
}

static void format_group_latex(StringBuf* sb, Map* map, int depth) {
    Item content = get_field_item(map, "content");
    if (content.item != ItemNull.item) {
        stringbuf_append_char(sb, '{');
        format_node_latex(sb, content, depth + 1);
        stringbuf_append_char(sb, '}');
    }
}

// Check if an item is a single character that doesn't need braces in sub/sup
static bool is_simple_script_content(Item item) {
    if (get_type_id(item) != LMD_TYPE_MAP) return false;
    Map* map = item.map;
    const char* node_type = get_node_type_string(map);
    if (!node_type) return false;
    
    // Check if it's a single symbol or number
    if (strcmp(node_type, "symbol") == 0 || strcmp(node_type, "number") == 0) {
        const char* value = get_field_string(map, "value");
        // Single character values don't need braces
        if (value && strlen(value) == 1) {
            return true;
        }
    }
    return false;
}

static void format_subsup_latex(StringBuf* sb, Map* map, int depth) {
    Item base = get_field_item(map, "base");
    Item sub = get_field_item(map, "sub");
    Item sup = get_field_item(map, "sup");
    
    // Format base
    if (base.item != ItemNull.item) {
        format_node_latex(sb, base, depth + 1);
    }
    
    // Format subscript
    if (sub.item != ItemNull.item) {
        stringbuf_append_char(sb, '_');
        if (is_simple_script_content(sub)) {
            format_node_latex(sb, sub, depth + 1);
        } else {
            stringbuf_append_char(sb, '{');
            format_node_latex(sb, sub, depth + 1);
            stringbuf_append_char(sb, '}');
        }
    }
    
    // Format superscript
    if (sup.item != ItemNull.item) {
        stringbuf_append_char(sb, '^');
        if (is_simple_script_content(sup)) {
            format_node_latex(sb, sup, depth + 1);
        } else {
            stringbuf_append_char(sb, '{');
            format_node_latex(sb, sup, depth + 1);
            stringbuf_append_char(sb, '}');
        }
    }
}

static void format_fraction_latex(StringBuf* sb, Map* map, int depth) {
    const char* cmd = get_field_string(map, "cmd");
    Item numer = get_field_item(map, "numer");
    Item denom = get_field_item(map, "denom");
    
    // Use the command from the node (dfrac, tfrac, etc.) or default to \frac
    if (cmd && cmd[0] == '\\') {
        stringbuf_append_str(sb, cmd);
    } else {
        stringbuf_append_str(sb, "\\frac");
    }
    
    stringbuf_append_char(sb, '{');
    if (numer.item != ItemNull.item) {
        format_node_latex(sb, numer, depth + 1);
    }
    stringbuf_append_char(sb, '}');
    
    stringbuf_append_char(sb, '{');
    if (denom.item != ItemNull.item) {
        format_node_latex(sb, denom, depth + 1);
    }
    stringbuf_append_char(sb, '}');
}

static void format_binomial_latex(StringBuf* sb, Map* map, int depth) {
    const char* cmd = get_field_string(map, "cmd");
    Item top = get_field_item(map, "top");
    Item bottom = get_field_item(map, "bottom");
    
    if (cmd && cmd[0] == '\\') {
        stringbuf_append_str(sb, cmd);
    } else {
        stringbuf_append_str(sb, "\\binom");
    }
    
    stringbuf_append_char(sb, '{');
    if (top.item != ItemNull.item) {
        format_node_latex(sb, top, depth + 1);
    }
    stringbuf_append_char(sb, '}');
    
    stringbuf_append_char(sb, '{');
    if (bottom.item != ItemNull.item) {
        format_node_latex(sb, bottom, depth + 1);
    }
    stringbuf_append_char(sb, '}');
}

static void format_radical_latex(StringBuf* sb, Map* map, int depth) {
    Item radicand = get_field_item(map, "radicand");
    Item index = get_field_item(map, "index");
    
    stringbuf_append_str(sb, "\\sqrt");
    
    // Optional index [n]
    if (index.item != ItemNull.item) {
        stringbuf_append_char(sb, '[');
        format_node_latex(sb, index, depth + 1);
        stringbuf_append_char(sb, ']');
    }
    
    stringbuf_append_char(sb, '{');
    if (radicand.item != ItemNull.item) {
        format_node_latex(sb, radicand, depth + 1);
    }
    stringbuf_append_char(sb, '}');
}

static void format_delimiter_latex(StringBuf* sb, Map* map, int depth) {
    const char* left = get_field_string(map, "left");
    const char* right = get_field_string(map, "right");
    Item content = get_field_item(map, "content");
    
    stringbuf_append_str(sb, "\\left");
    if (left) {
        stringbuf_append_str(sb, left);
    } else {
        stringbuf_append_char(sb, '(');
    }
    
    if (content.item != ItemNull.item) {
        format_node_latex(sb, content, depth + 1);
    }
    
    stringbuf_append_str(sb, "\\right");
    if (right) {
        stringbuf_append_str(sb, right);
    } else {
        stringbuf_append_char(sb, ')');
    }
}

static void format_accent_latex(StringBuf* sb, Map* map, int depth) {
    const char* cmd = get_field_string(map, "cmd");
    Item base = get_field_item(map, "base");
    
    if (cmd) {
        stringbuf_append_str(sb, cmd);
    } else {
        stringbuf_append_str(sb, "\\hat");
    }
    
    stringbuf_append_char(sb, '{');
    if (base.item != ItemNull.item) {
        format_node_latex(sb, base, depth + 1);
    }
    stringbuf_append_char(sb, '}');
}

static void format_bigop_latex(StringBuf* sb, Map* map, int depth) {
    const char* op = get_field_string(map, "op");
    Item lower = get_field_item(map, "lower");
    Item upper = get_field_item(map, "upper");
    
    if (op) {
        stringbuf_append_str(sb, op);
    }
    
    // Format limits
    if (lower.item != ItemNull.item) {
        stringbuf_append_str(sb, "_{");
        format_node_latex(sb, lower, depth + 1);
        stringbuf_append_char(sb, '}');
    }
    
    if (upper.item != ItemNull.item) {
        stringbuf_append_str(sb, "^{");
        format_node_latex(sb, upper, depth + 1);
        stringbuf_append_char(sb, '}');
    }
}

static void format_text_latex(StringBuf* sb, Map* map, int /* depth */) {
    const char* cmd = get_field_string(map, "cmd");
    const char* content = get_field_string(map, "content");
    
    if (cmd) {
        stringbuf_append_str(sb, cmd);
    } else {
        stringbuf_append_str(sb, "\\text");
    }
    
    stringbuf_append_char(sb, '{');
    if (content) {
        stringbuf_append_str(sb, content);
    }
    stringbuf_append_char(sb, '}');
}

static void format_style_latex(StringBuf* sb, Map* map, int depth) {
    const char* cmd = get_field_string(map, "cmd");
    Item content = get_field_item(map, "content");
    
    if (cmd) {
        stringbuf_append_str(sb, cmd);
    }
    
    if (content.item != ItemNull.item) {
        stringbuf_append_char(sb, '{');
        format_node_latex(sb, content, depth + 1);
        stringbuf_append_char(sb, '}');
    }
}

static void format_environment_latex(StringBuf* sb, Map* map, int depth) {
    const char* env_name = get_field_string(map, "name");
    if (!env_name) env_name = "matrix";
    
    // Output \begin{name}
    stringbuf_append_str(sb, "\\begin{");
    stringbuf_append_str(sb, env_name);
    stringbuf_append_str(sb, "}");
    
    // Get rows - this is a row node containing the rows as items
    Item rows_node = get_field_item(map, "rows");
    
    // Check if it's a row node (Map with "node":"row" and "items")
    if (rows_node.item != ItemNull.item && get_type_id(rows_node) == LMD_TYPE_MAP) {
        Map* rows_map = rows_node.map;
        Item rows_items = get_field_item(rows_map, "items");
        
        if (rows_items.item != ItemNull.item && get_type_id(rows_items) == LMD_TYPE_LIST) {
            List* rows_list = rows_items.list;
            
            for (int row_idx = 0; row_idx < rows_list->length; row_idx++) {
                if (row_idx > 0) {
                    stringbuf_append_str(sb, " \\\\ ");
                } else {
                    stringbuf_append_char(sb, ' ');
                }
                
                // Each row is also a row node containing cells as items
                Item row = rows_list->items[row_idx];
                if (get_type_id(row) == LMD_TYPE_MAP) {
                    Map* row_map = row.map;
                    Item cells = get_field_item(row_map, "items");
                    
                    if (cells.item != ItemNull.item && get_type_id(cells) == LMD_TYPE_LIST) {
                        List* cells_list = cells.list;
                        
                        for (int cell_idx = 0; cell_idx < cells_list->length; cell_idx++) {
                            if (cell_idx > 0) {
                                stringbuf_append_str(sb, " & ");
                            }
                            
                            Item cell = cells_list->items[cell_idx];
                            // Cell is a row node containing items
                            if (cell.item != ItemNull.item) {
                                format_node_latex(sb, cell, depth + 1);
                            }
                        }
                    }
                } else {
                    // Direct item
                    format_node_latex(sb, row, depth + 1);
                }
            }
            stringbuf_append_char(sb, ' ');
        }
    }
    
    // Output \end{name}
    stringbuf_append_str(sb, "\\end{");
    stringbuf_append_str(sb, env_name);
    stringbuf_append_str(sb, "}");
}

static void format_space_latex(StringBuf* sb, Map* map, int /* depth */) {
    const char* cmd = get_field_string(map, "cmd");
    if (cmd) {
        stringbuf_append_str(sb, cmd);
    }
}

static void format_node_latex(StringBuf* sb, Item node, int depth) {
    if (node.item == ItemNull.item) return;
    
    // Prevent infinite recursion
    if (depth > 100) {
        log_error("format_node_latex: max depth exceeded");
        return;
    }
    
    TypeId type = get_type_id(node);
    
    // Handle non-Map items
    if (type == LMD_TYPE_STRING) {
        String* str = node.get_string();
        if (str) stringbuf_append_str(sb, str->chars);
        return;
    }
    if (type == LMD_TYPE_SYMBOL) {
        String* str = node.get_symbol();
        if (str) stringbuf_append_str(sb, str->chars);
        return;
    }
    if (type == LMD_TYPE_INT) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", node.int_val);
        stringbuf_append_str(sb, buf);
        return;
    }
    
    // Must be a Map (MathNode)
    if (type != LMD_TYPE_MAP) {
        log_debug("format_node_latex: unexpected type %d", type);
        return;
    }
    
    Map* map = node.map;
    const char* node_type = get_node_type_string(map);
    
    if (!node_type) {
        log_debug("format_node_latex: missing node type");
        return;
    }
    
    // Dispatch based on node type
    if (strcmp(node_type, "symbol") == 0) {
        format_symbol_latex(sb, map);
    }
    else if (strcmp(node_type, "number") == 0) {
        format_number_latex(sb, map);
    }
    else if (strcmp(node_type, "command") == 0) {
        format_command_latex(sb, map);
    }
    else if (strcmp(node_type, "row") == 0) {
        format_row_latex(sb, map, depth);
    }
    else if (strcmp(node_type, "group") == 0) {
        format_group_latex(sb, map, depth);
    }
    else if (strcmp(node_type, "subsup") == 0) {
        format_subsup_latex(sb, map, depth);
    }
    else if (strcmp(node_type, "frac") == 0) {
        format_fraction_latex(sb, map, depth);
    }
    else if (strcmp(node_type, "binom") == 0) {
        format_binomial_latex(sb, map, depth);
    }
    else if (strcmp(node_type, "radical") == 0) {
        format_radical_latex(sb, map, depth);
    }
    else if (strcmp(node_type, "delimiter") == 0) {
        format_delimiter_latex(sb, map, depth);
    }
    else if (strcmp(node_type, "accent") == 0) {
        format_accent_latex(sb, map, depth);
    }
    else if (strcmp(node_type, "bigop") == 0) {
        format_bigop_latex(sb, map, depth);
    }
    else if (strcmp(node_type, "text") == 0) {
        format_text_latex(sb, map, depth);
    }
    else if (strcmp(node_type, "style") == 0) {
        format_style_latex(sb, map, depth);
    }
    else if (strcmp(node_type, "space") == 0) {
        format_space_latex(sb, map, depth);
    }
    else if (strcmp(node_type, "environment") == 0) {
        format_environment_latex(sb, map, depth);
    }
    else if (strcmp(node_type, "error") == 0) {
        const char* msg = get_field_string(map, "message");
        stringbuf_append_str(sb, "\\text{Error: ");
        if (msg) stringbuf_append_str(sb, msg);
        stringbuf_append_char(sb, '}');
    }
    else {
        log_debug("format_node_latex: unknown node type '%s'", node_type);
    }
}

// ============================================================================
// Public API
// ============================================================================

// Format MathNode tree to LaTeX
String* format_math2_latex(Pool* pool, Item root) {
    if (root.item == ItemNull.item) return nullptr;
    
    // Check if this is a new-style MathNode (Map-based)
    if (!is_math_node(root)) {
        log_debug("format_math2_latex: not a MathNode, falling back");
        return nullptr;
    }
    
    StringBuf* sb = stringbuf_new(pool);
    if (!sb) return nullptr;
    
    format_node_latex(sb, root, 0);
    
    // Create result string
    String* result = create_string(pool, sb->str->chars);
    stringbuf_free(sb);
    
    return result;
}

// Format MathNode tree to Typst (stub for now)
String* format_math2_typst(Pool* pool, Item root) {
    (void)root;
    // TODO: Implement Typst formatting
    return create_string(pool, "/* Typst format not implemented */");
}

// Format MathNode tree to ASCII (stub for now)
String* format_math2_ascii(Pool* pool, Item root) {
    (void)root;
    // TODO: Implement ASCII formatting
    return create_string(pool, "/* ASCII format not implemented */");
}

// Format MathNode tree to MathML (stub for now)
String* format_math2_mathml(Pool* pool, Item root) {
    (void)root;
    // TODO: Implement MathML formatting
    return create_string(pool, "<!-- MathML format not implemented -->");
}

// Check if an item is a new-style MathNode
bool is_math_node_item(Item item) {
    return is_math_node(item);
}
