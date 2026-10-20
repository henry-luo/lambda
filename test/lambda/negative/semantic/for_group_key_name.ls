// @expect-error: E239
// @description: FC9/S14.1.2v2: an omitted `as` infers a key name only from a
// trailing field access; a computed key must be named, never given a
// generated or empty name.

let words = ["a", "bb", "cc"];
[for (w in words group by len(w) into g) len(g)]
