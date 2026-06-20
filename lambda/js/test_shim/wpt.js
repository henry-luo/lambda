'use strict';

// Lambda-compatible subset of Node's WPT common helper.

const assert = require('assert');
const fixtures = require('./fixtures');
const fs = require('fs');
const path = require('path');

function failInTest(desc, err) {
  if (desc) console.error('In ' + desc + ':');
  throw err;
}

function test(fn, desc) {
  try {
    fn();
  } catch (err) {
    failInTest(desc, err);
  }
}

function promiseTest(fn, desc) {
  return Promise.resolve().then(fn).catch(function(err) {
    failInTest(desc, err);
  });
}

function assertThrows(expected, func, desc) {
  assert.throws(func, function(err) {
    if (!expected) return true;
    if (typeof expected === 'function') {
      return err instanceof expected || err.name === expected.name;
    }
    if (expected instanceof RegExp) {
      return expected.test(String(err && (err.stack || err.message || err)));
    }
    if (typeof expected === 'object' && expected.name) {
      return !!err && typeof err.name === 'string' &&
             err.name.indexOf(expected.name) === 0;
    }
    return true;
  }, desc);
}

function assertObjectEquals(actual, expected, desc) {
  assert.deepStrictEqual(actual, expected, desc);
}

function assertInArray(actual, expected, desc) {
  assert.notStrictEqual(expected.indexOf(actual), -1, desc);
}

const harness = {
  test: test,
  promise_test: promiseTest,
  assert_equals: assert.strictEqual,
  assert_not_equals: assert.notStrictEqual,
  assert_true(value, message) {
    assert.strictEqual(value, true, message);
  },
  assert_false(value, message) {
    assert.strictEqual(value, false, message);
  },
  assert_array_equals: assert.deepStrictEqual,
  assert_object_equals: assertObjectEquals,
  assert_throws: assertThrows,
  assert_throws_js: assertThrows,
  assert_throws_exactly(expected, func, desc) {
    assert.throws(func, function(err) { return err === expected; }, desc);
  },
  assert_greater_than(actual, expected, desc) {
    assert.ok(actual > expected, desc);
  },
  assert_greater_than_equal(actual, expected, desc) {
    assert.ok(actual >= expected, desc);
  },
  assert_less_than(actual, expected, desc) {
    assert.ok(actual < expected, desc);
  },
  assert_less_than_equal(actual, expected, desc) {
    assert.ok(actual <= expected, desc);
  },
  assert_in_array: assertInArray,
  assert_unreached(desc) {
    assert.fail('Reached unreachable code: ' + desc);
  },
};

function ResourceLoader() {}

ResourceLoader.prototype.toRealFilePath = function(from, url) {
  url = url.replace(
    '/resources/WebIDLParser.js',
    '/resources/webidl2/lib/webidl2.js');
  const base = path.dirname(from);
  return url.indexOf('/') === 0 ?
    fixtures.path('wpt', url) :
    fixtures.path('wpt', base, url);
};

ResourceLoader.prototype.read = function(from, url) {
  return fs.readFileSync(this.toRealFilePath(from, url), 'utf8');
};

ResourceLoader.prototype.readAsFetch = function(from, url) {
  const data = fs.readFileSync(this.toRealFilePath(from, url));
  return Promise.resolve({
    ok: true,
    arrayBuffer() { return data.buffer; },
    bytes() { return new Uint8Array(data); },
    json() { return JSON.parse(data.toString()); },
    text() { return data.toString(); },
  });
};

function WPTRunner() {
  throw new Error('WPTRunner is not supported by the Lambda common/wpt shim');
}

module.exports = {
  harness,
  ResourceLoader,
  WPTRunner,
};
