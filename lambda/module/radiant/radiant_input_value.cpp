#include "radiant_input_value.hpp"
#include "radiant_host_api.hpp"
#include "../../input/css/dom_element.hpp"
#include "../../input/css/css_style.hpp"
#include "../../../lib/arraylist.h"
#include "../../../lib/mem.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct RadiantInputStateEntry {
    DomElement* element;
    char* value;
    Item files;
    bool files_rooted;
} RadiantInputStateEntry;

typedef struct RadiantInputState {
    DomDocument* document;
    ArrayList* entries;
} RadiantInputState;

static bool riv_type_is(const char* type, const char* name) {
    return type && strcasecmp(type, name) == 0;
}

extern "C" RadiantInputValueKind radiant_input_value_kind(const char* type) {
    if (!type || !type[0] || riv_type_is(type, "text") ||
        riv_type_is(type, "search") || riv_type_is(type, "tel") ||
        riv_type_is(type, "url") || riv_type_is(type, "email") ||
        riv_type_is(type, "password") || riv_type_is(type, "hidden") ||
        riv_type_is(type, "button") || riv_type_is(type, "submit") ||
        riv_type_is(type, "reset") || riv_type_is(type, "image") ||
        riv_type_is(type, "checkbox") || riv_type_is(type, "radio")) {
        return RADIANT_INPUT_VALUE_TEXT;
    }
    if (riv_type_is(type, "number")) return RADIANT_INPUT_VALUE_NUMBER;
    if (riv_type_is(type, "range")) return RADIANT_INPUT_VALUE_RANGE;
    if (riv_type_is(type, "date")) return RADIANT_INPUT_VALUE_DATE;
    if (riv_type_is(type, "month")) return RADIANT_INPUT_VALUE_MONTH;
    if (riv_type_is(type, "week")) return RADIANT_INPUT_VALUE_WEEK;
    if (riv_type_is(type, "time")) return RADIANT_INPUT_VALUE_TIME;
    if (riv_type_is(type, "datetime-local")) return RADIANT_INPUT_VALUE_DATETIME_LOCAL;
    if (riv_type_is(type, "color")) return RADIANT_INPUT_VALUE_COLOR;
    if (riv_type_is(type, "file")) return RADIANT_INPUT_VALUE_FILE;
    return RADIANT_INPUT_VALUE_UNSUPPORTED;
}

extern "C" const char* radiant_input_type_normalize(const char* type,
                                                       char* output,
                                                       size_t output_size) {
    if (!output || output_size == 0) return "text";
    size_t length = type ? strlen(type) : 0;
    if (length == 0 || length >= output_size) {
        snprintf(output, output_size, "text");
        return output;
    }
    for (size_t i = 0; i < length; i++) {
        output[i] = (char)tolower((unsigned char)type[i]);
    }
    output[length] = '\0';
    if (radiant_input_value_kind(output) == RADIANT_INPUT_VALUE_UNSUPPORTED) {
        snprintf(output, output_size, "text");
    }
    return output;
}

static bool riv_copy(const char* value, char* output, size_t output_size) {
    if (!output || output_size == 0) return false;
    size_t length = value ? strlen(value) : 0;
    if (length >= output_size) {
        output[0] = '\0';
        return false;
    }
    memcpy(output, value ? value : "", length + 1);
    return true;
}

static bool riv_parse_digits(const char** cursor, int count, int* output) {
    const char* value = *cursor;
    int result = 0;
    for (int i = 0; i < count; i++) {
        if (!isdigit((unsigned char)value[i])) return false;
        result = result * 10 + (value[i] - '0');
    }
    *cursor += count;
    *output = result;
    return true;
}

static bool riv_is_leap(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static int riv_days_in_month(int year, int month) {
    static const int days[12] = {31, 28, 31, 30, 31, 30,
                                 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    return days[month - 1] + ((month == 2 && riv_is_leap(year)) ? 1 : 0);
}

// Gregorian civil-day conversion keeps every date-like IDL path on one epoch.
static int64_t riv_days_from_civil(int year, int month, int day) {
    year -= month <= 2;
    int era = (year >= 0 ? year : year - 399) / 400;
    unsigned yoe = (unsigned)(year - era * 400);
    unsigned doy = (153U * (unsigned)(month + (month > 2 ? -3 : 9)) + 2U) / 5U +
                   (unsigned)day - 1U;
    unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return (int64_t)era * 146097LL + (int64_t)doe - 719468LL;
}

static void riv_civil_from_days(int64_t days, int* year, int* month, int* day) {
    days += 719468LL;
    int64_t era = (days >= 0 ? days : days - 146096LL) / 146097LL;
    unsigned doe = (unsigned)(days - era * 146097LL);
    unsigned yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    int y = (int)yoe + (int)era * 400;
    unsigned doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    unsigned mp = (5U * doy + 2U) / 153U;
    unsigned d = doy - (153U * mp + 2U) / 5U + 1U;
    unsigned m = mp + (mp < 10U ? 3U : (unsigned)-9);
    y += m <= 2U;
    *year = y;
    *month = (int)m;
    *day = (int)d;
}

static int riv_iso_weekday(int64_t days) {
    int weekday = (int)((days + 3LL) % 7LL);
    if (weekday < 0) weekday += 7;
    return weekday + 1;
}

static int riv_iso_weeks_in_year(int year) {
    int jan1 = riv_iso_weekday(riv_days_from_civil(year, 1, 1));
    return jan1 == 4 || (jan1 == 3 && riv_is_leap(year)) ? 53 : 52;
}

static bool riv_parse_year(const char** cursor, int* year) {
    const char* start = *cursor;
    int digits = 0;
    int value = 0;
    while (isdigit((unsigned char)start[digits])) {
        if (value > 1000000) return false;
        value = value * 10 + start[digits] - '0';
        digits++;
    }
    if (digits < 4 || value <= 0) return false;
    *cursor = start + digits;
    *year = value;
    return true;
}

static bool riv_parse_date(const char* value, int* year, int* month, int* day) {
    if (!value) return false;
    const char* cursor = value;
    if (!riv_parse_year(&cursor, year) || *cursor++ != '-' ||
        !riv_parse_digits(&cursor, 2, month) || *cursor++ != '-' ||
        !riv_parse_digits(&cursor, 2, day) || *cursor != '\0') return false;
    return *month >= 1 && *month <= 12 && *day >= 1 &&
           *day <= riv_days_in_month(*year, *month);
}

static bool riv_parse_month(const char* value, int* year, int* month) {
    if (!value) return false;
    const char* cursor = value;
    if (!riv_parse_year(&cursor, year) || *cursor++ != '-' ||
        !riv_parse_digits(&cursor, 2, month) || *cursor != '\0') return false;
    return *month >= 1 && *month <= 12;
}

static bool riv_parse_week(const char* value, int* year, int* week) {
    if (!value) return false;
    const char* cursor = value;
    if (!riv_parse_year(&cursor, year) || *cursor++ != '-' || *cursor++ != 'W' ||
        !riv_parse_digits(&cursor, 2, week) || *cursor != '\0') return false;
    return *week >= 1 && *week <= riv_iso_weeks_in_year(*year);
}

static bool riv_parse_time(const char* value, int* hour, int* minute,
                           int* second, int* millisecond) {
    if (!value) return false;
    const char* cursor = value;
    *second = 0;
    *millisecond = 0;
    if (!riv_parse_digits(&cursor, 2, hour) || *cursor++ != ':' ||
        !riv_parse_digits(&cursor, 2, minute)) return false;
    if (*cursor == ':') {
        cursor++;
        if (!riv_parse_digits(&cursor, 2, second)) return false;
        if (*cursor == '.') {
            cursor++;
            int digits = 0;
            int fraction = 0;
            while (digits < 3 && isdigit((unsigned char)*cursor)) {
                fraction = fraction * 10 + (*cursor++ - '0');
                digits++;
            }
            if (digits == 0 || isdigit((unsigned char)*cursor)) return false;
            *millisecond = digits == 1 ? fraction * 100 :
                           digits == 2 ? fraction * 10 : fraction;
        }
    }
    return *cursor == '\0' && *hour >= 0 && *hour <= 23 &&
           *minute >= 0 && *minute <= 59 && *second >= 0 && *second <= 59;
}

static bool riv_parse_datetime(const char* value, int* year, int* month, int* day,
                               int* hour, int* minute, int* second,
                               int* millisecond) {
    if (!value) return false;
    const char* separator = strchr(value, 'T');
    if (!separator) separator = strchr(value, ' ');
    if (!separator || separator == value || separator[1] == '\0' ||
        strchr(separator + 1, 'T') || strchr(separator + 1, ' ')) return false;
    size_t date_length = (size_t)(separator - value);
    if (date_length >= 32) return false;
    char date[32];
    memcpy(date, value, date_length);
    date[date_length] = '\0';
    return riv_parse_date(date, year, month, day) &&
           riv_parse_time(separator + 1, hour, minute, second, millisecond);
}

static bool riv_parse_finite_number(const char* value, double* output) {
    if (!value || !value[0] || isspace((unsigned char)value[0])) return false;
    char* end = nullptr;
    double number = strtod(value, &end);
    if (end == value || *end != '\0' || !isfinite(number)) return false;
    *output = number;
    return true;
}

extern "C" bool radiant_input_value_sanitize(const char* type, const char* value,
                                                char* output, size_t output_size) {
    RadiantInputValueKind kind = radiant_input_value_kind(type);
    const char* source = value ? value : "";
    int year, month, day, week, hour, minute, second, millis;
    double number;
    bool valid = true;
    switch (kind) {
        case RADIANT_INPUT_VALUE_NUMBER:
        case RADIANT_INPUT_VALUE_RANGE:
            valid = source[0] == '\0' || riv_parse_finite_number(source, &number);
            break;
        case RADIANT_INPUT_VALUE_DATE:
            valid = source[0] == '\0' || riv_parse_date(source, &year, &month, &day);
            break;
        case RADIANT_INPUT_VALUE_MONTH:
            valid = source[0] == '\0' || riv_parse_month(source, &year, &month);
            break;
        case RADIANT_INPUT_VALUE_WEEK:
            valid = source[0] == '\0' || riv_parse_week(source, &year, &week);
            break;
        case RADIANT_INPUT_VALUE_TIME:
            valid = source[0] == '\0' ||
                riv_parse_time(source, &hour, &minute, &second, &millis);
            break;
        case RADIANT_INPUT_VALUE_DATETIME_LOCAL:
            valid = source[0] == '\0' || riv_parse_datetime(source, &year, &month,
                &day, &hour, &minute, &second, &millis);
            if (valid && source[0] && strchr(source, ' ')) {
                if (!riv_copy(source, output, output_size)) return false;
                char* space = strchr(output, ' ');
                if (space) *space = 'T';
                return true;
            }
            break;
        case RADIANT_INPUT_VALUE_COLOR: {
            CssColor color = {};
            valid = strlen(source) == 7 && source[0] == '#' &&
                    css_parse_color(source, &color) && color.a == 255;
            if (!valid) return riv_copy("#000000", output, output_size);
            if (!riv_copy(source, output, output_size)) return false;
            for (size_t i = 1; output[i]; i++) {
                output[i] = (char)tolower((unsigned char)output[i]);
            }
            return true;
        }
        case RADIANT_INPUT_VALUE_FILE:
            return riv_copy("", output, output_size);
        default:
            break;
    }
    return riv_copy(valid ? source : "", output, output_size);
}

static int64_t riv_week_monday_days(int year, int week) {
    int64_t jan4 = riv_days_from_civil(year, 1, 4);
    return jan4 - (riv_iso_weekday(jan4) - 1) + (int64_t)(week - 1) * 7LL;
}

extern "C" bool radiant_input_value_as_number(const char* type, const char* value,
                                                 double* output) {
    if (!output || !value || !value[0]) return false;
    int year, month, day, week, hour, minute, second, millis;
    switch (radiant_input_value_kind(type)) {
        case RADIANT_INPUT_VALUE_NUMBER:
        case RADIANT_INPUT_VALUE_RANGE:
            return riv_parse_finite_number(value, output);
        case RADIANT_INPUT_VALUE_DATE:
            if (!riv_parse_date(value, &year, &month, &day)) return false;
            *output = (double)riv_days_from_civil(year, month, day) * 86400000.0;
            return true;
        case RADIANT_INPUT_VALUE_MONTH:
            if (!riv_parse_month(value, &year, &month)) return false;
            *output = (double)riv_days_from_civil(year, month, 1) * 86400000.0;
            return true;
        case RADIANT_INPUT_VALUE_WEEK:
            if (!riv_parse_week(value, &year, &week)) return false;
            *output = (double)riv_week_monday_days(year, week) * 86400000.0;
            return true;
        case RADIANT_INPUT_VALUE_TIME:
            if (!riv_parse_time(value, &hour, &minute, &second, &millis)) return false;
            *output = (double)hour * 3600000.0 + (double)minute * 60000.0 +
                      (double)second * 1000.0 + millis;
            return true;
        case RADIANT_INPUT_VALUE_DATETIME_LOCAL:
            if (!riv_parse_datetime(value, &year, &month, &day, &hour, &minute,
                                    &second, &millis)) return false;
            *output = (double)riv_days_from_civil(year, month, day) * 86400000.0 +
                      (double)hour * 3600000.0 + (double)minute * 60000.0 +
                      (double)second * 1000.0 + millis;
            return true;
        default:
            return false;
    }
}

static bool riv_format_year_date(int year, int month, int day,
                                 char* output, size_t output_size) {
    int count = snprintf(output, output_size, "%04d-%02d-%02d", year, month, day);
    return count > 0 && (size_t)count < output_size;
}

static void riv_iso_week_from_days(int64_t days, int* year, int* week) {
    int weekday = riv_iso_weekday(days);
    int y, month, day;
    riv_civil_from_days(days + (4 - weekday), &y, &month, &day);
    int64_t jan4 = riv_days_from_civil(y, 1, 4);
    int64_t week1 = jan4 - (riv_iso_weekday(jan4) - 1);
    *year = y;
    *week = (int)((days - week1) / 7LL) + 1;
}

extern "C" bool radiant_input_value_from_number(const char* type, double value,
                                                   char* output,
                                                   size_t output_size) {
    if (!output || output_size == 0 || !isfinite(value)) return false;
    RadiantInputValueKind kind = radiant_input_value_kind(type);
    int64_t integer = (int64_t)floor(value);
    int year, month, day, week;
    if (kind == RADIANT_INPUT_VALUE_NUMBER || kind == RADIANT_INPUT_VALUE_RANGE) {
        int count = snprintf(output, output_size, "%.15g", value);
        return count > 0 && (size_t)count < output_size;
    }
    if (kind == RADIANT_INPUT_VALUE_DATE || kind == RADIANT_INPUT_VALUE_MONTH ||
        kind == RADIANT_INPUT_VALUE_WEEK || kind == RADIANT_INPUT_VALUE_DATETIME_LOCAL) {
        int64_t days = (int64_t)floor(value / 86400000.0);
        int64_t in_day = integer - days * 86400000LL;
        if (in_day < 0) { days--; in_day += 86400000LL; }
        riv_civil_from_days(days, &year, &month, &day);
        if (year <= 0) return false;
        if (kind == RADIANT_INPUT_VALUE_DATE) {
            return riv_format_year_date(year, month, day, output, output_size);
        }
        if (kind == RADIANT_INPUT_VALUE_MONTH) {
            int count = snprintf(output, output_size, "%04d-%02d", year, month);
            return count > 0 && (size_t)count < output_size;
        }
        if (kind == RADIANT_INPUT_VALUE_WEEK) {
            riv_iso_week_from_days(days, &year, &week);
            int count = snprintf(output, output_size, "%04d-W%02d", year, week);
            return count > 0 && (size_t)count < output_size;
        }
        int hour = (int)(in_day / 3600000LL);
        int minute = (int)((in_day / 60000LL) % 60LL);
        int second = (int)((in_day / 1000LL) % 60LL);
        int millis = (int)(in_day % 1000LL);
        if (!riv_format_year_date(year, month, day, output, output_size)) return false;
        size_t used = strlen(output);
        int count = millis ? snprintf(output + used, output_size - used,
                    "T%02d:%02d:%02d.%03d", hour, minute, second, millis) :
                    second ? snprintf(output + used, output_size - used,
                    "T%02d:%02d:%02d", hour, minute, second) :
                    snprintf(output + used, output_size - used,
                    "T%02d:%02d", hour, minute);
        return count > 0 && (size_t)count < output_size - used;
    }
    if (kind == RADIANT_INPUT_VALUE_TIME) {
        if (integer < 0 || integer >= 86400000LL) return false;
        int hour = (int)(integer / 3600000LL);
        int minute = (int)((integer / 60000LL) % 60LL);
        int second = (int)((integer / 1000LL) % 60LL);
        int millis = (int)(integer % 1000LL);
        int count = millis ? snprintf(output, output_size, "%02d:%02d:%02d.%03d",
                    hour, minute, second, millis) :
                    second ? snprintf(output, output_size, "%02d:%02d:%02d",
                    hour, minute, second) :
                    snprintf(output, output_size, "%02d:%02d", hour, minute);
        return count > 0 && (size_t)count < output_size;
    }
    return false;
}

extern "C" bool radiant_input_value_as_date_supported(const char* type) {
    RadiantInputValueKind kind = radiant_input_value_kind(type);
    return kind == RADIANT_INPUT_VALUE_DATE || kind == RADIANT_INPUT_VALUE_MONTH ||
           kind == RADIANT_INPUT_VALUE_WEEK || kind == RADIANT_INPUT_VALUE_TIME;
}

static double riv_step_scale(RadiantInputValueKind kind) {
    if (kind == RADIANT_INPUT_VALUE_DATE) return 86400000.0;
    if (kind == RADIANT_INPUT_VALUE_WEEK) return 604800000.0;
    if (kind == RADIANT_INPUT_VALUE_TIME || kind == RADIANT_INPUT_VALUE_DATETIME_LOCAL)
        return 1000.0;
    return 1.0;
}

static double riv_default_step(RadiantInputValueKind kind) {
    return (kind == RADIANT_INPUT_VALUE_TIME ||
            kind == RADIANT_INPUT_VALUE_DATETIME_LOCAL) ? 60.0 : 1.0;
}

static double riv_step_base(const char* type, const char* min_value) {
    double base;
    if (min_value && radiant_input_value_as_number(type, min_value, &base)) return base;
    return radiant_input_value_kind(type) == RADIANT_INPUT_VALUE_WEEK
        ? -259200000.0 : 0.0;
}

static bool riv_step_number(const char* value, double default_value, double* output) {
    if (!value || !value[0]) { *output = default_value; return true; }
    if (strcasecmp(value, "any") == 0) return false;
    return riv_parse_finite_number(value, output) && *output > 0.0;
}

extern "C" void radiant_input_value_validate(const char* type, const char* value,
                                                const char* min_value,
                                                const char* max_value,
                                                const char* step_value,
                                                RadiantInputValidity* output) {
    if (!output) return;
    memset(output, 0, sizeof(*output));
    output->has_value = value && value[0];
    if (!output->has_value) return;
    double number;
    output->value_valid = radiant_input_value_as_number(type, value, &number);
    if (!output->value_valid) return;
    double bound;
    if (min_value && radiant_input_value_as_number(type, min_value, &bound))
        output->range_underflow = number < bound;
    if (max_value && radiant_input_value_as_number(type, max_value, &bound))
        output->range_overflow = number > bound;

    RadiantInputValueKind kind = radiant_input_value_kind(type);
    double step;
    if (!riv_step_number(step_value, riv_default_step(kind), &step)) return;
    if (kind == RADIANT_INPUT_VALUE_MONTH) {
        int year, month, base_year = 1970, base_month = 1;
        if (!riv_parse_month(value, &year, &month)) return;
        if (min_value) riv_parse_month(min_value, &base_year, &base_month);
        double distance = (double)((year - base_year) * 12 + month - base_month);
        double remainder = fmod(fabs(distance), step);
        output->step_mismatch = remainder > 1e-7 && step - remainder > 1e-7;
        return;
    }
    double scaled_step = step * riv_step_scale(kind);
    double remainder = fmod(fabs(number - riv_step_base(type, min_value)), scaled_step);
    output->step_mismatch = remainder > 1e-7 && scaled_step - remainder > 1e-7;
}

extern "C" bool radiant_input_value_step(const char* type, const char* value,
                                            const char* min_value,
                                            const char* max_value,
                                            const char* step_value, int count,
                                            char* output, size_t output_size) {
    RadiantInputValueKind kind = radiant_input_value_kind(type);
    double step;
    if (!riv_step_number(step_value, riv_default_step(kind), &step)) return false;
    if (kind == RADIANT_INPUT_VALUE_MONTH) {
        int year, month;
        if (!riv_parse_month(value, &year, &month)) {
            if (!min_value || !riv_parse_month(min_value, &year, &month)) {
                year = 1970; month = 1;
            }
        }
        int64_t index = (int64_t)year * 12LL + month - 1 +
                        (int64_t)count * (int64_t)step;
        year = (int)(index / 12LL);
        month = (int)(index % 12LL) + 1;
        if (month <= 0) { month += 12; year--; }
        int written = snprintf(output, output_size, "%04d-%02d", year, month);
        return written > 0 && (size_t)written < output_size;
    }
    double number;
    if (!radiant_input_value_as_number(type, value, &number)) {
        number = riv_step_base(type, min_value);
    }
    number += (double)count * step * riv_step_scale(kind);
    double bound;
    if (min_value && radiant_input_value_as_number(type, min_value, &bound) && number < bound)
        number = bound;
    if (max_value && radiant_input_value_as_number(type, max_value, &bound) && number > bound)
        number = bound;
    return radiant_input_value_from_number(type, number, output, output_size);
}

static void riv_entry_destroy(RadiantInputStateEntry* entry) {
    if (!entry) return;
    if (entry->files_rooted && radiant_host_api && radiant_host_api->gc) {
        radiant_host_api->gc->unregister_root(&entry->files.item);
    }
    mem_free(entry->value);
    mem_free(entry);
}

static void riv_state_destroy(void* data) {
    RadiantInputState* state = (RadiantInputState*)data;
    if (!state) return;
    if (state->entries) {
        for (int i = 0; i < state->entries->length; i++)
            riv_entry_destroy((RadiantInputStateEntry*)arraylist_get(state->entries, i));
        arraylist_free(state->entries);
    }
    mem_free(state);
}

static RadiantInputState* riv_state_get(DomDocument* document, bool create) {
    if (!document) return nullptr;
    for (DomDocumentResource* resource = document->resources; resource; resource = resource->next) {
        if (resource->destroy == riv_state_destroy) return (RadiantInputState*)resource->data;
    }
    if (!create) return nullptr;
    RadiantInputState* state = (RadiantInputState*)mem_calloc(
        1, sizeof(RadiantInputState), MEM_CAT_JS_RUNTIME);
    if (!state) return nullptr;
    state->document = document;
    state->entries = arraylist_new(8);
    if (!state->entries || !dom_document_add_resource(document, state, riv_state_destroy)) {
        if (state->entries) arraylist_free(state->entries);
        mem_free(state);
        return nullptr;
    }
    return state;
}

static RadiantInputStateEntry* riv_entry_get(DomElement* element, bool create) {
    if (!element) return nullptr;
    RadiantInputState* state = riv_state_get(element->doc, create);
    if (!state) return nullptr;
    for (int i = 0; i < state->entries->length; i++) {
        RadiantInputStateEntry* entry =
            (RadiantInputStateEntry*)arraylist_get(state->entries, i);
        if (entry && entry->element == element) return entry;
    }
    if (!create) return nullptr;
    RadiantInputStateEntry* entry = (RadiantInputStateEntry*)mem_calloc(
        1, sizeof(RadiantInputStateEntry), MEM_CAT_JS_RUNTIME);
    if (!entry) return nullptr;
    entry->element = element;
    const char* type = dom_element_get_attribute(element, "type");
    const char* initial = dom_element_get_attribute(element, "value");
    char sanitized[128];
    radiant_input_value_sanitize(type, initial ? initial : "", sanitized, sizeof(sanitized));
    entry->value = mem_strdup(sanitized, MEM_CAT_JS_RUNTIME);
    if (!arraylist_append(state->entries, entry)) {
        riv_entry_destroy(entry);
        return nullptr;
    }
    return entry;
}

extern "C" const char* radiant_input_live_value(DomElement* element) {
    RadiantInputStateEntry* entry = riv_entry_get(element, true);
    return entry && entry->value ? entry->value : "";
}

extern "C" bool radiant_input_set_live_value(DomElement* element, const char* value) {
    RadiantInputStateEntry* entry = riv_entry_get(element, true);
    if (!entry) return false;
    const char* type = dom_element_get_attribute(element, "type");
    char sanitized[128];
    if (!radiant_input_value_sanitize(type, value ? value : "", sanitized,
                                      sizeof(sanitized))) return false;
    char* replacement = mem_strdup(sanitized, MEM_CAT_JS_RUNTIME);
    if (!replacement) return false;
    mem_free(entry->value);
    entry->value = replacement;
    return true;
}

extern "C" void radiant_input_reset_live_value(DomElement* element) {
    if (!element) return;
    radiant_input_set_live_value(element,
        dom_element_get_attribute(element, "value"));
}

extern "C" void radiant_input_type_changed(DomElement* element) {
    RadiantInputStateEntry* entry = riv_entry_get(element, false);
    if (!entry) return;
    // Changing type re-runs the new state's sanitizer over the live value.
    radiant_input_set_live_value(element, entry->value ? entry->value : "");
}

extern "C" Item radiant_input_files(DomElement* element) {
    RadiantInputStateEntry* entry = riv_entry_get(element, true);
    return entry ? entry->files : ItemNull;
}

extern "C" void radiant_input_set_files(DomElement* element, Item files) {
    RadiantInputStateEntry* entry = riv_entry_get(element, true);
    if (!entry) return;
    if (entry->files_rooted && radiant_host_api && radiant_host_api->gc)
        radiant_host_api->gc->unregister_root(&entry->files.item);
    entry->files = files;
    entry->files_rooted = false;
    if (files.item != ITEM_NULL && radiant_host_api && radiant_host_api->gc) {
        radiant_host_api->gc->register_root(&entry->files.item);
        entry->files_rooted = true;
    }
}

extern "C" void radiant_input_reset_document(DomDocument* document) {
    RadiantInputState* state = riv_state_get(document, false);
    if (!state) return;
    for (int i = 0; i < state->entries->length; i++) {
        RadiantInputStateEntry* entry =
            (RadiantInputStateEntry*)arraylist_get(state->entries, i);
        if (entry) radiant_input_reset_live_value(entry->element);
    }
}
