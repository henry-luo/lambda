// Test: DateTime sub-types and member properties

// Sub-type checks: is date
"Sub-type checks"
(t'2025-04-26' is date)
(t'2025' is date)
(t'10:30:00' is date)
(t'2025-04-26T10:30:00' is date)

// Sub-type checks: is time
(t'10:30:00' is time)
(t'10:30:00z' is time)
(t'2025-04-26' is time)
(t'2025-04-26T10:30:00' is time)

// Sub-type checks: is datetime (all datetime values match)
(t'2025-04-26' is datetime)
(t'10:30:00' is datetime)
(t'2025-04-26T10:30:00' is datetime)

// Member properties - date components
"Date properties"
(let dt = t'2025-04-26T10:30:45', dt.year)
(let dt = t'2025-04-26T10:30:45', dt.month)
(let dt = t'2025-04-26T10:30:45', dt.day)

// Member properties - time components
"Time properties"
(let dt = t'2025-04-26T10:30:45', dt.hour)
(let dt = t'2025-04-26T10:30:45', dt.minute)
(let dt = t'2025-04-26T10:30:45', dt.second)
(let dt = t'2025-04-26T10:30:45.123', dt.millisecond)

// Member properties - computed
"Computed properties"
(let dt = t'2025-04-26T10:30:45', dt.weekday)
(let dt = t'2025-01-01T00:00:00', dt.yearday)
(let dt = t'2025-04-26T10:30:45', dt.week)
(let dt = t'2025-04-26T10:30:45', dt.quarter)
(let dt = t'2024-02-29', dt.is_leap_year)
(let dt = t'2025-02-28', dt.is_leap_year)
(let dt = t'2024-02-29', dt.days_in_month)

// Timezone offset in minutes
"Timezone offset"
(let dt = t'2025-04-26T10:30:00+05:30', dt.timezone)
(let dt = t'2025-04-26T10:30:00-08:00', dt.timezone)
(let dt = t'2025-04-26T10:30:00z', dt.timezone)

// Unix timestamp
"Unix timestamp"
(let dt = t'1970-01-01T00:00:00z', dt.unix)
(t'2025-04-26T10:30:45z'.unix > 0)

// Precision checks
"Precision checks"
(let dt = t'2025-04-26', dt.is_date)
(let dt = t'10:30:00', dt.is_time)
(let dt = t'2025-04-26T10:30:00', dt.is_date)
(let dt = t'2025-04-26T10:30:00', dt.is_time)

// UTC checks
"UTC checks"
(let dt = t'2025-04-26T10:30:00z', dt.is_utc)
(let dt = t'2025-04-26T10:30:00+05:00', dt.is_utc)

// UTC offset
"UTC offset"
(let dt = t'2025-04-26T10:30:00+05:30', dt.utc_offset)
(let dt = t'2025-04-26T10:30:00z', dt.utc_offset)

// Comparison (cross-timezone)
"Comparison"
(t'2025-04-26T10:30:00z' == t'2025-04-26T10:30:00+00:00')
(t'2025-04-26T10:30:00z' < t'2025-04-26T11:30:00z')
(t'2025-04-26T10:30:00+05:00' < t'2025-04-26T10:30:00z')

// Extraction properties
"Extraction properties"
(let dt = t'2025-04-26T10:30:45', dt.date)
(let dt = t'2025-04-26T10:30:45', dt.date is date)
(let dt = t'2025-04-26T10:30:45', dt.time)
(let dt = t'2025-04-26T10:30:45', dt.time is time)
(let dt = t'2025-04-26T10:30:00+05:00', dt.utc)

// Constructors
"Constructors"
(date(2025, 4, 26) == t'2025-04-26')
(time(10, 30, 45) == t'10:30:45')
(date(2025, 4, 26) is date)
(time(10, 30, 45) is time)

// String conversion - verify string() returns expected values
"String conversion"
(string(t'2025-04-26') == "t'2025-04-26'")
(string(t'10:30:00') == "t'10:30:00'")

// Format function - verify format() returns expected values
"Format function"
(format(t'2025-04-26', "YYYY/MM/DD") == "2025/04/26")
(format(t'2025-04-26T10:30:45', "date") == "2025-04-26")
(format(t'2025-04-26T10:30:45', "time") == "10:30:45")

// ===== CORNER CASES =====

// Leap year rules
"Leap year rules"
(t'2024-01-01'.is_leap_year)      // 2024 divisible by 4 → leap
(t'2000-01-01'.is_leap_year)      // 2000 divisible by 400 → leap
(t'1900-01-01'.is_leap_year)      // 1900 divisible by 100 but not 400 → not leap
(t'2100-01-01'.is_leap_year)      // 2100 divisible by 100 but not 400 → not leap
(t'2025-01-01'.is_leap_year)      // 2025 not divisible by 4 → not leap

// Days in month - all months
"Days in month"
(t'2025-01-15'.days_in_month)     // January: 31
(t'2025-02-15'.days_in_month)     // February (non-leap): 28
(t'2024-02-15'.days_in_month)     // February (leap): 29
(t'2025-04-15'.days_in_month)     // April: 30
(t'2025-12-15'.days_in_month)     // December: 31

// Year day boundary cases
"Year day boundaries"
(t'2025-01-01'.yearday)           // Jan 1 = day 1
(t'2025-12-31'.yearday)           // Dec 31 non-leap = day 365
(t'2024-12-31'.yearday)           // Dec 31 leap year = day 366
(t'2024-02-29'.yearday)           // Feb 29 leap year = day 60
(t'2025-03-01'.yearday)           // Mar 1 non-leap = day 60

// Week number edge cases (ISO 8601)
"Week number"
(t'2025-01-01'.week)              // Jan 1, 2025 = week 1
(t'2024-12-30'.week)              // Dec 30, 2024 = week 1 of 2025
(t'2024-12-29'.week)              // Dec 29, 2024 = week 52
(t'2020-12-31'.week)              // Dec 31, 2020 = week 53

// Weekday verification (known dates)
"Weekday verification"
(t'2025-01-01'.weekday)           // Jan 1, 2025 = Wednesday = 3
(t'2024-07-04'.weekday)           // July 4, 2024 = Thursday = 4
(t'2000-01-01'.weekday)           // Jan 1, 2000 = Saturday = 6
(t'1970-01-01'.weekday)           // Jan 1, 1970 (Unix epoch) = Thursday = 4

// Quarter boundaries
"Quarter boundaries"
(t'2025-01-01'.quarter)           // Jan = Q1
(t'2025-03-31'.quarter)           // Mar = Q1
(t'2025-04-01'.quarter)           // Apr = Q2
(t'2025-07-01'.quarter)           // Jul = Q3
(t'2025-10-01'.quarter)           // Oct = Q4
(t'2025-12-31'.quarter)           // Dec = Q4

// Midnight and edge times
"Time boundaries"
(t'00:00:00'.hour)                // Midnight hour
(t'23:59:59'.hour)                // Last hour
(t'23:59:59'.minute)              // Last minute
(t'23:59:59'.second)              // Last second
(t'12:00:00.999'.millisecond)     // Max millisecond

// Year boundaries
"Year boundaries"
(t'0001-01-01'.year)              // Year 1 AD
(t'2999-12-31'.year)              // Far future (within range)

// Extraction preserves components
"Extraction preserves components"
(t'2024-02-29T23:59:59'.date.day)     // Feb 29 preserved
(t'2024-02-29T23:59:59'.date.month)   // Month preserved
(t'2024-02-29T23:59:59'.time.hour)    // Hour preserved
(t'2024-02-29T23:59:59'.time.second)  // Second preserved
