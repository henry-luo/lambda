'use strict';

const assert = require('assert');
const { Readable, Writable, Duplex, Transform, PassThrough } = require('stream');

const readable = new Readable({ read() {} });
const writable = new Writable({ write() {} });
const duplex = new Duplex({ read() {}, write() {} });
const transform = new Transform({ transform() {} });
const pass = new PassThrough();

assert.strictEqual(readable instanceof Readable, true);
assert.strictEqual(writable instanceof Readable, false);
assert.strictEqual(duplex instanceof Readable, true);
assert.strictEqual(transform instanceof Readable, true);
assert.strictEqual(pass instanceof Readable, true);

assert.strictEqual(readable instanceof Writable, false);
assert.strictEqual(writable instanceof Writable, true);
assert.strictEqual(duplex instanceof Writable, true);
assert.strictEqual(transform instanceof Writable, true);
assert.strictEqual(pass instanceof Writable, true);

assert.strictEqual(readable instanceof Duplex, false);
assert.strictEqual(writable instanceof Duplex, false);
assert.strictEqual(duplex instanceof Duplex, true);
assert.strictEqual(transform instanceof Duplex, true);
assert.strictEqual(pass instanceof Duplex, true);

assert.strictEqual(duplex instanceof Transform, false);
assert.strictEqual(transform instanceof Transform, true);
assert.strictEqual(pass instanceof Transform, true);

function CustomWritable() {
  assert.ok(this instanceof CustomWritable,
            `${this} does not inherit from CustomWritable`);
  assert.ok(this instanceof Writable,
            `${this} does not inherit from Writable`);
}

Object.setPrototypeOf(CustomWritable, Writable);
Object.setPrototypeOf(CustomWritable.prototype, Writable.prototype);

new CustomWritable();

assert.throws(
  CustomWritable,
  {
    code: 'ERR_ASSERTION',
    constructor: assert.AssertionError,
    message: 'undefined does not inherit from CustomWritable'
  }
);

class OtherCustomWritable extends Writable {}

assert.strictEqual(new OtherCustomWritable() instanceof CustomWritable, false);
assert.strictEqual(new CustomWritable() instanceof OtherCustomWritable, false);

console.log('stream inheritance ok');
