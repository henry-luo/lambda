var word = /[\p{Alphabetic}\p{Number}_]/u;

console.log(word.test('A'));
console.log(word.test('\u03bb'));
console.log(word.test('\u2167'));
console.log(word.test('_'));
console.log(word.test('-'));

// Bare \p{...} — not inside a character class. The /v class rewriter flattens
// binary properties only within a class, so the bare spelling used to reach RE2
// unflattened and throw "invalid character class range", even though the
// identical [\p{...}] form above matched correctly.
console.log(/^\p{White_Space}$/u.test(' '));
console.log(/^\p{White_Space}$/u.test('a'));
console.log(/^\p{Alphabetic}$/u.test('a'));
console.log(/^\p{Alphabetic}$/u.test('1'));
console.log(/^\p{Math}$/u.test('+'));
console.log(/^\p{Uppercase}$/u.test('A'));
console.log(/^\p{Uppercase}$/u.test('a'));
// negated bare form
console.log(/^\P{White_Space}$/u.test('a'));
console.log(/^\P{White_Space}$/u.test(' '));
// quantified, and mixed with a class in one pattern
console.log(/^\p{White_Space}+$/u.test('  \t'));
console.log(/^[\p{White_Space}]\p{Alphabetic}$/u.test(' a'));
// an unrecognised property name must still be a SyntaxError
try { new RegExp('\\p{NotARealProperty}', 'u'); console.log('no-throw'); }
catch (e) { console.log(e instanceof SyntaxError); }
