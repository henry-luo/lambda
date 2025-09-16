#include "../lib/unit_test/include/criterion/criterion.h"
#include "../lib/datetime.h"
#include "../lib/strbuf.h"
#include <string.h>

/* Test fixture setup */
VariableMemPool* pool;

void datetime_setup(void) {
    pool_variable_init(&pool, 4096, 20);
}

void datetime_teardown(void) {
    if (pool) {
        pool_variable_destroy(pool);
        pool = NULL;
    }
}

TestSuite(datetime, .init = datetime_setup, .fini = datetime_teardown);

/* Test DateTime structure size and bitfield packing */
Test(datetime, struct_size_and_packing) {
    cr_assert_eq(sizeof(DateTime), 8, "DateTime struct should be exactly 8 bytes (64 bits)");
    
    DateTime dt = {0};
    
    /* Test year_month field (17 bits) */
    DATETIME_SET_YEAR_MONTH(&dt, 2025, 8);
    cr_assert_eq(DATETIME_GET_YEAR(&dt), 2025, "Year should be correctly stored and retrieved");
    cr_assert_eq(DATETIME_GET_MONTH(&dt), 8, "Month should be correctly stored and retrieved");
    
    /* Test extreme values */
    DATETIME_SET_YEAR_MONTH(&dt, DATETIME_MIN_YEAR, 1);
    cr_assert_eq(DATETIME_GET_YEAR(&dt), DATETIME_MIN_YEAR, "Min year should be stored correctly");
    
    DATETIME_SET_YEAR_MONTH(&dt, DATETIME_MAX_YEAR, 12);
    cr_assert_eq(DATETIME_GET_YEAR(&dt), DATETIME_MAX_YEAR, "Max year should be stored correctly");
}

/* Test timezone offset handling */
Test(datetime, timezone_offset_handling) {
    DateTime dt = {0};
    
    /* Test UTC timezone */
    DATETIME_SET_TZ_OFFSET(&dt, 0);
    cr_assert(DATETIME_HAS_TIMEZONE(&dt), "UTC timezone should be detected");
    cr_assert_eq(DATETIME_GET_TZ_OFFSET(&dt), 0, "UTC offset should be 0");
    
    /* Test positive offset */
    DATETIME_SET_TZ_OFFSET(&dt, 300); /* UTC+5 hours */
    cr_assert(DATETIME_HAS_TIMEZONE(&dt), "Positive timezone should be detected");
    cr_assert_eq(DATETIME_GET_TZ_OFFSET(&dt), 300, "Positive offset should be stored correctly");
    
    /* Test negative offset */
    DATETIME_SET_TZ_OFFSET(&dt, -480); /* UTC-8 hours */
    cr_assert(DATETIME_HAS_TIMEZONE(&dt), "Negative timezone should be detected");
    cr_assert_eq(DATETIME_GET_TZ_OFFSET(&dt), -480, "Negative offset should be stored correctly");
    
    /* Test no timezone */
    DATETIME_CLEAR_TIMEZONE(&dt);
    cr_assert(!DATETIME_HAS_TIMEZONE(&dt), "No timezone should be detected after clearing");
}

/* Test DateTime creation and initialization */
Test(datetime, datetime_new) {
    DateTime* dt = datetime_new(pool);
    
    cr_assert_not_null(dt, "datetime_new should return non-null DateTime");
    cr_assert_eq(dt->precision, DATETIME_PRECISION_DATE_TIME, "Default precision should be full date-time");
    cr_assert_eq(dt->format_hint, DATETIME_FORMAT_ISO8601, "Default format should be ISO8601");
}

/* Test DateTime validation */
Test(datetime, datetime_validation) {
    DateTime* dt = datetime_new(pool);
    cr_assert_not_null(dt);
    
    /* Set valid date */
    DATETIME_SET_YEAR_MONTH(dt, 2025, 8);
    dt->day = 12;
    dt->hour = 14;
    dt->minute = 30;
    dt->second = 45;
    dt->millisecond = 123;
    DATETIME_SET_TZ_OFFSET(dt, 0);
    
    cr_assert(datetime_is_valid(dt), "Valid DateTime should pass validation");
    
    /* Test invalid month */
    DATETIME_SET_YEAR_MONTH(dt, 2025, 13);
    cr_assert(!datetime_is_valid(dt), "DateTime with invalid month should fail validation");
    
    /* Reset to valid and test invalid day */
    DATETIME_SET_YEAR_MONTH(dt, 2025, 2);
    dt->day = 30; /* February can't have 30 days */
    cr_assert(!datetime_is_valid(dt), "DateTime with invalid day should fail validation");
    
    /* Test leap year February 29 */
    DATETIME_SET_YEAR_MONTH(dt, 2024, 2); /* 2024 is a leap year */
    dt->day = 29;
    cr_assert(datetime_is_valid(dt), "February 29 in leap year should be valid");
    
    /* Test non-leap year February 29 */
    DATETIME_SET_YEAR_MONTH(dt, 2023, 2); /* 2023 is not a leap year */
    dt->day = 29;
    cr_assert(!datetime_is_valid(dt), "February 29 in non-leap year should be invalid");
}

/* Test ISO8601 parsing */
Test(datetime, iso8601_parsing) {
    /* Test basic date-time parsing */
    DateTime* dt = datetime_parse_iso8601(pool, "2025-08-12T14:30:45Z");
    cr_assert_not_null(dt, "ISO8601 parsing should succeed");
    cr_assert_eq(DATETIME_GET_YEAR(dt), 2025, "Year should be parsed correctly");
    cr_assert_eq(DATETIME_GET_MONTH(dt), 8, "Month should be parsed correctly");
    cr_assert_eq(dt->day, 12, "Day should be parsed correctly");
    cr_assert_eq(dt->hour, 14, "Hour should be parsed correctly");
    cr_assert_eq(dt->minute, 30, "Minute should be parsed correctly");
    cr_assert_eq(dt->second, 45, "Second should be parsed correctly");
    cr_assert(DATETIME_HAS_TIMEZONE(dt), "UTC timezone should be detected");
    cr_assert_eq(DATETIME_GET_TZ_OFFSET(dt), 0, "UTC offset should be 0");
    
    /* Test with milliseconds */
    dt = datetime_parse_iso8601(pool, "2025-08-12T14:30:45.123Z");
    cr_assert_not_null(dt, "ISO8601 parsing with milliseconds should succeed");
    cr_assert_eq(dt->millisecond, 123, "Milliseconds should be parsed correctly");
    
    /* Test with timezone offset */
    dt = datetime_parse_iso8601(pool, "2025-08-12T14:30:45+05:30");
    cr_assert_not_null(dt, "ISO8601 parsing with timezone should succeed");
    cr_assert_eq(DATETIME_GET_TZ_OFFSET(dt), 330, "Timezone offset should be parsed correctly (5*60+30=330)");
    
    /* Test negative timezone offset */
    dt = datetime_parse_iso8601(pool, "2025-08-12T14:30:45-08:00");
    cr_assert_not_null(dt, "ISO8601 parsing with negative timezone should succeed");
    cr_assert_eq(DATETIME_GET_TZ_OFFSET(dt), -480, "Negative timezone offset should be parsed correctly (-8*60=-480)");
    
    /* Test date only */
    dt = datetime_parse_iso8601(pool, "2025-08-12");
    cr_assert_not_null(dt, "ISO8601 date-only parsing should succeed");
    cr_assert_eq(DATETIME_GET_YEAR(dt), 2025, "Year should be parsed correctly for date-only");
    cr_assert_eq(DATETIME_GET_MONTH(dt), 8, "Month should be parsed correctly for date-only");
    cr_assert_eq(dt->day, 12, "Day should be parsed correctly for date-only");
    cr_assert_eq(dt->precision, DATETIME_PRECISION_DATE_ONLY, "Date-only precision should be set correctly");
}

/* Test ICS format parsing */
Test(datetime, ics_parsing) {
    /* Test ICS date-time format */
    DateTime* dt = datetime_parse_ics(pool, "20250812T143045Z");
    cr_assert_not_null(dt, "ICS parsing should succeed");
    cr_assert_eq(DATETIME_GET_YEAR(dt), 2025, "ICS year should be parsed correctly");
    cr_assert_eq(DATETIME_GET_MONTH(dt), 8, "ICS month should be parsed correctly");
    cr_assert_eq(dt->day, 12, "ICS day should be parsed correctly");
    cr_assert_eq(dt->hour, 14, "ICS hour should be parsed correctly");
    cr_assert_eq(dt->minute, 30, "ICS minute should be parsed correctly");
    cr_assert_eq(dt->second, 45, "ICS second should be parsed correctly");
    cr_assert(DATETIME_HAS_TIMEZONE(dt), "ICS UTC timezone should be detected");
    
    /* Test ICS date-only format */
    dt = datetime_parse_ics(pool, "20250812");
    cr_assert_not_null(dt, "ICS date-only parsing should succeed");
    cr_assert_eq(DATETIME_GET_YEAR(dt), 2025, "ICS date-only year should be parsed correctly");
    cr_assert_eq(DATETIME_GET_MONTH(dt), 8, "ICS date-only month should be parsed correctly");
    cr_assert_eq(dt->day, 12, "ICS date-only day should be parsed correctly");
    cr_assert_eq(dt->precision, DATETIME_PRECISION_DATE_ONLY, "ICS date-only precision should be set correctly");
}

/* Test DateTime formatting */
Test(datetime, iso8601_formatting) {
    DateTime* dt = datetime_new(pool);
    cr_assert_not_null(dt);
    
    /* Set up a test DateTime */
    DATETIME_SET_YEAR_MONTH(dt, 2025, 8);
    dt->day = 12;
    dt->hour = 14;
    dt->minute = 30;
    dt->second = 45;
    dt->millisecond = 123;
    DATETIME_SET_TZ_OFFSET(dt, 0);
    dt->format_hint = DATETIME_FORMAT_ISO8601_UTC;
    
    StrBuf* strbuf = strbuf_new();
    datetime_format_iso8601(strbuf, dt);
    cr_assert_not_null(strbuf->str, "ISO8601 formatting should succeed");
    cr_assert_str_eq(strbuf->str, "2025-08-12T14:30:45.123Z", "ISO8601 formatting should produce correct string");
    
    /* Test without milliseconds */
    dt->millisecond = 0;
    strbuf_reset(strbuf);
    datetime_format_iso8601(strbuf, dt);
    cr_assert_not_null(strbuf->str);
    cr_assert_str_eq(strbuf->str, "2025-08-12T14:30:45Z", "ISO8601 formatting without milliseconds should be correct");
    
    /* Test with timezone offset */
    DATETIME_SET_TZ_OFFSET(dt, 330); /* +05:30 */
    dt->format_hint = DATETIME_FORMAT_ISO8601;
    strbuf_reset(strbuf);
    datetime_format_iso8601(strbuf, dt);
    cr_assert_not_null(strbuf->str);
    cr_assert_str_eq(strbuf->str, "2025-08-12T14:30:45+05:30", "ISO8601 formatting with timezone should be correct");
    
    strbuf_free(strbuf);
}

/* Test ICS formatting */
Test(datetime, ics_formatting) {
    DateTime* dt = datetime_new(pool);
    cr_assert_not_null(dt);
    
    /* Set up a test DateTime */
    DATETIME_SET_YEAR_MONTH(dt, 2025, 8);
    dt->day = 12;
    dt->hour = 14;
    dt->minute = 30;
    dt->second = 45;
    DATETIME_SET_TZ_OFFSET(dt, 0);
    dt->format_hint = DATETIME_FORMAT_ISO8601_UTC;
    
    StrBuf* strbuf = strbuf_new();
    datetime_format_ics(strbuf, dt);
    cr_assert_not_null(strbuf->str, "ICS formatting should succeed");
    cr_assert_str_eq(strbuf->str, "20250812T143045Z", "ICS formatting should produce correct string");
    
    /* Test date-only */
    dt->precision = DATETIME_PRECISION_DATE_ONLY;
    strbuf_reset(strbuf);
    datetime_format_ics(strbuf, dt);
    cr_assert_not_null(strbuf->str);
    cr_assert_str_eq(strbuf->str, "20250812", "ICS date-only formatting should be correct");
    
    strbuf_free(strbuf);
}

/* Test Unix timestamp conversion */
Test(datetime, unix_timestamp_conversion) {
    /* Create a DateTime for a known timestamp */
    DateTime* dt = datetime_new(pool);
    cr_assert_not_null(dt);
    
    DATETIME_SET_YEAR_MONTH(dt, 2025, 1);
    dt->day = 1;
    dt->hour = 0;
    dt->minute = 0;
    dt->second = 0;
    dt->millisecond = 0;
    DATETIME_SET_TZ_OFFSET(dt, 0); /* UTC */
    
    int64_t unix_time = datetime_to_unix(dt);
    cr_assert_gt(unix_time, 0, "Unix timestamp should be positive");
    
    /* Convert back from unix timestamp */
    DateTime* dt2 = datetime_from_unix(pool, unix_time);
    cr_assert_not_null(dt2, "Conversion from unix timestamp should succeed");
    cr_assert_eq(DATETIME_GET_YEAR(dt2), 2025, "Year should be preserved in round-trip conversion");
    cr_assert_eq(DATETIME_GET_MONTH(dt2), 1, "Month should be preserved in round-trip conversion");
    cr_assert_eq(dt2->day, 1, "Day should be preserved in round-trip conversion");
}

/* Test DateTime comparison */
Test(datetime, datetime_comparison) {
    DateTime* dt1 = datetime_new(pool);
    DateTime* dt2 = datetime_new(pool);
    cr_assert_not_null(dt1);
    cr_assert_not_null(dt2);
    
    /* Set up two different DateTimes */
    DATETIME_SET_YEAR_MONTH(dt1, 2025, 8);
    dt1->day = 12;
    dt1->hour = 14;
    dt1->minute = 30;
    dt1->second = 45;
    DATETIME_SET_TZ_OFFSET(dt1, 0);
    
    DATETIME_SET_YEAR_MONTH(dt2, 2025, 8);
    dt2->day = 12;
    dt2->hour = 14;
    dt2->minute = 30;
    dt2->second = 46; /* 1 second later */
    DATETIME_SET_TZ_OFFSET(dt2, 0);
    
    int comparison = datetime_compare(dt1, dt2);
    cr_assert_eq(comparison, -1, "Earlier DateTime should compare as less than later DateTime");
    
    comparison = datetime_compare(dt2, dt1);
    cr_assert_eq(comparison, 1, "Later DateTime should compare as greater than earlier DateTime");
    
    /* Test equal DateTimes */
    dt2->second = 45;
    comparison = datetime_compare(dt1, dt2);
    cr_assert_eq(comparison, 0, "Equal DateTimes should compare as equal");
}

/* Test round-trip parsing and formatting */
Test(datetime, round_trip_iso8601) {
    const char* test_strings[] = {
        "2025-08-12T14:30:45Z",
        "2025-08-12T14:30:45.123Z",
        "2025-08-12T14:30:45+05:30",
        "2025-08-12T14:30:45-08:00",
        "2025-08-12",
    };
    
    for (size_t i = 0; i < sizeof(test_strings) / sizeof(test_strings[0]); i++) {
        /* Parse the string */
        DateTime* dt = datetime_parse_iso8601(pool, test_strings[i]);
        cr_assert_not_null(dt, "Parsing should succeed for test string: %s", test_strings[i]);
        
        /* Format it back */
        StrBuf* strbuf = strbuf_new();
        datetime_format_iso8601(strbuf, dt);
        cr_assert_not_null(strbuf->str, "Formatting should succeed for test string: %s", test_strings[i]);
        
        /* For date-only strings, we don't expect perfect round-trip since formatting includes default time */
        if (strchr(test_strings[i], 'T') != NULL) {
            cr_assert_str_eq(strbuf->str, test_strings[i], 
                           "Round-trip should preserve original string: %s", test_strings[i]);
        }
        strbuf_free(strbuf);
    }
}

/* Test error handling */
Test(datetime, error_handling) {
    /* Test NULL input handling */
    cr_assert_null(datetime_parse_iso8601(pool, NULL), "Parsing NULL string should return NULL");
    cr_assert_null(datetime_parse_iso8601(NULL, "2025-08-12"), "Parsing with NULL pool should return NULL");
    
    /* Test formatting with NULL inputs - these should not crash but just return early */
    StrBuf* strbuf = strbuf_new();
    datetime_format_iso8601(NULL, datetime_new(pool)); /* Should not crash with NULL strbuf */
    datetime_format_iso8601(strbuf, NULL); /* Should not crash with NULL DateTime */
    strbuf_free(strbuf);
    
    /* Test invalid date strings */
    cr_assert_null(datetime_parse_iso8601(pool, "invalid"), "Parsing invalid string should return NULL");
    cr_assert_null(datetime_parse_iso8601(pool, "2025-13-01"), "Parsing invalid month should return NULL");
    cr_assert_null(datetime_parse_iso8601(pool, "2025-02-30"), "Parsing invalid day should return NULL");
    cr_assert_null(datetime_parse_iso8601(pool, "2025-08-12T25:00:00"), "Parsing invalid hour should return NULL");
}

/* Test new precision system with year-only flag */
Test(datetime, precision_year_only) {
    /* Test year-only parsing with ISO8601 format */
    DateTime* dt = datetime_parse(pool, "2024", DATETIME_PARSE_ISO8601, NULL);
    cr_assert_not_null(dt, "Year-only parsing should succeed (returned: %p)", dt);
    if (dt) {
        cr_assert_eq(dt->precision, DATETIME_PRECISION_YEAR_ONLY, "Precision should be year-only (expected: %d, got: %d)", DATETIME_PRECISION_YEAR_ONLY, dt->precision);
        cr_assert_eq(DATETIME_GET_YEAR(dt), 2024, "Year should be parsed correctly (expected: %d, got: %d)", 2024, DATETIME_GET_YEAR(dt));
        cr_assert_eq(DATETIME_GET_MONTH(dt), 1, "Month should default to 1 for year-only (expected: %d, got: %d)", 1, DATETIME_GET_MONTH(dt));
        cr_assert_eq(dt->day, 1, "Day should default to 1 for year-only (expected: %d, got: %d)", 1, dt->day);
    }
    
    /* Test year-only formatting */
    StrBuf* strbuf = strbuf_new();
    datetime_format_iso8601(strbuf, dt);
    cr_assert_not_null(strbuf->str, "Year-only formatting should succeed");
    cr_assert_str_eq(strbuf->str, "2024", "Year-only should format as just the year");
    strbuf_free(strbuf);
}

/* Test precision flags for different formats */
Test(datetime, precision_flags) {
    DateTime* dt;
    
    /* Test date-only precision */
    dt = datetime_parse(pool, "2024-08-12", DATETIME_PARSE_ISO8601, NULL);
    cr_assert_not_null(dt, "Date-only parsing should succeed");
    cr_assert_eq(dt->precision, DATETIME_PRECISION_DATE_ONLY, "Precision should be date-only");
    cr_assert_eq(DATETIME_GET_YEAR(dt), 2024, "Year should be parsed correctly");
    cr_assert_eq(DATETIME_GET_MONTH(dt), 8, "Month should be parsed correctly");
    cr_assert_eq(dt->day, 12, "Day should be parsed correctly");
    
    /* Test full datetime precision */
    dt = datetime_parse(pool, "2024-08-12T14:30:45", DATETIME_PARSE_ISO8601, NULL);
    cr_assert_not_null(dt, "Full datetime parsing should succeed");
    cr_assert_eq(dt->precision, DATETIME_PRECISION_DATE_TIME, "Precision should be full datetime");
    cr_assert_eq(DATETIME_GET_YEAR(dt), 2024, "Year should be parsed correctly");
    cr_assert_eq(DATETIME_GET_MONTH(dt), 8, "Month should be parsed correctly");
    cr_assert_eq(dt->day, 12, "Day should be parsed correctly");
    cr_assert_eq(dt->hour, 14, "Hour should be parsed correctly");
    cr_assert_eq(dt->minute, 30, "Minute should be parsed correctly");
    cr_assert_eq(dt->second, 45, "Second should be parsed correctly");
}

/* Test Lambda format parsing and precision */
Test(datetime, lambda_format_parsing) {
    DateTime* dt;
    
    /* Test Lambda year-only format */
    dt = datetime_parse(pool, "2024", DATETIME_PARSE_LAMBDA, NULL);
    cr_assert_not_null(dt, "Lambda year-only parsing should succeed (returned: %p)", dt);
    if (dt) {
        cr_assert_eq(dt->precision, DATETIME_PRECISION_YEAR_ONLY, "Lambda year-only precision should be year-only (expected: %d, got: %d)", DATETIME_PRECISION_YEAR_ONLY, dt->precision);
    }
    cr_assert_eq(DATETIME_GET_YEAR(dt), 2024, "Lambda year should be parsed correctly");
    cr_assert_eq(DATETIME_GET_MONTH(dt), 1, "Lambda year-only month should default to 1");
    cr_assert_eq(dt->day, 1, "Lambda year-only day should default to 1");
    
    /* Test Lambda full datetime format */
    dt = datetime_parse(pool, "2024-08-12 14:30:45", DATETIME_PARSE_LAMBDA, NULL);
    cr_assert_not_null(dt, "Lambda full datetime parsing should succeed");
    cr_assert_eq(dt->precision, DATETIME_PRECISION_DATE_TIME, "Lambda datetime precision should be full datetime");
    cr_assert_eq(DATETIME_GET_YEAR(dt), 2024, "Lambda datetime year should be parsed correctly");
    cr_assert_eq(DATETIME_GET_MONTH(dt), 8, "Lambda datetime month should be parsed correctly");
    cr_assert_eq(dt->day, 12, "Lambda datetime day should be parsed correctly");
    cr_assert_eq(dt->hour, 14, "Lambda datetime hour should be parsed correctly");
    cr_assert_eq(dt->minute, 30, "Lambda datetime minute should be parsed correctly");
    cr_assert_eq(dt->second, 45, "Lambda datetime second should be parsed correctly");
    
    /* Test Lambda date-only format */
    dt = datetime_parse(pool, "2024-08-12", DATETIME_PARSE_LAMBDA, NULL);
    cr_assert_not_null(dt, "Lambda date-only parsing should succeed");
    cr_assert_eq(dt->precision, DATETIME_PRECISION_DATE_ONLY, "Lambda date-only precision should be date-only");
    cr_assert_eq(DATETIME_GET_YEAR(dt), 2024, "Lambda date year should be parsed correctly");
    cr_assert_eq(DATETIME_GET_MONTH(dt), 8, "Lambda date month should be parsed correctly");
    cr_assert_eq(dt->day, 12, "Lambda date day should be parsed correctly");
}

/* Test precision-aware formatting */
Test(datetime, precision_aware_formatting) {
    DateTime* dt;
    StrBuf* strbuf = strbuf_new();
    
    /* Test year-only formatting preserves precision */
    dt = datetime_parse(pool, "2024", DATETIME_PARSE_ISO8601, NULL);
    cr_assert_not_null(dt, "Year-only parsing should succeed for formatting test (returned: %p)", dt);
    if (dt) {
        strbuf_reset(strbuf);
        datetime_format_iso8601(strbuf, dt);
        cr_assert_not_null(strbuf->str, "Year-only formatting should succeed (strbuf->str: %p)", strbuf->str);
        if (strbuf->str) {
            cr_assert_str_eq(strbuf->str, "2024", "Year-only formatting should output just the year (expected: '2024', got: '%s')", strbuf->str);
        }
    }
    
    /* Test date-only formatting preserves precision */
    dt = datetime_parse(pool, "2024-08-12", DATETIME_PARSE_ISO8601, NULL);
    cr_assert_not_null(dt, "Date-only parsing should succeed for formatting test");
    strbuf_reset(strbuf);
    datetime_format_iso8601(strbuf, dt);
    cr_assert_not_null(strbuf->str, "Date-only formatting should succeed");
    cr_assert_str_eq(strbuf->str, "2024-08-12", "Date-only formatting should output just the date");
    
    /* Test full datetime formatting */
    dt = datetime_parse(pool, "2024-08-12T14:30:45", DATETIME_PARSE_ISO8601, NULL);
    cr_assert_not_null(dt, "Full datetime parsing should succeed for formatting test");
    strbuf_reset(strbuf);
    datetime_format_iso8601(strbuf, dt);
    cr_assert_not_null(strbuf->str, "Full datetime formatting should succeed");
    cr_assert_str_eq(strbuf->str, "2024-08-12T14:30:45", "Full datetime formatting should output date and time");
    
    strbuf_free(strbuf);
}
