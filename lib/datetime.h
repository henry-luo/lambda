#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "mem-pool/include/mem_pool.h"
#include "string.h"

#ifdef __cplusplus
extern "C" {
#endif

// Lambda DateTime Structure (Bitfield-packed)
// Represents date and time with timezone information
// Total size: 64 bits (8 bytes) exactly
typedef struct DateTime {
    // All fields in a single 64-bit word for optimal packing
    uint64_t year_month : 17;      // Packed: (year+4000)*16 + month, covers years -4000 to +4191
    uint64_t day : 5;              // 0-31 (1-31 for days, 0 for invalid/unset)
    uint64_t hour : 5;             // 0-31 (0-23 for hours, extra bits for validation)
    uint64_t minute : 6;           // 0-63 (0-59 for minutes)
    uint64_t second : 6;           // 0-63 (0-59 for seconds)
    uint64_t millisecond : 10;     // 0-1023 (0-999 for milliseconds)
    uint64_t tz_offset_biased : 11; // Timezone offset + 1024 bias, 0 = no timezone
    uint64_t precision : 2;        // DateTime precision flags (2 bits = 4 flags)
    uint64_t format_hint : 2;      // Format hint + UTC flag (2 bits = 4 combinations)
    // Total: 17+5+5+6+6+10+11+2+2 = 64 bits exactly
} DateTime;

// DateTime precision flags (2 bits = 4 possible flags max)
#define DATETIME_HAS_DATE      0x01
#define DATETIME_HAS_TIME      0x02
// Combinations: 0x00=none, 0x01=date only, 0x02=time only, 0x03=date+time

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
DateTime* datetime_new(VariableMemPool* pool);
DateTime* datetime_from_string(VariableMemPool* pool, const char* datetime_str);
DateTime* datetime_now(VariableMemPool* pool);
String* datetime_to_string(VariableMemPool* pool, DateTime* dt, DateTimeFormat format);

// Parsing functions for different formats
DateTime* datetime_parse_iso8601(VariableMemPool* pool, const char* iso_str);
DateTime* datetime_parse_ics(VariableMemPool* pool, const char* ics_str);
DateTime* datetime_parse_rfc2822(VariableMemPool* pool, const char* rfc_str);

// General parsing function that returns DateTime* and end pointer
DateTime* datetime_parse(VariableMemPool* pool, const char* str, char** end);

// Formatting functions
String* datetime_format_iso8601(VariableMemPool* pool, DateTime* dt);
String* datetime_format_ics(VariableMemPool* pool, DateTime* dt);
String* datetime_format_rfc2822(VariableMemPool* pool, DateTime* dt);
String* datetime_format_human(VariableMemPool* pool, DateTime* dt);

// Utility functions
bool datetime_is_valid(DateTime* dt);
int datetime_compare(DateTime* dt1, DateTime* dt2);
DateTime* datetime_add_seconds(VariableMemPool* pool, DateTime* dt, int64_t seconds);
DateTime* datetime_to_utc(VariableMemPool* pool, DateTime* dt);
DateTime* datetime_to_local(VariableMemPool* pool, DateTime* dt);

// Convert between DateTime and unix timestamp
int64_t datetime_to_unix(DateTime* dt);
DateTime* datetime_from_unix(VariableMemPool* pool, int64_t unix_timestamp);

#ifdef __cplusplus
}
#endif
