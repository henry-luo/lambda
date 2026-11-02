// S7.6.9: a selector names a type/pattern, not an error value for equality.
let named = error("same");
error("same") ^named { 7 }
