// Each negative lookahead becomes a post-filter. The 17th must survive the
// compiled-regex rewrite instead of remaining as an unsupported RE2 assertion.
let source = '';
for (let i = 0; i < 17; i++) source += '(?!x)';
source += 'a';
const expression = new RegExp(source);
console.log(expression.test('a'), expression.test('x'));
