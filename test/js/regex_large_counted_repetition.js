// Counted repetitions above RE2's limit must retain ECMAScript semantics via
// the backtracking engine; these are shapes loaded by live client bundles.
var payload = "";
for (var i = 0; i < 2048; i++) payload += "x";

console.log("bounded:" + /^[\s\S]{0,2048}$/.test(payload) + "," +
    /^[\s\S]{0,2048}$/.test(payload + "x"));

var token = "";
for (var j = 0; j < 16384; j++) token += "a";
console.log("token:" + /^[A-Za-z0-9_-]{80,16384}={0,2}$/.test(token) + "," +
    /^[A-Za-z0-9_-]{80,16384}={0,2}$/.test(token + "=" + "=" + "="));

// Each count fits RE2's per-quantifier cap, but their product exceeds it.
const external = /^external-box:(?:account:(?:[A-Za-z0-9._~-]|%[0-9A-F]{2}){1,512}:)?file:([1-9][0-9]{0,511})$/u;
const account = '%41'.repeat(512);
console.log('compound:' + external.test('external-box:account:' + account + ':file:123') + ',' +
    external.test('external-box:account:' + account + '%41:file:123'));
console.log('capture:' + external.exec('external-box:account:%41:file:123')[1]);
const nested = /^(?:ab{2}){501}$/u;
console.log('nested:' + nested.test('abb'.repeat(501)) + ',' + nested.test('abb'.repeat(500)));
let invalid = 0;
for (const source of ['a{512,2}', '(?:a{3,1}){512}']) {
    try { new RegExp(source, 'u'); } catch (error) { if (error instanceof SyntaxError) invalid++; }
}
console.log('invalid:' + invalid);
