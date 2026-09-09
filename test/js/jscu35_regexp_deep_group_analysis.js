// The lookahead sits under 65 ordinary groups, beyond the former fixed
// classification stack. It must retain its enclosing-pattern semantics.
let open = '';
let close = '';
for (let i = 0; i < 65; i++) {
    open += '(';
    close += ')';
}
const expression = new RegExp(open + '(?=a)a' + close);
console.log(expression.test('a'), expression.test('b'));
