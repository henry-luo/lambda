#include "input.hpp"
#include "../mark_builder.hpp"
#include "input-context.hpp"
#include "source_tracker.hpp"

using namespace lambda;

// External declaration for thread-local input context
extern __thread Context* input_context;

// Helper macro to access tracker from context
#define TRACKER (ctx.tracker())

// Forward declarations for CSS stylesheet parsing
static Item parse_css_stylesheet(InputContext& ctx);
static Array* parse_css_rules(InputContext& ctx);
static Item parse_css_rule(InputContext& ctx);
static Item parse_css_at_rule(InputContext& ctx);
static Item parse_css_qualified_rule(InputContext& ctx);
static Array* parse_css_selectors(InputContext& ctx);
static Item parse_css_selector(InputContext& ctx);
static Array* parse_css_declarations(InputContext& ctx);
static Item parse_css_declaration(InputContext& ctx);
static Item parse_css_value(InputContext& ctx);
static Item parse_css_function(InputContext& ctx);
static Item parse_css_measure(InputContext& ctx);
static Item parse_css_color(InputContext& ctx);
static Item parse_css_string(InputContext& ctx);
static Item parse_css_url(InputContext& ctx);
static Item parse_css_number(InputContext& ctx);
static Item parse_css_identifier(InputContext& ctx);
static Array* parse_css_value_list(InputContext& ctx);
static Array* parse_css_function_params(InputContext& ctx);
static Item flatten_single_array(Array* arr);

// Global array to collect all rules including nested ones
static Array* g_all_rules = NULL;

// Helper functions for whitespace/comment skipping that still use tracker directly
static void skip_css_whitespace(SourceTracker& tracker) {
    while (!tracker.atEnd() && (tracker.current() == ' ' || tracker.current() == '\n' ||
                                  tracker.current() == '\r' || tracker.current() == '\t')) {
        tracker.advance();
    }
}

static void skip_css_comments(SourceTracker& tracker) {
    skip_css_whitespace(tracker);
    while (!tracker.atEnd() && tracker.current() == '/' && tracker.peek(1) == '*') {
        tracker.advance(2); // Skip /*
        while (!tracker.atEnd() && !(tracker.current() == '*' && tracker.peek(1) == '/')) {
            tracker.advance();
        }
        if (!tracker.atEnd() && tracker.current() == '*' && tracker.peek(1) == '/') {
            tracker.advance(2); // Skip */
        }
        skip_css_whitespace(tracker);
    }
}

static bool is_css_identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-';
}

static bool is_css_identifier_char(char c) {
    return is_css_identifier_start(c) || (c >= '0' && c <= '9');
}

static bool is_css_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_css_hex_digit(char c) {
    return is_css_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// CSS Stylesheet parsing functions
static Item parse_css_stylesheet(InputContext& ctx) {
    SourceTracker& tracker = TRACKER;

    skip_css_comments(tracker);

    // Create separate collections for different rule types
    Array* rules = array_pooled(ctx.input()->pool);          // Regular CSS rules
    Array* keyframes = array_pooled(ctx.input()->pool);      // @keyframes rules
    Array* media_queries = array_pooled(ctx.input()->pool);  // @media rules
    Array* supports_queries = array_pooled(ctx.input()->pool); // @supports rules
    Array* font_faces = array_pooled(ctx.input()->pool);     // @font-face rules
    Array* other_at_rules = array_pooled(ctx.input()->pool); // Other at-rules

    // Initialize global array to collect ALL rules including nested ones
    g_all_rules = rules;

    if (!rules || !keyframes || !media_queries || !supports_queries || !font_faces || !other_at_rules) {
        return {.item = ITEM_ERROR};
    }

    // Parse all rules and categorize them
    while (!tracker.atEnd()) {
        skip_css_comments(tracker);
        if (tracker.atEnd()) break;

        printf("Parsing CSS rule\n");

        // Check if this is an at-rule before parsing
        bool is_at_rule = (tracker.current() == '@');
        const char* at_rule_name = NULL;

        if (is_at_rule) {
            // Extract at-rule name for categorization
            const char* name_start = tracker.rest() + 1; // Skip @
            const char* name_end = name_start;
            while (*name_end && is_css_identifier_char(*name_end)) {
                name_end++;
            }

            if (name_end > name_start) {
                size_t name_len = name_end - name_start;
                char* name_buf = (char*)malloc(name_len + 1);
                if (name_buf) {
                    strncpy(name_buf, name_start, name_len);
                    name_buf[name_len] = '\0';
                    at_rule_name = name_buf;
                }
            }
        }

        Item rule = parse_css_rule(ctx);
        if (rule .item != ITEM_ERROR) {

            if (is_at_rule && at_rule_name) {
                // Categorize at-rules
                if (strcmp(at_rule_name, "keyframes") == 0) {
                    array_append(keyframes, rule, ctx.input()->pool);
                } else if (strcmp(at_rule_name, "media") == 0) {
                    array_append(media_queries, rule, ctx.input()->pool);
                } else if (strcmp(at_rule_name, "supports") == 0) {
                    array_append(supports_queries, rule, ctx.input()->pool);
                } else if (strcmp(at_rule_name, "font-face") == 0) {
                    array_append(font_faces, rule, ctx.input()->pool);
                } else {
                    array_append(other_at_rules, rule, ctx.input()->pool);
                }
                free((void*)at_rule_name); // Free the allocated name buffer
            } else {
                // Regular CSS rule
                array_append(rules, rule, ctx.input()->pool);
            }
        } else {
            // Skip invalid rule content until next rule or end
            while (!tracker.atEnd() && tracker.current() != '}' && tracker.current() != '@') {
                tracker.advance();
            }
            if (tracker.current() == '}') {
                tracker.advance();
            }

            if (at_rule_name) {
                free((void*)at_rule_name);
            }
        }

        skip_css_comments(tracker);
    }

    // Build stylesheet element with all collections as attributes
    ElementBuilder stylesheet = ctx.builder().element("stylesheet");
    stylesheet.attr("rules", {.item = (uint64_t)rules});

    if (keyframes->length > 0) {
        stylesheet.attr("keyframes", {.item = (uint64_t)keyframes});
    }
    if (media_queries->length > 0) {
        stylesheet.attr("media", {.item = (uint64_t)media_queries});
    }
    if (supports_queries->length > 0) {
        stylesheet.attr("supports", {.item = (uint64_t)supports_queries});
    }
    if (font_faces->length > 0) {
        stylesheet.attr("font_faces", {.item = (uint64_t)font_faces});
    }
    if (other_at_rules->length > 0) {
        stylesheet.attr("at_rules", {.item = (uint64_t)other_at_rules});
    }

    return stylesheet.final();
}

static Array* parse_css_rules(InputContext& ctx) {
    SourceTracker& tracker = TRACKER;
    Array* rules = array_pooled(ctx.input()->pool);
    if (!rules) return NULL;

    while (!tracker.atEnd()) {
        skip_css_comments(tracker);
        if (tracker.atEnd()) break;

        printf("Parsing CSS rule\n");
        Item rule = parse_css_rule(ctx);
        if (rule .item != ITEM_ERROR) {
            array_append(rules, rule, ctx.input()->pool);
        } else {
            // Skip invalid rule content until next rule or end
            while (!tracker.atEnd() && tracker.current() != '}' && tracker.current() != '@') {
                tracker.advance();
            }
            if (tracker.current() == '}') {
                tracker.advance();
            }
        }

        skip_css_comments(tracker);
    }

    return rules;
}

static Item parse_css_rule(InputContext& ctx) {
    SourceTracker& tracker = TRACKER;
    skip_css_comments(tracker);

    if (tracker.current() == '@') {
        return parse_css_at_rule(ctx);
    } else {
        return parse_css_qualified_rule(ctx);
    }
}

static Item parse_css_at_rule(InputContext& ctx) {
    SourceTracker& tracker = TRACKER;
    if (tracker.current() != '@') return {.item = ITEM_ERROR};

    tracker.advance(); // Skip @

    // Parse at-rule name
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);  // Reset buffer to avoid accumulating previous content
    while (is_css_identifier_char(tracker.current())) {
        stringbuf_append_char(sb, tracker.current());
        tracker.advance();
    }

    String* at_rule_name = ctx.builder().createString(sb->str->chars, sb->length);
    if (!at_rule_name) return {.item = ITEM_ERROR};

    // Start building the at-rule element
    ElementBuilder at_rule = ctx.builder().element("at-rule");
    at_rule.attr("name", at_rule_name->chars);

    skip_css_comments(tracker);

    // Parse at-rule prelude (everything before { or ;)
    StringBuf* prelude_sb = stringbuf_new(ctx.input()->pool);
    int paren_depth = 0;

    while (!tracker.atEnd() && (tracker.current() != '{' && tracker.current() != ';')) {
        char c = tracker.current();
        if (c == '(') paren_depth++;
        else if (c == ')') paren_depth--;

        // Don't break on braces inside parentheses
        if (c == '{' && paren_depth > 0) {
            stringbuf_append_char(prelude_sb, c);
            tracker.advance();
            continue;
        }

        if (c == '{' || c == ';') break;

        stringbuf_append_char(prelude_sb, c);
        tracker.advance();
    }

    String* prelude_str = stringbuf_to_string(prelude_sb);
    if (prelude_str && prelude_str->len > 0) {
        char* trimmed = input_trim_whitespace(prelude_str->chars);
        if (trimmed && strlen(trimmed) > 0) {
            at_rule.attr("prelude", trimmed);
        }
        if (trimmed) free(trimmed);
    }

    skip_css_comments(tracker);

    if (tracker.current() == '{') {
        tracker.advance(); // Skip opening brace

        // Parse nested rules or declarations
        if (strcmp(at_rule_name->chars, "media") == 0 ||
            strcmp(at_rule_name->chars, "supports") == 0 ||
            strcmp(at_rule_name->chars, "document") == 0 ||
            strcmp(at_rule_name->chars, "container") == 0) {
            // These at-rules contain nested rules
            Array* nested_rules = array_pooled(ctx.input()->pool);
            if (nested_rules) {
                // Parse nested CSS rules until we hit the closing brace
                while (!tracker.atEnd() && tracker.current() != '}') {
                    skip_css_comments(tracker);
                    if (tracker.current() == '}') break;

                    Item nested_rule = parse_css_rule(ctx);
                    if (nested_rule .item != ITEM_ERROR) {
                        array_append(nested_rules, nested_rule, ctx.input()->pool);

                        // Also add nested rule to global rules array
                        if (g_all_rules) {
                            array_append(g_all_rules, nested_rule, ctx.input()->pool);
                            printf("DEBUG: Added nested rule to global rules array\n");
                        }
                    } else {
                        // Skip malformed rule
                        while (!tracker.atEnd() && tracker.current() != '}' && tracker.current() != '{') {
                            tracker.advance();
                        }
                        if (tracker.current() == '{') {
                            // Skip entire block
                            int brace_count = 1;
                            tracker.advance();
                            while (!tracker.atEnd() && brace_count > 0) {
                                if (tracker.current() == '{') brace_count++;
                                else if (tracker.current() == '}') brace_count--;
                                tracker.advance();
                            }
                        }
                    }
                    skip_css_comments(tracker);
                }

                at_rule.attr("rules", {.item = (uint64_t)nested_rules});
            }
        } else if (strcmp(at_rule_name->chars, "keyframes") == 0) {
            // @keyframes contains keyframe rules
            Array* keyframe_rules = array_pooled(ctx.input()->pool);
            if (keyframe_rules) {
                while (!tracker.atEnd() && tracker.current() != '}') {
                    skip_css_comments(tracker);
                    if (tracker.current() == '}') break;

                    // Parse keyframe selector (0%, 50%, from, to, etc.)
                    StringBuf* keyframe_sb = stringbuf_new(ctx.input()->pool);
                    while (!tracker.atEnd() && tracker.current() != '{' && tracker.current() != '}') {
                        stringbuf_append_char(keyframe_sb, tracker.current());
                        tracker.advance();
                    }
                    String* keyframe_selector = stringbuf_to_string(keyframe_sb);

                    if (keyframe_selector && tracker.current() == '{') {
                        tracker.advance(); // Skip opening brace

                        ElementBuilder keyframe_rule = ctx.builder().element("keyframe");

                        char* trimmed = input_trim_whitespace(keyframe_selector->chars);
                        if (trimmed) {
                            keyframe_rule.attr("selector", trimmed);
                            free(trimmed);
                        }

                        // Parse declarations within keyframe
                        while (!tracker.atEnd() && tracker.current() != '}') {
                            skip_css_comments(tracker);
                            if (tracker.current() == '}') break;

                            // Parse property name
                            StringBuf* prop_sb = stringbuf_new(ctx.input()->pool);
                            while (!tracker.atEnd() && tracker.current() != ':' && tracker.current() != ';' && tracker.current() != '}' && !isspace(tracker.current())) {
                                stringbuf_append_char(prop_sb, tracker.current());
                                tracker.advance();
                            }
                            String* property_str = stringbuf_to_string(prop_sb);
                            if (!property_str) {
                                // Skip to next declaration
                                while (!tracker.atEnd() && tracker.current() != ';' && tracker.current() != '}') tracker.advance();
                                if (tracker.current() == ';') tracker.advance();
                                continue;
                            }

                            skip_css_comments(tracker);

                            if (tracker.current() == ':') {
                                tracker.advance(); // Skip colon
                                skip_css_comments(tracker);

                                // Parse value list
                                Array* values = parse_css_value_list(ctx);
                                if (values) {
                                    Item values_item = flatten_single_array(values);
                                    keyframe_rule.attr(property_str->chars, values_item);
                                }

                                // Check for !important
                                skip_css_comments(tracker);
                                if (tracker.current() == '!' && tracker.match("!important")) {
                                    tracker.advance(10);
                                }
                            }

                            skip_css_comments(tracker);
                            if (tracker.current() == ';') {
                                tracker.advance(); // Skip semicolon
                                skip_css_comments(tracker);
                            }
                        }

                        if (tracker.current() == '}') {
                            tracker.advance(); // Skip closing brace
                        }

                        array_append(keyframe_rules, keyframe_rule.final(), ctx.input()->pool);
                    }

                    skip_css_comments(tracker);
                }

                at_rule.attr("keyframes", {.item = (uint64_t)keyframe_rules});
            }
        } else {
            // Other at-rules contain declarations - parse them directly as properties
            while (!tracker.atEnd() && tracker.current() != '}') {
                skip_css_comments(tracker);
                if (tracker.current() == '}') break;

                // Parse property name
                StringBuf* prop_sb = stringbuf_new(ctx.input()->pool);
                while (!tracker.atEnd() && tracker.current() != ':' && tracker.current() != ';' && tracker.current() != '}' && !isspace(tracker.current())) {
                    stringbuf_append_char(prop_sb, tracker.current());
                    tracker.advance();
                }
                String* property_str = stringbuf_to_string(prop_sb);
                if (!property_str) {
                    // Skip to next declaration
                    while (!tracker.atEnd() && tracker.current() != ';' && tracker.current() != '}') tracker.advance();
                    if (tracker.current() == ';') tracker.advance();
                    continue;
                }

                skip_css_comments(tracker);

                if (tracker.current() == ':') {
                    tracker.advance(); // Skip colon
                    skip_css_comments(tracker);

                    // Parse value list
                    Array* values = parse_css_value_list(ctx);
                    if (values) {
                        // Flatten single property value array
                        Item values_item = flatten_single_array(values);
                        at_rule.attr(property_str->chars, values_item);
                    }
                    // Check for !important
                    skip_css_comments(tracker);
                    if (tracker.current() == '!' && tracker.match("!important")) {
                        tracker.advance(10);
                        // Could add importance as property_name + "_important" if needed
                    }
                }

                skip_css_comments(tracker);
                if (tracker.current() == ';') {
                    tracker.advance(); // Skip semicolon
                    skip_css_comments(tracker);
                }
            }
        }

        skip_css_comments(tracker);
        if (tracker.current() == '}') {
            tracker.advance(); // Skip closing brace
        }
    } else if (tracker.current() == ';') {
        tracker.advance(); // Skip semicolon
    }

    return at_rule.final();
}

static Item parse_css_qualified_rule(InputContext& ctx) {
    SourceTracker& tracker = TRACKER;

    // Start building the rule element
    ElementBuilder rule = ctx.builder().element("rule");

    // Parse selectors
    printf("Parsing CSS qualified rule\n");
    Array* selectors = parse_css_selectors(ctx);
    if (selectors) {
        // Flatten single selector array
        Item selectors_item = flatten_single_array(selectors);
        rule.attr("_", selectors_item);
    }

    skip_css_comments(tracker);

    if (tracker.current() == '{') {
        tracker.advance(); // Skip opening brace

        // Parse declarations and add them directly as properties of the rule
        while (!tracker.atEnd() && tracker.current() != '}') {
            skip_css_comments(tracker);
            if (tracker.current() == '}') break;

            // Parse property name
            StringBuf* sb = ctx.sb;
            stringbuf_reset(sb);  // Reset the buffer before parsing each property
            while (!tracker.atEnd() && tracker.current() != ':' && tracker.current() != ';' && tracker.current() != '}' && !isspace(tracker.current())) {
                stringbuf_append_char(sb, tracker.current());
                tracker.advance();
            }
            String* property_str = ctx.builder().createString(sb->str->chars, sb->length);
            printf("got CSS property: %s\n", property_str ? property_str->chars : "NULL");
            if (!property_str) {
                // Skip to next declaration
                while (!tracker.atEnd() && tracker.current() != ';' && tracker.current() != '}') tracker.advance();
                if (tracker.current() == ';') tracker.advance();
                continue;
            }

            skip_css_comments(tracker);
            if (tracker.current() == ':') {
                tracker.advance(); // Skip colon
                skip_css_comments(tracker);

                // Parse value list
                Array* values = parse_css_value_list(ctx);
                if (values) {
                    // Flatten single property value array
                    Item values_item = flatten_single_array(values);

                    // Check for !important before adding the property
                    skip_css_comments(tracker);
                    bool is_important = false;
                    if (tracker.current() == '!' && tracker.match("!important")) {
                        tracker.advance(10);
                        is_important = true;
                    }

                    // If important, add the flag as a separate property
                    printf("Adding property %s with values %p\n", property_str->chars, (void*)values_item.item);
                    rule.attr(property_str->chars, values_item);

                    if (is_important) {
                        // Add an "important" flag attribute with boolean true value
                        StringBuf* important_sb = stringbuf_new(ctx.input()->pool);
                        stringbuf_append_str(important_sb, property_str->chars);
                        stringbuf_append_str(important_sb, "-important");
                        String* important_key = stringbuf_to_string(important_sb);

                        Item true_item = {.item = ITEM_TRUE};
                        rule.attr(important_key->chars, true_item);
                    }
                }
            }

            skip_css_comments(tracker);
            if (tracker.current() == ';') {
                tracker.advance(); // Skip semicolon
                skip_css_comments(tracker);
            }
        }

        skip_css_comments(tracker);
        if (tracker.current() == '}') {
            tracker.advance(); // Skip closing brace
        }
    }

    return rule.final();
}

static Array* parse_css_selectors(InputContext& ctx) {
    SourceTracker& tracker = TRACKER;
    Array* selectors = array_pooled(ctx.input()->pool);
    if (!selectors) return NULL;

    while (!tracker.atEnd() && tracker.current() != '{') {
        skip_css_comments(tracker);
        if (tracker.current() == '{') break;

        Item selector = parse_css_selector(ctx);
        if (selector .item != ITEM_ERROR) {
            array_append(selectors, selector, ctx.input()->pool);
        }

        skip_css_comments(tracker);
        if (tracker.current() == ',') {
            tracker.advance(); // Skip comma separator
            skip_css_comments(tracker);
        } else if (tracker.current() != '{') {
            break;
        }
    }

    return selectors;
}

static Item parse_css_selector(InputContext& ctx) {
    SourceTracker& tracker = TRACKER;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);  // Reset the buffer before parsing each selector

    // Parse selector text until comma or opening brace
    // Handle complex selectors including attribute selectors, pseudo-classes, pseudo-elements
    int bracket_depth = 0;
    int paren_depth = 0;

    while (!tracker.atEnd() && tracker.current() != ',' && tracker.current() != '{') {
        char c = tracker.current();

        // Track bracket and parenthesis depth for complex selectors
        if (c == '[') {
            bracket_depth++;
        } else if (c == ']') {
            bracket_depth--;
        } else if (c == '(') {
            paren_depth++;
        } else if (c == ')') {
            paren_depth--;
        }

        // Handle escaped characters in selectors
        if (c == '\\' && tracker.peek(1)) {
            stringbuf_append_char(sb, c);
            tracker.advance();
            stringbuf_append_char(sb, tracker.current());
            tracker.advance();
            continue;
        }

        // Don't break on comma or brace inside brackets or parentheses
        if ((c == ',' || c == '{') && (bracket_depth > 0 || paren_depth > 0)) {
            stringbuf_append_char(sb, c);
            tracker.advance();
            continue;
        }

        if (c == ',' || c == '{') {
            break;
        }

        stringbuf_append_char(sb, c);
        tracker.advance();
    }

    if (sb->length > 0) {
        char* trimmed = input_trim_whitespace(sb->str->chars);
        if (trimmed) {
            String* trimmed_str = ctx.builder().createString(trimmed);
            free(trimmed);
            return trimmed_str ? (Item){.item = s2it(trimmed_str)} : (Item){.item = ITEM_ERROR};
        }
    }

    return {.item = ITEM_ERROR};
}

static Array* parse_css_declarations(InputContext& ctx) {
    SourceTracker& tracker = TRACKER;
    Array* declarations = array_pooled(ctx.input()->pool);
    if (!declarations) return NULL;

    while (!tracker.atEnd() && tracker.current() != '}') {
        skip_css_comments(tracker);
        if (tracker.current() == '}') break;

        Item declaration = parse_css_declaration(ctx);
        if (declaration .item != ITEM_ERROR) {
            array_append(declarations, declaration, ctx.input()->pool);
        }

        skip_css_comments(tracker);
        if (tracker.current() == ';') {
            tracker.advance(); // Skip semicolon
            skip_css_comments(tracker);
        }
    }

    return declarations;
}

static Item parse_css_declaration(InputContext& ctx) {
    SourceTracker& tracker = TRACKER;
    printf("Parsing CSS declaration\n");
    skip_css_comments(tracker);

    // Parse property name
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);  // Reset the buffer before parsing property name
    while (!tracker.atEnd() && tracker.current() != ':' && tracker.current() != ';' && tracker.current() != '}') {
        stringbuf_append_char(sb, tracker.current());
        tracker.advance();
    }
    String* property_str = ctx.builder().createString(sb->str->chars, sb->length);
    if (!property_str) return {.item = ITEM_ERROR};
    printf("Parsing CSS property: %s\n", property_str->chars);

    char* property_trimmed = input_trim_whitespace(property_str->chars);
    if (!property_trimmed || strlen(property_trimmed) == 0) {
        if (property_trimmed) free(property_trimmed);
        return {.item = ITEM_ERROR};
    }

    ElementBuilder declaration = ctx.builder().element("declaration");
    declaration.attr("property", property_trimmed);
    free(property_trimmed);

    skip_css_comments(tracker);

    if (tracker.current() == ':') {
        tracker.advance(); // Skip colon
        skip_css_comments(tracker);

        // Parse value
        Array* values = parse_css_value_list(ctx);
        if (values) {
            declaration.attr("values", {.item = (uint64_t)values});
        }

        // Check for !important
        skip_css_comments(tracker);
        if (tracker.current() == '!' && tracker.match("!important")) {
            tracker.advance(10);
            declaration.attr("important", "true");
        }
    }

    return declaration.final();
}

static Item parse_css_string(InputContext& ctx) {
    SourceTracker& tracker = TRACKER;
    printf("Parsing CSS string\n");
    char quote = tracker.current();
    if (quote != '"' && quote != '\'') return {.item = ITEM_ERROR};

    // Normalize: always use double quotes internally regardless of input quote style
    // This ensures 'Segoe UI' and "Segoe UI" both normalize to "Segoe UI"

    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);  // Reset the buffer before parsing string

    tracker.advance(); // Skip opening quote

    while (!tracker.atEnd() && tracker.current() != quote) {
        if (tracker.current() == '\\') {
            tracker.advance();
            switch (tracker.current()) {
                case '"': stringbuf_append_char(sb, '"'); break;
                case '\'': stringbuf_append_char(sb, '\''); break;
                case '\\': stringbuf_append_char(sb, '\\'); break;
                case '/': stringbuf_append_char(sb, '/'); break;
                case 'n': stringbuf_append_char(sb, '\n'); break;
                case 'r': stringbuf_append_char(sb, '\r'); break;
                case 't': stringbuf_append_char(sb, '\t'); break;
                case 'f': stringbuf_append_char(sb, '\f'); break;
                default:
                    // Handle hex escapes like \A0
                    if (is_css_hex_digit(tracker.current())) {
                        char hex[7] = {0};
                        int hex_len = 0;
                        while (hex_len < 6 && is_css_hex_digit(tracker.current())) {
                            hex[hex_len++] = tracker.current();
                            tracker.advance();
                        }
                        // Note: we advanced one too many, will correct at end of main loop
                        int codepoint = (int)strtol(hex, NULL, 16);
                        // Simple UTF-8 encoding for basic cases
                        if (codepoint < 0x80) {
                            stringbuf_append_char(sb, (char)codepoint);
                        } else if (codepoint < 0x800) {
                            stringbuf_append_char(sb, (char)(0xC0 | (codepoint >> 6)));
                            stringbuf_append_char(sb, (char)(0x80 | (codepoint & 0x3F)));
                        } else {
                            stringbuf_append_char(sb, (char)(0xE0 | (codepoint >> 12)));
                            stringbuf_append_char(sb, (char)(0x80 | ((codepoint >> 6) & 0x3F)));
                            stringbuf_append_char(sb, (char)(0x80 | (codepoint & 0x3F)));
                        }
                        continue; // Skip the advance at end of loop
                    } else {
                        stringbuf_append_char(sb, tracker.current());
                    }
                    break;
            }
        } else {
            stringbuf_append_char(sb, tracker.current());
        }
        tracker.advance();
    }

    if (tracker.current() == quote) {
        tracker.advance(); // Skip closing quote
    }

    String* str = ctx.builder().createString(sb->str->chars, sb->length);
    return str ? (Item){.item = s2it(str)} : (Item){.item = ITEM_ERROR};
}

static Item parse_css_url(InputContext& ctx) {
    SourceTracker& tracker = TRACKER;
    if (!tracker.match("url(")) return {.item = ITEM_ERROR};

    tracker.advance(4); // Skip "url("
    skip_css_whitespace(tracker);

    Item url_value = {.item = ITEM_ERROR};

    // Parse URL - can be quoted or unquoted
    if (tracker.current() == '"' || tracker.current() == '\'') {
        url_value = parse_css_string(ctx);
    } else {
        // Unquoted URL
        StringBuf* sb = ctx.sb;
        stringbuf_reset(sb);
        while (!tracker.atEnd() && tracker.current() != ')' && !(tracker.current() == ' ' || tracker.current() == '\t' || tracker.current() == '\n' || tracker.current() == '\r')) {
            if (tracker.current() == '\\') {
                tracker.advance();
                if (!tracker.atEnd()) {
                    stringbuf_append_char(sb, tracker.current());
                }
            } else {
                stringbuf_append_char(sb, tracker.current());
            }
            tracker.advance();
        }
        String* str = ctx.builder().createString(sb->str->chars, sb->length);
        url_value = str ? (Item){.item = s2it(str)} : (Item){.item = ITEM_ERROR};
    }

    skip_css_whitespace(tracker);
    if (tracker.current() == ')') {
        tracker.advance(); // Skip closing parenthesis
    }

    // Create url element with the URL as content
    ElementBuilder url_element = ctx.builder().element("url");
    if (url_value .item != ITEM_ERROR) {
        // Add URL as child content
        url_element.child(url_value);
    }

    return url_element.final();
}

static Item parse_css_color(InputContext& ctx) {
    SourceTracker& tracker = TRACKER;
    log_debug("parse_css_color called, current='%c' (0x%02x), next 10='%.10s'",
              tracker.current(), (unsigned char)tracker.current(), tracker.rest());

    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);    if (tracker.current() == '#') {
        // Hex color
        stringbuf_append_char(sb, tracker.current());
        tracker.advance();

        int hex_count = 0;
        while (!tracker.atEnd() && is_css_hex_digit(tracker.current()) && hex_count < 8) {
            stringbuf_append_char(sb, tracker.current());
            tracker.advance();
            hex_count++;
        }

        // Valid hex colors are 3, 4, 6, or 8 digits
        if (hex_count == 3 || hex_count == 4 || hex_count == 6 || hex_count == 8) {
            String* color_str = ctx.builder().createString(sb->str->chars, sb->length);
            return color_str ? (Item){.item = s2it(color_str)} : (Item){.item = ITEM_ERROR};
        }
        return {.item = ITEM_ERROR};
    }

    // Check for CSS3 color functions (rgba, hsla, etc.)
    if (is_css_identifier_start(tracker.current())) {
        size_t start = tracker.offset();

        // Check for color function names
        if (tracker.match("rgba(") ||
            tracker.match("hsla(") ||
            tracker.match("rgb(") ||
            tracker.match("hsl(")) {
            // Parse as function
            return parse_css_function(ctx);
        }

        // Check for named colors
        while (is_css_identifier_char(tracker.current())) {
            stringbuf_append_char(sb, tracker.current());
            tracker.advance();
        }

        if (sb->length > 0) {
            String* color_name = ctx.builder().createString(sb->str->chars, sb->length);
            if (color_name) {
                // Common CSS color names - return as symbol
                const char* name = color_name->chars;
                if (strcmp(name, "red") == 0 || strcmp(name, "blue") == 0 ||
                    strcmp(name, "green") == 0 || strcmp(name, "white") == 0 ||
                    strcmp(name, "black") == 0 || strcmp(name, "yellow") == 0 ||
                    strcmp(name, "transparent") == 0 || strcmp(name, "currentColor") == 0) {
                    return (Item){.item = y2it(color_name)};
                }
            }
        }

        // Reset if not a color - we can't restore position with SourceTracker
        // So we just fail and let caller handle it
    }

    return {.item = ITEM_ERROR};
}

static Item parse_css_number(InputContext& ctx) {
    SourceTracker& tracker = TRACKER;
    double *dval;
    dval = (double*)pool_calloc(ctx.input()->pool, sizeof(double));
    if (dval == NULL) return {.item = ITEM_ERROR};

    char* end;
    *dval = strtod(tracker.rest(), &end);
    // Advance tracker by the number of characters parsed
    tracker.advance(end - tracker.rest());

    return {.item = d2it(dval)};
}

static Item parse_css_measure(InputContext& ctx) {
    SourceTracker& tracker = TRACKER;
    log_debug("parse_css_measure: START, current='%c' (0x%02x)", tracker.current(), (unsigned char)tracker.current());
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);  // Reset the buffer before parsing measure
    size_t start = tracker.offset();

    // Parse number part
    if (tracker.current() == '+' || tracker.current() == '-') {
        stringbuf_append_char(sb, tracker.current());
        tracker.advance();
    }

    bool has_digits = false;
    while (is_css_digit(tracker.current())) {
        stringbuf_append_char(sb, tracker.current());
        tracker.advance();
        has_digits = true;
    }

    if (tracker.current() == '.') {
        stringbuf_append_char(sb, tracker.current());
        tracker.advance();
        while (is_css_digit(tracker.current())) {
            stringbuf_append_char(sb, tracker.current());
            tracker.advance();
            has_digits = true;
        }
    }

    if (!has_digits) {
        // Can't restore position - just fail
        log_debug("parse_css_measure: No digits found, returning ERROR");
        return {.item = ITEM_ERROR};
    }

    // Parse unit part
    size_t unit_start = tracker.offset();
    if (tracker.current() == '%') {
        stringbuf_append_char(sb, tracker.current());
        tracker.advance();
    } else if (is_css_identifier_start(tracker.current())) {
        // Handle CSS3 units
        while (is_css_identifier_char(tracker.current())) {
            stringbuf_append_char(sb, tracker.current());
            tracker.advance();
        }
    }

    // If we have a unit, return the complete dimension token as a single string
    if (tracker.offset() > unit_start) {
        // Create the complete dimension string (e.g., "10px")
        String* dimension_str = ctx.builder().createString(sb->str->chars, sb->length);
        log_debug("parse_css_measure: Parsed '%s' (with unit)", dimension_str ? dimension_str->chars : "NULL");
        return (Item){.item = s2it(dimension_str)};
    } else {
        // No unit - this should be handled as a number
        // But we can't backtrack, so return what we have as a dimension anyway
        String* dimension_str = ctx.builder().createString(sb->str->chars, sb->length);
        log_debug("parse_css_measure: Parsed '%s' (no unit)", dimension_str ? dimension_str->chars : "NULL");
        return (Item){.item = s2it(dimension_str)};
    }
}

static Item parse_css_identifier(InputContext& ctx) {
    SourceTracker& tracker = TRACKER;
    log_debug("parse_css_identifier: START, current='%c' (0x%02x)", tracker.current(), (unsigned char)tracker.current());
    if (!is_css_identifier_start(tracker.current())) return {.item = ITEM_ERROR};

    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);  // Reset the buffer before parsing identifier

    // Handle CSS pseudo-elements (::) and pseudo-classes (:)
    if (tracker.current() == ':') {
        stringbuf_append_char(sb, tracker.current());
        tracker.advance();

        // Check for double colon (pseudo-elements)
        if (tracker.current() == ':') {
            stringbuf_append_char(sb, tracker.current());
            tracker.advance();
        }
    }

    while (is_css_identifier_char(tracker.current()) || tracker.current() == '-') {
        stringbuf_append_char(sb, tracker.current());
        tracker.advance();
    }

    // Handle CSS3 pseudo-class functions like :nth-child(2n+1)
    if (tracker.current() == '(') {
        stringbuf_append_char(sb, tracker.current());
        tracker.advance();

        int paren_depth = 1;
        while (!tracker.atEnd() && paren_depth > 0) {
            if (tracker.current() == '(') paren_depth++;
            else if (tracker.current() == ')') paren_depth--;

            stringbuf_append_char(sb, tracker.current());
            tracker.advance();
        }
    }

    String* id_str = ctx.builder().createString(sb->str->chars, sb->length);
    if (!id_str) {
        log_debug("parse_css_identifier: Failed to create string, returning ERROR");
        return {.item = ITEM_ERROR};
    }

    log_debug("parse_css_identifier: Parsed '%s'", id_str->chars);
    // Convert CSS keyword values to Lambda symbols using y2it()
    // CSS identifiers like 'flex', 'red', etc. should be symbols, not strings
    return (Item){.item = y2it(id_str)};
}

static Array* parse_css_function_params(InputContext& ctx) {
    SourceTracker& tracker = TRACKER;
    Array* params = array_pooled(ctx.input()->pool);
    if (!params) return NULL;

    skip_css_comments(tracker);

    if (tracker.current() == ')') {
        return params; // empty parameter list
    }

    while (!tracker.atEnd() && tracker.current() != ')') {
        skip_css_comments(tracker);
        if (tracker.current() == ')') break;

        const char* start_pos = tracker.rest(); // track position to detect infinite loops

        Item param = parse_css_value(ctx);
        if (param .item != ITEM_ERROR) {
            array_append(params, param, ctx.input()->pool);
        } else {
            // if we can't parse a value and haven't advanced, skip one character to avoid infinite loop
            if (tracker.rest() == start_pos) {
                tracker.advance();
            }
        }

        skip_css_comments(tracker);

        // handle parameter separators
        if (tracker.current() == ',') {
            tracker.advance(); // skip comma
            skip_css_comments(tracker);
        } else if (tracker.current() == ')') {
            // end of parameters
            break;
        } else if (tracker.current() != ')') {
            // Check for calc operators (+, -, *, /) before treating as space-separated or slash-separated
            skip_css_whitespace(tracker);

            // Check if we have a calc operator
            if (tracker.current() == '+' || tracker.current() == '-' || tracker.current() == '*' || tracker.current() == '/') {
                // Special case: don't treat -- as two operators (it's a CSS custom property prefix)
                if (tracker.current() == '-' && tracker.peek(1) == '-') {
                    // This is --, not an operator - continue as space-separated
                    continue;
                }

                // Check if this looks like a negative number (- followed by digit or .)
                if (tracker.current() == '-' && (tracker.peek(1) == '.' || is_css_digit(tracker.peek(1)))) {
                    // This is a negative number, not an operator - it will be parsed in next iteration
                    continue;
                }

                // Special handling for slash:
                // - In calc() functions, / is a division operator
                // - In rgba/hsla functions, / is a separator for alpha channel
                // We can't always tell from context, but we treat it as operator
                // The formatter will output it correctly with spaces

                // Capture the operator as a separate parameter (symbol)
                char op_char = tracker.current();
                tracker.advance(); // advance past operator

                // Create a string for the operator using stringbuf
                StringBuf* op_sb = stringbuf_new(ctx.input()->pool);
                stringbuf_append_char(op_sb, op_char);
                String* op_str = stringbuf_to_string(op_sb);
                if (op_str) {
                    Item op_item = {.item = y2it(op_str)};  // Store as symbol
                    array_append(params, op_item, ctx.input()->pool);
                }

                skip_css_whitespace(tracker);
                continue; // Continue parsing next parameter
            }

            if (!tracker.atEnd() && tracker.current() != ',' && tracker.current() != ')') {
                // Only continue parsing for rgba() style space separation, not calc() expressions
                // For now, treat any remaining content as end of this parameter
                continue;
            }
        }
    }

    return params;
}

// Helper function to flatten single-element arrays
static Item flatten_single_array(Array* arr) {
    if (!arr) {
        return {.item = ITEM_ERROR};
    }

    if (arr->length != 1) {
        // Return array as-is if not single element
        return {.item = (uint64_t)arr};
    }

    // For single-element arrays, return the single item directly
    // Use array_get to safely access the item
    Item single_item = arr->items[0];

    // Debug: check what type we're flattening
    if (single_item .item != ITEM_ERROR) {
        // For container types (like Elements), the type is determined by the container's type_id field
        // Check if this is a direct pointer to a container
        if ((single_item.item >> 56) == 0) {
            Container* container = (Container*)single_item.item;
            if (container && container->type_id == LMD_TYPE_ELEMENT) {
                return single_item;
            }
        }
    }

    return single_item;
}

static Item parse_css_function(InputContext& ctx) {
    SourceTracker& tracker = TRACKER;
    // Parse function name
    if (!is_css_identifier_start(tracker.current())) return {.item = ITEM_ERROR};

    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);
    while (is_css_identifier_char(tracker.current())) {
        stringbuf_append_char(sb, tracker.current());
        tracker.advance();
    }

    skip_css_comments(tracker);
    if (tracker.current() != '(') {
        // Not a function, treat as identifier (symbol)
        String* id_str = ctx.builder().createString(sb->str->chars, sb->length);
        return id_str ? (Item){.item = y2it(id_str)} : (Item){.item = ITEM_ERROR};
    }

    String* func_name = ctx.builder().createString(sb->str->chars, sb->length);
    if (!func_name) return {.item = ITEM_ERROR};

    printf("Parsing CSS function: %s\n", func_name->chars);

    tracker.advance(); // Skip opening parenthesis

    // Parse function parameters
    Array* params = parse_css_function_params(ctx);

    if (tracker.current() == ')') {
        tracker.advance(); // Skip closing parenthesis
    }

    // Create Lambda element with function name as element name
    ElementBuilder func_element = ctx.builder().element(func_name->chars);

    // Add parameters as child content
    // Disable string merging to keep function parameters separate
    if (params && params->length > 0) {
        bool prev_disable_string_merging = input_context->disable_string_merging;
        input_context->disable_string_merging = true;

        for (long i = 0; i < params->length; i++) {
            Item param = params->items[i];
            func_element.child(param);
        }

        input_context->disable_string_merging = prev_disable_string_merging;
    }

    printf("Created function element '%s' with %ld parameters\n", func_name->chars, params ? params->length : 0);

    // Return element with proper type tagging for LMD_TYPE_ELEMENT
    return func_element.final();
}

static Array* parse_css_value_list(InputContext& ctx) {
    SourceTracker& tracker = TRACKER;
    Array* values = array_pooled(ctx.input()->pool);
    if (!values) return NULL;

    log_debug("parse_css_value_list: START, initial char='%c' (0x%02x), text='%.30s'",
              tracker.current(), (unsigned char)tracker.current(), tracker.rest());

    while (!tracker.atEnd() && tracker.current() != ';' && tracker.current() != '}' && tracker.current() != '!' && tracker.current() != ')') {
        skip_css_comments(tracker);
        if (tracker.atEnd() || tracker.current() == ';' || tracker.current() == '}' || tracker.current() == '!' || tracker.current() == ')') {
            log_debug("parse_css_value_list: Breaking at terminator check, atEnd=%d, current='%c' (0x%02x)",
                      tracker.atEnd(), tracker.current(), (unsigned char)tracker.current());
            break;
        }

        const char* start_pos = tracker.rest(); // Track position to detect infinite loops

        log_debug("parse_css_value_list: Loop iteration %d, current='%c' (0x%02x), text='%.30s'",
                  values->length, tracker.current(), (unsigned char)tracker.current(), tracker.rest());

        Item value = parse_css_value(ctx);
        log_debug("parse_css_value_list: Parsed value, type_id=%d, error=%d",
                  get_type_id(value), value.item == ITEM_ERROR);
        if (value .item != ITEM_ERROR) {
            array_append(values, value, ctx.input()->pool);
            log_debug("parse_css_value_list: Appended value, array length now=%d", values->length);
        } else {
            log_debug("parse_css_value_list: Value parse returned ERROR");
            // If we can't parse a value and haven't advanced, skip one character to avoid infinite loop
            if (tracker.rest() == start_pos) {
                log_debug("parse_css_value_list: No progress made, advancing by 1");
                tracker.advance();
                continue;
            }
        }

        log_debug("parse_css_value_list: After parse, current='%c' (0x%02x), checking separators",
                  tracker.current(), (unsigned char)tracker.current());

        // Check for value separators BEFORE calling skip_css_comments
        // because skip_css_comments would consume the space we want to detect
        if (tracker.current() == ',') {
            log_debug("parse_css_value_list: Found comma separator");
            // Preserve comma separator by adding a marker symbol
            String* comma_marker = ctx.builder().createString(",");
            if (comma_marker) {
                Item comma_item = {.item = y2it(comma_marker)};
                array_append(values, comma_item, ctx.input()->pool);
            }
            tracker.advance();
            skip_css_comments(tracker);
        } else if (tracker.current() == '/') {
            log_debug("parse_css_value_list: Found slash, peek(1)='%c'", tracker.peek(1));
            // Check if this is a slash separator (not division operator)
            // In CSS, slash is used for: border-radius (horizontal/vertical), rgba alpha separator
            // We need to look ahead to distinguish from comment
            if (tracker.peek(1) != '*' && tracker.peek(1) != '/') {
                log_debug("parse_css_value_list: Slash is separator, not comment");
                // Preserve slash separator by adding a marker symbol
                String* slash_marker = ctx.builder().createString("/");
                if (slash_marker) {
                    Item slash_item = {.item = y2it(slash_marker)};
                    array_append(values, slash_item, ctx.input()->pool);
                }
            }
            tracker.advance();
            skip_css_comments(tracker);
        } else if (tracker.current() == ' ' || tracker.current() == '\t' || tracker.current() == '\n' || tracker.current() == '\r') {
            log_debug("parse_css_value_list: Found whitespace, skipping...");
            skip_css_whitespace(tracker);
            log_debug("parse_css_value_list: After skip whitespace, current='%c' (0x%02x)",
                      tracker.current(), (unsigned char)tracker.current());
            // Skip any comments after whitespace
            while (tracker.current() == '/' && tracker.peek(1) == '*') {
                tracker.advance(2); // Skip /*
                while (!tracker.atEnd() && !(tracker.current() == '*' && tracker.peek(1) == '/')) {
                    tracker.advance();
                }
                if (tracker.current() == '*' && tracker.peek(1) == '/') {
                    tracker.advance(2); // Skip */
                }
                skip_css_whitespace(tracker);
            }
            // After skipping whitespace, check if we now have a separator
            if (tracker.current() == ',') {
                log_debug("parse_css_value_list: Found comma after whitespace");
                // Preserve comma separator
                String* comma_marker = ctx.builder().createString(",");
                if (comma_marker) {
                    Item comma_item = {.item = y2it(comma_marker)};
                    array_append(values, comma_item, ctx.input()->pool);
                }
                tracker.advance();
                skip_css_comments(tracker);
            } else if (tracker.current() == '/' && tracker.peek(1) != '*' && tracker.peek(1) != '/') {
                log_debug("parse_css_value_list: Found slash after whitespace");
                // Preserve slash separator
                String* slash_marker = ctx.builder().createString("/");
                if (slash_marker) {
                    Item slash_item = {.item = y2it(slash_marker)};
                    array_append(values, slash_item, ctx.input()->pool);
                }
                tracker.advance();
                skip_css_comments(tracker);
            }
            // Space-separated values continue - loop back to parse next value
            log_debug("parse_css_value_list: Space separator handled, continuing loop. Current='%c'",
                      tracker.current());
            continue;
        } else {
            // No explicit separator found after the value
            // Check if we're at a terminator - if so, break
            // Otherwise, continue to parse next space-separated value
            if (tracker.current() == ';' || tracker.current() == '}' ||
                tracker.current() == '!' || tracker.current() == ')' || tracker.atEnd()) {
                log_debug("parse_css_value_list: At terminator, breaking. Current='%c' (0x%02x)",
                          tracker.current(), (unsigned char)tracker.current());
                break;
            }
            // Otherwise, this is a space-separated value list, continue parsing
            log_debug("parse_css_value_list: No separator but not at terminator, continuing. Current='%c' (0x%02x)",
                      tracker.current(), (unsigned char)tracker.current());
            continue;
        }
    }

    log_debug("parse_css_value_list: FINISHED, array length=%d", values ? values->length : -1);
    return values;
}

static Item parse_css_value(InputContext& ctx) {
    SourceTracker& tracker = TRACKER;
    skip_css_comments(tracker);

    if (tracker.atEnd()) return {.item = ITEM_ERROR};

    log_debug("parse_css_value: current='%c' (0x%02x), text='%.20s'",
              tracker.current(), (unsigned char)tracker.current(), tracker.rest());

    // Try to parse different CSS value types
    switch (tracker.current()) {
        case '"':
        case '\'':
            log_debug("parse_css_value: Parsing string");
            return parse_css_string(ctx);

        case '#':
            log_debug("parse_css_value: Parsing color (hash)");
            return parse_css_color(ctx);

        case '+':
        case '-':
            // Check if this is a CSS custom property (starts with --)
            if (tracker.current() == '-' && tracker.peek(1) == '-') {
                log_debug("parse_css_value: Parsing custom property");
                return parse_css_identifier(ctx);
            }
            // Otherwise fall through to parse as number
            log_debug("parse_css_value: Parsing measure (signed)");
            return parse_css_measure(ctx);

        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        case '.':
            log_debug("parse_css_value: Parsing measure (digit)");
            return parse_css_measure(ctx);

        default:
            if (tracker.current() == 'u' && tracker.match("url(")) {
                log_debug("parse_css_value: Matched 'url(', parsing url. Current position: '%.20s'", tracker.rest());
                return parse_css_url(ctx);
            } else if (is_css_identifier_start(tracker.current())) {
                log_debug("parse_css_value: Identifier start detected at '%.20s'", tracker.rest());
                log_debug("parse_css_value: Identifier start detected, checking for function");
                // Look ahead to see if this is a function
                const char* lookahead = tracker.rest();
                while (is_css_identifier_char(*lookahead)) {
                    lookahead++;
                }
                // Skip whitespace in lookahead
                while (*lookahead && (*lookahead == ' ' || *lookahead == '\t' || *lookahead == '\n' || *lookahead == '\r')) {
                    lookahead++;
                }
                if (*lookahead == '(') {
                    log_debug("parse_css_value: Found '(', parsing as function");
                    // Check for CSS3 functions
                    const char* start = tracker.rest();
                    if (strncmp(start, "calc(", 5) == 0 ||
                        strncmp(start, "var(", 4) == 0 ||
                        strncmp(start, "linear-gradient(", 16) == 0 ||
                        strncmp(start, "radial-gradient(", 16) == 0 ||
                        strncmp(start, "repeating-linear-gradient(", 26) == 0 ||
                        strncmp(start, "repeating-radial-gradient(", 26) == 0 ||
                        strncmp(start, "rgba(", 5) == 0 ||
                        strncmp(start, "hsla(", 5) == 0 ||
                        strncmp(start, "rgb(", 4) == 0 ||
                        strncmp(start, "hsl(", 4) == 0 ||
                        strncmp(start, "cubic-bezier(", 13) == 0 ||
                        strncmp(start, "steps(", 6) == 0 ||
                        strncmp(start, "rotate(", 7) == 0 ||
                        strncmp(start, "rotateX(", 8) == 0 ||
                        strncmp(start, "rotateY(", 8) == 0 ||
                        strncmp(start, "rotateZ(", 8) == 0 ||
                        strncmp(start, "rotate3d(", 9) == 0 ||
                        strncmp(start, "scale(", 6) == 0 ||
                        strncmp(start, "scaleX(", 7) == 0 ||
                        strncmp(start, "scaleY(", 7) == 0 ||
                        strncmp(start, "scaleZ(", 7) == 0 ||
                        strncmp(start, "scale3d(", 8) == 0 ||
                        strncmp(start, "translate(", 10) == 0 ||
                        strncmp(start, "translateX(", 11) == 0 ||
                        strncmp(start, "translateY(", 11) == 0 ||
                        strncmp(start, "translateZ(", 11) == 0 ||
                        strncmp(start, "translate3d(", 12) == 0 ||
                        strncmp(start, "skew(", 5) == 0 ||
                        strncmp(start, "skewX(", 6) == 0 ||
                        strncmp(start, "skewY(", 6) == 0 ||
                        strncmp(start, "matrix(", 7) == 0 ||
                        strncmp(start, "matrix3d(", 9) == 0 ||
                        strncmp(start, "perspective(", 12) == 0 ||
                        strncmp(start, "blur(", 5) == 0 ||
                        strncmp(start, "brightness(", 11) == 0 ||
                        strncmp(start, "contrast(", 9) == 0 ||
                        strncmp(start, "drop-shadow(", 12) == 0 ||
                        strncmp(start, "grayscale(", 10) == 0 ||
                        strncmp(start, "hue-rotate(", 11) == 0 ||
                        strncmp(start, "invert(", 7) == 0 ||
                        strncmp(start, "opacity(", 8) == 0 ||
                        strncmp(start, "saturate(", 9) == 0 ||
                        strncmp(start, "sepia(", 6) == 0 ||
                        strncmp(start, "minmax(", 7) == 0 ||
                        strncmp(start, "repeat(", 7) == 0 ||
                        strncmp(start, "fit-content(", 12) == 0) {
                        return parse_css_function(ctx);
                    } else {
                        return parse_css_function(ctx);
                    }
                } else {
                    log_debug("parse_css_value: No '(' found, parsing as identifier");
                    // Just parse as identifier - parse_css_identifier handles color keywords too
                    return parse_css_identifier(ctx);
                }
            }
            log_debug("parse_css_value: No match found, returning ERROR");
            return {.item = ITEM_ERROR};
    }
}

void parse_css(Input* input, const char* css_string) {
    printf("css_parse (stylesheet)\n");

    // create error tracking context with tracker
    InputContext ctx(input, css_string, strlen(css_string));
    SourceTracker& tracker = ctx.tracker();

    skip_css_comments(tracker);

    // Parse as complete CSS stylesheet
    if (!tracker.atEnd()) {
        input->root = parse_css_stylesheet(ctx);
    } else {
        // Empty stylesheet
        ElementBuilder empty_stylesheet = ctx.builder().element("stylesheet");
        Array* empty_rules = array_pooled(input->pool);
        if (empty_rules) {
            empty_stylesheet.attr("rules", {.item = (uint64_t)empty_rules});
            input->root = empty_stylesheet.final();
        } else {
            ctx.addError("Failed to allocate memory for empty CSS stylesheet");
            input->root = {.item = ITEM_ERROR};
        }
    }

    if (ctx.hasErrors()) {
        // errors occurred during parsing
    }
}
