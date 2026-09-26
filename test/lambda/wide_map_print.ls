// LR11-4: a map is printed whatever its width. The printer bailed on more
// than 10000 fields and printed `{[invalid map_type length]}` for the whole
// map, although every field read back fine.
let text = "{" ++ join(for (i in 0 to 10000) "\"k" ++ string(i) ++ "\": " ++ string(i), ", ") ++ "}";
let wide = parse(text, 'json') ^ { null };
let printed = string(wide);
[len(wide), wide.k10000, len(printed) > 100000, slice(printed, 0, 22)]
