// assert.Assert constructor/options fidelity
var assert = require('node:assert');
var Assert = assert.Assert;

try {
    Assert();
    console.log('plain Assert: did not throw');
} catch (e) {
    console.log('plain Assert: ' + e.name + ':' + e.code);
}

var loose = new Assert({ strict: false, diff: undefined });
console.log('instance ok isolated: ' + (loose.ok !== assert && loose.ok.strictEqual === undefined));
loose.equal(null, undefined);
loose.equal(2, '2');
console.log('loose equal: pass');

var strict = new Assert();
console.log('strict aliases: ' +
    (strict.equal === strict.strictEqual) + ':' +
    (strict.deepEqual === strict.deepStrictEqual) + ':' +
    (strict.notEqual === strict.notStrictEqual) + ':' +
    (strict.notDeepEqual === strict.notDeepStrictEqual));

try {
    new Assert({ diff: 'invalid' });
    console.log('invalid diff: did not throw');
} catch (e) {
    console.log('invalid diff: ' + e.name + ':' + e.code + ':' + e.message);
}

var full = new Assert({ diff: 'full', strict: false });
try {
    full.strictEqual('a', 'b');
    console.log('full diff: did not throw');
} catch (e) {
    console.log('full diff: ' + e.code + ':' + e.diff);
}
