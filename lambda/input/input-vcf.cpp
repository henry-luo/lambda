#include "input.hpp"
#include "../mark_builder.hpp"
#include "input-context.hpp"
#include "source_tracker.hpp"
#include <ctype.h>

using namespace lambda;

// Helper function to skip whitespace at the beginning of a line
static void skip_line_whitespace(const char **vcf) {
    while (**vcf && (**vcf == ' ' || **vcf == '\t')) {
        (*vcf)++;
    }
}

// Helper function to skip to the next line
static void skip_to_newline(const char **vcf) {
    while (**vcf && **vcf != '\n' && **vcf != '\r') {
        (*vcf)++;
    }
    if (**vcf == '\r' && *(*vcf + 1) == '\n') {
        (*vcf) += 2; // skip \r\n
    } else if (**vcf == '\n' || **vcf == '\r') {
        (*vcf)++; // skip \n or \r
    }
}

// Helper function to check if line starts with whitespace (indicating folded line)
static bool is_folded_line(const char *vcf) {
    return *vcf == ' ' || *vcf == '\t';
}

// Helper function to parse property name (before the colon)
static String* parse_property_name(InputContext& ctx, const char **vcf) {
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    while (**vcf && **vcf != ':' && **vcf != ';' && **vcf != '\n' && **vcf != '\r') {
        stringbuf_append_char(sb, **vcf);
        (*vcf)++;
    }

    if (sb->length > sizeof(uint32_t)) {
        String* result = builder.createString(sb->str->chars, sb->length);
        return result;
    }
    return NULL;
}

// Helper function to parse property parameters (between ; and :)
static void parse_property_parameters(InputContext& ctx, const char **vcf, Map* params_map) {
    MarkBuilder& builder = ctx.builder;
    Input* input = ctx.input();

    while (**vcf == ';') {
        (*vcf)++; // skip ';'

        // Parse parameter name
        StringBuf* sb = ctx.sb;
        stringbuf_reset(sb);

        while (**vcf && **vcf != '=' && **vcf != ':' && **vcf != '\n' && **vcf != '\r') {
            stringbuf_append_char(sb, tolower(**vcf));
            (*vcf)++;
        }

        if (sb->length <= sizeof(uint32_t)) continue;

        String* param_name = builder.createName(sb->str->chars, sb->length);
        if (!param_name) continue;

        String* param_value = NULL;

        if (**vcf == '=') {
            (*vcf)++; // skip '='
            stringbuf_reset(sb);

            // Handle quoted values
            bool in_quotes = false;
            if (**vcf == '"') {
                (*vcf)++;
                in_quotes = true;
            }

            while (**vcf &&
                   (in_quotes ? **vcf != '"' : (**vcf != ';' && **vcf != ':')) &&
                   **vcf != '\n' && **vcf != '\r') {
                stringbuf_append_char(sb, **vcf);
                (*vcf)++;
            }

            if (in_quotes && **vcf == '"') {
                (*vcf)++; // skip closing quote
            }

            if (sb->length > sizeof(uint32_t)) {
                param_value = builder.createString(sb->str->chars, sb->length);
            }
        }

        if (param_value) {
            Item value = {.item = s2it(param_value)};
            ctx.builder.putToMap(params_map, param_name, value);
        }
    }
}

// Helper function to parse property value (after the colon, handling folded lines)
static String* parse_property_value(InputContext& ctx, const char **vcf) {
    if (**vcf != ':') return NULL;

    (*vcf)++; // skip ':'

    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);

    // Parse the value, handling line folding
    while (**vcf) {
        if (**vcf == '\r' || **vcf == '\n') {
            // Check for line folding (next line starts with space or tab)
            const char* next_line = *vcf;

            // Skip current line ending
            if (*next_line == '\r' && *(next_line + 1) == '\n') {
                next_line += 2;
            } else {
                next_line++;
            }

            // Check if next line is folded
            if (is_folded_line(next_line)) {
                // This is a folded line, replace line ending with space and continue
                stringbuf_append_char(sb, ' ');
                *vcf = next_line;
                skip_line_whitespace(vcf); // skip the folding whitespace
            } else {
                // End of this property value
                *vcf = next_line;
                break;
            }
        } else {
            stringbuf_append_char(sb, **vcf);
            (*vcf)++;
        }
    }

    if (sb->length > sizeof(uint32_t)) {
        return builder.createString(sb->str->chars, sb->length);
    }
    return NULL;
}

// Helper function to normalize property name to lowercase
static void normalize_property_name(char* name) {
    while (*name) {
        *name = tolower(*name);
        name++;
    }
}

// Helper function to parse structured name (N property)
static Map* parse_structured_name(InputContext& ctx, const char* value) {
    Input* input = ctx.input();
    MarkBuilder& builder = ctx.builder;

    Map* name_map = map_pooled(input->pool);
    if (!name_map) return NULL;

    // N property format: Family;Given;Additional;Prefix;Suffix
    const char* ptr = value;
    const char* field_names[] = {"family", "given", "additional", "prefix", "suffix"};
    int field_count = sizeof(field_names) / sizeof(field_names[0]);

    for (int i = 0; i < field_count && *ptr; i++) {
        StringBuf* sb = ctx.sb;
        stringbuf_reset(sb);

        // Parse until semicolon or end
        while (*ptr && *ptr != ';') {
            stringbuf_append_char(sb, *ptr);
            ptr++;
        }

        if (sb->length > sizeof(uint32_t)) {
            String* field_value = builder.createString(sb->str->chars, sb->length);
            if (field_value && field_value->len > 0) {
                String* field_key = builder.createName(field_names[i]);
                Item value_item = {.item = s2it(field_value)};
                ctx.builder.putToMap(name_map, field_key, value_item);
            }
        }

        if (*ptr == ';') ptr++; // skip semicolon
    }

    return name_map;
}

// Helper function to parse address (ADR property)
static Map* parse_address(InputContext& ctx, const char* value) {
    Input* input = ctx.input();
    MarkBuilder& builder = ctx.builder;

    Map* addr_map = map_pooled(input->pool);
    if (!addr_map) return NULL;

    // ADR property format: PO Box;Extended;Street;City;State;Postal Code;Country
    const char* ptr = value;
    const char* field_names[] = {"po_box", "extended", "street", "city", "state", "postal_code", "country"};
    int field_count = sizeof(field_names) / sizeof(field_names[0]);

    for (int i = 0; i < field_count && *ptr; i++) {
        StringBuf* sb = ctx.sb;
        stringbuf_reset(sb);

        // Parse until semicolon or end
        while (*ptr && *ptr != ';') {
            stringbuf_append_char(sb, *ptr);
            ptr++;
        }

        if (sb->length > sizeof(uint32_t)) {
            String* field_value = builder.createString(sb->str->chars, sb->length);
            if (field_value && field_value->len > 0) {
                String* field_key = builder.createName(field_names[i]);
                Item value_item = {.item = s2it(field_value)};
                ctx.builder.putToMap(addr_map, field_key, value_item);
            }
        }

        if (*ptr == ';') ptr++; // skip semicolon
    }

    return addr_map;
}

// Main vCard parsing function
void parse_vcf(Input* input, const char* vcf_string) {
    if (!vcf_string || !input) return;

    // create error tracking context with source tracking
    InputContext ctx(input, vcf_string, strlen(vcf_string));
    MarkBuilder& builder = ctx.builder;

    const char* vcf = vcf_string;

    // Initialize contact map
    Map* contact_map = map_pooled(input->pool);
    if (!contact_map) {
        ctx.addError(ctx.tracker.location(), "Failed to allocate memory for contact map");
        return;
    }

    // Initialize properties map to store all raw properties
    Map* properties_map = map_pooled(input->pool);
    if (!properties_map) {
        ctx.addError(ctx.tracker.location(), "Failed to allocate memory for properties map");
        return;
    }

    bool in_vcard = false;

    // Parse vCard line by line
    while (*vcf) {
        // Skip empty lines
        if (*vcf == '\n' || *vcf == '\r') {
            skip_to_newline(&vcf);
            continue;
        }

        // Skip lines that start with whitespace if we're not in a vCard
        if (!in_vcard && is_folded_line(vcf)) {
            skip_to_newline(&vcf);
            continue;
        }

        // Parse property name
        String* property_name = parse_property_name(ctx, &vcf);
        if (!property_name) {
            skip_to_newline(&vcf);
            continue;
        }

        // Normalize property name to lowercase
        normalize_property_name(property_name->chars);

        // Parse property parameters
        Map* params_map = map_pooled(input->pool);
        if (params_map) {
            parse_property_parameters(ctx, &vcf, params_map);
        }

        // Parse property value
        String* property_value = parse_property_value(ctx, &vcf);
        if (!property_value) {
            continue;
        }

        // Handle vCard start and end
        if (strcmp(property_name->chars, "begin") == 0) {
            if (strcasecmp(property_value->chars, "VCARD") == 0) {
                in_vcard = true;
            }
            continue;
        }

        if (strcmp(property_name->chars, "end") == 0) {
            if (strcasecmp(property_value->chars, "VCARD") == 0) {
                in_vcard = false;
            }
            continue;
        }

        if (!in_vcard) continue;

        // Store raw property in properties map
        Item prop_value = {.item = s2it(property_value)};
        ctx.builder.putToMap(properties_map, property_name, prop_value);

        // Handle common properties with special processing
        if (strcmp(property_name->chars, "fn") == 0) {
            // Full Name - store as top-level field
            String* fn_key = builder.createName("full_name");
            Item fn_value = {.item = s2it(property_value)};
            ctx.builder.putToMap(contact_map, fn_key, fn_value);
        }
        else if (strcmp(property_name->chars, "n") == 0) {
            // Structured Name
            Map* name_struct = parse_structured_name(ctx, property_value->chars);
            if (name_struct) {
                String* name_key = builder.createName("name");
                Item name_value = {.item = ((((uint64_t)LMD_TYPE_MAP)<<56) | (uint64_t)(name_struct))};
                ctx.builder.putToMap(contact_map, name_key, name_value);
            }
        }
        else if (strcmp(property_name->chars, "email") == 0) {
            // Email - store as top-level field
            String* email_key = builder.createName("email");
            Item email_value = {.item = s2it(property_value)};
            ctx.builder.putToMap(contact_map, email_key, email_value);
        }
        else if (strcmp(property_name->chars, "tel") == 0) {
            // Phone - store as top-level field
            String* phone_key = builder.createName("phone");
            Item phone_value = {.item = s2it(property_value)};
            ctx.builder.putToMap(contact_map, phone_key, phone_value);
        }
        else if (strcmp(property_name->chars, "adr") == 0) {
            // Address
            Map* addr_struct = parse_address(ctx, property_value->chars);
            if (addr_struct) {
                String* addr_key = builder.createName("address");
                Item addr_value = {.item = ((((uint64_t)LMD_TYPE_MAP)<<56) | (uint64_t)(addr_struct))};
                ctx.builder.putToMap(contact_map, addr_key, addr_value);
            }
        }
        else if (strcmp(property_name->chars, "org") == 0) {
            // Organization - store as top-level field
            String* org_key = builder.createName("organization");
            Item org_value = {.item = s2it(property_value)};
            ctx.builder.putToMap(contact_map, org_key, org_value);
        }
        else if (strcmp(property_name->chars, "title") == 0) {
            // Job Title - store as top-level field
            String* title_key = builder.createName("title");
            Item title_value = {.item = s2it(property_value)};
            ctx.builder.putToMap(contact_map, title_key, title_value);
        }
        else if (strcmp(property_name->chars, "note") == 0) {
            // Note - store as top-level field
            String* note_key = builder.createName("note");
            Item note_value = {.item = s2it(property_value)};
            ctx.builder.putToMap(contact_map, note_key, note_value);
        }
        else if (strcmp(property_name->chars, "url") == 0) {
            // URL - store as top-level field
            String* url_key = builder.createName("url");
            Item url_value = {.item = s2it(property_value)};
            ctx.builder.putToMap(contact_map, url_key, url_value);
        }
        else if (strcmp(property_name->chars, "bday") == 0) {
            // Birthday - store as top-level field
            String* bday_key = builder.createName("birthday");
            Item bday_value = {.item = s2it(property_value)};
            ctx.builder.putToMap(contact_map, bday_key, bday_value);
        }
        else if (strcmp(property_name->chars, "version") == 0) {
            // vCard Version - store as top-level field
            String* version_key = builder.createName("version");
            Item version_value = {.item = s2it(property_value)};
            ctx.builder.putToMap(contact_map, version_key, version_value);
        }
    }

    // Store properties map in contact
    String* properties_key = builder.createName("properties");
    Item properties_value = {.item = ((((uint64_t)LMD_TYPE_MAP)<<56) | (uint64_t)(properties_map))};
    ctx.builder.putToMap(contact_map, properties_key, properties_value);

    // Set the contact map as the root of the input
    input->root = {.item = ((uint64_t)LMD_TYPE_MAP << 56) | (uint64_t)contact_map};

    if (ctx.hasErrors()) {
        ctx.logErrors();
    }
}
