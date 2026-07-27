'use strict';

const assert = require('assert');

function expectsError(validator) {
    const assertion = assert;
    return function(error) {
        assertion.throws(function() { throw error; }, validator);
        return true;
    };
}

if (typeof module !== 'undefined') {
    module.exports = { expectsError };
}

console.log('cjs helper loaded');
