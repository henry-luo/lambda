// Test: Cross-Type DateTime
// Layer: 3 | Category: boundary | Covers: datetime comparison, in collection, conversion

// ===== DateTime comparison =====
let d1 = t'2024-01-01'
let d2 = t'2024-06-15'
let d3 = t'2024-12-31'
d1 < d2
d2 < d3
d3 > d1
d1 == d1
d1 != d2

// ===== DateTime equals same date =====
let a = t'2024-03-15'
let b = t'2024-03-15'
a == b

// ===== DateTime in array =====
let dates = [t'2024-01-01', t'2024-06-15', t'2024-12-31']
len(dates)
dates[0].year
dates[1].month
dates[2].day

// ===== DateTime type check =====
d1 is date
d1 is int
d1 is string

// ===== DateTime to string =====
str(d1)

// ===== DateTime in map =====
let events = {
    start: t'2024-01-01',
    end: t'2024-12-31'
}
events.start.year
events.end.month

// ===== DateTime sorting =====
let unsorted = [t'2024-12-01', t'2024-01-15', t'2024-06-30']
unsorted | sort()

// ===== DateTime filter =====
let year_dates = [t'2023-06-01', t'2024-03-15', t'2024-09-01', t'2025-01-01']
year_dates | filter((d) => d.year == 2024)

// ===== Time comparison =====
let t1 = t'08:00:00'
let t2 = t'12:00:00'
let t3 = t'20:00:00'
t1 < t2
t2 < t3

// ===== DateTime in conditional =====
let today = t'2024-06-15'
if (today.month >= 6 and today.month <= 8) "summer" else "not summer"

// ===== DateTime properties as ints =====
let dt = t'2024-03-15T14:30:45'
dt.year + dt.month + dt.day
dt.hour * 60 + dt.minute
