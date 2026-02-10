#include "datetime.h"
#include "string.h"
#include "strbuf.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include "log.h"

/* Helper function to check if a year is a leap year */
static bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/* Helper function to get days in month */
static int days_in_month(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month == 2 && is_leap_year(year)) return 29;
    return days[month - 1];
}

DateTime* datetime_new(Pool* pool) {
    DateTime* dt = (DateTime*)pool_calloc(pool, sizeof(DateTime));
    if (!dt) return NULL;
    dt->precision = DATETIME_PRECISION_DATE_TIME;  // Full datetime by default
    dt->format_hint = DATETIME_FORMAT_ISO8601;
    return dt;
}

DateTime* datetime_now(Pool* pool) {
    time_t now = time(NULL);
    return datetime_from_unix(pool, (int64_t)now);
}

DateTime* datetime_from_unix(Pool* pool, int64_t unix_timestamp) {
    DateTime* dt = datetime_new(pool);
    if (!dt) return NULL;

    time_t timestamp = (time_t)unix_timestamp;
    struct tm* tm_utc = gmtime(&timestamp);
    if (!tm_utc) return NULL;

    /* Use the new macros for year/month setting */
    DATETIME_SET_YEAR_MONTH(dt, tm_utc->tm_year + 1900, tm_utc->tm_mon + 1);
    dt->day = tm_utc->tm_mday;
    dt->hour = tm_utc->tm_hour;
    dt->minute = tm_utc->tm_min;
    dt->second = tm_utc->tm_sec;
    dt->millisecond = 0;

    /* Set UTC timezone (offset 0) and precision */
    DATETIME_SET_TZ_OFFSET(dt, 0);
    dt->precision = DATETIME_PRECISION_DATE_TIME;  // Full datetime from unix timestamp
    dt->format_hint = DATETIME_FORMAT_ISO8601_UTC;
    log_debug("Created DateTime: year=%d, month=%d, day=%d, hour=%d, minute=%d, second=%d",
        DATETIME_GET_YEAR(dt), DATETIME_GET_MONTH(dt), dt->day, dt->hour, dt->minute, dt->second);
    return dt;
}

/* Helper to calculate days since Unix epoch (1970-01-01) */
static int64_t days_since_epoch(int year, int month, int day) {
    /* Days in each month (non-leap year) */
    static const int month_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

    int64_t days = 0;

    /* Add days for complete years */
    for (int y = 1970; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }

    /* Add days for complete months in the current year */
    days += month_days[month - 1];

    /* Add extra day if it's a leap year and we're past February */
    if (is_leap_year(year) && month > 2) {
        days++;
    }

    /* Add the day of month (subtract 1 because day is 1-based) */
    days += day - 1;

    return days;
}

int64_t datetime_to_unix(DateTime* dt) {
    if (!dt || !datetime_is_valid(dt)) return 0;

    int year = DATETIME_GET_YEAR(dt);
    int month = DATETIME_GET_MONTH(dt);
    int day = dt->day;

    /* Calculate days since Unix epoch */
    int64_t days = days_since_epoch(year, month, day);

    /* Convert to seconds and add time components */
    int64_t seconds = days * 86400; /* 24 * 60 * 60 */
    seconds += dt->hour * 3600;     /* 60 * 60 */
    seconds += dt->minute * 60;
    seconds += dt->second;

    /* Adjust for timezone offset if present */
    if (DATETIME_HAS_TIMEZONE(dt)) {
        int tz_offset = DATETIME_GET_TZ_OFFSET(dt);
        seconds -= tz_offset * 60; /* Convert minutes to seconds */
    }

    return seconds;
}

bool datetime_is_valid(DateTime* dt) {
    if (!dt) return false;

    int year = DATETIME_GET_YEAR(dt);
    int month = DATETIME_GET_MONTH(dt);

    if (year < DATETIME_MIN_YEAR || year > DATETIME_MAX_YEAR) return false;
    if (month < 1 || month > DATETIME_MAX_MONTH) return false;

    /* Allow day = 0 for partial dates (year-month only), but validate full dates */
    if (dt->day == 0) {
        /* Partial date is valid if precision indicates year-only or date-only */
        if (dt->precision != DATETIME_PRECISION_YEAR_ONLY &&
            dt->precision != DATETIME_PRECISION_DATE_ONLY) {
            return false;  /* day=0 not valid for time-based precisions */
        }
    } else if (dt->day < 1 || dt->day > days_in_month(year, month)) {
        return false;  /* Invalid day for full dates */
    }

    if (dt->hour > DATETIME_MAX_HOUR) return false;
    if (dt->minute > DATETIME_MAX_MINUTE) return false;
    if (dt->second > DATETIME_MAX_SECOND) return false;
    if (dt->millisecond > DATETIME_MAX_MILLIS) return false;

    /* Check timezone offset if present */
    if (DATETIME_HAS_TIMEZONE(dt)) {
        int tz_offset = DATETIME_GET_TZ_OFFSET(dt);
        if (tz_offset < DATETIME_MIN_TZ_OFFSET || tz_offset > DATETIME_MAX_TZ_OFFSET) return false;
    }

    return true;
}

/* Helper function to skip whitespace */
static void skip_whitespace(const char** str) {
    while (**str && isspace(**str)) (*str)++;
}

/* Helper function to parse integer with specific width */
static bool parse_int_small(const char** str, int width, int8_t* result) {
    if (!str || !*str || !result) return false;

    int temp = 0;
    for (int i = 0; i < width; i++) {
        if (!isdigit(**str)) return false;
        temp = temp * 10 + (**str - '0');
        (*str)++;
    }
    *result = (int8_t)temp;
    return true;
}

/* Helper function to parse integer with specific width for int32_t */
static bool parse_int(const char** str, int width, int32_t* result) {
    if (!str || !*str || !result) return false;

    *result = 0;
    for (int i = 0; i < width; i++) {
        if (!isdigit(**str)) return false;
        *result = *result * 10 + (**str - '0');
        (*str)++;
    }
    return true;
}

/* Helper function to parse integer with specific width for int16_t */
static bool parse_int_short(const char** str, int width, int16_t* result) {
    if (!str || !*str || !result) return false;

    int temp = 0;
    for (int i = 0; i < width; i++) {
        if (!isdigit(**str)) return false;
        temp = temp * 10 + (**str - '0');
        (*str)++;
    }
    *result = (int16_t)temp;
    return true;
}

/* Internal parsing function forward declarations */
static bool datetime_parse_internal(DateTime* dt, const char** ptr, DateTimeParseFormat format);
static bool datetime_parse_ics_internal(DateTime* dt, const char** ptr);

DateTime* datetime_parse_iso8601(Pool* pool, const char* iso_str) {
    if (!iso_str || !pool) return NULL;

    DateTime* dt = datetime_new(pool);
    if (!dt) return NULL;

    const char* ptr = iso_str;
    skip_whitespace(&ptr);

    if (!datetime_parse_internal(dt, &ptr, DATETIME_PARSE_ISO8601)) {
        return NULL;
    }

    if (!datetime_is_valid(dt)) {
        return NULL;
    }

    return dt;
}

/* Combined internal parser that handles both ISO8601 and Lambda formats
 * ISO8601 format: YYYY[-MM[-DD]][T|t|space]HH:MM:SS[.sss][Z|±HH:MM]
 * Lambda format: All ISO8601 features plus:
 *   - Negative years (e.g., -0001 for 1 BC)
 *   - Time-only parsing (HH:MM:SS without date)
 *   - More flexible whitespace and separator handling
 *   - Both T/t and space separators for datetime
 */
static bool datetime_parse_internal(DateTime* dt, const char** ptr, DateTimeParseFormat format) {
    if (!dt || !ptr || !*ptr) return false;

    const char* start = *ptr;
    skip_whitespace(ptr);

    /* Lambda format: Check for negative year first */
    bool negative_year = false;
    if (format == DATETIME_PARSE_LAMBDA && **ptr == '-') {
        negative_year = true;
        (*ptr)++;
        skip_whitespace(ptr);  /* Skip any additional whitespace after negative sign */
    }

    /* Lambda format: Try to parse as time-only first (starts with digit digit:) */
    if (format == DATETIME_PARSE_LAMBDA &&
        isdigit((*ptr)[0]) && isdigit((*ptr)[1]) && (*ptr)[2] == ':') {

        /* Time only format: HH:MM:SS.sss with optional timezone */
        int32_t hour, minute = 0, second = 0;

        if (!parse_int(ptr, 2, &hour)) return false;
        if (hour > DATETIME_MAX_HOUR) return false;
        dt->hour = hour;

        if (**ptr == ':') {
            (*ptr)++;
            if (!parse_int(ptr, 2, &minute)) return false;
            if (minute > DATETIME_MAX_MINUTE) return false;
            dt->minute = minute;

            if (**ptr == ':') {
                (*ptr)++;
                if (!parse_int(ptr, 2, &second)) return false;
                if (second > DATETIME_MAX_SECOND) return false;
                dt->second = second;
            }
        }

        /* Parse optional milliseconds */
        if (**ptr == '.') {
            (*ptr)++;
            int32_t millis;
            if (!parse_int(ptr, 3, &millis)) return false;
            if (millis > DATETIME_MAX_MILLIS) return false;
            dt->millisecond = millis;
        }

        /* Parse optional timezone */
        bool parsed_z_suffix = false;
        if (**ptr == 'z' || **ptr == 'Z') {
            (*ptr)++;
            DATETIME_SET_TZ_OFFSET(dt, 0);  /* UTC */
            parsed_z_suffix = true;
        } else if (**ptr == '+' || **ptr == '-') {
            bool tz_negative = (**ptr == '-');
            (*ptr)++;

            int32_t tz_hour, tz_minute = 0;
            if (!parse_int(ptr, 2, &tz_hour)) return false;
            if (**ptr == ':') {
                (*ptr)++;
                if (!parse_int(ptr, 2, &tz_minute)) return false;
            }

            int tz_offset = tz_hour * 60 + tz_minute;
            if (tz_negative) tz_offset = -tz_offset;
            if (tz_offset < DATETIME_MIN_TZ_OFFSET || tz_offset > DATETIME_MAX_TZ_OFFSET) return false;
            DATETIME_SET_TZ_OFFSET(dt, tz_offset);
        }

        /* Set default date values and time-only precision */
        DATETIME_SET_YEAR_MONTH(dt, 1970, 1);
        dt->day = 1;
        dt->precision = DATETIME_PRECISION_TIME_ONLY;
        dt->format_hint = parsed_z_suffix ? DATETIME_FORMAT_ISO8601_UTC : DATETIME_FORMAT_ISO8601;
        return true;
    }

    /* Parse year (required, 4 digits) */
    if (!isdigit(**ptr)) return false;
    int32_t year;
    if (!parse_int(ptr, 4, &year)) return false;
    if (negative_year) year = -year;
    if (year < DATETIME_MIN_YEAR || year > DATETIME_MAX_YEAR) return false;

    /* Default values - use 1 for month, 0 for day when not specified */
    int month = 1, day = 0;  /* Default: January for year-only, day=0 when not specified */
    int hour = 0, minute = 0, second = 0, millisecond = 0;
    uint8_t precision = DATETIME_PRECISION_YEAR_ONLY;  /* Start with year-only */

    /* Parse optional month */
    if (format == DATETIME_PARSE_LAMBDA) {
        skip_whitespace(ptr);  /* Lambda: Handle whitespace before month separator */
    }

    if (**ptr == '-') {
        (*ptr)++;
        if (!parse_int(ptr, 2, &month)) return false;
        if (month < 1 || month > DATETIME_MAX_MONTH) return false;
        precision = DATETIME_PRECISION_DATE_ONLY;  /* Now we have year-month */

        /* Parse optional day */
        if (**ptr == '-') {
            (*ptr)++;
            if (!parse_int(ptr, 2, &day)) return false;
            if (day < 1 || day > days_in_month(year, month)) return false;
            /* precision remains DATETIME_PRECISION_DATE_ONLY but now with full date */
        } else {
            /* Month specified but no day - keep day = 0 to indicate partial date */
        }
    }

    /* Parse optional time part */
    bool has_time_separator = false;
    if (format == DATETIME_PARSE_LAMBDA) {
        /* Lambda: supports 'T', 't', and space separators */
        has_time_separator = (**ptr == ' ' || **ptr == 'T' || **ptr == 't');
    } else {
        /* ISO8601: supports 'T', 't', and space separators */
        has_time_separator = (**ptr == 'T' || **ptr == 't' || **ptr == ' ');
    }

    if (has_time_separator) {
        /* Skip separator */
        if (**ptr == ' ') {
            if (format == DATETIME_PARSE_LAMBDA) {
                skip_whitespace(ptr);  /* Lambda: skip all whitespace */
            } else {
                (*ptr)++;  /* ISO8601: skip single space */
            }
        } else {
            (*ptr)++;  /* Skip T/t */
        }

        /* Update precision */
        if (precision == DATETIME_PRECISION_DATE_ONLY) {
            precision = DATETIME_PRECISION_DATE_TIME;  // Full datetime
        } else {
            precision = DATETIME_PRECISION_TIME_ONLY;  // Year + time becomes time only
        }

        /* Parse time: HH:MM:SS */
        if (!parse_int(ptr, 2, &hour)) return false;
        if (hour > DATETIME_MAX_HOUR) return false;

        if (**ptr == ':') {
            (*ptr)++;
            if (!parse_int(ptr, 2, &minute)) return false;
            if (minute > DATETIME_MAX_MINUTE) return false;

            if (**ptr == ':') {
                (*ptr)++;
                if (!parse_int(ptr, 2, &second)) return false;
                if (second > DATETIME_MAX_SECOND) return false;
            }
        }

        /* Parse optional milliseconds */
        if (**ptr == '.') {
            (*ptr)++;
            if (format == DATETIME_PARSE_LAMBDA) {
                /* Lambda: fixed 3-digit milliseconds */
                if (!parse_int(ptr, 3, &millisecond)) return false;
                if (millisecond > DATETIME_MAX_MILLIS) return false;
            } else {
                /* ISO8601: variable width milliseconds (normalize to 3 digits) */
                int millis_width = 0;
                millisecond = 0;
                while (isdigit(**ptr) && millis_width < 3) {
                    millisecond = millisecond * 10 + (**ptr - '0');
                    (*ptr)++;
                    millis_width++;
                }
                /* Normalize to 3 digits */
                while (millis_width < 3) {
                    millisecond *= 10;
                    millis_width++;
                }
            }
        }

        /* Parse timezone */
        if (format == DATETIME_PARSE_ISO8601) {
            skip_whitespace(ptr);  /* ISO8601: skip whitespace before timezone */
        }

        if (**ptr == 'Z' || **ptr == 'z') {
            DATETIME_SET_TZ_OFFSET(dt, 0);
            dt->format_hint = DATETIME_FORMAT_ISO8601_UTC;
            (*ptr)++;
        } else if (**ptr == '+' || **ptr == '-') {
            bool tz_negative = (**ptr == '-');
            (*ptr)++;

            int32_t tz_hour, tz_minute = 0;
            if (!parse_int(ptr, 2, &tz_hour)) return false;

            if (**ptr == ':') {
                (*ptr)++;
                if (!parse_int(ptr, 2, &tz_minute)) return false;
            }

            int tz_offset = tz_hour * 60 + tz_minute;
            if (tz_negative) tz_offset = -tz_offset;
            if (tz_offset < DATETIME_MIN_TZ_OFFSET || tz_offset > DATETIME_MAX_TZ_OFFSET) return false;
            DATETIME_SET_TZ_OFFSET(dt, tz_offset);
        } else {
            /* No timezone specified */
            DATETIME_CLEAR_TIMEZONE(dt);
        }
    } else {
        /* Date only, no time part - clear time fields */
        hour = 0;
        minute = 0;
        second = 0;
        millisecond = 0;
        DATETIME_CLEAR_TIMEZONE(dt);
    }

    /* Set all the parsed values */
    DATETIME_SET_YEAR_MONTH(dt, year, month);

    /* For year-only precision, set day to 1 to represent first day of year */
    if (precision == DATETIME_PRECISION_YEAR_ONLY) {
        dt->day = 1;
    } else {
        dt->day = day;
    }

    dt->hour = hour;
    dt->minute = minute;
    dt->second = second;
    dt->millisecond = millisecond;
    dt->precision = precision;

    if (dt->format_hint == 0) {  /* Not set to UTC format above */
        dt->format_hint = DATETIME_FORMAT_ISO8601;
    }
    return true;
}

DateTime* datetime_parse(Pool* pool, const char* str, DateTimeParseFormat format, char** end) {
    if (!str || !pool) {
        if (end) *end = (char*)str;
        return NULL;
    }

    DateTime* dt = datetime_new(pool);
    if (!dt) {
        if (end) *end = (char*)str;
        return NULL;
    }

    const char* ptr = str;
    skip_whitespace(&ptr);

    /* Parse based on format */
    switch (format) {
        case DATETIME_PARSE_ISO8601:
            if (!datetime_parse_internal(dt, &ptr, DATETIME_PARSE_ISO8601)) goto error;
            break;

        case DATETIME_PARSE_LAMBDA:
            /* Lambda format: parse content directly without t'...' wrapper */
            if (!datetime_parse_internal(dt, &ptr, DATETIME_PARSE_LAMBDA)) goto error;
            break;

        case DATETIME_PARSE_ICS:
            if (!datetime_parse_ics_internal(dt, &ptr)) goto error;
            break;

        default:
            goto error;
    }

    if (!datetime_is_valid(dt)) goto error;

    /* Set end pointer to where parsing stopped */
    if (end) *end = (char*)ptr;
    return dt;

error:
    if (end) *end = (char*)str;  /* Reset end pointer on error */
    return NULL;  /* Let heap manager handle cleanup */
}

DateTime* datetime_parse_lambda(Pool* pool, const char* lambda_str) {
    if (!lambda_str || !pool) return NULL;

    /* Handle Lambda format with t'...' wrapper */
    if (strncmp(lambda_str, "t'", 2) == 0) {
        const char* content = lambda_str + 2;
        size_t len = strlen(content);
        if (len > 0 && content[len-1] == '\'') {
            char* temp = (char*)pool_calloc(pool, len);
            strncpy(temp, content, len-1);
            temp[len-1] = '\0';
            return datetime_parse(pool, temp, DATETIME_PARSE_LAMBDA, NULL);
        }
    }

    /* If no wrapper, parse directly */
    return datetime_parse(pool, lambda_str, DATETIME_PARSE_LAMBDA, NULL);
}

DateTime* datetime_parse_ics(Pool* pool, const char* ics_str) {
    return datetime_parse(pool, ics_str, DATETIME_PARSE_ICS, NULL);
}

DateTime* datetime_from_string(Pool* pool, const char* datetime_str) {
    if (!datetime_str || !pool) return NULL;

    /* Try different formats in order of specificity */
    DateTime* dt = NULL;

    /* Try Lambda format first if it starts with t' */
    if (strncmp(datetime_str, "t'", 2) == 0) {
        const char* content = datetime_str + 2;
        size_t len = strlen(content);
        if (len > 0 && content[len-1] == '\'') {
            char* temp = (char*)pool_calloc(pool, len);
            strncpy(temp, content, len-1);
            temp[len-1] = '\0';
            dt = datetime_parse(pool, temp, DATETIME_PARSE_LAMBDA, NULL);
            if (dt) return dt;
        }
    }

    /* Try ICS format (YYYYMMDDTHHMMSS or YYYYMMDD) */
    if (strlen(datetime_str) >= 8 && isdigit(datetime_str[0])) {
        dt = datetime_parse(pool, datetime_str, DATETIME_PARSE_ICS, NULL);
        if (dt) return dt;
    }

    /* Try ISO8601 format */
    dt = datetime_parse(pool, datetime_str, DATETIME_PARSE_ISO8601, NULL);
    return dt;
}

/* Internal ICS parser (common logic) */
static bool datetime_parse_ics_internal(DateTime* dt, const char** ptr) {
    if (!dt || !ptr || !*ptr) return false;

    /* Parse ICS format: YYYYMMDD or YYYYMMDDTHHMMSS or YYYYMMDDTHHMMSSZ */
    const char* start = *ptr;
    if (strlen(start) < 8) return false;

    /* Parse date: YYYYMMDD */
    int year, month, day;
    if (!parse_int(ptr, 4, &year)) return false;
    if (!parse_int(ptr, 2, &month)) return false;
    if (!parse_int(ptr, 2, &day)) return false;

    DATETIME_SET_YEAR_MONTH(dt, year, month);
    dt->day = day;

    /* Check for time part */
    if (**ptr == 'T' && strlen(*ptr) >= 7) {
        (*ptr)++; /* skip 'T' */
        dt->precision = DATETIME_PRECISION_DATE_TIME;  // Full datetime

        int hour, minute, second;
        if (!parse_int(ptr, 2, &hour)) return false;
        if (!parse_int(ptr, 2, &minute)) return false;
        if (!parse_int(ptr, 2, &second)) return false;

        dt->hour = hour;
        dt->minute = minute;
        dt->second = second;

        /* Check for UTC marker */
        if (**ptr == 'Z') {
            DATETIME_SET_TZ_OFFSET(dt, 0);
            dt->format_hint = DATETIME_FORMAT_ISO8601_UTC;
            (*ptr)++;
        } else {
            DATETIME_CLEAR_TIMEZONE(dt);
        }
    } else {
        /* Date only */
        dt->precision = DATETIME_PRECISION_DATE_ONLY;
        DATETIME_CLEAR_TIMEZONE(dt);
    }

    if (dt->format_hint == 0) {
        dt->format_hint = DATETIME_FORMAT_ISO8601;
    }

    return true;
}

void datetime_format_internal(StrBuf *strbuf, DateTime* dt, DateTimeParseFormat format) {
    if (!dt || !strbuf) return;
    char separator = (format == DATETIME_PARSE_LAMBDA) ? ' ' : 'T';

    int year = DATETIME_GET_YEAR(dt);
    int month = DATETIME_GET_MONTH(dt);
    int day = dt->day;

    /* Format based on precision */
    switch (dt->precision) {
        case DATETIME_PRECISION_YEAR_ONLY:
            /* Just year */
            strbuf_append_format(strbuf, "%04d", year);
            break;

        case DATETIME_PRECISION_DATE_ONLY:
            /* Check if this is a partial date (year-month only) or full date */
            if (day == 0) {
                /* Year and month only (day was not specified) */
                strbuf_append_format(strbuf, "%04d-%02d", year, month);
            } else {
                /* Full date */
                strbuf_append_format(strbuf, "%04d-%02d-%02d", year, month, day);
            }
            break;

        case DATETIME_PRECISION_TIME_ONLY:
            /* Time only - omit seconds if they are 0 and no milliseconds */
            if (dt->second == 0 && dt->millisecond == 0) {
                strbuf_append_format(strbuf, "%02d:%02d", dt->hour, dt->minute);
            } else {
                strbuf_append_format(strbuf, "%02d:%02d:%02d",
                                   dt->hour, dt->minute, dt->second);
                if (dt->millisecond > 0) {
                    strbuf_append_format(strbuf, ".%03d", dt->millisecond);
                }
            }
            break;

        case DATETIME_PRECISION_DATE_TIME:
            /* Full datetime */
            strbuf_append_format(strbuf, "%04d-%02d-%02d%c%02d:%02d:%02d",
                year, month, day, separator, dt->hour, dt->minute, dt->second);
            if (dt->millisecond > 0) {
                strbuf_append_format(strbuf, ".%03d", dt->millisecond);
            }
            break;
    }

    /* Handle timezone formatting for time-based precisions */
    if (dt->precision == DATETIME_PRECISION_TIME_ONLY || dt->precision == DATETIME_PRECISION_DATE_TIME) {
        /* Handle timezone formatting */
        if (DATETIME_HAS_TIMEZONE(dt)) {
            if (DATETIME_IS_UTC_FORMAT(dt)) {
                strbuf_append_char(strbuf,  format == DATETIME_PARSE_LAMBDA ? 'z' : 'Z');
            } else {
                int tz_offset = DATETIME_GET_TZ_OFFSET(dt);
                int hours = abs(tz_offset) / 60;
                int minutes = abs(tz_offset) % 60;
                strbuf_append_format(strbuf, "%c%02d:%02d",
                                   tz_offset >= 0 ? '+' : '-', hours, minutes);
            }
        }
    }
}

void datetime_format_iso8601(StrBuf *strbuf, DateTime* dt) {
    datetime_format_internal(strbuf, dt, DATETIME_PARSE_ISO8601);
}

void datetime_format_lambda(StrBuf *strbuf, DateTime* dt) {
    datetime_format_internal(strbuf, dt, DATETIME_PARSE_LAMBDA);
}

void datetime_format_ics(StrBuf *strbuf, DateTime* dt) {
    if (!dt || !strbuf) return;

    /* Format based on precision */
    switch (dt->precision) {
        case DATETIME_PRECISION_YEAR_ONLY:
            /* Year only - not standard ICS, but we'll format as year0101 */
            strbuf_append_format(strbuf, "%04d0101", DATETIME_GET_YEAR(dt));
            break;

        case DATETIME_PRECISION_DATE_ONLY:
            /* Date only */
            strbuf_append_format(strbuf, "%04d%02d%02d",
                               DATETIME_GET_YEAR(dt), DATETIME_GET_MONTH(dt), dt->day);
            break;

        case DATETIME_PRECISION_TIME_ONLY:
            /* Time only - not standard ICS, but we'll format as 19700101T time */
            strbuf_append_format(strbuf, "19700101T%02d%02d%02d",
                               dt->hour, dt->minute, dt->second);
            if (DATETIME_IS_UTC_FORMAT(dt)) {
                strbuf_append_char(strbuf, 'Z');
            }
            break;

        case DATETIME_PRECISION_DATE_TIME:
            /* Full datetime */
            strbuf_append_format(strbuf, "%04d%02d%02dT%02d%02d%02d",
                               DATETIME_GET_YEAR(dt), DATETIME_GET_MONTH(dt), dt->day,
                               dt->hour, dt->minute, dt->second);
            if (DATETIME_IS_UTC_FORMAT(dt)) {
                strbuf_append_char(strbuf, 'Z');
            }
            break;
    }
}

void datetime_to_string(StrBuf *strbuf, DateTime* dt, DateTimeFormat format) {
    if (!dt || !strbuf) return;

    switch (format) {
        case DATETIME_FORMAT_ISO8601:
        case DATETIME_FORMAT_ISO8601_UTC:
            datetime_format_iso8601(strbuf, dt);
            break;
        case DATETIME_FORMAT_HUMAN:
        case DATETIME_FORMAT_HUMAN_UTC:
            datetime_format_human(strbuf, dt);
            break;
        default:
            datetime_format_iso8601(strbuf, dt);
            break;
    }
}

int datetime_compare(DateTime* dt1, DateTime* dt2) {
    if (!dt1 || !dt2) return 0;

    int64_t unix1 = datetime_to_unix(dt1);
    int64_t unix2 = datetime_to_unix(dt2);

    if (unix1 < unix2) return -1;
    if (unix1 > unix2) return 1;
    return 0;
}

/* Placeholder implementations for remaining functions */
DateTime* datetime_parse_rfc2822(Pool* pool, const char* rfc_str) {
    /* TODO: Implement RFC2822 parsing */
    (void)pool; (void)rfc_str; /* suppress unused warnings */
    return NULL;
}

void datetime_format_rfc2822(StrBuf *strbuf, DateTime* dt) {
    /* TODO: Implement RFC2822 formatting */
    (void)strbuf; (void)dt; /* suppress unused warnings */
}

void datetime_format_human(StrBuf *strbuf, DateTime* dt) {
    /* TODO: Implement human-readable formatting */
    /* Fallback to ISO8601 for now */
    datetime_format_iso8601(strbuf, dt);
}

DateTime* datetime_add_seconds(Pool* pool, DateTime* dt, int64_t seconds) {
    if (!dt || !pool) return NULL;

    int64_t unix_time = datetime_to_unix(dt);
    unix_time += seconds;

    return datetime_from_unix(pool, unix_time);
}

DateTime* datetime_to_utc(Pool* pool, DateTime* dt) {
    if (!dt || !pool || !DATETIME_HAS_TIMEZONE(dt) || DATETIME_IS_UTC_FORMAT(dt)) return dt;

    DateTime* utc_dt = datetime_new(pool);
    if (!utc_dt) return NULL;

    *utc_dt = *dt;  /* Copy all fields */

    /* Convert to UTC by subtracting offset */
    int64_t unix_time = datetime_to_unix(dt);
    DateTime* result = datetime_from_unix(pool, unix_time);
    if (result) {
        result->precision = dt->precision;
        result->format_hint = DATETIME_FORMAT_ISO8601_UTC;
    }

    return result;
}

DateTime* datetime_to_local(Pool* pool, DateTime* dt) {
    /* TODO: Implement local timezone conversion */
    /* For now, just return the original datetime */
    (void)pool; /* suppress unused warning */
    return dt;
}
/* Convert DateTime to unix timestamp in milliseconds */
int64_t datetime_to_unix_ms(DateTime* dt) {
    int64_t seconds = datetime_to_unix(dt);
    return seconds * 1000 + (dt ? dt->millisecond : 0);
}

/* Create DateTime from unix timestamp in milliseconds */
DateTime* datetime_from_unix_ms(Pool* pool, int64_t unix_ms) {
    int64_t seconds = unix_ms / 1000;
    int ms = (int)(unix_ms % 1000);
    if (ms < 0) { ms += 1000; seconds--; } /* handle negative timestamps */
    DateTime* dt = datetime_from_unix(pool, seconds);
    if (dt) dt->millisecond = ms;
    return dt;
}

/* Calculate day of week (0=Sunday, 6=Saturday) using Zeller-like algorithm */
int datetime_weekday(DateTime* dt) {
    if (!dt) return -1;
    int year = DATETIME_GET_YEAR(dt);
    int month = DATETIME_GET_MONTH(dt);
    int day = dt->day;
    if (day == 0) day = 1; /* default day for partial dates */

    /* Tomohiko Sakamoto's algorithm */
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) year--;
    int dow = (year + year/4 - year/100 + year/400 + t[month - 1] + day) % 7;
    return dow; /* 0=Sunday, 1=Monday, ..., 6=Saturday */
}

/* Calculate day of year (1-366) */
int datetime_yearday(DateTime* dt) {
    if (!dt) return -1;
    int year = DATETIME_GET_YEAR(dt);
    int month = DATETIME_GET_MONTH(dt);
    int day = dt->day;
    if (day == 0) day = 1;

    static const int cumulative_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    if (month < 1 || month > 12) return -1;
    int yday = cumulative_days[month - 1] + day;
    if (is_leap_year(year) && month > 2) yday++;
    return yday;
}

/* Calculate ISO week number (1-53) */
int datetime_week_number(DateTime* dt) {
    if (!dt) return -1;

    /* ISO 8601: week starts on Monday, week 1 contains January 4th */
    int yday = datetime_yearday(dt);
    int wday = datetime_weekday(dt);
    /* convert Sunday=0 to Monday=0 system: Mon=0, Tue=1, ..., Sun=6 */
    int iso_wday = (wday + 6) % 7;

    /* calculate the Thursday of this week's ISO year */
    int thursday_yday = yday + (3 - iso_wday);
    if (thursday_yday < 1) return 53; /* belongs to previous year's last week */
    int year = DATETIME_GET_YEAR(dt);
    int days_in_year = is_leap_year(year) ? 366 : 365;
    if (thursday_yday > days_in_year) return 1; /* belongs to next year's week 1 */

    return (thursday_yday - 1) / 7 + 1;
}

/* Calculate quarter (1-4) */
int datetime_quarter(DateTime* dt) {
    if (!dt) return -1;
    int month = DATETIME_GET_MONTH(dt);
    return (month - 1) / 3 + 1;
}

/* Check if the datetime's year is a leap year */
bool datetime_is_leap_year_dt(DateTime* dt) {
    if (!dt) return false;
    return is_leap_year(DATETIME_GET_YEAR(dt));
}

/* Get number of days in the datetime's month */
int datetime_days_in_month_dt(DateTime* dt) {
    if (!dt) return -1;
    return days_in_month(DATETIME_GET_YEAR(dt), DATETIME_GET_MONTH(dt));
}

/* Format datetime using a custom pattern string.
 * Supported tokens:
 *   YYYY - 4-digit year, YY - 2-digit year
 *   MMMM - full month name, MMM - abbreviated month, MM - 2-digit month, M - month
 *   DD - 2-digit day, D - day
 *   dddd - full weekday, ddd - abbreviated weekday
 *   hh - 2-digit 24h hour, h - hour, HH - 2-digit 12h hour
 *   mm - 2-digit minute, ss - 2-digit second, SSS - 3-digit millisecond
 *   A - AM/PM, Z - UTC offset (+05:30), ZZ - compact offset (+0530)
 */
void datetime_format_pattern(StrBuf* strbuf, DateTime* dt, const char* pattern) {
    if (!strbuf || !dt || !pattern) return;

    static const char* month_names[] = {
        "", "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    static const char* month_abbr[] = {
        "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    static const char* weekday_names[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
    };
    static const char* weekday_abbr[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };

    int year = DATETIME_GET_YEAR(dt);
    int month = DATETIME_GET_MONTH(dt);
    int day = dt->day;
    int hour24 = dt->hour;
    int hour12 = hour24 % 12;
    if (hour12 == 0) hour12 = 12;
    int wday = datetime_weekday(dt);

    const char* p = pattern;
    while (*p) {
        /* try matching multi-char tokens first */
        if (strncmp(p, "YYYY", 4) == 0) {
            strbuf_append_format(strbuf, "%04d", year);
            p += 4;
        } else if (strncmp(p, "YY", 2) == 0) {
            strbuf_append_format(strbuf, "%02d", abs(year) % 100);
            p += 2;
        } else if (strncmp(p, "MMMM", 4) == 0) {
            strbuf_append_str(strbuf, (month >= 1 && month <= 12) ? month_names[month] : "???");
            p += 4;
        } else if (strncmp(p, "MMM", 3) == 0) {
            strbuf_append_str(strbuf, (month >= 1 && month <= 12) ? month_abbr[month] : "???");
            p += 3;
        } else if (strncmp(p, "MM", 2) == 0) {
            strbuf_append_format(strbuf, "%02d", month);
            p += 2;
        } else if (*p == 'M' && *(p+1) != 'M') {
            strbuf_append_format(strbuf, "%d", month);
            p += 1;
        } else if (strncmp(p, "DD", 2) == 0) {
            strbuf_append_format(strbuf, "%02d", day);
            p += 2;
        } else if (*p == 'D' && *(p+1) != 'D') {
            strbuf_append_format(strbuf, "%d", day);
            p += 1;
        } else if (strncmp(p, "dddd", 4) == 0) {
            strbuf_append_str(strbuf, (wday >= 0 && wday <= 6) ? weekday_names[wday] : "???");
            p += 4;
        } else if (strncmp(p, "ddd", 3) == 0) {
            strbuf_append_str(strbuf, (wday >= 0 && wday <= 6) ? weekday_abbr[wday] : "???");
            p += 3;
        } else if (strncmp(p, "HH", 2) == 0) {
            strbuf_append_format(strbuf, "%02d", hour24);
            p += 2;
        } else if (strncmp(p, "hh", 2) == 0) {
            strbuf_append_format(strbuf, "%02d", hour12);
            p += 2;
        } else if (*p == 'h' && *(p+1) != 'h') {
            strbuf_append_format(strbuf, "%d", hour24);
            p += 1;
        } else if (strncmp(p, "mm", 2) == 0) {
            strbuf_append_format(strbuf, "%02d", dt->minute);
            p += 2;
        } else if (strncmp(p, "SSS", 3) == 0) {
            strbuf_append_format(strbuf, "%03d", dt->millisecond);
            p += 3;
        } else if (strncmp(p, "ss", 2) == 0) {
            strbuf_append_format(strbuf, "%02d", dt->second);
            p += 2;
        } else if (*p == 'A') {
            strbuf_append_str(strbuf, hour24 < 12 ? "AM" : "PM");
            p += 1;
        } else if (strncmp(p, "ZZ", 2) == 0) {
            if (DATETIME_HAS_TIMEZONE(dt)) {
                int tz = DATETIME_GET_TZ_OFFSET(dt);
                int h = abs(tz) / 60, m = abs(tz) % 60;
                strbuf_append_format(strbuf, "%c%02d%02d", tz >= 0 ? '+' : '-', h, m);
            }
            p += 2;
        } else if (*p == 'Z' && *(p+1) != 'Z') {
            if (DATETIME_HAS_TIMEZONE(dt)) {
                int tz = DATETIME_GET_TZ_OFFSET(dt);
                int h = abs(tz) / 60, m = abs(tz) % 60;
                strbuf_append_format(strbuf, "%c%02d:%02d", tz >= 0 ? '+' : '-', h, m);
            }
            p += 1;
        } else {
            /* literal character */
            strbuf_append_char(strbuf, *p);
            p += 1;
        }
    }
}