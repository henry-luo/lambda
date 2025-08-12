#include "datetime.h"
#include "lambda-data.hpp"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

// Simple string creation helper
static String* create_string(Context* ctx, const char* str) {
    if (!str || !ctx) return NULL;
    
    size_t len = strlen(str);
    String* string = (String*)pool_calloc((VariableMemPool*)ctx->ast_pool, sizeof(String) + len + 1);
    if (!string) return NULL;
    
    string->len = (uint32_t)len;
    string->ref_cnt = 1;
    memcpy(string->chars, str, len);
    string->chars[len] = '\0';
    
    return string;
}

// Helper function to check if a year is a leap year
static bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// Helper function to get days in month
static int days_in_month(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month == 2 && is_leap_year(year)) return 29;
    return days[month - 1];
}

DateTime* datetime_new(Context* ctx) {
    DateTime* dt = (DateTime*)pool_calloc((VariableMemPool*)ctx->ast_pool, sizeof(DateTime));
    if (!dt) return NULL;
    
    memset(dt, 0, sizeof(DateTime));
    dt->precision = DATETIME_HAS_DATE | DATETIME_HAS_TIME;
    dt->format_hint = DATETIME_FORMAT_ISO8601;
    return dt;
}

DateTime* datetime_now(Context* ctx) {
    time_t now = time(NULL);
    return datetime_from_unix(ctx, (int64_t)now);
}

DateTime* datetime_from_unix(Context* ctx, int64_t unix_timestamp) {
    DateTime* dt = datetime_new(ctx);
    if (!dt) return NULL;
    
    time_t timestamp = (time_t)unix_timestamp;
    struct tm* tm_utc = gmtime(&timestamp);
    if (!tm_utc) return NULL;
    
    dt->year = tm_utc->tm_year + 1900;
    dt->month = tm_utc->tm_mon + 1;
    dt->day = tm_utc->tm_mday;
    dt->hour = tm_utc->tm_hour;
    dt->minute = tm_utc->tm_min;
    dt->second = tm_utc->tm_sec;
    dt->millisecond = 0;
    
    dt->tz_offset_minutes = 0;
    dt->has_timezone = true;
    dt->is_utc = true;
    dt->precision = DATETIME_HAS_DATE | DATETIME_HAS_TIME | DATETIME_HAS_SECONDS | DATETIME_HAS_TIMEZONE;
    
    return dt;
}

int64_t datetime_to_unix(DateTime* dt) {
    if (!dt || !datetime_is_valid(dt)) return 0;
    
    struct tm tm_time = {0};
    tm_time.tm_year = dt->year - 1900;
    tm_time.tm_mon = dt->month - 1;
    tm_time.tm_mday = dt->day;
    tm_time.tm_hour = dt->hour;
    tm_time.tm_min = dt->minute;
    tm_time.tm_sec = dt->second;
    
    // Convert to UTC timestamp
    time_t timestamp = mktime(&tm_time);
    if (timestamp == -1) return 0;
    
    // Adjust for timezone offset
    if (dt->has_timezone && !dt->is_utc) {
        timestamp -= dt->tz_offset_minutes * 60;
    }
    
    return (int64_t)timestamp;
}

bool datetime_is_valid(DateTime* dt) {
    if (!dt) return false;
    if (dt->year < 1 || dt->year > 9999) return false;
    if (dt->month < 1 || dt->month > 12) return false;
    if (dt->day < 1 || dt->day > days_in_month(dt->year, dt->month)) return false;
    if (dt->hour < 0 || dt->hour > 23) return false;
    if (dt->minute < 0 || dt->minute > 59) return false;
    if (dt->second < 0 || dt->second > 59) return false;
    if (dt->millisecond < 0 || dt->millisecond > 999) return false;
    if (dt->tz_offset_minutes < -720 || dt->tz_offset_minutes > 840) return false;
    return true;
}

// Helper function to skip whitespace
static void skip_whitespace(const char** str) {
    while (**str && isspace(**str)) (*str)++;
}

// Helper function to parse integer with specific width
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

// Helper function to parse integer with specific width for int32_t
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

// Helper function to parse integer with specific width for int16_t
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

DateTime* datetime_parse_iso8601(Context* ctx, const char* iso_str) {
    if (!iso_str || !ctx) return NULL;
    
    DateTime* dt = datetime_new(ctx);
    if (!dt) return NULL;
    
    const char* ptr = iso_str;
    skip_whitespace(&ptr);
    
    // Parse date: YYYY-MM-DD
    if (!parse_int(&ptr, 4, &dt->year)) goto error;
    dt->precision |= DATETIME_HAS_DATE;
    
    if (*ptr == '-') ptr++;
    if (!parse_int_small(&ptr, 2, &dt->month)) goto error;
    
    if (*ptr == '-') ptr++;
    if (!parse_int_small(&ptr, 2, &dt->day)) goto error;
    
    // Check for time separator 'T' or space
    if (*ptr == 'T' || *ptr == ' ') {
        ptr++;
        dt->precision |= DATETIME_HAS_TIME;
        
        // Parse time: HH:MM:SS
        if (!parse_int_small(&ptr, 2, &dt->hour)) goto error;
        
        if (*ptr == ':') ptr++;
        if (!parse_int_small(&ptr, 2, &dt->minute)) goto error;
        
        if (*ptr == ':') {
            ptr++;
            dt->precision |= DATETIME_HAS_SECONDS;
            if (!parse_int_small(&ptr, 2, &dt->second)) goto error;
            
            // Parse optional milliseconds
            if (*ptr == '.') {
                ptr++;
                dt->precision |= DATETIME_HAS_MILLIS;
                int millis_width = 0;
                while (isdigit(*ptr) && millis_width < 3) {
                    dt->millisecond = dt->millisecond * 10 + (*ptr - '0');
                    ptr++;
                    millis_width++;
                }
                // Normalize to 3 digits
                while (millis_width < 3) {
                    dt->millisecond *= 10;
                    millis_width++;
                }
            }
        }
        
        // Parse timezone
        skip_whitespace(&ptr);
        if (*ptr == 'Z') {
            dt->has_timezone = true;
            dt->is_utc = true;
            dt->tz_offset_minutes = 0;
            dt->precision |= DATETIME_HAS_TIMEZONE;
            ptr++;
        } else if (*ptr == '+' || *ptr == '-') {
            dt->has_timezone = true;
            dt->precision |= DATETIME_HAS_TIMEZONE;
            
            bool negative = (*ptr == '-');
            ptr++;
            
            int tz_hours = 0, tz_minutes = 0;
            if (!parse_int(&ptr, 2, &tz_hours)) goto error;
            
            if (*ptr == ':') ptr++;
            if (!parse_int(&ptr, 2, &tz_minutes)) goto error;
            
            dt->tz_offset_minutes = tz_hours * 60 + tz_minutes;
            if (negative) dt->tz_offset_minutes = -dt->tz_offset_minutes;
        }
    }
    
    dt->format_hint = DATETIME_FORMAT_ISO8601;
    
    if (!datetime_is_valid(dt)) goto error;
    return dt;
    
error:
    return NULL;  // Let heap manager handle cleanup
}

DateTime* datetime_parse_ics(Context* ctx, const char* ics_str) {
    if (!ics_str || !ctx) return NULL;
    
    DateTime* dt = datetime_new(ctx);
    if (!dt) return NULL;
    
    const char* ptr = ics_str;
    
    // Parse ICS format: YYYYMMDD or YYYYMMDDTHHMMSS or YYYYMMDDTHHMMSSZ
    if (strlen(ptr) < 8) goto error;
    
    // Parse date: YYYYMMDD
    if (!parse_int(&ptr, 4, &dt->year)) goto error;
    if (!parse_int_small(&ptr, 2, &dt->month)) goto error;
    if (!parse_int_small(&ptr, 2, &dt->day)) goto error;
    dt->precision |= DATETIME_HAS_DATE;
    
    // Check for time part
    if (*ptr == 'T' && strlen(ptr) >= 7) {
        ptr++; // skip 'T'
        dt->precision |= DATETIME_HAS_TIME | DATETIME_HAS_SECONDS;
        
        if (!parse_int_small(&ptr, 2, &dt->hour)) goto error;
        if (!parse_int_small(&ptr, 2, &dt->minute)) goto error;
        if (!parse_int_small(&ptr, 2, &dt->second)) goto error;
        
        // Check for UTC marker
        if (*ptr == 'Z') {
            dt->has_timezone = true;
            dt->is_utc = true;
            dt->tz_offset_minutes = 0;
            dt->precision |= DATETIME_HAS_TIMEZONE;
        }
    }
    
    dt->format_hint = DATETIME_FORMAT_ICS;
    
    if (!datetime_is_valid(dt)) goto error;
    return dt;
    
error:
    return NULL;
}

DateTime* datetime_from_string(Context* ctx, const char* datetime_str) {
    if (!datetime_str || !ctx) return NULL;
    
    // Try different parsing formats in order of specificity
    DateTime* dt = NULL;
    
    // Try ISO8601 first (most common)
    dt = datetime_parse_iso8601(ctx, datetime_str);
    if (dt) return dt;
    
    // Try ICS format
    dt = datetime_parse_ics(ctx, datetime_str);
    if (dt) return dt;
    
    // If all parsing fails, return NULL
    return NULL;
}

String* datetime_format_iso8601(Context* ctx, DateTime* dt) {
    if (!dt || !ctx) return NULL;
    
    char buffer[32];
    int len = 0;
    
    if (dt->precision & DATETIME_HAS_DATE) {
        len += snprintf(buffer + len, sizeof(buffer) - len, "%04d-%02d-%02d", 
                       dt->year, dt->month, dt->day);
    }
    
    if (dt->precision & DATETIME_HAS_TIME) {
        len += snprintf(buffer + len, sizeof(buffer) - len, "T%02d:%02d", 
                       dt->hour, dt->minute);
        
        if (dt->precision & DATETIME_HAS_SECONDS) {
            len += snprintf(buffer + len, sizeof(buffer) - len, ":%02d", dt->second);
            
            if (dt->precision & DATETIME_HAS_MILLIS) {
                len += snprintf(buffer + len, sizeof(buffer) - len, ".%03d", dt->millisecond);
            }
        }
        
        if (dt->precision & DATETIME_HAS_TIMEZONE) {
            if (dt->is_utc) {
                len += snprintf(buffer + len, sizeof(buffer) - len, "Z");
            } else {
                int hours = abs(dt->tz_offset_minutes) / 60;
                int minutes = abs(dt->tz_offset_minutes) % 60;
                len += snprintf(buffer + len, sizeof(buffer) - len, "%c%02d:%02d",
                               dt->tz_offset_minutes >= 0 ? '+' : '-', hours, minutes);
            }
        }
    }
    
    return create_string(ctx, buffer);
}

String* datetime_format_ics(Context* ctx, DateTime* dt) {
    if (!dt || !ctx) return NULL;
    
    char buffer[32];
    int len = 0;
    
    if (dt->precision & DATETIME_HAS_DATE) {
        len += snprintf(buffer + len, sizeof(buffer) - len, "%04d%02d%02d", 
                       dt->year, dt->month, dt->day);
    }
    
    if (dt->precision & DATETIME_HAS_TIME) {
        len += snprintf(buffer + len, sizeof(buffer) - len, "T%02d%02d%02d", 
                       dt->hour, dt->minute, dt->second);
        
        if (dt->is_utc) {
            len += snprintf(buffer + len, sizeof(buffer) - len, "Z");
        }
    }
    
    return create_string(ctx, buffer);
}

String* datetime_to_string(Context* ctx, DateTime* dt, DateTimeFormat format) {
    if (!dt || !ctx) return NULL;
    
    switch (format) {
        case DATETIME_FORMAT_ISO8601:
            return datetime_format_iso8601(ctx, dt);
        case DATETIME_FORMAT_ICS:
            return datetime_format_ics(ctx, dt);
        // Add other formats as needed
        default:
            return datetime_format_iso8601(ctx, dt);
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

// Placeholder implementations for remaining functions
DateTime* datetime_parse_rfc2822(Context* ctx, const char* rfc_str) {
    // TODO: Implement RFC2822 parsing
    return NULL;
}

String* datetime_format_rfc2822(Context* ctx, DateTime* dt) {
    // TODO: Implement RFC2822 formatting
    return NULL;
}

String* datetime_format_human(Context* ctx, DateTime* dt) {
    // TODO: Implement human-readable formatting
    return datetime_format_iso8601(ctx, dt);  // Fallback for now
}

DateTime* datetime_add_seconds(Context* ctx, DateTime* dt, int64_t seconds) {
    if (!dt || !ctx) return NULL;
    
    int64_t unix_time = datetime_to_unix(dt);
    unix_time += seconds;
    
    return datetime_from_unix(ctx, unix_time);
}

DateTime* datetime_to_utc(Context* ctx, DateTime* dt) {
    if (!dt || !ctx || !dt->has_timezone || dt->is_utc) return dt;
    
    DateTime* utc_dt = datetime_new(ctx);
    if (!utc_dt) return NULL;
    
    *utc_dt = *dt;  // Copy all fields
    
    // Convert to UTC by subtracting offset
    int64_t unix_time = datetime_to_unix(dt);
    DateTime* result = datetime_from_unix(ctx, unix_time);
    if (result) {
        result->precision = dt->precision;
        result->format_hint = dt->format_hint;
    }
    
    return result;
}

DateTime* datetime_to_local(Context* ctx, DateTime* dt) {
    // TODO: Implement local timezone conversion
    // For now, just return the original datetime
    return dt;
}
