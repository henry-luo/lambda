#include "input.h"
#include <ctype.h>

// Helper function to skip whitespace at the beginning of a line
static void skip_line_whitespace(const char **eml) {
    while (**eml && (**eml == ' ' || **eml == '\t')) {
        (*eml)++;
    }
}

// Helper function to skip to the next line
static void skip_to_newline(const char **eml) {
    while (**eml && **eml != '\n' && **eml != '\r') {
        (*eml)++;
    }
    if (**eml == '\r' && *(*eml + 1) == '\n') {
        (*eml) += 2; // skip \r\n
    } else if (**eml == '\n' || **eml == '\r') {
        (*eml)++; // skip \n or \r
    }
}

// Helper function to check if line starts with whitespace (indicating continuation)
static bool is_continuation_line(const char *eml) {
    return *eml == ' ' || *eml == '\t';
}

// Helper function to parse header name
static String* parse_header_name(Input *input, const char **eml) {
    StrBuf* sb = input->sb;
    strbuf_reset(sb);  // Reset buffer for reuse
    
    while (**eml && **eml != ':' && **eml != '\n' && **eml != '\r') {
        strbuf_append_char(sb, **eml);
        (*eml)++;
    }
    
    String *string = (String*)sb->str;
    if (string && sb->length > sizeof(uint32_t)) {
        string->len = sb->length - sizeof(uint32_t);  
        string->ref_cnt = 0;
        return strbuf_to_string(sb);
    }
    return NULL;
}

// Helper function to parse header value (including continuation lines)
static String* parse_header_value(Input *input, const char **eml) {
    StrBuf* sb = input->sb;
    strbuf_reset(sb);  // Reset buffer for reuse
    
    // Skip the colon and initial whitespace
    if (**eml == ':') {
        (*eml)++;
        skip_line_whitespace(eml);
    }
    
    // Read the header value, handling continuation lines
    while (**eml) {
        if (**eml == '\n' || **eml == '\r') {
            // Check if next line is a continuation
            const char* next_line = *eml;
            if (*next_line == '\r' && *(next_line + 1) == '\n') {
                next_line += 2;
            } else if (*next_line == '\n' || *next_line == '\r') {
                next_line++;
            }
            
            if (is_continuation_line(next_line)) {
                // Replace line break with space and continue
                strbuf_append_char(sb, ' ');
                skip_to_newline(eml);
                skip_line_whitespace(eml);
            } else {
                // End of header value
                break;
            }
        } else {
            strbuf_append_char(sb, **eml);
            (*eml)++;
        }
    }
    
    // Trim trailing whitespace
    while (sb->length > sizeof(uint32_t) && 
           (sb->str[sb->length - 1] == ' ' || sb->str[sb->length - 1] == '\t')) {
        sb->length--;
    }
    
    String *string = (String*)sb->str;
    if (string && sb->length > sizeof(uint32_t)) {
        string->len = sb->length - sizeof(uint32_t);  
        string->ref_cnt = 0;
        return strbuf_to_string(sb);
    }
    return NULL;
}

// Helper function to normalize header name to lowercase
static void normalize_header_name(char* name) {
    for (int i = 0; name[i]; i++) {
        name[i] = tolower(name[i]);
    }
}

// Helper function to parse email addresses from a header value
static String* extract_email_address(Input *input, const char* header_value) {
    if (!header_value) return NULL;
    
    StrBuf* sb = input->sb;
    strbuf_reset(sb);  // Reset buffer for reuse
    const char* start = strchr(header_value, '<');
    const char* end = NULL;
    
    if (start) {
        // Format: "Name <email@domain.com>"
        start++;
        end = strchr(start, '>');
        if (end) {
            while (start < end) {
                strbuf_append_char(sb, *start);
                start++;
            }
        }
    } else {
        // Format: "email@domain.com" or "Name email@domain.com"
        const char* at_pos = strchr(header_value, '@');
        if (at_pos) {
            // Find start of email (look backwards for space or start of string)
            start = at_pos;
            while (start > header_value && *(start-1) != ' ' && *(start-1) != '\t') {
                start--;
            }
            
            // Find end of email (look forwards for space or end of string)
            end = at_pos;
            while (*end && *end != ' ' && *end != '\t' && *end != '\n' && *end != '\r') {
                end++;
            }
            
            while (start < end) {
                strbuf_append_char(sb, *start);
                start++;
            }
        }
    }
    
    if (sb->length > sizeof(uint32_t)) {
        String *string = (String*)sb->str;
        string->len = sb->length - sizeof(uint32_t);  
        string->ref_cnt = 0;
        return strbuf_to_string(sb);
    }
    
    return NULL;
}

// Helper function to parse date value
static String* parse_date_value(Input *input, const char* date_str) {
    if (!date_str) return NULL;
    
    // For now, just return the raw date string
    // TODO: Could parse into structured datetime format
    return input_create_string(input, date_str);
}

// Main EML parsing function
void parse_eml(Input* input, const char* eml_string) {
    if (!eml_string || !input) return;
    
    // Initialize string buffer for parsing
    input->sb = strbuf_new_pooled(input->pool);
    if (!input->sb) return;
    
    const char* eml = eml_string;
    
    // Create root map for the email
    Map* email_map = map_pooled(input->pool);
    if (!email_map) return;
    
    // Initialize headers map
    Map* headers_map = map_pooled(input->pool);
    if (!headers_map) return;
    
    // Parse headers  
    while (*eml) {
        // Check if we've reached the empty line separating headers from body
        if (*eml == '\n') {
            // Look ahead to see if the next character is also a newline (empty line)
            if (*(eml+1) == '\n') {
                // Found double newline - end of headers
                eml += 2; // Skip both newlines to get to body
                break;
            } else if (*(eml+1) == '\r' && *(eml+2) == '\n') {
                // Found \n\r\n - end of headers
                eml += 3; // Skip all three characters to get to body  
                break;
            }
        } else if (*eml == '\r' && *(eml+1) == '\n') {
            // Check for \r\n\r\n or \r\n\n
            if (*(eml+2) == '\r' && *(eml+3) == '\n') {
                // Found \r\n\r\n - end of headers
                eml += 4; // Skip all four characters to get to body
                break;
            } else if (*(eml+2) == '\n') {
                // Found \r\n\n - end of headers
                eml += 3; // Skip all three characters to get to body
                break;
            }
        }
        
        // Skip empty lines in headers
        if (*eml == '\n' || *eml == '\r') {
            skip_to_newline(&eml);
            continue;
        }
        
        // Skip lines that start with whitespace (continuation lines are handled in parse_header_value)
        if (is_continuation_line(eml)) {
            skip_to_newline(&eml);
            continue;
        }
        
        // Parse header name
        String* header_name = parse_header_name(input, &eml);
        if (!header_name) {
            skip_to_newline(&eml);
            continue;
        }
        
        // Parse header value
        String* header_value = parse_header_value(input, &eml);
        if (!header_value) {
            skip_to_newline(&eml);
            continue;
        }
        
        // Normalize header name to lowercase for consistency
        normalize_header_name(header_name->chars);
        
        // Store header in headers map
        LambdaItem value = (LambdaItem)s2it(header_value);
        map_put(headers_map, header_name, value, input);
        
        // Also store common headers as top-level fields for easier access
        if (strcmp(header_name->chars, "from") == 0) {
            String* from_email = extract_email_address(input, header_value->chars);
            if (from_email) {
                String* from_key = input_create_string(input, "from");
                LambdaItem from_value = (LambdaItem)s2it(from_email);
                map_put(email_map, from_key, from_value, input);
            }
        }
        else if (strcmp(header_name->chars, "to") == 0) {
            String* to_email = extract_email_address(input, header_value->chars);
            if (to_email) {
                String* to_key = input_create_string(input, "to");
                LambdaItem to_value = (LambdaItem)s2it(to_email);
                map_put(email_map, to_key, to_value, input);
            }
        }
        else if (strcmp(header_name->chars, "subject") == 0) {
            String* subject_key = input_create_string(input, "subject");
            LambdaItem subject_value = (LambdaItem)s2it(header_value);
            map_put(email_map, subject_key, subject_value, input);
        }
        else if (strcmp(header_name->chars, "date") == 0) {
            String* date_parsed = parse_date_value(input, header_value->chars);
            if (date_parsed) {
                String* date_key = input_create_string(input, "date");
                LambdaItem date_value = (LambdaItem)s2it(date_parsed);
                map_put(email_map, date_key, date_value, input);
            }
        }
        else if (strcmp(header_name->chars, "message-id") == 0) {
            String* msgid_key = input_create_string(input, "message_id");
            LambdaItem msgid_value = (LambdaItem)s2it(header_value);
            map_put(email_map, msgid_key, msgid_value, input);
        }
    }
    
    // Store headers map in email
    String* headers_key = input_create_string(input, "headers");
    LambdaItem headers_value = (LambdaItem)((((uint64_t)LMD_TYPE_MAP)<<56) | (uint64_t)(headers_map));
    map_put(email_map, headers_key, headers_value, input);
    
    // At this point, eml should be positioned at the start of the body
    // Parse body
    StrBuf* body_sb = input->sb;
    strbuf_reset(body_sb);  // Reset buffer for reuse
    
    while (*eml) {
        strbuf_append_char(body_sb, *eml);
        eml++;
    }
    
    if (body_sb->length > sizeof(uint32_t)) {
        String* body_string = strbuf_to_string(body_sb);
        if (body_string) {
            String* body_key = input_create_string(input, "body");
            LambdaItem body_value = (LambdaItem)s2it(body_string);
            map_put(email_map, body_key, body_value, input);
        }
    }
    
    // Set the email map as the root of the input
    input->root = (Item)email_map;
    input->root |= ((uint64_t)LMD_TYPE_MAP << 56);
}
