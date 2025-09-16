#include <catch2/catch_test_macros.hpp>
#include <cstring>

extern "C" {
#include "../lib/datetime.h"
#include "../lib/strbuf.h"
}

// Global memory pool for tests
static VariableMemPool *test_pool = NULL;

// Setup function to initialize memory pool
void setup_datetime_tests() {
    if (!test_pool) {
        MemPoolError err = pool_variable_init(&test_pool, 4096, 20);
        REQUIRE(err == MEM_POOL_ERR_OK);
    }
}

// Teardown function to cleanup memory pool
void teardown_datetime_tests() {
    if (test_pool) {
        pool_variable_destroy(test_pool);
        test_pool = NULL;
    }
}

TEST_CASE("DateTime Structure Size and Packing", "[datetime][struct]") {
    setup_datetime_tests();
    
    REQUIRE(sizeof(DateTime) == 8); // DateTime struct should be exactly 8 bytes (64 bits)
    
    DateTime dt = {0};
    
    SECTION("Year-month field (17 bits)") {
        DATETIME_SET_YEAR_MONTH(&dt, 2025, 8);
        REQUIRE(DATETIME_GET_YEAR(&dt) == 2025);
        REQUIRE(DATETIME_GET_MONTH(&dt) == 8);
    }
    
    SECTION("Extreme values") {
        DATETIME_SET_YEAR_MONTH(&dt, DATETIME_MIN_YEAR, 1);
        REQUIRE(DATETIME_GET_YEAR(&dt) == DATETIME_MIN_YEAR);
        
        DATETIME_SET_YEAR_MONTH(&dt, DATETIME_MAX_YEAR, 12);
        REQUIRE(DATETIME_GET_YEAR(&dt) == DATETIME_MAX_YEAR);
    }
    
    teardown_datetime_tests();
}

TEST_CASE("DateTime Timezone Offset Handling", "[datetime][timezone]") {
    setup_datetime_tests();
    
    DateTime dt = {0};
    
    SECTION("UTC timezone") {
        DATETIME_SET_TZ_OFFSET(&dt, 0);
        REQUIRE(DATETIME_HAS_TIMEZONE(&dt));
        REQUIRE(DATETIME_GET_TZ_OFFSET(&dt) == 0);
    }
    
    SECTION("Positive offset") {
        DATETIME_SET_TZ_OFFSET(&dt, 300); // UTC+5 hours
        REQUIRE(DATETIME_HAS_TIMEZONE(&dt));
        REQUIRE(DATETIME_GET_TZ_OFFSET(&dt) == 300);
    }
    
    SECTION("Negative offset") {
        DATETIME_SET_TZ_OFFSET(&dt, -480); // UTC-8 hours
        REQUIRE(DATETIME_HAS_TIMEZONE(&dt));
        REQUIRE(DATETIME_GET_TZ_OFFSET(&dt) == -480);
    }
    
    SECTION("No timezone") {
        DATETIME_CLEAR_TIMEZONE(&dt);
        REQUIRE_FALSE(DATETIME_HAS_TIMEZONE(&dt));
    }
    
    teardown_datetime_tests();
}

TEST_CASE("DateTime Creation and Initialization", "[datetime][new]") {
    setup_datetime_tests();
    
    DateTime* dt = datetime_new(test_pool);
    
    REQUIRE(dt != nullptr);
    REQUIRE(dt->precision == DATETIME_PRECISION_DATE_TIME);
    REQUIRE(dt->format_hint == DATETIME_FORMAT_ISO8601);
    
    teardown_datetime_tests();
}

TEST_CASE("DateTime Validation", "[datetime][validation]") {
    setup_datetime_tests();
    
    DateTime* dt = datetime_new(test_pool);
    REQUIRE(dt != nullptr);
    
    SECTION("Valid date") {
        DATETIME_SET_YEAR_MONTH(dt, 2025, 8);
        dt->day = 12;
        dt->hour = 14;
        dt->minute = 30;
        dt->second = 45;
        dt->millisecond = 123;
        DATETIME_SET_TZ_OFFSET(dt, 0);
        
        REQUIRE(datetime_is_valid(dt));
    }
    
    SECTION("Invalid month") {
        DATETIME_SET_YEAR_MONTH(dt, 2025, 13);
        REQUIRE_FALSE(datetime_is_valid(dt));
    }
    
    SECTION("Invalid day") {
        DATETIME_SET_YEAR_MONTH(dt, 2025, 2);
        dt->day = 30; // February can't have 30 days
        REQUIRE_FALSE(datetime_is_valid(dt));
    }
    
    SECTION("Leap year February 29") {
        DATETIME_SET_YEAR_MONTH(dt, 2024, 2); // 2024 is a leap year
        dt->day = 29;
        REQUIRE(datetime_is_valid(dt));
    }
    
    SECTION("Non-leap year February 29") {
        DATETIME_SET_YEAR_MONTH(dt, 2023, 2); // 2023 is not a leap year
        dt->day = 29;
        REQUIRE_FALSE(datetime_is_valid(dt));
    }
    
    teardown_datetime_tests();
}

TEST_CASE("ISO8601 Parsing", "[datetime][iso8601][parsing]") {
    setup_datetime_tests();
    
    SECTION("Basic date-time parsing") {
        DateTime* dt = datetime_parse_iso8601(test_pool, "2025-08-12T14:30:45Z");
        REQUIRE(dt != nullptr);
        REQUIRE(DATETIME_GET_YEAR(dt) == 2025);
        REQUIRE(DATETIME_GET_MONTH(dt) == 8);
        REQUIRE(dt->day == 12);
        REQUIRE(dt->hour == 14);
        REQUIRE(dt->minute == 30);
        REQUIRE(dt->second == 45);
        REQUIRE(DATETIME_HAS_TIMEZONE(dt));
        REQUIRE(DATETIME_GET_TZ_OFFSET(dt) == 0);
    }
    
    SECTION("With milliseconds") {
        DateTime* dt = datetime_parse_iso8601(test_pool, "2025-08-12T14:30:45.123Z");
        REQUIRE(dt != nullptr);
        REQUIRE(dt->millisecond == 123);
    }
    
    SECTION("With timezone offset") {
        DateTime* dt = datetime_parse_iso8601(test_pool, "2025-08-12T14:30:45+05:30");
        REQUIRE(dt != nullptr);
        REQUIRE(DATETIME_GET_TZ_OFFSET(dt) == 330); // 5*60+30=330
    }
    
    SECTION("Negative timezone offset") {
        DateTime* dt = datetime_parse_iso8601(test_pool, "2025-08-12T14:30:45-08:00");
        REQUIRE(dt != nullptr);
        REQUIRE(DATETIME_GET_TZ_OFFSET(dt) == -480); // -8*60=-480
    }
    
    SECTION("Date only") {
        DateTime* dt = datetime_parse_iso8601(test_pool, "2025-08-12");
        REQUIRE(dt != nullptr);
        REQUIRE(DATETIME_GET_YEAR(dt) == 2025);
        REQUIRE(DATETIME_GET_MONTH(dt) == 8);
        REQUIRE(dt->day == 12);
        REQUIRE(dt->precision == DATETIME_PRECISION_DATE_ONLY);
    }
    
    teardown_datetime_tests();
}

TEST_CASE("ICS Format Parsing", "[datetime][ics][parsing]") {
    setup_datetime_tests();
    
    SECTION("ICS date-time format") {
        DateTime* dt = datetime_parse_ics(test_pool, "20250812T143045Z");
        REQUIRE(dt != nullptr);
        REQUIRE(DATETIME_GET_YEAR(dt) == 2025);
        REQUIRE(DATETIME_GET_MONTH(dt) == 8);
        REQUIRE(dt->day == 12);
        REQUIRE(dt->hour == 14);
        REQUIRE(dt->minute == 30);
        REQUIRE(dt->second == 45);
        REQUIRE(DATETIME_HAS_TIMEZONE(dt));
    }
    
    SECTION("ICS date-only format") {
        DateTime* dt = datetime_parse_ics(test_pool, "20250812");
        REQUIRE(dt != nullptr);
        REQUIRE(DATETIME_GET_YEAR(dt) == 2025);
        REQUIRE(DATETIME_GET_MONTH(dt) == 8);
        REQUIRE(dt->day == 12);
        REQUIRE(dt->precision == DATETIME_PRECISION_DATE_ONLY);
    }
    
    teardown_datetime_tests();
}

TEST_CASE("ISO8601 Formatting", "[datetime][iso8601][formatting]") {
    setup_datetime_tests();
    
    DateTime* dt = datetime_new(test_pool);
    REQUIRE(dt != nullptr);
    
    // Set up a test DateTime
    DATETIME_SET_YEAR_MONTH(dt, 2025, 8);
    dt->day = 12;
    dt->hour = 14;
    dt->minute = 30;
    dt->second = 45;
    dt->millisecond = 123;
    DATETIME_SET_TZ_OFFSET(dt, 0);
    dt->format_hint = DATETIME_FORMAT_ISO8601_UTC;
    
    SECTION("With milliseconds") {
        StrBuf* strbuf = strbuf_new();
        datetime_format_iso8601(strbuf, dt);
        REQUIRE(strbuf->str != nullptr);
        REQUIRE(strcmp(strbuf->str, "2025-08-12T14:30:45.123Z") == 0);
        strbuf_free(strbuf);
    }
    
    SECTION("Without milliseconds") {
        dt->millisecond = 0;
        StrBuf* strbuf = strbuf_new();
        datetime_format_iso8601(strbuf, dt);
        REQUIRE(strbuf->str != nullptr);
        REQUIRE(strcmp(strbuf->str, "2025-08-12T14:30:45Z") == 0);
        strbuf_free(strbuf);
    }
    
    SECTION("With timezone offset") {
        DATETIME_SET_TZ_OFFSET(dt, 330); // +05:30
        dt->format_hint = DATETIME_FORMAT_ISO8601;
        dt->millisecond = 0;
        StrBuf* strbuf = strbuf_new();
        datetime_format_iso8601(strbuf, dt);
        REQUIRE(strbuf->str != nullptr);
        REQUIRE(strcmp(strbuf->str, "2025-08-12T14:30:45+05:30") == 0);
        strbuf_free(strbuf);
    }
    
    teardown_datetime_tests();
}

TEST_CASE("ICS Formatting", "[datetime][ics][formatting]") {
    setup_datetime_tests();
    
    DateTime* dt = datetime_new(test_pool);
    REQUIRE(dt != nullptr);
    
    // Set up a test DateTime
    DATETIME_SET_YEAR_MONTH(dt, 2025, 8);
    dt->day = 12;
    dt->hour = 14;
    dt->minute = 30;
    dt->second = 45;
    DATETIME_SET_TZ_OFFSET(dt, 0);
    dt->format_hint = DATETIME_FORMAT_ISO8601_UTC;
    
    SECTION("Full date-time") {
        StrBuf* strbuf = strbuf_new();
        datetime_format_ics(strbuf, dt);
        REQUIRE(strbuf->str != nullptr);
        REQUIRE(strcmp(strbuf->str, "20250812T143045Z") == 0);
        strbuf_free(strbuf);
    }
    
    SECTION("Date only") {
        dt->precision = DATETIME_PRECISION_DATE_ONLY;
        StrBuf* strbuf = strbuf_new();
        datetime_format_ics(strbuf, dt);
        REQUIRE(strbuf->str != nullptr);
        REQUIRE(strcmp(strbuf->str, "20250812") == 0);
        strbuf_free(strbuf);
    }
    
    teardown_datetime_tests();
}

TEST_CASE("Unix Timestamp Conversion", "[datetime][unix]") {
    setup_datetime_tests();
    
    // Create a DateTime for a known timestamp
    DateTime* dt = datetime_new(test_pool);
    REQUIRE(dt != nullptr);
    
    DATETIME_SET_YEAR_MONTH(dt, 2025, 1);
    dt->day = 1;
    dt->hour = 0;
    dt->minute = 0;
    dt->second = 0;
    dt->millisecond = 0;
    DATETIME_SET_TZ_OFFSET(dt, 0); // UTC
    
    int64_t unix_time = datetime_to_unix(dt);
    REQUIRE(unix_time > 0);
    
    // Convert back from unix timestamp
    DateTime* dt2 = datetime_from_unix(test_pool, unix_time);
    REQUIRE(dt2 != nullptr);
    REQUIRE(DATETIME_GET_YEAR(dt2) == 2025);
    REQUIRE(DATETIME_GET_MONTH(dt2) == 1);
    REQUIRE(dt2->day == 1);
    
    teardown_datetime_tests();
}

TEST_CASE("DateTime Comparison", "[datetime][comparison]") {
    setup_datetime_tests();
    
    DateTime* dt1 = datetime_new(test_pool);
    DateTime* dt2 = datetime_new(test_pool);
    REQUIRE(dt1 != nullptr);
    REQUIRE(dt2 != nullptr);
    
    // Set up two different DateTimes
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
    dt2->second = 46; // 1 second later
    DATETIME_SET_TZ_OFFSET(dt2, 0);
    
    SECTION("Earlier vs later") {
        int comparison = datetime_compare(dt1, dt2);
        REQUIRE(comparison == -1);
        
        comparison = datetime_compare(dt2, dt1);
        REQUIRE(comparison == 1);
    }
    
    SECTION("Equal DateTimes") {
        dt2->second = 45;
        int comparison = datetime_compare(dt1, dt2);
        REQUIRE(comparison == 0);
    }
    
    teardown_datetime_tests();
}

TEST_CASE("Round-trip ISO8601", "[datetime][roundtrip]") {
    setup_datetime_tests();
    
    const char* test_strings[] = {
        "2025-08-12T14:30:45Z",
        "2025-08-12T14:30:45.123Z",
        "2025-08-12T14:30:45+05:30",
        "2025-08-12T14:30:45-08:00",
        "2025-08-12",
    };
    
    for (size_t i = 0; i < sizeof(test_strings) / sizeof(test_strings[0]); i++) {
        // Parse the string
        DateTime* dt = datetime_parse_iso8601(test_pool, test_strings[i]);
        REQUIRE(dt != nullptr);
        
        // Format it back
        StrBuf* strbuf = strbuf_new();
        datetime_format_iso8601(strbuf, dt);
        REQUIRE(strbuf->str != nullptr);
        
        // For date-only strings, we don't expect perfect round-trip since formatting includes default time
        if (strchr(test_strings[i], 'T') != NULL) {
            REQUIRE(strcmp(strbuf->str, test_strings[i]) == 0);
        }
        strbuf_free(strbuf);
    }
    
    teardown_datetime_tests();
}

TEST_CASE("Error Handling", "[datetime][error]") {
    setup_datetime_tests();
    
    SECTION("NULL input handling") {
        REQUIRE(datetime_parse_iso8601(test_pool, NULL) == nullptr);
        REQUIRE(datetime_parse_iso8601(NULL, "2025-08-12") == nullptr);
    }
    
    SECTION("Formatting with NULL inputs") {
        StrBuf* strbuf = strbuf_new();
        datetime_format_iso8601(NULL, datetime_new(test_pool)); // Should not crash with NULL strbuf
        datetime_format_iso8601(strbuf, NULL); // Should not crash with NULL DateTime
        strbuf_free(strbuf);
    }
    
    SECTION("Invalid date strings") {
        REQUIRE(datetime_parse_iso8601(test_pool, "invalid") == nullptr);
        REQUIRE(datetime_parse_iso8601(test_pool, "2025-13-01") == nullptr);
        REQUIRE(datetime_parse_iso8601(test_pool, "2025-02-30") == nullptr);
        REQUIRE(datetime_parse_iso8601(test_pool, "2025-08-12T25:00:00") == nullptr);
    }
    
    teardown_datetime_tests();
}

TEST_CASE("Precision Year Only", "[datetime][precision][year]") {
    setup_datetime_tests();
    
    SECTION("Year-only parsing") {
        DateTime* dt = datetime_parse(test_pool, "2024", DATETIME_PARSE_ISO8601, NULL);
        REQUIRE(dt != nullptr);
        REQUIRE(dt->precision == DATETIME_PRECISION_YEAR_ONLY);
        REQUIRE(DATETIME_GET_YEAR(dt) == 2024);
        REQUIRE(DATETIME_GET_MONTH(dt) == 1);
        REQUIRE(dt->day == 1);
    }
    
    SECTION("Year-only formatting") {
        DateTime* dt = datetime_parse(test_pool, "2024", DATETIME_PARSE_ISO8601, NULL);
        REQUIRE(dt != nullptr);
        
        StrBuf* strbuf = strbuf_new();
        datetime_format_iso8601(strbuf, dt);
        REQUIRE(strbuf->str != nullptr);
        REQUIRE(strcmp(strbuf->str, "2024") == 0);
        strbuf_free(strbuf);
    }
    
    teardown_datetime_tests();
}

TEST_CASE("Precision Flags", "[datetime][precision][flags]") {
    setup_datetime_tests();
    
    SECTION("Date-only precision") {
        DateTime* dt = datetime_parse(test_pool, "2024-08-12", DATETIME_PARSE_ISO8601, NULL);
        REQUIRE(dt != nullptr);
        REQUIRE(dt->precision == DATETIME_PRECISION_DATE_ONLY);
        REQUIRE(DATETIME_GET_YEAR(dt) == 2024);
        REQUIRE(DATETIME_GET_MONTH(dt) == 8);
        REQUIRE(dt->day == 12);
    }
    
    SECTION("Full datetime precision") {
        DateTime* dt = datetime_parse(test_pool, "2024-08-12T14:30:45", DATETIME_PARSE_ISO8601, NULL);
        REQUIRE(dt != nullptr);
        REQUIRE(dt->precision == DATETIME_PRECISION_DATE_TIME);
        REQUIRE(DATETIME_GET_YEAR(dt) == 2024);
        REQUIRE(DATETIME_GET_MONTH(dt) == 8);
        REQUIRE(dt->day == 12);
        REQUIRE(dt->hour == 14);
        REQUIRE(dt->minute == 30);
        REQUIRE(dt->second == 45);
    }
    
    teardown_datetime_tests();
}

TEST_CASE("Lambda Format Parsing", "[datetime][lambda][parsing]") {
    setup_datetime_tests();
    
    SECTION("Lambda year-only format") {
        DateTime* dt = datetime_parse(test_pool, "2024", DATETIME_PARSE_LAMBDA, NULL);
        REQUIRE(dt != nullptr);
        REQUIRE(dt->precision == DATETIME_PRECISION_YEAR_ONLY);
        REQUIRE(DATETIME_GET_YEAR(dt) == 2024);
        REQUIRE(DATETIME_GET_MONTH(dt) == 1);
        REQUIRE(dt->day == 1);
    }
    
    SECTION("Lambda full datetime format") {
        DateTime* dt = datetime_parse(test_pool, "2024-08-12 14:30:45", DATETIME_PARSE_LAMBDA, NULL);
        REQUIRE(dt != nullptr);
        REQUIRE(dt->precision == DATETIME_PRECISION_DATE_TIME);
        REQUIRE(DATETIME_GET_YEAR(dt) == 2024);
        REQUIRE(DATETIME_GET_MONTH(dt) == 8);
        REQUIRE(dt->day == 12);
        REQUIRE(dt->hour == 14);
        REQUIRE(dt->minute == 30);
        REQUIRE(dt->second == 45);
    }
    
    SECTION("Lambda date-only format") {
        DateTime* dt = datetime_parse(test_pool, "2024-08-12", DATETIME_PARSE_LAMBDA, NULL);
        REQUIRE(dt != nullptr);
        REQUIRE(dt->precision == DATETIME_PRECISION_DATE_ONLY);
        REQUIRE(DATETIME_GET_YEAR(dt) == 2024);
        REQUIRE(DATETIME_GET_MONTH(dt) == 8);
        REQUIRE(dt->day == 12);
    }
    
    teardown_datetime_tests();
}

TEST_CASE("Precision Aware Formatting", "[datetime][precision][formatting]") {
    setup_datetime_tests();
    
    StrBuf* strbuf = strbuf_new();
    
    SECTION("Year-only formatting preserves precision") {
        DateTime* dt = datetime_parse(test_pool, "2024", DATETIME_PARSE_ISO8601, NULL);
        REQUIRE(dt != nullptr);
        
        strbuf_reset(strbuf);
        datetime_format_iso8601(strbuf, dt);
        REQUIRE(strbuf->str != nullptr);
        REQUIRE(strcmp(strbuf->str, "2024") == 0);
    }
    
    SECTION("Date-only formatting preserves precision") {
        DateTime* dt = datetime_parse(test_pool, "2024-08-12", DATETIME_PARSE_ISO8601, NULL);
        REQUIRE(dt != nullptr);
        
        strbuf_reset(strbuf);
        datetime_format_iso8601(strbuf, dt);
        REQUIRE(strbuf->str != nullptr);
        REQUIRE(strcmp(strbuf->str, "2024-08-12") == 0);
    }
    
    SECTION("Full datetime formatting") {
        DateTime* dt = datetime_parse(test_pool, "2024-08-12T14:30:45", DATETIME_PARSE_ISO8601, NULL);
        REQUIRE(dt != nullptr);
        
        strbuf_reset(strbuf);
        datetime_format_iso8601(strbuf, dt);
        REQUIRE(strbuf->str != nullptr);
        REQUIRE(strcmp(strbuf->str, "2024-08-12T14:30:45") == 0);
    }
    
    strbuf_free(strbuf);
    teardown_datetime_tests();
}
