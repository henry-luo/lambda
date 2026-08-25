// Test: `;` cannot open element content
// Layer: 2 | Category: negative | Covers: S16.9.3 element separators
// `;` separates content ITEMS (`<div "a"; "b">` is valid), but cannot stand
// between a tag and its content — there is no preceding item to separate.
// The diagnostic must name that rule rather than report "expected an
// expression", because `;` is the separator everywhere else in the language.

let vs = [1, 2]
let d = <diagnostics; for (v in vs) v>
d
