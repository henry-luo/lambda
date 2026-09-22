// Test: DateTime Functions
// Layer: 2 | Category: function | Covers: datetime(), date(), time(), today(), format()
// today() reads the clock, so it is a pn (S12.1.1v2): the script runs as a pn main

pn show(value) {
    print(value)
    print("\n")
}

pn main() {
    // ===== Constructors =====
    show(datetime(2025, 4, 26))
    show(date(2025, 4, 26))
    show(time(10, 30, 45))

    // ===== Type checks =====
    show(datetime(2025, 4, 26) is datetime)
    show(date(2025, 4, 26) is date)
    show(time(10, 30, 45) is time)

    // ===== today() returns date =====
    show(today() is date)
    show(today().year >= 2025)

    // ===== Member access on constructed =====
    let d = date(2025, 4, 26)
    show(d.year)
    show(d.month)
    show(d.day)

    let t = time(10, 30, 45)
    show(t.hour)
    show(t.minute)
    show(t.second)

    // ===== format =====
    show(t'2025-04-26T10:30:45'.format("YYYY-MM-DD"))
    show(t'2025-04-26T10:30:45'.format("HH:mm:ss"))

    // ===== Comparison operations =====
    show(date(2025, 1, 1) < date(2025, 12, 31))
    show(time(10, 0, 0) < time(12, 0, 0))
}
