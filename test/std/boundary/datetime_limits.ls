// Test: DateTime Limits
// Layer: 3 | Category: boundary | Covers: year extremes, leap years, edge timestamps

// ===== Basic datetime creation =====
let d1 = t'2024-01-01'
d1.year
d1.month
d1.day

// ===== Leap year Feb 29 =====
let leap = t'2024-02-29'
leap.month
leap.day
leap.is_leap_year

// ===== Non-leap year =====
let non_leap = t'2023-01-01'
non_leap.is_leap_year

// ===== Century leap year rule (2000 is leap) =====
let y2000 = t'2000-02-29'
y2000.is_leap_year
y2000.day

// ===== End of year =====
let eoy = t'2024-12-31'
eoy.month
eoy.day
eoy.day_of_year

// ===== Start of year =====
let soy = t'2024-01-01'
soy.day_of_year

// ===== Midnight vs noon =====
let midnight = t'2024-01-01T00:00:00'
midnight.hour
midnight.minute
midnight.second

let noon = t'2024-01-01T12:00:00'
noon.hour

// ===== End of day =====
let eod = t'2024-01-01T23:59:59'
eod.hour
eod.minute
eod.second

// ===== DateTime comparison =====
let earlier = t'2024-01-01'
let later = t'2024-12-31'
earlier < later
later > earlier
earlier == earlier

// ===== Weekday boundary =====
// 2024-01-01 is Monday
let mon = t'2024-01-01'
mon.weekday

// 2024-01-07 is Sunday
let sun = t'2024-01-07'
sun.weekday

// ===== Quarter boundaries =====
t'2024-01-01'.quarter
t'2024-04-01'.quarter
t'2024-07-01'.quarter
t'2024-10-01'.quarter

// ===== Time-only =====
let t1 = t'10:30:00'
t1.hour
t1.minute
t1 is time

// ===== Date-only vs datetime =====
let date_only = t'2024-06-15'
date_only is date
let datetime_full = t'2024-06-15T10:30:00'
datetime_full is datetime
