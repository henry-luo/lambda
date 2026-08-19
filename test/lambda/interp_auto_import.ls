import .interp_auto_import_provider

fn hot(x: int) int => shift(x) + offset

[hot(1), hot(2), hot(3), hot(4)]
