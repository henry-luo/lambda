#include <gtest/gtest.h>

extern "C" {
#include "../lib/datetime.h"
#include "../lib/mem-pool/include/mem_pool.h"
}

class DateTimeTest : public ::testing::Test {
protected:
    VariableMemPool* pool = nullptr;

    void SetUp() override {
        pool_variable_init(&pool, 4096, 20);
    }

    void TearDown() override {
        if (pool) {
            pool_variable_destroy(pool);
            pool = nullptr;
        }
    }
};

TEST_F(DateTimeTest, Basic) {
    EXPECT_EQ(sizeof(DateTime), 8UL);
}

// Test 1: DateTime structure size and bitfield packing
TEST_F(DateTimeTest, StructSizeAndPacking) {
    EXPECT_EQ(sizeof(DateTime), 8) << "DateTime struct should be exactly 8 bytes (64 bits)";
    
    DateTime dt = {0};
    
    // Test year_month field (17 bits)
    DATETIME_SET_YEAR_MONTH(&dt, 2025, 8);
    EXPECT_EQ(DATETIME_GET_YEAR(&dt), 2025) << "Year should be correctly stored and retrieved";
    EXPECT_EQ(DATETIME_GET_MONTH(&dt), 8) << "Month should be correctly stored and retrieved";
    
    // Test extreme values
    DATETIME_SET_YEAR_MONTH(&dt, DATETIME_MIN_YEAR, 1);
    EXPECT_EQ(DATETIME_GET_YEAR(&dt), DATETIME_MIN_YEAR) << "Min year should be stored correctly";
    
    DATETIME_SET_YEAR_MONTH(&dt, DATETIME_MAX_YEAR, 12);
    EXPECT_EQ(DATETIME_GET_YEAR(&dt), DATETIME_MAX_YEAR) << "Max year should be stored correctly";
}

// Test 2: Timezone offset handling
TEST_F(DateTimeTest, TimezoneOffsetHandling) {
    DateTime dt = {0};
    
    // Test UTC timezone
    DATETIME_SET_TZ_OFFSET(&dt, 0);
    EXPECT_TRUE(DATETIME_HAS_TIMEZONE(&dt)) << "UTC timezone should be detected";
    EXPECT_EQ(DATETIME_GET_TZ_OFFSET(&dt), 0) << "UTC offset should be 0";
    
    // Test positive offset
    DATETIME_SET_TZ_OFFSET(&dt, 300); // UTC+5 hours
    EXPECT_TRUE(DATETIME_HAS_TIMEZONE(&dt)) << "Positive timezone should be detected";
    EXPECT_EQ(DATETIME_GET_TZ_OFFSET(&dt), 300) << "Positive offset should be stored correctly";
    
    // Test negative offset
    DATETIME_SET_TZ_OFFSET(&dt, -480); // UTC-8 hours
    EXPECT_TRUE(DATETIME_HAS_TIMEZONE(&dt)) << "Negative timezone should be detected";
    EXPECT_EQ(DATETIME_GET_TZ_OFFSET(&dt), -480) << "Negative offset should be stored correctly";
    
    // Test no timezone
    DATETIME_CLEAR_TIMEZONE(&dt);
    EXPECT_FALSE(DATETIME_HAS_TIMEZONE(&dt)) << "No timezone should be detected after clearing";
}

// Test 3: DateTime creation and initialization
TEST_F(DateTimeTest, DatetimeNew) {
    DateTime* dt = datetime_new(pool);
    
    EXPECT_NE(dt, nullptr) << "datetime_new should return non-null DateTime";
    EXPECT_EQ(dt->precision, DATETIME_PRECISION_DATE_TIME) << "Default precision should be full date-time";
    EXPECT_EQ(dt->format_hint, DATETIME_FORMAT_ISO8601) << "Default format should be ISO8601";
}

// Test 4: DateTime validation
TEST_F(DateTimeTest, DatetimeValidation) {
    DateTime* dt = datetime_new(pool);
    EXPECT_NE(dt, nullptr);
    
    // Set valid date
    DATETIME_SET_YEAR_MONTH(dt, 2025, 8);
    dt->day = 12;
    dt->hour = 14;
    dt->minute = 30;
    dt->second = 45;
    dt->millisecond = 123;
    DATETIME_SET_TZ_OFFSET(dt, 0);
    
    EXPECT_TRUE(datetime_is_valid(dt)) << "Valid DateTime should pass validation";
    
    // Test invalid month
    DATETIME_SET_YEAR_MONTH(dt, 2025, 13);
    EXPECT_FALSE(datetime_is_valid(dt)) << "DateTime with invalid month should fail validation";
    
    // Reset to valid and test invalid day
    DATETIME_SET_YEAR_MONTH(dt, 2025, 2);
    dt->day = 30; // February can't have 30 days
    EXPECT_FALSE(datetime_is_valid(dt)) << "DateTime with invalid day should fail validation";
    
    // Test leap year February 29
    DATETIME_SET_YEAR_MONTH(dt, 2024, 2); // 2024 is a leap year
    dt->day = 29;
    EXPECT_TRUE(datetime_is_valid(dt)) << "February 29 in leap year should be valid";
    
    // Test non-leap year February 29
    DATETIME_SET_YEAR_MONTH(dt, 2023, 2); // 2023 is not a leap year
    dt->day = 29;
    EXPECT_FALSE(datetime_is_valid(dt)) << "February 29 in non-leap year should be invalid";
}

// Test 5: ISO8601 parsing
TEST_F(DateTimeTest, Iso8601Parsing) {
    // Test basic date-time parsing
    DateTime* dt = datetime_parse_iso8601(pool, "2025-08-12T14:30:45Z");
    EXPECT_NE(dt, nullptr) << "ISO8601 parsing should succeed";
    EXPECT_EQ(DATETIME_GET_YEAR(dt), 2025) << "Year should be parsed correctly";
    EXPECT_EQ(DATETIME_GET_MONTH(dt), 8) << "Month should be parsed correctly";
    EXPECT_EQ(dt->day, 12) << "Day should be parsed correctly";
    EXPECT_EQ(dt->hour, 14) << "Hour should be parsed correctly";
    EXPECT_EQ(dt->minute, 30) << "Minute should be parsed correctly";
    EXPECT_EQ(dt->second, 45) << "Second should be parsed correctly";
    EXPECT_TRUE(DATETIME_HAS_TIMEZONE(dt)) << "UTC timezone should be detected";
    EXPECT_EQ(DATETIME_GET_TZ_OFFSET(dt), 0) << "UTC offset should be 0";
    
    // Test with milliseconds
    dt = datetime_parse_iso8601(pool, "2025-08-12T14:30:45.123Z");
    EXPECT_NE(dt, nullptr) << "ISO8601 parsing with milliseconds should succeed";
    EXPECT_EQ(dt->millisecond, 123) << "Milliseconds should be parsed correctly";
    
    // Test with timezone offset
    dt = datetime_parse_iso8601(pool, "2025-08-12T14:30:45+05:30");
    EXPECT_NE(dt, nullptr) << "ISO8601 parsing with timezone should succeed";
    EXPECT_EQ(DATETIME_GET_TZ_OFFSET(dt), 330) << "Timezone offset should be parsed correctly (5*60+30=330)";
    
    // Test negative timezone offset
    dt = datetime_parse_iso8601(pool, "2025-08-12T14:30:45-08:00");
    EXPECT_NE(dt, nullptr) << "ISO8601 parsing with negative timezone should succeed";
    EXPECT_EQ(DATETIME_GET_TZ_OFFSET(dt), -480) << "Negative timezone offset should be parsed correctly (-8*60=-480)";
    
    // Test date only
    dt = datetime_parse_iso8601(pool, "2025-08-12");
    EXPECT_NE(dt, nullptr) << "ISO8601 date-only parsing should succeed";
    EXPECT_EQ(DATETIME_GET_YEAR(dt), 2025) << "Year should be parsed correctly for date-only";
    EXPECT_EQ(DATETIME_GET_MONTH(dt), 8) << "Month should be parsed correctly for date-only";
    EXPECT_EQ(dt->day, 12) << "Day should be parsed correctly for date-only";
    EXPECT_EQ(dt->precision, DATETIME_PRECISION_DATE_ONLY) << "Date-only precision should be set correctly";
}

// Test 6: ICS format parsing
TEST_F(DateTimeTest, IcsParsing) {
    // Test ICS date-time format
    DateTime* dt = datetime_parse_ics(pool, "20250812T143045Z");
    EXPECT_NE(dt, nullptr) << "ICS parsing should succeed";
    EXPECT_EQ(DATETIME_GET_YEAR(dt), 2025) << "ICS year should be parsed correctly";
    EXPECT_EQ(DATETIME_GET_MONTH(dt), 8) << "ICS month should be parsed correctly";
    EXPECT_EQ(dt->day, 12) << "ICS day should be parsed correctly";
    EXPECT_EQ(dt->hour, 14) << "ICS hour should be parsed correctly";
    EXPECT_EQ(dt->minute, 30) << "ICS minute should be parsed correctly";
    EXPECT_EQ(dt->second, 45) << "ICS second should be parsed correctly";
    EXPECT_TRUE(DATETIME_HAS_TIMEZONE(dt)) << "ICS UTC timezone should be detected";
    
    // Test ICS date-only format
    dt = datetime_parse_ics(pool, "20250812");
    EXPECT_NE(dt, nullptr) << "ICS date-only parsing should succeed";
    EXPECT_EQ(DATETIME_GET_YEAR(dt), 2025) << "ICS date-only year should be parsed correctly";
    EXPECT_EQ(DATETIME_GET_MONTH(dt), 8) << "ICS date-only month should be parsed correctly";
    EXPECT_EQ(dt->day, 12) << "ICS date-only day should be parsed correctly";
    EXPECT_EQ(dt->precision, DATETIME_PRECISION_DATE_ONLY) << "ICS date-only precision should be set correctly";
}

// Test 7: ISO8601 formatting
TEST_F(DateTimeTest, Iso8601Formatting) {
    DateTime* dt = datetime_new(pool);
    EXPECT_NE(dt, nullptr);
    
    // Set up a test DateTime
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
    EXPECT_NE(strbuf->str, nullptr) << "ISO8601 formatting should succeed";
    EXPECT_STREQ(strbuf->str, "2025-08-12T14:30:45.123Z") << "ISO8601 formatting should produce correct string";
    strbuf_free(strbuf);
}

// Test 8: ICS formatting
TEST_F(DateTimeTest, IcsFormatting) {
    DateTime* dt = datetime_new(pool);
    EXPECT_NE(dt, nullptr);
    
    // Set up a test DateTime
    DATETIME_SET_YEAR_MONTH(dt, 2025, 8);
    dt->day = 12;
    dt->hour = 14;
    dt->minute = 30;
    dt->second = 45;
    DATETIME_SET_TZ_OFFSET(dt, 0);
    dt->format_hint = DATETIME_FORMAT_ISO8601_UTC;
    
    StrBuf* strbuf = strbuf_new();
    datetime_format_ics(strbuf, dt);
    EXPECT_NE(strbuf->str, nullptr) << "ICS formatting should succeed";
    EXPECT_STREQ(strbuf->str, "20250812T143045Z") << "ICS formatting should produce correct string";
    
    // Test date-only
    dt->precision = DATETIME_PRECISION_DATE_ONLY;
    strbuf_reset(strbuf);
    datetime_format_ics(strbuf, dt);
    EXPECT_NE(strbuf->str, nullptr);
    EXPECT_STREQ(strbuf->str, "20250812") << "ICS date-only formatting should be correct";
    
    strbuf_free(strbuf);
}

// Test 9: Unix timestamp conversion
TEST_F(DateTimeTest, UnixTimestampConversion) {
    // Create a DateTime for a known timestamp
    DateTime* dt = datetime_new(pool);
    EXPECT_NE(dt, nullptr);
    
    // Set up a test DateTime (2025-08-12T14:30:45Z)
    DATETIME_SET_YEAR_MONTH(dt, 2025, 8);
    dt->day = 12;
    dt->hour = 14;
    dt->minute = 30;
    dt->second = 45;
    DATETIME_SET_TZ_OFFSET(dt, 0);
    
    // Convert to Unix timestamp and back
    int64_t timestamp = datetime_to_unix(dt);
    EXPECT_GT(timestamp, 0) << "Unix timestamp should be positive";
    
    DateTime* dt2 = datetime_from_unix(pool, timestamp);
    EXPECT_NE(dt2, nullptr) << "DateTime from Unix timestamp should succeed";
    EXPECT_EQ(DATETIME_GET_YEAR(dt2), 2025) << "Year should be preserved in round-trip";
    EXPECT_EQ(DATETIME_GET_MONTH(dt2), 8) << "Month should be preserved in round-trip";
    EXPECT_EQ(dt2->day, 12) << "Day should be preserved in round-trip";
}

// Test 10: DateTime comparison
TEST_F(DateTimeTest, DatetimeComparison) {
    DateTime* dt1 = datetime_new(pool);
    DateTime* dt2 = datetime_new(pool);
    EXPECT_NE(dt1, nullptr);
    EXPECT_NE(dt2, nullptr);
    
    // Set same date-time
    DATETIME_SET_YEAR_MONTH(dt1, 2025, 8);
    dt1->day = 12;
    dt1->hour = 14;
    dt1->minute = 30;
    dt1->second = 45;
    
    DATETIME_SET_YEAR_MONTH(dt2, 2025, 8);
    dt2->day = 12;
    dt2->hour = 14;
    dt2->minute = 30;
    dt2->second = 45;
    
    EXPECT_EQ(datetime_compare(dt1, dt2), 0) << "Equal DateTimes should compare as equal";
    
    // Make dt2 later
    dt2->second = 46;
    EXPECT_LT(datetime_compare(dt1, dt2), 0) << "Earlier DateTime should compare as less";
    EXPECT_GT(datetime_compare(dt2, dt1), 0) << "Later DateTime should compare as greater";
}

// Test 11: Error handling
TEST_F(DateTimeTest, ErrorHandling) {
    // Test NULL input to parsing functions
    DateTime* dt = datetime_parse_iso8601(pool, nullptr);
    EXPECT_EQ(dt, nullptr) << "Parsing NULL string should return NULL";
    
    dt = datetime_parse_iso8601(pool, "");
    EXPECT_EQ(dt, nullptr) << "Parsing empty string should return NULL";
    
    dt = datetime_parse_iso8601(pool, "invalid-date");
    EXPECT_EQ(dt, nullptr) << "Parsing invalid date should return NULL";
}

// Test 12: Round trip ISO8601
TEST_F(DateTimeTest, RoundTripIso8601) {
    // Test that parsing and formatting are inverse operations
    const char* original = "2025-08-12T14:30:45.123Z";
    
    DateTime* dt = datetime_parse_iso8601(pool, original);
    EXPECT_NE(dt, nullptr) << "Original string should parse successfully";
    
    StrBuf* strbuf = strbuf_new();
    datetime_format_iso8601(strbuf, dt);
    EXPECT_NE(strbuf->str, nullptr) << "Formatting should succeed";
    EXPECT_STREQ(strbuf->str, original) << "Round-trip should preserve original string";
    
    // Test round-trip with timezone
    const char* original_tz = "2025-08-12T14:30:45+05:30";
    dt = datetime_parse_iso8601(pool, original_tz);
    EXPECT_NE(dt, nullptr) << "Timezone string should parse successfully";
    
    strbuf_reset(strbuf);
    datetime_format_iso8601(strbuf, dt);
    EXPECT_NE(strbuf->str, nullptr) << "Timezone formatting should succeed";
    EXPECT_STREQ(strbuf->str, original_tz) << "Round-trip should preserve timezone string";
    
    strbuf_free(strbuf);
}

// Test 13: Precision year only
TEST_F(DateTimeTest, PrecisionYearOnly) {
    DateTime* dt = datetime_new(pool);
    EXPECT_NE(dt, nullptr);
    
    // Set only year information
    DATETIME_SET_YEAR_MONTH(dt, 2025, 1);
    dt->day = 1;
    dt->precision = DATETIME_PRECISION_YEAR_ONLY;
    
    EXPECT_TRUE(datetime_is_valid(dt)) << "Year-only DateTime should be valid";
    
    // Test formatting with year-only precision
    StrBuf* strbuf = strbuf_new();
    datetime_format_iso8601(strbuf, dt);
    EXPECT_NE(strbuf->str, nullptr) << "Year-only formatting should succeed";
    // Should format as just the year
    EXPECT_TRUE(strstr(strbuf->str, "2025") != nullptr) << "Year should appear in formatted string";
    
    strbuf_free(strbuf);
}

// Test 14: Precision flags
TEST_F(DateTimeTest, PrecisionFlags) {
    DateTime* dt = datetime_new(pool);
    EXPECT_NE(dt, nullptr);
    
    // Test different precision levels
    dt->precision = DATETIME_PRECISION_DATE_ONLY;
    EXPECT_TRUE(dt->precision == DATETIME_PRECISION_DATE_ONLY || dt->precision == DATETIME_PRECISION_DATE_TIME) << "Date-only precision should indicate date availability";
    EXPECT_FALSE(dt->precision == DATETIME_PRECISION_TIME_ONLY || dt->precision == DATETIME_PRECISION_DATE_TIME) << "Date-only precision should not indicate time availability";
    
    dt->precision = DATETIME_PRECISION_DATE_TIME;
    EXPECT_TRUE(dt->precision == DATETIME_PRECISION_DATE_ONLY || dt->precision == DATETIME_PRECISION_DATE_TIME) << "Date-time precision should indicate date availability";
    EXPECT_TRUE(dt->precision == DATETIME_PRECISION_TIME_ONLY || dt->precision == DATETIME_PRECISION_DATE_TIME) << "Date-time precision should indicate time availability";
    
    dt->precision = DATETIME_PRECISION_TIME_ONLY;
    EXPECT_FALSE(dt->precision == DATETIME_PRECISION_DATE_ONLY || dt->precision == DATETIME_PRECISION_DATE_TIME) << "Time-only precision should not indicate date availability";
    EXPECT_TRUE(dt->precision == DATETIME_PRECISION_TIME_ONLY || dt->precision == DATETIME_PRECISION_DATE_TIME) << "Time-only precision should indicate time availability";
    
    dt->precision = DATETIME_PRECISION_YEAR_ONLY;
    EXPECT_TRUE(dt->precision == DATETIME_PRECISION_YEAR_ONLY) << "Year-only precision should indicate partial date availability";
    EXPECT_FALSE(dt->precision == DATETIME_PRECISION_TIME_ONLY || dt->precision == DATETIME_PRECISION_DATE_TIME) << "Year-only precision should not indicate time availability";
}

// Test 15: Lambda format parsing
TEST_F(DateTimeTest, LambdaFormatParsing) {
    // Test Lambda-specific date format parsing
    DateTime* dt = datetime_parse_lambda(pool, "t'2025-08-12'");
    EXPECT_NE(dt, nullptr) << "Lambda date format should parse successfully";
    EXPECT_EQ(DATETIME_GET_YEAR(dt), 2025) << "Lambda format year should be parsed correctly";
    EXPECT_EQ(DATETIME_GET_MONTH(dt), 8) << "Lambda format month should be parsed correctly";
    EXPECT_EQ(dt->day, 12) << "Lambda format day should be parsed correctly";
    EXPECT_EQ(dt->precision, DATETIME_PRECISION_DATE_ONLY) << "Lambda date format should set date-only precision";
    
    // Test Lambda date-time format
    dt = datetime_parse_lambda(pool, "t'2025-08-12T14:30:45'");
    EXPECT_NE(dt, nullptr) << "Lambda date-time format should parse successfully";
    EXPECT_EQ(DATETIME_GET_YEAR(dt), 2025) << "Lambda date-time year should be parsed correctly";
    EXPECT_EQ(DATETIME_GET_MONTH(dt), 8) << "Lambda date-time month should be parsed correctly";
    EXPECT_EQ(dt->day, 12) << "Lambda date-time day should be parsed correctly";
    EXPECT_EQ(dt->hour, 14) << "Lambda date-time hour should be parsed correctly";
    EXPECT_EQ(dt->minute, 30) << "Lambda date-time minute should be parsed correctly";
    EXPECT_EQ(dt->second, 45) << "Lambda date-time second should be parsed correctly";
    EXPECT_EQ(dt->precision, DATETIME_PRECISION_DATE_TIME) << "Lambda date-time format should set full precision";
    
    // Test Lambda time-only format
    dt = datetime_parse_lambda(pool, "t'14:30:45'");
    EXPECT_NE(dt, nullptr) << "Lambda time-only format should parse successfully";
    EXPECT_EQ(dt->hour, 14) << "Lambda time-only hour should be parsed correctly";
    EXPECT_EQ(dt->minute, 30) << "Lambda time-only minute should be parsed correctly";
    EXPECT_EQ(dt->second, 45) << "Lambda time-only second should be parsed correctly";
    EXPECT_EQ(dt->precision, DATETIME_PRECISION_TIME_ONLY) << "Lambda time-only format should set time-only precision";
}

// Test 16: Precision aware formatting
TEST_F(DateTimeTest, PrecisionAwareFormatting) {
    DateTime* dt = datetime_new(pool);
    EXPECT_NE(dt, nullptr);
    
    // Set up base DateTime
    DATETIME_SET_YEAR_MONTH(dt, 2025, 8);
    dt->day = 12;
    dt->hour = 14;
    dt->minute = 30;
    dt->second = 45;
    dt->millisecond = 123;
    
    StrBuf* strbuf = strbuf_new();
    
    // Test date-only precision formatting
    dt->precision = DATETIME_PRECISION_DATE_ONLY;
    datetime_format_iso8601(strbuf, dt);
    EXPECT_NE(strbuf->str, nullptr) << "Date-only formatting should succeed";
    EXPECT_STREQ(strbuf->str, "2025-08-12") << "Date-only formatting should exclude time";
    
    // Test year-only precision formatting  
    strbuf_reset(strbuf);
    dt->precision = DATETIME_PRECISION_YEAR_ONLY;
    datetime_format_iso8601(strbuf, dt);
    EXPECT_NE(strbuf->str, nullptr) << "Year-only formatting should succeed";
    EXPECT_STREQ(strbuf->str, "2025") << "Year-only formatting should show only year";
    
    // Test time-only precision formatting
    strbuf_reset(strbuf);
    dt->precision = DATETIME_PRECISION_TIME_ONLY;
    datetime_format_iso8601(strbuf, dt);
    EXPECT_NE(strbuf->str, nullptr) << "Time-only formatting should succeed";
    EXPECT_STREQ(strbuf->str, "14:30:45.123") << "Time-only formatting should exclude date";
    
    // Test full precision formatting
    strbuf_reset(strbuf);
    dt->precision = DATETIME_PRECISION_DATE_TIME;
    DATETIME_SET_TZ_OFFSET(dt, 0);
    dt->format_hint = DATETIME_FORMAT_ISO8601_UTC;
    datetime_format_iso8601(strbuf, dt);
    EXPECT_NE(strbuf->str, nullptr) << "Full precision formatting should succeed";
    EXPECT_STREQ(strbuf->str, "2025-08-12T14:30:45.123Z") << "Full precision formatting should include everything";
    
    strbuf_free(strbuf);
    
    // Test NULL DateTime validation
    EXPECT_FALSE(datetime_is_valid(nullptr)) << "NULL DateTime should be invalid";
}
