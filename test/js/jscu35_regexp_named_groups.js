// Named-capture validation keeps every pattern fact, including names beyond
// the old 96-row frontend limit.
let duplicate = '';
for (let i = 0; i < 97; i++) {
    duplicate += '(?<namedGroup' + i + '>x)';
}
duplicate += '(?<namedGroup96>x)';

try {
    new RegExp(duplicate);
    console.log('accepted');
} catch (error) {
    console.log(error.name);
}

console.log(/(?<same>x)\k<same>/.test('xx'));

// RE2 aliases for ECMAScript-valid `$` capture names use the same dynamic
// one-row mapping in the compiler and the completed RegExp payload.
let alias_source = '';
let alias_input = '';
for (let i = 0; i < 97; i++) {
    alias_source += '(?<$alias' + i + '>a)';
    alias_input += 'a';
}
console.log(new RegExp(alias_source).exec(alias_input).groups['$alias96']);

/(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)(l)/.exec('abcdefghijkl');
gc();
console.log(RegExp.$9, RegExp['$+']);
