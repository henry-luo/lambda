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

// setup() and done() are no-ops in our synchronous model
function setup(func_or_props, maybe_props) {
    if (typeof func_or_props === "function") {
        func_or_props();
    }
}
function done() {}

// Print summary at end (called by GTest runner via appended code)
function _wpt_print_summary() {
    console.log("WPT_RESULT: " + _wpt_pass + "/" + _wpt_total + " passed");
}
