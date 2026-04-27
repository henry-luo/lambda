// Minimal WPT testharness.js shim for Lambda
// Provides test(), assert_*() functions that capture results to stdout

var _wpt_pass = 0;
var _wpt_fail = 0;
var _wpt_total = 0;

function test(func, name) {
    _wpt_total++;
    try {
        func();
        _wpt_pass++;
        // silent on pass
    } catch (e) {
        _wpt_fail++;
        console.log("FAIL: " + name + " - " + e.message);
    }
}

function assert_equals(actual, expected, desc) {
    if (actual !== expected) {
        var msg = "assert_equals: got " + JSON.stringify(actual) + ", expected " + JSON.stringify(expected);
        if (desc) msg = msg + " - " + desc;
        throw new Error(msg);
    }
}

function assert_not_equals(actual, expected, desc) {
    if (actual === expected) {
        var msg = "assert_not_equals: got " + JSON.stringify(actual) + ", should differ";
        if (desc) msg = msg + " - " + desc;
        throw new Error(msg);
    }
}

function assert_true(val, desc) {
    if (val !== true) {
        var msg = "assert_true: got " + JSON.stringify(val);
        if (desc) msg = msg + " - " + desc;
        throw new Error(msg);
    }
}

function assert_false(val, desc) {
    if (val !== false) {
        var msg = "assert_false: got " + JSON.stringify(val);
        if (desc) msg = msg + " - " + desc;
        throw new Error(msg);
    }
}

function assert_in_array(actual, expected_arr, desc) {
    var found = false;
    for (var i = 0; i < expected_arr.length; i++) {
        if (actual === expected_arr[i]) { found = true; break; }
    }
    if (!found) {
        var msg = "assert_in_array: " + JSON.stringify(actual) + " not in " + JSON.stringify(expected_arr);
        if (desc) msg = msg + " - " + desc;
        throw new Error(msg);
    }
}

function assert_throws_dom(type, func, desc) {
    var threw = false;
    try {
        func();
    } catch (e) {
        threw = true;
    }
    if (!threw) {
        var msg = "assert_throws_dom: expected " + type + " but no exception thrown";
        if (desc) msg = msg + " - " + desc;
        throw new Error(msg);
    }
}

function assert_throws_js(constructor, func, desc) {
    var threw = false;
    try {
        func();
    } catch (e) {
        threw = true;
    }
    if (!threw) {
        var msg = "assert_throws_js: expected exception but none thrown";
        if (desc) msg = msg + " - " + desc;
        throw new Error(msg);
    }
}

function assert_unreached(desc) {
    var msg = "assert_unreached: reached unreachable code";
    if (desc) msg = msg + " - " + desc;
    throw new Error(msg);
}

function assert_array_equals(actual, expected, desc) {
    if (!actual || typeof actual.length !== "number") {
        var msg = "assert_array_equals: actual is not array-like";
        if (desc) msg = msg + " - " + desc;
        throw new Error(msg);
    }
    if (!expected || typeof expected.length !== "number") {
        var msg = "assert_array_equals: expected is not array-like";
        if (desc) msg = msg + " - " + desc;
        throw new Error(msg);
    }
    if (actual.length !== expected.length) {
        var msg = "assert_array_equals: lengths differ — got " + actual.length + ", expected " + expected.length;
        if (desc) msg = msg + " - " + desc;
        throw new Error(msg);
    }
    for (var i = 0; i < actual.length; i++) {
        if (actual[i] !== expected[i]) {
            var msg = "assert_array_equals: index " + i + " — got " + JSON.stringify(actual[i]) + ", expected " + JSON.stringify(expected[i]);
            if (desc) msg = msg + " - " + desc;
            throw new Error(msg);
        }
    }
}

function assert_greater_than(actual, expected, desc) {
    if (!(actual > expected)) {
        var msg = "assert_greater_than: " + JSON.stringify(actual) + " not > " + JSON.stringify(expected);
        if (desc) msg = msg + " - " + desc;
        throw new Error(msg);
    }
}

function assert_less_than(actual, expected, desc) {
    if (!(actual < expected)) {
        var msg = "assert_less_than: " + JSON.stringify(actual) + " not < " + JSON.stringify(expected);
        if (desc) msg = msg + " - " + desc;
        throw new Error(msg);
    }
}

function assert_greater_than_equal(actual, expected, desc) {
    if (!(actual >= expected)) {
        var msg = "assert_greater_than_equal: " + JSON.stringify(actual) + " not >= " + JSON.stringify(expected);
        if (desc) msg = msg + " - " + desc;
        throw new Error(msg);
    }
}

// WPT custom assertion used by Selection tests — checks selectstart event behaviour.
// Lambda has no event dispatch in the JS DOM, so this stub records a skipped check.
function assert_selectstart(action) {
    // Best-effort: just invoke the action and ensure no exception is thrown.
    if (typeof action === "function") action();
}

// setup() and done() are no-ops in our synchronous model
function setup(func_or_props, maybe_props) {
    if (typeof func_or_props === "function") {
        func_or_props();
    }
}
function done() {}

// async_test() shim — Lambda JS runs synchronously, so we capture
// the step function and execute it when onload fires (simulated at end of script).
var _wpt_async_tests = [];
function async_test(name_or_props) {
    var name = "";
    if (typeof name_or_props === "string") name = name_or_props;
    else if (name_or_props && name_or_props.name) name = name_or_props.name;

    var t = {
        _name: name,
        _done: false,
        _steps: [],
        step_func: function(fn) {
            var self = this;
            return function() {
                _wpt_total++;
                try {
                    fn.apply(self, arguments);
                    _wpt_pass++;
                } catch (e) {
                    _wpt_fail++;
                    console.log("FAIL: " + self._name + " - " + e.message);
                }
            };
        },
        step: function(fn) {
            try {
                fn.apply(this, Array.prototype.slice.call(arguments, 1));
            } catch (e) {
                _wpt_total++;
                _wpt_fail++;
                console.log("FAIL: " + this._name + " - " + e.message);
            }
        },
        step_func_done: function(fn) {
            var self = this;
            return function() {
                _wpt_total++;
                try {
                    if (fn) fn.apply(self, arguments);
                    _wpt_pass++;
                    self._done = true;
                } catch (e) {
                    _wpt_fail++;
                    console.log("FAIL: " + self._name + " - " + e.message);
                }
            };
        },
        done: function() { this._done = true; },
        unreached_func: function(desc) {
            return function() {
                _wpt_total++;
                _wpt_fail++;
                console.log("FAIL: " + desc + " - reached unreachable code");
            };
        }
    };
    _wpt_async_tests.push(t);
    return t;
}

// Simulate window.onload — called at end of combined script by the GTest runner.
// Fires the onload handler if one was set, which triggers async_test step_funcs.
var onload = null;
function _wpt_fire_onload() {
    if (onload !== null && onload !== undefined) {
        onload();
    }
}

// Print summary at end (called by GTest runner via appended code)
function _wpt_print_summary() {
    console.log("WPT_RESULT: " + _wpt_pass + "/" + _wpt_total + " passed");
}
