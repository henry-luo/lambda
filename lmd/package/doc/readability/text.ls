// ECMAScript white space: Readability's \s and String.prototype.trim.
// Importers compose it inside their own \(...) islands: a pattern named
// across modules does not yet resolve as a value (RD-O2, LR07-48).
pub type ws = \("\s" | "\u000B" | "\u00A0" | "\u1680" | "\u2000" to "\u200A" | "\u2028" | "\u2029" |
    "\u202F" | "\u205F" | "\u3000" | "\uFEFF")
