#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "mempool.h"
#include "string.h"
#include "strbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

// Check if DateTime was already defined in lambda.h as uint64_t
#ifndef _DATETIME_DEFINED_
// Lambda DateTime Structure (Bitfield-packed)
// Represents date and time with timezone information
// Total size: 64 bits (8 bytes) exactly
typedef struct DateTime {
    union {
        struct {
            // All fields in a single 64-bit word for optimal packing
            uint64_t year_month : 17;      // Packed: (year+4000)*16 + month, covers years -4000 to +4191
            uint64_t day : 5;              // 0-31 (1-31 for days, 0 for invalid/unset)
            uint64_t hour : 5;             // 0-31 (0-23 for hours, extra bits for validation)
            uint64_t minute : 6;           // 0-63 (0-59 for minutes)
            uint64_t second : 6;           // 0-63 (0-59 for seconds)
            uint64_t millisecond : 10;     // 0-1023 (0-999 for milliseconds)
            uint64_t tz_offset_biased : 11; // Timezone offset + 1024 bias, 0 = no timezone
            uint64_t precision : 2;        // DateTimePrecision enum (2 bits = 4 possible values)
            uint64_t format_hint : 2;      // Format hint + UTC flag (2 bits = 4 combinations)
            // Total: 17+5+5+6+6+10+11+2+2 = 64 bits exactly
        };
        uint64_t int64_val;
    };
} DateTime;

#define _DATETIME_DEFINED_
#endif

// DateTime format types for parsing
typedef enum {
    DATETIME_PARSE_ISO8601 = 0,      // ISO8601 format (2024-01-15T10:30:00)
    DATETIME_PARSE_ICS = 1,          // ICS format (20240115T103000Z)
    DATETIME_PARSE_RFC2822 = 2,      // RFC2822 format (Mon, 15 Jan 2024 10:30:00 +0500)
    DATETIME_PARSE_LAMBDA = 3,       // Lambda format (YYYY-MM-DD hh:mm:ss, without t'...' wrapper)
    DATETIME_PARSE_HUMAN = 4,        // Human readable format
} DateTimeParseFormat;

// DateTime precision levels (2 bits = 4 possible values)
typedef enum {
    DATETIME_PRECISION_YEAR_ONLY = 0,  // Year only (2024)
    DATETIME_PRECISION_DATE_ONLY = 1,  // Date (year, month, day) (2024-01-15)
    DATETIME_PRECISION_TIME_ONLY = 2,  // Time only (hour, minute, second) (10:30:00)
    DATETIME_PRECISION_DATE_TIME = 3,  // Full datetime (date + time) (2024-01-15T10:30:00)
} DateTimePrecision;

// DateTime format hints with UTC flag (2 bits = 4 combinations)
typedef enum {
    DATETIME_FORMAT_ISO8601 = 0,     // 2024-01-15T10:30:00+05:00 (with timezone)
    DATETIME_FORMAT_HUMAN = 1,       // 2024-01-15 10:30 AM (human readable)
    DATETIME_FORMAT_ISO8601_UTC = 2, // 2024-01-15T10:30:00Z (UTC/Z suffix)
    DATETIME_FORMAT_HUMAN_UTC = 3,   // 2024-01-15 10:30 AM UTC (human readable UTC)
} DateTimeFormat;

// Helper macros for packed year_month field
// Formula: year_month = (year + 4000) * 16 + month
// This gives us: years -4000 to +4191, months 0-15
#define DATETIME_YEAR_BIAS 4000
#define DATETIME_SET_YEAR_MONTH(dt, y, m) \
    ((dt)->year_month = (uint64_t)(((y) + DATETIME_YEAR_BIAS) * 16 + (m)))
#define DATETIME_GET_YEAR(dt) \
    ((int32_t)((dt)->year_month / 16) - DATETIME_YEAR_BIAS)
#define DATETIME_GET_MONTH(dt) \
    ((uint32_t)((dt)->year_month % 16))

// Check if format hint indicates UTC
#define DATETIME_IS_UTC_FORMAT(dt) \
    ((dt)->format_hint == DATETIME_FORMAT_ISO8601_UTC || (dt)->format_hint == DATETIME_FORMAT_HUMAN_UTC)

// Timezone offset handling - uses bias and reserved value for "no timezone"
// Reserved value: 0 = no timezone specified
// Valid range: 1-2047 = timezone offsets -1023 to +1023 minutes
#define DATETIME_TZ_OFFSET_BIAS 1024
#define DATETIME_TZ_NO_TIMEZONE 0
#define DATETIME_SET_TZ_OFFSET(dt, offset_mins) \
    ((dt)->tz_offset_biased = (uint64_t)((offset_mins) + DATETIME_TZ_OFFSET_BIAS))
#define DATETIME_GET_TZ_OFFSET(dt) \
    ((int32_t)(dt)->tz_offset_biased - DATETIME_TZ_OFFSET_BIAS)
#define DATETIME_HAS_TIMEZONE(dt) \
    ((dt)->tz_offset_biased != DATETIME_TZ_NO_TIMEZONE)
#define DATETIME_CLEAR_TIMEZONE(dt) \
    ((dt)->tz_offset_biased = DATETIME_TZ_NO_TIMEZONE)

// Validation macros for bitfield limits
#define DATETIME_MAX_YEAR       4191   // Max year with bias: (131071/16) - 4000 = 4191
#define DATETIME_MIN_YEAR       (-4000) // Min year with bias: -4000
#define DATETIME_MAX_MONTH      12     // Valid months 1-12, 0 for unset
#define DATETIME_MAX_DAY        31
#define DATETIME_MAX_HOUR       23
#define DATETIME_MAX_MINUTE     59
#define DATETIME_MAX_SECOND     59
#define DATETIME_MAX_MILLIS     999
#define DATETIME_MAX_TZ_OFFSET  1023   // Actual range: -1023 to +1023 minutes (~±17 hours)
#define DATETIME_MIN_TZ_OFFSET  (-1023)

// Utility macros for common timezone offsets
#define DATETIME_TZ_UTC         0      // UTC+0
#define DATETIME_TZ_EST         (-300) // UTC-5 (Eastern Standard Time)
#define DATETIME_TZ_PST         (-480) // UTC-8 (Pacific Standard Time)
#define DATETIME_TZ_CET         60     // UTC+1 (Central European Time)
#define DATETIME_TZ_JST         540    // UTC+9 (Japan Standard Time)

// Core DateTime functions
DateTime* datetime_new(Pool* pool);
DateTime* datetime_from_string(Pool* pool, const char* datetime_str);
DateTime* datetime_now(Pool* pool);
void datetime_to_string(StrBuf *strbuf, DateTime* dt, DateTimeFormat format);

// Parsing functions for different formats
DateTime* datetime_parse_iso8601(Pool* pool, const char* iso_str);
DateTime* datetime_parse_ics(Pool* pool, const char* ics_str);
DateTime* datetime_parse_rfc2822(Pool* pool, const char* rfc_str);
DateTime* datetime_parse_lambda(Pool* pool, const char* lambda_str);

// General parsing function with format parameter
DateTime* datetime_parse(Pool* pool, const char* str, DateTimeParseFormat format, char** end);

// Formatting functions
void datetime_format_iso8601(StrBuf *strbuf, DateTime* dt);
void datetime_format_lambda(StrBuf *strbuf, DateTime* dt);
void datetime_format_ics(StrBuf *strbuf, DateTime* dt);
void datetime_format_rfc2822(StrBuf *strbuf, DateTime* dt);
void datetime_format_human(StrBuf *strbuf, DateTime* dt);

// Utility functions
bool datetime_is_valid(DateTime* dt);
int datetime_compare(DateTime* dt1, DateTime* dt2);
DateTime* datetime_add_seconds(Pool* pool, DateTime* dt, int64_t seconds);
DateTime* datetime_to_utc(Pool* pool, DateTime* dt);
DateTime* datetime_to_local(Pool* pool, DateTime* dt);

// Convert between DateTime and unix timestamp
int64_t datetime_to_unix(DateTime* dt);
int64_t datetime_to_unix_ms(DateTime* dt);
DateTime* datetime_from_unix(Pool* pool, int64_t unix_timestamp);
DateTime* datetime_from_unix_ms(Pool* pool, int64_t unix_ms);

// Calendar utility functions
int datetime_weekday(DateTime* dt);          // 0=Sunday, 6=Saturday
int datetime_yearday(DateTime* dt);          // 1-366
int datetime_week_number(DateTime* dt);      // ISO week number 1-53
int datetime_quarter(DateTime* dt);          // 1-4
bool datetime_is_leap_year_dt(DateTime* dt); // check if datetime's year is a leap year
int datetime_days_in_month_dt(DateTime* dt); // days in the datetime's month

// Custom format pattern support
void datetime_format_pattern(StrBuf* strbuf, DateTime* dt, const char* pattern);

#ifdef __cplusplus
}
#endif
