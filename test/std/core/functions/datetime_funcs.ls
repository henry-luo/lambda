// Test: DateTime Functions
// Layer: 2 | Category: function | Covers: datetime(), date(), time(), today(), format()

// ===== Constructors =====
datetime(2025, 4, 26)
date(2025, 4, 26)
time(10, 30, 45)

// ===== Type checks =====
(datetime(2025, 4, 26) is datetime)
(date(2025, 4, 26) is date)
(time(10, 30, 45) is time)

// ===== today() returns date =====
(today() is date)
(today().year >= 2025)

// ===== Member access on constructed =====
let d = date(2025, 4, 26)
d.year
d.month
d.day

let t = time(10, 30, 45)
t.hour
t.minute
t.second

// ===== format =====
t'2025-04-26T10:30:45'.format("YYYY-MM-DD")
t'2025-04-26T10:30:45'.format("HH:mm:ss")

// ===== Comparison operations =====
(date(2025, 1, 1) < date(2025, 12, 31))
(time(10, 0, 0) < time(12, 0, 0))
