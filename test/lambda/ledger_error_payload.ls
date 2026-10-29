let first = error("first");
let failed = int("abc");
let parsed = parse("!", 'json') ^ { ^ };
let later = error("later");
let pressure = [for (i in 1 to 80) {index: i, text: string(i)}];
[first.message, failed.code, failed.message, later.message, len(pressure), parsed.code == 407, contains(parsed.message, "parse:"), parsed.message != later.message]
