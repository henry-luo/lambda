var util = require('node:util');
var internalUtil = require('internal/util');

function add(a, b, callback) {
    callback(null, a + b);
}

function fail(callback) {
    callback(new Error('bad'));
}

function multi(callback) {
    callback(null, 'left', 'right');
}

function named(callback) {
    callback(null, 'left', 'right');
}
named[internalUtil.customPromisifyArgs] = ['first', 'second'];

function throws(callback) {
    throw new Error('boom');
}

var target = {
    base: 7,
    method: function(delta, callback) {
        callback(null, this.base + delta);
    }
};

var customTarget = function(callback) {
    callback(null, 'unused');
};
var customFn = function() {
    return Promise.resolve('custom');
};
customTarget[util.promisify.custom] = customFn;

console.log('custom symbol:', util.promisify.custom === Symbol.for('nodejs.util.promisify.custom'));
console.log('custom function:', util.promisify(customTarget) === customFn);
var addPromise = util.promisify(add);
console.log('idempotent:', util.promisify(addPromise) === addPromise);

try {
    util.promisify(42);
} catch (e) {
    console.log('nonfunction code:', e.code);
}

addPromise(2, 3).then(function(value) {
    console.log('resolved:', value);
});

util.promisify(fail)().catch(function(err) {
    console.log('rejected:', err.message);
});

util.promisify(multi)().then(function(value) {
    console.log('multi first:', value);
});

util.promisify(named)().then(function(value) {
    console.log('named:', value.first + '/' + value.second);
});

util.promisify(throws)().catch(function(err) {
    console.log('thrown:', err.message);
});

util.promisify(target.method).call(target, 5).then(function(value) {
    console.log('this:', value);
});

var warnings = 0;
function removedWarning() {
    console.log('removed warning called');
}
function warningHandler(warning) {
    if (warning.code === 'DEP0174') warnings++;
}
process.on('warning', removedWarning);
process.off('warning', removedWarning);
process.on('warning', warningHandler);
util.promisify(async function(callback) {
    callback(null, 'async-return');
    return 'ignored';
})().then(function(value) {
    console.log('async warning:', value, warnings);
});
