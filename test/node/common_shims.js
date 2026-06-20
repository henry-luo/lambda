'use strict';

const shimInternet = require('../../lambda/js/test_shim/internet');
const shimWpt = require('../../lambda/js/test_shim/wpt');

const h = shimWpt.harness;

h.test(function() {
  h.assert_equals(shimInternet.addresses.INVALID_HOST, 'something.invalid');
  h.assert_true(shimInternet.hasInternet);
  h.assert_equals(typeof shimInternet.skip, 'function');
  h.assert_array_equals([1, 2], [1, 2]);
  h.assert_object_equals({ value: 1 }, { value: 1 });
  h.assert_throws({ name: 'TypeError' }, function() {
    throw new TypeError('expected');
  });
}, 'tracked common shims');

shimWpt.harness.test(function() {
  shimWpt.harness.assert_equals(
    shimInternet.addresses.INVALID_HOST,
    'something.invalid');
  shimWpt.harness.assert_equals(typeof shimInternet.skipIfNoInternet, 'function');
}, 'internet helper surface');

console.log('common shims ok');
