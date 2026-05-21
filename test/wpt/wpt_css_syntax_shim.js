// Minimal WPT testharness shim for css-syntax tests.
// Keep this runner scoped: the broader WPT shim includes editing/selection
// helpers that are irrelevant here and stress unrelated JS runtime paths.

if (typeof Node === 'undefined' || !Node) {
    var Node = {
        ELEMENT_NODE: 1,
        TEXT_NODE: 3,
        DOCUMENT_NODE: 9,
        DOCUMENT_FRAGMENT_NODE: 11,
    };
}

var _wpt_state = { pass: 0, fail: 0, total: 0 };
var _wpt_completion_callbacks = [];
var onload = null;

function format_value(v) {
    if (v === null) return "null";
    if (v === undefined) return "undefined";
    if (typeof v === "string") return '"' + v + '"';
    return String(v);
}

function test(func, name) {
    _wpt_state.total++;
    var t = {
        _name: name || "",
        _cleanups: [],
        add_cleanup: function(fn) { this._cleanups.push(fn); },
        step: function(fn) { fn.apply(this, Array.prototype.slice.call(arguments, 1)); },
        step_func: function(fn) {
            var self = this;
            return function() { return fn.apply(self, arguments); };
        },
        done: function() {},
    };
    try {
        func.call(t, t);
        _wpt_state.pass++;
    } catch (e) {
        _wpt_state.fail++;
        console.log("FAIL: " + (name || "") + " - " + (e && e.message ? e.message : e));
    }
    for (var _wpt_cleanup_i = 0; _wpt_cleanup_i < t._cleanups.length; _wpt_cleanup_i++) {
        try { t._cleanups[_wpt_cleanup_i](); } catch (_) {}
    }
}

function async_test(arg1, arg2) {
    var name = "";
    var func = null;
    if (typeof arg1 === "function") {
        func = arg1;
        if (typeof arg2 === "string") name = arg2;
    } else if (typeof arg1 === "string") {
        name = arg1;
    }
    var t = {
        _name: name,
        _done: false,
        _counted: false,
        step: function(fn) {
            try { return fn.apply(this, Array.prototype.slice.call(arguments, 1)); }
            catch (e) { this._record_failure(e); }
        },
        step_func: function(fn) {
            var self = this;
            return function() {
                if (self._done) return;
                if (!self._counted) {
                    _wpt_state.total++;
                    self._counted = true;
                }
                try {
                    var result = fn.apply(self, arguments);
                    if (!self._done) self.done();
                    return result;
                } catch (e) {
                    self._record_failure(e);
                }
            };
        },
        step_func_done: function(fn) {
            var self = this;
            return function() {
                if (self._done) return;
                if (!self._counted) {
                    _wpt_state.total++;
                    self._counted = true;
                }
                try {
                    if (fn) fn.apply(self, arguments);
                    self.done();
                } catch (e) {
                    self._record_failure(e);
                }
            };
        },
        done: function() {
            if (this._done) return;
            if (!this._counted) {
                _wpt_state.total++;
                this._counted = true;
            }
            _wpt_state.pass++;
            this._done = true;
        },
        _record_failure: function(e) {
            if (this._done) return;
            if (!this._counted) {
                _wpt_state.total++;
                this._counted = true;
            }
            _wpt_state.fail++;
            this._done = true;
            console.log("FAIL: " + this._name + " - " + (e && e.message ? e.message : e));
        },
    };
    if (func) {
        try { func.call(t, t); } catch (e) { t._record_failure(e); }
    }
    return t;
}

function generate_tests(test_func, params, properties) {
    if (!params || !params.length) return;
    for (var _wpt_param_i = 0; _wpt_param_i < params.length; _wpt_param_i++) {
        var _wpt_entry = params[_wpt_param_i];
        if (!_wpt_entry || !_wpt_entry.length) continue;
        var name = _wpt_entry[0];
        var args = Array.prototype.slice.call(_wpt_entry, 1);
        (function(n, a) {
            test(function() { test_func.apply(this, a); }, n);
        })(name, args);
    }
}

function setup(func_or_props, maybe_props) {
    if (typeof func_or_props === "function") {
        try { func_or_props(); } catch (e) {
            console.log("FAIL: setup - " + (e && e.message ? e.message : e));
        }
    }
}

function add_completion_callback(fn) {
    if (typeof fn === "function") _wpt_completion_callbacks.push(fn);
}

function assert_equals(actual, expected, desc) {
    if (actual !== expected) {
        var msg = "assert_equals: got " + JSON.stringify(actual) + ", expected " + JSON.stringify(expected);
        if (desc) msg += " - " + desc;
        throw new Error(msg);
    }
}

function assert_not_equals(actual, expected, desc) {
    if (actual === expected) {
        var msg = "assert_not_equals: got " + JSON.stringify(actual) + ", should differ";
        if (desc) msg += " - " + desc;
        throw new Error(msg);
    }
}

function assert_true(value, desc) {
    if (value !== true) {
        var msg = "assert_true: got " + JSON.stringify(value);
        if (desc) msg += " - " + desc;
        throw new Error(msg);
    }
}

function assert_false(value, desc) {
    if (value !== false) {
        var msg = "assert_false: got " + JSON.stringify(value);
        if (desc) msg += " - " + desc;
        throw new Error(msg);
    }
}

function assert_array_equals(actual, expected, desc) {
    if (!actual || !expected || actual.length !== expected.length) {
        throw new Error("assert_array_equals: length mismatch" + (desc ? " - " + desc : ""));
    }
    for (var _wpt_array_i = 0; _wpt_array_i < actual.length; _wpt_array_i++) {
        if (actual[_wpt_array_i] !== expected[_wpt_array_i]) {
            throw new Error("assert_array_equals: index " + _wpt_array_i + " got " +
                JSON.stringify(actual[_wpt_array_i]) + ", expected " + JSON.stringify(expected[_wpt_array_i]) +
                (desc ? " - " + desc : ""));
        }
    }
}

function assert_throws_dom(type, func, desc) {
    var threw = false;
    try { func(); } catch (e) { threw = true; }
    if (!threw) {
        throw new Error("assert_throws_dom: expected " + type + " but no exception thrown" +
            (desc ? " - " + desc : ""));
    }
}

function assert_throws_js(constructor, func, desc) {
    var threw = false;
    try { func(); } catch (e) { threw = true; }
    if (!threw) {
        throw new Error("assert_throws_js: expected exception but none thrown" +
            (desc ? " - " + desc : ""));
    }
}

function assert_unreached(desc) {
    throw new Error("assert_unreached" + (desc ? ": " + desc : ""));
}

function on_event(object, event, callback) {
    object.addEventListener(event, callback, false);
}

if (typeof CSSNestedDeclarations === "undefined") {
    function CSSNestedDeclarations() {}
    if (typeof globalThis !== "undefined") globalThis.CSSNestedDeclarations = CSSNestedDeclarations;
    if (typeof window !== "undefined" && window) window.CSSNestedDeclarations = CSSNestedDeclarations;
}

var _wpt_load_listeners = [];
function addEventListener(type, fn, opts) {
    if ((type === "load" || type === "DOMContentLoaded") && typeof fn === "function") {
        _wpt_load_listeners.push(fn);
    }
}
if (typeof window !== "undefined" && window && typeof window.addEventListener !== "function") {
    window.addEventListener = addEventListener;
}

function _wpt_fire_onload() {
    var fn = null;
    if (typeof window !== "undefined" && window && typeof window.onload === "function") {
        fn = window.onload;
    } else if (typeof globalThis !== "undefined" && globalThis && typeof globalThis.onload === "function") {
        fn = globalThis.onload;
    } else if (typeof onload === "function") {
        fn = onload;
    }
    if (fn) {
        try { fn(); } catch (e) {
            console.log("FAIL: window.onload threw - " + (e && e.message ? e.message : e));
        }
    }
    for (var _wpt_load_i = 0; _wpt_load_i < _wpt_load_listeners.length; _wpt_load_i++) {
        try { _wpt_load_listeners[_wpt_load_i](); } catch (e) {
            console.log("FAIL: load listener threw - " + (e && e.message ? e.message : e));
        }
    }
    for (var _wpt_completion_i = 0; _wpt_completion_i < _wpt_completion_callbacks.length; _wpt_completion_i++) {
        try { _wpt_completion_callbacks[_wpt_completion_i]([]); } catch (_) {}
    }
}

function _wpt_print_summary() {
    console.log("WPT_RESULT: " + _wpt_state.pass + "/" + _wpt_state.total + " passed");
}
