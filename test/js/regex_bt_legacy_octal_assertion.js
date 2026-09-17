// Annex B octal escapes must be normalized before a lookahead selects the
// backtracking matcher; the Kotlin documentation bundle relies on this shape.
var tag_prefix = /<\/?(?!\1\b)/;
console.log(tag_prefix.test("<tag"));
console.log(tag_prefix.test("</tag"));
console.log(tag_prefix.test("<\x01x"));
