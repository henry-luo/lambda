var s, e, a, c, r, i, f, m, j = {}, t = {};

function o(id) {
    var cached = t[id];
    if (cached) return cached.exports;
    var module = t[id] = {id, exports: {}};
    j[id].call(module.exports, module, module.exports, o);
    return module.exports;
}

e = Symbol("rspack queues");
a = o.aE = Symbol("rspack exports");
c = Symbol("rspack error");
r = Symbol("rspack done");
i = o.zS = Symbol("rspack defer");
o.zT = ids => {
    if (ids.some(id => {
        var module = t[id];
        return !module || module[r] === false;
    })) {
        return {then: (resolve, reject) => Promise.all(ids.map(o)).then(resolve, reject)};
    }
};
f = queue => {
    if (queue && queue.d < 1) {
        queue.d = 1;
        queue.forEach(fn => fn.r--);
        queue.forEach(fn => fn.r-- ? fn.r++ : fn());
    }
};
o.a = (module, body, has_await, module_wrapper) => {
    if (has_await) (m = []).d = -1;
    var queues, resolve, reject, notify, listeners = new Set;
    var exports = module.exports;
    var promise = new Promise((done, fail) => { reject = fail; resolve = done; });
    promise[a] = exports;
    promise[e] = callback => {
        if (m) callback(m);
        listeners.forEach(callback);
        promise.catch(() => {});
    };
    module.exports = promise;
    var wrapped_module = module;
    if (module_wrapper) (wrapped_module = Object.create(module)).exports = exports;
    body(inputs => {
        queues = inputs.map(input => {
            if (input !== null && typeof input === "object") {
                if (!input[e] && input[i]) {
                    var wait = o.zT(input[i]);
                    if (!wait) return input;
                    var dependency = input;
                    input = {then(resolve, reject) { wait.then(() => resolve(dependency), reject); }};
                }
                if (input[e]) return input;
                if (input.then) {
                    var pending = [];
                    pending.d = 0;
                    input.then(value => { state[a] = value; f(pending); },
                        error => { state[c] = error; f(pending); });
                    var state = {};
                    state[i] = false;
                    state[e] = callback => callback(pending);
                    return state;
                }
            }
            var state = {};
            state[e] = () => {};
            state[a] = input;
            return state;
        });
        var release;
        var values = () => queues.map(state => {
            if (state[i]) return state;
            if (state[c]) throw state[c];
            return state[a];
        });
        var deferred = new Promise(done => {
            (release = () => done(values)).r = 0;
            var register = queue => queue !== m && !listeners.has(queue) &&
                listeners.add(queue) && queue && !queue.d && (release.r++, queue.push(release));
            queues.map(state => state[i] || state[e](register));
        });
        return release.r ? deferred : values();
    }, error => {
        if (error) reject(promise[c] = error);
        else {
            if (module_wrapper) exports = promise[a] = wrapped_module.exports;
            resolve(exports);
        }
        f(m);
        promise[r] = true;
    }, wrapped_module);
    if (m && m.d < 0) m.d = 0;
};

o.m = j;

export { o as __webpack_require__ };
