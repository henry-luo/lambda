var word = /[\p{Alphabetic}\p{Number}_]/u;

console.log(word.test('A'));
console.log(word.test('\u03bb'));
console.log(word.test('\u2167'));
console.log(word.test('_'));
console.log(word.test('-'));
