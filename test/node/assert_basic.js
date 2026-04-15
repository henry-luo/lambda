// assert module basic tests
var assert = require('node:assert');

// assert.ok
assert.ok(true);
console.log('ok true: pass');

assert.ok(1);
console.log('ok 1: pass');

// assert.equal (loose)
assert.equal(1, 1);
console.log('equal 1,1: pass');

assert.equal('1', 1);
console.log('equal str 1, num 1: pass');

// assert.notEqual
assert.notEqual(1, 2);
console.log('notEqual 1,2: pass');

// assert.strictEqual
assert.strictEqual(1, 1);
console.log('strictEqual 1,1: pass');

// assert.notStrictEqual
assert.notStrictEqual(1, '1');
console.log('notStrictEqual 1,str1: pass');

// assert.deepStrictEqual
assert.deepStrictEqual({ a: 1, b: 2 }, { a: 1, b: 2 });
console.log('deepStrictEqual obj: pass');

assert.deepStrictEqual([1, 2, 3], [1, 2, 3]);
console.log('deepStrictEqual arr: pass');

// assert.notDeepStrictEqual
assert.notDeepStrictEqual({ a: 1 }, { a: 2 });
console.log('notDeepStrictEqual: pass');

// assert.deepEqual
assert.deepEqual([1, 2], [1, 2]);
console.log('deepEqual: pass');

// assert.notDeepEqual
assert.notDeepEqual([1, 2], [1, 3]);
console.log('notDeepEqual: pass');

// assert.throws
assert.throws(function() { throw new Error('boom'); });
console.log('throws: pass');

// assert.doesNotThrow
assert.doesNotThrow(function() { return 42; });
console.log('doesNotThrow: pass');

// assert.ifError - null should not throw
assert.ifError(null);
console.log('ifError null: pass');

assert.ifError(undefined);
console.log('ifError undefined: pass');

// assert.match
assert.match('hello world', /world/);
console.log('match: pass');

// assert.doesNotMatch
assert.doesNotMatch('hello', /xyz/);
console.log('doesNotMatch: pass');

// assert.fail should throw
try {
    assert.fail('failure message');
    console.log('fail: did not throw');
} catch(e) {
    console.log('fail: caught');
}

// assert as callable function
try {
    assert(true);
    console.log('assert(true): pass');
} catch(e) {
    console.log('assert(true): fail');
}

// assert(false) should throw
try {
    assert(false);
    console.log('assert(false): did not throw');
} catch(e) {
    console.log('assert(false): caught');
}
