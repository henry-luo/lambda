// The first formal is specialized; the second remains an ordinary Item.
// A mismatched first argument must run the boxed body, not a numeric coercion.
function plusOne(value, ignored) {
  return value + 1;
}

console.log(plusOne(2, "unused"));
console.log(plusOne("x", "unused"));
