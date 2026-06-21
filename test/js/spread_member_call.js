'use strict';

function assertEq(actual, expected, label) {
    if (actual !== expected) {
        throw new Error(label + ': expected ' + expected + ', got ' + actual);
    }
}

const obj = {
    f(a, b, c) {
        return [arguments.length, a, b, c].join(',');
    },
    createHash(a) {
        return [arguments.length, a].join(',');
    }
};

const key = 'f';
const args = [1, 2, 3];
assertEq(obj[key](...args), '3,1,2,3', 'computed identifier spread');

const clazz = 'Hash';
const one = ['sha1'];
assertEq(obj[`create${clazz}`](...one), '1,sha1', 'computed template spread');

const maybe = obj;
assertEq(maybe?.[key](...args), '3,1,2,3', 'optional computed receiver spread');
assertEq(maybe[key]?.(...args), '3,1,2,3', 'optional computed callee spread');

console.log('computed member spread:', true);
