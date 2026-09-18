// Result47: a Number-specialized function still needs a boxed body for a
// direct BigInt call. Its intermediate division result must remain a BigInt.
function idiv(a, b) {
    const d = a / b;
    return d * b;
}

console.log(idiv(123456789012345678901234567890n, 3n).toString());
