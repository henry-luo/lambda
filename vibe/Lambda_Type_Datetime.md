# Lambda DateTime Enhancement Proposal

> **Status**: Proposal  
> **Date**: 2026-02-09  
> **Related**: [Lambda Data](../doc/Lambda_Data.md), [Lambda Type System](../doc/Lambda_Type.md), [Lambda System Functions](../doc/Lambda_Sys_Func.md)

---

## Table of Contents

1. [Motivation](#motivation)
2. [Current State Analysis](#current-state-analysis)
3. [Proposal: Sub-Types `date` and `time`](#proposal-sub-types-date-and-time)
4. [Proposal: Constructor Functions](#proposal-constructor-functions)
5. [Proposal: Member Properties](#proposal-member-properties)
6. [Proposal: Member Functions](#proposal-member-functions)
7. [Proposal: Formatting](#proposal-formatting)
8. [Proposal: Parsing Enhancements](#proposal-parsing-enhancements)
9. [Known Bugs & Fixes](#known-bugs--fixes)
10. [Implementation Plan](#implementation-plan)
11. [Test Plan](#test-plan)
12. [Phase 2: Arithmetic & Duration (Future)](#phase-2-arithmetic--duration-future)

---

## Motivation

Lambda's datetime support is currently minimal — it has a compact 64-bit runtime representation (`DateTime` struct in `lib/datetime.h`) with four precision levels, but exposes very few operations to user scripts. Real-world data processing frequently involves date/time manipulation: filtering by date ranges, extracting year/month/day components, formatting for output, computing durations between events, etc.

**Goals** (Phase 1):
1. Introduce `date` and `time` as language-level sub-types of `datetime`, allowing type-safe APIs and clearer intent.
2. Provide rich constructor functions to create datetime values from components, strings, and timestamps.
3. Expose member properties (`.year`, `.month`, `.day`, `.hour`, etc.) for field extraction.
4. Add member functions for formatting and conversion.
5. Fix existing bugs in datetime comparison, output, and parsing.

> **Phase 2** (future): DateTime arithmetic (`+`, `-`, `diff`), duration type, and calendar math. See [Phase 2](#phase-2-arithmetic--duration-future).

---

## Current State Analysis

### Runtime Representation (`lib/datetime.h`)

The `DateTime` struct is a 64-bit bitfield-packed value type (no heap allocation needed):

| Field | Bits | Range | Purpose |
|-------|------|-------|---------|
| `year_month` | 17 | years −4000..+4191, months 0–12 | Packed `(year+4000)*16 + month` |
| `day` | 5 | 0–31 | Day of month (0 = unset) |
| `hour` | 5 | 0–23 | Hour |
| `minute` | 6 | 0–59 | Minute |
| `second` | 6 | 0–59 | Second |
| `millisecond` | 10 | 0–999 | Millisecond |
| `tz_offset_biased` | 11 | ±1023 minutes | Timezone offset (0 = no timezone) |
| `precision` | 2 | 4 levels | `YEAR_ONLY`, `DATE_ONLY`, `TIME_ONLY`, `DATE_TIME` |
| `format_hint` | 2 | 4 combos | ISO8601, Human, with/without UTC |

**Key property**: The `precision` field already distinguishes date-only vs time-only vs full-datetime values at runtime. This is the foundation for the proposed sub-types.

### Existing Literals

```lambda
t'2025-04-26'           // DATE_ONLY precision
t'10:30:45'             // TIME_ONLY precision
t'2025-04-26T10:30:45'  // DATE_TIME precision
t'2025'                 // YEAR_ONLY precision
```

### Existing Functions

| Function | Args | Returns | Description |
|----------|------|---------|-------------|
| `datetime()` | 0 | datetime | Current date and time (UTC) |
| `datetime(x)` | 1 | datetime | Parse string as datetime |
| `today()` | 0 | datetime | Current date (DATE_ONLY precision) |
| `justnow()` | 0 | datetime | Current time (TIME_ONLY precision) |
| `date(dt)` | 1 | datetime | Extract date part from datetime |
| `time(dt)` | 1 | datetime | Extract time part from datetime |

### Existing C Library Functions (in `lib/datetime.c`)

Available but **not yet exposed** to Lambda scripts:
- `datetime_to_unix(dt)` / `datetime_from_unix(pool, timestamp)` — Unix timestamp conversion
- `datetime_add_seconds(pool, dt, seconds)` — Add seconds to datetime
- `datetime_to_utc(pool, dt)` / `datetime_to_local(pool, dt)` — Timezone conversion (stubs)
- `datetime_compare(dt1, dt2)` — Proper comparison
- `datetime_is_valid(dt)` — Validation
- Multiple format parsers: ISO8601, ICS, RFC2822, Lambda format

### Known Gaps

1. **No sub-field access** — Cannot extract `.year`, `.month`, `.day`, etc.
2. **No formatting control** — Only Lambda's default format; no user-specified format strings.
3. **No component constructors** — Cannot build a datetime from `(year, month, day)`.
4. **Comparison is bitwise** — `fn_lt`/`fn_gt` compare `int64_val` directly, which breaks for cross-timezone comparisons.
5. **Thread safety** — `fn_datetime()` uses a static variable.
6. **No arithmetic** — Cannot add/subtract durations or compute differences. *(Phase 2)*
7. **No duration type** — No way to represent or compute time spans. *(Phase 2)*

---

## Proposal: Sub-Types `date` and `time`

### Design

Introduce `date` and `time` as **language-level sub-types** of `datetime`. At runtime, all three are `LMD_TYPE_DTIME` — the distinction is carried by the `precision` field of the `DateTime` bitfield.

```
         datetime
        /        \
     date        time
```

### Type Relationships

```lambda
// Type hierarchy
date <: datetime       // date is a sub-type of datetime
time <: datetime       // time is a sub-type of datetime

// Type checking
t'2025-04-26' is date        // true  (DATE_ONLY precision)
t'2025-04-26' is datetime    // true  (date <: datetime)
t'10:30:00' is time          // true  (TIME_ONLY precision)
t'10:30:00' is datetime      // true  (time <: datetime)
t'2025-04-26T10:30' is date  // false (DATE_TIME precision)
t'2025-04-26T10:30' is time  // false (DATE_TIME precision)
```

### Type Names in Annotations

```lambda
fn format_date(d: date) => ...         // accepts date and datetime (via coercion)
fn format_time(t: time) => ...         // accepts time and datetime (via coercion)
fn schedule(dt: datetime) => ...       // accepts date, time, or datetime

// Function return types
fn get_birthday(user): date => ...
fn get_alarm(name): time => ...
```

### Implementation Approach

**No new `TypeId` enum values needed.** The `date` and `time` sub-types are resolved via:

1. **Type-level**: Add `TYPE_DATE` and `TYPE_TIME` as special `Type` instances that carry `LMD_TYPE_DTIME` as the `type_id` but are tagged with precision metadata.
2. **`is` operator**: When checking `x is date`, inspect the `precision` field of the `DateTime` value:
   - `date` matches `DATETIME_PRECISION_DATE_ONLY` or `DATETIME_PRECISION_YEAR_ONLY`
   - `time` matches `DATETIME_PRECISION_TIME_ONLY`
   - `datetime` matches all precisions
3. **Grammar**: Register `date` and `time` as type names in the grammar (they already exist as function names; add them to the type keyword list).
4. **Type coercion**: A `datetime` can be implicitly narrowed to `date` (drops time) or `time` (drops date) when passed to a function expecting a sub-type, via the existing `date()` and `time()` extraction functions.

### AST / Build Changes

In `build_ast.cpp`, the type name resolution for `"date"` and `"time"` should produce `Type` descriptors with `type_id == LMD_TYPE_DTIME` plus a precision tag:

```cpp
// pseudo-code for type resolution
if (name == "date")     return &TYPE_DATE;   // LMD_TYPE_DTIME + DATE_ONLY
if (name == "time")     return &TYPE_TIME;   // LMD_TYPE_DTIME + TIME_ONLY
if (name == "datetime") return &TYPE_DTIME;  // LMD_TYPE_DTIME + any precision
```

---

## Proposal: Constructor Functions

### `datetime(...)` — Full Constructor

```lambda
// Existing (unchanged)
datetime()                           // current date and time (UTC)
datetime("2025-04-26T10:30:00")      // parse ISO 8601 string

// New: component constructor
datetime(2025, 4, 26)                // date only: t'2025-04-26'
datetime(2025, 4, 26, 10, 30)        // date + time: t'2025-04-26 10:30'
datetime(2025, 4, 26, 10, 30, 45)    // full: t'2025-04-26 10:30:45'

// New: from unix timestamp
datetime(1714100000)                 // from unix timestamp (int → datetime)

// New: from map
datetime({year: 2025, month: 4, day: 26, hour: 10, minute: 30})
```

### `date(...)` — Date Constructor

```lambda
// Existing
date(some_datetime)          // extract date part

// New: component constructor
date(2025, 4, 26)            // t'2025-04-26'
date(2025, 4)                // t'2025-04'   (year-month)
date(2025)                   // t'2025'      (year only)

// New: from string
date("2025-04-26")           // parse date string

// New: current date
date()                       // same as today()
```

### `time(...)` — Time Constructor

```lambda
// Existing
time(some_datetime)          // extract time part

// New: component constructor
time(10, 30)                 // t'10:30'
time(10, 30, 45)             // t'10:30:45'
time(10, 30, 45, 500)        // t'10:30:45.500'

// New: from string
time("10:30:45")             // parse time string

// New: current time
time()                       // same as justnow()
```

### Implementation Notes

The constructors use **arity overloading** (already supported in the sys func table):
- 0 args → current date/time
- 1 arg → parse (string) or extract (datetime) or from-unix (int)
- 2+ args → component constructor

Dispatch in the transpiler/evaluator:

```
datetime(0 args)  → fn_datetime()     (existing)
datetime(1 arg)   → fn_datetime(x)    (existing, extend: if int, treat as unix timestamp)
datetime(3 args)  → fn_datetime3(y, m, d)
datetime(5 args)  → fn_datetime5(y, m, d, h, min)
datetime(6 args)  → fn_datetime6(y, m, d, h, min, s)

date(0 args)      → fn_date0()        (new: today())
date(1 arg)       → fn_date(x)        (existing: extract date from datetime; extend: parse string, or from int)
date(2 args)      → fn_date2(y, m)
date(3 args)      → fn_date3(y, m, d)

time(0 args)      → fn_time0()        (new: justnow())
time(1 arg)       → fn_time(x)        (existing: extract time from datetime; extend: parse string)
time(2 args)      → fn_time2(h, m)
time(3 args)      → fn_time3(h, m, s)
time(4 args)      → fn_time4(h, m, s, ms)
```

---

## Proposal: Member Properties

Member properties allow field extraction using dot notation on datetime values. These are **read-only** computed properties resolved at compile time or via runtime dispatch.

### Date Properties

```lambda
let dt = t'2025-04-26T10:30:45.123+05:30'

dt.year          // 2025       (int)
dt.month         // 4          (int, 1–12)
dt.day           // 26         (int, 1–31)
dt.weekday       // 6          (int, 0=Sunday, 6=Saturday)
dt.yearday       // 116        (int, 1–366, day of year)
dt.week          // 17         (int, ISO week number 1–53)
dt.quarter       // 2          (int, 1–4)
```

### Time Properties

```lambda
dt.hour          // 10         (int, 0–23)
dt.minute        // 30         (int, 0–59)
dt.second        // 45         (int, 0–59)
dt.millisecond   // 123        (int, 0–999)
```

### Timezone Properties

```lambda
dt.timezone      // 330        (int, offset in minutes from UTC)
dt.utc_offset    // '+05:30'   (symbol, formatted offset)
dt.is_utc        // false      (bool)
```

### Meta Properties

```lambda
dt.unix          // 1745635845123 (int, Unix timestamp in milliseconds)
dt.is_date       // true       (bool, has date component)
dt.is_time       // true       (bool, has time component)
dt.is_leap_year  // false      (bool)
dt.days_in_month // 30         (int, days in the datetime's month)
```

### Implementation Approach

Member access on `LMD_TYPE_DTIME` values is resolved in `fn_member()` / `item_attr()`:

```cpp
// in lambda-data-runtime.cpp or lambda-eval.cpp
Item datetime_member(DateTime* dt, const char* key) {
    if (strcmp(key, "year") == 0)        return i2it(DATETIME_GET_YEAR(dt));
    if (strcmp(key, "month") == 0)       return i2it(DATETIME_GET_MONTH(dt));
    if (strcmp(key, "day") == 0)         return i2it(dt->day);
    if (strcmp(key, "hour") == 0)        return i2it(dt->hour);
    if (strcmp(key, "minute") == 0)      return i2it(dt->minute);
    if (strcmp(key, "second") == 0)      return i2it(dt->second);
    if (strcmp(key, "millisecond") == 0) return i2it(dt->millisecond);
    if (strcmp(key, "unix") == 0)        return push_l(datetime_to_unix_ms(dt));
    if (strcmp(key, "weekday") == 0)     return i2it(datetime_weekday(dt));
    if (strcmp(key, "timezone") == 0)    return i2it(DATETIME_GET_TZ_OFFSET(dt));
    if (strcmp(key, "utc_offset") == 0)  return y2it(datetime_utc_offset_symbol(dt));
    // ... etc
    return ITEM_NULL;
}
```

This follows the same pattern as `Path` member access (e.g., `path.scheme`). The member name is an interned symbol, so comparison can use pointer equality for performance.

---

## Proposal: Member Functions

Member functions are called with method syntax via the pipe operator or dot-call syntax.

### Formatting

```lambda
let dt = t'2025-04-26T10:30:45'

// format with pattern string
dt.format("YYYY-MM-DD")              // "2025-04-26"
dt.format("DD/MM/YYYY")              // "26/04/2025"
dt.format("hh:mm A")                 // "10:30 AM"
dt.format("YYYY-MM-DD hh:mm:ss")     // "2025-04-26 10:30:45"
dt.format("MMM DD, YYYY")            // "Apr 26, 2025"

// predefined format names (symbol argument)
dt.format('iso)                   // "2025-04-26T10:30:45"
dt.format('human')                 // "April 26, 2025 10:30 AM"
dt.format('date')                  // "2025-04-26"
dt.format('time')                  // "10:30:45"
```

**Format pattern tokens**:

| Token | Meaning | Example |
|-------|---------|---------|
| `YYYY` | 4-digit year | `2025` |
| `YY` | 2-digit year | `25` |
| `MM` | 2-digit month | `04` |
| `M` | Month without padding | `4` |
| `MMM` | Abbreviated month name | `Apr` |
| `MMMM` | Full month name | `April` |
| `DD` | 2-digit day | `26` |
| `D` | Day without padding | `26` |
| `ddd` | Abbreviated weekday | `Sat` |
| `dddd` | Full weekday | `Saturday` |
| `hh` | 2-digit hour (24h) | `10` |
| `h` | Hour without padding | `10` |
| `HH` | 2-digit hour (12h) | `10` |
| `mm` | 2-digit minute | `30` |
| `ss` | 2-digit second | `45` |
| `SSS` | 3-digit millisecond | `000` |
| `A` | AM/PM | `AM` |
| `Z` | UTC offset | `+00:00` |
| `ZZ` | UTC offset compact | `+0000` |

### Extraction & Conversion

```lambda
let dt = t'2025-04-26T10:30:45+05:30'

dt.date()                // t'2025-04-26'     (extract date part — existing)
dt.time()                // t'10:30:45'       (extract time part — existing)
dt.utc()                 // t'2025-04-26T05:00:45Z' (convert to UTC)
dt.local()               // convert to local timezone
```

### Implementation Approach

Member functions are dispatched through the existing method call mechanism. When the transpiler sees `dt.format(...)` and `dt` has type `LMD_TYPE_DTIME`, it resolves to a system function call:

```
dt.format(pattern)    → fn_dt_format(dt, pattern)
dt.utc()              → fn_dt_utc(dt)
dt.local()            → fn_dt_local(dt)
dt.date()             → fn_date(dt)       (existing)
dt.time()             → fn_time(dt)       (existing)
```

This uses the same mechanism that already exists for `date(dt)` and `time(dt)` as "method-eligible" system functions (the `is_method` flag in the sys func table).

> **Phase 2**: Arithmetic member functions (`.add_days()`, `.add_months()`, etc.), component replacement (`.with()`), and boundary functions (`.start_of()`, `.end_of()`) will be added in Phase 2.

---

## Proposal: Formatting

### `format()` System Function Extension

Currently `format(item)` / `format(item, options)` exists for general formatting. Extend it with datetime-specific behavior:

```lambda
// Symbol selects predefined format
format(dt, 'iso)            // "2025-04-26T10:30:45"
format(dt, 'human)          // "April 26, 2025 10:30 AM"
format(dt, 'date)           // "2025-04-26"
format(dt, 'time)           // "10:30:45"

// String for custom pattern
format(dt, "YYYY/MM/DD")    // "2025/04/26"
format(dt, "hh:mm A")       // "10:30 AM"
```

### String Interpolation

DateTime values should format cleanly when converted to string (in `fn_string()`):

```lambda
let dt = t'2025-04-26T10:30:45'
"Meeting on " ++ string(dt)          // "Meeting on 2025-04-26 10:30:45"
"Date: " ++ string(date(dt))        // "Date: 2025-04-26"
"Time: " ++ string(time(dt))        // "Time: 10:30:45"
```

### JSON Output

When outputting datetime to JSON, format as ISO 8601 string:

```lambda
let data = {event: "launch", when: t'2025-04-26T10:30:00Z'}
format(data, 'json)
// {"event": "launch", "when": "2025-04-26T10:30:00Z"}
```

---

## Proposal: Parsing Enhancements

### Multi-Format Parsing

```lambda
// Auto-detect format (existing behavior, enhanced)
datetime("2025-04-26")                    // ISO date
datetime("2025-04-26T10:30:00")           // ISO datetime
datetime("Sat, 26 Apr 2025 10:30:00 +0000")  // RFC 2822
datetime("20250426T103045Z")              // ICS format

// Explicit format hint
datetime("04/26/2025", "MM/DD/YYYY")     // US date format
datetime("26-Apr-2025", "DD-MMM-YYYY")   // Custom format
```

### Parsing with Validation

```lambda
// Returns error on invalid input (using Lambda's error handling)
let result = datetime("not-a-date")   // returns error value
let valid = datetime("2025-13-01")    // returns error (month 13 invalid)

// Type-safe parsing
let d: date^ = date("2025-04-26")    // date^error return type
```

---

## Known Bugs & Fixes

The following bugs were identified during analysis and should be fixed alongside the enhancements:

### 1. Bitwise Comparison (Critical)

**Location**: `lambda-eval.cpp` — `fn_lt`, `fn_gt`, `fn_le`, `fn_ge`

**Bug**: Compares `DateTime.int64_val` directly. Due to bitfield packing order, this does NOT produce correct chronological ordering when timezone offsets differ.

**Fix**: Use `datetime_compare()` from `lib/datetime.c` which normalizes to UTC before comparing.

### 2. Thread Safety in `fn_datetime()`

**Location**: `lambda-eval.cpp`

**Bug**: Uses `static DateTime` for the return value. Not thread-safe.

**Fix**: Allocate on `num_stack` via `push_k()` instead of using a static.

### 3. `print_typeditem` Formatting

**Location**: `lambda/print.cpp`

**Bug**: For `LMD_TYPE_DTIME`, prints raw `int64_val` instead of formatted datetime string.

**Fix**: Format through `datetime_format_lambda()` like `item_to_string()` does.

### 4. YAML Output

**Location**: `lambda/format/format-yaml.cpp`

**Bug**: `LMD_TYPE_DTIME` case falls through to string handling, treating the DateTime pointer as a String pointer.

**Fix**: Add proper `LMD_TYPE_DTIME` case that formats via `datetime_format_iso8601()`.

### 5. Mark Input DateTime Parsing

**Location**: `lambda/input/input-mark.cpp`

**Bug**: Parses `t'...'` datetime literals but returns them as `LMD_TYPE_STRING` Items.

**Fix**: Parse through `datetime_parse_lambda()` and return as `LMD_TYPE_DTIME`.

---

## Implementation Plan

This plan covers **Phase 1** only — datetime parsing, formatting, member access, sub-types, and bug fixes.

### Step 1: Bug Fixes & Foundation (Priority: High)

1. Fix datetime comparison to use `datetime_compare()`
2. Fix `fn_datetime()` thread safety
3. Fix `print_typeditem` datetime formatting
4. Fix YAML output datetime handling
5. Fix Mark input datetime parsing
6. Add `datetime_weekday()`, `datetime_yearday()`, `datetime_week_number()` to `lib/datetime.c`
7. Add `datetime_to_unix_ms()` to `lib/datetime.c`

### Step 2: Member Properties (Priority: High)

1. Implement `datetime_member()` dispatch in `fn_member()` / `item_attr()`
2. Add all date properties: `.year`, `.month`, `.day`, `.weekday`, `.yearday`, `.week`, `.quarter`
3. Add all time properties: `.hour`, `.minute`, `.second`, `.millisecond`
4. Add meta properties: `.unix` (milliseconds), `.is_date`, `.is_time`, `.is_utc`, `.is_leap_year`, `.days_in_month`
5. Add timezone properties: `.timezone` (int, minutes), `.utc_offset` (symbol)

### Step 3: Sub-Types `date` and `time` (Priority: Medium)

1. Define `TYPE_DATE` and `TYPE_TIME` type descriptors
2. Register `date` and `time` as type names in grammar/AST builder
3. Implement `is date` / `is time` checks based on precision field
4. Update type inference: `date()` returns `date`, `time()` returns `time`
5. Update documentation

### Step 4: Constructor Enhancements (Priority: Medium)

1. Extend `datetime()` with multi-arg component constructor
2. Extend `date()` with component constructor and 0-arg form
3. Extend `time()` with component constructor and 0-arg form
4. Add unix timestamp support: `datetime(unix_int)`
5. Add map constructor: `datetime({year: ..., month: ...})`

### Step 5: Formatting & Member Functions (Priority: Medium)

1. Implement format pattern parsing and `fn_dt_format()`
2. Extend `format()` system function with datetime-specific options (`'iso`, `'human`, `'date`, `'time`)
3. Fix JSON output for datetime values
4. Implement `.format(pattern)`, `.utc()`, `.local()` member functions

---

## Test Plan

### Unit Tests (`test/test_datetime_gtest.cpp` — extend existing)

```
DateTimeTest.MemberProperties       — .year, .month, .day, .hour, etc.
DateTimeTest.WeekdayCalculation     — .weekday for known dates
DateTimeTest.YeardayCalculation     — .yearday boundary cases
DateTimeTest.UnixTimestampMs        — .unix returns milliseconds, round-trip
DateTimeTest.ComparisonCrossTZ      — compare datetimes in different timezones
DateTimeTest.FormatPattern          — custom format patterns
DateTimeTest.FormatPredefined       — 'iso, 'human, 'date, 'time
DateTimeTest.ComponentConstructor   — datetime(y, m, d, h, min, s)
DateTimeTest.UtcOffsetSymbol        — .utc_offset returns symbol '+05:30, '+00:00, etc.
```

### Lambda Script Tests (`test/lambda/`)

```lambda
// test_datetime_types.ls — sub-type checks
(t'2025-04-26' is date)                    // true
(t'10:30:00' is time)                      // true
(t'2025-04-26T10:30' is datetime)          // true
(t'2025-04-26' is datetime)                // true (date <: datetime)
(t'2025-04-26T10:30' is date)              // false

// test_datetime_properties.ls — member access
(let dt = t'2025-04-26T10:30:45', dt.year)     // 2025
(let dt = t'2025-04-26T10:30:45', dt.month)    // 4
(let dt = t'2025-04-26T10:30:45', dt.day)      // 26
(let dt = t'2025-04-26T10:30:45', dt.hour)     // 10
(let dt = t'2025-04-26T10:30:45', dt.minute)   // 30
(let dt = t'2025-04-26T10:30:45', dt.second)   // 45
(let dt = t'2025-04-26T10:30:45+05:30', dt.utc_offset)  // '+05:30

// test_datetime_constructors.ls — constructors
(date(2025, 4, 26) == t'2025-04-26')           // true
(time(10, 30, 45) == t'10:30:45')              // true
(datetime(2025, 4, 26, 10, 30) == t'2025-04-26 10:30') // true

// test_datetime_format.ls — formatting
(t'2025-04-26'.format("YYYY/MM/DD"))           // "2025/04/26"
(format(t'2025-04-26T10:30:45', 'iso))         // "2025-04-26T10:30:45"
(format(t'2025-04-26T10:30:45', 'human))       // "April 26, 2025 10:30 AM"
```

---

## Summary (Phase 1 Scope)

| Feature | Priority | Complexity | Dependencies |
|---------|----------|------------|--------------|
| Bug fixes (comparison, YAML, print, Mark) | High | Low | None |
| Member properties (.year, .month, .unix, etc.) | High | Medium | Bug fixes |
| Sub-types `date` and `time` | Medium | Medium | Type system |
| Constructor enhancements | Medium | Medium | None |
| Format patterns & predefined formats | Medium | Medium | New format parser |
| Member functions (.format, .utc, .local) | Medium | Medium | Member properties |

The overall design philosophy is: **datetime values are compact value types (64 bits) with rich access patterns**. No new runtime TypeId is needed — the `precision` bitfield in the existing `DateTime` struct already distinguishes date/time/datetime, and the language-level sub-types simply expose this distinction to the type system.

---

## Phase 2: Arithmetic & Duration (Future)

The following features are deferred to Phase 2:

### DateTime Arithmetic

```lambda
// Difference between datetimes
t'2025-04-26' - t'2025-04-20'          // 6 (days for date, seconds for datetime)

// Addition/subtraction with integers
t'2025-04-26' + 10                     // t'2025-05-06' (date + days)
t'10:30:00' + 3600                     // t'11:30:00' (time + seconds)
t'2025-04-26T10:30:00' + 86400         // t'2025-04-27T10:30:00'
```

### Arithmetic Member Functions

```lambda
dt.add_years(1)          dt.add_months(3)         dt.add_days(10)
dt.add_hours(5)          dt.add_minutes(45)       dt.add_seconds(3600)
```

### Timezone Conversion

```lambda
dt.with_timezone(540)            // set timezone offset (minutes)
dt.with_timezone("+09:00")       // set timezone by string
```

### Component Replacement

```lambda
dt.with(year: 2026)              // t'2026-04-26T10:30:00'
dt.with(month: 12, day: 25)     // t'2025-12-25T10:30:00'
```

### Range & Boundary

```lambda
dt.start_of('month)     // t'2025-04-01T00:00:00'
dt.end_of('month)       // t'2025-04-30T23:59:59.999'
```

### Duration Type

```lambda
duration(days: 3)                      // 3 days
duration(hours: 2, minutes: 30)        // 2h 30min
let span = diff(t'2025-04-26', t'2025-04-20')  // 6 days
meeting + duration(hours: 1, minutes: 30)      // datetime + duration
```
