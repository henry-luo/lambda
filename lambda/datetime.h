#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// Lambda DateTime Structure
// Represents date and time with timezone information
typedef struct DateTime {
    // Date components
    int32_t year;      // Full year (e.g., 2024)
    int8_t month;      // Month (1-12)
    int8_t day;        // Day of month (1-31)
    
    // Time components
    int8_t hour;       // Hour (0-23)
    int8_t minute;     // Minute (0-59)
    int8_t second;     // Second (0-59)
    int16_t millisecond; // Millisecond (0-999)
    
    // Timezone information
    int16_t tz_offset_minutes; // UTC offset in minutes (-720 to +840)
    bool has_timezone;         // Whether timezone is specified
    bool is_utc;              // Whether explicitly UTC (Z suffix)
    
    // Metadata
    uint8_t precision;        // DateTime precision flags
    uint8_t format_hint;      // Original format hint for serialization
} DateTime;

// DateTime precision flags
#define DATETIME_HAS_DATE      0x01
#define DATETIME_HAS_TIME      0x02
#define DATETIME_HAS_SECONDS   0x04
#define DATETIME_HAS_MILLIS    0x08
#define DATETIME_HAS_TIMEZONE  0x10

// DateTime format hints for serialization
typedef enum {
    DATETIME_FORMAT_ISO8601,     // 2024-01-15T10:30:00Z
    DATETIME_FORMAT_ISO_DATE,    // 2024-01-15
    DATETIME_FORMAT_ISO_TIME,    // 10:30:00
    DATETIME_FORMAT_ICS,         // 20240115T103000Z
    DATETIME_FORMAT_RFC2822,     // Mon, 15 Jan 2024 10:30:00 +0000
    DATETIME_FORMAT_HUMAN        // 2024-01-15 10:30 AM
} DateTimeFormat;

// Forward declarations
typedef struct String String;
typedef struct Context Context;

// Core DateTime functions
DateTime* datetime_new(Context* ctx);
DateTime* datetime_from_string(Context* ctx, const char* datetime_str);
DateTime* datetime_now(Context* ctx);
String* datetime_to_string(Context* ctx, DateTime* dt, DateTimeFormat format);

// Parsing functions for different formats
DateTime* datetime_parse_iso8601(Context* ctx, const char* iso_str);
DateTime* datetime_parse_ics(Context* ctx, const char* ics_str);
DateTime* datetime_parse_rfc2822(Context* ctx, const char* rfc_str);

// Formatting functions
String* datetime_format_iso8601(Context* ctx, DateTime* dt);
String* datetime_format_ics(Context* ctx, DateTime* dt);
String* datetime_format_rfc2822(Context* ctx, DateTime* dt);
String* datetime_format_human(Context* ctx, DateTime* dt);

// Utility functions
bool datetime_is_valid(DateTime* dt);
int datetime_compare(DateTime* dt1, DateTime* dt2);
DateTime* datetime_add_seconds(Context* ctx, DateTime* dt, int64_t seconds);
DateTime* datetime_to_utc(Context* ctx, DateTime* dt);
DateTime* datetime_to_local(Context* ctx, DateTime* dt);

// Convert between DateTime and unix timestamp
int64_t datetime_to_unix(DateTime* dt);
DateTime* datetime_from_unix(Context* ctx, int64_t unix_timestamp);

#ifdef __cplusplus
}
#endif
