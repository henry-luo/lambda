// v5 §5.6: guest numbers remain IEEE numbers across the Item boundary. In
// particular, shared poison must not re-enter the Lambda int/symbol tag space.
function through(value) { return value; }

const positive = through(Infinity);
const negative = through(-Infinity);
const nan = through(NaN);

console.log(typeof positive, positive === Infinity);
console.log(typeof negative, negative === -Infinity);
console.log(typeof nan, nan !== nan);
