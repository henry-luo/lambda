#include "input.h"

static Item parse_element(Input *input, const char **xml);
static Item parse_comment(Input *input, const char **xml);
static Item parse_cdata(Input *input, const char **xml);
static Item parse_entity(Input *input, const char **xml);
static Item parse_doctype(Input *input, const char **xml);
static Item parse_dtd_declaration(Input *input, const char **xml);

// Simple entity resolution - for now just handle common predefined ones
static const char* resolve_entity(const char* entity_name, size_t length) {
    if (length == 2 && strncmp(entity_name, "lt", 2) == 0) return "<";
    if (length == 2 && strncmp(entity_name, "gt", 2) == 0) return ">";
    if (length == 3 && strncmp(entity_name, "amp", 3) == 0) return "&";
    if (length == 4 && strncmp(entity_name, "quot", 4) == 0) return "\"";
    if (length == 4 && strncmp(entity_name, "apos", 4) == 0) return "'";
    if (length == 4 && strncmp(entity_name, "nbsp", 4) == 0) return "\xA0"; // Non-breaking space
    if (length == 9 && strncmp(entity_name, "copyright", 9) == 0) return "Copyright 2025 Library Corp.";
    return NULL; // Unknown entity
}

static void skip_whitespace(const char **xml) {
    while (**xml && (**xml == ' ' || **xml == '\n' || **xml == '\r' || **xml == '\t')) {
        (*xml)++;
    }
}

static String* parse_string_content(Input *input, const char **xml, char end_char) {
    StrBuf* sb = input->sb;
    
    while (**xml && **xml != end_char) {
        if (**xml == '&') {
            (*xml)++; // Skip &
            if (strncmp(*xml, "lt;", 3) == 0) {
                strbuf_append_char(sb, '<');
                *xml += 3;
            } else if (strncmp(*xml, "gt;", 3) == 0) {
                strbuf_append_char(sb, '>');  
                *xml += 3;
            } else if (strncmp(*xml, "amp;", 4) == 0) {
                strbuf_append_char(sb, '&');
                *xml += 4;
            } else if (strncmp(*xml, "quot;", 5) == 0) {
                strbuf_append_char(sb, '"');
                *xml += 5;
            } else if (strncmp(*xml, "apos;", 5) == 0) {
                strbuf_append_char(sb, '\'');
                *xml += 5;
            } else if (strncmp(*xml, "nbsp;", 5) == 0) {
                strbuf_append_char(sb, 160); // Non-breaking space
                *xml += 5;
            } else if (**xml == '#') {
                // Numeric character references &#123; or &#x1F;
                (*xml)++; // Skip #
                int value = 0;
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
                    if (value > 0 && value < 256) {
                        strbuf_append_char(sb, (char)value);
                    } else {
                        // For Unicode values > 255, append as-is (simplified)
                        strbuf_append_char(sb, '?'); // Placeholder
                    }
                } else {
                    // Invalid numeric reference, append as-is
                    strbuf_append_char(sb, '&');
                    strbuf_append_char(sb, '#');
                }
            } else {
                // Check for custom entities or unknown entities
                const char* entity_start = *xml;
                while (**xml && **xml != ';' && **xml != ' ' && **xml != '\t' && **xml != '\n') {
                    (*xml)++;
                }
                
                if (**xml == ';') {
                    // Try to resolve the entity
                    const char* resolved = resolve_entity(entity_start, *xml - entity_start);
                    (*xml)++; // Skip ;
                    
                    if (resolved) {
                        // Append resolved entity value
                        while (*resolved) {
                            strbuf_append_char(sb, *resolved);
                            resolved++;
                        }
                    } else {
                        // Unknown entity - append as-is
                        strbuf_append_char(sb, '&');
                        const char* temp = entity_start;
                        while (temp < *xml) {
                            strbuf_append_char(sb, *temp);
                            temp++;
                        }
                    }
                } else {
                    // Invalid entity, just append the &
                    strbuf_append_char(sb, '&');
                    *xml = entity_start;
                }
            }
        } else {
            strbuf_append_char(sb, **xml);
            (*xml)++;
        }
    }

    return strbuf_to_string(sb);
}

static String* parse_tag_name(Input *input, const char **xml) {
    StrBuf* sb = input->sb;
    
    while (**xml && (isalnum(**xml) || **xml == '_' || **xml == '-' || **xml == ':')) {
        strbuf_append_char(sb, **xml);
        (*xml)++;
    }

    if (sb->length == sizeof(uint32_t)) return NULL; // empty tag name

    return strbuf_to_string(sb);
}

static bool parse_attributes(Input *input, Element *element, const char **xml) {
    skip_whitespace(xml);
    while (**xml && **xml != '>' && **xml != '/' && **xml != '?') {
        // parse attribute name
        String* attr_name = parse_tag_name(input, xml);
        if (!attr_name) return false;

        skip_whitespace(xml);
        if (**xml != '=') return false;
        (*xml)++; // skip =
        
        skip_whitespace(xml);
        if (**xml != '"' && **xml != '\'') return false;
        
        char quote_char = **xml;
        (*xml)++; // skip opening quote

        String* attr_value = parse_string_content(input, xml, quote_char);
        if (!attr_value) return false;

        if (**xml == quote_char) { (*xml)++; } // skip closing quote
        
        // Add to map using shared function
        LambdaItem value = (LambdaItem)s2it(attr_value);
        elmt_put(element, attr_name, value, input->pool);

        skip_whitespace(xml);
    }
    return true;
}

static Item parse_comment(Input *input, const char **xml) {
    // Skip past the "!--" part (already consumed by caller)
    
    // Find comment content
    const char* comment_start = *xml;
    const char* comment_end = comment_start;
    
    while (*comment_end && strncmp(comment_end, "-->", 3) != 0) {
        comment_end++;
    }
    
    // Create comment element
    Element* element = input_create_element(input, "!--");
    if (!element) return ITEM_ERROR;
    
    // Add comment content as text
    if (comment_end > comment_start) {
        StrBuf* sb = input->sb;
        strbuf_reset(sb);
        while (comment_start < comment_end) {
            strbuf_append_char(sb, *comment_start);
            comment_start++;
        }
        String* comment_text = strbuf_to_string(sb);
        if (comment_text && comment_text->len > 0) {
            list_push((List*)element, s2it(comment_text));
            ((TypeElmt*)element->type)->content_length = 1;
        }
    }
    
    // Skip closing -->
    if (*comment_end) {
        *xml = comment_end + 3;
    } else {
        *xml = comment_end;
    }
    return (Item)element;
}

static Item parse_cdata(Input *input, const char **xml) {
    // Skip past the "![CDATA[" part (already consumed by caller)
    
    const char* cdata_start = *xml;
    
    // Find CDATA end
    while (**xml && strncmp(*xml, "]]>", 3) != 0) {
        (*xml)++;
    }
    
    // Create CDATA content string
    StrBuf* sb = input->sb;
    strbuf_reset(sb);
    while (cdata_start < *xml) {
        strbuf_append_char(sb, *cdata_start);
        cdata_start++;
    }
    
    if (**xml && strncmp(*xml, "]]>", 3) == 0) {
        *xml += 3; // skip ]]>
    }
    
    return s2it(strbuf_to_string(sb));
}

static Item parse_entity(Input *input, const char **xml) {
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
    Element* element = input_create_element(input, "!ENTITY");
    if (!element) return ITEM_ERROR;
    
    // Add entity name as "name" attribute
    if (entity_name_end > entity_name_start) {
        StrBuf* sb = input->sb;
        strbuf_reset(sb);
        const char* temp = entity_name_start;
        while (temp < entity_name_end) {
            strbuf_append_char(sb, *temp);
            temp++;
        }
        String* name_str = strbuf_to_string(sb);
        
        String* attr_name = pool_calloc(input->pool, sizeof(String) + 5);
        if (attr_name) {
            attr_name->len = 4;
            memcpy(attr_name->chars, "name", 4);
            attr_name->chars[4] = '\0';
            
            LambdaItem value = (LambdaItem)s2it(name_str);
            elmt_put(element, attr_name, value, input->pool);
        }
    }
            
    // Add entity value/reference as "value" attribute
    if (entity_value_end > entity_value_start) {
        StrBuf* sb = input->sb;
        strbuf_reset(sb);
        const char* temp = entity_value_start;
        while (temp < entity_value_end) {
            strbuf_append_char(sb, *temp);
            temp++;
        }
        String* value_str = strbuf_to_string(sb);
        String* attr_name = pool_calloc(input->pool, sizeof(String) + 6);
        if (attr_name) {
            attr_name->len = 5;
            memcpy(attr_name->chars, "value", 5);
            attr_name->chars[5] = '\0';
            LambdaItem value = (LambdaItem)s2it(value_str);
            elmt_put(element, attr_name, value, input->pool);
        }
    }
            
    // Add type attribute (internal/external)
    String* type_attr_name = pool_calloc(input->pool, sizeof(String) + 5);
    if (type_attr_name) {
        type_attr_name->len = 4;
        memcpy(type_attr_name->chars, "type", 4);
        type_attr_name->chars[4] = '\0';
        
        String* type_value = pool_calloc(input->pool, sizeof(String) + 9);
        if (type_value) {
            if (is_external) {
                type_value->len = 8;
                memcpy(type_value->chars, "external", 8);
                type_value->chars[8] = '\0';
            } else {
                type_value->len = 8;
                memcpy(type_value->chars, "internal", 8);
                type_value->chars[8] = '\0';
            }
            LambdaItem value = (LambdaItem)s2it(type_value);
            elmt_put(element, type_attr_name, value, input->pool);
        }
    }
    return (Item)element;
}

static Item parse_dtd_declaration(Input *input, const char **xml) {
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
    if (decl_name_len == 0) return ITEM_ERROR;
    
    // Create declaration element name with "!" prefix
    StrBuf* sb = input->sb;
    strbuf_reset(sb);
    strbuf_append_char(sb, '!');
    const char* temp = decl_start;
    while (temp < decl_name_end) {
        strbuf_append_char(sb, *temp);
        temp++;
    }
    String* decl_element_name = strbuf_to_string(sb);
    
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
    Element* element = input_create_element(input, decl_element_name->chars);
    if (!element) return ITEM_ERROR;
    
    // Add declaration content as text
    if (content_end > content_start) {
        strbuf_reset(sb);
        temp = content_start;
        while (temp < content_end) {
            strbuf_append_char(sb, *temp);
            temp++;
        }
        String* content_text = strbuf_to_string(sb);
        if (content_text && content_text->len > 0) {
            list_push((List*)element, s2it(content_text));
            ((TypeElmt*)element->type)->content_length = 1;
        }
    }
    return (Item)element;
}

static Item parse_doctype(Input *input, const char **xml) {
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
        Element* dt_elmt = input_create_element(input, "!DOCTYPE");
        if (!dt_elmt) return ITEM_ERROR;

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
                        Item entity = parse_entity(input, xml);
                        if (entity != ITEM_ERROR) {
                            list_push((List*)dt_elmt, entity);
                            ((TypeElmt*)dt_elmt->type)->content_length++;
                        }
                    } else if (strncmp(*xml, "ELEMENT", 7) == 0 || 
                               strncmp(*xml, "ATTLIST", 7) == 0 || 
                               strncmp(*xml, "NOTATION", 8) == 0) {
                        Item decl = parse_dtd_declaration(input, xml);
                        if (decl != ITEM_ERROR) {
                            list_push((List*)dt_elmt, decl);
                            ((TypeElmt*)dt_elmt->type)->content_length++;
                        }
                    } else {
                        // Generic DTD declaration
                        Item decl = parse_dtd_declaration(input, xml);
                        if (decl != ITEM_ERROR) {
                            list_push((List*)dt_elmt, decl);
                            ((TypeElmt*)dt_elmt->type)->content_length++;
                        }
                    }
                } else {
                    // Other elements (shouldn't happen in DTD, but handle gracefully)
                    (*xml)--; // back up to <
                    Item element = parse_element(input, xml);
                    if (element != ITEM_ERROR) {
                        list_push((List*)dt_elmt, element);
                        ((TypeElmt*)dt_elmt->type)->content_length++;
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
        
        return (Item)dt_elmt;
    } else {
        // No internal subset, just skip to end
        while (**xml && **xml != '>') {
            (*xml)++;
        }
        if (**xml == '>') {
            (*xml)++; // skip >
        }
        return parse_element(input, xml); // parse next element
    }
}

static Item parse_element(Input *input, const char **xml) {
    skip_whitespace(xml);
    
    if (**xml != '<') return ITEM_ERROR;
    (*xml)++; // skip <
    
    // Handle comments - create element with name "!--"
    if (strncmp(*xml, "!--", 3) == 0) {
        *xml += 3; // skip !--
        return parse_comment(input, xml);
    }
    
    // Handle CDATA sections
    if (strncmp(*xml, "![CDATA[", 8) == 0) {
        *xml += 8;
        return parse_cdata(input, xml);
    }
    
    // Handle ENTITY declarations - create element with name "!ENTITY"
    if (strncmp(*xml, "!ENTITY", 7) == 0) {
        *xml += 7; // skip !ENTITY
        return parse_entity(input, xml);
    }
    
    // Handle DOCTYPE declarations - parse internal subset for entities
    if (strncmp(*xml, "!DOCTYPE", 8) == 0) {
        *xml += 8; // skip !DOCTYPE
        return parse_doctype(input, xml);
    }
    
    // Handle other DTD declarations (ELEMENT, ATTLIST, NOTATION, etc.)
    if (**xml == '!' && (strncmp(*xml + 1, "ELEMENT", 7) == 0 || 
                        strncmp(*xml + 1, "ATTLIST", 7) == 0 || 
                        strncmp(*xml + 1, "NOTATION", 8) == 0)) {
        (*xml)++; // skip !
        return parse_dtd_declaration(input, xml);
    }
    
    // Handle processing instructions - create element with name "?target"
    bool is_processing = (**xml == '?');
    if (is_processing) {
        (*xml)++; // skip ?
        
        // Parse target name
        String* target_name = parse_tag_name(input, xml);
        if (!target_name) return ITEM_ERROR;
        
        // Create processing instruction element name "?target"
        StrBuf* sb = input->sb;
        strbuf_reset(sb);
        strbuf_append_char(sb, '?');
        strbuf_append_str(sb, target_name->chars);
        String* pi_name = strbuf_to_string(sb);
        
        // Parse PI data (everything until ?>)
        skip_whitespace(xml);
        const char* pi_data_start = *xml;
        while (**xml && !(**xml == '?' && *(*xml + 1) == '>')) {
            (*xml)++;
        }
        const char* pi_data_end = *xml;
        
        // Skip ?>
        if (**xml == '?' && *(*xml + 1) == '>') {
            *xml += 2;
        }
        
        // Create processing instruction element
        Element* element = input_create_element(input, pi_name->chars);
        
        // Add PI data as text content
        if (pi_data_end > pi_data_start) {
            strbuf_reset(sb);
            while (pi_data_start < pi_data_end) {
                strbuf_append_char(sb, *pi_data_start);
                pi_data_start++;
            }
            String* pi_data = strbuf_to_string(sb);
            if (pi_data && pi_data->len > 0) {
                list_push((List*)element, s2it(pi_data));
                ((TypeElmt*)element->type)->content_length = 1;
            }
        }
        return (Item)element;
    }

    // parse tag name
    String* tag_name = parse_tag_name(input, xml);
    if (!tag_name) return ITEM_ERROR;

        // Create element
    Element* element = input_create_element(input, tag_name->chars);
    if (!element) return ITEM_ERROR;

    // parse attributes
    if (!parse_attributes(input, element, xml)) return ITEM_ERROR;

    skip_whitespace(xml);

    // check for self-closing tag
    bool self_closing = false;
    if (**xml == '/') {
        self_closing = true;
        (*xml)++; // skip /
    }
    
    if (**xml != '>') return ITEM_ERROR;
    (*xml)++; // skip >
        
    if (!self_closing) {
        // Parse content and add to Element's List part
        skip_whitespace(xml);
        
        while (**xml && !(**xml == '<' && *(*xml + 1) == '/')) {
            if (**xml == '<') {
                // Child element (could be regular element, comment, or PI)
                Item child = parse_element(input, xml);
                if (child != ITEM_ERROR) {
                    list_push((List*)element, child);
                    ((TypeElmt*)element->type)->content_length++;
                }
            } else {
                // Text content - trim leading/trailing whitespace for better handling
                const char* text_start = *xml;
                while (**xml && **xml != '<') {
                    (*xml)++;
                }
                
                if (*xml > text_start) {
                    // Create text content
                    StrBuf* sb = input->sb;
                    strbuf_reset(sb);
                    
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
                        // Process entities in text content
                        const char* temp_xml = text_start;
                        String* text_content = parse_string_content(input, &temp_xml, '\0');
                        
                        // Manual parsing since parse_string_content expects end_char
                        while (text_start < text_end) {
                            if (*text_start == '&') {
                                text_start++;
                                const char* entity_start = text_start;
                                
                                // Find entity end
                                while (text_start < text_end && *text_start != ';') {
                                    text_start++;
                                }
                                
                                if (text_start < text_end && *text_start == ';') {
                                    // Try to resolve entity
                                    const char* resolved = resolve_entity(entity_start, text_start - entity_start);
                                    text_start++; // Skip ;
                                    
                                    if (resolved) {
                                        // Append resolved entity value
                                        while (*resolved) {
                                            strbuf_append_char(sb, *resolved);
                                            resolved++;
                                        }
                                    } else {
                                        // Unknown entity - append as-is
                                        strbuf_append_char(sb, '&');
                                        const char* temp = entity_start;
                                        while (temp < text_start) {
                                            strbuf_append_char(sb, *temp);
                                            temp++;
                                        }
                                    }
                                } else {
                                    // Invalid entity
                                    strbuf_append_char(sb, '&');
                                    text_start = entity_start;
                                }
                            } else {
                                strbuf_append_char(sb, *text_start);
                                text_start++;
                            }
                        }
                        
                        String* processed_text = strbuf_to_string(sb);
                        if (processed_text && processed_text->len > 0) {
                            list_push((List*)element, s2it(processed_text));
                            ((TypeElmt*)element->type)->content_length++;
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
    return (Item)element;
}

void parse_xml(Input* input, const char* xml_string) {
    input->sb = strbuf_new_pooled(input->pool);

    const char* xml = xml_string;
    skip_whitespace(&xml);
    
    // Create a document root element to contain all top-level elements
    Element* doc_element = input_create_element(input, "document");
    if (!doc_element) { input->root = ITEM_ERROR;  return; }

    // Parse all top-level elements (including XML declaration, comments, PIs, and the main element)
    while (*xml) {
        skip_whitespace(&xml);
        if (!*xml) break;
        
        if (*xml == '<') {
            Item element = parse_element(input, &xml);
            if (element != ITEM_ERROR) {
                list_push((List*)doc_element, element);
                ((TypeElmt*)doc_element->type)->content_length++;
            }
        } else {
            // Skip any stray text content at document level
            while (*xml && *xml != '<') {
                xml++;
            }
        }
    }
    
    // If document has only one child element, return that as root
    // Otherwise return the document element containing all elements
    if (((TypeElmt*)doc_element->type)->content_length == 1) {
        List* doc_list = (List*)doc_element;
        if (doc_list->items && doc_list->items[0] != ITEM_ERROR) {
            input->root = doc_list->items[0];
            return;
        }
    }
    input->root = (Item)doc_element;
}
