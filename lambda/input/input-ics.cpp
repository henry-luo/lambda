#include "input.h"
#include "../mark_builder.hpp"
#include <ctype.h>

// Helper function to skip whitespace at the beginning of a line
static void skip_line_whitespace(const char **ics) {
    while (**ics && (**ics == ' ' || **ics == '\t')) {
        (*ics)++;
    }
}

// Helper function to skip to the next line
static void skip_to_newline(const char **ics) {
    while (**ics && **ics != '\n' && **ics != '\r') {
        (*ics)++;
    }
    if (**ics == '\r' && *(*ics + 1) == '\n') {
        (*ics) += 2; // skip \r\n
    } else if (**ics == '\n' || **ics == '\r') {
        (*ics)++; // skip \n or \r
    }
}

// Helper function to check if line starts with whitespace (indicating folded line)
static bool is_folded_line(const char *ics) {
    return *ics == ' ' || *ics == '\t';
}

// Helper function to parse property name (before the colon)
static String* parse_property_name(Input *input, MarkBuilder* builder, const char **ics) {
    StringBuf* sb = builder->stringBuf();
    stringbuf_reset(sb);  // Reset buffer for reuse

    while (**ics && **ics != ':' && **ics != ';' && **ics != '\n' && **ics != '\r') {
        stringbuf_append_char(sb, **ics);
        (*ics)++;
    }

    if (sb->length > sizeof(uint32_t)) {
        String* result = builder->createString(sb->str->chars, sb->length);
        return result;
    }
    return NULL;
}

// Helper function to parse property parameters (between ; and :)
static void parse_property_parameters(Input *input, MarkBuilder* builder, const char **ics, Map* params_map) {
    while (**ics == ';') {
        (*ics)++; // skip ';'

        // Parse parameter name
        StringBuf* sb = builder->stringBuf();
        stringbuf_reset(sb);

        while (**ics && **ics != '=' && **ics != ':' && **ics != '\n' && **ics != '\r') {
            stringbuf_append_char(sb, toupper(**ics));
            (*ics)++;
        }

        if (sb->length <= sizeof(uint32_t)) continue;

        String* param_name = builder->createString(sb->str->chars, sb->length);
        if (!param_name) continue;

        String* param_value = NULL;

        if (**ics == '=') {
            (*ics)++; // skip '='
            stringbuf_reset(sb);

            // Handle quoted values
            bool in_quotes = false;
            if (**ics == '"') {
                (*ics)++;
                in_quotes = true;
            }

            while (**ics &&
                   (in_quotes ? **ics != '"' : (**ics != ';' && **ics != ':')) &&
                   **ics != '\n' && **ics != '\r') {
                stringbuf_append_char(sb, **ics);
                (*ics)++;
            }

            if (in_quotes && **ics == '"') {
                (*ics)++; // skip closing quote
            }

            if (sb->length > sizeof(uint32_t)) {
                param_value = stringbuf_to_string(sb);
            }
        }

        if (param_value) {
            Item value = {.item = s2it(param_value)};
            map_put(params_map, param_name, value, input);
        }
    }
}

// Helper function to parse property value (after the colon, handling folded lines)
static String* parse_property_value(Input *input, MarkBuilder* builder, const char **ics) {
    if (**ics != ':') return NULL;

    (*ics)++; // skip ':'

    StringBuf* sb = builder->stringBuf();
    stringbuf_reset(sb);  // Reset buffer for reuse

    // Parse the value, handling line folding
    while (**ics) {
        if (**ics == '\r' || **ics == '\n') {
            // Check for line folding (next line starts with space or tab)
            const char* next_line = *ics;

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
                *ics = next_line;
                skip_line_whitespace(ics); // skip the folding whitespace
            } else {
                // End of this property value
                *ics = next_line;
                break;
            }
        } else {
            stringbuf_append_char(sb, **ics);
            (*ics)++;
        }
    }

    if (sb->length > sizeof(uint32_t)) {
        return builder->createString(sb->str->chars, sb->length);
    }
    return NULL;
}

// Helper function to normalize property name to uppercase
static void normalize_property_name(char* name) {
    while (*name) {
        *name = toupper(*name);
        name++;
    }
}

// Helper function to parse date-time values
static Map* parse_datetime(Input *input, const char* value) {
    if (!input || !value) return NULL;

    Map* dt_map = map_pooled(input->pool);
    if (!dt_map) return NULL;

    // Parse various date-time formats
    // Format: YYYYMMDD, YYYYMMDDTHHMMSS, YYYYMMDDTHHMMSSZ
    const char* ptr = value;
    StringBuf* sb = input->sb;

    // Parse year (4 digits)
    if (strlen(ptr) >= 8) {
        stringbuf_reset(sb);
        for (int i = 0; i < 4; i++) {
            if (isdigit(*ptr)) {
                stringbuf_append_char(sb, *ptr++);
            } else {
                return dt_map; // Invalid format
            }
        }
        if (sb->length > sizeof(uint32_t)) {
            String* year_str = stringbuf_to_string(sb);
            String* year_key = input_create_string(input, "year");
            map_put(dt_map, year_key, {.item = s2it(year_str)}, input);
        }

        // Parse month (2 digits)
        stringbuf_reset(sb);
        for (int i = 0; i < 2; i++) {
            if (isdigit(*ptr)) {
                stringbuf_append_char(sb, *ptr++);
            } else {
                return dt_map;
            }
        }
        if (sb->length > sizeof(uint32_t)) {
            String* month_str = stringbuf_to_string(sb);
            String* month_key = input_create_string(input, "month");
            map_put(dt_map, month_key, {.item = s2it(month_str)}, input);
        }

        // Parse day (2 digits)
        stringbuf_reset(sb);
        for (int i = 0; i < 2; i++) {
            if (isdigit(*ptr)) {
                stringbuf_append_char(sb, *ptr++);
            } else {
                return dt_map;
            }
        }
        if (sb->length > sizeof(uint32_t)) {
            String* day_str = stringbuf_to_string(sb);
            String* day_key = input_create_string(input, "day");
            map_put(dt_map, day_key, {.item = s2it(day_str)}, input);
        }

        // Check for time part (T separator)
        if (*ptr == 'T' && strlen(ptr) >= 7) {
            ptr++; // skip 'T'

            // Parse hour (2 digits)
            stringbuf_reset(sb);
            for (int i = 0; i < 2; i++) {
                if (isdigit(*ptr)) {
                    stringbuf_append_char(sb, *ptr++);
                } else {
                    return dt_map;
                }
            }
            if (sb->length > sizeof(uint32_t)) {
                String* hour_str = stringbuf_to_string(sb);
                String* hour_key = input_create_string(input, "hour");
                map_put(dt_map, hour_key, {.item = s2it(hour_str)}, input);
            }

            // Parse minute (2 digits)
            stringbuf_reset(sb);
            for (int i = 0; i < 2; i++) {
                if (isdigit(*ptr)) {
                    stringbuf_append_char(sb, *ptr++);
                } else {
                    return dt_map;
                }
            }
            if (sb->length > sizeof(uint32_t)) {
                String* minute_str = stringbuf_to_string(sb);
                String* minute_key = input_create_string(input, "minute");
                map_put(dt_map, minute_key, {.item = s2it(minute_str)}, input);
            }

            // Parse second (2 digits)
            stringbuf_reset(sb);
            for (int i = 0; i < 2; i++) {
                if (isdigit(*ptr)) {
                    stringbuf_append_char(sb, *ptr++);
                } else {
                    return dt_map;
                }
            }
            if (sb->length > sizeof(uint32_t)) {
                String* second_str = stringbuf_to_string(sb);
                String* second_key = input_create_string(input, "second");
                map_put(dt_map, second_key, {.item = s2it(second_str)}, input);
            }

            // Check for timezone (Z for UTC)
            if (*ptr == 'Z') {
                String* tz_key = input_create_string(input, "timezone");
                String* tz_value = input_create_string(input, "UTC");
                map_put(dt_map, tz_key, {.item = s2it(tz_value)}, input);
            }
        }
    }

    return dt_map;
}

// Helper function to parse duration values
static Map* parse_duration(Input *input, const char* value) {
    if (!input || !value) return NULL;

    Map* dur_map = map_pooled(input->pool);
    if (!dur_map) return NULL;

    // Parse duration format: P[n]DT[n]H[n]M[n]S or P[n]W
    const char* ptr = value;

    if (*ptr != 'P') return dur_map; // Invalid duration format
    ptr++; // skip 'P'

    StringBuf* sb = input->sb;
    bool in_time_part = false;

    while (*ptr) {
        if (*ptr == 'T') {
            in_time_part = true;
            ptr++;
            continue;
        }

        // Parse number
        stringbuf_reset(sb);
        while (isdigit(*ptr)) {
            stringbuf_append_char(sb, *ptr++);
        }

        if (sb->length <= sizeof(uint32_t)) {
            ptr++; // skip the unit character
            continue;
        }

        String* num_str = stringbuf_to_string(sb);

        // Parse unit
        char unit = *ptr++;
        String* key = NULL;

        switch (unit) {
            case 'W': key = input_create_string(input, "weeks"); break;
            case 'D': key = input_create_string(input, "days"); break;
            case 'H': key = input_create_string(input, "hours"); break;
            case 'M':
                if (in_time_part) {
                    key = input_create_string(input, "minutes");
                } else {
                    key = input_create_string(input, "months");
                }
                break;
            case 'S': key = input_create_string(input, "seconds"); break;
            default: break;
        }

        if (key && num_str) {
            map_put(dur_map, key, {.item = s2it(num_str)}, input);
        }
    }

    return dur_map;
}

// Main iCalendar parsing function
void parse_ics(Input* input, const char* ics_string) {
    if (!ics_string || !input) {
        return;
    }

    // Initialize MarkBuilder for proper Lambda Item creation
    MarkBuilder builder(input);

    const char* ics = ics_string;

    // Initialize calendar map
    Map* calendar_map = map_pooled(input->pool);
    if (!calendar_map) {
        return;
    }

    // Initialize components list to store events, todos, etc.
    List* components_list = (List*)pool_calloc(input->pool, sizeof(List));
    if (components_list) {
        components_list->type_id = LMD_TYPE_LIST;
        components_list->length = 0;
        components_list->capacity = 0;
        components_list->items = NULL;
    }
    if (!components_list) {
        return;
    }

    // Initialize properties map to store calendar-level properties
    Map* properties_map = map_pooled(input->pool);
    if (!properties_map) {
        return;
    }

    Map* current_component = NULL;
    Map* current_component_props = NULL;
    String* current_component_type = NULL;
    bool in_calendar = false;

    // Parse iCalendar line by line
    while (*ics) {
        // Skip empty lines
        if (*ics == '\n' || *ics == '\r') {
            skip_to_newline(&ics);
            continue;
        }

        // Skip lines that start with whitespace if we're not in a calendar
        if (!in_calendar && is_folded_line(ics)) {
            skip_to_newline(&ics);
            continue;
        }

        // Parse property name
        String* property_name = parse_property_name(input, &builder, &ics);
        if (!property_name || !property_name->chars) {
            skip_to_newline(&ics);
            continue;
        }

        // Normalize property name to uppercase
        normalize_property_name(property_name->chars);

        // Parse property parameters
        Map* params_map = map_pooled(input->pool);
        if (params_map) {
            parse_property_parameters(input, &builder, &ics, params_map);
        }

        // Parse property value
        String* property_value = parse_property_value(input, &builder, &ics);
        if (!property_value || !property_value->chars) {
            continue;
        }

        // Handle calendar start and end
        if (strcmp(property_name->chars, "BEGIN") == 0) {
            if (strcasecmp(property_value->chars, "VCALENDAR") == 0) {
                in_calendar = true;
            } else if (in_calendar) {
                // Start of a component (VEVENT, VTODO, etc.)
                current_component = map_pooled(input->pool);
                current_component_props = map_pooled(input->pool);
                current_component_type = input_create_string(input, property_value->chars);

                // Verify all components were created successfully
                if (!current_component || !current_component_props || !current_component_type) {
                    // Clean up partially created component
                    current_component = NULL;
                    current_component_props = NULL;
                    current_component_type = NULL;
                } else {
                    // Store component type
                    String* type_key = input_create_string(input, "type");
                    if (type_key) {
                        map_put(current_component, type_key, {.item = s2it(current_component_type)}, input);
                    }
                }
            }
            continue;
        }

        if (strcmp(property_name->chars, "END") == 0) {
            if (strcasecmp(property_value->chars, "VCALENDAR") == 0) {
                in_calendar = false;
            } else if (current_component && current_component_type &&
                      strcasecmp(property_value->chars, current_component_type->chars) == 0) {
                // End of current component
                if (current_component_props) {
                    String* props_key = input_create_string(input, "properties");
                    Item props_value = {.item = (uint64_t)current_component_props};
                    map_put(current_component, props_key, props_value, input);
                }

                // Add component to list
                Item component_item = {.item = (uint64_t)current_component};
                list_push(components_list, component_item);

                current_component = NULL;
                current_component_props = NULL;
                current_component_type = NULL;
            }
            continue;
        }

        if (!in_calendar) continue;

        // Store property based on current context
        Item prop_value = {.item = s2it(property_value)};

        if (current_component && current_component_props) {
            // We're inside a component, store in component properties
            map_put(current_component_props, property_name, prop_value, input);

            // Handle common component properties with special processing
            if (strcmp(property_name->chars, "SUMMARY") == 0) {
                String* summary_key = input_create_string(input, "summary");
                map_put(current_component, summary_key, prop_value, input);
            }
            else if (strcmp(property_name->chars, "DESCRIPTION") == 0) {
                String* desc_key = input_create_string(input, "description");
                map_put(current_component, desc_key, prop_value, input);
            }
            else if (strcmp(property_name->chars, "DTSTART") == 0) {
                String* start_key = input_create_string(input, "start_time");
                Map* dt_struct = parse_datetime(input, property_value->chars);
                if (dt_struct) {
                    Item dt_value = {.item = (uint64_t)dt_struct};
                    map_put(current_component, start_key, dt_value, input);
                } else {
                    map_put(current_component, start_key, prop_value, input);
                }
            }
            else if (strcmp(property_name->chars, "DTEND") == 0) {
                String* end_key = input_create_string(input, "end_time");
                Map* dt_struct = parse_datetime(input, property_value->chars);
                if (dt_struct) {
                    Item dt_value = {.item = (uint64_t)dt_struct};
                    map_put(current_component, end_key, dt_value, input);
                } else {
                    map_put(current_component, end_key, prop_value, input);
                }
            }
            else if (strcmp(property_name->chars, "DURATION") == 0) {
                String* duration_key = input_create_string(input, "duration");
                Map* dur_struct = parse_duration(input, property_value->chars);
                if (dur_struct) {
                    Item dur_value = {.item = (uint64_t)dur_struct};
                    map_put(current_component, duration_key, dur_value, input);
                } else {
                    map_put(current_component, duration_key, prop_value, input);
                }
            }
            else if (strcmp(property_name->chars, "LOCATION") == 0) {
                String* location_key = input_create_string(input, "location");
                map_put(current_component, location_key, prop_value, input);
            }
            else if (strcmp(property_name->chars, "STATUS") == 0) {
                String* status_key = input_create_string(input, "status");
                map_put(current_component, status_key, prop_value, input);
            }
            else if (strcmp(property_name->chars, "PRIORITY") == 0) {
                String* priority_key = input_create_string(input, "priority");
                map_put(current_component, priority_key, prop_value, input);
            }
            else if (strcmp(property_name->chars, "ORGANIZER") == 0) {
                String* organizer_key = input_create_string(input, "organizer");
                map_put(current_component, organizer_key, prop_value, input);
            }
            else if (strcmp(property_name->chars, "ATTENDEE") == 0) {
                String* attendee_key = input_create_string(input, "attendee");
                map_put(current_component, attendee_key, prop_value, input);
            }
            else if (strcmp(property_name->chars, "UID") == 0) {
                String* uid_key = input_create_string(input, "uid");
                map_put(current_component, uid_key, prop_value, input);
            }
        } else {
            // Calendar-level property
            map_put(properties_map, property_name, prop_value, input);

            // Handle common calendar properties with special processing
            if (strcmp(property_name->chars, "VERSION") == 0) {
                String* version_key = input_create_string(input, "version");
                map_put(calendar_map, version_key, prop_value, input);
            }
            else if (strcmp(property_name->chars, "PRODID") == 0) {
                String* prodid_key = input_create_string(input, "product_id");
                map_put(calendar_map, prodid_key, prop_value, input);
            }
            else if (strcmp(property_name->chars, "CALSCALE") == 0) {
                String* scale_key = input_create_string(input, "calendar_scale");
                map_put(calendar_map, scale_key, prop_value, input);
            }
            else if (strcmp(property_name->chars, "METHOD") == 0) {
                String* method_key = input_create_string(input, "method");
                map_put(calendar_map, method_key, prop_value, input);
            }
        }
    }

    // Store components list in calendar
    String* components_key = input_create_string(input, "components");
    Item components_value = {.item = (uint64_t)components_list};
    map_put(calendar_map, components_key, components_value, input);

    // Store properties map in calendar
    String* properties_key = input_create_string(input, "properties");
    Item properties_value = {.item = (uint64_t)properties_map};
    map_put(calendar_map, properties_key, properties_value, input);

    // Set the calendar map as the root of the input
    input->root = {.item = (uint64_t)calendar_map};
}
