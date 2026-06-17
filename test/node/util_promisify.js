var util = require('node:util');

function add(a, b, callback) {
    callback(null, a + b);
}

function fail(callback) {
    callback(new Error('bad'));
}

function multi(callback) {
    callback(null, 'left', 'right');
}

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

util.promisify(multi)().then(function(values) {
    console.log('multi:', Array.isArray(values), values.length, values[0] + '/' + values[1]);
});

util.promisify(throws)().catch(function(err) {
    console.log('thrown:', err.message);
});

util.promisify(target.method).call(target, 5).then(function(value) {
    console.log('this:', value);
});
