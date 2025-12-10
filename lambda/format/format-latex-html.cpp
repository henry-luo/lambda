#include "format-latex-html.h"
#include "../mark_reader.hpp"
#include "format.h"
#include "../../lib/stringbuf.h"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include <string.h>
#include <stdlib.h>
#include <map>
#include <string>
#include <vector>

// =============================================================================
// Counter System
// =============================================================================

struct Counter {
    int value = 0;
    std::string parent;  // Parent counter name (reset when parent steps)
    std::vector<std::string> children;  // Child counters to reset when this counter steps
};

static std::map<std::string, Counter> counter_registry;

// Initialize standard LaTeX counters
static void init_counters() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    // Standard LaTeX counters with their parent relationships
    counter_registry["part"] = {0, "", {}};
    counter_registry["chapter"] = {0, "", {}};
    counter_registry["section"] = {0, "chapter", {}};
    counter_registry["subsection"] = {0, "section", {}};
    counter_registry["subsubsection"] = {0, "subsection", {}};
    counter_registry["paragraph"] = {0, "subsubsection", {}};
    counter_registry["subparagraph"] = {0, "paragraph", {}};
    counter_registry["page"] = {1, "", {}};
    counter_registry["equation"] = {0, "chapter", {}};
    counter_registry["figure"] = {0, "chapter", {}};
    counter_registry["table"] = {0, "chapter", {}};
    counter_registry["footnote"] = {0, "chapter", {}};
    counter_registry["mpfootnote"] = {0, "", {}};
    counter_registry["enumi"] = {0, "", {}};
    counter_registry["enumii"] = {0, "enumi", {}};
    counter_registry["enumiii"] = {0, "enumii", {}};
    counter_registry["enumiv"] = {0, "enumiii", {}};

    // Set up parent-child relationships
    counter_registry["chapter"].children.push_back("section");
    counter_registry["chapter"].children.push_back("equation");
    counter_registry["chapter"].children.push_back("figure");
    counter_registry["chapter"].children.push_back("table");
    counter_registry["chapter"].children.push_back("footnote");
    counter_registry["section"].children.push_back("subsection");
    counter_registry["subsection"].children.push_back("subsubsection");
    counter_registry["subsubsection"].children.push_back("paragraph");
    counter_registry["paragraph"].children.push_back("subparagraph");
    counter_registry["enumi"].children.push_back("enumii");
    counter_registry["enumii"].children.push_back("enumiii");
    counter_registry["enumiii"].children.push_back("enumiv");
}

// Create a new counter (optionally with parent)
static void new_counter(const std::string& name, const std::string& parent = "") {
    init_counters();
    Counter c;
    c.value = 0;
    c.parent = parent;
    counter_registry[name] = c;

    // Add as child to parent if specified
    if (!parent.empty() && counter_registry.count(parent)) {
        counter_registry[parent].children.push_back(name);
    }
}

// Set counter value
static void set_counter(const std::string& name, int value) {
    init_counters();
    if (counter_registry.count(name)) {
        counter_registry[name].value = value;
    }
}

// Add to counter value
static void add_to_counter(const std::string& name, int delta) {
    init_counters();
    if (counter_registry.count(name)) {
        counter_registry[name].value += delta;
    }
}

// Step counter (increment and reset children)
static void step_counter(const std::string& name);

// Reset counter and all its descendants recursively
static void reset_counter_recursive(const std::string& name) {
    init_counters();
    if (!counter_registry.count(name)) return;

    counter_registry[name].value = 0;

    // Recursively reset all children
    for (const auto& child : counter_registry[name].children) {
        reset_counter_recursive(child);
    }
}

// Step counter (increment and reset children recursively)
static void step_counter(const std::string& name) {
    init_counters();
    if (!counter_registry.count(name)) return;

    counter_registry[name].value++;

    // Reset all child counters recursively
    for (const auto& child : counter_registry[name].children) {
        reset_counter_recursive(child);
    }
}

// Get counter value
static int get_counter_value(const std::string& name) {
    init_counters();
    if (counter_registry.count(name)) {
        return counter_registry[name].value;
    }
    return 0;
}

// Format counter as arabic numerals
static std::string format_arabic(int value) {
    return std::to_string(value);
}

// Format counter as lowercase roman numerals
static std::string format_roman(int value, bool upper) {
    if (value <= 0) return "0";
    if (value > 3999) return std::to_string(value);

    static const char* ones_lower[] = {"", "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix"};
    static const char* tens_lower[] = {"", "x", "xx", "xxx", "xl", "l", "lx", "lxx", "lxxx", "xc"};
    static const char* hund_lower[] = {"", "c", "cc", "ccc", "cd", "d", "dc", "dcc", "dccc", "cm"};
    static const char* thou_lower[] = {"", "m", "mm", "mmm"};

    static const char* ones_upper[] = {"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX"};
    static const char* tens_upper[] = {"", "X", "XX", "XXX", "XL", "L", "LX", "LXX", "LXXX", "XC"};
    static const char* hund_upper[] = {"", "C", "CC", "CCC", "CD", "D", "DC", "DCC", "DCCC", "CM"};
    static const char* thou_upper[] = {"", "M", "MM", "MMM"};

    std::string result;
    if (upper) {
        result = std::string(thou_upper[value/1000]) + hund_upper[(value%1000)/100] +
                 tens_upper[(value%100)/10] + ones_upper[value%10];
    } else {
        result = std::string(thou_lower[value/1000]) + hund_lower[(value%1000)/100] +
                 tens_lower[(value%100)/10] + ones_lower[value%10];
    }
    return result;
}

// Format counter as alphabetic (a, b, c, ... z, aa, ab, ...)
static std::string format_alph(int value, bool upper) {
    if (value <= 0) return "0";

    std::string result;
    while (value > 0) {
        value--;
        char c = (value % 26) + (upper ? 'A' : 'a');
        result = c + result;
        value /= 26;
    }
    return result;
}

// =============================================================================
// Label/Reference System
// =============================================================================

// Label information struct
struct LabelInfo {
    std::string anchor_id;  // HTML anchor ID (e.g., "sec-1", "c-1")
    std::string ref_text;   // Text to display for \ref (e.g., "1", "1.1")
};

// Label registry - maps label names to their info
static std::map<std::string, LabelInfo> label_registry;
static bool label_registry_initialized = false;

// Current label info (set by \refstepcounter, sections, etc.)
static std::string current_label_anchor = "";
static std::string current_label_text = "";
static int label_counter = 0;  // Auto-generate unique anchor IDs

static void init_labels() {
    if (label_registry_initialized) return;
    label_registry.clear();
    current_label_anchor = "";
    current_label_text = "";
    label_counter = 0;
    label_registry_initialized = true;
}

// Set the current label context (called by \refstepcounter, section commands, etc.)
static void set_current_label(const std::string& anchor, const std::string& text) {
    init_labels();
    current_label_anchor = anchor;
    current_label_text = text;
}

// Register a label with its anchor and ref text
static void register_label(const std::string& name) {
    init_labels();
    LabelInfo info;
    info.anchor_id = current_label_anchor;
    info.ref_text = current_label_text;
    label_registry[name] = info;
}

// Get label info for a reference (returns empty info if not found)
static LabelInfo get_label_info(const std::string& name) {
    init_labels();
    if (label_registry.count(name)) {
        return label_registry[name];
    }
    // Return empty info for undefined labels
    LabelInfo empty;
    empty.anchor_id = "";
    empty.ref_text = "";
    return empty;
}

// Generate a unique anchor ID
static std::string generate_anchor_id(const std::string& prefix = "ref") {
    init_labels();
    label_counter++;
    return prefix + "-" + std::to_string(label_counter);
}

// =============================================================================
// Macro System
// =============================================================================

// Macro definition struct
struct MacroDefinition {
    std::string name;           // Command name (without backslash)
    int num_params;             // Number of parameters (0-9)
    std::vector<std::string> default_values;  // Default values for optional params
    Element* definition;        // The replacement text as an Element tree
    bool is_environment;        // True if defined with \newenvironment
};

// Macro registry - maps command names to their definitions
static std::map<std::string, MacroDefinition> macro_registry;
static bool macro_registry_initialized = false;

static void init_macros() {
    if (macro_registry_initialized) return;
    macro_registry.clear();
    macro_registry_initialized = true;
}

// Register a new macro
static void register_macro(const std::string& name, int num_params, Element* definition, bool is_environment = false) {
    init_macros();
    MacroDefinition macro;
    macro.name = name;
    macro.num_params = num_params;
    macro.definition = definition;
    macro.is_environment = is_environment;
    macro_registry[name] = macro;
}

// Check if a command is a user-defined macro
static bool is_macro(const std::string& name) {
    init_macros();
    return macro_registry.count(name) > 0;
}

// Get macro definition
static MacroDefinition* get_macro(const std::string& name) {
    init_macros();
    if (macro_registry.count(name)) {
        return &macro_registry[name];
    }
    return nullptr;
}

// Clone an Element tree (deep copy for macro expansion)
static Element* clone_element(Element* elem, Pool* pool);

// Helper: Replace parameter references (#1, #2, etc.) in a string
// Returns a list of Items: strings and/or elements that replace the original string
static std::vector<Item> substitute_params_in_string(const char* text, size_t len, 
                                                      const std::vector<Element*>& args, Pool* pool) {
    std::vector<Item> result;
    
    size_t i = 0;
    size_t segment_start = 0;
    
    while (i < len) {
        // Look for # followed by a digit
        if (text[i] == '#' && i + 1 < len && text[i + 1] >= '1' && text[i + 1] <= '9') {
            // Found parameter reference
            int param_num = text[i + 1] - '0';  // Convert '1'-'9' to 1-9
            
            // Add any text before the parameter reference
            if (i > segment_start) {
                size_t seg_len = i - segment_start;
                String* seg_str = (String*)pool_calloc(pool, sizeof(String) + seg_len + 1);
                seg_str->len = seg_len;
                memcpy(seg_str->chars, text + segment_start, seg_len);
                seg_str->chars[seg_len] = '\0';
                
                Item str_item;
                str_item.string_ptr = (uint64_t)seg_str;
                str_item._8_s = LMD_TYPE_STRING;
                result.push_back(str_item);
            }
            
            // Add the argument element (if it exists)
            if (param_num > 0 && param_num <= (int)args.size() && args[param_num - 1]) {
                Item arg_item;
                arg_item.element = args[param_num - 1];
                result.push_back(arg_item);
            }
            
            // Skip past #N
            i += 2;
            segment_start = i;
        } else {
            i++;
        }
    }
    
    // Add any remaining text
    if (segment_start < len) {
        size_t seg_len = len - segment_start;
        String* seg_str = (String*)pool_calloc(pool, sizeof(String) + seg_len + 1);
        seg_str->len = seg_len;
        memcpy(seg_str->chars, text + segment_start, seg_len);
        seg_str->chars[seg_len] = '\0';
        
        Item str_item;
        str_item.string_ptr = (uint64_t)seg_str;
        str_item._8_s = LMD_TYPE_STRING;
        result.push_back(str_item);
    }
    
    return result;
}

// Helper: Recursively substitute parameters in an Element tree (modifies in place)
static void substitute_params_recursive(Element* elem, const std::vector<Element*>& args, Pool* pool) {
    if (!elem || !elem->items || elem->length == 0) return;
    
    // We'll build a new items array with parameter substitutions
    std::vector<Item> new_items;
    
    for (size_t i = 0; i < elem->length; i++) {
        Item item = elem->items[i];
        TypeId type = get_type_id(item);
        
        if (type == LMD_TYPE_STRING) {
            // Check if this string contains parameter references
            String* str = item.get_string();
            if (str && str->len > 0) {
                bool has_param = false;
                for (size_t j = 0; j < str->len - 1; j++) {
                    if (str->chars[j] == '#' && str->chars[j + 1] >= '1' && str->chars[j + 1] <= '9') {
                        has_param = true;
                        break;
                    }
                }
                
                if (has_param) {
                    // Substitute parameters in this string
                    std::vector<Item> substituted = substitute_params_in_string(str->chars, str->len, args, pool);
                    new_items.insert(new_items.end(), substituted.begin(), substituted.end());
                } else {
                    // No parameters, keep as is
                    new_items.push_back(item);
                }
            } else {
                new_items.push_back(item);
            }
        } else if (type == LMD_TYPE_ELEMENT) {
            // Recursively process child elements
            substitute_params_recursive(item.element, args, pool);
            new_items.push_back(item);
        } else {
            // Other types: keep as is
            new_items.push_back(item);
        }
    }
    
    // Replace the items array if we made changes
    if (new_items.size() != elem->length) {
        elem->length = new_items.size();
        elem->capacity = new_items.size();
        elem->items = (Item*)pool_calloc(pool, new_items.size() * sizeof(Item));
        for (size_t i = 0; i < new_items.size(); i++) {
            elem->items[i] = new_items[i];
        }
    }
}

// Substitute macro parameters (#1, #2, etc.) in the definition
// This creates a new Element tree with parameters replaced by actual arguments
static Element* expand_macro_params(Element* definition, const std::vector<Element*>& args, Pool* pool) {
    if (!definition) return nullptr;
    
    // Clone the definition first
    Element* expanded = clone_element(definition, pool);
    if (!expanded) return nullptr;
    
    // Walk the cloned tree and replace parameter references (#1, #2, etc.)
    substitute_params_recursive(expanded, args, pool);
    
    return expanded;
}

// Clone an Element tree recursively
static Element* clone_element(Element* elem, Pool* pool) {
    if (!elem) return nullptr;
    
    Element* cloned = (Element*)pool_calloc(pool, sizeof(Element));
    cloned->type_id = elem->type_id;
    cloned->type = elem->type;  // Type pointer can be shared
    
    // Clone items array
    if (elem->items && elem->length > 0) {
        cloned->length = elem->length;
        cloned->capacity = elem->length;
        cloned->items = (Item*)pool_calloc(pool, elem->length * sizeof(Item));
        
        for (size_t i = 0; i < elem->length; i++) {
            Item item = elem->items[i];
            TypeId type = get_type_id(item);
            
            if (type == LMD_TYPE_ELEMENT) {
                // Recursively clone child elements
                Element* child = clone_element(item.element, pool);
                Item child_item;
                child_item.element = child;
                cloned->items[i] = child_item;
            } else if (type == LMD_TYPE_STRING) {
                // Strings need to be copied
                String* orig_str = item.get_string();
                if (orig_str) {
                    String* new_str = (String*)pool_calloc(pool, sizeof(String) + orig_str->len + 1);
                    new_str->len = orig_str->len;
                    memcpy(new_str->chars, orig_str->chars, orig_str->len + 1);
                    Item str_item;
                    str_item.string_ptr = (uint64_t)new_str;
                    str_item._8_s = LMD_TYPE_STRING;
                    cloned->items[i] = str_item;
                }
            } else {
                // For other types (numbers, etc.), copy as-is
                cloned->items[i] = item;
            }
        }
    }
    
    return cloned;
}

// =============================================================================
// Counter Formatters (continued)
// =============================================================================

// Format counter as footnote symbol
static std::string format_fnsymbol(int value) {
    // Standard footnote symbols: * † ‡ § ¶ ‖ ** †† ‡‡
    static const char* symbols[] = {
        "",           // 0
        "*",          // 1
        "\xE2\x80\xA0",  // † (2)
        "\xE2\x80\xA1",  // ‡ (3)
        "\xC2\xA7",      // § (4)
        "\xC2\xB6",      // ¶ (5)
        "\xE2\x80\x96",  // ‖ (6)
        "**",            // 7
        "\xE2\x80\xA0\xE2\x80\xA0",  // †† (8)
        "\xE2\x80\xA1\xE2\x80\xA1"   // ‡‡ (9)
    };

    if (value < 0 || value > 9) return std::to_string(value);
    return symbols[value];
}

// =============================================================================
// Counter Expression Evaluator
// =============================================================================
// Evaluates arithmetic expressions in counter arguments like:
//   3 * -(2+1)
//   3*\real{1.6} * \real{1.7} + -- 2
//   \value{c}
// Supports: +, -, *, parentheses, \real{}, \value{}
// Double negatives (-- or en-dash –) are treated as positive

// Forward declaration for recursive parsing
static double parse_expression(const char*& pos);

// Skip whitespace
static void skip_ws(const char*& pos) {
    while (*pos == ' ' || *pos == '\t') pos++;
}

// Parse a number (integer or floating point)
static double parse_number(const char*& pos) {
    skip_ws(pos);
    double result = 0;
    bool negative = false;

    if (*pos == '-') {
        negative = true;
        pos++;
        skip_ws(pos);
    } else if (*pos == '+') {
        pos++;
        skip_ws(pos);
    }

    // Check for en-dash (–, UTF-8: E2 80 93) which indicates double negative
    if ((unsigned char)*pos == 0xE2 && (unsigned char)*(pos+1) == 0x80 && (unsigned char)*(pos+2) == 0x93) {
        // En-dash after minus means double negative, skip it
        pos += 3;
        skip_ws(pos);
        negative = !negative;  // Double negative
    }

    // Parse integer part
    while (*pos >= '0' && *pos <= '9') {
        result = result * 10 + (*pos - '0');
        pos++;
    }

    // Parse fractional part
    if (*pos == '.') {
        pos++;
        double fraction = 0.1;
        while (*pos >= '0' && *pos <= '9') {
            result += (*pos - '0') * fraction;
            fraction *= 0.1;
            pos++;
        }
    }

    return negative ? -result : result;
}

// Parse a factor (number, parenthesized expression, or \real{}/\value{} command)
static double parse_factor(const char*& pos);

// Parse term (handles *, /)
static double parse_term(const char*& pos) {
    double result = parse_factor(pos);
    skip_ws(pos);

    while (*pos == '*' || *pos == '/') {
        char op = *pos++;
        double rhs = parse_factor(pos);
        if (op == '*') {
            // Truncate to integer after each multiplication (LaTeX counter semantics)
            result = (int)(result * rhs);
        }
        else if (rhs != 0) result /= rhs;
        skip_ws(pos);
    }

    return result;
}// Parse expression (handles +, -)
static double parse_expression(const char*& pos) {
    skip_ws(pos);

    // Handle leading negative/positive
    bool negative = false;
    while (*pos == '-' || *pos == '+') {
        if (*pos == '-') negative = !negative;
        pos++;
        skip_ws(pos);
        // Check for en-dash (represents -- = double negative, so flip twice = no change)
        if ((unsigned char)*pos == 0xE2 && (unsigned char)*(pos+1) == 0x80 && (unsigned char)*(pos+2) == 0x93) {
            // En-dash represents --, which is double negative, so DON'T change the sign
            // (two flips cancel out)
            pos += 3;
            skip_ws(pos);
        }
    }

    double result = parse_term(pos);
    if (negative) result = -result;

    skip_ws(pos);

    while (*pos == '+' || *pos == '-' ||
           ((unsigned char)*pos == 0xE2 && (unsigned char)*(pos+1) == 0x80 && (unsigned char)*(pos+2) == 0x93)) {
        bool subtract = false;

        // Handle regular +/-
        if (*pos == '-') {
            subtract = true;
            pos++;
        } else if (*pos == '+') {
            pos++;
        }
        // Handle en-dash (–) - represents -- which is double negative
        // En-dash alone acts as a single minus
        else if ((unsigned char)*pos == 0xE2) {
            subtract = true;
            pos += 3;  // Skip UTF-8 en-dash
        }

        skip_ws(pos);

        // Check for double negative (- followed by - or en-dash)
        // En-dash here represents --, so it flips twice (no net change)
        while (*pos == '-' ||
               ((unsigned char)*pos == 0xE2 && (unsigned char)*(pos+1) == 0x80 && (unsigned char)*(pos+2) == 0x93)) {
            if (*pos == '-') {
                // Single hyphen: flip once
                subtract = !subtract;
                pos++;
            } else {
                // En-dash: represents --, flip twice (no change)
                // But we're in a context where we're looking for MORE negatives
                // after an operator, so en-dash represents the original --
                // which was double negative = no flip
                pos += 3;
            }
            skip_ws(pos);
        }

        double rhs = parse_term(pos);
        if (subtract) result -= rhs;
        else result += rhs;
        skip_ws(pos);
    }

    return result;
}

// Parse a factor
static double parse_factor(const char*& pos) {
    skip_ws(pos);

    // Handle unary minus/plus
    bool negative = false;
    while (*pos == '-' || *pos == '+') {
        if (*pos == '-') negative = !negative;
        pos++;
        skip_ws(pos);
    }

    double result = 0;

    // Parenthesized expression
    if (*pos == '(') {
        pos++;  // Skip '('
        result = parse_expression(pos);
        skip_ws(pos);
        if (*pos == ')') pos++;  // Skip ')'
    }
    // Number
    else if ((*pos >= '0' && *pos <= '9') || *pos == '.') {
        result = parse_number(pos);
    }
    // Should not happen in well-formed input, return 0

    return negative ? -result : result;
}

// Serialize element content to expression string for evaluation
// Handles: strings, \real{}, \value{}, groups
static std::string serialize_expr_element(Element* elem);

static std::string serialize_expr_item(Item item) {
    TypeId type = get_type_id(item);

    if (type == LMD_TYPE_STRING) {
        String* str = item.get_string();
        if (str && str->len > 0) {
            return std::string(str->chars, str->len);
        }
        return "";
    }

    if (type == LMD_TYPE_ELEMENT) {
        Element* elem = item.element;
        if (!elem || !elem->type) return "";

        TypeElmt* elem_type = (TypeElmt*)elem->type;
        StrView name = elem_type->name;

        // \real{value} - return the floating point value as string
        if (name.length == 4 && strncmp(name.str, "real", 4) == 0) {
            // Get the argument value
            std::string arg_content = serialize_expr_element(elem);
            // Strip spaces and parse as double
            const char* p = arg_content.c_str();
            while (*p == ' ') p++;
            double val = atof(p);
            char buf[64];
            snprintf(buf, sizeof(buf), "%.10g", val);
            return buf;
        }

        // \value{counter} - return the counter value as string
        if (name.length == 5 && strncmp(name.str, "value", 5) == 0) {
            // Get the counter name from argument
            std::string counter_name = serialize_expr_element(elem);
            // Strip spaces
            size_t start = counter_name.find_first_not_of(" \t");
            size_t end = counter_name.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos) {
                counter_name = counter_name.substr(start, end - start + 1);
            }
            int value = get_counter_value(counter_name);
            return std::to_string(value);
        }

        // argument or group - recurse into children
        if ((name.length == 8 && strncmp(name.str, "argument", 8) == 0) ||
            (name.length == 5 && strncmp(name.str, "group", 5) == 0)) {
            return serialize_expr_element(elem);
        }

        // Unknown element, try to serialize children
        return serialize_expr_element(elem);
    }

    return "";
}

static std::string serialize_expr_element(Element* elem) {
    if (!elem) return "";

    std::string result;
    for (int64_t i = 0; i < elem->length; i++) {
        result += serialize_expr_item(elem->items[i]);
    }
    return result;
}

// Evaluate counter expression from element content
// Returns integer result (truncates floating point)
static int evaluate_counter_expression(Element* elem) {
    std::string expr = serialize_expr_element(elem);
    if (expr.empty()) return 0;

    const char* pos = expr.c_str();
    double result = parse_expression(pos);

    return (int)result;
}

// =============================================================================
// Document metadata storage
// =============================================================================

// Document metadata storage
typedef struct {
    char* title;
    char* author;
    char* date;
    bool in_document;
    int section_counter;
} DocumentState;

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

typedef struct {
    FontSeries series;
    FontShape shape;
    FontFamily family;
    bool em_active;  // Track if \em is active (for toggling)
} FontContext;

// Alignment context for tracking paragraph alignment
typedef enum {
    ALIGN_NORMAL,      // Default paragraph alignment
    ALIGN_CENTERING,   // \centering command
    ALIGN_RAGGEDRIGHT, // \raggedright command
    ALIGN_RAGGEDLEFT   // \raggedleft command
} AlignmentMode;

static void generate_latex_css(StringBuf* css_buf);
static void process_latex_element(StringBuf* html_buf, Item item, Pool* pool, int depth, FontContext* font_ctx);
static void process_element_content(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx);
static void process_element_content_simple(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx);
static void process_title(StringBuf* html_buf, Element* elem, Pool* pool, int depth);
static void process_author(StringBuf* html_buf, Element* elem, Pool* pool, int depth);
static void process_date(StringBuf* html_buf, Element* elem, Pool* pool, int depth);
static void process_maketitle(StringBuf* html_buf, Pool* pool, int depth);
static void process_chapter(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx);
static void process_section_h2(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx);
static void process_section(StringBuf* html_buf, Element* elem, Pool* pool, int depth, const char* css_class, FontContext* font_ctx);
static void process_environment(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx);
static void process_itemize(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx, int list_depth);
static void process_enumerate(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx, int list_depth, int& item_counter);
static void process_description(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx);
static void process_quote(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx, const char* env_type);
static void process_verbatim(StringBuf* html_buf, Element* elem, Pool* pool, int depth);
static void process_alignment_environment(StringBuf* html_buf, Element* elem, Pool* pool, int depth, const char* css_class, FontContext* font_ctx);
static void process_text_command(StringBuf* html_buf, Element* elem, Pool* pool, int depth, const char* css_class, const char* tag, FontContext* font_ctx);
static void process_font_scoped_command(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx, FontSeries series, FontShape shape, FontFamily family);
static void process_emph_command(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx);
static bool process_item(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx, int list_depth, bool is_enumerate, int item_number);
static void append_escaped_text(StringBuf* html_buf, const char* text);
static void append_escaped_text_with_ligatures(StringBuf* html_buf, const char* text, bool is_tt);
static void append_indent(StringBuf* html_buf, int depth);
static void close_paragraph(StringBuf* html_buf, bool add_newline);
static void open_paragraph(StringBuf* html_buf, bool noindent, bool cont);
static bool is_block_level_element(const char* cmd_name);

// reader-based forward declarations
static void process_latex_element_reader(StringBuf* html_buf, const ItemReader& item, Pool* pool, int depth, FontContext* font_ctx);
static void process_element_content_reader(StringBuf* html_buf, const ElementReader& elem, Pool* pool, int depth, FontContext* font_ctx);
static void process_element_content_simple_reader(StringBuf* html_buf, const ElementReader& elem, Pool* pool, int depth, FontContext* font_ctx);

static DocumentState doc_state = {0};
static FontContext font_context = {FONT_SERIES_NORMAL, FONT_SHAPE_UPRIGHT, FONT_FAMILY_ROMAN, false};
static int chapter_counter = 0;         // Counter for chapter numbering within document
static int section_counter = 0;         // Counter for section numbering within chapter (resets on new chapter)
static int global_section_id = 0;       // Global counter for id="sec-N" attribute
static AlignmentMode current_alignment = ALIGN_NORMAL;  // Track current paragraph alignment

// Alignment context helper functions
static const char* get_alignment_css_class(AlignmentMode align) {
    switch (align) {
        case ALIGN_CENTERING: return "centering";
        case ALIGN_RAGGEDRIGHT: return "raggedright";
        case ALIGN_RAGGEDLEFT: return "raggedleft";
        default: return nullptr;
    }
}

// Helper functions for font context
static const char* get_font_css_class(FontContext* ctx) {
    // Generate CSS class based on font context
    // Priority: family > series > shape

    if (ctx->family == FONT_FAMILY_TYPEWRITER) {
        return "tt";
    } else if (ctx->family == FONT_FAMILY_SANS_SERIF) {
        return "sf";
    }

    // For roman family, check series and shape
    if (ctx->series == FONT_SERIES_BOLD && ctx->shape == FONT_SHAPE_ITALIC) {
        return "bf-it";
    } else if (ctx->series == FONT_SERIES_BOLD && ctx->shape == FONT_SHAPE_SLANTED) {
        return "bf-sl";
    } else if (ctx->series == FONT_SERIES_BOLD) {
        return "bf";
    } else if (ctx->shape == FONT_SHAPE_ITALIC) {
        return "it";
    } else if (ctx->shape == FONT_SHAPE_SLANTED) {
        return "sl";
    } else if (ctx->shape == FONT_SHAPE_SMALL_CAPS) {
        return "sc";
    }

    return "up";  // Default upright
}

static bool needs_font_span(FontContext* ctx) {
    // Check if we need a font span (not in default state)
    return ctx->series != FONT_SERIES_NORMAL ||
           ctx->shape != FONT_SHAPE_UPRIGHT ||
           ctx->family != FONT_FAMILY_ROMAN;
}

// Helper: Extract string content from element's first argument
// Returns empty string if not found
static std::string extract_argument_string(Element* elem, int arg_index = 0) {
    if (!elem || elem->length <= arg_index) return "";

    Item child = elem->items[arg_index];
    TypeId child_type = get_type_id(child);

    // Direct string
    if (child_type == LMD_TYPE_STRING) {
        String* str = child.get_string();
        if (str && str->len > 0) {
            return std::string(str->chars, str->len);
        }
        return "";
    }

    // Argument element wrapping a string
    if (child_type == LMD_TYPE_ELEMENT) {
        Element* child_elem = child.element;
        if (child_elem && child_elem->type) {
            TypeElmt* child_elmt_type = (TypeElmt*)child_elem->type;
            StrView child_name = child_elmt_type->name;
            if (child_name.length == 8 && strncmp(child_name.str, "argument", 8) == 0) {
                // Recursively extract from argument
                if (child_elem->length > 0) {
                    Item inner = child_elem->items[0];
                    TypeId inner_type = get_type_id(inner);
                    if (inner_type == LMD_TYPE_STRING) {
                        String* str = inner.get_string();
                        if (str && str->len > 0) {
                            return std::string(str->chars, str->len);
                        }
                    }
                }
            }
        }
    }

    return "";
}

// Helper: Extract integer value from element's argument (for counter values)
// Handles simple integer expressions
static int extract_argument_int(Element* elem, int arg_index = 0) {
    if (!elem || elem->length <= arg_index) return 0;

    Item child = elem->items[arg_index];
    TypeId child_type = get_type_id(child);

    // Direct string containing number
    if (child_type == LMD_TYPE_STRING) {
        String* str = child.get_string();
        if (str && str->len > 0) {
            return atoi(str->chars);
        }
        return 0;
    }

    // Argument element
    if (child_type == LMD_TYPE_ELEMENT) {
        Element* child_elem = child.element;
        if (child_elem && child_elem->type) {
            TypeElmt* child_elmt_type = (TypeElmt*)child_elem->type;
            StrView child_name = child_elmt_type->name;
            if (child_name.length == 8 && strncmp(child_name.str, "argument", 8) == 0) {
                return extract_argument_int(child_elem, 0);
            }
        }
    }

    return 0;
}

// Helper: Extract argument element (for complex expression evaluation)
static Element* extract_argument_element(Element* elem, int arg_index = 0) {
    if (!elem || elem->length <= arg_index) return nullptr;

    Item child = elem->items[arg_index];
    TypeId child_type = get_type_id(child);

    if (child_type == LMD_TYPE_ELEMENT) {
        Element* child_elem = child.element;
        if (child_elem && child_elem->type) {
            TypeElmt* child_elmt_type = (TypeElmt*)child_elem->type;
            StrView child_name = child_elmt_type->name;
            if (child_name.length == 8 && strncmp(child_name.str, "argument", 8) == 0) {
                return child_elem;
            }
        }
        return child_elem;  // Return as-is if not wrapped in argument
    }

    return nullptr;
}

// Helper: Unwrap argument element and process its content
// Returns true if an argument was found and processed, false otherwise
static bool unwrap_and_process_argument(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx) {
    if (elem->length > 0) {
        Item first_child = elem->items[0];
        TypeId child_type = get_type_id(first_child);
        if (child_type == LMD_TYPE_ELEMENT) {
            Element* child_elem = first_child.element;
            if (child_elem && child_elem->type) {
                TypeElmt* child_elmt_type = (TypeElmt*)child_elem->type;
                StrView child_name = child_elmt_type->name;
                if (child_name.length == 8 && strncmp(child_name.str, "argument", 8) == 0) {
                    process_element_content_simple(html_buf, child_elem, pool, depth, font_ctx);
                    return true;
                }
            }
        }
    }
    return false;
}

// Text command mapping: LaTeX command -> CSS class
// These are scoped commands that wrap content in HTML with CSS classes
// Using short class names for compatibility with LaTeX.js
static const struct {
    const char* cmd;
    const char* css_class;
} text_command_map[] = {
    // Basic text formatting - using short names matching LaTeX.js
    {"textbf", "bf"},
    {"textit", "it"},
    {"texttt", "tt"},
    // Note: emph is NOT in this map - it uses process_emph_command for proper toggling

    // Additional text styles
    {"textup", "up"},
    {"textsl", "sl"},
    {"textsc", "sc"},

    // Text decorations
    {"underline", "underline"},
    {"sout", "sout"},

    // Font sizes - using LaTeX.js naming convention
    {"tiny", "tiny"},
    {"scriptsize", "scriptsize"},
    {"footnotesize", "footnotesize"},
    {"small", "small"},
    {"normalsize", "normalsize"},
    {"large", "large"},
    {"Large", "Large"},
    {"LARGE", "LARGE"},
    {"huge", "huge"},
    {"Huge", "Huge"},

    // Sentinel
    {NULL, NULL}
};

// Ligature conversion table: multi-char sequences to Unicode ligatures
// Longer matches are checked first
static const struct {
    const char* pattern;
    const char* replacement;
    int pattern_len;
    bool skip_in_tt;  // Skip in typewriter font
} ligature_table[] = {
    // Must check longer patterns first
    {"ffi", "\xEF\xAC\x83", 3, true},   // U+FB03 ﬃ
    {"ffl", "\xEF\xAC\x84", 3, true},   // U+FB04 ﬄ
    {"ff",  "\xEF\xAC\x80", 2, true},   // U+FB00 ﬀ
    {"fi",  "\xEF\xAC\x81", 2, true},   // U+FB01 ﬁ
    {"fl",  "\xEF\xAC\x82", 2, true},   // U+FB02 ﬂ
    // Quote ligatures
    {"``", "\xE2\x80\x9C", 2, false},   // U+201C " left double quote
    {"''", "\xE2\x80\x9D", 2, false},   // U+201D " right double quote
    {"!\xC2\xB4", "\xC2\xA1", 3, false},       // U+00A1 ¡ inverted exclamation (!´)
    {"?\xC2\xB4", "\xC2\xBF", 3, false},       // U+00BF ¿ inverted question (?´)
    {"<<", "\xC2\xAB", 2, false},       // U+00AB « left guillemet
    {">>", "\xC2\xBB", 2, false},       // U+00BB » right guillemet
    // Sentinel
    {NULL, NULL, 0, false}
};

// Symbol command table: LaTeX command names -> Unicode symbols
static const struct {
    const char* cmd;
    const char* symbol;
} symbol_table[] = {
    // Spaces
    {"space", " "},
    {"nobreakspace", "\xC2\xA0"},           // U+00A0 nbsp
    {"thinspace", "\xE2\x80\x89"},          // U+2009 THIN SPACE
    {"enspace", "\xE2\x80\x82"},            // U+2002
    {"enskip", "\xE2\x80\x82"},             // U+2002
    {"quad", "\xE2\x80\x83"},               // U+2003 em space
    {"qquad", "\xE2\x80\x83\xE2\x80\x83"},  // Double em space
    {"textvisiblespace", "\xE2\x90\xA3"},   // U+2423 ␣
    {"textcompwordmark", "\xE2\x80\x8C"},   // U+200C ZWNJ

    // Basic Latin - special characters
    {"textdollar", "$"},
    {"textless", "<"},
    {"textgreater", ">"},
    {"textbackslash", "\\"},
    {"textasciicircum", "^"},
    {"textunderscore", "_"},
    {"lbrack", "["},
    {"rbrack", "]"},
    {"textbraceleft", "{"},
    {"textbraceright", "}"},
    {"textasciitilde", "~"},
    {"slash", "/"},

    // Non-ASCII letters
    {"AA", "\xC3\x85"},     // Å
    {"aa", "\xC3\xA5"},     // å
    {"AE", "\xC3\x86"},     // Æ
    {"ae", "\xC3\xA6"},     // æ
    {"OE", "\xC5\x92"},     // Œ
    {"oe", "\xC5\x93"},     // œ
    {"O", "\xC3\x98"},      // Ø
    {"o", "\xC3\xB8"},      // ø
    {"DH", "\xC3\x90"},     // Ð
    {"dh", "\xC3\xB0"},     // ð
    {"TH", "\xC3\x9E"},     // Þ
    {"th", "\xC3\xBE"},     // þ
    {"ss", "\xC3\x9F"},     // ß
    {"SS", "\xE1\xBA\x9E"}, // ẞ capital eszett
    {"L", "\xC5\x81"},      // Ł
    {"l", "\xC5\x82"},      // ł
    {"i", "\xC4\xB1"},      // ı dotless i
    {"j", "\xC8\xB7"},      // ȷ dotless j

    // Quotes
    {"textquoteleft", "\xE2\x80\x98"},      // ' U+2018
    {"textquoteright", "\xE2\x80\x99"},     // ' U+2019
    {"textquotedblleft", "\xE2\x80\x9C"},   // " U+201C
    {"textquotedblright", "\xE2\x80\x9D"},  // " U+201D
    {"textquotesingle", "'"},
    {"textquotedbl", "\""},
    {"lq", "\xE2\x80\x98"},
    {"rq", "\xE2\x80\x99"},
    {"quotesinglbase", "\xE2\x80\x9A"},     // ‚ U+201A
    {"quotedblbase", "\xE2\x80\x9E"},       // „ U+201E
    {"guillemotleft", "\xC2\xAB"},          // « U+00AB
    {"guillemotright", "\xC2\xBB"},         // » U+00BB
    {"guilsinglleft", "\xE2\x80\xB9"},      // ‹ U+2039
    {"guilsinglright", "\xE2\x80\xBA"},     // › U+203A

    // Punctuation
    {"textendash", "\xE2\x80\x93"},         // – U+2013
    {"textemdash", "\xE2\x80\x94"},         // — U+2014
    {"textellipsis", "\xE2\x80\xA6"},       // … U+2026
    {"dots", "\xE2\x80\xA6"},
    {"ldots", "\xE2\x80\xA6"},
    {"textbullet", "\xE2\x80\xA2"},         // • U+2022
    {"textperiodcentered", "\xC2\xB7"},     // · U+00B7
    {"textdagger", "\xE2\x80\xA0"},         // † U+2020
    {"dag", "\xE2\x80\xA0"},
    {"textdaggerdbl", "\xE2\x80\xA1"},      // ‡ U+2021
    {"ddag", "\xE2\x80\xA1"},
    {"textexclamdown", "\xC2\xA1"},         // ¡ U+00A1
    {"textquestiondown", "\xC2\xBF"},       // ¿ U+00BF
    {"textsection", "\xC2\xA7"},            // § U+00A7
    {"S", "\xC2\xA7"},
    {"textparagraph", "\xC2\xB6"},          // ¶ U+00B6
    {"P", "\xC2\xB6"},

    // Math-like symbols in text
    {"textasteriskcentered", "\xE2\x88\x97"}, // ∗ U+2217
    {"textbardbl", "\xE2\x80\x96"},           // ‖ U+2016

    // Currency
    {"textcent", "\xC2\xA2"},               // ¢ U+00A2
    {"textsterling", "\xC2\xA3"},           // £ U+00A3
    {"pounds", "\xC2\xA3"},
    {"textyen", "\xC2\xA5"},                // ¥ U+00A5
    {"texteuro", "\xE2\x82\xAC"},           // € U+20AC

    // Misc symbols
    {"textcopyright", "\xC2\xA9"},          // © U+00A9
    {"copyright", "\xC2\xA9"},
    {"textregistered", "\xC2\xAE"},         // ® U+00AE
    {"texttrademark", "\xE2\x84\xA2"},      // ™ U+2122
    {"textdegree", "\xC2\xB0"},             // ° U+00B0
    {"textordfeminine", "\xC2\xAA"},        // ª U+00AA
    {"textordmasculine", "\xC2\xBA"},       // º U+00BA
    {"textpm", "\xC2\xB1"},                 // ± U+00B1
    {"texttimes", "\xC3\x97"},              // × U+00D7
    {"textdiv", "\xC3\xB7"},                // ÷ U+00F7

    // Sentinel
    {NULL, NULL}
};

// Diacritics table: LaTeX accent command -> Unicode combining character
static const struct {
    char accent_char;      // The character after backslash
    const char* combining; // Combining character
    const char* standalone; // Standalone version
} diacritics_table[] = {
    {'\'', "\xCC\x81", "\xC2\xB4"},  // acute: á
    {'`',  "\xCC\x80", "`"},         // grave: à
    {'^',  "\xCC\x82", "^"},         // circumflex: â
    {'"',  "\xCC\x88", "\xC2\xA8"},  // umlaut: ä
    {'~',  "\xCC\x83", "~"},         // tilde: ã
    {'=',  "\xCC\x84", "\xC2\xAF"},  // macron: ā
    {'.',  "\xCC\x87", "\xCB\x99"},  // dot above: ȧ
    {'u',  "\xCC\x86", "\xCB\x98"},  // breve: ă
    {'v',  "\xCC\x8C", "\xCB\x87"},  // caron: ǎ
    {'H',  "\xCC\x8B", "\xCB\x9D"},  // double acute: ő
    {'c',  "\xCC\xA7", "\xC2\xB8"},  // cedilla: ç
    {'d',  "\xCC\xA3", ""},          // dot below: ạ
    {'b',  "\xCC\xB2", "_"},         // underline: a̲
    {'r',  "\xCC\x8A", "\xCB\x9A"},  // ring above: å
    {'k',  "\xCC\xA8", "\xCB\x9B"},  // ogonek: ą
    {'t',  "\xCD\x81", ""},          // tie above
    {'\0', NULL, NULL}  // Sentinel
};

// Helper: look up command in symbol_table
static const char* lookup_symbol(const char* cmd) {
    for (int i = 0; symbol_table[i].cmd != NULL; i++) {
        if (strcmp(symbol_table[i].cmd, cmd) == 0) {
            return symbol_table[i].symbol;
        }
    }
    return NULL;
}

// Helper: look up diacritic by accent character
static const char* lookup_diacritic_combining(char accent) {
    for (int i = 0; diacritics_table[i].accent_char != '\0'; i++) {
        if (diacritics_table[i].accent_char == accent) {
            return diacritics_table[i].combining;
        }
    }
    return NULL;
}

// Convert LaTeX dimension to CSS pixels
// Supports: cm, mm, in, pt, pc, em, ex
static double latex_dim_to_pixels(const char* dim_str) {
    if (!dim_str) return 0.0;

    // Parse number
    char* end;
    double value = strtod(dim_str, &end);
    if (end == dim_str) return 0.0; // No number found

    // Skip whitespace
    while (*end == ' ' || *end == '\t') end++;

    // Parse unit
    if (strncmp(end, "cm", 2) == 0) {
        return value * 37.795; // 1cm = 37.795px at 96dpi
    } else if (strncmp(end, "mm", 2) == 0) {
        return value * 3.7795; // 1mm = 3.7795px
    } else if (strncmp(end, "in", 2) == 0) {
        return value * 96.0; // 1in = 96px
    } else if (strncmp(end, "pt", 2) == 0) {
        return value * 1.33333; // 1pt = 1.33333px
    } else if (strncmp(end, "pc", 2) == 0) {
        return value * 16.0; // 1pc = 16px
    } else if (strncmp(end, "em", 2) == 0) {
        return value * 16.0; // 1em ≈ 16px (depends on font)
    } else if (strncmp(end, "ex", 2) == 0) {
        return value * 8.0; // 1ex ≈ 8px (depends on font)
    }

    // Default: assume pixels
    return value;
}

// Main API function
void format_latex_to_html(StringBuf* html_buf, StringBuf* css_buf, Item latex_ast, Pool* pool) {
    if (!html_buf || !css_buf || !pool) {
        return;
    }

    // printf("DEBUG: format_latex_to_html - html_buf=%p, css_buf=%p\n", html_buf, css_buf);

    // Initialize document state
    memset(&doc_state, 0, sizeof(DocumentState));

    // Initialize font context
    font_context.series = FONT_SERIES_NORMAL;
    font_context.shape = FONT_SHAPE_UPRIGHT;
    font_context.family = FONT_FAMILY_ROMAN;
    font_context.em_active = false;

    // Reset counters
    chapter_counter = 0;
    section_counter = 0;
    global_section_id = 0;

    // Start HTML document container (using "body" class for LaTeX.js compatibility)
    stringbuf_append_str(html_buf, "<div class=\"body\">\n");
    // printf("DEBUG: Added HTML container to html_buf\n");


    // Check if we have a valid AST
    if (latex_ast.item == ITEM_NULL) {
        fprintf(stderr, "DEBUG: format_latex_to_html: latex_ast is NULL\n");
    } else {
        // Process the LaTeX AST without automatic paragraph wrapper
        // Individual text content will be wrapped in paragraphs as needed
        fprintf(stderr, "DEBUG: format_latex_to_html: About to process LaTeX AST, type_id=%d\n", get_type_id(latex_ast));

        // use MarkReader API
        ItemReader ast_reader(latex_ast.to_const());
        process_latex_element_reader(html_buf, ast_reader, pool, 1, &font_context);
        
        fprintf(stderr, "DEBUG: format_latex_to_html: Finished processing LaTeX AST\n");
    }

    // Break down the CSS into smaller chunks to avoid C++ compiler issues with very long string literals

    // Document styles (using "body" class for LaTeX.js compatibility)
    stringbuf_append_str(css_buf, ".body {\n");
    stringbuf_append_str(css_buf, "  font-family: 'Computer Modern', 'Latin Modern', serif;\n");
    stringbuf_append_str(css_buf, "  max-width: 800px;\n");
    stringbuf_append_str(css_buf, "  margin: 0 auto;\n");
    stringbuf_append_str(css_buf, "  padding: 2rem;\n");
    stringbuf_append_str(css_buf, "  line-height: 1.6;\n");
    stringbuf_append_str(css_buf, "  color: #333;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Title styles
    stringbuf_append_str(css_buf, ".latex-title {\n");
    stringbuf_append_str(css_buf, "  text-align: center;\n");
    stringbuf_append_str(css_buf, "  font-size: 2.5em;\n");
    stringbuf_append_str(css_buf, "  font-weight: bold;\n");
    stringbuf_append_str(css_buf, "  margin: 2rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Author styles
    stringbuf_append_str(css_buf, ".latex-author {\n");
    stringbuf_append_str(css_buf, "  text-align: center;\n");
    stringbuf_append_str(css_buf, "  font-size: 1.2em;\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Date styles
    stringbuf_append_str(css_buf, ".latex-date {\n");
    stringbuf_append_str(css_buf, "  text-align: center;\n");
    stringbuf_append_str(css_buf, "  font-style: italic;\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0 2rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Section styles
    stringbuf_append_str(css_buf, ".latex-section {\n");
    stringbuf_append_str(css_buf, "  font-size: 1.8em;\n");
    stringbuf_append_str(css_buf, "  font-weight: bold;\n");
    stringbuf_append_str(css_buf, "  margin: 2rem 0 1rem 0;\n");
    stringbuf_append_str(css_buf, "  border-bottom: 1px solid #ccc;\n");
    stringbuf_append_str(css_buf, "  padding-bottom: 0.5rem;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Subsection styles
    stringbuf_append_str(css_buf, ".latex-subsection {\n");
    stringbuf_append_str(css_buf, "  font-size: 1.4em;\n");
    stringbuf_append_str(css_buf, "  font-weight: bold;\n");
    stringbuf_append_str(css_buf, "  margin: 1.5rem 0 1rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Subsubsection styles
    stringbuf_append_str(css_buf, ".latex-subsubsection {\n");
    stringbuf_append_str(css_buf, "  font-size: 1.2em;\n");
    stringbuf_append_str(css_buf, "  font-weight: bold;\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0 0.5rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Text formatting styles
    stringbuf_append_str(css_buf, ".latex-textbf {\n");
    stringbuf_append_str(css_buf, "  font-weight: bold;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-textit {\n");
    stringbuf_append_str(css_buf, "  font-style: italic;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-emph {\n");
    stringbuf_append_str(css_buf, "  font-style: italic;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-texttt {\n");
    stringbuf_append_str(css_buf, "  font-family: 'Courier New', monospace;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-underline {\n");
    stringbuf_append_str(css_buf, "  text-decoration: underline;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-sout {\n");
    stringbuf_append_str(css_buf, "  text-decoration: line-through;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Font size classes
    stringbuf_append_str(css_buf, ".latex-tiny { font-size: 0.5em; }\n");
    stringbuf_append_str(css_buf, ".latex-small { font-size: 0.8em; }\n");
    stringbuf_append_str(css_buf, ".latex-normalsize { font-size: 1em; }\n");
    stringbuf_append_str(css_buf, ".latex-large { font-size: 1.2em; }\n");
    stringbuf_append_str(css_buf, ".latex-Large { font-size: 1.4em; }\n");
    stringbuf_append_str(css_buf, ".latex-huge { font-size: 2em; }\n");

    // Font family classes
    stringbuf_append_str(css_buf, ".latex-textrm { font-family: serif; }\n");
    stringbuf_append_str(css_buf, ".latex-textsf { font-family: sans-serif; }\n");

    // Font weight classes
    stringbuf_append_str(css_buf, ".latex-textmd { font-weight: normal; }\n");

    // Font shape classes
    stringbuf_append_str(css_buf, ".latex-textup { font-style: normal; }\n");
    stringbuf_append_str(css_buf, ".latex-textsl { font-style: oblique; }\n");
    stringbuf_append_str(css_buf, ".latex-textsc { font-variant: small-caps; }\n");

    // Reset to normal
    stringbuf_append_str(css_buf, ".latex-textnormal { font-family: serif; font-weight: normal; font-style: normal; font-variant: normal; }\n");

    // Verbatim styles
    stringbuf_append_str(css_buf, ".latex-verbatim { font-family: 'Courier New', 'Lucida Console', monospace; background-color: #f5f5f5; padding: 0.2em 0.4em; border-radius: 3px; }\n");

    // List styles
    stringbuf_append_str(css_buf, ".latex-itemize {\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0;\n");
    stringbuf_append_str(css_buf, "  padding-left: 2rem;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-enumerate {\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0;\n");
    stringbuf_append_str(css_buf, "  padding-left: 2rem;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-item {\n");
    stringbuf_append_str(css_buf, "  margin: 0.5rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Alignment environment styles
    stringbuf_append_str(css_buf, ".list.center {\n");
    stringbuf_append_str(css_buf, "  text-align: center;\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".list.flushleft {\n");
    stringbuf_append_str(css_buf, "  text-align: left;\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".list.flushright {\n");
    stringbuf_append_str(css_buf, "  text-align: right;\n");
    stringbuf_append_str(css_buf, "  margin: 1rem 0;\n");
    stringbuf_append_str(css_buf, "}\n");

    // Spacing styles
    stringbuf_append_str(css_buf, ".negthinspace { margin-left: -0.16667em; }\n");
    stringbuf_append_str(css_buf, ".breakspace { display: block; }\n");
    stringbuf_append_str(css_buf, ".vspace { display: block; }\n");
    stringbuf_append_str(css_buf, ".vspace.smallskip { margin-top: 0.5rem; }\n");
    stringbuf_append_str(css_buf, ".vspace.medskip { margin-top: 1rem; }\n");
    stringbuf_append_str(css_buf, ".vspace.bigskip { margin-top: 2rem; }\n");
    stringbuf_append_str(css_buf, ".vspace-inline { display: inline; }\n");

    // Font declaration styles (short class names for LaTeX.js compatibility)
    stringbuf_append_str(css_buf, ".bf { font-weight: bold; }\n");
    stringbuf_append_str(css_buf, ".it { font-style: italic; }\n");
    stringbuf_append_str(css_buf, ".sl { font-style: oblique; }\n");
    stringbuf_append_str(css_buf, ".sc { font-variant: small-caps; }\n");
    stringbuf_append_str(css_buf, ".up { font-weight: normal; font-style: normal; }\n");
    stringbuf_append_str(css_buf, ".tt { font-family: 'Courier New', monospace; }\n");
    stringbuf_append_str(css_buf, ".sf { font-family: sans-serif; }\n");
    stringbuf_append_str(css_buf, ".bf-it { font-weight: bold; font-style: italic; }\n");
    stringbuf_append_str(css_buf, ".bf-sl { font-weight: bold; font-style: oblique; }\n");

    // Close document container
    stringbuf_append_str(html_buf, "</div>\n");
    // printf("DEBUG: Closed HTML container\n");

    // printf("DEBUG: HTML and CSS generation completed\n");
}

// Generate comprehensive CSS for LaTeX documents
static void generate_latex_css(StringBuf* css_buf) {
    if (!css_buf) {
        // printf("DEBUG: css_buf is NULL!\n");
        return;
    }

    // printf("DEBUG: css_buf=%p, pool=%p, str=%p, length=%zu, capacity=%zu\n",
    //        css_buf, css_buf->pool, css_buf->str, css_buf->length, css_buf->capacity);

    // Validate StringBuf structure before using it
    if (!css_buf->pool) {
        // printf("DEBUG: css_buf->pool is NULL!\n");
        return;
    }

    if (css_buf->str && css_buf->capacity == 0) {
        // printf("DEBUG: css_buf has str but zero capacity!\n");
        return;
    }

    if (css_buf->length > css_buf->capacity) {
        // printf("DEBUG: css_buf length (%zu) > capacity (%zu)!\n", css_buf->length, css_buf->capacity);
        return;
    }

    // Generate basic CSS for LaTeX documents - broken into small chunks to avoid C++ compiler issues
    // Using "body" class for LaTeX.js compatibility
    stringbuf_append_str(css_buf, ".body {\n");
    stringbuf_append_str(css_buf, "  font-family: 'Computer Modern', 'Latin Modern', serif;\n");
    stringbuf_append_str(css_buf, "  max-width: 800px;\n");
    stringbuf_append_str(css_buf, "  margin: 0 auto;\n");
    stringbuf_append_str(css_buf, "  padding: 2rem;\n");
    stringbuf_append_str(css_buf, "  line-height: 1.6;\n");
    stringbuf_append_str(css_buf, "  color: #333;\n");
    stringbuf_append_str(css_buf, "}\n");

    stringbuf_append_str(css_buf, ".latex-textbf { font-weight: bold; }\n");
    stringbuf_append_str(css_buf, ".latex-textit { font-style: italic; }\n");
    stringbuf_append_str(css_buf, ".latex-section { font-size: 1.8em; font-weight: bold; margin: 2rem 0 1rem 0; }\n");
    stringbuf_append_str(css_buf, ".latex-subsection { font-size: 1.4em; font-weight: bold; margin: 1.5rem 0 1rem 0; }\n");

    // Additional CSS for list environments
    stringbuf_append_str(css_buf, ".latex-itemize, .latex-enumerate { margin: 1rem 0; padding-left: 2rem; }\n");
    stringbuf_append_str(css_buf, ".latex-item { margin: 0.5rem 0; }\n");

    // TeX/LaTeX logo CSS - based on latex.js styling
    stringbuf_append_str(css_buf, ".tex, .latex { font-family: 'Computer Modern', 'Latin Modern', serif; text-transform: uppercase; }\n");
    stringbuf_append_str(css_buf, ".tex .e { position: relative; top: 0.5ex; margin-left: -0.1667em; margin-right: -0.125em; text-transform: lowercase; }\n");
    stringbuf_append_str(css_buf, ".latex .a { position: relative; top: -0.5ex; font-size: 0.85em; margin-left: -0.36em; margin-right: -0.15em; text-transform: uppercase; }\n");
    stringbuf_append_str(css_buf, ".latex .e { position: relative; top: 0.5ex; margin-left: -0.1667em; margin-right: -0.125em; text-transform: lowercase; }\n");
    stringbuf_append_str(css_buf, ".latex .epsilon { font-family: serif; font-style: italic; }\n");
    stringbuf_append_str(css_buf, ".tex .xe { position: relative; margin-left: -0.125em; margin-right: -0.1667em; }\n");
}

// Helper function to check if an element is block-level (should not be wrapped in <p>)
static bool is_block_level_element(const char* cmd_name) {
    if (!cmd_name) return false;
    
    // List environments
    if (strcmp(cmd_name, "itemize") == 0) return true;
    if (strcmp(cmd_name, "enumerate") == 0) return true;
    if (strcmp(cmd_name, "description") == 0) return true;
    
    // Sectioning commands
    if (strcmp(cmd_name, "section") == 0) return true;
    if (strcmp(cmd_name, "subsection") == 0) return true;
    if (strcmp(cmd_name, "subsubsection") == 0) return true;
    if (strcmp(cmd_name, "chapter") == 0) return true;
    if (strcmp(cmd_name, "part") == 0) return true;
    
    // Display environments
    if (strcmp(cmd_name, "verbatim") == 0) return true;
    if (strcmp(cmd_name, "equation") == 0) return true;
    if (strcmp(cmd_name, "align") == 0) return true;
    if (strcmp(cmd_name, "displaymath") == 0) return true;
    if (strcmp(cmd_name, "center") == 0) return true;
    if (strcmp(cmd_name, "flushleft") == 0) return true;
    if (strcmp(cmd_name, "flushright") == 0) return true;
    if (strcmp(cmd_name, "quote") == 0) return true;
    if (strcmp(cmd_name, "quotation") == 0) return true;
    if (strcmp(cmd_name, "verse") == 0) return true;
    
    // Table and figure environments
    if (strcmp(cmd_name, "table") == 0) return true;
    if (strcmp(cmd_name, "tabular") == 0) return true;
    if (strcmp(cmd_name, "figure") == 0) return true;
    
    return false;
}

// Process a LaTeX element and convert to HTML
static void process_latex_element(StringBuf* html_buf, Item item, Pool* pool, int depth, FontContext* font_ctx) {
    if (item.item == ITEM_NULL) {
        return;
    }

    TypeId type = get_type_id(item);

    // CRITICAL DEBUG: Check for invalid type values
    if (type > LMD_TYPE_ERROR) {
        printf("ERROR: Invalid type %d detected in process_latex_element! Max valid type is %d\n", type, LMD_TYPE_ERROR);
        printf("ERROR: Stack trace: depth=%d\n", depth);
        return;
    }

    if (type == LMD_TYPE_ELEMENT) {
        Element* elem = item.element;
        if (!elem || !elem->type) {
            return;
        }

        TypeElmt* elmt_type = (TypeElmt*)elem->type;
        if (!elmt_type) {
            return;
        }

        StrView name = elmt_type->name;
        if (!name.str || name.length == 0 || name.length > 100) return;

        // Convert name to null-terminated string for easier comparison
        char cmd_name[64];
        int name_len = name.length < 63 ? name.length : 63;
        strncpy(cmd_name, name.str, name_len);
        cmd_name[name_len] = '\0';

        // Debug: log all commands
        if (depth < 3) {  // Only log top-level commands to avoid spam
            log_debug("Processing command: %s (depth=%d)", cmd_name, depth);
        }

        // Check if this is a user-defined macro and expand it
        if (is_macro(cmd_name)) {
            MacroDefinition* macro_def = get_macro(cmd_name);
            if (macro_def && macro_def->definition) {
                log_debug("Expanding macro '%s' with %d params", cmd_name, macro_def->num_params);
                // Extract arguments for the macro
                std::vector<Element*> args;
                for (int i = 0; i < macro_def->num_params && i < elem->length; i++) {
                    Element* arg = extract_argument_element(elem, i);
                    if (arg) {
                        log_debug("  arg[%d] = %p", i, arg);
                        args.push_back(arg);
                    }
                }
                
                // Expand the macro with arguments substituted
                Element* expanded = expand_macro_params(macro_def->definition, args, pool);
                if (expanded) {
                    log_debug("Expanded macro to element %p", expanded);
                    // Process the expanded definition
                    Item expanded_item;
                    expanded_item.element = expanded;
                    process_latex_element(html_buf, expanded_item, pool, depth + 1, font_ctx);
                }
                return;
            }
        }

        // Handle different LaTeX commands
        if (strcmp(cmd_name, "latex_document") == 0) {
            // Root document - handle content with paragraph breaks
            // Smart paragraph management: don't wrap block-level elements
            
            bool in_paragraph = false;
            bool needs_paragraph = false;  // Track if we need to open a paragraph
            
            List* list = (List*)elem;
            for (size_t i = 0; i < list->length; i++) {
                Item child = list->items[i];
                TypeId child_type = get_type_id(child);
                
                // Check for parbreak symbol
                if (child_type == LMD_TYPE_SYMBOL) {
                    Symbol* sym = (Symbol*)(child.symbol_ptr);
                    if (sym && strcmp(sym->chars, "parbreak") == 0) {
                        // Close current paragraph and mark that we need a new one
                        if (in_paragraph) {
                            stringbuf_append_str(html_buf, "</p>\n");
                            in_paragraph = false;
                        }
                        needs_paragraph = true;
                        continue;
                    }
                }
                
                // Check if this is a block-level element
                bool is_block = false;
                if (child_type == LMD_TYPE_ELEMENT) {
                    Element* child_elem = child.element;
                    if (child_elem && child_elem->type) {
                        TypeElmt* child_elmt_type = (TypeElmt*)child_elem->type;
                        if (child_elmt_type && child_elmt_type->name.str) {
                            char child_cmd[64];
                            int child_name_len = child_elmt_type->name.length < 63 ? child_elmt_type->name.length : 63;
                            strncpy(child_cmd, child_elmt_type->name.str, child_name_len);
                            child_cmd[child_name_len] = '\0';
                            is_block = is_block_level_element(child_cmd);
                        }
                    }
                }
                
                // Close paragraph before block element
                if (is_block && in_paragraph) {
                    stringbuf_append_str(html_buf, "</p>\n");
                    in_paragraph = false;
                }
                
                // Reset needs_paragraph flag when we encounter a block element
                // (don't open empty paragraphs between consecutive blocks)
                if (is_block) {
                    needs_paragraph = false;
                }
                
                // Open paragraph for inline content if needed
                if (!is_block && !in_paragraph) {
                    stringbuf_append_str(html_buf, "<p>");
                    in_paragraph = true;
                    needs_paragraph = false;
                }
                
                // Handle LIST children (tree-sitter creates lists for mixed content)
                if (child_type == LMD_TYPE_LIST) {
                    List* child_list = child.list;
                    for (size_t j = 0; j < child_list->length; j++) {
                        Item list_item = child_list->items[j];
                        
                        // Check for parbreak in list items
                        if (get_type_id(list_item) == LMD_TYPE_SYMBOL) {
                            Symbol* sym = (Symbol*)(list_item.symbol_ptr);
                            if (sym && strcmp(sym->chars, "parbreak") == 0) {
                                if (in_paragraph) {
                                    stringbuf_append_str(html_buf, "</p>\n");
                                    in_paragraph = false;
                                }
                                needs_paragraph = true;
                                continue;
                            }
                        }
                        
                        // Check if list item is block-level
                        bool item_is_block = false;
                        if (get_type_id(list_item) == LMD_TYPE_ELEMENT) {
                            Element* item_elem = list_item.element;
                            if (item_elem && item_elem->type) {
                                TypeElmt* item_elmt_type = (TypeElmt*)item_elem->type;
                                if (item_elmt_type && item_elmt_type->name.str) {
                                    char item_cmd[64];
                                    int item_name_len = item_elmt_type->name.length < 63 ? item_elmt_type->name.length : 63;
                                    strncpy(item_cmd, item_elmt_type->name.str, item_name_len);
                                    item_cmd[item_name_len] = '\0';
                                    item_is_block = is_block_level_element(item_cmd);
                                }
                            }
                        }
                        
                        // Close paragraph before block element
                        if (item_is_block && in_paragraph) {
                            stringbuf_append_str(html_buf, "</p>\n");
                            in_paragraph = false;
                        }
                        
                        // Open paragraph for inline content
                        if (!item_is_block && !in_paragraph) {
                            stringbuf_append_str(html_buf, "<p>");
                            in_paragraph = true;
                        }
                        
                        process_latex_element(html_buf, list_item, pool, depth + 1, font_ctx);
                    }
                } else {
                    process_latex_element(html_buf, child, pool, depth + 1, font_ctx);
                }
            }
            
            // Close final paragraph if still open
            if (in_paragraph) {
                stringbuf_append_str(html_buf, "</p>");
            }
            return;
        } else if (strcmp(cmd_name, "argument") == 0) {
            // Process argument content (nested LaTeX) without paragraph wrapping
            process_element_content_simple(html_buf, elem, pool, depth, font_ctx);
            return;
        } else if (strcmp(cmd_name, "group") == 0 || strcmp(cmd_name, "curly_group") == 0) {
            // Curly braces create a font and alignment scope - save/restore context
            FontContext saved_ctx = *font_ctx;
            AlignmentMode saved_alignment = current_alignment;
            size_t before_len = html_buf->length;

            // Check if this group contains nested groups
            bool has_nested_groups = false;
            List* list = (List*)elem;
            for (size_t i = 0; i < list->length; i++) {
                Item child = list->items[i];
                TypeId child_type = get_type_id(child);
                if (child_type == LMD_TYPE_ELEMENT) {
                    Element* child_elem = child.element;
                    TypeElmt* child_type_info = (TypeElmt*)child_elem->type;
                    StrView child_name = child_type_info->name;
                    if ((child_name.length == 5 && strncmp(child_name.str, "group", 5) == 0) ||
                        (child_name.length == 11 && strncmp(child_name.str, "curly_group", 11) == 0)) {
                        has_nested_groups = true;
                        break;
                    }
                }
            }

            process_element_content_simple(html_buf, elem, pool, depth, font_ctx);
            *font_ctx = saved_ctx;
            current_alignment = saved_alignment;  // Restore alignment scope

            // Add zero-width space after group for word boundary (U+200B)
            // Empty groups {} are explicitly used in LaTeX for word boundary
            // Non-empty groups also need ZWSP unless they end with whitespace
            bool is_empty_group = (html_buf->length == before_len);
            if (is_empty_group) {
                // Empty {} is explicitly a word boundary marker
                stringbuf_append_str(html_buf, "\xE2\x80\x8B");
            } else {
                char last_char = html_buf->str->chars[html_buf->length - 1];
                bool ends_with_space = (last_char == ' ' || last_char == '\t' || last_char == '\n');
                if (has_nested_groups || !ends_with_space) {
                    stringbuf_append_str(html_buf, "\xE2\x80\x8B");
                }
            }
            return;
        } else if (strcmp(cmd_name, "emph") == 0) {
            // \emph toggles italic/upright based on current state
            process_emph_command(html_buf, elem, pool, depth, font_ctx);
            return;
        } else if (strcmp(cmd_name, "documentclass") == 0) {
            // Skip documentclass - it's metadata
            return;
        }
        else if (strcmp(cmd_name, "usepackage") == 0) {
            // Skip usepackage - it's preamble metadata
            return;
        }
        else if (strcmp(cmd_name, "setlength") == 0) {
            // Skip setlength - it's layout metadata
            return;
        }
        // =================================================================
        // Counter commands
        // =================================================================
        else if (strcmp(cmd_name, "newcounter") == 0) {
            // \newcounter{name}[parent] - create a new counter
            std::string counter_name = extract_argument_string(elem, 0);
            std::string parent_name = extract_argument_string(elem, 1);  // optional
            if (!counter_name.empty()) {
                new_counter(counter_name, parent_name);
            }
            return;
        }
        else if (strcmp(cmd_name, "setcounter") == 0) {
            // \setcounter{name}{expression} - set counter to expression value
            std::string counter_name = extract_argument_string(elem, 0);
            Element* expr_elem = extract_argument_element(elem, 1);
            int value = expr_elem ? evaluate_counter_expression(expr_elem) : 0;
            if (!counter_name.empty()) {
                set_counter(counter_name, value);
            }
            return;
        }
        else if (strcmp(cmd_name, "stepcounter") == 0) {
            // \stepcounter{name}
            std::string counter_name = extract_argument_string(elem, 0);
            if (!counter_name.empty()) {
                step_counter(counter_name);
            }
            return;
        }
        else if (strcmp(cmd_name, "addtocounter") == 0) {
            // \addtocounter{name}{expression} - add expression value to counter
            std::string counter_name = extract_argument_string(elem, 0);
            Element* expr_elem = extract_argument_element(elem, 1);
            int delta = expr_elem ? evaluate_counter_expression(expr_elem) : 0;
            if (!counter_name.empty()) {
                add_to_counter(counter_name, delta);
            }
            return;
        }
        else if (strcmp(cmd_name, "arabic") == 0) {
            // \arabic{counter} - output counter value as arabic numeral
            std::string counter_name = extract_argument_string(elem, 0);
            if (!counter_name.empty()) {
                int value = get_counter_value(counter_name);
                stringbuf_append_str(html_buf, format_arabic(value).c_str());
            }
            return;
        }
        else if (strcmp(cmd_name, "roman") == 0) {
            // \roman{counter} - output counter value as lowercase roman
            std::string counter_name = extract_argument_string(elem, 0);
            if (!counter_name.empty()) {
                int value = get_counter_value(counter_name);
                stringbuf_append_str(html_buf, format_roman(value, false).c_str());
            }
            return;
        }
        else if (strcmp(cmd_name, "Roman") == 0) {
            // \Roman{counter} - output counter value as uppercase roman
            std::string counter_name = extract_argument_string(elem, 0);
            if (!counter_name.empty()) {
                int value = get_counter_value(counter_name);
                stringbuf_append_str(html_buf, format_roman(value, true).c_str());
            }
            return;
        }
        else if (strcmp(cmd_name, "alph") == 0) {
            // \alph{counter} - output counter value as lowercase letter
            std::string counter_name = extract_argument_string(elem, 0);
            if (!counter_name.empty()) {
                int value = get_counter_value(counter_name);
                stringbuf_append_str(html_buf, format_alph(value, false).c_str());
            }
            return;
        }
        else if (strcmp(cmd_name, "Alph") == 0) {
            // \Alph{counter} - output counter value as uppercase letter
            std::string counter_name = extract_argument_string(elem, 0);
            if (!counter_name.empty()) {
                int value = get_counter_value(counter_name);
                stringbuf_append_str(html_buf, format_alph(value, true).c_str());
            }
            return;
        }
        else if (strcmp(cmd_name, "fnsymbol") == 0) {
            // \fnsymbol{counter} - output counter value as footnote symbol
            std::string counter_name = extract_argument_string(elem, 0);
            if (!counter_name.empty()) {
                int value = get_counter_value(counter_name);
                stringbuf_append_str(html_buf, format_fnsymbol(value).c_str());
            }
            return;
        }
        else if (strcmp(cmd_name, "real") == 0) {
            // \real{number} - floating point value (usually in counter expressions, but can appear standalone)
            std::string num_str = extract_argument_string(elem, 0);
            if (!num_str.empty()) {
                // Just output the number as-is (stripped of extra spaces)
                const char* p = num_str.c_str();
                while (*p == ' ') p++;
                stringbuf_append_str(html_buf, p);
            }
            return;
        }
        else if (strcmp(cmd_name, "value") == 0) {
            // \value{counter} - output counter value (used in expressions, but for now just output)
            std::string counter_name = extract_argument_string(elem, 0);
            if (!counter_name.empty()) {
                int value = get_counter_value(counter_name);
                stringbuf_append_str(html_buf, std::to_string(value).c_str());
            }
            return;
        }
        else if (strcmp(cmd_name, "the") == 0) {
            // \the alone - no-op, the following \value{} or counter command handles output
            // This handles cases like \the\value{c}
            return;
        }
        else if (strncmp(cmd_name, "the", 3) == 0 && strlen(cmd_name) > 3) {
            // \theFOO - dynamic counter display command (e.g., \thesection, \thepage)
            std::string counter_name = cmd_name + 3;  // Skip "the" prefix
            int value = get_counter_value(counter_name);
            stringbuf_append_str(html_buf, format_arabic(value).c_str());
            return;
        }
        // =================================================================
        // Label and Reference commands
        // =================================================================
        else if (strcmp(cmd_name, "refstepcounter") == 0) {
            // \refstepcounter{counter} - step counter and set current label
            std::string counter_name = extract_argument_string(elem, 0);
            if (!counter_name.empty()) {
                step_counter(counter_name);
                int value = get_counter_value(counter_name);
                std::string anchor = counter_name + "-" + std::to_string(value);
                std::string text = std::to_string(value);
                set_current_label(anchor, text);
                // Output the anchor
                stringbuf_append_str(html_buf, "<a id=\"");
                stringbuf_append_str(html_buf, anchor.c_str());
                stringbuf_append_str(html_buf, "\"></a>");
            }
            return;
        }
        else if (strcmp(cmd_name, "label") == 0) {
            // \label{name} - register current label context under this name
            std::string label_name = extract_argument_string(elem, 0);
            if (!label_name.empty()) {
                register_label(label_name);
                // If there's a current label anchor, we don't need to output anything
                // (the anchor was already output by refstepcounter/section)
            }
            return;
        }
        else if (strcmp(cmd_name, "ref") == 0) {
            // \ref{name} - output link to label
            std::string label_name = extract_argument_string(elem, 0);
            LabelInfo info = get_label_info(label_name);
            stringbuf_append_str(html_buf, "<a href=\"#");
            stringbuf_append_str(html_buf, info.anchor_id.c_str());
            stringbuf_append_str(html_buf, "\">");
            stringbuf_append_str(html_buf, info.ref_text.c_str());
            stringbuf_append_str(html_buf, "</a>");
            return;
        }
        else if (strcmp(cmd_name, "pageref") == 0) {
            // \pageref{name} - in HTML, just output ?? since pages don't exist
            stringbuf_append_str(html_buf, "??");
            return;
        }
        
        // Macro definition commands
        // =================================================================
        else if (strcmp(cmd_name, "new_command_definition") == 0 || strcmp(cmd_name, "renew_command_definition") == 0) {
            // \newcommand{\name}[numargs]{definition}
            // \renewcommand{\name}[numargs]{definition}
            // Parser creates structure: new_command_definition with children:
            //   0: \newcommand element (skip)
            //   1: curly_group_command_name (contains the command name)
            //   2: brack_group_argc (optional, contains number)
            //   3: curly_group (contains definition)
            
            log_debug("Processing new_command_definition with %zu children", elem->length);
            
            // Simple approach: scan for non-system element names
            // System names: start with \ or are group names (curly_group, brack_group_argc)
            // Command name: first regular element name (like "greet")
            // Param count: inside brack_group_argc
            // Definition: inside curly_group (last one)
            
            std::string new_cmd_name;
            int num_params = 0;
            Element* definition = nullptr;
            
            for (size_t i = 0; i < elem->length; i++) {
                if (get_type_id(elem->items[i]) != LMD_TYPE_ELEMENT) continue;
                Element* child = elem->items[i].element;
                if (!child || !child->type) continue;
                
                TypeElmt* child_type = (TypeElmt*)child->type;
                StrView child_name = child_type->name;
                char child_name_str[64];
                int child_name_len = child_name.length < 63 ? child_name.length : 63;
                strncpy(child_name_str, child_name.str, child_name_len);
                child_name_str[child_name_len] = '\0';
                
                log_debug("  child[%zu]: %s", i, child_name_str);
                
                // Check for command name group
                if (strcmp(child_name_str, "curly_group_command_name") == 0) {
                    // Look inside this group for the actual command element
                    for (size_t j = 0; j < child->length; j++) {
                        if (get_type_id(child->items[j]) == LMD_TYPE_ELEMENT) {
                            Element* cmd_elem = child->items[j].element;
                            if (cmd_elem && cmd_elem->type) {
                                TypeElmt* cmd_type = (TypeElmt*)cmd_elem->type;
                                new_cmd_name = std::string(cmd_type->name.str, cmd_type->name.length);
                                log_debug("    Found command name: %s", new_cmd_name.c_str());
                                break;
                            }
                        }
                    }
                }
                // Check for param count
                else if (strcmp(child_name_str, "brack_group_argc") == 0) {
                    std::string param_str = extract_argument_string(child, 0);
                    if (!param_str.empty()) {
                        num_params = atoi(param_str.c_str());
                        log_debug("    Found param count: %d", num_params);
                    }
                }
                // Check for definition body
                else if (strcmp(child_name_str, "curly_group") == 0) {
                    definition = child;
                    log_debug("    Found definition element");
                }
            }
            
            if (!new_cmd_name.empty() && definition) {
                log_debug("Registering macro '%s' with %d params", new_cmd_name.c_str(), num_params);
                register_macro(new_cmd_name, num_params, definition, false);
            }
            return;
        }
        
        // =================================================================
        else if (strcmp(cmd_name, "title") == 0) {
            process_title(html_buf, elem, pool, depth);
            return;
        }
        else if (strcmp(cmd_name, "author") == 0) {
            process_author(html_buf, elem, pool, depth);
            return;
        }
        else if (strcmp(cmd_name, "date") == 0) {
            process_date(html_buf, elem, pool, depth);
            return;
        }
        else if (strcmp(cmd_name, "maketitle") == 0) {
            process_maketitle(html_buf, pool, depth);
            return;
        }
        else if (strcmp(cmd_name, "chapter") == 0) {
            process_chapter(html_buf, elem, pool, depth, font_ctx);
            return;
        }
        else if (strcmp(cmd_name, "section") == 0) {
            // Always use h2 with numbering for sections
            process_section_h2(html_buf, elem, pool, depth, font_ctx);
            return;
        }
        else if (strcmp(cmd_name, "subsection") == 0) {
            process_section(html_buf, elem, pool, depth, "latex-subsection", font_ctx);
            return;
        }
        else if (strcmp(cmd_name, "subsubsection") == 0) {
            process_section(html_buf, elem, pool, depth, "latex-subsubsection", font_ctx);
            return;
        }
        else if (strcmp(cmd_name, "begin") == 0) {
            process_environment(html_buf, elem, pool, depth, font_ctx);
            return;
        }
        else if (strcmp(cmd_name, "center") == 0) {
            process_alignment_environment(html_buf, elem, pool, depth, "latex-center", font_ctx);
            return;
        }
        else if (strcmp(cmd_name, "flushleft") == 0) {
            process_alignment_environment(html_buf, elem, pool, depth, "latex-flushleft", font_ctx);
            return;
        }
        else if (strcmp(cmd_name, "flushright") == 0) {
            process_alignment_environment(html_buf, elem, pool, depth, "latex-flushright", font_ctx);
            return;
        }
        else if (strcmp(cmd_name, "quote") == 0) {
            process_quote(html_buf, elem, pool, depth, font_ctx, "quote");
            return;
        }
        else if (strcmp(cmd_name, "quotation") == 0) {
            process_quote(html_buf, elem, pool, depth, font_ctx, "quotation");
            return;
        }
        else if (strcmp(cmd_name, "verse") == 0) {
            process_quote(html_buf, elem, pool, depth, font_ctx, "verse");
            return;
        }
        else if (strcmp(cmd_name, "verbatim") == 0) {
            process_verbatim(html_buf, elem, pool, depth);
            return;
        }
        else if (strcmp(cmd_name, "comment") == 0) {
            // comment environment: suppress all content (do nothing)
            return;
        }

        // Check text command map for common formatting commands
        // This handles: textbf, textit, texttt, emph, textup, textsl, textsc, underline, sout, font sizes
        bool handled_by_map = false;
        for (int i = 0; text_command_map[i].cmd != NULL; i++) {
            if (strcmp(cmd_name, text_command_map[i].cmd) == 0) {
                process_text_command(html_buf, elem, pool, depth, text_command_map[i].css_class, "span", font_ctx);
                handled_by_map = true;
                break;
            }
        }
        if (handled_by_map) {
            return;
        }

        // Font family commands use font context (not in map since they need special handling)
        if (strcmp(cmd_name, "textrm") == 0) {
            process_font_scoped_command(html_buf, elem, pool, depth, font_ctx, FONT_SERIES_NORMAL, FONT_SHAPE_UPRIGHT, FONT_FAMILY_ROMAN);
        }
        else if (strcmp(cmd_name, "textsf") == 0) {
            process_font_scoped_command(html_buf, elem, pool, depth, font_ctx, FONT_SERIES_NORMAL, FONT_SHAPE_UPRIGHT, FONT_FAMILY_SANS_SERIF);
        }
        else if (strcmp(cmd_name, "textmd") == 0) {
            process_font_scoped_command(html_buf, elem, pool, depth, font_ctx, FONT_SERIES_NORMAL, FONT_SHAPE_UPRIGHT, FONT_FAMILY_ROMAN);
        }
        else if (strcmp(cmd_name, "textnormal") == 0) {
            // textnormal resets to defaults
            process_font_scoped_command(html_buf, elem, pool, depth, font_ctx, FONT_SERIES_NORMAL, FONT_SHAPE_UPRIGHT, FONT_FAMILY_ROMAN);
        }
        else if (strcmp(cmd_name, "linebreak") == 0 || strcmp(cmd_name, "linebreak_command") == 0 || strcmp(cmd_name, "newline") == 0) {
            // Check if linebreak has spacing argument (dimension)
            if (elem->length > 0 && elem->items) {
                Item spacing_item = elem->items[0];
                if (get_type_id(spacing_item) == LMD_TYPE_STRING) {
                    String* spacing_str = spacing_item.get_string();
                    if (spacing_str && spacing_str->len > 0) {
                        // Output <br> with spacing style
                        double pixels = latex_dim_to_pixels(spacing_str->chars);

                        char px_str[32];
                        snprintf(px_str, sizeof(px_str), "%.3fpx", pixels);

                        stringbuf_append_str(html_buf, "<span class=\"breakspace\" style=\"margin-bottom:");
                        stringbuf_append_str(html_buf, px_str);
                        stringbuf_append_str(html_buf, "\"></span>");
                        return;
                    }
                }
            }
            // Regular linebreak without spacing
            stringbuf_append_str(html_buf, "<br>");
        }
        else if (strcmp(cmd_name, "par") == 0) {
            // Par creates a paragraph break - handled by paragraph logic
            // This is a no-op in HTML since paragraph breaks are handled by the paragraph wrapper
        }
        // Alignment commands - set current paragraph alignment
        else if (strcmp(cmd_name, "centering") == 0) {
            current_alignment = ALIGN_CENTERING;
            return;  // No HTML output, just sets state
        }
        else if (strcmp(cmd_name, "raggedright") == 0) {
            current_alignment = ALIGN_RAGGEDRIGHT;
            return;  // No HTML output, just sets state
        }
        else if (strcmp(cmd_name, "raggedleft") == 0) {
            current_alignment = ALIGN_RAGGEDLEFT;
            return;  // No HTML output, just sets state
        }
        else if (strcmp(cmd_name, "verb") == 0) {
            // \verb - verbatim text (LaTeX.js uses latex-verbatim class)
            stringbuf_append_str(html_buf, "<code class=\"latex-verbatim\">");
            process_element_content_simple(html_buf, elem, pool, depth, font_ctx);
            stringbuf_append_str(html_buf, "</code>");
        }
        else if (strcmp(cmd_name, "thinspace") == 0 || strcmp(cmd_name, ",") == 0) {
            // \thinspace or \, - thin space (LaTeX.js compatibility)
            stringbuf_append_str(html_buf, "\xE2\x80\x89"); // U+2009 THIN SPACE
            return;
        }
        else if (strcmp(cmd_name, "mbox") == 0 || strcmp(cmd_name, "makebox") == 0 || strcmp(cmd_name, "hbox") == 0) {
            // \mbox{content} - horizontal box, prevents line breaks and ligatures
            // Creates <span class="hbox"><span>content</span></span>
            stringbuf_append_str(html_buf, "<span class=\"hbox\"><span>");
            process_element_content_simple(html_buf, elem, pool, depth, font_ctx);
            stringbuf_append_str(html_buf, "</span></span>");
            return;
        }
        else if (strcmp(cmd_name, "literal") == 0) {
            // Render literal character content with HTML escaping
            // Extract the character from elem->items[0] (which should be a string)
            if (elem->items && elem->length > 0) {
                Item content_item = elem->items[0];
                if (content_item.type_id() == LMD_TYPE_STRING) {
                    String* str = content_item.get_string();
                    if (str && str->chars) {
                        // Use append_escaped_text to properly escape HTML entities
                        append_escaped_text(html_buf, str->chars);
                    }
                }
            }

            // Literal characters don't need trailing space - whitespace in source is preserved
            return;
        }
        // Special character escapes (tree-sitter creates these as elements)
        else if (strcmp(cmd_name, "#") == 0) {
            stringbuf_append_char(html_buf, '#');
            return;
        }
        else if (strcmp(cmd_name, "$") == 0) {
            stringbuf_append_char(html_buf, '$');
            return;
        }
        else if (strcmp(cmd_name, "%") == 0) {
            stringbuf_append_char(html_buf, '%');
            return;
        }
        else if (strcmp(cmd_name, "&") == 0) {
            stringbuf_append_str(html_buf, "&amp;");
            return;
        }
        else if (strcmp(cmd_name, "_") == 0) {
            stringbuf_append_char(html_buf, '_');
            return;
        }
        else if (strcmp(cmd_name, "{") == 0) {
            stringbuf_append_char(html_buf, '{');
            return;
        }
        else if (strcmp(cmd_name, "}") == 0) {
            stringbuf_append_char(html_buf, '}');
            return;
        }
        else if (strcmp(cmd_name, "^") == 0) {
            stringbuf_append_char(html_buf, '^');
            return;
        }
        else if (strcmp(cmd_name, "~") == 0) {
            stringbuf_append_char(html_buf, '~');
            return;
        }
        else if (strcmp(cmd_name, "-") == 0) {
            // Soft hyphen for line breaking
            stringbuf_append_str(html_buf, "\xC2\xAD"); // U+00AD SOFT HYPHEN
            return;
        }
        else if (strcmp(cmd_name, "textbackslash") == 0) {
            stringbuf_append_char(html_buf, '\\'); // Render backslash
            stringbuf_append_str(html_buf, "\u200B"); // Add ZWSP for word boundary (matches latex-js)
            // Output any content directly (e.g., preserved trailing space from \textbackslash{})
            for (size_t i = 0; i < elem->length; i++) {
                Item child = elem->items[i];
                if (get_type_id(child) == LMD_TYPE_STRING) {
                    String* str = child.get_string();
                    stringbuf_append_str_n(html_buf, str->chars, str->len);
                }
            }
            return;
        }
        else if (strcmp(cmd_name, "section") == 0 || strcmp(cmd_name, "subsection") == 0 || 
                 strcmp(cmd_name, "subsubsection") == 0) {
            // Tree-sitter creates section elements with: \section command, title in curly_group, and content
            // We need to extract the title and output just the header, then process remaining content outside
            
            // Extract title from the curly_group (second child)
            StringBuf* title_buf = stringbuf_new(pool);
            
            if (elem->items && elem->length > 1) {
                // Second child should be curly_group with title
                if (get_type_id(elem->items[1]) == LMD_TYPE_ELEMENT) {
                    Element* title_elem = elem->items[1].element;
                    // Extract text from title element without processing ZWSP or other formatting
                    if (title_elem && title_elem->items && title_elem->length > 0) {
                        for (int64_t j = 0; j < title_elem->length; j++) {
                            Item title_child = title_elem->items[j];
                            if (get_type_id(title_child) == LMD_TYPE_STRING) {
                                String* str = title_child.get_string();
                                if (str && str->chars) {
                                    stringbuf_append_str(title_buf, str->chars);
                                }
                            }
                        }
                    }
                }
            }
            
            // Output the section header
            const char* tag = "h2";  // Default to h2 for section
            if (strcmp(cmd_name, "subsection") == 0) tag = "div";
            else if (strcmp(cmd_name, "subsubsection") == 0) tag = "div";
            
            // Increment section counter and output header
            global_section_id++;
            stringbuf_append_str(html_buf, "<");
            stringbuf_append_str(html_buf, tag);
            if (strcmp(cmd_name, "subsection") == 0) {
                stringbuf_append_str(html_buf, " class=\"latex-subsection\"");
            } else if (strcmp(cmd_name, "subsubsection") == 0) {
                stringbuf_append_str(html_buf, " class=\"latex-subsubsection\"");
            } else {
                char id_buf[32];
                snprintf(id_buf, sizeof(id_buf), " id=\"sec-%d\"", global_section_id);
                stringbuf_append_str(html_buf, id_buf);
            }
            stringbuf_append_str(html_buf, ">");
            
            // Output section number (simplified - just use global counter for now)
            if (strcmp(cmd_name, "section") == 0) {
                char num_buf[16];
                snprintf(num_buf, sizeof(num_buf), "%d ", global_section_id);
                stringbuf_append_str(html_buf, num_buf);
            }
            
            // Output title
            if (title_buf->length > 0) {
                String* title_str = stringbuf_to_string(title_buf);
                if (title_str && title_str->chars) {
                    append_escaped_text(html_buf, title_str->chars);
                }
            }
            
            stringbuf_free(title_buf);
            
            stringbuf_append_str(html_buf, "</");
            stringbuf_append_str(html_buf, tag);
            stringbuf_append_str(html_buf, ">\n");
            
            // Process remaining content (everything after title) as normal document content
            // This should be wrapped in paragraphs as needed
            bool in_para = false;
            for (int64_t i = 2; i < elem->length; i++) {
                Item content_item = elem->items[i];
                TypeId content_type = get_type_id(content_item);
                
                // Check if it's a block element
                bool is_block = false;
                if (content_type == LMD_TYPE_ELEMENT) {
                    Element* content_elem = content_item.element;
                    if (content_elem && content_elem->type) {
                        TypeElmt* content_elmt_type = (TypeElmt*)content_elem->type;
                        if (content_elmt_type && content_elmt_type->name.str) {
                            char content_cmd[64];
                            int content_name_len = content_elmt_type->name.length < 63 ? content_elmt_type->name.length : 63;
                            strncpy(content_cmd, content_elmt_type->name.str, content_name_len);
                            content_cmd[content_name_len] = '\0';
                            is_block = is_block_level_element(content_cmd);
                        }
                    }
                }
                
                // Manage paragraphs
                if (is_block && in_para) {
                    stringbuf_append_str(html_buf, "</p>\n");
                    in_para = false;
                }
                if (!is_block && !in_para) {
                    stringbuf_append_str(html_buf, "<p>");
                    in_para = true;
                }
                
                process_latex_element(html_buf, content_item, pool, depth + 1, font_ctx);
            }
            
            if (in_para) {
                stringbuf_append_str(html_buf, "</p>");
            }
            return;
        }
        else if (strcmp(cmd_name, "item") == 0) {
            // Item should be processed within itemize/enumerate context
            // If we get here directly, use default formatting
            process_item(html_buf, elem, pool, depth, font_ctx, 0, false, 0);
        }
        else if (strcmp(cmd_name, "itemize") == 0) {
            // printf("DEBUG: Processing itemize environment directly\n");
            process_itemize(html_buf, elem, pool, depth, font_ctx, 0);
        }
        else if (strcmp(cmd_name, "enumerate") == 0) {
            // printf("DEBUG: Processing enumerate environment directly\n");
            int counter = 0;
            process_enumerate(html_buf, elem, pool, depth, font_ctx, 0, counter);
        }
        else if (strcmp(cmd_name, "description") == 0) {
            process_description(html_buf, elem, pool, depth, font_ctx);
        }
        else if (strcmp(cmd_name, "quad") == 0) {
            // \quad - em space (U+2003)
            stringbuf_append_str(html_buf, "\xE2\x80\x83");
            return;
        }
        else if (strcmp(cmd_name, "qquad") == 0) {
            // \qquad - two em spaces
            stringbuf_append_str(html_buf, "\xE2\x80\x83\xE2\x80\x83");
            return;
        }
        else if (strcmp(cmd_name, "enspace") == 0) {
            // \enspace - en space (U+2002)
            stringbuf_append_str(html_buf, "\xE2\x80\x82");
            return;
        }
        else if (strcmp(cmd_name, "negthinspace") == 0) {
            // \! - negative thin space (output span with negthinspace class)
            stringbuf_append_str(html_buf, "<span class=\"negthinspace\"></span>");
            return;
        }
        else if (strcmp(cmd_name, "hspace") == 0) {
            // \hspace{dimension} - horizontal space with specific dimension
            if (elem->items && elem->length > 0) {
                Item arg_item = elem->items[0];
                if (get_type_id(arg_item) == LMD_TYPE_ELEMENT) {
                    Element* arg_elem = arg_item.element;
                    // Extract dimension from argument
                    if (arg_elem->items && arg_elem->length > 0) {
                        Item dim_item = arg_elem->items[0];
                        if (get_type_id(dim_item) == LMD_TYPE_STRING) {
                            String* dim_str = dim_item.get_string();
                            if (dim_str && dim_str->len > 0) {
                                // Convert LaTeX dimension to pixels
                                double pixels = latex_dim_to_pixels(dim_str->chars);

                                char px_str[32];
                                snprintf(px_str, sizeof(px_str), "%.3fpx", pixels);

                                stringbuf_append_str(html_buf, "<span style=\"margin-right:");
                                stringbuf_append_str(html_buf, px_str);
                                stringbuf_append_str(html_buf, "\"></span>");
                                return;
                            }
                        }
                    }
                }
            }
            // If we couldn't extract dimension, skip
            return;
        }
        else if (strcmp(cmd_name, "empty") == 0) {
            // \begin{empty}...\end{empty} - outputs content with ZWSP at end
            process_element_content_simple(html_buf, elem, pool, depth, font_ctx);
            stringbuf_append_str(html_buf, "\xE2\x80\x8B"); // ZWSP for word boundary
            return;
        }
        else if (strcmp(cmd_name, "relax") == 0) {
            // No-op command that produces nothing
            return;
        }
        else if (strcmp(cmd_name, "smallskip") == 0) {
            // \smallskip - small vertical space (inline if in paragraph)
            stringbuf_append_str(html_buf, "<span class=\"vspace-inline smallskip\"></span>");
            return;
        }
        else if (strcmp(cmd_name, "medskip") == 0) {
            // \medskip - medium vertical space (between paragraphs)
            stringbuf_append_str(html_buf, "<span class=\"vspace medskip\"></span>");
            return;
        }
        else if (strcmp(cmd_name, "bigskip") == 0) {
            // \bigskip - large vertical space (between paragraphs)
            stringbuf_append_str(html_buf, "<span class=\"vspace bigskip\"></span>");
            return;
        }
        else if (strcmp(cmd_name, "smallbreak") == 0) {
            // \smallbreak - small vertical space with paragraph break
            stringbuf_append_str(html_buf, "<span class=\"vspace smallskip\"></span>");
            return;
        }
        else if (strcmp(cmd_name, "medbreak") == 0) {
            // \medbreak - medium vertical space with paragraph break
            stringbuf_append_str(html_buf, "<span class=\"vspace medskip\"></span>");
            return;
        }
        else if (strcmp(cmd_name, "bigbreak") == 0) {
            // \bigbreak - large vertical space with paragraph break
            stringbuf_append_str(html_buf, "<span class=\"vspace bigskip\"></span>");
            return;
        }
        // Font declaration commands - change font state for subsequent text
        else if (strcmp(cmd_name, "bfseries") == 0) {
            font_ctx->series = FONT_SERIES_BOLD;
            return;
        }
        else if (strcmp(cmd_name, "mdseries") == 0) {
            font_ctx->series = FONT_SERIES_NORMAL;
            return;
        }
        else if (strcmp(cmd_name, "itshape") == 0) {
            font_ctx->shape = FONT_SHAPE_ITALIC;
            return;
        }
        else if (strcmp(cmd_name, "slshape") == 0) {
            font_ctx->shape = FONT_SHAPE_SLANTED;
            return;
        }
        else if (strcmp(cmd_name, "scshape") == 0) {
            font_ctx->shape = FONT_SHAPE_SMALL_CAPS;
            return;
        }
        else if (strcmp(cmd_name, "upshape") == 0) {
            font_ctx->shape = FONT_SHAPE_UPRIGHT;
            return;
        }
        else if (strcmp(cmd_name, "rmfamily") == 0) {
            font_ctx->family = FONT_FAMILY_ROMAN;
            return;
        }
        else if (strcmp(cmd_name, "sffamily") == 0) {
            font_ctx->family = FONT_FAMILY_SANS_SERIF;
            return;
        }
        else if (strcmp(cmd_name, "ttfamily") == 0) {
            font_ctx->family = FONT_FAMILY_TYPEWRITER;
            return;
        }
        else if (strcmp(cmd_name, "em") == 0) {
            // Toggle between italic and upright
            if (font_ctx->shape == FONT_SHAPE_UPRIGHT) {
                font_ctx->shape = FONT_SHAPE_ITALIC;
                font_ctx->em_active = true;
            } else {
                font_ctx->shape = FONT_SHAPE_UPRIGHT;
                font_ctx->em_active = false;
            }
            return;
        }
        else if (strcmp(cmd_name, "normalfont") == 0) {
            font_ctx->series = FONT_SERIES_NORMAL;
            font_ctx->shape = FONT_SHAPE_UPRIGHT;
            font_ctx->family = FONT_FAMILY_ROMAN;
            font_ctx->em_active = false;
            return;
        }
        // TeX/LaTeX logos
        else if (strcmp(cmd_name, "TeX") == 0) {
            stringbuf_append_str(html_buf, "<span class=\"tex\">T<span class=\"e\">e</span>X</span>");
            return;
        }
        else if (strcmp(cmd_name, "LaTeX") == 0) {
            stringbuf_append_str(html_buf, "<span class=\"latex\">L<span class=\"a\">a</span>T<span class=\"e\">e</span>X</span>");
            return;
        }
        else if (strcmp(cmd_name, "LaTeXe") == 0) {
            stringbuf_append_str(html_buf, "<span class=\"latex\">L<span class=\"a\">a</span>T<span class=\"e\">e</span>X 2<span class=\"epsilon\">\xCE\xB5</span></span>");
            return;
        }
        else if (strcmp(cmd_name, "XeTeX") == 0) {
            stringbuf_append_str(html_buf, "<span class=\"tex\">X<span class=\"xe\">&#x018e;</span>T<span class=\"e\">e</span>X</span>");
            return;
        }
        else if (strcmp(cmd_name, "XeLaTeX") == 0) {
            stringbuf_append_str(html_buf, "<span class=\"latex\">X<span class=\"xe\">&#x018e;</span>L<span class=\"a\">a</span>T<span class=\"e\">e</span>X</span>");
            return;
        }
        else if (strcmp(cmd_name, "LuaTeX") == 0) {
            stringbuf_append_str(html_buf, "<span class=\"tex\">Lua<span class=\"lua\"></span>T<span class=\"e\">e</span>X</span>");
            return;
        }
        else if (strcmp(cmd_name, "LuaLaTeX") == 0) {
            stringbuf_append_str(html_buf, "<span class=\"latex\">Lua<span class=\"lua\"></span>L<span class=\"a\">a</span>T<span class=\"e\">e</span>X</span>");
            return;
        }
        else {
            // Check symbol table for known symbols
            const char* symbol = lookup_symbol(cmd_name);
            if (symbol) {
                stringbuf_append_str(html_buf, symbol);
                return;
            }
            // Generic element - process children
            // printf("DEBUG: Processing generic element: '%s' (length: %d)\n", cmd_name, name_len);
            // printf("DEBUG: Checking texttt comparison: strcmp('%s', 'texttt') = %d\n", cmd_name, strcmp(cmd_name, "texttt"));
            process_element_content(html_buf, elem, pool, depth, font_ctx);
        }
    }
    else if (type == LMD_TYPE_STRING) {
        // Handle text content with ligature conversion
        String* str = item.get_string();
        if (str && str->len > 0) {
            // Skip space if buffer already ends with a space (collapse multiple spaces)
            if (str->len == 1 && str->chars[0] == ' ') {
                if (html_buf->length > 0 && html_buf->str && html_buf->str->chars) {
                    char last_char = html_buf->str->chars[html_buf->length - 1];
                    if (last_char == ' ' || last_char == '\n' || last_char == '\t' || last_char == '>') {
                        // Skip this space - already have whitespace or just started a tag
                        return;
                    }
                }
            }
            // Apply ligature conversion based on font context
            bool is_tt = (font_ctx && font_ctx->family == FONT_FAMILY_TYPEWRITER);
            append_escaped_text_with_ligatures(html_buf, str->chars, is_tt);
        }
    }
    else if (type == LMD_TYPE_SYMBOL) {
        // Handle special character symbols from tree-sitter parser
        Symbol* sym = (Symbol*)(item.symbol_ptr);
        if (sym && sym->chars) {
            // Map LaTeX escaped characters to their actual characters
            if (strcmp(sym->chars, "#") == 0) {
                stringbuf_append_str(html_buf, "#");
            } else if (strcmp(sym->chars, "$") == 0) {
                stringbuf_append_str(html_buf, "$");
            } else if (strcmp(sym->chars, "%") == 0) {
                stringbuf_append_str(html_buf, "%");
            } else if (strcmp(sym->chars, "&") == 0) {
                stringbuf_append_str(html_buf, "&amp;");
            } else if (strcmp(sym->chars, "_") == 0) {
                stringbuf_append_str(html_buf, "_");
            } else if (strcmp(sym->chars, "{") == 0) {
                stringbuf_append_str(html_buf, "{");
            } else if (strcmp(sym->chars, "}") == 0) {
                stringbuf_append_str(html_buf, "}");
            } else if (strcmp(sym->chars, "^") == 0) {
                stringbuf_append_str(html_buf, "^");
            } else if (strcmp(sym->chars, "~") == 0) {
                stringbuf_append_str(html_buf, "~");
            } else if (strcmp(sym->chars, "\\") == 0 || strcmp(sym->chars, "textbackslash") == 0) {
                stringbuf_append_str(html_buf, "\\");
            } else if (strcmp(sym->chars, "parbreak") == 0) {
                // parbreak should be handled at document level, ignore here
                return;
            } else if (strcmp(sym->chars, ",") == 0 || strcmp(sym->chars, "thinspace") == 0) {
                // \, or \thinspace - thin space (U+2009)
                stringbuf_append_str(html_buf, "\xE2\x80\x89"); // U+2009 THIN SPACE
            } else {
                // Unknown symbol - output as-is
                stringbuf_append_str(html_buf, sym->chars);
            }
        }
    }
    else if (type == LMD_TYPE_ARRAY) {
        // Process array of elements
        Array* arr = item.array;
        if (arr && arr->items) {
            for (int i = 0; i < arr->length; i++) {
                process_latex_element(html_buf, arr->items[i], pool, depth, font_ctx);
            }
        }
    }
    else if (type == LMD_TYPE_LIST) {
        // Process list of elements (tree-sitter creates lists for mixed content)
        List* list = item.list;
        if (list && list->items) {
            for (int i = 0; i < list->length; i++) {
                process_latex_element(html_buf, list->items[i], pool, depth, font_ctx);
            }
        }
    }
}

// Check if an element is a block-level element that should not be wrapped in paragraphs
static bool is_block_element(Item item) {
    TypeId type = get_type_id(item);
    if (type != LMD_TYPE_ELEMENT) {
        return false;
    }

    Element* elem = item.element;
    if (!elem || !elem->type) {
        return false;
    }

    TypeElmt* elmt_type = (TypeElmt*)elem->type;
    if (!elmt_type || !elmt_type->name.str) {
        return false;
    }

    // Convert name to null-terminated string
    char cmd_name[64];
    int name_len = elmt_type->name.length < 63 ? elmt_type->name.length : 63;
    strncpy(cmd_name, elmt_type->name.str, name_len);
    cmd_name[name_len] = '\0';

    // Block-level elements that should not be wrapped in paragraphs
    return (strcmp(cmd_name, "chapter") == 0 ||
            strcmp(cmd_name, "section") == 0 ||
            strcmp(cmd_name, "subsection") == 0 ||
            strcmp(cmd_name, "subsubsection") == 0 ||
            strcmp(cmd_name, "itemize") == 0 ||
            strcmp(cmd_name, "enumerate") == 0 ||
            strcmp(cmd_name, "description") == 0 ||
            strcmp(cmd_name, "quote") == 0 ||
            strcmp(cmd_name, "quotation") == 0 ||
            strcmp(cmd_name, "verse") == 0 ||
            strcmp(cmd_name, "verbatim") == 0 ||
            strcmp(cmd_name, "center") == 0 ||
            strcmp(cmd_name, "flushleft") == 0 ||
            strcmp(cmd_name, "flushright") == 0 ||
            strcmp(cmd_name, "title") == 0 ||
            strcmp(cmd_name, "author") == 0 ||
            strcmp(cmd_name, "date") == 0 ||
            strcmp(cmd_name, "maketitle") == 0 ||
            strcmp(cmd_name, "document") == 0 ||
            strcmp(cmd_name, "documentclass") == 0 ||
            strcmp(cmd_name, "medskip") == 0 ||
            strcmp(cmd_name, "bigskip") == 0 ||
            strcmp(cmd_name, "medbreak") == 0 ||
            strcmp(cmd_name, "bigbreak") == 0 ||
            strcmp(cmd_name, "par") == 0);
}

// Check if an element is a "silent" command that doesn't produce visual output
// These commands should not trigger paragraph creation
static bool is_silent_command(Item item) {
    TypeId type = get_type_id(item);
    if (type != LMD_TYPE_ELEMENT) {
        return false;
    }

    Element* elem = item.element;
    if (!elem || !elem->type) {
        return false;
    }

    TypeElmt* elmt_type = (TypeElmt*)elem->type;
    if (!elmt_type || !elmt_type->name.str) {
        return false;
    }

    // Convert name to null-terminated string
    char cmd_name[64];
    int name_len = elmt_type->name.length < 63 ? elmt_type->name.length : 63;
    strncpy(cmd_name, elmt_type->name.str, name_len);
    cmd_name[name_len] = '\0';

    // Silent commands that don't produce output
    return (strcmp(cmd_name, "newcounter") == 0 ||
            strcmp(cmd_name, "setcounter") == 0 ||
            strcmp(cmd_name, "stepcounter") == 0 ||
            strcmp(cmd_name, "addtocounter") == 0 ||
            strcmp(cmd_name, "label") == 0 ||
            strcmp(cmd_name, "new_command_definition") == 0 ||
            strcmp(cmd_name, "renew_command_definition") == 0 ||
            strcmp(cmd_name, "newenvironment") == 0 ||
            strcmp(cmd_name, "renewenvironment") == 0 ||
            strcmp(cmd_name, "newlength") == 0 ||
            strcmp(cmd_name, "setlength") == 0 ||
            strcmp(cmd_name, "addtolength") == 0 ||
            strcmp(cmd_name, "settowidth") == 0 ||
            strcmp(cmd_name, "settoheight") == 0 ||
            strcmp(cmd_name, "settodepth") == 0);
}

// Process element content without paragraph wrapping (for titles, etc.)
static void process_element_content_simple(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx) {
    if (!elem || !elem->items) {
        return;
    }

    // Process element items, wrapping text in font spans as needed
    if (elem->length > 0 && elem->length < 1000) { // Reasonable limit
        bool font_span_open = false;

        for (int i = 0; i < elem->length; i++) {
            Item content_item = elem->items[i];
            TypeId item_type = get_type_id(content_item);

            // Before processing, check if we need to open a font span for text/textblocks
            if (needs_font_span(font_ctx) && !font_span_open && item_type == LMD_TYPE_STRING) {
                const char* css_class = get_font_css_class(font_ctx);
                stringbuf_append_str(html_buf, "<span class=\"");
                stringbuf_append_str(html_buf, css_class);
                stringbuf_append_str(html_buf, "\">");
                font_span_open = true;
            }

            // If font returned to default and span is open, close it
            if (!needs_font_span(font_ctx) && font_span_open) {
                stringbuf_append_str(html_buf, "</span>");
                font_span_open = false;
            }

            // Process the item (this may change font_ctx for declarations)
            process_latex_element(html_buf, content_item, pool, depth, font_ctx);
        }

        // Close any open font span at the end
        if (font_span_open) {
            stringbuf_append_str(html_buf, "</span>");
        }
    }
}

// Process element content with intelligent paragraph wrapping
static void process_element_content(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx) {
    if (!elem || !elem->items) {
        // printf("DEBUG: process_element_content - elem or items is null\n");
        return;
    }

    // printf("DEBUG: process_element_content - elem->length = %d\n", elem->length);

    // Process element items with intelligent paragraph grouping
    if (elem->length > 0 && elem->length < 1000) { // Reasonable limit
        bool in_paragraph = false;
        bool need_new_paragraph = false;
        bool font_span_open = false;
        bool next_paragraph_noindent = false;  // Track if next paragraph should have noindent class
        bool next_paragraph_continue = false;  // Track if next paragraph should have continue class (after lists)

        for (int i = 0; i < elem->length; i++) {
            // printf("DEBUG: Processing element content item %d\n", i);
            Item content_item = elem->items[i];
            TypeId item_type = get_type_id(content_item);
            // printf("DEBUG: Content item %d has type %d\n", i, item_type);

            // CRITICAL DEBUG: Check for invalid type values
            if (item_type > LMD_TYPE_ERROR) {
                log_error("ERROR: Invalid type %d detected in process_element_content! Max valid type is %d", item_type, LMD_TYPE_ERROR);
                continue; // Skip processing this invalid item
            }

            bool is_block = is_block_element(content_item);
            bool is_text = (item_type == LMD_TYPE_STRING);
            bool is_inline = (item_type == LMD_TYPE_ELEMENT && !is_block);

            // printf("DEBUG: is_block=%d, is_text=%d, is_inline=%d\n", is_block, is_text, is_inline);

            // Check if this is a paragraph break element or textblock or noindent
            bool is_par_break = false;
            bool is_textblock = false;
            bool is_noindent = false;
            
            // Check for parbreak symbol (tree-sitter creates these)
            if (item_type == LMD_TYPE_SYMBOL) {
                Symbol* sym = (Symbol*)(content_item.symbol_ptr);
                if (sym && strcmp(sym->chars, "parbreak") == 0) {
                    is_par_break = true;
                }
            }
            else if (item_type == LMD_TYPE_ELEMENT) {
                Element* elem_ptr = content_item.element;
                if (elem_ptr && elem_ptr->type) {
                    StrView elem_name = ((TypeElmt*)elem_ptr->type)->name;
                    // printf("DEBUG: Element name: '%.*s' (length: %zu)\n", (int)elem_name.length, elem_name.str, elem_name.length);
                    if ((elem_name.length == 3 && strncmp(elem_name.str, "par", 3) == 0) ||
                        (elem_name.length == 8 && strncmp(elem_name.str, "parbreak", 8) == 0)) {
                        is_par_break = true;
                        // printf("DEBUG: Detected par break element\n");
                    } else if (elem_name.length == 9 && strncmp(elem_name.str, "textblock", 9) == 0) {
                        is_textblock = true;
                        // printf("DEBUG: Detected textblock element\n");
                    } else if (elem_name.length == 8 && strncmp(elem_name.str, "noindent", 8) == 0) {
                        is_noindent = true;
                        // Only set noindent flag if next item is a plain string (direct content after noindent)
                        // If next item is textblock or another noindent, the noindent is "consumed"
                        // In LaTeX, \noindent followed by blank line means noindent is consumed by the blank line
                        bool should_set_noindent = false;  // Default to false
                        if (i + 1 < elem->length) {
                            Item next_item = elem->items[i + 1];
                            TypeId next_type = get_type_id(next_item);
                            // Only set if next is a plain string (direct content)
                            if (next_type == LMD_TYPE_STRING) {
                                should_set_noindent = true;
                                // printf("DEBUG: noindent followed by string - setting flag\n");
                            } else {
                                // printf("DEBUG: noindent NOT followed by string - not setting flag\n");
                            }
                        }
                        if (should_set_noindent) {
                            next_paragraph_noindent = true;
                        }
                    }
                }
            }

            // Skip noindent elements - they just set a flag for next paragraph
            if (is_noindent) {
                // printf("DEBUG: Finished processing element content item %d (noindent)\n", i);
                continue;
            }

            // Handle paragraph wrapping logic
            if (is_textblock) {
                // Process textblock: text + parbreak
                Element* textblock_elem = content_item.element;
                // printf("DEBUG: Processing textblock with length: %lld\n", textblock_elem ? textblock_elem->length : -1);
                if (textblock_elem && textblock_elem->items && textblock_elem->length >= 1) {
                    // Process the text part
                    Item text_item = textblock_elem->items[0];
                    // printf("DEBUG: First item type: %d (LMD_TYPE_STRING=%d)\n", get_type_id(text_item), LMD_TYPE_STRING);
                    if (get_type_id(text_item) == LMD_TYPE_STRING) {
                        // printf("DEBUG: Processing text item in textblock\n");
                    } else {
                        // printf("DEBUG: First item is not a string, processing as element\n");
                        // Process as element instead
                        if (!in_paragraph || need_new_paragraph) {
                            if (need_new_paragraph && in_paragraph) {
                                close_paragraph(html_buf, true);
                            }
                            open_paragraph(html_buf, next_paragraph_noindent, next_paragraph_continue);
                            next_paragraph_noindent = false;
                            next_paragraph_continue = false;
                            in_paragraph = true;
                            need_new_paragraph = false;
                        }
                        process_latex_element(html_buf, text_item, pool, depth, font_ctx);
                    }

                    // Original string processing
                    if (get_type_id(text_item) == LMD_TYPE_STRING) {
                        if (!in_paragraph || need_new_paragraph) {
                            if (need_new_paragraph && in_paragraph) {
                                // Close font span before closing paragraph
                                if (font_span_open) {
                                    stringbuf_append_str(html_buf, "</span>");
                                    font_span_open = false;
                                }
                                close_paragraph(html_buf, true);
                            }
                            open_paragraph(html_buf, next_paragraph_noindent, next_paragraph_continue);
                            next_paragraph_noindent = false;
                            next_paragraph_continue = false;
                            in_paragraph = true;
                            need_new_paragraph = false;
                        }

                        // Check if we need to open a font span for text
                        if (needs_font_span(font_ctx) && !font_span_open) {
                            const char* css_class = get_font_css_class(font_ctx);
                            stringbuf_append_str(html_buf, "<span class=\"");
                            stringbuf_append_str(html_buf, css_class);
                            stringbuf_append_str(html_buf, "\">");
                            font_span_open = true;
                        }

                        // If font returned to default and span is open, close it
                        if (!needs_font_span(font_ctx) && font_span_open) {
                            stringbuf_append_str(html_buf, "</span>");
                            font_span_open = false;
                        }

                        process_latex_element(html_buf, text_item, pool, depth, font_ctx);
                    }

                    // Check if there's a parbreak (should be second element)
                    if (textblock_elem->length >= 2) {
                        Item parbreak_item = textblock_elem->items[1];
                        if (get_type_id(parbreak_item) == LMD_TYPE_ELEMENT) {
                            Element* parbreak_elem = parbreak_item.element;
                            if (parbreak_elem && parbreak_elem->type) {
                                StrView parbreak_name = ((TypeElmt*)parbreak_elem->type)->name;
                                if (parbreak_name.length == 8 && strncmp(parbreak_name.str, "parbreak", 8) == 0) {
                                    // Close font span before closing paragraph
                                    if (font_span_open) {
                                        stringbuf_append_str(html_buf, "</span>");
                                        font_span_open = false;
                                    }
                                    // Close current paragraph and force new paragraph for next content
                                    if (in_paragraph) {
                                        close_paragraph(html_buf, true);
                                        in_paragraph = false;
                                    }
                                    need_new_paragraph = true;
                                    // Clear noindent and continue flags - a parbreak consumes any pending modifiers
                                    next_paragraph_noindent = false;
                                    next_paragraph_continue = false;
                                }
                            }
                        }
                    }
                }
            } else if (is_par_break) {
                // Close font span before closing paragraph
                if (font_span_open) {
                    stringbuf_append_str(html_buf, "</span>");
                    font_span_open = false;
                }
                // Close current paragraph and force new paragraph for next content
                if (in_paragraph) {
                    close_paragraph(html_buf, true);
                    in_paragraph = false;
                }
                need_new_paragraph = true;
                // Clear noindent and continue flags - a parbreak consumes any pending modifiers
                next_paragraph_noindent = false;
                next_paragraph_continue = false;
                // Don't process the par element itself, just use it as a break marker
            } else if (is_block) {
                // Close font span before block element
                if (font_span_open) {
                    stringbuf_append_str(html_buf, "</span>");
                    font_span_open = false;
                }
                // Close any open paragraph before block element
                if (in_paragraph) {
                    close_paragraph(html_buf, true);
                    in_paragraph = false;
                }

                // Check if this is a list environment or alignment environment - set continue flag after processing
                bool is_list_env = false;
                if (item_type == LMD_TYPE_ELEMENT) {
                    Element* block_elem = content_item.element;
                    if (block_elem && block_elem->type) {
                        StrView bname = ((TypeElmt*)block_elem->type)->name;
                        if ((bname.length == 7 && strncmp(bname.str, "itemize", 7) == 0) ||
                            (bname.length == 9 && strncmp(bname.str, "enumerate", 9) == 0) ||
                            (bname.length == 11 && strncmp(bname.str, "description", 11) == 0) ||
                            (bname.length == 5 && strncmp(bname.str, "quote", 5) == 0) ||
                            (bname.length == 9 && strncmp(bname.str, "quotation", 9) == 0) ||
                            (bname.length == 5 && strncmp(bname.str, "verse", 5) == 0) ||
                            (bname.length == 6 && strncmp(bname.str, "center", 6) == 0) ||
                            (bname.length == 9 && strncmp(bname.str, "flushleft", 9) == 0) ||
                            (bname.length == 10 && strncmp(bname.str, "flushright", 10) == 0)) {
                            is_list_env = true;
                        }
                    }
                }

                // Process block element directly
                process_latex_element(html_buf, content_item, pool, depth, font_ctx);

                // Set continue flag after processing list/alignment environments
                if (is_list_env) {
                    next_paragraph_continue = true;
                }
            } else if (is_text || is_inline) {
                // printf("DEBUG: Entering is_text || is_inline branch\n");
                // Check if this is a silent command that doesn't produce output
                // These should not trigger paragraph creation
                bool is_silent = is_inline && is_silent_command(content_item);
                // printf("DEBUG: is_silent=%d\n", is_silent);
                if (is_silent) {
                    // Process the silent command without creating a paragraph
                    process_latex_element(html_buf, content_item, pool, depth, font_ctx);
                    // printf("DEBUG: Finished processing element content item %d (silent command)\n", i);
                    continue;
                }

                // Skip whitespace-only text strings at the start of a new paragraph
                if (is_text && need_new_paragraph) {
                    // printf("DEBUG: Checking text for whitespace at paragraph start, need_new_paragraph=%d\n", need_new_paragraph);
                    String* str = content_item.get_string();
                    if (str && str->chars) {
                        // printf("DEBUG: Text content: '%s' (len=%d)\n", str->chars, (int)str->len);
                        // Check if string is whitespace-only
                        bool is_whitespace_only = true;
                        for (const char* p = str->chars; *p; p++) {
                            if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
                                is_whitespace_only = false;
                                break;
                            }
                        }
                        if (is_whitespace_only) {
                            // printf("DEBUG: Skipping whitespace-only string at paragraph start\n");
                            continue;  // Skip this whitespace string
                        }
                    }
                }

                // Handle paragraph creation based on context
                // printf("DEBUG: in_paragraph=%d, need_new_paragraph=%d\n", in_paragraph, need_new_paragraph);
                if (!in_paragraph || need_new_paragraph) {
                    // printf("DEBUG: Creating paragraph\n");
                    if (need_new_paragraph && in_paragraph) {
                        // Close font span before closing paragraph
                        if (font_span_open) {
                            stringbuf_append_str(html_buf, "</span>");
                            font_span_open = false;
                        }
                        // This shouldn't happen since par breaks close paragraphs
                        close_paragraph(html_buf, true);
                    }
                    open_paragraph(html_buf, next_paragraph_noindent, next_paragraph_continue);
                    // printf("DEBUG: After open_paragraph, len=%zu\n", html_buf->length);
                    next_paragraph_noindent = false;
                    next_paragraph_continue = false;
                    in_paragraph = true;
                    need_new_paragraph = false;
                }

                // Check if we need to open a font span for text
                if (is_text && needs_font_span(font_ctx) && !font_span_open) {
                    const char* css_class = get_font_css_class(font_ctx);
                    stringbuf_append_str(html_buf, "<span class=\"");
                    stringbuf_append_str(html_buf, css_class);
                    stringbuf_append_str(html_buf, "\">");
                    font_span_open = true;
                }

                // If font returned to default and span is open, close it
                if (!needs_font_span(font_ctx) && font_span_open) {
                    stringbuf_append_str(html_buf, "</span>");
                    font_span_open = false;
                }

                // Process inline content (both text and inline elements)
                process_latex_element(html_buf, content_item, pool, depth, font_ctx);
                // printf("DEBUG: After process_latex_element, len=%zu\n", html_buf->length);
            } else {
                // Unknown content type - treat as inline if we're in a paragraph context
                if (!in_paragraph) {
                    open_paragraph(html_buf, next_paragraph_noindent, next_paragraph_continue);
                    next_paragraph_noindent = false;
                    next_paragraph_continue = false;
                    in_paragraph = true;
                }
                process_latex_element(html_buf, content_item, pool, depth, font_ctx);
            }

            // printf("DEBUG: Finished processing element content item %d\n", i);
        }

        // Close any open font span before closing paragraph
        if (font_span_open) {
            stringbuf_append_str(html_buf, "</span>");
        }

        // Close any remaining open paragraph
        if (in_paragraph) {
            // printf("DEBUG: Closing remaining paragraph, len before=%zu\n", html_buf->length);
            close_paragraph(html_buf, true);
            // printf("DEBUG: After close_paragraph, len=%zu\n", html_buf->length);
        }
    }
    // printf("DEBUG: process_element_content completed\n");
}

// Helper function for recursive text extraction from elements
static void extract_text_recursive(StringBuf* buf, Element* elem, Pool* pool) {
    if (!elem || !elem->items) return;

    for (int i = 0; i < elem->length; i++) {
        Item child = elem->items[i];
        TypeId type = get_type_id(child);

        if (type == LMD_TYPE_STRING) {
            String* str = child.get_string();
            if (str && str->chars) {
                stringbuf_append_str(buf, str->chars);
            }
        } else if (type == LMD_TYPE_ELEMENT) {
            Element* child_elem = child.element;
            extract_text_recursive(buf, child_elem, pool);
        }
    }
}

// Process title command
static void process_title(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    if (!elem) return;

    // Use temporary buffer to extract all text recursively
    StringBuf* temp_buf = stringbuf_new(pool);
    extract_text_recursive(temp_buf, elem, pool);

    String* title_str = stringbuf_to_string(temp_buf);
    if (title_str && title_str->len > 0) {
        doc_state.title = strdup(title_str->chars);
    }

    stringbuf_free(temp_buf);
}

// Process author command
static void process_author(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    if (!elem) return;

    // Use temporary buffer to extract all text recursively
    StringBuf* temp_buf = stringbuf_new(pool);
    extract_text_recursive(temp_buf, elem, pool);

    String* author_str = stringbuf_to_string(temp_buf);
    if (author_str && author_str->len > 0) {
        doc_state.author = strdup(author_str->chars);
    }

    stringbuf_free(temp_buf);
}

// Process date command
static void process_date(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    if (!elem) return;

    // Use temporary buffer to extract all text recursively
    StringBuf* temp_buf = stringbuf_new(pool);
    extract_text_recursive(temp_buf, elem, pool);

    String* date_str = stringbuf_to_string(temp_buf);
    if (date_str && date_str->len > 0) {
        doc_state.date = strdup(date_str->chars);
    }

    stringbuf_free(temp_buf);
}

// Process maketitle command
static void process_maketitle(StringBuf* html_buf, Pool* pool, int depth) {
    if (doc_state.title) {
        stringbuf_append_str(html_buf, "<div class=\"latex-title\">");
        append_escaped_text(html_buf, doc_state.title);
        stringbuf_append_str(html_buf, "</div>\n");
    }

    if (doc_state.author) {
        stringbuf_append_str(html_buf, "<div class=\"latex-author\">");
        append_escaped_text(html_buf, doc_state.author);
        stringbuf_append_str(html_buf, "</div>\n");
    }

    if (doc_state.date) {
        stringbuf_append_str(html_buf, "<div class=\"latex-date\">");
        append_escaped_text(html_buf, doc_state.date);
        stringbuf_append_str(html_buf, "</div>\n");
    }
}

// Process chapter command
// Output: <h1 id="sec-N"><div>Chapter N</div>Title</h1>
static void process_chapter(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx) {
    if (!elem) return;

    chapter_counter++;
    section_counter = 0;  // Reset section counter for new chapter
    global_section_id++;

    // Start h1 with id
    char id_buf[64];
    snprintf(id_buf, sizeof(id_buf), "<h1 id=\"sec-%d\">", global_section_id);
    stringbuf_append_str(html_buf, id_buf);

    // Add chapter label div
    char chapter_label[64];
    snprintf(chapter_label, sizeof(chapter_label), "<div>Chapter %d</div>", chapter_counter);
    stringbuf_append_str(html_buf, chapter_label);

    // Process chapter title without paragraph wrapping
    process_element_content_simple(html_buf, elem, pool, depth, font_ctx);

    stringbuf_append_str(html_buf, "</h1>\n");
}

// Process section commands
// In book mode (has chapters): <h2 id="sec-N">X.Y Title</h2>
// In article mode (no chapters): <h2 id="sec-N">Y Title</h2>
static void process_section_h2(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx) {
    if (!elem) return;

    section_counter++;
    global_section_id++;

    // Start h2 with id
    char id_buf[64];
    snprintf(id_buf, sizeof(id_buf), "<h2 id=\"sec-%d\">", global_section_id);
    stringbuf_append_str(html_buf, id_buf);

    // Add section number prefix followed by em space (U+2003)
    // In book mode: chapter.section (e.g., "1.1")
    // In article mode: just section (e.g., "1")
    char section_num[32];
    if (chapter_counter > 0) {
        snprintf(section_num, sizeof(section_num), "%d.%d", chapter_counter, section_counter);
    } else {
        snprintf(section_num, sizeof(section_num), "%d", section_counter);
    }
    stringbuf_append_str(html_buf, section_num);
    stringbuf_append_str(html_buf, "\xE2\x80\x83");  // UTF-8 encoded EM SPACE (U+2003)

    // Process section title without paragraph wrapping
    process_element_content_simple(html_buf, elem, pool, depth, font_ctx);

    stringbuf_append_str(html_buf, "</h2>\n");
}

// Process section commands (legacy - uses div)
static void process_section(StringBuf* html_buf, Element* elem, Pool* pool, int depth, const char* css_class, FontContext* font_ctx) {
    if (!elem) return;

    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "<div class=\"");
    stringbuf_append_str(html_buf, css_class);
    stringbuf_append_str(html_buf, "\">");

    // Process section title without paragraph wrapping
    process_element_content_simple(html_buf, elem, pool, depth, font_ctx);

    stringbuf_append_str(html_buf, "</div>\n");
}

// Process environments (begin/end blocks)
static void process_environment(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx) {
    if (!elem) return;

    // Get environment name from first child
    if (elem->items && elem->length > 0) {
        TypeId content_type = get_type_id(elem->items[0]);
        if (content_type == LMD_TYPE_STRING) {
            String* env_name = elem->items[0].get_string();
            if (env_name && env_name->chars && env_name->len > 0) {
                if (strcmp(env_name->chars, "document") == 0) {
                    doc_state.in_document = true;
                    // Process document content - find the matching content
                    // This is a simplified approach
                    return;
                }
                else if (strcmp(env_name->chars, "itemize") == 0) {
                    process_itemize(html_buf, elem, pool, depth, font_ctx, 0);
                }
                else if (strcmp(env_name->chars, "enumerate") == 0) {
                    int counter = 0;
                    process_enumerate(html_buf, elem, pool, depth, font_ctx, 0, counter);
                }
                else if (strcmp(env_name->chars, "description") == 0) {
                    process_description(html_buf, elem, pool, depth, font_ctx);
                }
                else if (strcmp(env_name->chars, "quote") == 0) {
                    process_quote(html_buf, elem, pool, depth, font_ctx, "quote");
                }
                else if (strcmp(env_name->chars, "quotation") == 0) {
                    process_quote(html_buf, elem, pool, depth, font_ctx, "quotation");
                }
                else if (strcmp(env_name->chars, "verse") == 0) {
                    process_quote(html_buf, elem, pool, depth, font_ctx, "verse");
                }
                else if (strcmp(env_name->chars, "verbatim") == 0) {
                    process_verbatim(html_buf, elem, pool, depth);
                }
                else if (strcmp(env_name->chars, "center") == 0) {
                    process_alignment_environment(html_buf, elem, pool, depth, "latex-center", font_ctx);
                }
                else if (strcmp(env_name->chars, "flushleft") == 0) {
                    process_alignment_environment(html_buf, elem, pool, depth, "latex-flushleft", font_ctx);
                }
                else if (strcmp(env_name->chars, "flushright") == 0) {
                    process_alignment_environment(html_buf, elem, pool, depth, "latex-flushright", font_ctx);
                }
                // Font environments - wrap content in appropriate span
                else if (strcmp(env_name->chars, "small") == 0 ||
                         strcmp(env_name->chars, "footnotesize") == 0 ||
                         strcmp(env_name->chars, "scriptsize") == 0 ||
                         strcmp(env_name->chars, "tiny") == 0 ||
                         strcmp(env_name->chars, "large") == 0 ||
                         strcmp(env_name->chars, "Large") == 0 ||
                         strcmp(env_name->chars, "LARGE") == 0 ||
                         strcmp(env_name->chars, "huge") == 0 ||
                         strcmp(env_name->chars, "Huge") == 0) {
                    // Font size environment - output span with class
                    stringbuf_append_str(html_buf, "<span class=\"");
                    stringbuf_append_str(html_buf, env_name->chars);
                    stringbuf_append_str(html_buf, "\">");
                    // Process environment content
                    for (int64_t i = 1; i < elem->length; i++) {
                        process_latex_element(html_buf, elem->items[i], pool, depth, font_ctx);
                    }
                    stringbuf_append_str(html_buf, "</span>");
                }
                else if (strcmp(env_name->chars, "bfseries") == 0 ||
                         strcmp(env_name->chars, "mdseries") == 0) {
                    // Font series environment - output span with class "bf" or "md"
                    const char* css_class = (strcmp(env_name->chars, "bfseries") == 0) ? "bf" : "md";
                    stringbuf_append_str(html_buf, "<span class=\"");
                    stringbuf_append_str(html_buf, css_class);
                    stringbuf_append_str(html_buf, "\">");
                    // Process environment content
                    for (int64_t i = 1; i < elem->length; i++) {
                        process_latex_element(html_buf, elem->items[i], pool, depth, font_ctx);
                    }
                    stringbuf_append_str(html_buf, "</span>");
                }
                else if (strcmp(env_name->chars, "itshape") == 0 ||
                         strcmp(env_name->chars, "slshape") == 0 ||
                         strcmp(env_name->chars, "upshape") == 0 ||
                         strcmp(env_name->chars, "scshape") == 0) {
                    // Font shape environment - output span with appropriate class
                    const char* css_class;
                    if (strcmp(env_name->chars, "itshape") == 0) css_class = "it";
                    else if (strcmp(env_name->chars, "slshape") == 0) css_class = "sl";
                    else if (strcmp(env_name->chars, "scshape") == 0) css_class = "sc";
                    else css_class = "up";
                    stringbuf_append_str(html_buf, "<span class=\"");
                    stringbuf_append_str(html_buf, css_class);
                    stringbuf_append_str(html_buf, "\">");
                    // Process environment content
                    for (int64_t i = 1; i < elem->length; i++) {
                        process_latex_element(html_buf, elem->items[i], pool, depth, font_ctx);
                    }
                    stringbuf_append_str(html_buf, "</span>");
                }
            }
        }
    }
}

// Get the bullet marker for a given itemize depth (0-indexed)
static const char* get_itemize_marker(int list_depth) {
    // LaTeX.js uses: •, –, *, · for levels 0-3
    switch (list_depth % 4) {
        case 0: return "•";      // U+2022 bullet
        case 1: return "–";      // U+2013 en-dash (with bold)
        case 2: return "*";      // asterisk
        default: return "·";     // U+00B7 middle dot
    }
}

// Check if itemize marker at this depth needs font wrapping
static bool itemize_marker_needs_font(int list_depth) {
    // Level 1 (depth=1) needs <span class="rm bf up">–</span>
    return (list_depth % 4) == 1;
}

// Process itemize environment with proper LaTeX.js structure
static void process_itemize(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx, int list_depth) {
    stringbuf_append_str(html_buf, "<ul class=\"list\">\n");

    // Iterate through children to find item elements
    if (elem && elem->items && elem->length > 0) {
        for (int64_t i = 0; i < elem->length; i++) {
            Item child = elem->items[i];
            TypeId child_type = get_type_id(child);

            if (child_type == LMD_TYPE_ELEMENT) {
                Element* child_elem = child.element;
                if (child_elem && child_elem->type) {
                    StrView child_name = ((TypeElmt*)child_elem->type)->name;
                    // Handle both old parser "item" and tree-sitter "enum_item"
                    if ((child_name.length == 4 && strncmp(child_name.str, "item", 4) == 0) ||
                        (child_name.length == 9 && strncmp(child_name.str, "enum_item", 9) == 0)) {
                        process_item(html_buf, child_elem, pool, depth + 1, font_ctx, list_depth, false, 0);
                    }
                }
            }
        }
    }

    stringbuf_append_str(html_buf, "</ul>\n");
}

// Process enumerate environment with proper LaTeX.js structure
static void process_enumerate(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx, int list_depth, int& item_counter) {
    stringbuf_append_str(html_buf, "<ol class=\"list\">\n");

    int local_counter = 1;

    // Iterate through children to find item elements
    if (elem && elem->items && elem->length > 0) {
        for (int64_t i = 0; i < elem->length; i++) {
            Item child = elem->items[i];
            TypeId child_type = get_type_id(child);

            if (child_type == LMD_TYPE_ELEMENT) {
                Element* child_elem = child.element;
                if (child_elem && child_elem->type) {
                    StrView child_name = ((TypeElmt*)child_elem->type)->name;
                    // Handle both old parser "item" and tree-sitter "enum_item"
                    if ((child_name.length == 4 && strncmp(child_name.str, "item", 4) == 0) ||
                        (child_name.length == 9 && strncmp(child_name.str, "enum_item", 9) == 0)) {
                        bool has_custom_label = process_item(html_buf, child_elem, pool, depth + 1, font_ctx, list_depth, true, local_counter);
                        // Only increment counter if this item doesn't have a custom label
                        if (!has_custom_label) {
                            local_counter++;
                        }
                    }
                }
            }
        }
    }

    item_counter = local_counter;
    stringbuf_append_str(html_buf, "</ol>\n");
}

// Process description environment - outputs <dl> with <dt>/<dd> pairs
static void process_description(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx) {
    stringbuf_append_str(html_buf, "<dl class=\"list\">\n");

    // Iterate through children to find item elements
    if (elem && elem->items && elem->length > 0) {
        for (int64_t i = 0; i < elem->length; i++) {
            Item child = elem->items[i];
            TypeId child_type = get_type_id(child);

            if (child_type == LMD_TYPE_ELEMENT) {
                Element* child_elem = child.element;
                if (child_elem && child_elem->type) {
                    StrView child_name = ((TypeElmt*)child_elem->type)->name;
                    // Handle both old parser "item" and tree-sitter "enum_item"
                    if ((child_name.length == 4 && strncmp(child_name.str, "item", 4) == 0) ||
                        (child_name.length == 9 && strncmp(child_name.str, "enum_item", 9) == 0)) {
                        
                        // Extract term from brack_group_text
                        StringBuf* term_buf = stringbuf_new(pool);
                        bool found_term = false;
                        
                        // Look for brack_group_text child
                        if (child_elem->items && child_elem->length > 0) {
                            for (int64_t j = 0; j < child_elem->length; j++) {
                                Item item_child = child_elem->items[j];
                                if (get_type_id(item_child) == LMD_TYPE_ELEMENT) {
                                    Element* item_child_elem = item_child.element;
                                    if (item_child_elem && item_child_elem->type) {
                                        StrView item_child_name = ((TypeElmt*)item_child_elem->type)->name;
                                        if (item_child_name.length == 16 && strncmp(item_child_name.str, "brack_group_text", 16) == 0) {
                                            // Extract text from brack_group_text
                                            process_element_content_simple(term_buf, item_child_elem, pool, depth + 1, font_ctx);
                                            found_term = true;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        
                        // Output <dt> with term
                        stringbuf_append_str(html_buf, "<dt>");
                        if (found_term && term_buf->length > 0 && term_buf->str) {
                            stringbuf_append_str(html_buf, term_buf->str->chars);
                        }
                        stringbuf_append_str(html_buf, "</dt>\n");
                        
                        // Output <dd> with description content
                        stringbuf_append_str(html_buf, "<dd>");
                        
                        // Process item content (everything except brack_group_text)
                        bool in_paragraph = false;
                        if (child_elem->items && child_elem->length > 0) {
                            for (int64_t j = 0; j < child_elem->length; j++) {
                                Item item_child = child_elem->items[j];
                                TypeId item_child_type = get_type_id(item_child);
                                
                                // Skip the command and brack_group_text
                                if (item_child_type == LMD_TYPE_ELEMENT) {
                                    Element* item_child_elem = item_child.element;
                                    if (item_child_elem && item_child_elem->type) {
                                        StrView item_child_name = ((TypeElmt*)item_child_elem->type)->name;
                                        // Skip \item command and brack_group
                                        if ((item_child_name.length == 5 && strncmp(item_child_name.str, "\\item", 5) == 0) ||
                                            (item_child_name.length == 16 && strncmp(item_child_name.str, "brack_group_text", 16) == 0)) {
                                            continue;
                                        }
                                    }
                                }
                                
                                // Check for parbreak
                                if (item_child_type == LMD_TYPE_SYMBOL) {
                                    Symbol* sym = (Symbol*)(item_child.symbol_ptr);
                                    if (sym && strcmp(sym->chars, "parbreak") == 0) {
                                        if (in_paragraph) {
                                            stringbuf_append_str(html_buf, "</p>");
                                            in_paragraph = false;
                                        }
                                        stringbuf_append_str(html_buf, "<p>");
                                        in_paragraph = true;
                                        continue;
                                    }
                                }
                                
                                // Open paragraph if not already open
                                if (!in_paragraph) {
                                    stringbuf_append_str(html_buf, "<p>");
                                    in_paragraph = true;
                                }
                                
                                // Process the child
                                process_latex_element(html_buf, item_child, pool, depth + 2, font_ctx);
                            }
                        }
                        
                        // Close final paragraph
                        if (in_paragraph) {
                            stringbuf_append_str(html_buf, "</p>");
                        }
                        
                        stringbuf_append_str(html_buf, "</dd>\n");
                    }
                }
            }
        }
    }

    stringbuf_append_str(html_buf, "</dl>\n");
}

// Process description environment - uses <dl>/<dt>/<dd> structure
// Process quote/quotation/verse environments
static void process_quote(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx, const char* env_type) {
    stringbuf_append_str(html_buf, "<div class=\"list ");
    stringbuf_append_str(html_buf, env_type);
    stringbuf_append_str(html_buf, "\">\n");

    process_element_content(html_buf, elem, pool, depth + 1, font_ctx);

    stringbuf_append_str(html_buf, "</div>\n");
}

// Process verbatim environment
static void process_verbatim(StringBuf* html_buf, Element* elem, Pool* pool, int depth) {
    append_indent(html_buf, depth);
    stringbuf_append_str(html_buf, "<pre class=\"latex-verbatim\">");

    // Use simple content processing to avoid adding paragraph tags
    // Note: verbatim doesn't need font context
    FontContext verb_ctx = {FONT_SERIES_NORMAL, FONT_SHAPE_UPRIGHT, FONT_FAMILY_ROMAN, false};
    process_element_content_simple(html_buf, elem, pool, depth, &verb_ctx);

    stringbuf_append_str(html_buf, "</pre>\n");
}

// Process alignment environments (center, flushleft, flushright)
static void process_alignment_environment(StringBuf* html_buf, Element* elem, Pool* pool, int depth, const char* css_class, FontContext* font_ctx) {
    stringbuf_append_str(html_buf, "<div class=\"");
    stringbuf_append_str(html_buf, css_class);
    stringbuf_append_str(html_buf, "\">\n");

    process_element_content(html_buf, elem, pool, depth + 1, font_ctx);

    stringbuf_append_str(html_buf, "</div>\n");
}

// Process font-scoped commands like \textit{}, \textbf{}, \texttt{}, \textup{}
// These temporarily override the font context for their content
static void process_font_scoped_command(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx,
                                        FontSeries series, FontShape shape, FontFamily family) {
    // Save current font context
    FontContext saved_ctx = *font_ctx;

    // Apply the scoped font changes (partial override - only change non-default values)
    if (series != FONT_SERIES_NORMAL) font_ctx->series = series;
    if (shape != FONT_SHAPE_UPRIGHT) font_ctx->shape = shape;
    if (family != FONT_FAMILY_ROMAN) font_ctx->family = family;

    // Wrap content in span with the modified font class
    const char* css_class = get_font_css_class(font_ctx);
    stringbuf_append_str(html_buf, "<span class=\"");
    stringbuf_append_str(html_buf, css_class);
    stringbuf_append_str(html_buf, "\">");

    // Reset font context to default so text inside doesn't add redundant spans
    font_ctx->series = FONT_SERIES_NORMAL;
    font_ctx->shape = FONT_SHAPE_UPRIGHT;
    font_ctx->family = FONT_FAMILY_ROMAN;
    font_ctx->em_active = false;

    // Process content with neutral context (text won't add spans)
    process_element_content_simple(html_buf, elem, pool, depth, font_ctx);

    stringbuf_append_str(html_buf, "</span>");

    // Restore saved context
    *font_ctx = saved_ctx;
}

// Process \emph{} command - toggles italic/upright based on current state
static void process_emph_command(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx) {
    // Save current font context
    FontContext saved_ctx = *font_ctx;

    // Toggle shape: if upright -> italic, if italic/slanted -> upright
    if (font_ctx->shape == FONT_SHAPE_UPRIGHT) {
        font_ctx->shape = FONT_SHAPE_ITALIC;
    } else {
        font_ctx->shape = FONT_SHAPE_UPRIGHT;
    }

    // Wrap content in span with the toggled font class
    const char* css_class = get_font_css_class(font_ctx);
    stringbuf_append_str(html_buf, "<span class=\"");
    stringbuf_append_str(html_buf, css_class);
    stringbuf_append_str(html_buf, "\">");

    // Reset font context to default so text inside doesn't add redundant spans
    font_ctx->series = FONT_SERIES_NORMAL;
    font_ctx->shape = FONT_SHAPE_UPRIGHT;
    font_ctx->family = FONT_FAMILY_ROMAN;
    font_ctx->em_active = false;

    // Process content with neutral context
    process_element_content_simple(html_buf, elem, pool, depth, font_ctx);

    stringbuf_append_str(html_buf, "</span>");

    // Restore saved context
    *font_ctx = saved_ctx;
}

// Process text formatting commands
static void process_text_command(StringBuf* html_buf, Element* elem, Pool* pool, int depth, const char* css_class, const char* tag, FontContext* font_ctx) {
    stringbuf_append_str(html_buf, "<");
    stringbuf_append_str(html_buf, tag);
    stringbuf_append_str(html_buf, " class=\"");
    stringbuf_append_str(html_buf, css_class);
    stringbuf_append_str(html_buf, "\">");

    process_element_content_simple(html_buf, elem, pool, depth, font_ctx);

    stringbuf_append_str(html_buf, "</");
    stringbuf_append_str(html_buf, tag);
    stringbuf_append_str(html_buf, ">");
}

// Process item command with proper LaTeX.js structure
// list_depth: 0 = first level, 1 = nested, etc.
// is_enumerate: true for ordered list, false for unordered
// item_number: counter for enumerate items
// Returns true if item has a custom label (for enumerate counter tracking)
static bool process_item(StringBuf* html_buf, Element* elem, Pool* pool, int depth, FontContext* font_ctx, int list_depth, bool is_enumerate, int item_number) {
    stringbuf_append_str(html_buf, "<li>");

    // Check if the first child is a custom label element
    bool has_custom_label = false;
    Element* label_elem = nullptr;
    int64_t content_start_index = 0;

    if (elem && elem->items && elem->length > 0) {
        Item first_child = elem->items[0];
        TypeId first_type = get_type_id(first_child);
        if (first_type == LMD_TYPE_ELEMENT) {
            Element* first_elem = first_child.element;
            if (first_elem && first_elem->type) {
                StrView name = ((TypeElmt*)first_elem->type)->name;
                if (name.length == 5 && strncmp(name.str, "label", 5) == 0) {
                    has_custom_label = true;
                    label_elem = first_elem;
                    content_start_index = 1;  // Skip the label element in content processing
                }
            }
        }
    }

    // Add item label
    stringbuf_append_str(html_buf, "<span class=\"itemlabel\"><span class=\"hbox llap\">");

    if (has_custom_label && label_elem) {
        // Custom label: check if it needs font wrapping
        // First scan to see if there are font-changing commands
        bool has_font_commands = false;
        for (int64_t i = 0; i < label_elem->length; i++) {
            Item label_child = label_elem->items[i];
            TypeId child_type = get_type_id(label_child);
            if (child_type == LMD_TYPE_ELEMENT) {
                Element* child_elem = label_child.element;
                if (child_elem && child_elem->type) {
                    StrView name = ((TypeElmt*)child_elem->type)->name;
                    if ((name.length == 7 && strncmp(name.str, "itshape", 7) == 0) ||
                        (name.length == 8 && strncmp(name.str, "bfseries", 8) == 0) ||
                        (name.length == 7 && strncmp(name.str, "scshape", 7) == 0) ||
                        (name.length == 8 && strncmp(name.str, "mdseries", 8) == 0) ||
                        (name.length == 7 && strncmp(name.str, "upshape", 7) == 0)) {
                        has_font_commands = true;
                        break;
                    }
                }
            }
        }

        // If font commands present, wrap in span
        if (has_font_commands) {
            stringbuf_append_str(html_buf, "<span>");
        }

        // Process label contents, tracking font changes
        FontContext label_font = *font_ctx;  // Copy initial font context
        bool font_span_open = false;

        for (int64_t i = 0; i < label_elem->length; i++) {
            Item label_child = label_elem->items[i];
            TypeId child_type = get_type_id(label_child);

            if (child_type == LMD_TYPE_STRING) {
                String* str = label_child.get_string();
                if (str && str->len > 0) {
                    // Check if we need to open a font span
                    if (has_font_commands && !font_span_open) {
                        if (label_font.shape == FONT_SHAPE_ITALIC) {
                            stringbuf_append_str(html_buf, "<span class=\"it\">");
                            font_span_open = true;
                        } else if (label_font.series == FONT_SERIES_BOLD) {
                            stringbuf_append_str(html_buf, "<span class=\"bf\">");
                            font_span_open = true;
                        } else if (label_font.shape == FONT_SHAPE_SMALL_CAPS) {
                            stringbuf_append_str(html_buf, "<span class=\"sc\">");
                            font_span_open = true;
                        }
                    }

                    // Escape and output text
                    for (size_t j = 0; j < str->len; j++) {
                        char c = str->chars[j];
                        if (c == '<') stringbuf_append_str(html_buf, "&lt;");
                        else if (c == '>') stringbuf_append_str(html_buf, "&gt;");
                        else if (c == '&') stringbuf_append_str(html_buf, "&amp;");
                        else stringbuf_append_char(html_buf, c);
                    }
                }
            } else if (child_type == LMD_TYPE_ELEMENT) {
                Element* child_elem = label_child.element;
                if (child_elem && child_elem->type) {
                    StrView name = ((TypeElmt*)child_elem->type)->name;

                    // Check for font-changing declarations
                    if (name.length == 7 && strncmp(name.str, "itshape", 7) == 0) {
                        label_font.shape = FONT_SHAPE_ITALIC;
                    } else if (name.length == 8 && strncmp(name.str, "bfseries", 8) == 0) {
                        label_font.series = FONT_SERIES_BOLD;
                    } else if (name.length == 7 && strncmp(name.str, "scshape", 7) == 0) {
                        label_font.shape = FONT_SHAPE_SMALL_CAPS;
                    } else {
                        // Other inline element - process normally (e.g., \textendash)
                        process_latex_element(html_buf, label_child, pool, depth, &label_font);
                    }
                }
            }
        }

        // Close any open font span
        if (font_span_open) {
            stringbuf_append_str(html_buf, "</span>");
        }

        // Close outer span if we added it
        if (has_font_commands) {
            stringbuf_append_str(html_buf, "</span>");
        }
    } else if (is_enumerate) {
        // Enumerate: numbered label with id
        char id_buf[32];
        char num_buf[32];
        snprintf(id_buf, sizeof(id_buf), "<span id=\"item-%d\">", item_number);
        snprintf(num_buf, sizeof(num_buf), "%d.", item_number);
        stringbuf_append_str(html_buf, id_buf);
        stringbuf_append_str(html_buf, num_buf);
        stringbuf_append_str(html_buf, "</span>");
    } else {
        // Itemize: bullet marker based on depth
        const char* marker = get_itemize_marker(list_depth);
        if (itemize_marker_needs_font(list_depth)) {
            stringbuf_append_str(html_buf, "<span class=\"rm bf up\">");
            stringbuf_append_str(html_buf, marker);
            stringbuf_append_str(html_buf, "</span>");
        } else {
            stringbuf_append_str(html_buf, marker);
        }
    }

    stringbuf_append_str(html_buf, "</span></span>");

    // Process content - collect text into paragraphs
    // First, gather all content and check for paragraph breaks
    bool has_parbreak = false;
    bool has_nested_list = false;

    if (elem && elem->items && elem->length > content_start_index) {
        // Check for paragraph breaks or nested lists
        for (int64_t i = content_start_index; i < elem->length; i++) {
            Item child = elem->items[i];
            TypeId child_type = get_type_id(child);
            if (child_type == LMD_TYPE_ELEMENT) {
                Element* child_elem = child.element;
                if (child_elem && child_elem->type) {
                    StrView name = ((TypeElmt*)child_elem->type)->name;
                    if ((name.length == 8 && strncmp(name.str, "parbreak", 8) == 0) ||
                        (name.length == 3 && strncmp(name.str, "par", 3) == 0)) {
                        has_parbreak = true;
                    }
                    if ((name.length == 7 && strncmp(name.str, "itemize", 7) == 0) ||
                        (name.length == 9 && strncmp(name.str, "enumerate", 9) == 0)) {
                        has_nested_list = true;
                    }
                }
            }
        }

        // Process content with paragraph wrapping
        bool in_paragraph = false;
        for (int64_t i = content_start_index; i < elem->length; i++) {
            Item child = elem->items[i];
            TypeId child_type = get_type_id(child);

            if (child_type == LMD_TYPE_STRING) {
                String* str = child.get_string();
                if (str && str->len > 0) {
                    // Start paragraph if not in one
                    if (!in_paragraph) {
                        stringbuf_append_str(html_buf, "<p>");
                        in_paragraph = true;
                    }
                    // Output text with ligatures
                    bool is_tt = (font_ctx && font_ctx->family == FONT_FAMILY_TYPEWRITER);
                    append_escaped_text_with_ligatures(html_buf, str->chars, is_tt);
                }
            } else if (child_type == LMD_TYPE_ELEMENT) {
                Element* child_elem = child.element;
                if (child_elem && child_elem->type) {
                    StrView name = ((TypeElmt*)child_elem->type)->name;

                    // Check for paragraph break
                    if ((name.length == 8 && strncmp(name.str, "parbreak", 8) == 0) ||
                        (name.length == 3 && strncmp(name.str, "par", 3) == 0)) {
                        if (in_paragraph) {
                            close_paragraph(html_buf, false);
                            in_paragraph = false;
                        }
                        continue;
                    }

                    // Check for nested itemize
                    if (name.length == 7 && strncmp(name.str, "itemize", 7) == 0) {
                        if (in_paragraph) {
                            close_paragraph(html_buf, true);
                            in_paragraph = false;
                        }
                        process_itemize(html_buf, child_elem, pool, depth + 1, font_ctx, list_depth + 1);
                        continue;
                    }

                    // Check for nested enumerate
                    if (name.length == 9 && strncmp(name.str, "enumerate", 9) == 0) {
                        if (in_paragraph) {
                            close_paragraph(html_buf, true);
                            in_paragraph = false;
                        }
                        int dummy_counter = 0;
                        process_enumerate(html_buf, child_elem, pool, depth + 1, font_ctx, list_depth + 1, dummy_counter);
                        continue;
                    }

                    // Check for textblock - extract content and handle parbreak
                    if (name.length == 9 && strncmp(name.str, "textblock", 9) == 0) {
                        // Process textblock contents directly
                        for (int64_t j = 0; j < child_elem->length; j++) {
                            Item tb_child = child_elem->items[j];
                            TypeId tb_type = get_type_id(tb_child);

                            if (tb_type == LMD_TYPE_STRING) {
                                String* str = tb_child.get_string();
                                if (str && str->len > 0) {
                                    if (!in_paragraph) {
                                        stringbuf_append_str(html_buf, "<p>");
                                        in_paragraph = true;
                                    }
                                    bool is_tt = (font_ctx && font_ctx->family == FONT_FAMILY_TYPEWRITER);
                                    append_escaped_text_with_ligatures(html_buf, str->chars, is_tt);
                                }
                            } else if (tb_type == LMD_TYPE_ELEMENT) {
                                Element* tb_elem = tb_child.element;
                                if (tb_elem && tb_elem->type) {
                                    StrView tb_name = ((TypeElmt*)tb_elem->type)->name;
                                    if ((tb_name.length == 8 && strncmp(tb_name.str, "parbreak", 8) == 0) ||
                                        (tb_name.length == 3 && strncmp(tb_name.str, "par", 3) == 0)) {
                                        if (in_paragraph) {
                                            close_paragraph(html_buf, false);
                                            in_paragraph = false;
                                        }
                                    } else {
                                        // Other inline element within textblock
                                        if (!in_paragraph) {
                                            stringbuf_append_str(html_buf, "<p>");
                                            in_paragraph = true;
                                        }
                                        process_latex_element(html_buf, tb_child, pool, depth, font_ctx);
                                    }
                                }
                            }
                        }
                        continue;
                    }

                    // Other inline elements - start paragraph if needed
                    if (!in_paragraph) {
                        stringbuf_append_str(html_buf, "<p>");
                        in_paragraph = true;
                    }
                    process_latex_element(html_buf, child, pool, depth, font_ctx);
                }
            }
        }

        // Close any open paragraph
        if (in_paragraph) {
            close_paragraph(html_buf, false);
        }
    }

    stringbuf_append_str(html_buf, "</li>\n");
    return has_custom_label;
}


// Helper function to append escaped text with ligature and dash conversion
// Pass is_tt=true when in typewriter font to disable ligatures
static void append_escaped_text_with_ligatures(StringBuf* html_buf, const char* text, bool is_tt) {
    if (!text) return;

    for (const char* p = text; *p; p++) {
        // Check for UTF-8 non-breaking space (U+00A0 = 0xC2 0xA0)
        if ((unsigned char)*p == 0xC2 && (unsigned char)*(p+1) == 0xA0) {
            stringbuf_append_str(html_buf, "&nbsp;");
            p++;  // Skip second byte
            continue;
        }

        // Check for ligatures first (unless in typewriter font)
        if (!is_tt) {
            bool matched_ligature = false;
            for (int i = 0; ligature_table[i].pattern != NULL; i++) {
                const char* pat = ligature_table[i].pattern;
                int len = ligature_table[i].pattern_len;
                if (strncmp(p, pat, len) == 0) {
                    stringbuf_append_str(html_buf, ligature_table[i].replacement);
                    p += len - 1;  // -1 because loop will increment
                    matched_ligature = true;
                    break;
                }
            }
            if (matched_ligature) continue;
        }

        // Check for em-dash (---)
        if (*p == '-' && *(p+1) == '-' && *(p+2) == '-') {
            if (is_tt) {
                // In typewriter, keep literal dashes
                stringbuf_append_str(html_buf, "---");
            } else {
                stringbuf_append_str(html_buf, "\xE2\x80\x94"); // U+2014 em-dash
            }
            p += 2; // Skip next two dashes
        }
        // Check for en-dash (--)
        else if (*p == '-' && *(p+1) == '-') {
            if (is_tt) {
                stringbuf_append_str(html_buf, "--");
            } else {
                stringbuf_append_str(html_buf, "\xE2\x80\x93"); // U+2013 en-dash
            }
            p += 1; // Skip next dash
        }
        // Check for single hyphen (not part of em/en dash)
        else if (*p == '-') {
            if (is_tt) {
                stringbuf_append_char(html_buf, '-'); // U+002D hyphen-minus in typewriter
            } else {
                stringbuf_append_str(html_buf, "\xE2\x80\x90"); // U+2010 hyphen
            }
        }
        // HTML entity escaping
        else if (*p == '<') {
            stringbuf_append_str(html_buf, "&lt;");
        }
        else if (*p == '>') {
            stringbuf_append_str(html_buf, "&gt;");
        }
        else if (*p == '&') {
            stringbuf_append_str(html_buf, "&amp;");
        }
        // Note: We don't escape " to &quot; - quotes are valid in HTML text content
        else {
            stringbuf_append_char(html_buf, *p);
        }
    }
}

// Legacy function for backward compatibility (applies ligatures by default)
static void append_escaped_text(StringBuf* html_buf, const char* text) {
    append_escaped_text_with_ligatures(html_buf, text, false);
}

// Helper function to append indentation
static void append_indent(StringBuf* html_buf, int depth) {
    for (int i = 0; i < depth; i++) {
        stringbuf_append_str(html_buf, "  ");
    }
}

// Helper function to trim trailing whitespace from buffer before closing paragraph
static void close_paragraph(StringBuf* html_buf, bool add_newline) {
    // Trim trailing whitespace from the buffer
    if (html_buf->length > 0 && html_buf->str && html_buf->str->chars) {
        while (html_buf->length > 0 && (html_buf->str->chars[html_buf->length - 1] == ' ' ||
                                         html_buf->str->chars[html_buf->length - 1] == '\t' ||
                                         html_buf->str->chars[html_buf->length - 1] == '\n')) {
            html_buf->length--;
        }
        html_buf->str->chars[html_buf->length] = '\0';
        html_buf->str->len = html_buf->length;
    }

    // Check if the paragraph is empty (ends with <p> or <p class="...">)
    // If so, remove the opening tag instead of adding closing tag
    if (html_buf->length >= 3 && html_buf->str && html_buf->str->chars) {
        // Check for plain <p>
        if (html_buf->length >= 3 &&
            html_buf->str->chars[html_buf->length - 3] == '<' &&
            html_buf->str->chars[html_buf->length - 2] == 'p' &&
            html_buf->str->chars[html_buf->length - 1] == '>') {
            // Remove <p>
            html_buf->length -= 3;
            html_buf->str->chars[html_buf->length] = '\0';
            html_buf->str->len = html_buf->length;
            return;
        }

        // Check for <p class="..."> - must end with ">
        // First, check if buffer ends with '>'
        if (html_buf->str->chars[html_buf->length - 1] == '>') {
            // Search backwards for '<p ' or '<p>'
            size_t search_limit = (html_buf->length > 50) ? (html_buf->length - 50) : 0;
            for (size_t i = html_buf->length - 1; i > search_limit; i--) {
                if (html_buf->str->chars[i] == '<' && i + 1 < html_buf->length) {
                    // Check if this is <p or <p class="...">
                    if (html_buf->str->chars[i + 1] == 'p' &&
                        (i + 2 == html_buf->length - 1 || html_buf->str->chars[i + 2] == ' ') &&
                        html_buf->str->chars[html_buf->length - 1] == '>') {
                        // This is <p> or <p class="..."> - check there's no content after
                        // We've already trimmed whitespace, so if buffer ends with ">",
                        // this is the opening tag with no content
                        // BUT: we need to make sure there's no content between <p...> and end
                        // Just checking if buffer ends with > right after finding <p isn't enough
                        // We need to verify the > is the closing > of the <p tag
                        bool found_content = false;
                        for (size_t j = i; j < html_buf->length; j++) {
                            if (html_buf->str->chars[j] == '>') {
                                // Check if there's anything after this > (other than the final position)
                                if (j + 1 < html_buf->length) {
                                    found_content = true;
                                }
                                break;
                            }
                        }
                        if (!found_content) {
                            // Empty paragraph - remove the <p...> tag
                            html_buf->length = i;
                            html_buf->str->chars[html_buf->length] = '\0';
                            html_buf->str->len = html_buf->length;
                            return;
                        }
                    }
                    break;  // Found a <, stop searching
                }
            }
        }
    }

    if (add_newline) {
        stringbuf_append_str(html_buf, "</p>\n");
    } else {
        stringbuf_append_str(html_buf, "</p>");
    }
}

// Helper function to open paragraph with appropriate classes (alignment, noindent, continue)
static void open_paragraph(StringBuf* html_buf, bool noindent, bool cont) {
    const char* alignment_class = get_alignment_css_class(current_alignment);

    // Determine what classes to add
    bool has_class = (noindent || cont || alignment_class != nullptr);

    // printf("DEBUG: open_paragraph: has_class=%d, noindent=%d, cont=%d, alignment=%s\n", has_class, noindent, cont, alignment_class ? alignment_class : "(none)");

    if (!has_class) {
        // printf("DEBUG: Appending <p>, buf before='%s' (len=%zu)\n", html_buf->str ? html_buf->str->chars : "(null)", html_buf->length);
        stringbuf_append_str(html_buf, "<p>");
        // printf("DEBUG: buf after='%s' (len=%zu)\n", html_buf->str ? html_buf->str->chars : "(null)", html_buf->length);
        return;
    }

    stringbuf_append_str(html_buf, "<p class=\"");

    bool first_class = true;
    if (noindent) {
        stringbuf_append_str(html_buf, "noindent");
        first_class = false;
    }
    if (cont) {
        if (!first_class) stringbuf_append_str(html_buf, " ");
        stringbuf_append_str(html_buf, "continue");
        first_class = false;
    }
    if (alignment_class) {
        if (!first_class) stringbuf_append_str(html_buf, " ");
        stringbuf_append_str(html_buf, alignment_class);
    }

    stringbuf_append_str(html_buf, "\">");
}

// ===== MarkReader-based implementations =====

// process element content using reader API (simple version)
static void process_element_content_simple_reader(StringBuf* html_buf, const ElementReader& elem, Pool* pool, int depth, FontContext* font_ctx) {
    auto it = elem.children();
    ItemReader child;
    while (it.next(&child)) {
        process_latex_element_reader(html_buf, child, pool, depth, font_ctx);
    }
}

// process element content using reader API (with paragraph wrapping)
static void process_element_content_reader(StringBuf* html_buf, const ElementReader& elem, Pool* pool, int depth, FontContext* font_ctx) {
    // for now, just use simple version - can add paragraph logic later if needed
    process_element_content_simple_reader(html_buf, elem, pool, depth, font_ctx);
}

// main LaTeX element processor using reader API
static void process_latex_element_reader(StringBuf* html_buf, const ItemReader& item, Pool* pool, int depth, FontContext* font_ctx) {
    fprintf(stderr, "DEBUG: process_latex_element_reader called, depth=%d\n", depth);
    
    if (item.isNull()) {
        fprintf(stderr, "DEBUG: process_latex_element_reader: item is null\n");
        return;
    }

    if (item.isString()) {
        String* str = item.asString();
        fprintf(stderr, "DEBUG: process_latex_element_reader: item is string: %s\n", str ? str->chars : "NULL");
        if (str && str->chars) {
            append_escaped_text(html_buf, str->chars);
        }
        return;
    }

    if (!item.isElement()) {
        fprintf(stderr, "DEBUG: process_latex_element_reader: item is not element (type=%d)\n", item.getType());
        return;
    }

    ElementReader elem = item.asElement();
    const char* cmd_name = elem.tagName();
    fprintf(stderr, "DEBUG: process_latex_element_reader: Processing element: %s, children=%lld\n", 
            cmd_name ? cmd_name : "NULL", elem.childCount());
    
    fprintf(stderr, "DEBUG: elem.element() = %p\n", elem.element());
    
    if (!cmd_name) {
        fprintf(stderr, "DEBUG: cmd_name is NULL, returning\n");
        return;
    }

    // handle different LaTeX commands - delegate to existing handlers for now
    // convert reader back to Item/Element for compatibility with existing code
    if (elem.element()) {
        Element* raw_elem = (Element*)elem.element();
        fprintf(stderr, "DEBUG: Converting element to Item and calling process_latex_element\n");
        Item raw_item;
        raw_item.element = raw_elem;
        raw_item._type_id = LMD_TYPE_ELEMENT;

        process_latex_element(html_buf, raw_item, pool, depth, font_ctx);
        fprintf(stderr, "DEBUG: Returned from process_latex_element\n");
    } else {
        fprintf(stderr, "DEBUG: elem.element() returned NULL!\n");
    }
}
