// Protocol-only WPT harness for contenteditable acceptance. It deliberately
// does not choose edit ranges, mutate a document, normalize nodes, repair a
// Selection, or synthesize input/history outcomes; those are product behavior
// owned by lambda.dom under D7.2.5.
var _wpt_pass = 0;
var _wpt_fail = 0;
var _wpt_total = 0;

function _wpt_failure(name, error) {
    _wpt_fail++;
    console.log("FAIL: " + name + " - " +
                (error && error.message ? error.message : String(error)));
}

function test(callback, name) {
    _wpt_total++;
    try {
        callback.call({
            add_cleanup: function() {},
            step: function(fn) { fn(); },
            step_func: function(fn) { return fn; },
            done: function() {}
        });
        _wpt_pass++;
    } catch (error) {
        _wpt_failure(name || "unnamed test", error);
    }
}

function assert_true(value, description) {
    if (value !== true) throw new Error(description || "expected true");
}

function assert_false(value, description) {
    if (value !== false) throw new Error(description || "expected false");
}

function assert_equals(actual, expected, description) {
    if (actual !== expected) {
        throw new Error((description ? description + ": " : "") +
                        "got " + String(actual) + ", expected " + String(expected));
    }
}

function _wpt_fire_onload() {
    // WPT scripts that install an ordinary load listener may observe it. This
    // is lifecycle transport, not an editing default action.
    try {
        if (typeof window !== "undefined" && window &&
            typeof window.dispatchEvent === "function" && typeof Event === "function") {
            window.dispatchEvent(new Event("load"));
        }
    } catch (_) {}
    // An adapter may observe completion, but cannot replace this lifecycle
    // delivery or perform a product edit.
    if (typeof _wpt_after_onload === "function") _wpt_after_onload();
}

function _wpt_print_summary() {
    console.log("WPT_RESULT: " + _wpt_pass + "/" + _wpt_total + " passed");
}
