'use strict';

function add(a, b) { return a + b; }
function subtract(a, b) { return a - b; }
function multiply(a, b) { return a * b; }
function divide(a, b) { return a / b; }
function modulo(a, b) { return a % b; }
function power(a, b) { return a ** b; }
var operators = [add, subtract, multiply, divide, modulo, power];
var samples = [[Number.MIN_VALUE, 0], [Number.MIN_VALUE * 2, Number.MIN_VALUE],
    [Number.MIN_VALUE, 1], [Number.MIN_VALUE * 2, 2], [Number.MIN_VALUE, 1],
    [Number.MIN_VALUE, 1]];
var retained = [];
for (var i = 0; i < operators.length; i++) {
    var args = samples[i];
    var left = { valueOf: function() { return args[0]; } };
    retained.push(operators[i](left, args[1]));
}
console.log('wide-results', retained.every(function(x) { return x === Number.MIN_VALUE; }));
console.log('mixed-results', add('a', 2), String(add(3n, 4n)), String(power(2n, 70n)));
console.log('special-results', Object.is(multiply(-0, 1), -0), Number.isNaN(divide(0, 0)), divide(1, 0));
var borrowed = Number.MIN_VALUE;
function readBorrowed() { return +borrowed; }
var saved = readBorrowed();
borrowed = 0;
console.log('borrowed-owner', saved === Number.MIN_VALUE);
var order = [];
var first = { valueOf: function() { order.push('left'); return 9; } };
var second = { valueOf: function() { order.push('right'); return 2; } };
console.log('coercion-result', subtract(first, second), order.join(','));
try { add(first, Symbol('bad')); } catch (e) { console.log('symbol-error', e instanceof TypeError); }
try { divide(1n, 0n); } catch (e) { console.log('bigint-error', e instanceof RangeError); }
var caught = Number.MIN_VALUE;
var throwing = { valueOf: function() { throw caught; } };
try { multiply(throwing, 2); } catch (e) { console.log('wide-error', e === Number.MIN_VALUE); }
console.log('after-error', add(20, 22));
