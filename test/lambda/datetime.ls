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
