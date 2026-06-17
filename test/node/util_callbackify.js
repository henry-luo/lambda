var util = require('node:util');

function add(a, b) {
    return Promise.resolve(a + b);
}

function fail() {
    return Promise.reject(new Error('bad'));
}

function failFalsy() {
    return Promise.reject(null);
}

function plain(value) {
    return value + 1;
}

function throws() {
    throw new Error('boom');
}

var target = {
    base: 9,
    method: function(delta) {
        return Promise.resolve(this.base + delta);
    }
};

var addCallback = util.callbackify(add);
console.log('type:', typeof addCallback);

try {
    util.callbackify(42);
} catch (e) {
    console.log('nonfunction original:', e.code);
}

try {
    addCallback(1, 2, 'nope');
} catch (e) {
    console.log('nonfunction callback:', e.code);
}

var stillSync = true;
addCallback(2, 4, function(err, value) {
    console.log('resolved:', err === null, value, stillSync ? 'sync' : 'async');
});
stillSync = false;

util.callbackify(fail)(function(err) {
    console.log('rejected:', err.message);
});

util.callbackify(failFalsy)(function(err) {
    console.log('falsy:', err.code, err.reason === null);
});

util.callbackify(plain)(6, function(err, value) {
    console.log('plain:', err === null, value);
});

util.callbackify(throws)(function(err) {
    console.log('thrown:', err.message);
});

util.callbackify(target.method).call(target, 5, function(err, value) {
    console.log('this:', err === null, value);
});
