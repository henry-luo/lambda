#include "input.hpp"
#include "../mark_builder.hpp"
#include "input-context.hpp"
#include "source_tracker.hpp"
#include "../../lib/str.h"
#include <ctype.h>

using namespace lambda;

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
static String* parse_property_name(InputContext& ctx, const char **ics) {
    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);  // Reset buffer for reuse

    while (**ics && **ics != ':' && **ics != ';' && **ics != '\n' && **ics != '\r') {
        stringbuf_append_char(sb, **ics);
        (*ics)++;
    }

    if (sb->length > 0) {
        String* result = builder.createString(sb->str->chars, sb->length);
        return result;
    }
    return NULL;
}

// Helper function to parse property parameters (between ; and :)
static void parse_property_parameters(InputContext& ctx, const char **ics, Map* params_map) {
    MarkBuilder& builder = ctx.builder;

    while (**ics == ';') {
        (*ics)++; // skip ';'

        // Parse parameter name
        StringBuf* sb = ctx.sb;
        stringbuf_reset(sb);

        while (**ics && **ics != '=' && **ics != ':' && **ics != '\n' && **ics != '\r') {
            stringbuf_append_char(sb, toupper(**ics));
            (*ics)++;
        }

        if (sb->length == 0) continue;

        String* param_name = builder.createName(sb->str->chars, sb->length);
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

            if (sb->length > 0) {
                param_value = stringbuf_to_string(sb);
            }
        }

        if (param_value) {
            Item value = {.item = s2it(param_value)};
            builder.putToMap(params_map, param_name, value);
        }
    }
}

// Helper function to parse property value (after the colon, handling folded lines)
static String* parse_property_value(InputContext& ctx, const char **ics) {
    if (**ics != ':') return NULL;

    (*ics)++; // skip ':'

    MarkBuilder& builder = ctx.builder;
    StringBuf* sb = ctx.sb;
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

    if (sb->length > 0) {
        return builder.createString(sb->str->chars, sb->length);
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
static Map* parse_datetime(InputContext& ctx, const char* value) {
    Input* input = ctx.input();
    MarkBuilder& builder = ctx.builder;

    if (!input || !value) return NULL;

    Map* dt_map = map_pooled(input->pool);
    if (!dt_map) return NULL;

    // Parse various date-time formats
    // Format: YYYYMMDD, YYYYMMDDTHHMMSS, YYYYMMDDTHHMMSSZ
    const char* ptr = value;
    StringBuf* sb = ctx.sb;

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
        if (sb->length > 0) {
            String* year_str = stringbuf_to_string(sb);
            String* year_key = builder.createName("year");
            ctx.builder.putToMap(dt_map, year_key, {.item = s2it(year_str)});
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
        if (sb->length > 0) {
            String* month_str = stringbuf_to_string(sb);
            String* month_key = builder.createName("month");
            ctx.builder.putToMap(dt_map, month_key, {.item = s2it(month_str)});
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
        if (sb->length > 0) {
            String* day_str = stringbuf_to_string(sb);
            String* day_key = builder.createName("day");
            ctx.builder.putToMap(dt_map, day_key, {.item = s2it(day_str)});
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
            if (sb->length > 0) {
                String* hour_str = stringbuf_to_string(sb);
                String* hour_key = builder.createName("hour");
                ctx.builder.putToMap(dt_map, hour_key, {.item = s2it(hour_str)});
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
            if (sb->length > 0) {
                String* minute_str = stringbuf_to_string(sb);
                String* minute_key = builder.createName("minute");
                ctx.builder.putToMap(dt_map, minute_key, {.item = s2it(minute_str)});
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
            if (sb->length > 0) {
                String* second_str = stringbuf_to_string(sb);
                String* second_key = builder.createName("second");
                ctx.builder.putToMap(dt_map, second_key, {.item = s2it(second_str)});
            }

            // Check for timezone (Z for UTC)
            if (*ptr == 'Z') {
                String* tz_key = builder.createName("timezone");
                String* tz_value = builder.createString("UTC");
                ctx.builder.putToMap(dt_map, tz_key, {.item = s2it(tz_value)});
            }
        }
    }

    return dt_map;
}

// Helper function to parse duration values
static Map* parse_duration(InputContext& ctx, const char* value) {
    Input* input = ctx.input();
    MarkBuilder& builder = ctx.builder;

    if (!input || !value) return NULL;

    Map* dur_map = map_pooled(input->pool);
    if (!dur_map) return NULL;

    // Parse duration format: P[n]DT[n]H[n]M[n]S or P[n]W
    const char* ptr = value;

    if (*ptr != 'P') return dur_map; // Invalid duration format
    ptr++; // skip 'P'

    StringBuf* sb = ctx.sb;
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

        if (sb->length == 0) {
            ptr++; // skip the unit character
            continue;
        }

        String* num_str = stringbuf_to_string(sb);

        // Parse unit
        char unit = *ptr++;
        String* key = NULL;

        switch (unit) {
            case 'W': key = builder.createName("weeks"); break;
            case 'D': key = builder.createName("days"); break;
            case 'H': key = builder.createName("hours"); break;
            case 'M':
                if (in_time_part) {
                    key = builder.createName("minutes");
                } else {
                    key = builder.createName("months");
                }
                break;
            case 'S': key = builder.createName("seconds"); break;
            default: break;
        }

        if (key && num_str) {
            ctx.builder.putToMap(dur_map, key, {.item = s2it(num_str)});
        }
    }

    return dur_map;
}

// Main iCalendar parsing function
void parse_ics(Input* input, const char* ics_string) {
    if (!ics_string || !input) {
        return;
    }

    // create error tracking context with source tracking
    InputContext ctx(input, ics_string, strlen(ics_string));
    MarkBuilder& builder = ctx.builder;

    const char* ics = ics_string;

    // Initialize calendar map
    Map* calendar_map = map_pooled(input->pool);
    if (!calendar_map) {
        ctx.addError(ctx.tracker.location(), "Failed to allocate memory for calendar map");
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
        ctx.addError(ctx.tracker.location(), "Failed to allocate memory for components list");
        return;
    }

    // Initialize properties map to store calendar-level properties
    Map* properties_map = map_pooled(input->pool);
    if (!properties_map) {
        ctx.addError(ctx.tracker.location(), "Failed to allocate memory for properties map");
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
        String* property_name = parse_property_name(ctx, &ics);
        if (!property_name || !property_name->chars) {
            skip_to_newline(&ics);
            continue;
        }

        // Normalize property name to uppercase
        normalize_property_name(property_name->chars);

        // Parse property parameters
        Map* params_map = map_pooled(input->pool);
        if (params_map) {
            parse_property_parameters(ctx, &ics, params_map);
        }

        // Parse property value
        String* property_value = parse_property_value(ctx, &ics);
        if (!property_value || !property_value->chars) {
            continue;
        }

        // Handle calendar start and end
        if (strcmp(property_name->chars, "BEGIN") == 0) {
            if (str_ieq_const(property_value->chars, strlen(property_value->chars), "VCALENDAR")) {
                in_calendar = true;
            } else if (in_calendar) {
                // Start of a component (VEVENT, VTODO, etc.)
                current_component = map_pooled(input->pool);
                current_component_props = map_pooled(input->pool);
                current_component_type = builder.createString(property_value->chars);

                // Verify all components were created successfully
                if (!current_component || !current_component_props || !current_component_type) {
                    // Clean up partially created component
                    current_component = NULL;
                    current_component_props = NULL;
                    current_component_type = NULL;
                } else {
                    // Store component type
                    String* type_key = builder.createName("type");
                    if (type_key) {
                        ctx.builder.putToMap(current_component, type_key, {.item = s2it(current_component_type)});
                    }
                }
            }
            continue;
        }

        if (strcmp(property_name->chars, "END") == 0) {
            if (str_ieq_const(property_value->chars, strlen(property_value->chars), "VCALENDAR")) {
                in_calendar = false;
            } else if (current_component && current_component_type &&
                      str_ieq(property_value->chars, strlen(property_value->chars), current_component_type->chars, strlen(current_component_type->chars))) {
                // End of current component
                if (current_component_props) {
                    String* props_key = builder.createName("properties");
                    Item props_value = {.item = (uint64_t)current_component_props};
                    ctx.builder.putToMap(current_component, props_key, props_value);
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
            ctx.builder.putToMap(current_component_props, property_name, prop_value);

            // Handle common component properties with special processing
            if (strcmp(property_name->chars, "SUMMARY") == 0) {
                String* summary_key = builder.createName("summary");
                ctx.builder.putToMap(current_component, summary_key, prop_value);
            }
            else if (strcmp(property_name->chars, "DESCRIPTION") == 0) {
                String* desc_key = builder.createName("description");
                ctx.builder.putToMap(current_component, desc_key, prop_value);
            }
            else if (strcmp(property_name->chars, "DTSTART") == 0) {
                String* start_key = builder.createName("start_time");
                Map* dt_struct = parse_datetime(ctx, property_value->chars);
                if (dt_struct) {
                    Item dt_value = {.item = (uint64_t)dt_struct};
                    ctx.builder.putToMap(current_component, start_key, dt_value);
                } else {
                    ctx.builder.putToMap(current_component, start_key, prop_value);
                }
            }
            else if (strcmp(property_name->chars, "DTEND") == 0) {
                String* end_key = builder.createName("end_time");
                Map* dt_struct = parse_datetime(ctx, property_value->chars);
                if (dt_struct) {
                    Item dt_value = {.item = (uint64_t)dt_struct};
                    ctx.builder.putToMap(current_component, end_key, dt_value);
                } else {
                    ctx.builder.putToMap(current_component, end_key, prop_value);
                }
            }
            else if (strcmp(property_name->chars, "DURATION") == 0) {
                String* duration_key = builder.createName("duration");
                Map* dur_struct = parse_duration(ctx, property_value->chars);
                if (dur_struct) {
                    Item dur_value = {.item = (uint64_t)dur_struct};
                    ctx.builder.putToMap(current_component, duration_key, dur_value);
                } else {
                    ctx.builder.putToMap(current_component, duration_key, prop_value);
                }
            }
            else if (strcmp(property_name->chars, "LOCATION") == 0) {
                String* location_key = builder.createName("location");
                ctx.builder.putToMap(current_component, location_key, prop_value);
            }
            else if (strcmp(property_name->chars, "STATUS") == 0) {
                String* status_key = builder.createName("status");
                ctx.builder.putToMap(current_component, status_key, prop_value);
            }
            else if (strcmp(property_name->chars, "PRIORITY") == 0) {
                String* priority_key = builder.createName("priority");
                ctx.builder.putToMap(current_component, priority_key, prop_value);
            }
            else if (strcmp(property_name->chars, "ORGANIZER") == 0) {
                String* organizer_key = builder.createName("organizer");
                ctx.builder.putToMap(current_component, organizer_key, prop_value);
            }
            else if (strcmp(property_name->chars, "ATTENDEE") == 0) {
                String* attendee_key = builder.createName("attendee");
                ctx.builder.putToMap(current_component, attendee_key, prop_value);
            }
            else if (strcmp(property_name->chars, "UID") == 0) {
                String* uid_key = builder.createName("uid");
                ctx.builder.putToMap(current_component, uid_key, prop_value);
            }
        } else {
            // Calendar-level property
            ctx.builder.putToMap(properties_map, property_name, prop_value);

            // Handle common calendar properties with special processing
            if (strcmp(property_name->chars, "VERSION") == 0) {
                String* version_key = builder.createName("version");
                ctx.builder.putToMap(calendar_map, version_key, prop_value);
            }
            else if (strcmp(property_name->chars, "PRODID") == 0) {
                String* prodid_key = builder.createName("product_id");
                ctx.builder.putToMap(calendar_map, prodid_key, prop_value);
            }
            else if (strcmp(property_name->chars, "CALSCALE") == 0) {
                String* scale_key = builder.createName("calendar_scale");
                ctx.builder.putToMap(calendar_map, scale_key, prop_value);
            }
            else if (strcmp(property_name->chars, "METHOD") == 0) {
                String* method_key = builder.createName("method");
                ctx.builder.putToMap(calendar_map, method_key, prop_value);
            }
        }
    }

    // Store components list in calendar
    String* components_key = builder.createName("components");
    Item components_value = {.item = (uint64_t)components_list};
    ctx.builder.putToMap(calendar_map, components_key, components_value);

    // Store properties map in calendar
    String* properties_key = builder.createName("properties");
    Item properties_value = {.item = (uint64_t)properties_map};
    ctx.builder.putToMap(calendar_map, properties_key, properties_value);

    // Set the calendar map as the root of the input
    input->root = {.item = (uint64_t)calendar_map};

    if (ctx.hasErrors()) {
        ctx.logErrors();
    }
}
