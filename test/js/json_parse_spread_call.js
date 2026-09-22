// A parsed JSON array must use the same iterable path as an array literal.
const versions = JSON.parse('[{"current":"19.4","next":"19.5"}]');
const version = Object.assign(...versions);

console.log(version.current);
console.log(version.next);
