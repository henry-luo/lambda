#include "input.hpp"
#include "../mark_builder.hpp"
#include "input-context.hpp"
#include "source_tracker.hpp"
#include "html_entities.h"

using namespace lambda;

static const int XML_MAX_DEPTH = 512;

static Item parse_element(InputContext& ctx, const char **xml, int depth = 0);
static Item parse_comment(InputContext& ctx, const char **xml);
static Item parse_cdata(InputContext& ctx, const char **xml);
static Item parse_entity(InputContext& ctx, const char **xml);
static Item parse_doctype(InputContext& ctx, const char **xml, int depth = 0);
static Item parse_dtd_declaration(InputContext& ctx, const char **xml);
static String* parse_string_content(InputContext& ctx, const char **xml, char end_char);

static String* parse_string_content(InputContext& ctx, const char **xml, char end_char) {
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    while (**xml && **xml != end_char) {
        if (**xml == '&') {
            (*xml)++; // Skip &

            if (**xml == '#') {
                // Numeric character references &#123; or &#x1F;
                (*xml)++; // Skip #
                uint32_t value = 0;
                bool is_hex = false;

                if (**xml == 'x' || **xml == 'X') {
                    is_hex = true;
                    (*xml)++; // Skip x
                }

                while (**xml && **xml != ';') {
                    if (is_hex) {
                        if (**xml >= '0' && **xml <= '9') {
                            value = value * 16 + (**xml - '0');
                        } else if (**xml >= 'a' && **xml <= 'f') {
                            value = value * 16 + (**xml - 'a' + 10);
                        } else if (**xml >= 'A' && **xml <= 'F') {
                            value = value * 16 + (**xml - 'A' + 10);
                        } else break;
                    } else {
                        if (**xml >= '0' && **xml <= '9') {
                            value = value * 10 + (**xml - '0');
                        } else break;
                    }
                    (*xml)++;
                }

                if (**xml == ';') {
                    (*xml)++; // Skip ;
                    // Convert Unicode codepoint to UTF-8
                    char utf8_buf[8];
                    int utf8_len = unicode_to_utf8(value, utf8_buf);
                    if (utf8_len > 0) {
                        stringbuf_append_str(sb, utf8_buf);
                    } else {
                        stringbuf_append_char(sb, '?'); // Invalid codepoint
                    }
                } else {
                    // Invalid numeric reference, append as-is
                    stringbuf_append_char(sb, '&');
                    stringbuf_append_char(sb, '#');
                }
            } else {
                // Named entity reference - use html_entities module
                const char* entity_start = *xml;
                while (**xml && **xml != ';' && **xml != ' ' && **xml != '\t' &&
                       **xml != '\n' && **xml != '<' && **xml != '&') {
                    (*xml)++;
                }

                if (**xml == ';') {
                    size_t entity_len = *xml - entity_start;
                    EntityResult result = html_entity_resolve(entity_start, entity_len);
                    (*xml)++; // Skip ;

                    if (result.type == ENTITY_ASCII_ESCAPE) {
                        // ASCII escapes: decode inline
                        stringbuf_append_str(sb, result.decoded);
                    } else if (result.type == ENTITY_UNICODE_SPACE) {
                        // Unicode space entities: decode inline as UTF-8
                        char utf8_buf[8];
                        int utf8_len = unicode_to_utf8(result.named.codepoint, utf8_buf);
                        if (utf8_len > 0) {
                            stringbuf_append_str(sb, utf8_buf);
                        }
                    } else if (result.type == ENTITY_NAMED) {
                        // Named entities: decode to UTF-8 for attribute values
                        char utf8_buf[8];
                        int utf8_len = unicode_to_utf8(result.named.codepoint, utf8_buf);
                        if (utf8_len > 0) {
                            stringbuf_append_str(sb, utf8_buf);
                        }
                    } else {
                        // Unknown entity - preserve as-is for roundtrip compatibility
                        stringbuf_append_char(sb, '&');
                        for (const char* p = entity_start; p < *xml - 1; p++) {
                            stringbuf_append_char(sb, *p);
                        }
                        stringbuf_append_char(sb, ';');
                    }
                } else {
                    // Invalid entity, just append the &
                    stringbuf_append_char(sb, '&');
                    *xml = entity_start;
                }
            }
        } else {
            stringbuf_append_char(sb, **xml);
            (*xml)++;
        }
    }

    return builder.createString(sb->str->chars, sb->length);
}

static String* parse_tag_name(InputContext& ctx, const char **xml) {
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    while (**xml && (isalnum(**xml) || **xml == '_' || **xml == '-' || **xml == ':')) {
        stringbuf_append_char(sb, **xml);
        (*xml)++;
    }

    if (sb->length == 0) return NULL; // empty tag name

    return builder.createString(sb->str->chars, sb->length);
}

static bool parse_attributes(InputContext& ctx, ElementBuilder& element, const char **xml) {
    skip_whitespace(xml);
    while (**xml && **xml != '>' && **xml != '/' && **xml != '?') {
        // parse attribute name
        String* attr_name = parse_tag_name(ctx, xml);
        if (!attr_name) return false;

        skip_whitespace(xml);
        if (**xml != '=') return false;
        (*xml)++; // skip =

        skip_whitespace(xml);
        if (**xml != '"' && **xml != '\'') return false;

        char quote_char = **xml;
        (*xml)++; // skip opening quote

        String* attr_value = parse_string_content(ctx, xml, quote_char);
        if (!attr_value) return false;

        if (**xml == quote_char) { (*xml)++; } // skip closing quote

        // Add attribute to element (wrap String* in Item)
        element.attr(attr_name->chars, Item{.item = s2it(attr_value)});

        skip_whitespace(xml);
    }
    return true;
}

static Item parse_comment(InputContext& ctx, const char **xml) {
    MarkBuilder& builder = ctx.builder;
    // Skip past the "!--" part (already consumed by caller)

    // Find comment content
    const char* comment_start = *xml;
    const char* comment_end = comment_start;

    while (*comment_end && strncmp(comment_end, "-->", 3) != 0) {
        comment_end++;
    }

    // Create comment element
    ElementBuilder element = builder.element("!--");

    // Add comment content as text
    if (comment_end > comment_start) {
        StringBuf* sb = ctx.sb;
        stringbuf_reset(sb);
        while (comment_start < comment_end) {
            stringbuf_append_char(sb, *comment_start);
            comment_start++;
        }
        String* comment_text = builder.createString(sb->str->chars, sb->length);
        if (comment_text && comment_text->len > 0) {
            element.child(Item{.item = s2it(comment_text)});
        }
    }

    // Skip closing -->
    if (*comment_end) {
        *xml = comment_end + 3;
    } else {
        *xml = comment_end;
    }
    return element.final();
}

static Item parse_cdata(InputContext& ctx, const char **xml) {
    MarkBuilder& builder = ctx.builder;
    // Skip past the "![CDATA[" part (already consumed by caller)

    const char* cdata_start = *xml;

    // Find CDATA end
    while (**xml && strncmp(*xml, "]]>", 3) != 0) {
        (*xml)++;
    }

    // Create CDATA content string
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);
    while (cdata_start < *xml) {
        stringbuf_append_char(sb, *cdata_start);
        cdata_start++;
    }

    if (**xml && strncmp(*xml, "]]>", 3) == 0) {
        *xml += 3; // skip ]]>
    }

    String* cdata_text = builder.createString(sb->str->chars, sb->length);
    return Item{.item = s2it(cdata_text)};
}

static Item parse_entity(InputContext& ctx, const char **xml) {
    MarkBuilder& builder = ctx.builder;
    Input* input = ctx.input();
    // Skip past the "!ENTITY" part (already consumed by caller)
    skip_whitespace(xml);

    // Parse entity name
    const char* entity_name_start = *xml;
    while (**xml && **xml != ' ' && **xml != '\t' && **xml != '\n' && **xml != '\r') {
        (*xml)++;
    }
    const char* entity_name_end = *xml;

    skip_whitespace(xml);

    // Parse entity value (quoted string or external reference)
    const char* entity_value_start = NULL;
    const char* entity_value_end = NULL;
    char quote_char = 0;
    bool is_external = false;

    if (**xml == '"' || **xml == '\'') {
        quote_char = **xml;
        (*xml)++; // skip opening quote
        entity_value_start = *xml;

        while (**xml && **xml != quote_char) {
            (*xml)++;
        }
        entity_value_end = *xml;

        if (**xml == quote_char) {
            (*xml)++; // skip closing quote
        }
    } else if (strncmp(*xml, "SYSTEM", 6) == 0 || strncmp(*xml, "PUBLIC", 6) == 0) {
        // External entity reference
        is_external = true;
        entity_value_start = *xml;
        while (**xml && **xml != '>') {
            (*xml)++;
        }
        entity_value_end = *xml;
    }

    // Skip to end of declaration
    while (**xml && **xml != '>') {
        (*xml)++;
    }
    if (**xml == '>') {
        (*xml)++; // skip >
    }

    // Create entity element
    ElementBuilder element = builder.element("!ENTITY");

    // Add entity name as "name" attribute
    if (entity_name_end > entity_name_start) {
        StringBuf* sb = ctx.sb;
        stringbuf_reset(sb);
        const char* temp = entity_name_start;
        while (temp < entity_name_end) {
            stringbuf_append_char(sb, *temp);
            temp++;
        }
        String* name_str = builder.createString(sb->str->chars, sb->length);
        element.attr("name", Item{.item = s2it(name_str)});
    }

    // Add entity value/reference as "value" attribute
    if (entity_value_end > entity_value_start) {
        StringBuf* sb = ctx.sb;
        stringbuf_reset(sb);
        const char* temp = entity_value_start;
        while (temp < entity_value_end) {
            stringbuf_append_char(sb, *temp);
            temp++;
        }
        String* value_str = builder.createString(sb->str->chars, sb->length);
        element.attr("value", Item{.item = s2it(value_str)});
    }

    // Add type attribute (internal/external)
    element.attr("type", is_external ? "external" : "internal");

    return element.final();
}

static Item parse_dtd_declaration(InputContext& ctx, const char **xml) {
    MarkBuilder& builder = ctx.builder;
    // Parse DTD declarations like ELEMENT, ATTLIST, NOTATION
    const char* decl_start = *xml;
    const char* decl_name_end = decl_start;

    // Find end of declaration name
    while (**xml && **xml != ' ' && **xml != '\t' && **xml != '\n' && **xml != '\r') {
        (*xml)++;
        decl_name_end = *xml;
    }

    // Extract declaration name
    size_t decl_name_len = decl_name_end - decl_start;
    if (decl_name_len == 0) return {.item = ITEM_ERROR};

    // Create declaration element name with "!" prefix
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);
    stringbuf_append_char(sb, '!');
    const char* temp = decl_start;
    while (temp < decl_name_end) {
        stringbuf_append_char(sb, *temp);
        temp++;
    }
    String* decl_element_name = builder.createString(sb->str->chars, sb->length);

    skip_whitespace(xml);

    // Parse declaration content until >
    const char* content_start = *xml;
    int paren_count = 0;
    while (**xml && (**xml != '>' || paren_count > 0)) {
        if (**xml == '(') paren_count++;
        else if (**xml == ')') paren_count--;
        (*xml)++;
    }
    const char* content_end = *xml;

    if (**xml == '>') {
        (*xml)++; // skip >
    }

    // Create DTD declaration element
    ElementBuilder element = builder.element(decl_element_name->chars);

    // Add declaration content as text
    if (content_end > content_start) {
        stringbuf_reset(sb);
        temp = content_start;
        while (temp < content_end) {
            stringbuf_append_char(sb, *temp);
            temp++;
        }
        String* content_text = builder.createString(sb->str->chars, sb->length);
        if (content_text && content_text->len > 0) {
            element.child(Item{.item = s2it(content_text)});
        }
    }
    return element.final();
}

static Item parse_doctype(InputContext& ctx, const char **xml, int depth) {
    MarkBuilder& builder = ctx.builder;
    // Skip past the "!DOCTYPE" part (already consumed by caller)
    skip_whitespace(xml);

    // Skip DOCTYPE name and external ID
    while (**xml && **xml != '[' && **xml != '>') {
        (*xml)++;
    }

    // If there's an internal subset [...]
    if (**xml == '[') {
        (*xml)++; // skip [

        // Create a document fragment to hold DTD declarations
        ElementBuilder dt_elmt = builder.element("!DOCTYPE");

        // Parse internal subset content
        while (**xml && **xml != ']') {
            skip_whitespace(xml);
            if (**xml == '<') {
                (*xml)++; // skip <
                if (**xml == '!') {
                    (*xml)++; // skip !
                    // Check for specific DTD declarations
                    if (strncmp(*xml, "ENTITY", 6) == 0) {
                        *xml += 6;
                        Item entity = parse_entity(ctx, xml);
                        if (entity.item != ITEM_ERROR) {
                            dt_elmt.child(entity);
                        }
                    } else if (strncmp(*xml, "ELEMENT", 7) == 0 ||
                               strncmp(*xml, "ATTLIST", 7) == 0 ||
                               strncmp(*xml, "NOTATION", 8) == 0) {
                        Item decl = parse_dtd_declaration(ctx, xml);
                        if (decl.item != ITEM_ERROR) {
                            dt_elmt.child(decl);
                        }
                    } else {
                        // Generic DTD declaration
                        Item decl = parse_dtd_declaration(ctx, xml);
                        if (decl.item != ITEM_ERROR) {
                            dt_elmt.child(decl);
                        }
                    }
                } else {
                    // Other elements (shouldn't happen in DTD, but handle gracefully)
                    (*xml)--; // back up to <
                    Item element = parse_element(ctx, xml, depth + 1);
                    if (element.item != ITEM_ERROR) {
                        dt_elmt.child(element);
                    }
                }
            } else {
                (*xml)++; // skip any other characters
            }
        }

        if (**xml == ']') {
            (*xml)++; // skip ]
        }

        // Skip to end of DOCTYPE
        while (**xml && **xml != '>') {
            (*xml)++;
        }
        if (**xml == '>') {
            (*xml)++; // skip >
        }

        return dt_elmt.final();
    } else {
        // No internal subset, just skip to end
        while (**xml && **xml != '>') {
            (*xml)++;
        }
        if (**xml == '>') {
            (*xml)++; // skip >
        }
        return parse_element(ctx, xml, depth); // parse next element
    }
}

static Item parse_element(InputContext& ctx, const char **xml, int depth) {
    MarkBuilder& builder = ctx.builder;
    skip_whitespace(xml);

    if (depth >= XML_MAX_DEPTH) {
        ctx.addError(ctx.tracker.location(), "Maximum XML nesting depth (%d) exceeded", XML_MAX_DEPTH);
        return {.item = ITEM_ERROR};
    }

    if (**xml != '<') return {.item = ITEM_ERROR};
    (*xml)++; // skip <

    // Handle comments - create element with name "!--"
    if (strncmp(*xml, "!--", 3) == 0) {
        *xml += 3; // skip !--
        return parse_comment(ctx, xml);
    }

    // Handle CDATA sections
    if (strncmp(*xml, "![CDATA[", 8) == 0) {
        *xml += 8;
        return parse_cdata(ctx, xml);
    }

    // Handle ENTITY declarations - create element with name "!ENTITY"
    if (strncmp(*xml, "!ENTITY", 7) == 0) {
        *xml += 7; // skip !ENTITY
        return parse_entity(ctx, xml);
    }

    // Handle DOCTYPE declarations - parse internal subset for entities
    if (strncmp(*xml, "!DOCTYPE", 8) == 0) {
        *xml += 8; // skip !DOCTYPE
        return parse_doctype(ctx, xml);
    }

    // Handle other DTD declarations (ELEMENT, ATTLIST, NOTATION, etc.)
    if (**xml == '!' && (strncmp(*xml + 1, "ELEMENT", 7) == 0 ||
                        strncmp(*xml + 1, "ATTLIST", 7) == 0 ||
                        strncmp(*xml + 1, "NOTATION", 8) == 0)) {
        (*xml)++; // skip !
        return parse_dtd_declaration(ctx, xml);
    }

    // Handle processing instructions - create element with name "?target"
    bool is_processing = (**xml == '?');
    if (is_processing) {
        (*xml)++; // skip ?

        // Parse target name
        String* target_name = parse_tag_name(ctx, xml);
        if (!target_name) return {.item = ITEM_ERROR};

        // Create processing instruction element name "?target"
        StringBuf* sb = ctx.sb;
        stringbuf_reset(sb);
        stringbuf_append_char(sb, '?');
        stringbuf_append_str(sb, target_name->chars);
        String* pi_name = builder.createString(sb->str->chars, sb->length);

        // Parse PI data (everything until ?>)
        skip_whitespace(xml);
        const char* pi_data_start = *xml;
        while (**xml && !(**xml == '?' && *(*xml + 1) == '>')) {
            (*xml)++;
        }
        const char* pi_data_end = *xml;

        // Extract stylesheet href if this is xml-stylesheet processing instruction
        if (strcmp(target_name->chars, "xml-stylesheet") == 0) {
            // Parse the pseudo-attributes in the PI data
            const char* href_start = strstr(pi_data_start, "href=");
            if (href_start && href_start < pi_data_end) {
                href_start += 5; // skip "href="
                skip_whitespace(&href_start);

                // Extract quoted value
                char quote = *href_start;
                if (quote == '"' || quote == '\'') {
                    href_start++; // skip opening quote
                    const char* href_end = strchr(href_start, quote);
                    if (href_end && href_end < pi_data_end) {
                        size_t href_len = href_end - href_start;
                        // Allocate from pool and store in input
                        Input* input = ctx.input();
                        input->xml_stylesheet_href = (char*)pool_alloc(input->pool, href_len + 1);
                        if (input->xml_stylesheet_href) {
                            strncpy(input->xml_stylesheet_href, href_start, href_len);
                            input->xml_stylesheet_href[href_len] = '\0';
                            log_debug("[XML Parser] Found xml-stylesheet href: %s", input->xml_stylesheet_href);
                        }
                    }
                }
            }
        }

        // Skip ?>
        if (**xml == '?' && *(*xml + 1) == '>') {
            *xml += 2;
        }

        // Create processing instruction element
        ElementBuilder element = builder.element(pi_name->chars);

        // Add PI data as text content
        if (pi_data_end > pi_data_start) {
            stringbuf_reset(sb);
            while (pi_data_start < pi_data_end) {
                stringbuf_append_char(sb, *pi_data_start);
                pi_data_start++;
            }
            String* pi_data = builder.createString(sb->str->chars, sb->length);
            if (pi_data && pi_data->len > 0) {
                element.child(Item{.item = s2it(pi_data)});
            }
        }
        return element.final();
    }

    // parse tag name
    String* tag_name = parse_tag_name(ctx, xml);
    if (!tag_name) return {.item = ITEM_ERROR};

    // Create element
    ElementBuilder element = builder.element(tag_name->chars);

    // parse attributes
    if (!parse_attributes(ctx, element, xml)) return {.item = ITEM_ERROR};

    skip_whitespace(xml);

    // check for self-closing tag
    bool self_closing = false;
    if (**xml == '/') {
        self_closing = true;
        (*xml)++; // skip /
    }

    if (**xml != '>') return {.item = ITEM_ERROR};
    (*xml)++; // skip >

    if (!self_closing) {
        // Parse content and add to Element's List part
        skip_whitespace(xml);

        while (**xml && !(**xml == '<' && *(*xml + 1) == '/')) {
            if (**xml == '<') {
                // Child element (could be regular element, comment, or PI)
                Item child = parse_element(ctx, xml, depth + 1);
                if (child.item != ITEM_ERROR) {
                    element.child(child);
                }
            } else {
                // Text content - trim leading/trailing whitespace for better handling
                const char* text_start = *xml;
                while (**xml && **xml != '<') {
                    (*xml)++;
                }

                if (*xml > text_start) {
                    // Create text content
                    StringBuf* sb = ctx.sb;
                    stringbuf_reset(sb);

                    // Trim leading whitespace
                    while (text_start < *xml && (*text_start == ' ' || *text_start == '\n' ||
                           *text_start == '\r' || *text_start == '\t')) {
                        text_start++;
                    }

                    const char* text_end = *xml;
                    // Trim trailing whitespace
                    while (text_end > text_start && (*(text_end-1) == ' ' || *(text_end-1) == '\n' ||
                           *(text_end-1) == '\r' || *(text_end-1) == '\t')) {
                        text_end--;
                    }

                    // Only add non-empty text content
                    if (text_end > text_start) {
                        // Manual entity parsing for text content
                        while (text_start < text_end) {
                            if (*text_start == '&') {
                                text_start++;
                                const char* entity_start = text_start;

                                // Find entity end
                                while (text_start < text_end && *text_start != ';') {
                                    text_start++;
                                }

                                if (text_start < text_end && *text_start == ';') {
                                    // Try to resolve entity using html_entities module
                                    size_t entity_len = text_start - entity_start;
                                    EntityResult result = html_entity_resolve(entity_start, entity_len);
                                    text_start++; // Skip ;

                                    if (result.type == ENTITY_ASCII_ESCAPE) {
                                        // ASCII escapes: decode inline
                                        stringbuf_append_str(sb, result.decoded);
                                    } else if (result.type == ENTITY_UNICODE_SPACE) {
                                        // Unicode space entities: decode inline as UTF-8
                                        char utf8_buf[8];
                                        int utf8_len = unicode_to_utf8(result.named.codepoint, utf8_buf);
                                        if (utf8_len > 0) {
                                            stringbuf_append_str(sb, utf8_buf);
                                        }
                                    } else if (result.type == ENTITY_NAMED) {
                                        // Named entities: decode to UTF-8
                                        char utf8_buf[8];
                                        int utf8_len = unicode_to_utf8(result.named.codepoint, utf8_buf);
                                        if (utf8_len > 0) {
                                            stringbuf_append_str(sb, utf8_buf);
                                        }
                                    } else {
                                        // Unknown entity - preserve as-is for roundtrip compatibility
                                        stringbuf_append_char(sb, '&');
                                        for (const char* p = entity_start; p < text_start; p++) {
                                            stringbuf_append_char(sb, *p);
                                        }
                                    }
                                } else {
                                    // Invalid entity
                                    stringbuf_append_char(sb, '&');
                                    text_start = entity_start;
                                }
                            } else {
                                stringbuf_append_char(sb, *text_start);
                                text_start++;
                            }
                        }

                        String* processed_text = builder.createString(sb->str->chars, sb->length);
                        if (processed_text && processed_text->len > 0) {
                            element.child(Item{.item = s2it(processed_text)});
                        }
                    }
                }
            }
            skip_whitespace(xml);
        }

        // Skip closing tag
        if (**xml == '<' && *(*xml + 1) == '/') {
            *xml += 2; // Skip </
            while (**xml && **xml != '>') {
                (*xml)++; // Skip tag name
            }
            if (**xml == '>') (*xml)++; // Skip >
        }
    }
    return element.final();
}

void parse_xml(Input* input, const char* xml_string) {
    if (!xml_string || !*xml_string) {
        input->root = {.item = ITEM_NULL};
        return;
    }
    InputContext ctx(input, xml_string, strlen(xml_string));
    MarkBuilder& builder = ctx.builder;

    const char* xml = xml_string;
    skip_whitespace(&xml);

    // Create a document root element to contain all top-level elements
    ElementBuilder doc_element = builder.element("document");

    int actual_element_count = 0;  // Count only actual XML elements (not PIs, comments, etc.)
    Item actual_root_element = {.item = ITEM_ERROR};

    // Parse all top-level elements (including XML declaration, comments, PIs, and the main element)
    while (*xml) {
        skip_whitespace(&xml);
        if (!*xml) break;

        const char* old_xml = xml; // Save position to detect infinite loops

        if (*xml == '<') {
            Item element = parse_element(ctx, &xml, 0);
            if (element.item != ITEM_ERROR) {
                doc_element.child(element);

                // Check if this is an actual XML element (not processing instruction, comment, DTD, etc.)
                Element* elem = (Element*)element.item;
                if (elem && elem->type) {
                    TypeElmt* elmt_type = (TypeElmt*)elem->type;
                    // Count as actual element if it doesn't start with ?, !, or --
                    if (elmt_type->name.length > 0 &&
                        elmt_type->name.str[0] != '?' &&
                        elmt_type->name.str[0] != '!' &&
                        !(elmt_type->name.length >= 3 && strncmp(elmt_type->name.str, "!--", 3) == 0)) {
                        actual_element_count++;
                        actual_root_element = element;
                    }
                }
            }
        } else {
            // Skip any stray text content at document level
            while (*xml && *xml != '<') {
                xml++;
            }
        }

        // Safety check: ensure we always advance to prevent infinite loops
        if (xml == old_xml) {
            ctx.addWarning(ctx.tracker.location(), "Possible infinite loop detected in XML parsing, forcing advance");
            xml++; // Force advance by at least one character
        }
    }

    // Report errors if any
    if (ctx.hasErrors()) {
        ctx.logErrors();
    }

    // Always return the document wrapper to maintain consistent structure
    // This ensures all XML content is wrapped in a <document> element
    input->root = doc_element.final();
}
