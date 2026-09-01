// Generic array slots are boxed Items even when their declared element type
// has a raw pointer lane. Indexed reads must describe that physical carrier
// before the nullable function-return boundary converts it.
fn pick_string(values: string[], index: int) string? { values[index] }

[pick_string(["first", "second"], 1), pick_string(["first"], 3)]
