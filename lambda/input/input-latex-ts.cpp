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

// Windows doesn't have memmem - provide portable implementation
#ifdef _WIN32
static void* memmem(const void* haystack, size_t haystack_len,
                    const void* needle, size_t needle_len) {
    if (needle_len == 0) return (void*)haystack;
    if (haystack_len < needle_len) return nullptr;

    const char* h = (const char*)haystack;
    const char* n = (const char*)needle;
    const char* end = h + haystack_len - needle_len + 1;

    for (; h < end; h++) {
        if (memcmp(h, n, needle_len) == 0) {
            return (void*)h;
        }
    }
    return nullptr;
}
#endif

using namespace lambda;

// Helper: Check if an item is EMPTY_STRING (the lambda.nil sentinel)
// Returns true if the item should be skipped when adding to elements
static bool is_empty_string_sentinel(Item item) {
    if (get_type_id(item) != LMD_TYPE_STRING) return false;
    String* str = item.get_string();
    if (!str) return false;
    // Check for EMPTY_STRING sentinel (has content "lambda.nil" or length 0)
    if (str->len == 0) return true;
    if (str->len == 10 && strncmp(str->chars, "lambda.nil", 10) == 0) return true;
    return false;
}

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

// Hybrid grammar (50 rules) node classification
// Matches LaTeX.js structure: generic commands, section hierarchy, environment types
static const std::unordered_map<std::string, NodeCategory> node_classification = {
    // Document structure
    {"source_file", NODE_CONTAINER},
    {"preamble", NODE_CONTAINER},
    {"document", NODE_CONTAINER},
    {"begin_document", NODE_LEAF},
    {"end_document", NODE_LEAF},

    // Block-level content
    {"paragraph", NODE_CONTAINER},
    {"paragraph_break", NODE_LEAF},
    {"section", NODE_CONTAINER},
    {"section_command", NODE_LEAF},

    // Commands (generic pattern)
    {"command", NODE_CONTAINER},
    {"verb_command", NODE_CONTAINER},  // \verb|text| inline verbatim (handled specially)
    {"char_command", NODE_CONTAINER},  // \char<number> TeX character code (handled specially)
    {"caret_char", NODE_CONTAINER},    // ^^XX or ^^^^XXXX TeX caret notation (handled specially)
    {"controlspace_command", NODE_CONTAINER},  // \<space>, \<tab>, \<newline> control space
    {"command_name", NODE_LEAF},
    {"star", NODE_LEAF},

    // Groups
    {"curly_group", NODE_CONTAINER},
    {"brack_group", NODE_CONTAINER},

    // Math
    {"math", NODE_CONTAINER},
    {"inline_math", NODE_CONTAINER},
    {"display_math", NODE_CONTAINER},
    {"math_text", NODE_TEXT},
    {"math_single_char", NODE_TEXT},  // Single char in sub/superscript
    {"subscript", NODE_CONTAINER},
    {"superscript", NODE_CONTAINER},

    // Environments
    {"environment", NODE_CONTAINER},
    {"generic_environment", NODE_CONTAINER},
    {"verbatim_environment", NODE_CONTAINER},
    {"comment_environment", NODE_CONTAINER},
    {"math_environment", NODE_CONTAINER},
    {"begin_env", NODE_CONTAINER},
    {"end_env", NODE_CONTAINER},
    {"env_name", NODE_LEAF},
    {"verbatim", NODE_TEXT},

    // Text content
    {"text", NODE_TEXT},
    {"space", NODE_LEAF},
    {"line_comment", NODE_LEAF},
    {"ligature", NODE_LEAF},
    {"control_symbol", NODE_CONTAINER},
    {"linebreak_command", NODE_CONTAINER},  // \\ with optional [<length>]

    // Special tokens
    {"nbsp", NODE_LEAF},
    {"alignment_tab", NODE_LEAF},

    // Punctuation (skip these)
    {"{", NODE_LEAF},
    {"}", NODE_LEAF},
    {"[", NODE_LEAF},
    {"]", NODE_LEAF},
    {"$", NODE_LEAF},

    // Error recovery
    {"ERROR", NODE_TEXT},
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

// LaTeX diacritic commands that need ZWSP when used with empty braces
struct DiacriticInfo {
    char cmd;              // The command character (e.g., '^' for \^)
    const char* standalone; // Standalone character when no base given (e.g., \^{})
};

static const DiacriticInfo diacritic_table[] = {
    {'\'', "\u00B4"},   // acute accent
    {'`',  "\u0060"},   // grave accent
    {'^',  "\u005E"},   // circumflex (caret)
    {'"',  "\u00A8"},   // umlaut/diaeresis
    {'~',  "\u007E"},   // tilde
    {'=',  "\u00AF"},   // macron
    {'.',  "\u02D9"},   // dot above
    {'u',  "\u02D8"},   // breve
    {'v',  "\u02C7"},   // caron/háček
    {'H',  "\u02DD"},   // double acute
    {'c',  "\u00B8"},   // cedilla
    {0, nullptr}        // sentinel
};

static const DiacriticInfo* find_diacritic(const char* cmd_name) {
    if (!cmd_name || cmd_name[0] == '\0' || cmd_name[1] != '\0') {
        return nullptr; // Not a single-character command
    }
    char cmd_char = cmd_name[0];
    for (const DiacriticInfo* d = diacritic_table; d->cmd != 0; d++) {
        if (d->cmd == cmd_char) return d;
    }
    return nullptr;
}

// Forward declarations
static Item convert_latex_node(InputContext& ctx, TSNode node, const char* source);
static Item convert_command(InputContext& ctx, TSNode node, const char* source);
static Item convert_environment(InputContext& ctx, TSNode node, const char* source);
static Item convert_text_node(InputContext& ctx, TSNode node, const char* source);
static Item convert_leaf_node(InputContext& ctx, TSNode node, const char* source);
static String* extract_text(InputContext& ctx, TSNode node, const char* source);
static String* normalize_latex_text(InputContext& ctx, const char* text, size_t len);

// Helper: Check if current child is a comment that eats the following space
// Returns true if current node is line_comment and next is space starting immediately after
static bool should_skip_comment_and_space(TSNode parent, uint32_t child_index) {
    uint32_t child_count = ts_node_child_count(parent);
    if (child_index + 1 >= child_count) {
        return false;  // No next child
    }

    TSNode child = ts_node_child(parent, child_index);
    const char* child_type = ts_node_type(child);

    if (strcmp(child_type, "line_comment") != 0) {
        return false;  // Not a comment
    }

    TSNode next_child = ts_node_child(parent, child_index + 1);
    const char* next_type = ts_node_type(next_child);
    uint32_t comment_end = ts_node_end_byte(child);
    uint32_t next_start = ts_node_start_byte(next_child);

    // If next node is space and starts immediately after comment (no gap),
    // it's the eaten newline - should skip both
    bool should_skip = (strcmp(next_type, "space") == 0 && comment_end == next_start);
    if (should_skip) {
        log_debug("latex_ts: skipping comment at %u-%u and space at %u-%u in parent '%s'",
                 ts_node_start_byte(child), comment_end, next_start, ts_node_end_byte(next_child),
                 ts_node_type(parent));
    }
    return should_skip;
}

// Helper: Check if previous sibling was a line_comment
// Used to strip leading whitespace from text nodes following comments
static bool prev_sibling_is_comment(TSNode parent, uint32_t child_index) {
    if (child_index == 0) {
        return false;
    }

    TSNode prev = ts_node_child(parent, child_index - 1);
    const char* prev_type = ts_node_type(prev);

    // Check if previous sibling is a line_comment
    if (strcmp(prev_type, "line_comment") == 0) {
        return true;
    }

    // Also check if previous-previous was comment (and previous was skipped space)
    if (child_index >= 2) {
        TSNode prev_prev = ts_node_child(parent, child_index - 2);
        const char* prev_prev_type = ts_node_type(prev_prev);
        if (strcmp(prev_prev_type, "line_comment") == 0 && strcmp(prev_type, "space") == 0) {
            return true;
        }
    }

    return false;
}

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

// Extract text with optional leading whitespace stripping
// Used for text nodes following line_comment nodes per LaTeX semantics
static String* extract_text_strip_leading(InputContext& ctx, TSNode node, const char* source, bool strip_leading) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    size_t len = end - start;

    if (len == 0) {
        return ctx.builder.createString("", 0);
    }

    const char* text = source + start;

    if (strip_leading) {
        // Skip leading whitespace (spaces, tabs, newlines)
        while (len > 0 && (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r')) {
            text++;
            len--;
        }
        if (len == 0) {
            return ctx.builder.createString("", 0);
        }
        log_debug("latex_ts: stripped leading whitespace, result='%.*s'", (int)len, text);
    }

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

    // Placeholder (#1, #2, etc.) -> Symbol("#1"), Symbol("#2"), etc.
    // Used in macro definitions for parameter substitution
    if (strcmp(node_type, "placeholder") == 0) {
        return {.item = y2it(builder.createSymbol(source + start, len))};
    }

    // Space -> preserve first character type (newline vs space)
    // This is important for latex-js compatibility where newline at start of hgroup
    // triggers ZWS output while regular space does not
    // Output "\n" for newline-starting, " " for space-starting
    if (strcmp(node_type, "space") == 0) {
        char first_char = source[start];
        if (first_char == '\n' || first_char == '\r') {
            // Newline-starting space: output "\n" to mark it specially
            return {.item = s2it(builder.createString("\n", 1))};
        } else {
            // Regular space: output " "
            return {.item = s2it(builder.createString(" ", 1))};
        }
    }

    // Line comment -> skip for now
    if (strcmp(node_type, "line_comment") == 0) {
        log_debug("latex_ts: found line_comment node at pos %u-%u", start, end);
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

    // Path, label, uri: extract actual text content
    if (strcmp(node_type, "path") == 0 || strcmp(node_type, "label") == 0 || strcmp(node_type, "uri") == 0) {
        return {.item = s2it(extract_text(ctx, node, source))};
    }

    // Command names: extract the actual command text (e.g., \greet -> "greet")
    if (strcmp(node_type, "command_name") == 0) {
        String* cmd_text = extract_text(ctx, node, source);
        if (cmd_text && cmd_text->len > 0) {
            // Skip leading backslash if present
            const char* name_start = cmd_text->chars;
            size_t name_len = cmd_text->len;
            if (name_start[0] == '\\') {
                name_start++;
                name_len--;
            }
            if (name_len > 0) {
                return {.item = s2it(builder.createString(name_start, name_len))};
            }
        }
    }

    // nbsp (~) -> Element with tag "nbsp" (not symbol)
    // This creates an element that the V2 formatter can handle
    if (strcmp(node_type, "nbsp") == 0) {
        ElementBuilder elem_builder = builder.element("nbsp");
        return elem_builder.final();
    }

    // Default: use node type as symbol name
    return {.item = y2it(builder.createSymbol(node_type))};
}

// Helper: Check if previous sibling is a line_comment (directly or with space between)
static bool has_prev_sibling_comment(TSNode node) {
    TSNode prev = ts_node_prev_sibling(node);
    if (ts_node_is_null(prev)) {
        return false;
    }

    const char* prev_type = ts_node_type(prev);
    if (strcmp(prev_type, "line_comment") == 0) {
        return true;
    }

    // Check if prev is space and prev-prev is comment
    if (strcmp(prev_type, "space") == 0) {
        TSNode prev_prev = ts_node_prev_sibling(prev);
        if (!ts_node_is_null(prev_prev)) {
            const char* prev_prev_type = ts_node_type(prev_prev);
            if (strcmp(prev_prev_type, "line_comment") == 0) {
                return true;
            }
        }
    }

    return false;
}

// Convert text node to String
static Item convert_text_node(InputContext& ctx, TSNode node, const char* source) {
    const char* node_type = ts_node_type(node);

    // Check if we should strip leading whitespace (after a line_comment)
    bool strip_leading = has_prev_sibling_comment(node);

    // For "word" nodes, extract and return as string
    if (strcmp(node_type, "word") == 0) {
        return {.item = s2it(extract_text_strip_leading(ctx, node, source, strip_leading))};
    }

    // For "text" and "math_text" nodes in hybrid grammar - they are terminal nodes (no children)
    // Just extract the raw text directly
    if (strcmp(node_type, "text") == 0 || strcmp(node_type, "math_text") == 0 ||
        strcmp(node_type, "verbatim") == 0) {
        return {.item = s2it(extract_text_strip_leading(ctx, node, source, strip_leading))};
    }

    // Default: extract as string (with potential strip)
    return {.item = s2it(extract_text_strip_leading(ctx, node, source, strip_leading))};
}

// Main conversion dispatcher
static Item convert_latex_node(InputContext& ctx, TSNode node, const char* source) {
    const char* node_type = ts_node_type(node);
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);

    // Get the actual text content for debugging
    uint32_t len = end - start;
    char text_preview[51] = {0};
    if (len > 0 && len < 50) {
        memcpy(text_preview, source + start, len);
        text_preview[len] = '\0';
    } else if (len > 0) {
        memcpy(text_preview, source + start, 50);
        text_preview[50] = '\0';
    }

    log_debug("convert_latex_node: type='%s', start=%u, end=%u, text='%s'",
              node_type, start, end, text_preview);

    // Handle ERROR nodes by recovering content
    // ERROR nodes may contain parseable content that should be processed
    if (strcmp(node_type, "ERROR") == 0) {
        log_warn("Parse error at position %u - attempting recovery", ts_node_start_byte(node));

        // Extract raw text from ERROR node
        uint32_t start = ts_node_start_byte(node);
        uint32_t end = ts_node_end_byte(node);
        if (end > start) {
            const char* text = source + start;
            size_t len = end - start;

            // Check if this ERROR contains comment-related content
            bool has_comment_content = (memmem(text, len, "\\begin{comment}", 15) != nullptr ||
                                       memmem(text, len, "\\end{comment}", 13) != nullptr);

            if (has_comment_content) {
                // Build processed text by stripping comments
                std::string result;
                result.reserve(len);

                size_t i = 0;
                while (i < len) {
                    // Check for \begin{comment}
                    if (i + 15 <= len && strncmp(text + i, "\\begin{comment}", 15) == 0) {
                        // Skip until \end{comment}
                        i += 15;
                        while (i < len) {
                            if (i + 13 <= len && strncmp(text + i, "\\end{comment}", 13) == 0) {
                                i += 13;
                                // Skip optional trailing newline
                                if (i < len && text[i] == '\n') {
                                    i++;
                                }
                                break;
                            }
                            i++;
                        }
                        continue;
                    }

                    // Check for line comment (%)
                    if (text[i] == '%') {
                        // Skip % and everything until end of line (including newline)
                        // In LaTeX, % eats the line break, joining adjacent text
                        while (i < len && text[i] != '\n') {
                            i++;
                        }
                        if (i < len && text[i] == '\n') {
                            i++;
                        }
                        continue;
                    }

                    // Copy regular character
                    result += text[i];
                    i++;
                }

                if (!result.empty()) {
                    return ctx.builder.createStringItem(result.c_str(), result.size());
                }
            }
        }
        return {.item = ITEM_NULL};
    }

    // Special handling for verb_command (single token but needs to be an element)
    if (strcmp(node_type, "verb_command") == 0) {
        MarkBuilder& builder = ctx.builder;

        // Extract the full token text
        uint32_t start = ts_node_start_byte(node);
        uint32_t end = ts_node_end_byte(node);
        const char* text = source + start;
        size_t len = end - start;

        log_debug("verb_command token: start=%u, end=%u, len=%zu, text='%.*s'",
                  start, end, len, (int)len, text);

        ElementBuilder elem = builder.element("verb_command");

        // Store the full token text as a string child
        // The formatter will parse it to extract delimiter and content
        Item token_string = builder.createStringItem(text, len);
        elem.child(token_string);

        return elem.final();
    }

    // Special handling for char_command (single token from external scanner)
    // Format: \char<decimal>, \char"<hex>, \char'<octal>
    if (strcmp(node_type, "char_command") == 0) {
        MarkBuilder& builder = ctx.builder;

        // Extract the full token text
        uint32_t start = ts_node_start_byte(node);
        uint32_t end = ts_node_end_byte(node);
        const char* text = source + start;
        size_t len = end - start;

        log_debug("char_command token: start=%u, end=%u, len=%zu, text='%.*s'",
                  start, end, len, (int)len, text);

        ElementBuilder elem = builder.element("char_command");

        // Store the full token text as a string child
        // The formatter will parse it to extract the character code
        Item token_string = builder.createStringItem(text, len);
        elem.child(token_string);

        return elem.final();
    }

    // Special handling for caret_char (single token from external scanner)
    // Format: ^^XX (2 hex digits) or ^^^^XXXX (4 hex digits) or ^^c (char +/- 64)
    if (strcmp(node_type, "caret_char") == 0) {
        MarkBuilder& builder = ctx.builder;

        // Extract the full token text
        uint32_t start = ts_node_start_byte(node);
        uint32_t end = ts_node_end_byte(node);
        const char* text = source + start;
        size_t len = end - start;

        log_debug("caret_char token: start=%u, end=%u, len=%zu, text='%.*s'",
                  start, end, len, (int)len, text);

        ElementBuilder elem = builder.element("caret_char");

        // Store the full token text as a string child
        // The formatter will parse it to extract the character code
        Item token_string = builder.createStringItem(text, len);
        elem.child(token_string);

        return elem.final();
    }

    // Special handling for controlspace_command (\<space>, \<tab>, \<newline>)
    if (strcmp(node_type, "controlspace_command") == 0) {
        MarkBuilder& builder = ctx.builder;

        // Extract the full token text (e.g., "\ ", "\t", "\n")
        uint32_t start = ts_node_start_byte(node);
        uint32_t end = ts_node_end_byte(node);
        const char* text = source + start;
        size_t len = end - start;

        // Create space_cmd element (reuse existing infrastructure)
        ElementBuilder elem = builder.element("space_cmd");

        // Store the command text as a string child
        Item token_string = builder.createStringItem(text, len);
        elem.child(token_string);

        return elem.final();
    }

    // Handle anonymous bracket tokens (literal [ and ] at document level)
    // These are literal punctuation, not part of optional argument syntax
    if (strcmp(node_type, "[") == 0 || strcmp(node_type, "]") == 0) {
        // Return as a string containing the literal bracket
        MarkBuilder& builder = ctx.builder;
        return {.item = s2it(builder.createString(node_type))};
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
                    // LaTeX comment handling: comments eat their newline
                    if (should_skip_comment_and_space(node, i)) {
                        log_debug("latex_ts: skipping comment and eaten newline at child %u", i);
                        i++;  // Skip both comment and following space
                        continue;
                    }

                    TSNode child = ts_node_child(node, i);

                    // Scanner now handles \verb properly, no special workarounds needed

                    Item child_item = convert_latex_node(ctx, child, source);
                    if (child_item.item != ITEM_NULL && !is_empty_string_sentinel(child_item)) {
                        root_builder.child(child_item);
                    }
                }

                return root_builder.final();
            }

            if (strcmp(node_type, "generic_command") == 0) {
                return convert_command(ctx, node, source);
            }

            // NEW: command type in hybrid grammar
            if (strcmp(node_type, "command") == 0) {
                return convert_command(ctx, node, source);
            }

            // Handle "environment" node - transparent wrapper from grammar choice rule
            // The grammar has: environment -> choice(generic_environment, math_environment, ...)
            // This unwraps the choice node and returns the actual environment directly
            if (strcmp(node_type, "environment") == 0) {
                uint32_t child_count = ts_node_child_count(node);
                for (uint32_t i = 0; i < child_count; i++) {
                    TSNode child = ts_node_child(node, i);
                    const char* child_type = ts_node_type(child);
                    // Skip any ERROR nodes
                    if (strcmp(child_type, "ERROR") == 0) continue;
                    // Return the first valid child (should be generic_environment, math_environment, etc.)
                    Item child_item = convert_latex_node(ctx, child, source);
                    if (child_item.item != ITEM_NULL) {
                        return child_item;
                    }
                }
                return {.item = ITEM_NULL};
            }

            if (strcmp(node_type, "generic_environment") == 0) {
                return convert_environment(ctx, node, source);
            }

            // Special handling for comment_environment - skip all content
            // The comment environment content is verbatim and should be discarded
            if (strcmp(node_type, "comment_environment") == 0) {
                // Return an empty "comment" element to mark that a comment was here
                return ctx.builder.element("comment").final();
            }

            // Special handling for document container: skip begin_document/end_document
            if (strcmp(node_type, "document") == 0) {
                MarkBuilder& builder = ctx.builder;
                ElementBuilder doc_builder = builder.element("document");

                uint32_t child_count = ts_node_child_count(node);
                for (uint32_t i = 0; i < child_count; i++) {
                    if (should_skip_comment_and_space(node, i)) {
                        i++;
                        continue;
                    }

                    TSNode child = ts_node_child(node, i);
                    const char* child_type = ts_node_type(child);

                    // Skip begin_document and end_document nodes
                    if (strcmp(child_type, "begin_document") == 0 ||
                        strcmp(child_type, "end_document") == 0) {
                        continue;
                    }

                    Item child_item = convert_latex_node(ctx, child, source);
                    if (child_item.item != ITEM_NULL && !is_empty_string_sentinel(child_item)) {
                        doc_builder.child(child_item);
                    }
                }

                return doc_builder.final();
            }

            // NEW: section handling in hybrid grammar
            // section has: command field (section_command), toc field (optional brack_group), title field (curly_group)
            if (strcmp(node_type, "section") == 0) {
                MarkBuilder& builder = ctx.builder;

                // Extract section command to determine level
                TSNode cmd_node = ts_node_child_by_field_name(node, "command", 7);
                String* cmd_str = ts_node_is_null(cmd_node) ? nullptr : extract_text(ctx, cmd_node, source);
                const char* section_type = cmd_str ? cmd_str->chars : "section";

                // Skip backslash if present
                if (section_type[0] == '\\') {
                    section_type++;
                }

                ElementBuilder section_builder = builder.element(section_type);

                // Extract title
                TSNode title_node = ts_node_child_by_field_name(node, "title", 5);
                if (!ts_node_is_null(title_node)) {
                    Item title_item = convert_latex_node(ctx, title_node, source);
                    if (title_item.item != ITEM_NULL) {
                        section_builder.attr("title", title_item);
                    }
                }

                // Extract optional TOC title
                TSNode toc_node = ts_node_child_by_field_name(node, "toc", 3);
                if (!ts_node_is_null(toc_node)) {
                    Item toc_item = convert_latex_node(ctx, toc_node, source);
                    if (toc_item.item != ITEM_NULL) {
                        section_builder.attr("toc", toc_item);
                    }
                }

                // Add section content (remaining children)
                uint32_t child_count = ts_node_child_count(node);
                for (uint32_t i = 0; i < child_count; i++) {
                    // LaTeX comment handling
                    if (should_skip_comment_and_space(node, i)) {
                        i++;  // Skip both comment and following space
                        continue;
                    }

                    TSNode child = ts_node_child(node, i);
                    const char* child_type = ts_node_type(child);

                    // Skip the command, title, and toc nodes (already processed)
                    if (strcmp(child_type, "section_command") == 0 ||
                        strcmp(child_type, "curly_group") == 0 ||
                        strcmp(child_type, "brack_group") == 0) {
                        continue;
                    }

                    Item child_item = convert_latex_node(ctx, child, source);
                    if (child_item.item != ITEM_NULL && !is_empty_string_sentinel(child_item)) {
                        section_builder.child(child_item);
                    }
                }

                return section_builder.final();
            }

            // Special case: placeholder (#1, #2, etc.) - terminal node, extract text
            if (strcmp(node_type, "placeholder") == 0) {
                uint32_t start = ts_node_start_byte(node);
                uint32_t end = ts_node_end_byte(node);
                size_t len = end - start;
                MarkBuilder& builder = ctx.builder;
                return {.item = y2it(builder.createSymbol(source + start, len))};
            }

            // Special case: inline_math and display_math - store original source as attribute
            if (strcmp(node_type, "inline_math") == 0 || strcmp(node_type, "display_math") == 0 ||
                strcmp(node_type, "math") == 0) {
                MarkBuilder& builder = ctx.builder;
                ElementBuilder elem_builder = builder.element(node_type);

                // Store the original source text as 'source' attribute (strip $ delimiters)
                uint32_t source_start = ts_node_start_byte(node);
                uint32_t source_end = ts_node_end_byte(node);
                size_t source_len = source_end - source_start;

                // Strip delimiters: $...$ or $$...$$ or \(...\) or \[...\]
                const char* text = source + source_start;
                if (source_len >= 2) {
                    if (text[0] == '$' && text[source_len - 1] == '$') {
                        // $...$ or $$...$$
                        if (source_len >= 4 && text[1] == '$' && text[source_len - 2] == '$') {
                            // $$...$$
                            text += 2;
                            source_len -= 4;
                        } else {
                            // $...$
                            text += 1;
                            source_len -= 2;
                        }
                    } else if (source_len >= 4 && text[0] == '\\' && text[1] == '(' &&
                               text[source_len - 2] == '\\' && text[source_len - 1] == ')') {
                        // \(...\)
                        text += 2;
                        source_len -= 4;
                    } else if (source_len >= 4 && text[0] == '\\' && text[1] == '[' &&
                               text[source_len - 2] == '\\' && text[source_len - 1] == ']') {
                        // \[...\]
                        text += 2;
                        source_len -= 4;
                    }
                }

                String* src_str = builder.createString(text, source_len);
                Item src_item = {.item = s2it(src_str)};
                elem_builder.attr("source", src_item);

                // Also process children for the tree structure
                uint32_t child_count = ts_node_child_count(node);
                for (uint32_t i = 0; i < child_count; i++) {
                    TSNode child = ts_node_child(node, i);
                    Item child_item = convert_latex_node(ctx, child, source);
                    if (child_item.item != ITEM_NULL && !is_empty_string_sentinel(child_item)) {
                        elem_builder.child(child_item);
                    }
                }

                return elem_builder.final();
            }

            // Special case: linebreak_command (\\ with optional [<length>])
            if (strcmp(node_type, "linebreak_command") == 0) {
                MarkBuilder& builder = ctx.builder;
                ElementBuilder elem_builder = builder.element("linebreak_command");

                // Check for optional brack_group (length argument)
                uint32_t child_count = ts_node_child_count(node);

                for (uint32_t i = 0; i < child_count; i++) {
                    TSNode child = ts_node_child(node, i);
                    const char* child_type = ts_node_type(child);

                    // If there's a brack_group, extract and store the length
                    if (strcmp(child_type, "brack_group") == 0) {
                        // Get the text inside the brackets
                        uint32_t start = ts_node_start_byte(child);
                        uint32_t end = ts_node_end_byte(child);
                        if (end > start + 2) {  // Skip '[' and ']'
                            String* length_str = builder.createString(source + start + 1, end - start - 2);
                            Item length_item = {.item = s2it(length_str)};
                            elem_builder.attr("length", length_item);
                        }
                        break;
                    }
                }

                return elem_builder.final();
            }

            // Special case: control_symbol (\%, \&, \#, etc.)
            if (strcmp(node_type, "control_symbol") == 0) {
                uint32_t start = ts_node_start_byte(node);
                uint32_t end = ts_node_end_byte(node);
                if (end > start + 1) {
                    MarkBuilder& builder = ctx.builder;
                    char escaped_char = source[start + 1];

                    // Note: \\ is now handled by linebreak_command, not here

                    // Spacing commands: \, \! \; \: \/ \@ \space \<tab> \<newline> - preserve as space_cmd element
                    // These need special handling in the formatter
                    // Note: \<space>, \<tab>, and \<newline> all produce zero-width space
                    if (escaped_char == ',' || escaped_char == '!' ||
                        escaped_char == ';' || escaped_char == ':' ||
                        escaped_char == '/' || escaped_char == '@' ||
                        escaped_char == ' ' || escaped_char == '\t' ||
                        escaped_char == '\n' || escaped_char == '\r') {
                        ElementBuilder elem_builder = builder.element("space_cmd");
                        // Store the full command including backslash
                        String* cmd_str = builder.createString(source + start, end - start);
                        elem_builder.child({.item = s2it(cmd_str)});
                        return elem_builder.final();
                    }

                    // Discretionary hyphen: \- - preserve as space_cmd element
                    // Formatter will convert to soft hyphen U+00AD
                    if (escaped_char == '-') {
                        ElementBuilder elem_builder = builder.element("space_cmd");
                        String* cmd_str = builder.createString(source + start, end - start);
                        elem_builder.child({.item = s2it(cmd_str)});
                        return elem_builder.final();
                    }

                    // Diacritic commands: \^ \' \` \" \~ \= \. - create elements for formatting
                    // These need special handling to combine with the following character/group
                    if (escaped_char == '\'' || escaped_char == '`' || escaped_char == '^' ||
                        escaped_char == '"' || escaped_char == '~' || escaped_char == '=' ||
                        escaped_char == '.') {
                        // Create element with diacritic character as tag
                        char tag[2] = {escaped_char, '\0'};
                        ElementBuilder elem_builder = builder.element(tag);
                        return elem_builder.final();
                    }

                    // For other control symbols, return the escaped character as string
                    return {.item = s2it(builder.createString(source + start + 1, end - start - 1))};
                }
                return {.item = ITEM_NULL};
            }

            // Special case: ligature (---, --, ``, '', <<, >>) - convert to proper characters
            if (strcmp(node_type, "ligature") == 0) {
                uint32_t start = ts_node_start_byte(node);
                uint32_t end = ts_node_end_byte(node);
                size_t len = end - start;
                const char* text = source + start;
                MarkBuilder& builder = ctx.builder;

                // Convert ligatures to proper Unicode characters
                if (len == 3 && strncmp(text, "---", 3) == 0) {
                    return {.item = s2it(builder.createString("\u2014", 3))}; // em dash
                } else if (len == 2 && strncmp(text, "--", 2) == 0) {
                    return {.item = s2it(builder.createString("\u2013", 3))}; // en dash
                } else if (len == 2 && strncmp(text, "``", 2) == 0) {
                    return {.item = s2it(builder.createString("\u201C", 3))}; // left double quote
                } else if (len == 2 && strncmp(text, "''", 2) == 0) {
                    return {.item = s2it(builder.createString("\u201D", 3))}; // right double quote
                } else if (len == 2 && strncmp(text, "<<", 2) == 0) {
                    return {.item = s2it(builder.createString("\u00AB", 2))}; // left guillemet
                } else if (len == 2 && strncmp(text, ">>", 2) == 0) {
                    return {.item = s2it(builder.createString("\u00BB", 2))}; // right guillemet
                }
                // Fallback: return as-is
                return {.item = s2it(builder.createString(text, len))};
            }

            // Special case: nbsp (~) - convert to non-breaking space element
            if (strcmp(node_type, "nbsp") == 0) {
                MarkBuilder& builder = ctx.builder;
                ElementBuilder elem_builder = builder.element("nbsp");
                return elem_builder.final();
            }

            // Special case: brack_group that is NOT a command argument
            // In LaTeX, brackets only have special meaning as optional args after commands.
            // When a brack_group appears standalone (in paragraph/inline context), or when
            // it contains parse errors (unbalanced brackets), treat it as plain text.
            // Check if parent is a command - if not, this is a standalone brack_group
            // ALSO check if previous sibling is a command - LaTeX allows optional args after whitespace
            if (strcmp(node_type, "brack_group") == 0) {
                // Check for parse errors (MISSING ']') - if so, treat as literal text
                // This handles cases like `test[ {t]]ext}` where there's no valid closing bracket
                if (ts_node_has_error(node)) {
                    log_debug("latex_ts: brack_group has parse error - converting children to sequence");
                    MarkBuilder& builder = ctx.builder;

                    // Create an element to hold "[" + all children as a sequence
                    // We use a special "error_brack" tag that the formatter can flatten
                    ElementBuilder seq_builder = builder.element("_seq");
                    seq_builder.child({.item = s2it(builder.createString("["))});

                    // Process all children (skipping the '[' delimiter)
                    uint32_t child_count = ts_node_child_count(node);
                    for (uint32_t i = 0; i < child_count; i++) {
                        TSNode child = ts_node_child(node, i);
                        const char* child_type = ts_node_type(child);
                        // Skip bracket delimiters
                        if (strcmp(child_type, "[") == 0 || strcmp(child_type, "]") == 0) {
                            continue;
                        }
                        Item child_item = convert_latex_node(ctx, child, source);
                        if (child_item.item != ITEM_NULL && !is_empty_string_sentinel(child_item)) {
                            seq_builder.child(child_item);
                        }
                    }

                    return seq_builder.final();
                }

                TSNode parent = ts_node_parent(node);
                const char* parent_type = ts_node_is_null(parent) ? "" : ts_node_type(parent);

                // If parent is a command, linebreak_command, or section, this is a proper optional arg
                bool is_command_arg = (strcmp(parent_type, "command") == 0 ||
                                       strcmp(parent_type, "linebreak_command") == 0 ||
                                       strcmp(parent_type, "section") == 0);

                // Also check if preceded by a command (with optional whitespace/comments between)
                // LaTeX: \cmd{arg1} [opt] {arg2} - the [opt] is still part of \cmd's arguments
                if (!is_command_arg) {
                    TSNode prev = ts_node_prev_sibling(node);
                    while (!ts_node_is_null(prev)) {
                        const char* prev_type = ts_node_type(prev);
                        // Skip whitespace, comments, and curly_groups (previous mandatory args)
                        if (strcmp(prev_type, "space") == 0 ||
                            strcmp(prev_type, "line_comment") == 0 ||
                            strcmp(prev_type, "curly_group") == 0 ||
                            strcmp(prev_type, "brack_group") == 0) {
                            prev = ts_node_prev_sibling(prev);
                            continue;
                        }
                        // Also skip text nodes that are pure whitespace (sometimes parser labels " " as text)
                        if (strcmp(prev_type, "text") == 0) {
                            uint32_t start = ts_node_start_byte(prev);
                            uint32_t end = ts_node_end_byte(prev);
                            bool is_whitespace = true;
                            for (uint32_t i = start; i < end; i++) {
                                char c = source[i];
                                if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                                    is_whitespace = false;
                                    break;
                                }
                            }
                            if (is_whitespace) {
                                prev = ts_node_prev_sibling(prev);
                                continue;
                            }
                        }
                        // If we find a command, this brack_group is an optional arg
                        if (strcmp(prev_type, "command") == 0) {
                            is_command_arg = true;
                            log_debug("latex_ts: brack_group follows command - treating as optional arg");
                        }
                        break;
                    }
                }

                if (!is_command_arg) {
                    // This is a standalone brack_group - convert to text "[" + content + "]"
                    log_debug("latex_ts: converting standalone brack_group to text (parent=%s)", parent_type);
                    MarkBuilder& builder = ctx.builder;

                    uint32_t start = ts_node_start_byte(node);
                    uint32_t end = ts_node_end_byte(node);
                    size_t len = end - start;

                    // Return the entire brack_group as raw text, including brackets
                    return {.item = s2it(builder.createString(source + start, len))};
                }
                // Otherwise, fall through to normal container handling for command args
                // But strip the bracket delimiters
                {
                    MarkBuilder& builder = ctx.builder;
                    ElementBuilder elem_builder = builder.element(node_type);

                    uint32_t child_count = ts_node_child_count(node);
                    for (uint32_t i = 0; i < child_count; i++) {
                        // LaTeX comment handling
                        if (should_skip_comment_and_space(node, i)) {
                            i++;  // Skip both comment and following space
                            continue;
                        }

                        TSNode child = ts_node_child(node, i);
                        const char* child_type = ts_node_type(child);

                        // Skip bracket delimiters for brack_group
                        if (strcmp(child_type, "[") == 0 || strcmp(child_type, "]") == 0) {
                            continue;
                        }

                        Item child_item = convert_latex_node(ctx, child, source);
                        // Skip NULL items and empty string sentinels
                        if (child_item.item != ITEM_NULL && !is_empty_string_sentinel(child_item)) {
                            elem_builder.child(child_item);
                        }
                    }

                    return elem_builder.final();
                }
            }

            // TODO: Add more specific handlers
            // For now, create generic element with node type as tag
            {
                MarkBuilder& builder = ctx.builder;
                ElementBuilder elem_builder = builder.element(node_type);

                uint32_t child_count = ts_node_child_count(node);
                for (uint32_t i = 0; i < child_count; i++) {
                    // LaTeX comment handling
                    if (should_skip_comment_and_space(node, i)) {
                        i++;  // Skip both comment and following space
                        continue;
                    }

                    TSNode child = ts_node_child(node, i);
                    const char* child_type = ts_node_type(child);

                    // Special case for paragraph: detect `controlspace_command` followed by `space`
                    // Pattern: `\<newline>` followed by another newline creates paragraph break
                    if (strcmp(node_type, "paragraph") == 0 && strcmp(child_type, "controlspace_command") == 0) {
                        // Check if next child is a space node
                        if (i + 1 < child_count) {
                            TSNode next_child = ts_node_child(node, i + 1);
                            const char* next_type = ts_node_type(next_child);

                            if (strcmp(next_type, "space") == 0) {
                                // Check if the space spans a newline (by checking start/end positions)
                                uint32_t space_start_row = ts_node_start_point(next_child).row;
                                uint32_t space_end_row = ts_node_end_point(next_child).row;

                                if (space_end_row > space_start_row) {
                                    // Space spans multiple lines - it contains newline!
                                    // This means: \<NL> followed by <NL> → paragraph break

                                    // First add the controlspace_command (will output ZWS + space)
                                    Item child_item = convert_latex_node(ctx, child, source);
if (child_item.item != ITEM_NULL && !is_empty_string_sentinel(child_item)) {
                                        elem_builder.child(child_item);
                                    }

                                    // Then add a paragraph break symbol
                                    elem_builder.child({.item = y2it(builder.createSymbol("parbreak"))});

                                    // Skip the space node since we converted it to parbreak
                                    i++;
                                    continue;
                                }
                            }
                        }
                    }

                    Item child_item = convert_latex_node(ctx, child, source);
                    if (child_item.item != ITEM_NULL && !is_empty_string_sentinel(child_item)) {
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
    // Extract command name - hybrid grammar uses "name" field
    TSNode cmd_name_node = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(cmd_name_node)) {
        // Fallback for old grammar structure
        cmd_name_node = ts_node_child_by_field_name(node, "command", 7);
    }
    if (ts_node_is_null(cmd_name_node)) {
        return {.item = ITEM_NULL};
    }

    String* cmd_name_str = extract_text(ctx, cmd_name_node, source);
    const char* cmd_name = cmd_name_str->chars;

    // Skip backslash if present
    if (cmd_name[0] == '\\') {
        cmd_name++;
    }

    // Check if this is a diacritic command with empty braces: \^{}, \~{}, etc.
    const DiacriticInfo* diacritic = find_diacritic(cmd_name);
    if (diacritic) {
        // Check if it has an empty curly_group argument
        uint32_t child_count = ts_node_child_count(node);
        bool has_empty_group = false;

        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            const char* child_type = ts_node_type(child);

            if (strcmp(child_type, "curly_group") == 0) {
                // Count non-brace children in the group
                uint32_t arg_child_count = ts_node_child_count(child);
                int content_count = 0;
                for (uint32_t j = 0; j < arg_child_count; j++) {
                    TSNode arg_child = ts_node_child(child, j);
                    const char* arg_child_type = ts_node_type(arg_child);
                    if (strcmp(arg_child_type, "{") != 0 && strcmp(arg_child_type, "}") != 0) {
                        content_count++;
                    }
                }
                if (content_count == 0) {
                    has_empty_group = true;
                }
                break;
            }
        }

        // If diacritic with empty group, return standalone + ZWSP as string
        if (has_empty_group) {
            MarkBuilder& builder = ctx.builder;
            StringBuf* sb = ctx.sb;
            stringbuf_reset(sb);
            stringbuf_append_str(sb, diacritic->standalone);
            stringbuf_append_str(sb, "\u200B"); // Zero-width space
            String* result = stringbuf_to_string(sb);
            return {.item = s2it(result)};
        }
    }

    // Create element with command name as tag
    MarkBuilder& builder = ctx.builder;
    ElementBuilder cmd_elem_builder = builder.element(cmd_name);

    // Special handling for macro definition commands
    // For \newcommand{\name}{definition}, output: newcommand[ "\\name", Element(definition) ]
    bool is_macro_def = (strcmp(cmd_name, "newcommand") == 0 ||
                         strcmp(cmd_name, "renewcommand") == 0 ||
                         strcmp(cmd_name, "providecommand") == 0 ||
                         strcmp(cmd_name, "def") == 0 ||
                         strcmp(cmd_name, "gdef") == 0 ||
                         strcmp(cmd_name, "edef") == 0 ||
                         strcmp(cmd_name, "xdef") == 0);

    // Extract arguments (curly_group and brack_group children)
    uint32_t child_count = ts_node_child_count(node);
    int arg_index = 0;
    for (uint32_t i = 0; i < child_count; i++) {
        // LaTeX comment handling
        if (should_skip_comment_and_space(node, i)) {
            i++;  // Skip both comment and following space
            continue;
        }

        TSNode child = ts_node_child(node, i);
        const char* child_type = ts_node_type(child);

        // Process bracket groups (optional arguments like [num])
        if (strcmp(child_type, "brack_group") == 0) {
            // Create brack_group element to preserve structure
            ElementBuilder brack_builder = builder.element("brack_group");
            uint32_t arg_child_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < arg_child_count; j++) {
                TSNode arg_child = ts_node_child(child, j);
                const char* arg_child_type = ts_node_type(arg_child);
                // Skip brackets
                if (strcmp(arg_child_type, "[") == 0 || strcmp(arg_child_type, "]") == 0) {
                    continue;
                }
                Item arg_item = convert_latex_node(ctx, arg_child, source);
                if (arg_item.item != ITEM_NULL) {
                    brack_builder.child(arg_item);
                }
            }
            cmd_elem_builder.child(brack_builder.final());
            continue;
        }

        // Process curly groups (required arguments)
        if (strcmp(child_type, "curly_group") == 0) {
            // For macro definitions, first curly_group contains the macro name
            if (is_macro_def && arg_index == 0) {
                // Extract macro name: {\hello} -> "\\hello"
                uint32_t arg_child_count = ts_node_child_count(child);
                for (uint32_t j = 0; j < arg_child_count; j++) {
                    TSNode arg_child = ts_node_child(child, j);
                    const char* arg_child_type = ts_node_type(arg_child);
                    // Skip braces
                    if (strcmp(arg_child_type, "{") == 0 || strcmp(arg_child_type, "}") == 0) {
                        continue;
                    }
                    // If it's a command, extract its full name with backslash
                    if (strcmp(arg_child_type, "command") == 0) {
                        String* macro_name_str = extract_text(ctx, arg_child, source);
                        // Store as string with backslash prefix
                        cmd_elem_builder.child({.item = s2it(macro_name_str)});
                    } else {
                        // Some other content, process normally
                        Item arg_item = convert_latex_node(ctx, arg_child, source);
                        if (arg_item.item != ITEM_NULL) {
                            cmd_elem_builder.child(arg_item);
                        }
                    }
                }
                arg_index++;
                continue;
            }

            // For macro definitions, wrap definition in curly_group element
            if (is_macro_def) {
                ElementBuilder curly_builder = builder.element("curly_group");
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
                        curly_builder.child(arg_item);
                    }
                }
                cmd_elem_builder.child(curly_builder.final());
                arg_index++;
                continue;
            }

            // Normal case: unwrap curly group and add its contents directly
            uint32_t arg_child_count = ts_node_child_count(child);
            bool has_content = false;
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
                    has_content = true;
                }
            }

            // If curly_group was empty, add an empty curly_group element as marker
            // This helps distinguish \cmd from \cmd{} in the formatter
            if (!has_content) {
                ElementBuilder empty_marker = builder.element("curly_group");
                cmd_elem_builder.child(empty_marker.final());
            }
        }
    }

    return cmd_elem_builder.final();
}

// Convert environment node
static Item convert_environment(InputContext& ctx, TSNode node, const char* source) {
    // Extract environment name from \begin{name}
    // Hybrid grammar: begin_env has name field containing curly_group
    TSNode begin_node = ts_node_child_by_field_name(node, "begin", 5);
    if (ts_node_is_null(begin_node)) {
        return {.item = ITEM_NULL};
    }

    // Get the name field from begin node (curly_group in hybrid grammar)
    TSNode name_node = ts_node_child_by_field_name(begin_node, "name", 4);
    if (ts_node_is_null(name_node)) {
        return {.item = ITEM_NULL};
    }

    // Extract environment name - may be curly_group, extract text content
    String* env_name = nullptr;
    const char* name_type = ts_node_type(name_node);
    if (strcmp(name_type, "curly_group") == 0) {
        // Find text child inside curly group
        uint32_t child_count = ts_node_child_count(name_node);
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(name_node, i);
            const char* child_type = ts_node_type(child);
            if (strcmp(child_type, "text") == 0 || strcmp(child_type, "env_name") == 0) {
                env_name = extract_text(ctx, child, source);
                break;
            }
            // Skip braces
            if (strcmp(child_type, "{") != 0 && strcmp(child_type, "}") != 0) {
                env_name = extract_text(ctx, child, source);
                break;
            }
        }
        // If still null, extract entire group content (minus braces)
        if (!env_name) {
            uint32_t start = ts_node_start_byte(name_node);
            uint32_t end = ts_node_end_byte(name_node);
            const char* text = source + start;
            size_t len = end - start;
            // Skip leading { and trailing }
            if (len >= 2 && text[0] == '{' && text[len-1] == '}') {
                text++;
                len -= 2;
            }
            env_name = ctx.builder.createString(text, len);
        }
    } else if (strcmp(name_type, "env_name") == 0) {
        // Direct env_name node (for verbatim/math environments)
        env_name = extract_text(ctx, name_node, source);
    } else {
        // Fallback: try extracting text directly
        TSNode text_node = ts_node_child_by_field_name(name_node, "text", 4);
        if (!ts_node_is_null(text_node)) {
            env_name = extract_text(ctx, text_node, source);
        } else {
            env_name = extract_text(ctx, name_node, source);
        }
    }

    if (!env_name || env_name->len == 0) {
        log_warn("Failed to extract environment name");
        env_name = ctx.builder.createString("unknown", 7);
    }

    // Special handling for comment environment
    // The comment environment treats all content as verbatim until \end{comment}
    // This includes handling malformed \end{comment} (missing brace) inside the content
    if (strcmp(env_name->chars, "comment") == 0) {
        // Find the real \end{comment} in the raw source
        // The parser may have incorrectly included content after it due to malformed instances
        uint32_t env_start = ts_node_start_byte(node);
        uint32_t env_end = ts_node_end_byte(node);

        // Look for the first valid \end{comment} after \begin{comment}
        // A valid \end{comment} is followed by } (not inside the comment content)
        uint32_t begin_end = ts_node_end_byte(begin_node);
        const char* search_start = source + begin_end;
        size_t search_len = env_end - begin_end;

        // Find \end{comment} followed by optional whitespace/newlines and the next content
        const char* real_end = nullptr;
        const char* p = search_start;
        const char* limit = source + env_end;

        while (p < limit) {
            // Look for \end{comment}
            if (p + 13 <= limit && strncmp(p, "\\end{comment}", 13) == 0) {
                real_end = p + 13;  // Position right after \end{comment}
                break;
            }
            p++;
        }

        // Create a container with the content AFTER the comment environment
        if (real_end && real_end < limit) {
            // There's content after \end{comment} that was incorrectly included
            // Skip any leading whitespace/newline after \end{comment}
            while (real_end < limit && (*real_end == ' ' || *real_end == '\t' || *real_end == '\n' || *real_end == '\r')) {
                real_end++;
            }

            size_t remaining_len = limit - real_end;
            if (remaining_len > 0) {
                // We have content after the comment that needs to be processed
                // Create a _seq element containing:
                // 1. Empty comment element
                // 2. The remaining content (which needs to be processed)

                // Process the remaining content by stripping comments
                std::string processed;
                processed.reserve(remaining_len);

                size_t i = 0;
                while (i < remaining_len) {
                    // Check for \begin{comment}
                    if (i + 15 <= remaining_len && strncmp(real_end + i, "\\begin{comment}", 15) == 0) {
                        // Skip until \end{comment}
                        i += 15;
                        while (i < remaining_len) {
                            if (i + 13 <= remaining_len && strncmp(real_end + i, "\\end{comment}", 13) == 0) {
                                i += 13;
                                // Skip trailing newline
                                if (i < remaining_len && real_end[i] == '\n') {
                                    i++;
                                }
                                break;
                            }
                            i++;
                        }
                        continue;
                    }

                    // Check for line comment (%)
                    if (real_end[i] == '%') {
                        // Skip % and everything until end of line (% eats the newline)
                        while (i < remaining_len && real_end[i] != '\n') {
                            i++;
                        }
                        if (i < remaining_len && real_end[i] == '\n') {
                            i++;
                        }
                        continue;
                    }

                    // Copy regular character
                    processed += real_end[i];
                    i++;
                }

                if (!processed.empty()) {
                    // Create a _seq element with comment + remaining text
                    ElementBuilder seq = ctx.builder.element("_seq");
                    seq.child(ctx.builder.element("comment").final());
                    seq.child(ctx.builder.createStringItem(processed.c_str(), processed.size()));
                    return seq.final();
                }
            }
        }

        // No remaining content after comment, return empty comment element
        return ctx.builder.element("comment").final();
    }

    // Create element with environment name as tag
    MarkBuilder& builder = ctx.builder;
    ElementBuilder env_elem_builder = builder.element(env_name->chars);

    // Extract arguments from begin_env (e.g., {2} for multicols)
    // The grammar has: begin_env -> \begin name:curly_group options:brack_group? arg:curly_group*
    uint32_t begin_child_count = ts_node_child_count(begin_node);
    for (uint32_t i = 0; i < begin_child_count; i++) {
        TSNode begin_child = ts_node_child(begin_node, i);
        const char* field_name = ts_node_field_name_for_child(begin_node, i);
        if (field_name && strcmp(field_name, "arg") == 0) {
            // Extract the text content from the curly group
            Item arg_item = convert_latex_node(ctx, begin_child, source);
            if (arg_item.item != ITEM_NULL) {
                env_elem_builder.child(arg_item);
            }
        }
    }

    // Process environment content (between \begin and \end)
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        // LaTeX comment handling
        if (should_skip_comment_and_space(node, i)) {
            i++;  // Skip both comment and following space
            continue;
        }

        TSNode child = ts_node_child(node, i);
        const char* child_type = ts_node_type(child);

        // Skip begin/end nodes (old grammar: "begin"/"end", hybrid: "begin_env"/"end_env")
        if (strcmp(child_type, "begin") == 0 || strcmp(child_type, "end") == 0 ||
            strcmp(child_type, "begin_env") == 0 || strcmp(child_type, "end_env") == 0) {
            continue;
        }

        Item child_item = convert_latex_node(ctx, child, source);
        if (child_item.item != ITEM_NULL && !is_empty_string_sentinel(child_item)) {
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
