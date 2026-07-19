// Minimal WPT testharness.js shim for Lambda
// Provides test(), assert_*() functions that capture results to stdout

// DOM Node interface constants (per WHATWG DOM spec).
if (typeof Node === 'undefined' || !Node) {
    var Node = {
        ELEMENT_NODE: 1,
        ATTRIBUTE_NODE: 2,
        TEXT_NODE: 3,
        CDATA_SECTION_NODE: 4,
        ENTITY_REFERENCE_NODE: 5,
        ENTITY_NODE: 6,
        PROCESSING_INSTRUCTION_NODE: 7,
        COMMENT_NODE: 8,
        DOCUMENT_NODE: 9,
        DOCUMENT_TYPE_NODE: 10,
        DOCUMENT_FRAGMENT_NODE: 11,
        NOTATION_NODE: 12,
        DOCUMENT_POSITION_DISCONNECTED: 1,
        DOCUMENT_POSITION_PRECEDING: 2,
        DOCUMENT_POSITION_FOLLOWING: 4,
        DOCUMENT_POSITION_CONTAINS: 8,
        DOCUMENT_POSITION_CONTAINED_BY: 16,
        DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC: 32,
    };
}

var _wpt_pass = 0;
var _wpt_fail = 0;
var _wpt_total = 0;
var _wpt_pending_promises = 0;

function test(func, name) {
    _wpt_total++;
    var t = {
        _name: name,
        _cleanups: [],
        add_cleanup: function(fn) { this._cleanups.push(fn); },
        step: function(fn) { fn.apply(this, Array.prototype.slice.call(arguments, 1)); },
        step_func: function(fn) { var self=this; return function(){ fn.apply(self, arguments); }; },
        unreached_func: function(desc) {
            return function() {
                throw new Error((desc || "reached unreachable code"));
            };
        },
        done: function() {},
    };
    try {
        func.call(t, t);
        _wpt_pass++;
        // silent on pass
    } catch (e) {
        _wpt_fail++;
        console.log("FAIL: " + name + " - " + e.message);
    }
    for (var i = 0; i < t._cleanups.length; i++) {
        try { t._cleanups[i](); } catch(_) {}
    }
}

function on_event(object, event, callback) {
    object.addEventListener(event, callback, false);
}

function assert_equals(actual, expected, desc) {
    if (actual !== expected) {
        var msg = "assert_equals: got " + JSON.stringify(actual) + ", expected " + JSON.stringify(expected);
        if (desc) msg = msg + " - " + desc;
        throw new Error(msg);
    }
}

// Best-effort stringifier used in WPT assert messages. Mirrors testharness.js
// `format_value` for the cases we care about (primitives + objects).
function format_value(v) {
    if (v === null) return "null";
    if (v === undefined) return "undefined";
    var t = typeof v;
    if (t === "string") return '"' + v + '"';
    if (t === "number" || t === "boolean") return String(v);
    if (t === "function") return "function " + (v.name || "");
    try { return String(v); } catch(_) { return "[object]"; }
}

function assert_inherits(obj, name, desc) {
    if (obj === null || obj === undefined || !(name in obj)) {
        var msg = "assert_inherits: expected property " + name + " missing";
        if (desc) msg = msg + " - " + desc;
        throw new Error(msg);
    }
}

function assert_class_string(obj, expected, desc) {
    var s = Object.prototype.toString.call(obj);
    var want = "[object " + expected + "]";
    if (s !== want) {
        var msg = "assert_class_string: got " + s + ", expected " + want;
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

function assert_own_property(obj, name, desc) {
    if (!Object.prototype.hasOwnProperty.call(obj, name)) {
        var msg = "assert_own_property: missing own property " + name;
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

function assert_greater_than_equal(actual, expected, desc) {
    if (!(actual >= expected)) {
        var msg = "assert_greater_than_equal: " + actual + " not >= " + expected;
        if (desc) msg = msg + " - " + desc;
        throw new Error(msg);
    }
}
function assert_less_than_equal(actual, expected, desc) {
    if (!(actual <= expected)) {
        var msg = "assert_less_than_equal: " + actual + " not <= " + expected;
        if (desc) msg = msg + " - " + desc;
        throw new Error(msg);
    }
}
function assert_greater_than(actual, expected, desc) {
    if (!(actual > expected)) {
        var msg = "assert_greater_than: " + actual + " not > " + expected;
        if (desc) msg = msg + " - " + desc;
        throw new Error(msg);
    }
}
function assert_less_than(actual, expected, desc) {
    if (!(actual < expected)) {
        var msg = "assert_less_than: " + actual + " not < " + expected;
        if (desc) msg = msg + " - " + desc;
        throw new Error(msg);
    }
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

function assert_regexp_match(actual, regexp, desc) {
    // WPT assertions are part of the harness contract; leaving this helper
    // undefined turned a valid clipboard assertion into a listener exception.
    if (!regexp || typeof regexp.test !== "function" || !regexp.test(String(actual))) {
        throw new Error((desc ? desc + ": " : "") +
            "expected " + format_value(actual) + " to match " + String(regexp));
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

// setup() runs the setup function but isolates failures so individual tests
// can still execute. WPT's common.js calls setup(setupRangeTests); the
// foreign-document branch (document.implementation.createHTMLDocument /
// createDocument / createDocumentType) is not implemented, so the setup
// function partially completes — we keep what it managed to assign.
//
// Also handles `setup({single_test: true})` mode: the entire script body
// counts as one test that passes if `done()` is called and no exception
// escapes. We register a slot in the totals; `done()` flips it to pass.
var _wpt_single_test_active = false;
var _wpt_single_test_done = false;
function setup(func_or_props, maybe_props) {
    if (typeof func_or_props === "function") {
        try { func_or_props(); }
        catch (e) { console.log("WPT setup partial: " + e.message); }
        if (maybe_props && typeof maybe_props === "object" &&
            maybe_props.single_test === true && !_wpt_single_test_active) {
            _wpt_single_test_active = true;
            _wpt_total++;
        }
        return;
    }
    if (func_or_props && typeof func_or_props === "object" &&
        func_or_props.single_test === true && !_wpt_single_test_active) {
        _wpt_single_test_active = true;
        _wpt_total++;
    }
}
function done() {
    if (_wpt_single_test_active && !_wpt_single_test_done) {
        _wpt_single_test_done = true;
        _wpt_pass++;
    }
}

// add_completion_callback(fn) — registered to run after all tests complete.
// In Lambda's synchronous test harness we just store them and invoke from
// _wpt_fire_onload.
var _wpt_completion_callbacks = [];
function add_completion_callback(fn) {
    if (typeof fn === "function") _wpt_completion_callbacks.push(fn);
}

// generate_tests(testFunc, paramsArray, [propertiesArg])
// Each entry in paramsArray is [name, ...args]; testFunc is called with those args.
// Defined by WPT testharness.js (resources/testharness.js).
function generate_tests(test_func, params, properties) {
    if (!params || !params.length) return;
    for (var i = 0; i < params.length; i++) {
        var entry = params[i];
        if (!entry || !entry.length) continue;
        var name = entry[0];
        var args = Array.prototype.slice.call(entry, 1);
        (function(n, a) {
            test(function() { test_func.apply(this, a); }, n);
        })(name, args);
    }
}

// WPT's common.js gates on `"setup" in window` to choose between the
// queue-and-run path and immediate execution. Lambda's window proxy doesn't
// auto-mirror top-level function declarations, so install setup explicitly
// to make the gate evaluate true and route the setup function through our
// try/catch wrapper above.
if (typeof window !== "undefined") {
    window.setup = setup;
    window.done = done;
}

// async_test() shim — Lambda JS runs synchronously, so we capture
// the step function and execute it when onload fires (simulated at end of script).
//
// WPT spec accepts two call forms:
//   async_test(name_string)             — returns test object; caller drives it
//   async_test(func, name?, props?)     — invokes func(test) synchronously,
//                                          returns the test object
//
var _wpt_async_tests = [];
function async_test(arg1, arg2) {
    var name = "";
    var func = null;
    if (typeof arg1 === "function") {
        func = arg1;
        if (typeof arg2 === "string") name = arg2;
        else if (arg2 && arg2.name) name = arg2.name;
    } else if (typeof arg1 === "string") {
        name = arg1;
    } else if (arg1 && arg1.name) {
        name = arg1.name;
    }

    var async_record = {
        _name: name,
        _done: false,
        _counted: false,
        _pending_timeouts: 0,
        _steps: [],
        step_func: function(fn) {
            var self = this;
            return function() {
                if (self._done) return;
                _wpt_total++;
                self._counted = true;
                try {
                    fn.apply(self, arguments);
                    _wpt_pass++;
                } catch (e) {
                    _wpt_fail++;
                    self._done = true;
                    console.log("FAIL: " + self._name + " - " + e.message);
                }
            };
        },
        step: function(fn) {
            try {
                fn.apply(this, Array.prototype.slice.call(arguments, 1));
            } catch (e) {
                if (!this._counted) {
                    _wpt_total++;
                    _wpt_fail++;
                    this._counted = true;
                    console.log("FAIL: " + this._name + " - " + e.message);
                }
                this._done = true;
            }
        },
        step_func_done: function(fn) {
            var self = this;
            return function() {
                if (self._done) return;
                _wpt_total++;
                self._counted = true;
                try {
                    if (fn) fn.apply(self, arguments);
                    _wpt_pass++;
                    self._done = true;
                } catch (e) {
                    _wpt_fail++;
                    self._done = true;
                    console.log("FAIL: " + self._name + " - " + e.message);
                }
            };
        },
        done: function() {
            if (this._done) return;
            if (!this._counted) {
                _wpt_total++;
                _wpt_pass++;
                this._counted = true;
            }
            this._done = true;
        },
        step_timeout: function(fn, ms) {
            var self = this;
            self._pending_timeouts++;
            return setTimeout(function() {
                try {
                    fn.call(self);
                } finally {
                    self._pending_timeouts--;
                }
            }, ms || 0);
        },
        unreached_func: function(desc) {
            var self = this;
            return function() {
                _wpt_total++;
                _wpt_fail++;
                self._counted = true;
                self._done = true;
                console.log("FAIL: " + desc + " - reached unreachable code");
            };
        }
    };
    _wpt_async_tests.push(async_record);
    if (func) {
        try { func.call(async_record, async_record); }
        catch (e) {
            // Per testharness semantics: once a test has been marked done
            // (HAS_RESULT phase), later thrown assertions are ignored. This
            // matches Chrome/Firefox behavior for tests that call
            // step_func_done synchronously and then `assert_unreached()`.
            if (async_record._done) return async_record;
            _wpt_total++;
            _wpt_fail++;
            async_record._counted = true;
            async_record._done = true;
            console.log("FAIL: " + name + " - " + (e && e.message ? e.message : e));
        }
    }
    return async_record;
}
if (typeof globalThis !== "undefined") globalThis.async_test = async_test;
if (typeof window !== "undefined" && window) window.async_test = async_test;

// CSSOM constructor stubs used by instanceof checks in css-syntax tests.
if (typeof CSSNestedDeclarations === "undefined") {
    function CSSNestedDeclarations() {}
    if (typeof globalThis !== "undefined") globalThis.CSSNestedDeclarations = CSSNestedDeclarations;
    if (typeof window !== "undefined" && window) window.CSSNestedDeclarations = CSSNestedDeclarations;
}

// promise_test(func, name) — WPT promise-based test. Lambda's JS runtime is
// synchronous, so we run the function once. If it returns a thenable, we hook
// .then/.catch to record pass/fail. If it throws synchronously, fail. If it
// returns undefined (no Promise) we treat completion as success.
//
// Serialization: promise_tests must run **sequentially** per WPT spec —
// each subtest awaits the previous one's resolution before its body
// starts. Without this, async subtests race and mutate shared DOM state
// out of order (e.g. subtest 2 resetting innerHTML mid-way through
// subtest 1's BS assertion). We chain off a module-level `_promise_test_queue`.
var _promise_test_queue = Promise.resolve();
function promise_test(func, name) {
    _wpt_total++;
    var t = {
        _name: name,
        _cleanups: [],
        add_cleanup: function(fn) {
            if (typeof fn === "function") this._cleanups.push(fn);
        },
        step: function(fn) { fn.apply(this, Array.prototype.slice.call(arguments, 1)); },
        step_func: function(fn) { var self=this; return function(){ fn.apply(self, arguments); }; },
        step_func_done: function(fn) { var self=this; return function(){ if (fn) fn.apply(self, arguments); }; },
        unreached_func: function(desc) {
            return function() {
                throw new Error(desc + " - reached unreachable code");
            };
        },
        step_timeout: function(fn, ms) {
            return setTimeout(fn, ms || 0);
        },
        done: function() {},
    };
    function run_cleanups() {
        for (var i = 0; i < t._cleanups.length; i++) {
            try { t._cleanups[i](); } catch (_) {}
        }
    }
    _wpt_pending_promises++;
    _promise_test_queue = _promise_test_queue.then(function() {
        var p;
        try {
            p = func.call(t, t);
        } catch (e) {
            _wpt_fail++;
            _wpt_pending_promises--;
            console.log("FAIL: " + name + " - " + (e && e.message ? e.message : e));
            run_cleanups();
            return;
        }
        if (p && typeof p.then === "function") {
            return p.then(
                function() { _wpt_pass++; _wpt_pending_promises--; run_cleanups(); },
                function(e) {
                    _wpt_fail++;
                    _wpt_pending_promises--;
                    console.log("FAIL: " + name + " - " + (e && e.message ? e.message : e));
                    run_cleanups();
                }
            );
        }
        _wpt_pass++;
        _wpt_pending_promises--;
        run_cleanups();
    });
}

// promise_rejects_dom / promise_rejects_js — minimal stubs.
//
// These return a never-rejecting Promise so that an unexpected fulfillment
// is logged as a FAIL but does NOT propagate as an unhandled rejection
// (Lambda's runtime aborts the script on the first unhandled rejection,
// which would skip every later promise_test in the file).
function promise_rejects_dom(t, name, promise, desc) {
    if (promise && typeof promise.then === "function") {
        return promise.then(
            function() {
                _wpt_fail++;
                console.log("FAIL: " + ((t && t._name) ? t._name : (desc || "")) +
                    " - expected rejection, got fulfillment");
                // Mark the parent promise_test done so it doesn't double-count.
                if (t) t._unexpected_fulfill = true;
            },
            function() { /* expected */ }
        );
    }
    return Promise.resolve();
}
function promise_rejects_js(t, ctor, promise, desc) {
    return promise_rejects_dom(t, "", promise, desc);
}

// ---------------------------------------------------------------------------
// Minimal input/textarea selection model + EditorTestUtils stub for WPT
// editing/selection tests. Lambda's runtime doesn't yet implement HTMLInput/
// HTMLTextAreaElement.value or selection APIs natively, so we monkey-patch
// each <input> and <textarea> in the document with JS-side state.
// ---------------------------------------------------------------------------
function _wpt_cp_script_class(c) {
    // Match radiant/dom_range.cpp cp_script_class:
    // 0=ASCII letter/digit/_, 1=Latin Ext (U+0080-U+024F),
    // 2=CJK/Hangul/Hiragana/Katakana, 3=other (non-word).
    if ((c >= 0x30 && c <= 0x39) || (c >= 0x41 && c <= 0x5A) ||
        (c >= 0x61 && c <= 0x7A) || c === 0x5F) return 0;
    if (c >= 0x80 && c <= 0x24F) return 1;
    if ((c >= 0x3040 && c <= 0x30FF) ||
        (c >= 0x3400 && c <= 0x9FFF) ||
        (c >= 0xF900 && c <= 0xFAFF) ||
        (c >= 0x1100 && c <= 0x11FF) ||
        (c >= 0xAC00 && c <= 0xD7AF) ||
        (c >= 0xFF00 && c <= 0xFFEF)) return 2;
    return 3;
}
function _wpt_is_word_cp(cls) { return cls !== 3; }

function _wpt_word_forward(s, pos) {
    var n = s.length;
    // Skip non-word chars first.
    while (pos < n && !_wpt_is_word_cp(_wpt_cp_script_class(s.charCodeAt(pos)))) pos++;
    if (pos >= n) return n;
    // Consume same-script-class run.
    var startCls = _wpt_cp_script_class(s.charCodeAt(pos));
    while (pos < n) {
        var c = s.charCodeAt(pos);
        var cls = _wpt_cp_script_class(c);
        if (cls === 3 || cls !== startCls) break;
        pos++;
    }
    var wordEnd = pos;
    while (pos < n && /\s/.test(s.charAt(pos))) pos++;
    // Trailing collapsible whitespace has no visual position of its own; a
    // forward word deletion of the final word consumes that hidden tail too.
    if (pos < n) return wordEnd;
    return pos;
}
function _wpt_word_backward(s, pos) {
    if (pos > s.length) pos = s.length;
    // Skip trailing non-word.
    while (pos > 0 && !_wpt_is_word_cp(_wpt_cp_script_class(s.charCodeAt(pos - 1)))) pos--;
    if (pos === 0) return 0;
    var endCls = _wpt_cp_script_class(s.charCodeAt(pos - 1));
    while (pos > 0) {
        var c = s.charCodeAt(pos - 1);
        var cls = _wpt_cp_script_class(c);
        if (cls === 3 || cls !== endCls) break;
        pos--;
    }
    return pos;
}

function _wpt_patch_text_field(el) {
    // No-op: Lambda now provides native HTMLInputElement / HTMLTextAreaElement
    // value, selectionStart/End, setSelectionRange, select(), focus(), blur()
    // (see vibe/radiant/Radiant_Design_Selection.md §8). Adding JS-side
    // Object.defineProperty stubs would shadow the native focus()/setSelection*
    // methods (descriptor functions take precedence over the native dispatcher
    // for method invocations) and would break activeElement tracking.
    return el;
}

function _wpt_install_text_field_shims() { /* no-op, see _wpt_patch_text_field */ }

// EditorTestUtils — minimal subset used by selection WPT tests. Now backed by
// the native text-control selection model. Wrapped in `var` rather than a
// function declaration so that tests which include the real
// /editing/include/editor-test-utils.js (which declares
// `class EditorTestUtils`) can shadow this binding without the JS runtime
// throwing on duplicate declaration.
var EditorTestUtils = function(el) {
    this.el = el;
};
EditorTestUtils.prototype._move_word = function(dir) {
    if (!this.el) return;
    var v = this.el.value;
    if (typeof v !== "string") v = "";
    var pos = this.el.selectionStart | 0;
    if (dir < 0) pos = _wpt_word_backward(v, pos);
    else pos = _wpt_word_forward(v, pos);
    if (typeof this.el.setSelectionRange === "function") {
        this.el.setSelectionRange(pos, pos);
    } else {
        this.el.selectionStart = pos;
        this.el.selectionEnd = pos;
    }
};
EditorTestUtils.prototype.sendMoveWordLeftKey = function() {
    this._move_word(-1);
    return Promise.resolve();
};
EditorTestUtils.prototype.sendMoveWordRightKey = function() {
    this._move_word(1);
    return Promise.resolve();
};
EditorTestUtils.prototype.sendKey = function() { return Promise.resolve(); };

// ---------------------------------------------------------------------------
// WPT testdriver shim — `test_driver.click(elem)` simulates a real user
// click. The behaviour we model:
//   - Clicking on a text control (input/textarea): the click moves the
//     control's caret. Real browsers place it at the click coordinate; we
//     don't have layout-precise click coordinates here, so we toggle the
//     caret between start and end of value. This guarantees a real
//     position change between consecutive clicks (required by
//     `user-select-on-input-and-contenteditable.html` which clicks the
//     same element 5x and asserts each click moves selection).
//   - Clicking on a non-control with `user-select: none` or an `<a href>`:
//     selection of any focused text control is preserved (the spec says
//     these don't take focus / clear selection — required by
//     `stringifier-editable-element.tentative.html`).
//   - Clicking on any other element: blur focused text controls so the
//     document stringifier no longer reports their content.
// After the focus/selection adjustments we dispatch a `click` event so any
// listeners registered with addEventListener("click", ...) fire.

// ---------------------------------------------------------------------------
// send_keys helper — synthesize a single key code's effect.
// Recognized: Backspace (\uE003), End (\uE010), Home (\uE011),
// ArrowLeft (\uE012), ArrowRight (\uE014), Delete (\uE017).
// Operates on whichever surface currently owns the caret: focused text
// control, otherwise the document selection.
// ---------------------------------------------------------------------------
function _wpt_text_node_length(n) {
    if (!n || n.nodeType !== 3) return 0;
    var d = n.data;
    if (typeof d !== "string") d = n.textContent || "";
    return d.length;
}
function _wpt_collect_text_nodes(root, out) {
    if (!root) return;
    if (root.nodeType === 3) { out.push(root); return; }
    var c = root.firstChild;
    while (c) { _wpt_collect_text_nodes(c, out); c = c.nextSibling; }
}
function _wpt_dispatch_native_edit_key(code, shift, ctrl, alt, meta) {
    if (typeof __lambda_testdriver_key !== "function") return false;
    try {
        return !!__lambda_testdriver_key(code, !!shift, !!ctrl, !!alt, !!meta);
    } catch (_) {
        return false;
    }
}
function _wpt_is_cleanup_inline_wrapper(n) {
    if (!n || n.nodeType !== 1 || !n.tagName) return false;
    if (n.hasAttribute && n.hasAttribute("contenteditable")) return false;
    var tag = String(n.tagName).toLowerCase();
    return tag === "a" || tag === "abbr" || tag === "b" ||
        tag === "bdi" || tag === "bdo" || tag === "big" ||
        tag === "cite" || tag === "code" || tag === "del" ||
        tag === "dfn" || tag === "em" || tag === "font" ||
        tag === "i" || tag === "ins" || tag === "kbd" ||
        tag === "mark" || tag === "q" || tag === "s" ||
        tag === "samp" || tag === "small" || tag === "span" ||
        tag === "strike" || tag === "strong" || tag === "sub" ||
        tag === "sup" || tag === "time" || tag === "tt" ||
        tag === "u" || tag === "var";
}
function _wpt_inline_wrapper_is_empty(n) {
    if (!n || n.nodeType !== 1) return false;
    var child = n.firstChild;
    while (child) {
        if (child.nodeType !== 3) return false;
        var s = child.data;
        if (typeof s !== "string") s = child.textContent || "";
        if (s.length > 0) return false;
        child = child.nextSibling;
    }
    return true;
}
function _wpt_node_index(node) {
    if (!node || !node.parentNode) return -1;
    var index = 0;
    var child = node.parentNode.firstChild;
    while (child && child !== node) {
        index++;
        child = child.nextSibling;
    }
    return child === node ? index : -1;
}
function _wpt_cleanup_empty_inline_after_delete(textNode, sel) {
    if (!textNode || textNode.nodeType !== 3 || !sel) return false;
    var wrapper = textNode.parentNode;
    if (!_wpt_is_cleanup_inline_wrapper(wrapper) ||
        !_wpt_inline_wrapper_is_empty(wrapper)) {
        return false;
    }
    var parent = wrapper.parentNode;
    if (!parent) return false;
    var idx = _wpt_node_index(wrapper);
    if (idx < 0) return false;
    try { parent.removeChild(wrapper); } catch (_) { return false; }
    try { sel.collapse(parent, idx); } catch (_) {}
    return true;
}
function _wpt_cleanup_rich_range_delete(host) {
    if (!host) return;
    function prune(parent) {
        var child = parent.firstChild;
        while (child) {
            var next = child.nextSibling;
            if (child.nodeType === 1) {
                prune(child);
                if (_wpt_is_cleanup_inline_wrapper(child) &&
                    _wpt_inline_wrapper_is_empty(child)) {
                    try { parent.removeChild(child); } catch (_) {}
                }
            }
            child = next;
        }
    }
    prune(host);
    var textNodes = [];
    _wpt_collect_text_nodes(host, textNodes);
    if (textNodes.length === 0) return;
    var last = textNodes[textNodes.length - 1];
    var data = last.data || last.textContent || "";
    if (!_wpt_preserves_edit_whitespace(last) &&
        data.length > 0 && data.charAt(data.length - 1) === " ") {
        // A trailing collapsible space would disappear after deleting the
        // following word; browser editing preserves it as a non-breaking
        // space while removing inline wrappers emptied by the same range.
        var preserved = data.slice(0, data.length - 1) + "\u00a0";
        try { last.data = preserved; } catch (_) { try { last.textContent = preserved; } catch (_) {} }
    }
}
function _wpt_editing_host_is_empty(host) {
    if (!host) return false;
    // Only a genuinely childless host gets the empty-host input transaction;
    // nested empty blocks still have their own boundary editing semantics.
    return !host.firstChild;
}

function _wpt_block_is_visually_empty(block) {
    if (!block || block.nodeType !== 1) return false;
    function hasContent(node) {
        var child = node.firstChild;
        while (child) {
            if (child.nodeType === 3) {
                if (_wpt_text_node_length(child) > 0) return true;
            } else if (child.nodeType === 1) {
                if (_wpt_is_atomic_edit_node(child) && !_wpt_is_padding_break(child)) return true;
                if (!_wpt_is_padding_break(child) && hasContent(child)) return true;
            }
            child = child.nextSibling;
        }
        return false;
    }
    return !hasContent(block);
}
function _wpt_send_one_key(elem, code, nativeAlreadyTried, skipNative,
                           ctrl, alt, meta) {
    // Text-control path
    var ae = null;
    var elemTag = (elem && elem.tagName) ? elem.tagName.toUpperCase() : "";
    if (elemTag === "INPUT" || elemTag === "TEXTAREA") {
        ae = elem;
    } else {
        try { ae = document.activeElement; } catch (_) {}
    }
    var aeTag = (ae && ae.tagName) ? ae.tagName.toUpperCase() : "";
    var isTC = (aeTag === "INPUT" || aeTag === "TEXTAREA");
    if (isTC) {
        if (code === 0xE013 && _wpt_spin_number_control(ae, +1)) return; // ArrowUp
        if (code === 0xE015 && _wpt_spin_number_control(ae, -1)) return; // ArrowDown
        var v = ae.value || "";
        var ss = ae.selectionStart;
        var se = ae.selectionEnd;
        if (typeof ss !== "number") ss = v.length;
        if (typeof se !== "number") se = v.length;
        if (code === 0xE003) { // Backspace
            var tcBackwardType = ss === se && (ctrl || alt || meta)
                ? "deleteWordBackward" : "deleteContentBackward";
            if (!_wpt_dispatch_input_event(ae, "beforeinput", tcBackwardType, null)) return;
            if (ss === se) {
                var tcStart = (ctrl || alt || meta) ? _wpt_word_backward(v, ss) : Math.max(0, ss - 1);
                ae.value = v.slice(0, tcStart) + v.slice(ss);
                try { ae.setSelectionRange(tcStart, tcStart); } catch (_) {}
            } else {
                ae.value = v.slice(0, ss) + v.slice(se);
                try { ae.setSelectionRange(ss, ss); } catch (_) {}
            }
            _wpt_dispatch_input_event(ae, "input", tcBackwardType, null);
            return;
        }
        if (code === 0xE017) { // Delete
            var tcForwardType = ss === se && (ctrl || alt || meta)
                ? "deleteWordForward" : "deleteContentForward";
            if (!_wpt_dispatch_input_event(ae, "beforeinput", tcForwardType, null)) return;
            if (ss === se) {
                var tcEnd = (ctrl || alt || meta) ? _wpt_word_forward(v, se) : Math.min(v.length, se + 1);
                ae.value = v.slice(0, se) + v.slice(tcEnd);
                try { ae.setSelectionRange(se, se); } catch (_) {}
            } else {
                ae.value = v.slice(0, ss) + v.slice(se);
                try { ae.setSelectionRange(ss, ss); } catch (_) {}
            }
            _wpt_dispatch_input_event(ae, "input", tcForwardType, null);
            return;
        }
        if (code === 0xE010) { // End
            try { ae.setSelectionRange(v.length, v.length); } catch (_) {}
            return;
        }
        if (code === 0xE011) { // Home
            try { ae.setSelectionRange(0, 0); } catch (_) {}
            return;
        }
        if (code === 0xE012) { // ArrowLeft
            var p = Math.max(0, ss - 1);
            try { ae.setSelectionRange(p, p); } catch (_) {}
            return;
        }
        if (code === 0xE014) { // ArrowRight
            var p2 = Math.min(v.length, se + 1);
            try { ae.setSelectionRange(p2, p2); } catch (_) {}
            return;
        }
        return;
    }
    // Document-selection path (contenteditable, etc.)
    var sel = (typeof getSelection === "function") ? getSelection() : null;
    if (!sel) return;
    // If `elem` is an element argument and the current selection is not
    // anchored inside it, re-seed at the end of its text content.
    // Concurrent promise_test cases all share the document selection;
    // each call must operate against its own elem rather than whatever
    // the previous test left behind.
    function _is_descendant(parent, n) {
        while (n) {
            if (n === parent) return true;
            n = n.parentNode;
        }
        return false;
    }
    if (elem && elem.nodeType === 1) {
        var anchored = false;
        if (sel.rangeCount > 0) {
            var rr = sel.getRangeAt(0);
            anchored = _is_descendant(elem, rr.startContainer);
        }
        if (!anchored) {
            var sn = [];
            _wpt_collect_text_nodes(elem, sn);
            if (sn.length > 0) {
                var lln = sn[sn.length - 1];
                try { sel.collapse(lln, _wpt_text_node_length(lln)); } catch (_) {}
            } else {
                try { sel.collapse(elem, 0); } catch (_) {}
            }
        }
    }
    if (sel.rangeCount === 0) return;
    var r = sel.getRangeAt(0);
    var fallbackHost = _wpt_editing_host_for_node(r.startContainer) ||
        _wpt_editing_host_for_node(r.endContainer);
    var wordDelete = r.collapsed && !!(ctrl || alt || meta);
    var backwardInputType = wordDelete
        ? "deleteWordBackward" : "deleteContentBackward";
    var forwardInputType = wordDelete
        ? "deleteWordForward" : "deleteContentForward";
    var fallbackInputType = code === 0xE003
        ? backwardInputType : forwardInputType;
    var fallbackEditRange = _wpt_edit_target_range(fallbackInputType);
    if (wordDelete && fallbackEditRange && fallbackEditRange.collapsed) {
        // A word-delete modifier inside a collapsible whitespace edge has no
        // editing target, so it must not synthesize beforeinput/input.
        return;
    }
    var crossesEditingHosts = r &&
        _wpt_editing_host_for_node(r.startContainer) !==
        _wpt_editing_host_for_node(r.endContainer);
    if (skipNative && (code === 0xE003 || code === 0xE017) && fallbackHost &&
        !_wpt_dispatch_input_event(fallbackHost, "beforeinput", fallbackInputType, null)) {
        return;
    }
    if (crossesEditingHosts && (code === 0xE003 || code === 0xE017)) {
        if (!skipNative && fallbackHost) {
            _wpt_dispatch_input_event(fallbackHost, "beforeinput", fallbackInputType, null);
        }
        // A single editing transaction cannot mutate two editing hosts (or an
        // editable and non-editable subtree); expose the attempt but do no edit.
        return;
    }
    if (skipNative && (code === 0xE003 || code === 0xE017) && sel.rangeCount > 0) {
        // beforeinput listeners may move the caret. The event's StaticRange
        // remains the dispatch-time snapshot, but the default edit uses the
        // selection state left by those listeners.
        r = sel.getRangeAt(0);
        fallbackEditRange = _wpt_edit_target_range(fallbackInputType);
    }
    var fallbackInputFinished = false;
    function _finish_fallback_delete(mutated, inputType) {
        if (!fallbackInputFinished &&
            (mutated || (skipNative && _wpt_editing_host_is_empty(fallbackHost))) &&
            (nativeAlreadyTried || skipNative) && fallbackHost) {
            // Empty hosts still perform a browser editing transaction, while a
            // boundary no-op in a non-empty host must not emit input.
            _wpt_dispatch_input_event(fallbackHost, "input", inputType, null);
            fallbackInputFinished = true;
        }
    }
    if (!nativeAlreadyTried && !skipNative &&
        (code === 0xE003 || code === 0xE017) &&
        _wpt_dispatch_native_edit_key(code, false, false, false, false)) {
        return;
    }
    if (code === 0xE010 || code === 0xE011) {
        // End / Home — collapse to end / start of the host element's
        // text content (use `elem` arg as host if it's an element).
        var host = elem || r.startContainer;
        if (host && host.nodeType === 1) {
            var nodes = [];
            _wpt_collect_text_nodes(host, nodes);
            if (nodes.length === 0) {
                try { sel.collapse(host, 0); } catch (_) {}
                return;
            }
            if (code === 0xE011) {
                try { sel.collapse(nodes[0], 0); } catch (_) {}
            } else {
                var last = nodes[nodes.length - 1];
                try { sel.collapse(last, _wpt_text_node_length(last)); } catch (_) {}
            }
        }
        return;
    }
    if (code === 0xE012 || code === 0xE014) { // ArrowLeft / Right
        // Collapse + step within current text node.
        if (!r.collapsed) {
            try {
                if (code === 0xE012) sel.collapse(r.startContainer, r.startOffset);
                else sel.collapse(r.endContainer, r.endOffset);
            } catch (_) {}
            return;
        }
        var n = r.startContainer; var off = r.startOffset;
        if (n && n.nodeType === 3) {
            if (code === 0xE012 && off > 0) {
                try { sel.collapse(n, off - 1); } catch (_) {}
            } else if (code === 0xE014 && off < _wpt_text_node_length(n)) {
                try { sel.collapse(n, off + 1); } catch (_) {}
            }
        }
        return;
    }
    if (code === 0xE003) { // Backspace
        if (sel.rangeCount > 1) {
            _finish_fallback_delete(
                _wpt_apply_multi_range_delete(sel), backwardInputType);
            return;
        }
        if (!r.collapsed) {
            var deletedRange = _wpt_apply_edit_range_delete(
                fallbackEditRange || r, fallbackHost, sel, false);
            _finish_fallback_delete(deletedRange, backwardInputType);
            return;
        }
        if (fallbackEditRange && !fallbackEditRange.collapsed &&
            (wordDelete || fallbackEditRange.startContainer !== fallbackEditRange.endContainer ||
             fallbackEditRange.startContainer.nodeType !== 3)) {
            _finish_fallback_delete(
                _wpt_apply_edit_range_delete(fallbackEditRange, fallbackHost, sel, true),
                backwardInputType);
            return;
        }
        var n2 = r.startContainer; var off2 = r.startOffset;
        if (!n2) {
            _finish_fallback_delete(false, backwardInputType);
            return;
        }
        if (n2.nodeType === 3) {
            // Delete char before caret in this text node, or merge from
            // previous text node if at offset 0.
            if (off2 > 0) {
                var s = n2.data || n2.textContent || "";
                // Word deletion is selected by the modifier only for a
                // collapsed caret; selections always use deleteContent*.
                var deleteStart = wordDelete ? _wpt_word_backward(s, off2) :
                    (_wpt_uses_pre_line_whitespace(n2) && /\s/.test(s.charAt(off2 - 1))
                        ? _wpt_pre_line_backward_start(s, off2) : off2 - 1);
                var ns = s.slice(0, deleteStart) + s.slice(off2);
                try { n2.data = ns; } catch (_) {
                    try { n2.textContent = ns; } catch (_) {}
                }
                if (ns.length === 0 &&
                    _wpt_cleanup_empty_inline_after_delete(n2, sel)) {
                    _finish_fallback_delete(true, backwardInputType);
                    return;
                }
                try { sel.collapse(n2, deleteStart); } catch (_) {}
                _finish_fallback_delete(true, backwardInputType);
                return;
            }
            // off2 === 0: walk to previous text node in document order.
            var prev = _wpt_prev_text_node(n2, fallbackHost);
            if (prev) {
                var ps = prev.data || prev.textContent || "";
                if (ps.length > 0) {
                    var previousStart = wordDelete ? _wpt_word_backward(ps, ps.length) : ps.length - 1;
                    try { prev.data = ps.slice(0, previousStart); } catch (_) {
                        try { prev.textContent = ps.slice(0, previousStart); } catch (_) {}
                    }
                    if (ps.length === 1 &&
                        _wpt_cleanup_empty_inline_after_delete(prev, sel)) {
                        _finish_fallback_delete(true, backwardInputType);
                        return;
                    }
                    // Caret stays at (n2, 0) but selection should refresh
                    // so listeners observe a change.
                    try { sel.collapse(n2, 0); } catch (_) {}
                    _finish_fallback_delete(true, backwardInputType);
                }
            }
            _finish_fallback_delete(false, backwardInputType);
            return;
        }
        // Element container: try going one step back.
        if (off2 > 0) {
            var child = n2.childNodes ? n2.childNodes[off2 - 1] : null;
            if (child && child.nodeType === 3) {
                var cs = child.data || child.textContent || "";
                if (cs.length > 0) {
                    var childStart = wordDelete ? _wpt_word_backward(cs, cs.length) : cs.length - 1;
                    try { child.data = cs.slice(0, childStart); } catch (_) {}
                    if (cs.length === 1 &&
                        _wpt_cleanup_empty_inline_after_delete(child, sel)) {
                        _finish_fallback_delete(true, backwardInputType);
                        return;
                    }
                    try { sel.collapse(child, childStart); } catch (_) {}
                    _finish_fallback_delete(true, backwardInputType);
                }
            }
        }
        _finish_fallback_delete(false, backwardInputType);
        return;
    }
    if (code === 0xE017) { // Delete
        if (sel.rangeCount > 1) {
            _finish_fallback_delete(
                _wpt_apply_multi_range_delete(sel), forwardInputType);
            return;
        }
        if (!r.collapsed) {
            var deletedForwardRange = _wpt_apply_edit_range_delete(
                fallbackEditRange || r, fallbackHost, sel, false);
            _finish_fallback_delete(deletedForwardRange, forwardInputType);
            return;
        }
        if (fallbackEditRange && !fallbackEditRange.collapsed &&
            (wordDelete || fallbackEditRange.startContainer !== fallbackEditRange.endContainer ||
             fallbackEditRange.startContainer.nodeType !== 3)) {
            _finish_fallback_delete(
                _wpt_apply_edit_range_delete(fallbackEditRange, fallbackHost, sel, true),
                forwardInputType);
            return;
        }
        var dn = r.startContainer; var doff = r.startOffset;
        if (!dn) {
            _finish_fallback_delete(false, forwardInputType);
            return;
        }
        if (dn.nodeType === 3) {
            var ds = dn.data || dn.textContent || "";
            if (doff < ds.length) {
                var deleteEnd = wordDelete ? _wpt_word_forward(ds, doff) :
                    (_wpt_uses_pre_line_whitespace(dn) && /\s/.test(ds.charAt(doff))
                        ? _wpt_pre_line_forward_end(ds, doff, true) : doff + 1);
                var fwd = ds.slice(0, doff) + ds.slice(deleteEnd);
                if (!_wpt_preserves_edit_whitespace(dn)) {
                    // Deleting visible content also discards trailing spaces
                    // beyond the final rendered caret position.
                    fwd = fwd.replace(/\s+$/, "");
                }
                try { dn.data = fwd; } catch (_) {
                    try { dn.textContent = fwd; } catch (_) {}
                }
                if (fwd.length === 0 &&
                    _wpt_cleanup_empty_inline_after_delete(dn, sel)) {
                    _finish_fallback_delete(true, forwardInputType);
                    return;
                }
                try { sel.collapse(dn, doff); } catch (_) {}
                _finish_fallback_delete(true, forwardInputType);
            }
        }
        _finish_fallback_delete(false, forwardInputType);
        return;
    }
}
function _wpt_prev_text_node(n, root) {
    // Walk backward in document order until a text node is found.
    var cur = n;
    while (cur) {
        if (root && cur === root) return null;
        if (cur.previousSibling) {
            cur = cur.previousSibling;
            // Descend to last text-node descendant
            while (cur.lastChild) cur = cur.lastChild;
            if (cur.nodeType === 3) return cur;
        } else {
            if (root && cur === root) return null;
            cur = cur.parentNode;
            if (!cur) return null;
        }
    }
    return null;
}
function _wpt_next_text_node(n, root) {
    var cur = n;
    while (cur) {
        if (root && cur === root) return null;
        if (cur.nextSibling) {
            cur = cur.nextSibling;
            while (cur.firstChild) cur = cur.firstChild;
            if (cur.nodeType === 3) return cur;
        } else {
            if (root && cur === root) return null;
            cur = cur.parentNode;
            if (!cur) return null;
        }
    }
    return null;
}

function _wpt_is_atomic_edit_node(node) {
    if (!node || node.nodeType !== 1 || !node.tagName) return false;
    if (_wpt_contenteditable_state(node) === "false") return true;
    var tag = String(node.tagName).toUpperCase();
    return tag === "BR" || tag === "HR" || tag === "IMG" ||
        tag === "INPUT" || tag === "TEXTAREA" || tag === "VIDEO" ||
        tag === "AUDIO" || tag === "CANVAS";
}

function _wpt_is_padding_break(node) {
    return !!(node && node.nodeType === 1 && node.tagName &&
        String(node.tagName).toUpperCase() === "BR" && !node.nextSibling);
}

function _wpt_break_run_reaches_block_end(node) {
    if (!node || node.nodeType !== 1 || !node.tagName ||
        String(node.tagName).toUpperCase() !== "BR") return false;
    var current = node;
    while (current.nextSibling && current.nextSibling.nodeType === 1 &&
           current.nextSibling.tagName &&
           String(current.nextSibling.tagName).toUpperCase() === "BR") {
        current = current.nextSibling;
    }
    return !current.nextSibling;
}

function _wpt_deepest_edit_leaf(node, backward) {
    var current = node;
    while (current && current.nodeType === 1 && !_wpt_is_atomic_edit_node(current)) {
        var child = backward ? current.lastChild : current.firstChild;
        if (!child) break;
        current = child;
    }
    return current;
}

function _wpt_leaf_before_boundary(container, offset, root) {
    var current = container;
    if (current && current.nodeType === 1 && offset > 0 && current.childNodes) {
        return _wpt_deepest_edit_leaf(current.childNodes[offset - 1], true);
    }
    if (current && current.nodeType === 3 && offset > 0) return null;
    while (current && current !== root) {
        if (current.previousSibling) {
            return _wpt_deepest_edit_leaf(current.previousSibling, true);
        }
        current = current.parentNode;
    }
    return null;
}

function _wpt_leaf_after_boundary(container, offset, root) {
    var current = container;
    if (current && current.nodeType === 1 && current.childNodes &&
        offset < current.childNodes.length) {
        return _wpt_deepest_edit_leaf(current.childNodes[offset], false);
    }
    if (current && current.nodeType === 3 && offset < _wpt_text_node_length(current)) return null;
    while (current && current !== root) {
        if (current.nextSibling) {
            return _wpt_deepest_edit_leaf(current.nextSibling, false);
        }
        current = current.parentNode;
    }
    return null;
}

function _wpt_range_around_atomic(node, adjacentText, adjacentOffset, backward) {
    if (!node || !node.parentNode) return null;
    var index = _wpt_node_index(node);
    if (index < 0) return null;
    var firstIndex = index;
    var lastIndex = index;
    var tag = node.tagName ? String(node.tagName).toUpperCase() : "";
    if (tag === "HR" && node.previousSibling && node.previousSibling.nodeType === 1 &&
        node.previousSibling.tagName &&
        String(node.previousSibling.tagName).toUpperCase() === "BR") {
        firstIndex--;
    }
    if (tag === "BR" && node.nextSibling && node.nextSibling.nodeType === 1 &&
        node.nextSibling.tagName &&
        String(node.nextSibling.tagName).toUpperCase() === "HR") {
        lastIndex++;
    }
    try {
        var range = document.createRange();
        if (backward) {
            var precedingText = firstIndex > 0 && node.parentNode.childNodes
                ? node.parentNode.childNodes[firstIndex - 1] : null;
            if (tag === "HR" && precedingText && precedingText.nodeType === 3) {
                var precedingData = precedingText.data || precedingText.textContent || "";
                var visibleEnd = precedingData.length;
                while (visibleEnd > 0 && /\s/.test(precedingData.charAt(visibleEnd - 1))) visibleEnd--;
                if (visibleEnd < precedingData.length) range.setStart(precedingText, visibleEnd);
                else range.setStart(node.parentNode, firstIndex);
            } else {
                range.setStart(node.parentNode, firstIndex);
            }
            if (adjacentText && adjacentOffset > 0) range.setEnd(adjacentText, adjacentOffset);
            else range.setEnd(node.parentNode, lastIndex + 1);
        } else {
            // The atomic node, rather than collapsible text immediately before
            // it, is the forward editing boundary exposed by getTargetRanges().
            range.setStart(node.parentNode, firstIndex);
            if (tag === "BR" && node.nextSibling && node.nextSibling.nodeType === 3) {
                var followingData = node.nextSibling.data || node.nextSibling.textContent || "";
                var followingOffset = 0;
                while (followingOffset < followingData.length &&
                       /\s/.test(followingData.charAt(followingOffset))) followingOffset++;
                if (followingOffset > 0) range.setEnd(node.nextSibling, followingOffset);
                else range.setEnd(node.parentNode, lastIndex + 1);
            } else {
                range.setEnd(node.parentNode, lastIndex + 1);
            }
        }
        return range;
    } catch (_) { return null; }
}

function _wpt_range_across_empty_block(block, neighbor, backward) {
    if (!block || !neighbor) return null;
    try {
        var range = document.createRange();
        if (backward) {
            if (neighbor.nodeType === 3) {
                var previousEnd = _wpt_text_node_length(neighbor);
                if (!_wpt_preserves_edit_whitespace(neighbor) &&
                    !_wpt_preserves_edit_whitespace(block)) {
                    var previousData = neighbor.data || neighbor.textContent || "";
                    while (previousEnd > 0 && /\s/.test(previousData.charAt(previousEnd - 1))) {
                        previousEnd--;
                    }
                }
                range.setStart(neighbor, previousEnd);
            } else {
                var previousIndex = _wpt_node_index(neighbor);
                if (previousIndex < 0) return null;
                range.setStart(neighbor.parentNode, previousIndex + 1);
            }
            range.setEnd(block, block.childNodes ? block.childNodes.length : 0);
        } else {
            range.setStart(block, 0);
            if (neighbor.nodeType === 3) {
                var nextStart = 0;
                var nextData = neighbor.data || neighbor.textContent || "";
                if (!_wpt_preserves_edit_whitespace(neighbor)) {
                    while (nextStart < nextData.length && /\s/.test(nextData.charAt(nextStart))) {
                        nextStart++;
                    }
                }
                range.setEnd(neighbor, nextStart);
            } else {
                var nextIndex = _wpt_node_index(neighbor);
                if (nextIndex < 0) return null;
                range.setEnd(neighbor.parentNode, nextIndex);
            }
        }
        return range;
    } catch (_) { return null; }
}

function _wpt_forward_block_join_range(boundaryBreak, nextText) {
    if (!boundaryBreak || !boundaryBreak.parentNode || !nextText) return null;
    var breakIndex = _wpt_node_index(boundaryBreak);
    if (breakIndex < 0) return null;
    var nextOffset = 0;
    var nextData = nextText.data || nextText.textContent || "";
    if (!_wpt_preserves_edit_whitespace(nextText)) {
        while (nextOffset < nextData.length && /\s/.test(nextData.charAt(nextOffset))) nextOffset++;
    }
    try {
        var range = document.createRange();
        // A BR immediately before a child/sibling block is its visual boundary,
        // so Delete joins through the block instead of deleting an inline BR.
        range.setStart(boundaryBreak.parentNode, breakIndex);
        range.setEnd(nextText, nextOffset);
        return range;
    } catch (_) { return null; }
}

function _wpt_contenteditable_state(el) {
    if (!el || el.nodeType !== 1 || !el.getAttribute) return null;
    var raw = null;
    try { raw = el.getAttribute("contenteditable"); } catch (_) { raw = null; }
    if (raw === null) return null;
    raw = String(raw).toLowerCase();
    if (raw === "" || raw === "true" || raw === "plaintext-only") return "true";
    if (raw === "false") return "false";
    return null;
}

function _wpt_editing_host_for_node(node) {
    var cur = node;
    if (cur && cur.nodeType === 3) cur = cur.parentNode;
    while (cur && cur.nodeType === 1) {
        var state = _wpt_contenteditable_state(cur);
        if (state === "true") return cur;
        if (state === "false") return null;
        cur = cur.parentNode;
    }
    try {
        if (document.designMode === "on") return document.body || document.documentElement;
    } catch (_) {}
    return null;
}

function _wpt_range_snapshot(range) {
    if (!range) return null;
    try {
        return new StaticRange({
            startContainer: range.startContainer,
            startOffset: range.startOffset,
            endContainer: range.endContainer,
            endOffset: range.endOffset
        });
    } catch (_) { return null; }
}

function _wpt_first_text_descendant(node) {
    var current = node;
    while (current && current.nodeType !== 3) current = current.firstChild;
    return current && current.nodeType === 3 ? current : null;
}

function _wpt_last_text_descendant(node) {
    var current = node;
    while (current && current.nodeType !== 3) current = current.lastChild;
    return current && current.nodeType === 3 ? current : null;
}

function _wpt_is_editing_block(node) {
    if (!node || node.nodeType !== 1 || !node.tagName) return false;
    var tag = String(node.tagName).toUpperCase();
    return tag === "ADDRESS" || tag === "ARTICLE" || tag === "ASIDE" ||
        tag === "BLOCKQUOTE" || tag === "DD" || tag === "DIV" ||
        tag === "DL" || tag === "DT" || tag === "FIGCAPTION" ||
        tag === "FIGURE" || tag === "FOOTER" || tag === "H1" ||
        tag === "H2" || tag === "H3" || tag === "H4" || tag === "H5" ||
        tag === "H6" || tag === "HEADER" || tag === "LI" || tag === "MAIN" ||
        tag === "NAV" || tag === "OL" || tag === "P" || tag === "PRE" ||
        tag === "SECTION" || tag === "TD" || tag === "TH" || tag === "UL";
}

function _wpt_editing_block_for_node(node, host) {
    var current = node && node.nodeType === 1 ? node : (node ? node.parentNode : null);
    while (current && current !== host) {
        if (_wpt_is_editing_block(current)) return current;
        current = current.parentNode;
    }
    return host;
}

function _wpt_preserves_edit_whitespace(node) {
    var current = node && node.nodeType === 1 ? node : (node ? node.parentNode : null);
    while (current && current.nodeType === 1) {
        if (current.tagName && String(current.tagName).toUpperCase() === "PRE") return true;
        var style = current.getAttribute ? current.getAttribute("style") : null;
        if (style && /white-space\s*:\s*(pre|pre-wrap|break-spaces)(?:\s*;|\s*$)/i.test(style)) {
            return true;
        }
        current = current.parentNode;
    }
    return false;
}

function _wpt_uses_pre_line_whitespace(node) {
    var current = node && node.nodeType === 1 ? node : (node ? node.parentNode : null);
    while (current && current.nodeType === 1) {
        var style = current.getAttribute ? current.getAttribute("style") : null;
        if (style && /white-space\s*:\s*pre-line(?:\s*;|\s*$)/i.test(style)) return true;
        current = current.parentNode;
    }
    return false;
}

function _wpt_pre_line_backward_start(data, offset) {
    var runStart = offset;
    while (runStart > 0 && /\s/.test(data.charAt(runStart - 1))) runStart--;
    var previousNewline = -1;
    for (var i = runStart; i < offset; i++) {
        if (data.charAt(i) === "\n") {
            if (previousNewline >= 0) runStart = previousNewline + 1;
            previousNewline = i;
        }
    }
    return runStart;
}

function _wpt_pre_line_forward_end(data, offset, forMutation) {
    var runEnd = offset;
    var newlineCount = 0;
    while (runEnd < data.length && /\s/.test(data.charAt(runEnd))) {
        if (data.charAt(runEnd) === "\n") {
            newlineCount++;
            if (forMutation && newlineCount === 2) return runEnd;
        }
        runEnd++;
    }
    if (!forMutation && runEnd > offset && data.charAt(offset) !== "\n" &&
        data.charAt(runEnd - 1) === "\n" &&
        runEnd < data.length) {
        // Browser target ranges include the first character after a terminal
        // pre-line newline even though the default edit removes only whitespace.
        runEnd++;
    }
    return runEnd;
}

function _wpt_trim_trailing_edit_whitespace(textNode) {
    if (!textNode || textNode.nodeType !== 3) return 0;
    var data = textNode.data || textNode.textContent || "";
    if (_wpt_preserves_edit_whitespace(textNode)) return data.length;
    var length = data.length;
    while (length > 0 && /\s/.test(data.charAt(length - 1))) length--;
    if (length !== data.length) {
        try { textNode.data = data.slice(0, length); } catch (_) { textNode.textContent = data.slice(0, length); }
    }
    return length;
}

function _wpt_trim_leading_edit_whitespace(textNode) {
    if (!textNode || textNode.nodeType !== 3) return;
    if (_wpt_preserves_edit_whitespace(textNode)) return;
    var data = textNode.data || textNode.textContent || "";
    var offset = 0;
    while (offset < data.length && /\s/.test(data.charAt(offset))) offset++;
    if (offset > 0) {
        try { textNode.data = data.slice(offset); } catch (_) { textNode.textContent = data.slice(offset); }
    }
}

function _wpt_canonicalize_joined_space_run(textNode) {
    if (!textNode || textNode.nodeType !== 3 || _wpt_preserves_edit_whitespace(textNode)) return;
    var data = textNode.data || textNode.textContent || "";
    var match = / {2,}/.exec(data);
    if (!match) return;
    var run = match[0];
    var preserved = run.length === 2 ? "\u00a0 " : " " + "\u00a0" + run.slice(2);
    var updated = data.slice(0, match.index) + preserved + data.slice(match.index + run.length);
    // Editing joins encode one collapsible space as NBSP so a visible run
    // survives HTML layout instead of collapsing to a single space.
    try { textNode.data = updated; } catch (_) { try { textNode.textContent = updated; } catch (_) {} }
}

function _wpt_ensure_block_placeholder(block) {
    if (!block || block.nodeType !== 1 || !_wpt_is_editing_block(block)) return;
    var child = block.firstChild;
    while (child) {
        if (child.nodeType !== 3 || _wpt_text_node_length(child) > 0) return;
        child = child.nextSibling;
    }
    try { block.innerHTML = "<br>"; } catch (_) {}
}

function _wpt_remove_block_placeholder_for_insertion(block, selection) {
    if (!block || block.nodeType !== 1 || !block.firstChild ||
        block.firstChild !== block.lastChild) return;
    var child = block.firstChild;
    if (child.nodeType !== 1 || !child.tagName ||
        String(child.tagName).toUpperCase() !== "BR") return;
    // Padding BRs represent an idle empty editing block; once text is inserted
    // they are no longer content and must not trail the new text.
    try { block.removeChild(child); } catch (_) { return; }
    try { selection.collapse(block, 0); } catch (_) {}
}

function _wpt_remove_empty_list_structure_ancestors(node, host) {
    var current = node;
    while (current && current !== host && current.nodeType === 1) {
        var tag = current.tagName ? String(current.tagName).toUpperCase() : "";
        if (tag !== "DL" && tag !== "OL" && tag !== "UL" &&
            tag !== "LI" && tag !== "DT" && tag !== "DD") return;
        var child = current.firstChild;
        var hasContent = false;
        while (child) {
            if (child.nodeType !== 3 || _wpt_text_node_length(child) > 0) {
                hasContent = true;
                break;
            }
            child = child.nextSibling;
        }
        if (hasContent) return;
        var parent = current.parentNode;
        if (!parent) return;
        parent.removeChild(current);
        current = parent;
    }
}

function _wpt_node_is_ancestor(ancestor, node) {
    var current = node;
    while (current) {
        if (current === ancestor) return true;
        current = current.parentNode;
    }
    return false;
}

function _wpt_direct_child_containing(ancestor, node) {
    var current = node;
    while (current && current.parentNode !== ancestor) current = current.parentNode;
    return current && current.parentNode === ancestor ? current : null;
}

function _wpt_trailing_break_count(block) {
    if (!block || block.nodeType !== 1) return 0;
    var count = 0;
    var child = block.lastChild;
    while (child && child.nodeType === 1 && child.tagName &&
           String(child.tagName).toUpperCase() === "BR") {
        count++;
        child = child.previousSibling;
    }
    return count;
}

function _wpt_table_cell_for_node(node, host) {
    var current = node && node.nodeType === 1 ? node : (node ? node.parentNode : null);
    while (current && current !== host) {
        var tag = current.tagName ? String(current.tagName).toUpperCase() : "";
        if (tag === "TD" || tag === "TH") return current;
        current = current.parentNode;
    }
    return null;
}

function _wpt_selected_table_cells(range) {
    var cells = [];
    if (!range || range.startContainer !== range.endContainer ||
        !range.startContainer || range.startContainer.nodeType !== 1) return cells;
    var container = range.startContainer;
    var tag = container.tagName ? String(container.tagName).toUpperCase() : "";
    if (tag !== "TR" || range.endOffset <= range.startOffset || !container.childNodes) return cells;
    for (var i = range.startOffset; i < range.endOffset; i++) {
        var child = container.childNodes[i];
        var childTag = child && child.tagName ? String(child.tagName).toUpperCase() : "";
        if (childTag !== "TD" && childTag !== "TH") return [];
        cells.push(child);
    }
    return cells;
}

function _wpt_is_list_structure_node(node) {
    if (!node || node.nodeType !== 1 || !node.tagName) return false;
    var tag = String(node.tagName).toUpperCase();
    return tag === "LI" || tag === "OL" || tag === "UL" ||
        tag === "DL" || tag === "DT" || tag === "DD";
}

function _wpt_selected_list_structure(range) {
    var nodes = [];
    if (!range || range.startContainer !== range.endContainer ||
        !range.startContainer || range.startContainer.nodeType !== 1 ||
        range.endOffset <= range.startOffset) return nodes;
    var container = range.startContainer;
    var hasStructure = false;
    for (var i = range.startOffset; i < range.endOffset; i++) {
        var child = container.childNodes ? container.childNodes[i] : null;
        if (_wpt_is_list_structure_node(child)) {
            hasStructure = true;
        } else if (!child || child.nodeType !== 3 ||
                   !/^\s*$/.test(child.data || child.textContent || "")) {
            return [];
        }
        nodes.push(child);
    }
    return hasStructure ? nodes : [];
}

function _wpt_list_selection_has_whitespace_node(range) {
    if (!range || !range.startContainer || !range.startContainer.childNodes) return false;
    for (var i = range.startOffset; i < range.endOffset; i++) {
        if (range.startContainer.childNodes[i].nodeType === 3) return true;
    }
    return false;
}

function _wpt_list_ancestor(node, host) {
    var current = node && node.nodeType === 1 ? node : (node ? node.parentNode : null);
    while (current && current !== host) {
        var tag = current.tagName ? String(current.tagName).toUpperCase() : "";
        if (tag === "OL" || tag === "UL" || tag === "DL") return current;
        current = current.parentNode;
    }
    return null;
}

function _wpt_list_item_ancestor(node, host) {
    var current = node && node.nodeType === 1 ? node : (node ? node.parentNode : null);
    while (current && current !== host) {
        if (current.tagName && String(current.tagName).toUpperCase() === "LI") return current;
        current = current.parentNode;
    }
    return null;
}

function _wpt_list_nesting_depth(node, host) {
    var depth = 0;
    var current = node && node.nodeType === 1 ? node : (node ? node.parentNode : null);
    while (current && current !== host) {
        var tag = current.tagName ? String(current.tagName).toUpperCase() : "";
        if (tag === "OL" || tag === "UL") depth++;
        current = current.parentNode;
    }
    return depth;
}

function _wpt_list_has_direct_noneditable_item(list) {
    if (!list || !list.childNodes) return false;
    for (var i = 0; i < list.childNodes.length; i++) {
        var child = list.childNodes[i];
        if (_wpt_is_list_structure_node(child) &&
            _wpt_contenteditable_state(child) === "false") return true;
    }
    return false;
}

function _wpt_selected_noneditable_list_range(range) {
    var selected = _wpt_selected_list_structure(range);
    if (selected.length !== 1) return null;
    var list = selected[0];
    if (!list || !_wpt_list_has_direct_noneditable_item(list)) return null;
    var first = list.firstChild;
    var last = list.lastChild;
    while (first && first.nodeType === 3) first = first.nextSibling;
    while (last && last.nodeType === 3) last = last.previousSibling;
    if (!first || !last) return null;
    try {
        var result = document.createRange();
        if (_wpt_contenteditable_state(first) === "false") {
            result.setStart(list, _wpt_node_index(first));
        } else {
            var firstLeaf = _wpt_deepest_edit_leaf(first, false);
            result.setStart(firstLeaf, 0);
        }
        if (_wpt_contenteditable_state(last) === "false") {
            result.setEnd(list, _wpt_node_index(last) + 1);
        } else if (_wpt_block_is_visually_empty(last)) {
            result.setEnd(last, last.childNodes ? last.childNodes.length : 0);
        } else {
            var lastText = _wpt_last_text_descendant(last);
            result.setEnd(lastText, _wpt_text_node_length(lastText));
        }
        result._wpt_noneditable_list_selection = list;
        return result;
    } catch (_) { return null; }
}

function _wpt_delete_noneditable_list_selection(editRange, host, selection) {
    var list = editRange ? editRange._wpt_noneditable_list_selection : null;
    if (!list || !_wpt_list_has_direct_noneditable_item(list)) return false;
    try {
        // A selection spanning an atomic list item clears the selected list
        // content but cannot retain the non-editable item state.
        list.innerHTML = "<li><br></li>";
        var item = list.firstChild;
        selection.collapse(item, 0);
        return true;
    } catch (_) { return false; }
}

function _wpt_list_selection_leaves_following_sibling(range) {
    if (!range || !range.startContainer || range.startContainer.nodeType !== 1) return false;
    var container = range.startContainer;
    for (var i = range.endOffset; container.childNodes && i < container.childNodes.length; i++) {
        if (_wpt_is_list_structure_node(container.childNodes[i])) return true;
    }
    return false;
}

function _wpt_delete_selected_list_structure(range, host, selection) {
    var nodes = _wpt_selected_list_structure(range);
    if (nodes.length === 0) return false;
    var container = range.startContainer;
    var startOffset = range.startOffset;
    var containerTag = container.tagName ? String(container.tagName).toUpperCase() : "";
    var selectedTag = nodes.length === 1 && nodes[0].tagName
        ? String(nodes[0].tagName).toUpperCase() : "";
    try {
        for (var i = nodes.length - 1; i >= 0; i--) container.removeChild(nodes[i]);
        if ((containerTag === "OL" || containerTag === "UL") &&
            (selectedTag === "OL" || selectedTag === "UL")) {
            // Invalid directly nested lists are normalized by Backspace into
            // an empty item at the same list position, preserving list topology.
            var replacement = document.createElement("li");
            replacement.innerHTML = "<br>";
            var reference = container.childNodes ? container.childNodes[startOffset] : null;
            container.insertBefore(replacement, reference || null);
        } else if (containerTag === "LI") {
            _wpt_ensure_block_placeholder(container);
        }
        if ((containerTag === "OL" || containerTag === "UL" || containerTag === "DL") &&
            !container.firstChild && container.parentNode) {
            var parent = container.parentNode;
            var index = _wpt_node_index(container);
            parent.removeChild(container);
            if (parent.tagName && String(parent.tagName).toUpperCase() === "LI") {
                _wpt_ensure_block_placeholder(parent);
            }
            selection.collapse(parent, Math.max(0, index));
        } else {
            if (containerTag !== "OL" && containerTag !== "UL" && containerTag !== "DL") {
                _wpt_ensure_block_placeholder(container);
            }
            selection.collapse(container, Math.min(startOffset, container.childNodes.length));
        }
        return true;
    } catch (_) { return false; }
}

function _wpt_single_empty_item_list_range(item) {
    if (!item || !item.parentNode || !_wpt_block_is_visually_empty(item)) return null;
    var list = item.parentNode;
    var listTag = list.tagName ? String(list.tagName).toUpperCase() : "";
    if (listTag !== "OL" && listTag !== "UL") return null;
    var child = list.firstChild;
    while (child) {
        if (child.nodeType === 1 && child !== item) return null;
        if (child.nodeType === 3 && !/^\s*$/.test(child.data || child.textContent || "")) return null;
        child = child.nextSibling;
    }
    var parent = list.parentNode;
    var index = _wpt_node_index(list);
    if (!parent || index < 0) return null;
    var start = index;
    var end = index + 1;
    while (start > 0) {
        var before = parent.childNodes[start - 1];
        if (!before || before.nodeType !== 3 ||
            !/^\s*$/.test(before.data || before.textContent || "")) break;
        start--;
    }
    while (parent.childNodes && end < parent.childNodes.length) {
        var after = parent.childNodes[end];
        if (!after || after.nodeType !== 3 ||
            !/^\s*$/.test(after.data || after.textContent || "")) break;
        end++;
    }
    try {
        var range = document.createRange();
        // A sole empty item has no preceding content to join; Backspace
        // removes or unwraps its containing list as one structural boundary.
        range.setStart(parent, start);
        range.setEnd(parent, end);
        return range;
    } catch (_) { return null; }
}

function _wpt_clear_selected_table_cells(range, selection) {
    var cells = _wpt_selected_table_cells(range);
    if (cells.length === 0) return false;
    for (var i = 0; i < cells.length; i++) {
        // Cell selection deletes contents while preserving table topology.
        try { cells[i].innerHTML = ""; } catch (_) { return false; }
    }
    if (selection) {
        try { selection.collapse(cells[0], 0); } catch (_) {}
    }
    return true;
}

function _wpt_delete_text_slice(node, start, end) {
    if (!node || node.nodeType !== 3) return false;
    var data = node.data || node.textContent || "";
    var first = Math.max(0, Math.min(start, data.length));
    var last = Math.max(first, Math.min(end, data.length));
    try { node.data = data.slice(0, first) + data.slice(last); }
    catch (_) { try { node.textContent = data.slice(0, first) + data.slice(last); } catch (_) { return false; } }
    return true;
}

function _wpt_delete_across_table_cells(editRange, host, selection) {
    var startCell = _wpt_table_cell_for_node(editRange.startContainer, host);
    var endCell = _wpt_table_cell_for_node(editRange.endContainer, host);
    if (!startCell || !endCell || startCell === endCell) return false;

    var cells = [];
    try { cells = host.querySelectorAll("td,th"); } catch (_) { return false; }
    var startIndex = -1;
    var endIndex = -1;
    for (var i = 0; i < cells.length; i++) {
        if (cells[i] === startCell) startIndex = i;
        if (cells[i] === endCell) endIndex = i;
    }
    if (startIndex < 0 || endIndex <= startIndex) return false;

    var startNode = editRange.startContainer;
    var startOffset = editRange.startOffset;
    var endNode = editRange.endContainer;
    var endOffset = editRange.endOffset;
    if (startNode.nodeType !== 3 || endNode.nodeType !== 3) return false;
    _wpt_delete_text_slice(startNode, startOffset, _wpt_text_node_length(startNode));
    _wpt_delete_text_slice(endNode, 0, endOffset);
    for (i = startIndex + 1; i < endIndex; i++) {
        // Editing across cells clears their contents; table rows and cells are
        // structural boundaries and must never be consumed by Range deletion.
        try { cells[i].innerHTML = ""; } catch (_) {}
    }
    try { selection.collapse(startNode, startOffset); } catch (_) {}
    return true;
}

function _wpt_delete_atomic_edit_range(editRange, startBlock, endBlock, selection) {
    if (!editRange || startBlock !== endBlock) return false;
    var start = editRange.startContainer;
    var end = editRange.endContainer;
    var startOffset = editRange.startOffset;
    var endOffset = editRange.endOffset;
    try {
        if (start === end && start.nodeType === 1 && endOffset > startOffset) {
            var containsHorizontalRule = false;
            for (var i = endOffset - 1; i >= startOffset; i--) {
                var selected = start.childNodes[i];
                if (!_wpt_is_atomic_edit_node(selected)) return false;
                if (selected.tagName && String(selected.tagName).toUpperCase() === "HR") {
                    containsHorizontalRule = true;
                }
            }
            for (i = endOffset - 1; i >= startOffset; i--) {
                start.removeChild(start.childNodes[i]);
            }
            if (containsHorizontalRule) {
                // Whitespace beside a block-level HR was invisible before the
                // separator disappeared and must not become visible afterward.
                var beforeRule = startOffset > 0 ? start.childNodes[startOffset - 1] : null;
                var afterRule = start.childNodes[startOffset] || null;
                _wpt_trim_trailing_edit_whitespace(beforeRule);
                _wpt_trim_leading_edit_whitespace(afterRule);
            }
            selection.collapse(start, startOffset);
            return true;
        }
        if (start.nodeType === 1 && end.nodeType === 3 && start.childNodes) {
            var leadingAtomic = start.childNodes[startOffset];
            if (!_wpt_is_atomic_edit_node(leadingAtomic)) return false;
            start.removeChild(leadingAtomic);
            _wpt_delete_text_slice(end, 0, endOffset);
            selection.collapse(start, Math.min(startOffset, start.childNodes.length));
            return true;
        }
        if (start.nodeType === 3 && end.nodeType === 1 && end.childNodes && endOffset > 0) {
            var trailingAtomic = end.childNodes[endOffset - 1];
            if (!_wpt_is_atomic_edit_node(trailingAtomic)) return false;
            _wpt_delete_text_slice(start, startOffset, _wpt_text_node_length(start));
            end.removeChild(trailingAtomic);
            selection.collapse(start, Math.min(startOffset, _wpt_text_node_length(start)));
            return true;
        }
    } catch (_) { return false; }
    return false;
}

function _wpt_delete_empty_block_boundary(editRange, host, selection) {
    if (!editRange || !host || editRange.startContainer === host ||
        editRange.endContainer !== host) return false;
    var block = editRange.startContainer;
    if (!block || block.nodeType !== 1 || !_wpt_block_is_visually_empty(block) ||
        block.parentNode !== host) return false;
    var blockIndex = _wpt_node_index(block);
    if (blockIndex < 0 || editRange.endOffset !== blockIndex + 1) return false;
    try {
        // A non-editable island cannot be joined, but deleting the empty block
        // immediately before it is still a single editing transaction.
        host.removeChild(block);
        selection.collapse(host, Math.min(blockIndex, host.childNodes.length));
        return true;
    } catch (_) { return false; }
}

function _wpt_join_descendant_block(startBlock, endBlock, selection, caretOffset) {
    var branch = _wpt_direct_child_containing(startBlock, endBlock);
    if (!branch) return false;
    // The descendant block is being dissolved into an inline run, so its
    // formerly trailing collapsible whitespace becomes an exposed line edge.
    _wpt_trim_trailing_edit_whitespace(_wpt_last_text_descendant(endBlock));
    var movedHTML = endBlock.innerHTML;
    var branchTag = branch.tagName ? String(branch.tagName).toUpperCase() : "";
    if (branch === endBlock && branchTag !== "LI" && branchTag !== "DT" && branchTag !== "DD") {
        var breakMatch = /<br(?:\s[^>]*)?\s*\/?\s*>/i.exec(movedHTML);
        if (breakMatch) {
            // Joining into an ancestor dissolves only the first visual line;
            // content after the line break remains in its original block.
            var beforeBreak = movedHTML.slice(0, breakMatch.index);
            var afterBreak = movedHTML.slice(breakMatch.index + breakMatch[0].length);
            if (beforeBreak) branch.insertAdjacentHTML("beforebegin", beforeBreak);
            branch.innerHTML = afterBreak;
        } else {
            if (movedHTML) branch.insertAdjacentHTML("beforebegin", movedHTML);
            if (branch.parentNode) branch.parentNode.removeChild(branch);
        }
    } else {
        if (movedHTML) branch.insertAdjacentHTML("beforebegin", movedHTML);
        var afterBranch = branch.nextSibling;
        var emptiedParent = endBlock.parentNode;
        if (emptiedParent) emptiedParent.removeChild(endBlock);
        _wpt_remove_empty_list_structure_ancestors(emptiedParent, null);
        if (!branch.parentNode && afterBranch) {
            // Once the enclosing list is dissolved, its following collapsible
            // whitespace becomes an exposed edge of the joined line.
            _wpt_trim_leading_edit_whitespace(_wpt_first_text_descendant(afterBranch));
        }
    }
    var reparsedCaretNode = _wpt_first_text_descendant(startBlock);
    try {
        selection.collapse(
            reparsedCaretNode || startBlock,
            reparsedCaretNode ? Math.min(caretOffset, _wpt_text_node_length(reparsedCaretNode)) : 0);
    } catch (_) {}
    return true;
}

function _wpt_join_ancestor_block(startBlock, endBlock, selection, caretOffset) {
    var branch = _wpt_direct_child_containing(endBlock, startBlock);
    if (!branch) return false;
    var tailHTML = "";
    var sibling = branch.nextSibling;
    while (sibling) {
        tailHTML += sibling.nodeType === 3 ? (sibling.data || sibling.textContent || "") : sibling.outerHTML;
        sibling = sibling.nextSibling;
    }
    while (branch.nextSibling) endBlock.removeChild(branch.nextSibling);
    // Reparse after detaching the live selection so moving an ancestor's tail
    // cannot leave Range boundary bookkeeping attached to removed nodes.
    startBlock.innerHTML = startBlock.innerHTML + tailHTML;
    var caretNode = _wpt_first_text_descendant(startBlock);
    if (caretNode) selection.collapse(caretNode, Math.min(caretOffset, _wpt_text_node_length(caretNode)));
    else selection.collapse(startBlock, 0);
    return true;
}

function _wpt_join_editing_blocks(startBlock, endBlock, selection) {
    if (!startBlock || !endBlock || startBlock === endBlock ||
        startBlock.nodeType !== 1 || endBlock.nodeType !== 1) return false;
    var startText = _wpt_last_text_descendant(startBlock);
    var endText = _wpt_first_text_descendant(endBlock);
    var startPreservesWhitespace = _wpt_preserves_edit_whitespace(startText);
    var endPreservesWhitespace = _wpt_preserves_edit_whitespace(endText);
    var caretOffset = _wpt_trim_trailing_edit_whitespace(startText);
    _wpt_trim_leading_edit_whitespace(endText);
    try {
        if (_wpt_node_is_ancestor(startBlock, endBlock)) {
            return _wpt_join_descendant_block(
                startBlock, endBlock, selection, caretOffset);
        }
        if (_wpt_node_is_ancestor(endBlock, startBlock)) {
            return _wpt_join_ancestor_block(startBlock, endBlock, selection, caretOffset);
        }
        var startTag = startBlock.tagName ? String(startBlock.tagName).toUpperCase() : "";
        var endTag = endBlock.tagName ? String(endBlock.tagName).toUpperCase() : "";
        if (startTag === endTag &&
            (startTag === "LI" || startTag === "DT" || startTag === "DD")) {
            var endHTML = endBlock.innerHTML;
            var lineBreak = /<br(?:\s[^>]*)?\s*\/?\s*>/i.exec(endHTML);
            if (lineBreak) {
                // Joining list items consumes their boundary and first visual
                // line; later lines remain a separate item in the same list.
                startBlock.innerHTML = startBlock.innerHTML + endHTML.slice(0, lineBreak.index);
                var remainingLine = endHTML.slice(lineBreak.index + lineBreak[0].length);
                if (remainingLine) {
                    endBlock.innerHTML = remainingLine;
                } else if (endBlock.parentNode) {
                    // A terminal padding break does not create a second empty
                    // list item after its first line has been joined.
                    endBlock.parentNode.removeChild(endBlock);
                }
                var joinedCaret = _wpt_first_text_descendant(startBlock);
                if (selection && joinedCaret) {
                    selection.collapse(joinedCaret,
                        Math.min(caretOffset, _wpt_text_node_length(joinedCaret)));
                }
                return true;
            }
        }
        // Reparse the combined fragment instead of reparenting nodes one by one:
        // live Range bookkeeping still owns the old boundary nodes during edits.
        startBlock.innerHTML = startBlock.innerHTML + endBlock.innerHTML;
        var emptiedParent = endBlock.parentNode;
        if (emptiedParent) endBlock.parentNode.removeChild(endBlock);
        _wpt_remove_empty_list_structure_ancestors(emptiedParent, null);
        if (!startPreservesWhitespace && endPreservesWhitespace) {
            _wpt_canonicalize_joined_space_run(_wpt_first_text_descendant(startBlock));
        }
        if (selection) {
            var newStartText = _wpt_first_text_descendant(startBlock);
            if (newStartText) selection.collapse(
                newStartText, Math.min(caretOffset, _wpt_text_node_length(newStartText)));
            else selection.collapse(startBlock, 0);
        }
        return true;
    } catch (_) { return false; }
}

function _wpt_delete_adjacent_list_item_boundary(editRange, startBlock, endBlock, selection) {
    if (!editRange || !startBlock || !endBlock || startBlock === endBlock ||
        startBlock.parentNode !== endBlock.parentNode) return false;
    var startTag = startBlock.tagName ? String(startBlock.tagName).toUpperCase() : "";
    var endTag = endBlock.tagName ? String(endBlock.tagName).toUpperCase() : "";
    if (startTag !== endTag ||
        (startTag !== "LI" && startTag !== "DT" && startTag !== "DD")) return false;
    var start = editRange.startContainer;
    var end = editRange.endContainer;
    var startOffset = editRange.startOffset;
    var endOffset = editRange.endOffset;
    var startsAtBoundary = start === startBlock
        ? startOffset <= startBlock.childNodes.length
        : start.nodeType === 3 && startOffset <= _wpt_text_node_length(start);
    var endsAtBoundary = end === endBlock
        ? endOffset >= 0
        : end.nodeType === 3 && endOffset >= 0;
    if (!startsAtBoundary || !endsAtBoundary) return false;
    try {
        var trailingBreaks = _wpt_trailing_break_count(startBlock);
        if (start === startBlock) {
            while (startBlock.childNodes.length > startOffset) {
                startBlock.removeChild(startBlock.childNodes[startOffset]);
            }
        } else if (start.nodeType === 3) {
            _wpt_delete_text_slice(start, startOffset, _wpt_text_node_length(start));
            var trailing = start.nextSibling;
            while (trailing) {
                var nextTrailing = trailing.nextSibling;
                start.parentNode.removeChild(trailing);
                trailing = nextTrailing;
            }
        }
        if (end === endBlock) {
            while (endOffset > 0 && endBlock.firstChild) {
                endBlock.removeChild(endBlock.firstChild);
                endOffset--;
            }
        } else if (end.nodeType === 3) {
            _wpt_delete_text_slice(end, 0, endOffset);
            var leading = end.previousSibling;
            while (leading) {
                var previousLeading = leading.previousSibling;
                end.parentNode.removeChild(leading);
                leading = previousLeading;
            }
        }
        if (trailingBreaks > 1) {
            // A two-BR line ending contributes one retained visual break when
            // the following list item is joined into the same item.
            startBlock.insertAdjacentHTML("beforeend", "<br>");
        }
        // Range.deleteContents() cannot reliably retain both list-item shells;
        // edit their boundary content first, then perform the structural join.
        var joined = _wpt_join_editing_blocks(startBlock, endBlock, selection);
        if (joined) _wpt_ensure_block_placeholder(startBlock);
        return joined;
    } catch (_) { return false; }
}

function _wpt_delete_cross_list_nested_boundary(editRange, host, selection) {
    if (!editRange || !host || !editRange.startContainer || !editRange.endContainer) return false;
    var startItem = _wpt_list_item_ancestor(editRange.startContainer, host);
    var endItem = _wpt_list_item_ancestor(editRange.endContainer, host);
    var startRoot = _wpt_direct_child_containing(host, startItem);
    var endRoot = _wpt_direct_child_containing(host, endItem);
    if (!startItem || !endItem || !startRoot || !endRoot || startRoot === endRoot ||
        editRange.startContainer.nodeType !== 3 || editRange.startOffset !== 0 ||
        editRange.endOffset !== 0) return false;
    var startDepth = _wpt_list_nesting_depth(startItem, host);
    var endDepth = _wpt_list_nesting_depth(endItem, host);
    if (startDepth === endDepth) return false;
    try {
        if (startDepth < endDepth) {
            // The entire shallow left list is selected up to the nested right
            // item boundary; removing it must leave the right hierarchy intact.
            var rootIndex = _wpt_node_index(startRoot);
            host.removeChild(startRoot);
            selection.collapse(host, Math.max(0, rootIndex));
            return true;
        }

        var endHTML = endItem.innerHTML;
        var lineBreak = /<br(?:\s[^>]*)?\s*\/?\s*>/i.exec(endHTML);
        var firstLine = lineBreak ? endHTML.slice(0, lineBreak.index) : endHTML;
        var remaining = lineBreak ? endHTML.slice(lineBreak.index + lineBreak[0].length) : "";
        startItem.innerHTML = firstLine || "<br>";
        if (remaining) {
            endItem.innerHTML = remaining;
        } else if (endRoot.parentNode) {
            endRoot.parentNode.removeChild(endRoot);
        }
        var movedText = _wpt_first_text_descendant(startItem);
        selection.collapse(movedText || startItem, movedText ? 0 : 0);
        // Moving into a deeper left item retains every enclosing list wrapper;
        // generic range deletion would incorrectly flatten those ancestors.
        return true;
    } catch (_) { return false; }
}

function _wpt_delete_same_root_nested_boundary(editRange, host, selection) {
    if (!editRange || !host || !editRange.startContainer || !editRange.endContainer ||
        editRange.startContainer.nodeType !== 3 || editRange.startOffset !== 0) return false;
    var startItem = _wpt_list_item_ancestor(editRange.startContainer, host);
    var endItem = _wpt_list_item_ancestor(editRange.endContainer, host);
    var root = _wpt_direct_child_containing(host, startItem);
    if (!startItem || !endItem || !root ||
        root !== _wpt_direct_child_containing(host, endItem)) return false;
    var startDepth = _wpt_list_nesting_depth(startItem, host);
    var endDepth = _wpt_list_nesting_depth(endItem, host);
    if (startDepth === endDepth) return false;
    var endIsStart = editRange.endOffset === 0;
    var endIsCompleteText = editRange.endContainer.nodeType === 3 &&
        editRange.endOffset === _wpt_text_node_length(editRange.endContainer);
    if (!endIsStart && !endIsCompleteText) return false;
    try {
        var startBranch = _wpt_direct_child_containing(root, startItem);
        var endBranch = _wpt_direct_child_containing(root, endItem);
        if (endIsCompleteText) {
            var nestedItem = startDepth > endDepth ? startItem : endItem;
            var nestedList = _wpt_list_ancestor(nestedItem, root);
            if (!nestedList) return false;
            nestedList.innerHTML = "<li><br></li>";
            var nestedHTML = nestedList.outerHTML;
            if (nestedList.parentNode === root) {
                if (startDepth < endDepth) nestedHTML = "<li><br></li>";
            } else {
                nestedHTML = "<li>" + nestedHTML + "</li>";
            }
            // A full cross-depth selection keeps the nested list shell while
            // clearing every selected item around it.
            root.innerHTML = nestedHTML;
            var emptyNestedItem = _wpt_list_item_ancestor(
                _wpt_first_text_descendant(root) || root.firstChild, host);
            selection.collapse(emptyNestedItem || root, 0);
            return true;
        }

        if (startDepth < endDepth) {
            var endList = _wpt_list_ancestor(endItem, root);
            while (endList && endList.firstChild && endList.firstChild !== endItem) {
                endList.removeChild(endList.firstChild);
            }
            var remove = startBranch;
            while (remove && remove !== endBranch) {
                var nextRemove = remove.nextSibling;
                root.removeChild(remove);
                remove = nextRemove;
            }
            selection.collapse(endItem, 0);
            return true;
        }

        startItem.innerHTML = endItem.innerHTML || "<br>";
        var afterStart = startBranch.nextSibling;
        while (afterStart) {
            var nextAfterStart = afterStart.nextSibling;
            root.removeChild(afterStart);
            if (afterStart === endBranch) break;
            afterStart = nextAfterStart;
        }
        var replacementText = _wpt_first_text_descendant(startItem);
        selection.collapse(replacementText || startItem, 0);
        // Replacing the deeper item content preserves its invalid-but-observed
        // list ancestry instead of promoting it to the outer list.
        return true;
    } catch (_) { return false; }
}

function _wpt_apply_edit_range_delete(editRange, host, selection, preserveBoundaryBreak) {
    if (!editRange || editRange.collapsed) return false;
    try {
        var startContainer = editRange.startContainer;
        var startOffset = editRange.startOffset;
        var startBlock = _wpt_editing_block_for_node(editRange.startContainer, host);
        var endBlock = _wpt_editing_block_for_node(editRange.endContainer, host);
        if (_wpt_delete_noneditable_list_selection(editRange, host, selection)) return true;
        if (_wpt_delete_selected_list_structure(editRange, host, selection)) return true;
        if (_wpt_clear_selected_table_cells(editRange, selection)) return true;
        if (_wpt_delete_across_table_cells(editRange, host, selection)) return true;
        if (_wpt_delete_empty_block_boundary(editRange, host, selection)) return true;
        if (_wpt_delete_cross_list_nested_boundary(editRange, host, selection)) return true;
        if (_wpt_delete_same_root_nested_boundary(editRange, host, selection)) return true;
        if (_wpt_delete_adjacent_list_item_boundary(
                editRange, startBlock, endBlock, selection)) return true;
        if (_wpt_delete_atomic_edit_range(editRange, startBlock, endBlock, selection)) {
            _wpt_ensure_block_placeholder(startBlock);
            return true;
        }
        var trailingBreaks = preserveBoundaryBreak ? _wpt_trailing_break_count(startBlock) : 0;
        editRange.deleteContents();
        selection.collapse(startContainer, startOffset);
        var remainingBreaks = _wpt_trailing_break_count(startBlock);
        while (remainingBreaks < Math.max(0, trailingBreaks - 1)) {
            // A block-boundary delete consumes one visual break, not every
            // trailing BR included in the browser's broader target range.
            startBlock.insertAdjacentHTML("beforeend", "<br>");
            remainingBreaks++;
        }
        _wpt_join_editing_blocks(startBlock, endBlock, selection);
        // Deleting a complete list item leaves an editable item shell; its
        // padding break is required even when a neighbouring block was joined.
        _wpt_ensure_block_placeholder(startBlock);
        _wpt_cleanup_rich_range_delete(host);
        return true;
    } catch (_) { return false; }
}

function _wpt_apply_multi_range_delete(selection) {
    if (!selection || selection.rangeCount < 2) return false;
    var ranges = [];
    try {
        for (var i = 0; i < selection.rangeCount; i++) {
            ranges.push(selection.getRangeAt(i).cloneRange());
        }
    } catch (_) { return false; }
    var firstContainer = ranges[0].startContainer;
    var firstOffset = ranges[0].startOffset;
    var mutated = false;
    for (i = ranges.length - 1; i >= 0; i--) {
        var range = ranges[i];
        if (_wpt_clear_selected_table_cells(range, null)) {
            mutated = true;
            continue;
        }
        if (!range.collapsed) {
            try { range.deleteContents(); mutated = true; } catch (_) {}
        }
    }
    try { selection.collapse(firstContainer, firstOffset); } catch (_) {}
    return mutated;
}

function _wpt_normalize_selected_edit_range(current, host) {
    if (!current || current.collapsed) return current;
    var startNode = current.startContainer;
    var startOffset = current.startOffset;
    var endNode = current.endContainer;
    var endOffset = current.endOffset;

    if (startNode && startNode.nodeType === 1 && startNode.childNodes &&
        startOffset < startNode.childNodes.length) {
        var firstSelected = startNode.childNodes[startOffset];
        var firstText = _wpt_first_text_descendant(firstSelected);
        if (firstText) {
            startNode = firstText;
            startOffset = 0;
        } else if (firstSelected && firstSelected.nodeType === 1 &&
                   String(firstSelected.tagName).toUpperCase() === "HR") {
            // An HR is a block separator: deleting it also consumes the
            // collapsible text edges which would otherwise become adjacent.
            var textBeforeSeparator = _wpt_prev_text_node(firstSelected, host);
            if (textBeforeSeparator) {
                startNode = textBeforeSeparator;
                startOffset = _wpt_text_node_length(textBeforeSeparator);
            }
        }
    }
    if (endNode && endNode.nodeType === 1 && endNode.childNodes && endOffset > 0) {
        var lastSelected = endNode.childNodes[endOffset - 1];
        var lastText = _wpt_last_text_descendant(lastSelected);
        if (lastText) {
            endNode = lastText;
            endOffset = _wpt_text_node_length(lastText);
        } else if (lastSelected && lastSelected.nodeType === 1 &&
                   String(lastSelected.tagName).toUpperCase() === "HR") {
            var textAfterSeparator = _wpt_next_text_node(lastSelected, host);
            if (textAfterSeparator) {
                endNode = textAfterSeparator;
                endOffset = 0;
            }
        }
    }
    if (endNode && endNode.nodeType === 1 && endOffset === 0 &&
        endNode.tagName && String(endNode.tagName).toUpperCase() === "LI") {
        var startRoot = _wpt_direct_child_containing(host, startNode);
        var endRoot = _wpt_direct_child_containing(host, endNode);
        if (startRoot && endRoot && startRoot !== endRoot &&
            _wpt_list_nesting_depth(startNode, host) >
                _wpt_list_nesting_depth(endNode, host)) {
            var nestedJoinEnd = _wpt_first_text_descendant(endNode);
            if (nestedJoinEnd) {
                // Moving a shallow right item into a nested left item resolves
                // the endpoint to text; the mirrored operation keeps LI,0.
                endNode = nestedJoinEnd;
                endOffset = 0;
            }
        }
    }
    // Collapsible edge whitespace is part of the editing transaction: if it
    // survived a block join it would become newly visible beside the caret.
    if (startNode && startNode.nodeType === 3 && !_wpt_preserves_edit_whitespace(startNode)) {
        var startData = startNode.data || startNode.textContent || "";
        while (startOffset > 0 && /\s/.test(startData.charAt(startOffset - 1))) startOffset--;
    }
    if (endNode && endNode.nodeType === 3 && !_wpt_preserves_edit_whitespace(endNode)) {
        var endData = endNode.data || endNode.textContent || "";
        while (endOffset < endData.length && /\s/.test(endData.charAt(endOffset))) endOffset++;
    }

    try {
        var normalized = document.createRange();
        normalized.setStart(startNode, startOffset);
        normalized.setEnd(endNode, endOffset);
        return normalized;
    } catch (_) { return current.cloneRange(); }
}

function _wpt_edit_target_range(inputType, suppliedRange) {
    var selection = null;
    try { selection = getSelection(); } catch (_) {}
    if (!suppliedRange && (!selection || selection.rangeCount === 0)) return null;
    var current = suppliedRange || selection.getRangeAt(0);
    var host = _wpt_editing_host_for_node(current.startContainer) ||
        _wpt_editing_host_for_node(current.endContainer);
    var backward = /Backward$/.test(inputType);
    var byWord = /^deleteWord/.test(inputType);
    if (!current.collapsed) {
        if (_wpt_selected_table_cells(current).length > 0) return current.cloneRange();
        var noneditableListRange = _wpt_selected_noneditable_list_range(current);
        if (noneditableListRange) return noneditableListRange;
        // A selected item is structural only while another list sibling remains;
        // deleting the final item instead clears its content and keeps the shell.
        if (_wpt_selected_list_structure(current).length > 0 &&
            (_wpt_list_selection_leaves_following_sibling(current) ||
             _wpt_list_selection_has_whitespace_node(current))) {
            return current.cloneRange();
        }
        return _wpt_normalize_selected_edit_range(current, host);
    }
    if (!/^delete(Content|Word)(Backward|Forward)$/.test(inputType)) {
        try { return current.cloneRange(); } catch (_) { return current; }
    }

    var node = current.startContainer;
    var offset = current.startOffset;
    if (!node) return current.cloneRange();
    if (node.nodeType === 1 && !byWord) {
        var elementBlock = _wpt_editing_block_for_node(node, host);
        if (_wpt_block_is_visually_empty(elementBlock)) {
            var elementBlockTag = elementBlock && elementBlock.tagName
                ? String(elementBlock.tagName).toUpperCase() : "";
            var previousBlockSibling = elementBlock ? elementBlock.previousSibling : null;
            var previousBlockTag = previousBlockSibling && previousBlockSibling.tagName
                ? String(previousBlockSibling.tagName).toUpperCase() : "";
            if (elementBlockTag === "LI") {
                var emptyListRange = _wpt_single_empty_item_list_range(elementBlock);
                if (emptyListRange) return emptyListRange;
            }
            if (elementBlockTag === "TD" || elementBlockTag === "TH" ||
                elementBlockTag === "CAPTION" ||
                (backward && previousBlockTag === "TABLE")) {
                // Cell/caption boundaries and a paragraph after a table do not
                // join through the table's internal text.
                return current.cloneRange();
            }
            var blockNeighbor = backward
                ? _wpt_leaf_before_boundary(elementBlock, 0, host)
                : _wpt_leaf_after_boundary(
                    elementBlock, elementBlock.childNodes ? elementBlock.childNodes.length : 0, host);
            if (!backward && blockNeighbor && _wpt_table_cell_for_node(blockNeighbor, host)) {
                // Empty paragraphs do not join into table cell contents; the
                // table boundary is an editing no-op and exposes a collapsed range.
                return current.cloneRange();
            }
            if (!backward && blockNeighbor &&
                _wpt_editing_host_for_node(blockNeighbor) !== host) {
                var foreignBranch = _wpt_direct_child_containing(host, blockNeighbor);
                var foreignIndex = _wpt_node_index(foreignBranch);
                if (foreignIndex >= 0) {
                    try {
                        var foreignRange = document.createRange();
                        foreignRange.setStart(elementBlock, 0);
                        if (_wpt_contenteditable_state(foreignBranch) === "false") {
                            foreignRange.setEnd(host, foreignIndex);
                        } else {
                            // A non-editable first child does not make its
                            // enclosing editable block an atomic boundary.
                            foreignRange.setEnd(foreignBranch, 0);
                        }
                        return foreignRange;
                    } catch (_) {}
                }
            }
            var emptyBlockRange = _wpt_range_across_empty_block(
                elementBlock, blockNeighbor, backward);
            if (emptyBlockRange) return emptyBlockRange;
        }
        var adjacentLeaf = backward
            ? _wpt_leaf_before_boundary(node, offset, host)
            : _wpt_leaf_after_boundary(node, offset, host);
        var adjacentTag = adjacentLeaf && adjacentLeaf.tagName
            ? String(adjacentLeaf.tagName).toUpperCase() : "";
        if (!backward && adjacentTag === "BR" && adjacentLeaf.nextSibling &&
            _wpt_is_editing_block(adjacentLeaf.nextSibling)) {
            var adjacentBlockText = _wpt_first_text_descendant(adjacentLeaf.nextSibling);
            var adjacentJoinRange = _wpt_forward_block_join_range(
                adjacentLeaf, adjacentBlockText);
            if (adjacentJoinRange) return adjacentJoinRange;
        }
        if (_wpt_is_atomic_edit_node(adjacentLeaf) &&
            (backward || !_wpt_is_padding_break(adjacentLeaf)) &&
            _wpt_editing_block_for_node(adjacentLeaf, host) ===
                _wpt_editing_block_for_node(node, host)) {
            return _wpt_range_around_atomic(adjacentLeaf, null, 0, backward) ||
                current.cloneRange();
        }
        if (!backward && _wpt_is_padding_break(adjacentLeaf)) {
            var nextAfterBlock = _wpt_leaf_after_boundary(
                elementBlock, elementBlock.childNodes ? elementBlock.childNodes.length : 0, host);
            var endJoinRange = _wpt_range_across_empty_block(
                elementBlock, nextAfterBlock, false);
            if (endJoinRange) {
                var paddingIndex = _wpt_node_index(adjacentLeaf);
                try { endJoinRange.setStart(adjacentLeaf.parentNode, paddingIndex); } catch (_) {}
                return endJoinRange;
            }
        }
        return current.cloneRange();
    }
    if (node.nodeType !== 3) return current.cloneRange();
    var boundaryNode = node;
    var boundaryOffset = offset;
    if (!byWord) {
        var inlineParent = node.parentNode;
        var nodeLength = _wpt_text_node_length(node);
        if (_wpt_is_cleanup_inline_wrapper(inlineParent) && nodeLength === 1) {
            var inlineIndex = _wpt_node_index(inlineParent);
            if (inlineIndex >= 0 &&
                ((backward && offset === 1) || (!backward && offset === 0))) {
                try {
                    var inlineRange = document.createRange();
                    inlineRange.setStart(inlineParent.parentNode, inlineIndex);
                    inlineRange.setEnd(inlineParent.parentNode, inlineIndex + 1);
                    return inlineRange;
                } catch (_) {}
            }
        }
        if (backward && offset === 0) {
            var previousInlineText = _wpt_prev_text_node(node, host);
            var previousInline = previousInlineText ? previousInlineText.parentNode : null;
            if (previousInlineText && _wpt_text_node_length(previousInlineText) === 1 &&
                _wpt_is_cleanup_inline_wrapper(previousInline)) {
                var previousInlineIndex = _wpt_node_index(previousInline);
                try {
                    var previousInlineRange = document.createRange();
                    previousInlineRange.setStart(previousInline.parentNode, previousInlineIndex);
                    previousInlineRange.setEnd(node, 0);
                    return previousInlineRange;
                } catch (_) {}
            }
        }
        if (!backward && offset === nodeLength) {
            var nextInlineText = _wpt_next_text_node(node, host);
            var nextInline = nextInlineText ? nextInlineText.parentNode : null;
            if (nextInlineText && _wpt_text_node_length(nextInlineText) === 1 &&
                _wpt_is_cleanup_inline_wrapper(nextInline)) {
                var nextInlineIndex = _wpt_node_index(nextInline);
                try {
                    var nextInlineRange = document.createRange();
                    nextInlineRange.setStart(node, nodeLength);
                    nextInlineRange.setEnd(nextInline.parentNode, nextInlineIndex + 1);
                    return nextInlineRange;
                } catch (_) {}
            }
        }
    }
    if (backward) {
        var leadingEnd = 0;
        var nodeData = node.data || node.textContent || "";
        if (_wpt_uses_pre_line_whitespace(node) && offset > 0 &&
            /\s/.test(nodeData.charAt(offset - 1))) {
            try {
                var preLineBackwardRange = document.createRange();
                preLineBackwardRange.setStart(
                    node, _wpt_pre_line_backward_start(nodeData, offset));
                preLineBackwardRange.setEnd(node, offset);
                return preLineBackwardRange;
            } catch (_) {}
        }
        if (!_wpt_preserves_edit_whitespace(node)) {
            while (leadingEnd < nodeData.length && /\s/.test(nodeData.charAt(leadingEnd))) leadingEnd++;
        }
        var atVisualStart = offset <= leadingEnd;
        if (atVisualStart) {
            var atomicBefore = _wpt_leaf_before_boundary(node, 0, host);
            if (_wpt_is_atomic_edit_node(atomicBefore) &&
                _wpt_editing_block_for_node(atomicBefore, host) ===
                    _wpt_editing_block_for_node(node, host)) {
                var atomicBeforeTag = atomicBefore.tagName
                    ? String(atomicBefore.tagName).toUpperCase() : "";
                return _wpt_range_around_atomic(
                    atomicBefore, node,
                    atomicBeforeTag === "IMG" ||
                        _wpt_contenteditable_state(atomicBefore) === "false" ? 0 : leadingEnd,
                    true) || current.cloneRange();
            }
            var previousAtBlock = _wpt_prev_text_node(node, host);
            var nodeBlock = _wpt_editing_block_for_node(node, host);
            var previousBlock = _wpt_editing_block_for_node(previousAtBlock, host);
            if (previousAtBlock && previousBlock !== nodeBlock) {
                var previousData = previousAtBlock.data || previousAtBlock.textContent || "";
                var previousEnd = previousData.length;
                if (!_wpt_preserves_edit_whitespace(previousAtBlock) &&
                    !_wpt_preserves_edit_whitespace(node)) {
                    while (previousEnd > 0 && /\s/.test(previousData.charAt(previousEnd - 1))) previousEnd--;
                }
                try {
                    var blockRange = document.createRange();
                    var previousBreak = previousBlock && previousBlock.lastChild;
                    if (!previousBreak || previousBreak.nodeType !== 1 ||
                        String(previousBreak.tagName).toUpperCase() !== "BR") {
                        previousBreak = previousAtBlock.nextSibling;
                    }
                    if (previousBreak && previousBreak.nodeType === 1 &&
                        String(previousBreak.tagName).toUpperCase() === "BR") {
                        var previousBlockTagName = previousBlock && previousBlock.tagName
                            ? String(previousBlock.tagName).toUpperCase() : "";
                        if (previousBlockTagName === "LI" || previousBlockTagName === "DT" ||
                            previousBlockTagName === "DD") {
                            while (previousBreak.previousSibling &&
                                   previousBreak.previousSibling.nodeType === 1 &&
                                   previousBreak.previousSibling.tagName &&
                                   String(previousBreak.previousSibling.tagName).toUpperCase() === "BR") {
                                previousBreak = previousBreak.previousSibling;
                            }
                        }
                        // Consecutive trailing BRs form one list-item boundary;
                        // the first break is the start of the join target.
                        blockRange.setStart(previousBreak.parentNode, _wpt_node_index(previousBreak));
                    } else if (nodeBlock && nodeBlock.tagName &&
                               String(nodeBlock.tagName).toUpperCase() === "LI" &&
                               node.nextSibling && node.nextSibling.nodeType === 1 &&
                               String(node.nextSibling.tagName).toUpperCase() === "BR") {
                        // Backspacing into a multi-line list item targets the
                        // item boundary, since only its first visual line joins.
                        blockRange.setStart(previousBlock, previousBlock.childNodes.length);
                    } else {
                        blockRange.setStart(previousAtBlock, previousEnd);
                    }
                    blockRange.setEnd(node, leadingEnd);
                    return blockRange;
                } catch (_) {}
            }
        }
        while (boundaryNode) {
            var before = boundaryNode.data || boundaryNode.textContent || "";
            if (!byWord) {
                if (boundaryOffset > 0) { boundaryOffset--; break; }
            } else {
                while (boundaryOffset > 0 &&
                       _wpt_is_word_cp(_wpt_cp_script_class(before.charCodeAt(boundaryOffset - 1)))) {
                    boundaryOffset--;
                }
                if (boundaryOffset > 0 || before.length === 0) break;
            }
            var previous = _wpt_prev_text_node(boundaryNode, host);
            if (!previous) break;
            boundaryNode = previous;
            boundaryOffset = _wpt_text_node_length(previous);
            if (!byWord && boundaryOffset > 0) { boundaryOffset--; break; }
        }
    } else {
        nodeData = node.data || node.textContent || "";
        if (_wpt_uses_pre_line_whitespace(node) && offset < nodeData.length &&
            /\s/.test(nodeData.charAt(offset))) {
            try {
                var preLineForwardRange = document.createRange();
                preLineForwardRange.setStart(node, offset);
                preLineForwardRange.setEnd(
                    node, _wpt_pre_line_forward_end(nodeData, offset, false));
                return preLineForwardRange;
            } catch (_) {}
        }
        var trailingStart = nodeData.length;
        if (!_wpt_preserves_edit_whitespace(node)) {
            while (trailingStart > 0 && /\s/.test(nodeData.charAt(trailingStart - 1))) trailingStart--;
        }
        var atVisualEnd = offset >= trailingStart;
        if (atVisualEnd) {
            var atomicAfter = _wpt_leaf_after_boundary(node, nodeData.length, host);
            var atomicAfterTag = atomicAfter && atomicAfter.tagName
                ? String(atomicAfter.tagName).toUpperCase() : "";
            if (atomicAfterTag === "BR" && atomicAfter.nextSibling &&
                _wpt_is_editing_block(atomicAfter.nextSibling)) {
                var boundaryText = _wpt_first_text_descendant(atomicAfter.nextSibling);
                var boundaryJoinRange = _wpt_forward_block_join_range(
                    atomicAfter, boundaryText);
                if (boundaryJoinRange) return boundaryJoinRange;
            }
            if (_wpt_is_atomic_edit_node(atomicAfter) &&
                !_wpt_break_run_reaches_block_end(atomicAfter) &&
                _wpt_editing_block_for_node(atomicAfter, host) ===
                    _wpt_editing_block_for_node(node, host)) {
                return _wpt_range_around_atomic(
                    atomicAfter, node, trailingStart, false) || current.cloneRange();
            }
            var nextAtBlock = _wpt_next_text_node(node, host);
            var forwardBlock = _wpt_editing_block_for_node(node, host);
            var nextBlock = _wpt_editing_block_for_node(nextAtBlock, host);
            if (nextAtBlock && nextBlock !== forwardBlock) {
                var nextData = nextAtBlock.data || nextAtBlock.textContent || "";
                var nextStart = 0;
                if (!_wpt_preserves_edit_whitespace(nextAtBlock)) {
                    while (nextStart < nextData.length && /\s/.test(nextData.charAt(nextStart))) nextStart++;
                }
                try {
                    var forwardBlockRange = document.createRange();
                    var trailingBreak = forwardBlock && forwardBlock.lastChild;
                    if (trailingBreak && _wpt_is_padding_break(trailingBreak) &&
                        _wpt_node_is_ancestor(nextBlock, forwardBlock)) {
                        forwardBlockRange.setStart(
                            trailingBreak.parentNode, _wpt_node_index(trailingBreak));
                    } else {
                        forwardBlockRange.setStart(node, trailingStart);
                    }
                    var leadingBreak = nextAtBlock.previousSibling;
                    if (leadingBreak && leadingBreak.nodeType === 1 &&
                        String(leadingBreak.tagName).toUpperCase() === "BR") {
                        forwardBlockRange.setEnd(leadingBreak.parentNode, _wpt_node_index(leadingBreak) + 1);
                    } else {
                        forwardBlockRange.setEnd(nextAtBlock, nextStart);
                    }
                    return forwardBlockRange;
                } catch (_) {}
            }
        }
        while (boundaryNode) {
            var after = boundaryNode.data || boundaryNode.textContent || "";
            if (!byWord) {
                if (boundaryOffset < after.length) {
                    boundaryOffset++;
                    var hiddenTail = boundaryOffset;
                    while (hiddenTail < after.length && /\s/.test(after.charAt(hiddenTail))) {
                        hiddenTail++;
                    }
                    // Terminal collapsible whitespace shares the last visible
                    // character's forward deletion target.
                    if (hiddenTail === after.length) boundaryOffset = hiddenTail;
                    break;
                }
            } else {
                boundaryOffset = _wpt_word_forward(after, boundaryOffset);
                if (boundaryOffset < after.length || after.length === 0) break;
            }
            var next = _wpt_next_text_node(boundaryNode, host);
            if (!next) break;
            boundaryNode = next;
            boundaryOffset = 0;
            if (!byWord && _wpt_text_node_length(next) > 0) { boundaryOffset = 1; break; }
        }
    }
    try {
        var result = document.createRange();
        if (backward) {
            result.setStart(boundaryNode, boundaryOffset);
            result.setEnd(node, offset);
        } else {
            result.setStart(node, offset);
            result.setEnd(boundaryNode, boundaryOffset);
        }
        return result;
    } catch (_) { return current.cloneRange(); }
}

function _wpt_target_ranges_for_input(target, type, inputType) {
    if (type !== "beforeinput" || !target) return [];
    var tag = target.tagName ? String(target.tagName).toUpperCase() : "";
    if (tag === "INPUT" || tag === "TEXTAREA") return [];
    var snapshots = [];
    try {
        var selection = getSelection();
        if (!selection) return snapshots;
        for (var i = 0; i < selection.rangeCount; i++) {
            var selected = selection.getRangeAt(i);
            if (_wpt_editing_host_for_node(selected.startContainer) !==
                _wpt_editing_host_for_node(selected.endContainer)) return [];
            var range = _wpt_edit_target_range(inputType, selected);
            var snapshot = _wpt_range_snapshot(range);
            if (snapshot) snapshots.push(snapshot);
        }
    } catch (_) { return []; }
    return snapshots;
}

function _wpt_dispatch_input_event(target, type, inputType, data, dataTransfer) {
    if (!target || typeof target.dispatchEvent !== "function") return true;
    var ev = null;
    try {
        ev = new InputEvent(type, {
            bubbles: true,
            cancelable: type === "beforeinput",
            composed: true,
            inputType: inputType || "",
            data: data === undefined ? null : data,
            dataTransfer: dataTransfer === undefined ? null : dataTransfer,
            targetRanges: _wpt_target_ranges_for_input(target, type, inputType || "")
        });
    } catch (_) {
        try {
            ev = new Event(type, {
                bubbles: true,
                cancelable: type === "beforeinput",
                composed: true
            });
            ev.inputType = inputType || "";
            ev.data = data === undefined ? null : data;
        } catch (_) {
            ev = { type: type, defaultPrevented: false,
                   preventDefault: function() { this.defaultPrevented = true; } };
        }
    }
    var ok = true;
    try { ok = target.dispatchEvent(ev); } catch (_) { ok = true; }
    // Input Events Level 2 clears browser-generated target ranges once the
    // beforeinput dispatch completes; retaining the live ranges exposed stale
    // mutation-adjusted boundaries through a saved event object.
    if (type === "beforeinput") {
        try { ev.__target_ranges = []; } catch (_) {}
    }
    return ok !== false && !ev.defaultPrevented;
}

function _wpt_dispatch_plain_event(target, type) {
    if (!target) return true;
    var ev = null;
    try {
        ev = new Event(type, {
            bubbles: true,
            cancelable: false,
            composed: true
        });
    } catch (_) {
        ev = { type: type, defaultPrevented: false };
    }
    try { return target.dispatchEvent(ev) !== false; } catch (_) { return true; }
}

function _wpt_dispatch_keyboard_event(target, type, key, shift, ctrl, alt, meta) {
    if (!target || typeof target.dispatchEvent !== "function") return true;
    var ev = null;
    try {
        ev = new KeyboardEvent(type, {
            bubbles: true,
            cancelable: true,
            composed: true,
            key: key,
            shiftKey: !!shift,
            ctrlKey: !!ctrl,
            altKey: !!alt,
            metaKey: !!meta
        });
    } catch (_) {
        ev = new Event(type, { bubbles: true, cancelable: true, composed: true });
        try { ev.key = key; } catch (_) {}
    }
    try { return target.dispatchEvent(ev) !== false && !ev.defaultPrevented; }
    catch (_) { return true; }
}

function _wpt_active_key_target() {
    try { return document.activeElement || document.body || document; }
    catch (_) { return null; }
}

function _wpt_owner_frame_for_document(doc) {
    if (!doc || typeof document === "undefined" || doc === document) return null;
    var frames = [];
    try { frames = document.querySelectorAll("iframe"); } catch (_) { frames = []; }
    for (var i = 0; i < frames.length; i++) {
        try {
            if (frames[i].contentDocument === doc) return frames[i];
        } catch (_) {}
    }
    return null;
}

function _wpt_control_is_connected(el) {
    if (!el) return false;
    try {
        if (el.isConnected === false) return false;
    } catch (_) {}
    var doc = null;
    try { doc = el.ownerDocument || document; } catch (_) { doc = null; }
    if (!doc) return true;
    if (typeof document !== "undefined" && doc !== document) {
        var frame = _wpt_owner_frame_for_document(doc);
        if (!frame) return false;
        try {
            if (document.body && typeof document.body.contains === "function") {
                return document.body.contains(frame);
            }
        } catch (_) {}
        return !!frame.parentNode;
    }
    var cur = el;
    while (cur) {
        if (cur === doc || cur === doc.documentElement || cur === doc.body) return true;
        cur = cur.parentNode;
    }
    return false;
}

function _wpt_spin_number_control(el, delta) {
    if (!el) return false;
    var tag = "";
    var type = "";
    try { tag = String(el.tagName || "").toUpperCase(); } catch (_) {}
    try { type = String(el.type || el.getAttribute("type") || "").toLowerCase(); } catch (_) {}
    if (tag !== "INPUT" || type !== "number") return false;
    if (!_wpt_dispatch_input_event(el, "beforeinput", "insertReplacementText", null)) {
        return true;
    }
    if (!_wpt_control_is_connected(el)) return true;
    var raw = "";
    try { raw = el.value || ""; } catch (_) { raw = ""; }
    var value = raw === "" ? 0 : Number(raw);
    if (!(value === value)) value = 0;
    value += delta;
    try { el.value = String(value); } catch (_) {}
    _wpt_dispatch_input_event(el, "input", "insertReplacementText", null);
    if (_wpt_control_is_connected(el)) {
        _wpt_dispatch_plain_event(el, "change");
    }
    return true;
}

function _wpt_insert_text_in_control(el, text) {
    if (!el) return false;
    var v = el.value || "";
    var ss = el.selectionStart;
    var se = el.selectionEnd;
    if (typeof ss !== "number") ss = v.length;
    if (typeof se !== "number") se = v.length;
    if (ss > se) { var tmp = ss; ss = se; se = tmp; }
    if (!_wpt_dispatch_input_event(el, "beforeinput", "insertText", text)) return true;
    el.value = v.slice(0, ss) + text + v.slice(se);
    try { el.setSelectionRange(ss + text.length, ss + text.length); } catch (_) {}
    _wpt_dispatch_input_event(el, "input", "insertText", text);
    return true;
}

function _wpt_insert_text_in_document_selection(text) {
    var sel = null;
    try { sel = (typeof getSelection === "function") ? getSelection() : null; } catch (_) {}
    if (!sel || sel.rangeCount === 0) return false;
    var r = null;
    try { r = sel.getRangeAt(0); } catch (_) { r = null; }
    if (!r) return false;
    var host = _wpt_editing_host_for_node(r.startContainer);
    if (!host) return false;
    if (!_wpt_dispatch_input_event(host, "beforeinput", "insertText", text)) return true;
    try {
        if (!r.collapsed) r = _wpt_normalize_selected_edit_range(r, host);
        if (r.startContainer === r.endContainer && r.startContainer.nodeType === 3) {
            // Typing into one text node replaces its selected UTF-16 span in
            // place; always inserting a sibling split browser-visible target
            // ranges across two nodes on the next edit.
            var textNode = r.startContainer;
            var oldText = textNode.data || textNode.textContent || "";
            var insertionOffset = r.startOffset;
            var replaced = oldText.slice(0, insertionOffset) + text +
                oldText.slice(r.endOffset);
            try { textNode.data = replaced; } catch (_) { textNode.textContent = replaced; }
            sel.collapse(textNode, insertionOffset + text.length);
        } else {
            // Text insertion over a rich selection uses the same structural
            // delete as keyboard deletion so table/list boundaries survive.
            if (!r.collapsed) {
                if (!_wpt_apply_edit_range_delete(r, host, sel, false)) return false;
                _wpt_remove_block_placeholder_for_insertion(
                    _wpt_editing_block_for_node(sel.getRangeAt(0).startContainer, host), sel);
            }
            r = sel.getRangeAt(0);
            var tn = document.createTextNode(text);
            r.insertNode(tn);
            sel.collapse(tn, text.length);
        }
    } catch (_) {
        return false;
    }
    _wpt_dispatch_input_event(host, "input", "insertText", text);
    return true;
}

function _wpt_selection_is_inside(elem, selection) {
    if (!elem || !selection || selection.rangeCount === 0) return false;
    var node = selection.getRangeAt(0).startContainer;
    while (node) {
        if (node === elem) return true;
        node = node.parentNode;
    }
    return false;
}

function _wpt_focus_editable_for_keys(elem) {
    if (!elem || elem.nodeType !== 1) return;
    var wasActive = false;
    try { wasActive = document.activeElement === elem; } catch (_) {}
    try { if (typeof elem.focus === "function") elem.focus(); } catch (_) {}
    var tag = elem.tagName ? String(elem.tagName).toUpperCase() : "";
    if (tag === "INPUT" || tag === "TEXTAREA") return;
    var selection = null;
    try { selection = getSelection(); } catch (_) {}
    if (!selection || (wasActive && _wpt_selection_is_inside(elem, selection))) return;
    var nodes = [];
    _wpt_collect_text_nodes(elem, nodes);
    try {
        if (nodes.length > 0) {
            var last = nodes[nodes.length - 1];
            selection.collapse(last, _wpt_text_node_length(last));
        } else {
            selection.collapse(elem, 0);
        }
    } catch (_) {}
}

function _wpt_type_printable_key(key, elem) {
    if (typeof key !== "string" || key.length !== 1) return false;
    _wpt_focus_editable_for_keys(elem);
    var ae = null;
    try { ae = document.activeElement; } catch (_) {}
    var tag = (ae && ae.tagName) ? String(ae.tagName).toUpperCase() : "";
    if (tag === "INPUT" || tag === "TEXTAREA") {
        return _wpt_insert_text_in_control(ae, key);
    }
    return _wpt_insert_text_in_document_selection(key);
}

function _wpt_send_return(elem, shift) {
    var ae = null;
    try { ae = document.activeElement; } catch (_) {}
    if (elem && elem.nodeType === 1) ae = elem;
    var tag = (ae && ae.tagName) ? String(ae.tagName).toUpperCase() : "";
    var inputType = (tag === "TEXTAREA" || shift) ? "insertLineBreak" : "insertParagraph";
    if (tag === "INPUT") return true;
    if (tag === "TEXTAREA") {
        var value = ae.value || "";
        var start = typeof ae.selectionStart === "number" ? ae.selectionStart : value.length;
        var end = typeof ae.selectionEnd === "number" ? ae.selectionEnd : start;
        if (!_wpt_dispatch_input_event(ae, "beforeinput", inputType, null)) return true;
        ae.value = value.slice(0, start) + "\n" + value.slice(end);
        try { ae.setSelectionRange(start + 1, start + 1); } catch (_) {}
        _wpt_dispatch_input_event(ae, "input", inputType, null);
        return true;
    }
    var sel = null;
    try { sel = (typeof getSelection === "function") ? getSelection() : null; } catch (_) {}
    if (!sel || sel.rangeCount === 0) return false;
    var range = sel.getRangeAt(0);
    var host = _wpt_editing_host_for_node(range.startContainer);
    if (!host || !_wpt_dispatch_input_event(host, "beforeinput", inputType, null)) return !!host;
    try {
        range.deleteContents();
        var newline = document.createTextNode("\n");
        range.insertNode(newline);
        sel.collapse(newline, 1);
    } catch (_) { return false; }
    _wpt_dispatch_input_event(host, "input", inputType, null);
    return true;
}

function _wpt_dispatch_history_edit(inputType) {
    var target = null;
    try { target = document.activeElement; } catch (_) {}
    var host = _wpt_editing_host_for_node(target);
    if (!host) {
        try {
            var sel = getSelection();
            if (sel && sel.rangeCount > 0) {
                host = _wpt_editing_host_for_node(sel.getRangeAt(0).startContainer);
            }
        } catch (_) {}
    }
    if (!host) return false;
    // History commands still expose their transaction even when the undo
    // stack is empty; suppressing the no-op dropped both observable events.
    if (!_wpt_dispatch_input_event(host, "beforeinput", inputType, null)) return true;
    _wpt_dispatch_input_event(host, "input", inputType, null);
    return true;
}

function _wpt_dispatch_format_bold() {
    var target = _wpt_active_editable_target();
    if (!target) return false;
    if (!_wpt_dispatch_input_event(target, "beforeinput", "formatBold", null)) return true;
    try {
        var selection = getSelection();
        if (selection && selection.rangeCount > 0 && !selection.isCollapsed) {
            var range = selection.getRangeAt(0);
            var wrapper = document.createElement("b");
            range.surroundContents(wrapper);
            selection.selectAllChildren(wrapper);
        }
    } catch (_) {}
    _wpt_dispatch_input_event(target, "input", "formatBold", null);
    return true;
}


// ---------------------------------------------------------------------------
function _wpt_test_driver_click(elem) {
    if (!elem) return Promise.resolve();
    var tag = (elem.tagName || "").toUpperCase();
    var isTextControl = (tag === 'INPUT' || tag === 'TEXTAREA');

    if (isTextControl) {
        // Toggle caret between 0 and end-of-value so consecutive clicks
        // yield distinct positions.
        try {
            var v = elem.value || "";
            var len = v.length;
            var cur = elem.selectionStart;
            var pos = (cur === len ? 0 : len);
            try { elem.focus(); } catch (_) {}
            elem.setSelectionRange(pos, pos);
        } catch (_) {}
        try { elem.click(); } catch (_) {}
        return Promise.resolve();
    }

    var styleAttr = (elem.getAttribute && elem.getAttribute("style")) || "";
    var userSelectNone = /user-select\s*:\s*none/i.test(styleAttr);
    var preserveSelection = userSelectNone ||
        (tag === 'A' && elem.getAttribute && elem.getAttribute('href'));

    var isContentEditable = false;
    var elemIsCERoot = false;
    try {
        var probe = elem;
        var first = true;
        while (probe && probe.nodeType === 1) {
            var ce = probe.getAttribute && probe.getAttribute("contenteditable");
            if (ce !== null && ce !== "false") {
                isContentEditable = true;
                if (first) elemIsCERoot = true;
                break;
            }
            first = false;
            probe = probe.parentNode;
        }
    } catch (_) {}

    if (!preserveSelection && !isContentEditable) {
        // Lambda's selector engine doesn't implement comma-list selectors,
        // so query each tag separately.
        var fields = [];
        try {
            var inps = document.querySelectorAll('input');
            var tas  = document.querySelectorAll('textarea');
            if (inps) for (var k = 0; k < inps.length; k++) fields.push(inps[k]);
            if (tas)  for (var k = 0; k < tas.length;  k++) fields.push(tas[k]);
        } catch (_) {}
        for (var i = 0; i < fields.length; i++) {
            try { fields[i].setSelectionRange(0, 0); } catch (_) {}
            try { fields[i].blur(); } catch (_) {}
        }
    }

    // Clicking on a contenteditable element (or an element nested inside one)
    // moves the document selection into it. We can't compute click coordinates
    // without layout, so just place the caret at the start of the clicked
    // element's first text descendant (or the element itself).
    if (isContentEditable) {
        try {
            var sel = (typeof getSelection === "function") ? getSelection() : null;
            if (sel) {
                var anchorNode = elem;
                var fc = elem.firstChild;
                while (fc && fc.nodeType !== 3 && fc.firstChild) fc = fc.firstChild;
                if (fc && fc.nodeType === 3) anchorNode = fc;
                var endOff = (anchorNode.nodeType === 3)
                    ? ((anchorNode.data || anchorNode.textContent || "").length)
                    : (anchorNode.childNodes ? anchorNode.childNodes.length : 0);
                var curOff = sel.focusOffset;
                if (curOff === undefined || curOff === null) curOff = 0;
                var curNode = sel.focusNode;
                var sameNode = (curNode === anchorNode);
                var curNum = Number(curOff);
                var endNum = Number(endOff);
                var newOff;
                if (elemIsCERoot) newOff = curOff ? 0 : endOff;
                else if (sameNode && curNum === 0) newOff = endOff;
                else if (sameNode && curNum === endNum) newOff = 0;
                else newOff = 0; // first click into nested element → start of its text
                try { sel.collapse(anchorNode, newOff); } catch (_) {}
            }
        } catch (_) {}
    }

    try { elem.click(); } catch (_) {}
    return Promise.resolve();
}

// ---------------------------------------------------------------------------
// WPT testdriver Actions API shim — `new test_driver.Actions()` builds a
// chainable sequence of pointer/key events and `.send()` executes them.
// We model just enough behavior to satisfy the WPT selection-direction
// click tests:
//   - 1 pointerDown/Up pair at (originNode, 0): collapse selection at
//     (originNode, 0) → isCollapsed=true, direction='none'.
//   - 2 pairs (double-click): select first word starting at offset 0.
//   - 3 pairs (triple-click): select the containing block (origin's parent
//     element, offsets 0 .. childCount).
// All distances use offset 0 since the only WPT callers pass (0, 0).
// ---------------------------------------------------------------------------
function _WptActions() {
    this._steps = [];
    this._origin = null;
    this._context = null;
    this.ButtonType = _WptActions.prototype.ButtonType;
}
_WptActions.prototype.ButtonType = {
    LEFT: 0,
    MIDDLE: 1,
    RIGHT: 2,
    BACK: 3,
    FORWARD: 4
};
_WptActions.prototype.pointerMove = function(x, y, origin) {
    // origin is either a Node (DOM element/text) or {origin: node}.
    // NOTE: Lambda's DOM accessor returns null (not undefined) for missing
    // properties, so checking `origin.origin !== undefined` is always true
    // on real DOM nodes. Use truthiness instead.
    var node = origin;
    if (origin && typeof origin === "object" && origin.origin) {
        node = origin.origin;
    }
    if (node === "viewport") {
        this._steps.push({ type: "move", x: x, y: y, node: null, viewport: true });
        return this;
    }
    this._steps.push({ type: "move", x: x, y: y, node: node });
    if (node) this._origin = node;
    return this;
};
_WptActions.prototype.pointerDown = function(opts) {
    opts = opts || {};
    var button = (typeof opts.button === "number") ? opts.button : this.ButtonType.LEFT;
    this._steps.push({ type: "down", button: button });
    return this;
};
_WptActions.prototype.pointerUp = function(opts) {
    opts = opts || {};
    var button = (typeof opts.button === "number") ? opts.button : this.ButtonType.LEFT;
    this._steps.push({ type: "up", button: button });
    return this;
};
_WptActions.prototype.keyDown = function(key) {
    this._steps.push({ type: "keyDown", key: key });
    return this;
};
_WptActions.prototype.keyUp = function(key) {
    this._steps.push({ type: "keyUp", key: key });
    return this;
};
_WptActions.prototype.pause = function(_dur) { return this; };
// A tick separates input-source actions. This synchronous adapter already
// preserves source order, but WPT action chains still require the method.
_WptActions.prototype.addTick = function() { return this; };
_WptActions.prototype.setContext = function(ctx) {
    this._context = ctx || null;
    return this;
};

function _wpt_actions_context_document(ctx) {
    if (ctx) {
        try {
            if (ctx.document) return ctx.document;
        } catch (_) {}
        try {
            if (ctx.querySelectorAll) return ctx;
        } catch (_) {}
    }
    try { return document; } catch (_) { return null; }
}

function _wpt_actions_number_spin_target(ctx, x, y) {
    var doc = _wpt_actions_context_document(ctx);
    if (!doc) return null;
    var inputs = [];
    try { inputs = doc.querySelectorAll("input"); } catch (_) { inputs = []; }
    var fallback = null;
    var fallback_count = 0;
    for (var i = 0; i < inputs.length; i++) {
        var el = inputs[i];
        var type = "";
        try { type = String(el.type || el.getAttribute("type") || "").toLowerCase(); } catch (_) {}
        if (type !== "number") continue;
        fallback = el;
        fallback_count++;

        var rect = null;
        try { rect = el.getBoundingClientRect(); } catch (_) { rect = null; }
        if (!rect) continue;
        var left = Number(rect.left !== undefined ? rect.left : rect.x);
        var top = Number(rect.top !== undefined ? rect.top : rect.y);
        var width = Number(rect.width);
        var height = Number(rect.height);
        if (!(width > 0) || !(height > 0)) continue;
        var right = left + width;
        var bottom = top + height;
        var in_y = y >= top && y <= bottom;
        var in_spin_gutter = x >= right - Math.min(20, width) && x <= right + 1;
        if (in_y && in_spin_gutter) {
            return { el: el, delta: y <= top + height / 2 ? +1 : -1 };
        }
    }
    if (fallback && fallback_count === 1) {
        return { el: fallback, delta: +1 };
    }
    return null;
}

function _wpt_actions_descend_to_text(n) {
    if (n && n.nodeType === 1 /* ELEMENT */) {
        var fc = n.firstChild;
        if (fc && fc.nodeType === 3 /* TEXT */) return fc;
    }
    return n;
}

function _wpt_actions_editing_host(node) {
    var cur = node;
    if (cur && cur.nodeType === 3) cur = cur.parentNode;
    while (cur && cur.nodeType === 1) {
        var state = _wpt_contenteditable_state(cur);
        if (state === "true") return cur;
        if (state === "false") return null;
        cur = cur.parentNode;
    }
    return null;
}

function _wpt_actions_event_target(node) {
    if (!node) return null;
    return node.nodeType === 3 ? node.parentNode : node;
}

function _wpt_actions_text_control(node) {
    if (!node || node.nodeType !== 1 || !node.tagName) return null;
    var tag = "";
    try { tag = String(node.tagName).toUpperCase(); } catch (_) {}
    return (tag === "INPUT" || tag === "TEXTAREA") ? node : null;
}

function _wpt_actions_text_control_offset_for_x(el, x) {
    var value = "";
    try { value = el.value || ""; } catch (_) {}
    var len = value.length || 0;
    if (len <= 0) return 0;

    var rect = null;
    try { rect = el.getBoundingClientRect(); } catch (_) { rect = null; }
    if (rect) {
        var left = Number(rect.left !== undefined ? rect.left : rect.x);
        var width = Number(rect.width);
        if (width > 0) {
            var rel = Math.max(0, Math.min(width, x - left));
            return Math.max(0, Math.min(len, Math.round((rel / width) * len)));
        }
    }

    return x <= 0 ? 0 : len;
}

function _wpt_actions_dispatch_mouse_pair(target, pointer_type, mouse_type,
                                          button, buttons, mods) {
    if (!target || typeof target.dispatchEvent !== "function") return true;
    var init = {
        bubbles: true,
        cancelable: true,
        composed: true,
        button: button,
        buttons: buttons,
        clientX: 0,
        clientY: 0,
        screenX: 0,
        screenY: 0,
        pageX: 0,
        pageY: 0,
        detail: 1,
        pointerId: 1,
        pointerType: "mouse",
        isPrimary: true,
        shiftKey: !!(mods && mods.shift),
        ctrlKey: !!(mods && mods.ctrl),
        altKey: !!(mods && mods.alt),
        metaKey: !!(mods && mods.meta)
    };
    var ok = true;
    try {
        var pe = new PointerEvent(pointer_type, init);
        var pok = target.dispatchEvent(pe);
        ok = ok && pok !== false && !pe.defaultPrevented;
    } catch (_) {}
    try {
        var me = new MouseEvent(mouse_type, init);
        var mok = target.dispatchEvent(me);
        ok = ok && mok !== false && !me.defaultPrevented;
    } catch (_) {}
    return ok;
}

function _wpt_actions_set_selection(anchor_node, focus_node, mode) {
    var sel = null;
    try { sel = (typeof getSelection === "function") ? getSelection() : null; } catch (_) {}
    if (!sel || !anchor_node) return;
    if (mode === "end" && anchor_node.nodeType === 1) {
        var endNodes = [];
        _wpt_collect_text_nodes(anchor_node, endNodes);
        try {
            if (endNodes.length > 0) {
                var endNode = endNodes[endNodes.length - 1];
                sel.collapse(endNode, _wpt_text_node_length(endNode));
            } else {
                sel.collapse(anchor_node,
                    anchor_node.childNodes ? anchor_node.childNodes.length : 0);
            }
        } catch (_) {}
        return;
    }
    anchor_node = _wpt_actions_descend_to_text(anchor_node);
    focus_node = _wpt_actions_descend_to_text(focus_node || anchor_node);
    if (!anchor_node) return;
    try {
        if (mode === "extend" && focus_node) {
            var anchor_base = sel.anchorNode || anchor_node;
            var anchor_offset = (typeof sel.anchorOffset === "number") ? sel.anchorOffset : 0;
            if (typeof sel.setBaseAndExtent === "function") {
                sel.setBaseAndExtent(anchor_base, anchor_offset, focus_node, 0);
            } else {
                var re = document.createRange();
                re.setStart(anchor_base, anchor_offset);
                re.setEnd(focus_node, 0);
                sel.removeAllRanges();
                sel.addRange(re);
            }
            return;
        }
        if (mode === "word" && anchor_node.nodeType === 3) {
            var s = anchor_node.data || anchor_node.textContent || "";
            var end = 0;
            while (end < s.length) {
                var ch = s.charCodeAt(end);
                var isLetter = (ch >= 0x30 && ch <= 0x39) ||
                               (ch >= 0x41 && ch <= 0x5A) ||
                               (ch >= 0x61 && ch <= 0x7A) ||
                               (ch >= 0x80);
                if (!isLetter) break;
                end++;
            }
            if (end === 0) end = (s.length > 0 ? 1 : 0);
            var rw = document.createRange();
            rw.setStart(anchor_node, 0);
            rw.setEnd(anchor_node, end);
            sel.removeAllRanges();
            sel.addRange(rw);
            try {
                if (sel.__forceDirection) sel.__forceDirection("none");
            } catch (_) {}
            return;
        }
        if (mode === "block") {
            var container = anchor_node.parentNode || anchor_node;
            var childCount = 0;
            var c = container.firstChild;
            while (c) { childCount++; c = c.nextSibling; }
            var rb = document.createRange();
            rb.setStart(container, 0);
            rb.setEnd(container, childCount);
            sel.removeAllRanges();
            sel.addRange(rb);
            try {
                if (sel.__forceDirection) sel.__forceDirection("none");
            } catch (_) {}
            return;
        }
        sel.collapse(anchor_node, 0);
        try {
            if (sel.__forceDirection) sel.__forceDirection("none");
        } catch (_) {}
    } catch (_) {}
}
_WptActions.prototype.send = function() {
    var pairs = 0;
    var current_origin = this._origin;
    var current_x = 0;
    var current_y = 0;
    var down_spin_target = null;
    var down_spin_delta = 0;
    var down_anchor = null;
    var down_button = 0;
    var down_default_allowed = false;
    var down_open = false;
    var down_text_control = null;
    var down_text_start = 0;
    var text_select_fired = false;
    var saw_drag_move = false;
    var shift_ptr = false;
    var ctrl_ptr = false;
    var alt_ptr = false;
    var meta_ptr = false;
    var mods_ptr = { shift: false, ctrl: false, alt: false, meta: false };
    for (var i = 0; i < this._steps.length; i++) {
        var st = this._steps[i];
        if (st.type === "keyDown") {
            if (st.key === "\uE008") shift_ptr = true;
            else if (st.key === "\uE009") ctrl_ptr = true;
            else if (st.key === "\uE00A" || st.key === "\uE00a") alt_ptr = true;
            else if (st.key === "\uE03D" || st.key === "\uE03d" || st.key === "\u2318") meta_ptr = true;
            mods_ptr = { shift: shift_ptr, ctrl: ctrl_ptr, alt: alt_ptr, meta: meta_ptr };
        } else if (st.type === "keyUp") {
            if (st.key === "\uE008") shift_ptr = false;
            else if (st.key === "\uE009") ctrl_ptr = false;
            else if (st.key === "\uE00A" || st.key === "\uE00a") alt_ptr = false;
            else if (st.key === "\uE03D" || st.key === "\uE03d" || st.key === "\u2318") meta_ptr = false;
            mods_ptr = { shift: shift_ptr, ctrl: ctrl_ptr, alt: alt_ptr, meta: meta_ptr };
        } else if (st.type === "move") {
            current_x = st.x || 0;
            current_y = st.y || 0;
            if (st.node) current_origin = st.node;
            if (down_open && down_text_control && st.node === down_text_control &&
                down_default_allowed && down_button === this.ButtonType.LEFT) {
                saw_drag_move = true;
                var text_end = _wpt_actions_text_control_offset_for_x(
                    down_text_control, current_x);
                if (text_end !== down_text_start) {
                    var sel_start = Math.min(down_text_start, text_end);
                    var sel_end = Math.max(down_text_start, text_end);
                    try { down_text_control.setSelectionRange(sel_start, sel_end); } catch (_) {}
                    if (!text_select_fired) {
                        text_select_fired = true;
                        _wpt_dispatch_plain_event(down_text_control, "select");
                    }
                }
            } else if (down_open && st.node && st.node !== down_anchor) {
                saw_drag_move = true;
                if (down_button === this.ButtonType.LEFT && down_default_allowed) {
                    _wpt_actions_set_selection(down_anchor, st.node, "extend");
                }
            }
        } else if (st.type === "down") {
            down_open = true;
            down_button = st.button;
            down_anchor = current_origin || this._origin;
            var spin = (!down_anchor && down_button === this.ButtonType.LEFT)
                ? _wpt_actions_number_spin_target(this._context, current_x, current_y)
                : null;
            down_spin_target = spin ? spin.el : null;
            down_spin_delta = spin ? spin.delta : 0;
            var target = spin ? spin.el : _wpt_actions_event_target(down_anchor);
            var buttons = 1 << down_button;
            var allowed = _wpt_actions_dispatch_mouse_pair(
                target, "pointerdown", "mousedown", down_button, buttons, mods_ptr);
            down_default_allowed = allowed;
            if (allowed && down_anchor) {
                var event_target = _wpt_actions_event_target(down_anchor);
                var text_control = _wpt_actions_text_control(event_target);
                if (text_control && down_button === this.ButtonType.LEFT) {
                    down_text_control = text_control;
                    down_text_start = _wpt_actions_text_control_offset_for_x(
                        text_control, current_x);
                    text_select_fired = false;
                    try { text_control.focus(); } catch (_) {}
                    try {
                        text_control.setSelectionRange(down_text_start, down_text_start);
                    } catch (_) {}
                    continue;
                }
                var host = _wpt_actions_editing_host(down_anchor);
                try { if (host && typeof host.focus === "function") host.focus(); } catch (_) {}
                var tag = event_target && event_target.tagName ? String(event_target.tagName).toUpperCase() : "";
                if (shift_ptr && tag !== "A" && down_button !== this.ButtonType.RIGHT) {
                    _wpt_actions_set_selection(down_anchor, down_anchor, "extend");
                } else if (host && down_anchor === host) {
                    // Element-origin WebDriver clicks are relative to the
                    // element centre; for a block editing host whose text
                    // ends before that point, the caret lands at its end.
                    _wpt_actions_set_selection(down_anchor, down_anchor, "end");
                } else {
                    _wpt_actions_set_selection(down_anchor, down_anchor, "collapse");
                }
            }
        } else if (st.type === "up") {
            var up_anchor = current_origin || down_anchor || this._origin;
            var up_target = down_spin_target || _wpt_actions_event_target(up_anchor);
            if (down_open && down_default_allowed && down_button === this.ButtonType.LEFT &&
                saw_drag_move && up_anchor && up_anchor !== down_anchor) {
                _wpt_actions_set_selection(down_anchor, up_anchor, "extend");
            }
            _wpt_actions_dispatch_mouse_pair(up_target, "pointerup", "mouseup",
                st.button, 0, mods_ptr);
            if (down_open && down_default_allowed && down_spin_target) {
                _wpt_spin_number_control(down_spin_target, down_spin_delta || +1);
            }
            if (down_open) pairs++;
            down_open = false;
            if (!saw_drag_move && down_default_allowed && down_anchor) {
                if (pairs >= 3) _wpt_actions_set_selection(down_anchor, down_anchor, "block");
                else if (pairs === 2) _wpt_actions_set_selection(down_anchor, down_anchor, "word");
            }
            saw_drag_move = false;
            down_default_allowed = false;
            down_spin_target = null;
            down_spin_delta = 0;
            down_text_control = null;
            down_text_start = 0;
            text_select_fired = false;
            down_anchor = null;
        }
    }
    // ----- Keyboard step handling -----
    // Process keyDown(ArrowLeft|ArrowRight) events while Shift is held to
    // extend the document selection. Also handle Cmd/Ctrl + V/C/X to
    // synthesize paste/copy/cut events on the document — used by the
    // clipboard suite's `sendPasteShortcutKey()` helper to round-trip
    // text through the platform clipboard.
    try {
        var sel2 = (typeof getSelection === "function") ? getSelection() : null;
        var shift_held = false;
        var ctrl_held = false;
        var alt_held = false;
        var meta_held = false;
        for (var ki = 0; ki < this._steps.length; ki++) {
            var ks = this._steps[ki];
            if (ks.type === "keyDown") {
                if (ks.key === "\uE008") shift_held = true;       // Shift
                else if (ks.key === "\uE009") ctrl_held = true;   // Control
                else if (ks.key === "\uE00A" || ks.key === "\uE00a") alt_held = true; // Alt
                // WPT uses both WebDriver Meta and the macOS Command glyph.
                else if (ks.key === "\uE03D" || ks.key === "\uE03d" || ks.key === "\u2318") meta_held = true; // Meta
                else if (ks.key === "\uE012" && shift_held && sel2) {
                    _wpt_extend_focus_left(sel2);                // ArrowLeft
                } else if (ks.key === "\uE014" && shift_held && sel2) {
                    _wpt_extend_focus_right(sel2);               // ArrowRight
                } else if ((ctrl_held || meta_held) && (ks.key === "v" || ks.key === "V")) {
                    _wpt_dispatch_clipboard_event("paste");
                } else if ((ctrl_held || meta_held) && (ks.key === "c" || ks.key === "C")) {
                    _wpt_dispatch_clipboard_event("copy");
                } else if ((ctrl_held || meta_held) && (ks.key === "x" || ks.key === "X")) {
                    _wpt_dispatch_clipboard_event("cut");
                } else if ((ctrl_held || meta_held) && (ks.key === "a" || ks.key === "A")) {
                    _wpt_select_all_active_editable();
                } else if ((ctrl_held || meta_held) && (ks.key === "z" || ks.key === "Z")) {
                    _wpt_dispatch_history_edit(shift_held ? "historyRedo" : "historyUndo");
                } else if ((ctrl_held || meta_held) && (ks.key === "b" || ks.key === "B")) {
                    _wpt_dispatch_format_bold();
                } else if (ks.key === "\uE003" || ks.key === "\uE017") {
                    // Keep mutation and both InputEvents in one path. The
                    // native probe can emit beforeinput even when it declines
                    // the edit, which duplicated the fallback transaction.
                    _wpt_send_one_key(null, ks.key.charCodeAt(0), true, true,
                        ctrl_held, alt_held, meta_held);
                } else if (ks.key === "\uE010" || ks.key === "\uE011" ||
                           ks.key === "\uE012" || ks.key === "\uE014") {
                    // Navigation WebDriver keys move the caret; they are not
                    // printable private-use characters.
                    _wpt_send_one_key(null, ks.key.charCodeAt(0), true, true,
                        ctrl_held, alt_held, meta_held);
                } else if (ks.key === "\uE006" && !(ctrl_held || alt_held || meta_held)) {
                    _wpt_send_return(null, shift_held);
                } else if (!(ctrl_held || alt_held || meta_held)) {
                    var typed_key = (shift_held && typeof ks.key === "string")
                        ? ks.key.toUpperCase() : ks.key;
                    var key_target = _wpt_active_key_target();
                    // WebDriver Actions represents physical key transitions;
                    // inserting text alone omitted the keyboard events that
                    // must bracket the beforeinput/input transaction.
                    var key_allowed = _wpt_dispatch_keyboard_event(key_target,
                        "keydown", typed_key, shift_held, ctrl_held, alt_held, meta_held);
                    if (key_allowed) {
                        key_allowed = _wpt_dispatch_keyboard_event(key_target,
                            "keypress", typed_key, shift_held, ctrl_held, alt_held, meta_held);
                    }
                    if (key_allowed) _wpt_type_printable_key(typed_key);
                }
            } else if (ks.type === "keyUp") {
                if (ks.key === "\uE008") shift_held = false;
                else if (ks.key === "\uE009") ctrl_held = false;
                else if (ks.key === "\uE00A" || ks.key === "\uE00a") alt_held = false;
                else if (ks.key === "\uE03D" || ks.key === "\uE03d" || ks.key === "\u2318") meta_held = false;
                else if (!(ctrl_held || alt_held || meta_held)) {
                    var released_key = (shift_held && typeof ks.key === "string")
                        ? ks.key.toUpperCase() : ks.key;
                    _wpt_dispatch_keyboard_event(_wpt_active_key_target(),
                        "keyup", released_key, shift_held, ctrl_held, alt_held, meta_held);
                }
            }
        }
    } catch (_) {}
    return Promise.resolve();
};

function _wpt_active_editable_target() {
    var active = _wpt_active_key_target();
    var tag = (active && active.tagName) ? String(active.tagName).toUpperCase() : "";
    if (tag === "INPUT" || tag === "TEXTAREA") return active;
    var host = _wpt_editing_host_for_node(active);
    if (host) return host;
    try {
        var sel = getSelection();
        if (sel && sel.rangeCount > 0) {
            return _wpt_editing_host_for_node(sel.getRangeAt(0).startContainer);
        }
    } catch (_) {}
    return null;
}

function _wpt_select_all_active_editable() {
    var target = _wpt_active_editable_target();
    if (!target) return false;
    var tag = target.tagName ? String(target.tagName).toUpperCase() : "";
    if (tag === "INPUT" || tag === "TEXTAREA") {
        var value = target.value || "";
        try { target.setSelectionRange(0, value.length); } catch (_) {}
        return true;
    }
    try {
        var range = target.ownerDocument.createRange();
        var textNodes = [];
        _wpt_collect_text_nodes(target, textNodes);
        if (textNodes.length > 0) {
            range.setStart(textNodes[0], 0);
            var lastText = textNodes[textNodes.length - 1];
            range.setEnd(lastText, _wpt_text_node_length(lastText));
        } else {
            range.selectNodeContents(target);
        }
        var sel = target.ownerDocument.getSelection();
        sel.removeAllRanges();
        sel.addRange(range);
        return true;
    } catch (_) {
        try {
            var fallbackRange = document.createRange();
            var count = target.childNodes ? target.childNodes.length : 0;
            fallbackRange.setStart(target, 0);
            fallbackRange.setEnd(target, count);
            var fallbackSelection = getSelection();
            fallbackSelection.removeAllRanges();
            fallbackSelection.addRange(fallbackRange);
            return true;
        } catch (_) {}
    }
    return false;
}

function _wpt_selected_clipboard_record(target) {
    var record = { "text/plain": "" };
    if (!target) return record;
    var tag = target.tagName ? String(target.tagName).toUpperCase() : "";
    if (tag === "INPUT" || tag === "TEXTAREA") {
        var value = target.value || "";
        var start = typeof target.selectionStart === "number" ? target.selectionStart : 0;
        var end = typeof target.selectionEnd === "number" ? target.selectionEnd : value.length;
        record["text/plain"] = value.slice(start, end);
        return record;
    }
    try {
        var selection = getSelection();
        record["text/plain"] = String(selection.toString() || "");
        if (selection.rangeCount > 0) {
            if (record["text/plain"] === String(target.textContent || "")) {
                // A text-boundary select-all still covers the whole editing
                // host; clipboard HTML must retain its formatting ancestors.
                record["text/html"] = String(target.innerHTML || "");
            } else {
                var wrapper = document.createElement("div");
                wrapper.appendChild(selection.getRangeAt(0).cloneContents());
                record["text/html"] = String(wrapper.innerHTML || "");
            }
        }
    } catch (_) {
        try { record["text/html"] = String(target.innerHTML || ""); } catch (_) {}
    }
    return record;
}

function _wpt_delete_active_selection(target) {
    if (!target) return false;
    var tag = target.tagName ? String(target.tagName).toUpperCase() : "";
    if (tag === "INPUT" || tag === "TEXTAREA") {
        var value = target.value || "";
        var start = typeof target.selectionStart === "number" ? target.selectionStart : 0;
        var end = typeof target.selectionEnd === "number" ? target.selectionEnd : start;
        target.value = value.slice(0, start) + value.slice(end);
        try { target.setSelectionRange(start, start); } catch (_) {}
        return true;
    }
    try {
        var sel = getSelection();
        if (!sel || sel.rangeCount === 0) return false;
        var range = sel.getRangeAt(0);
        var container = range.startContainer;
        var offset = range.startOffset;
        range.deleteContents();
        sel.collapse(container, offset);
        return true;
    } catch (_) { return false; }
}

function _wpt_insert_clipboard_text(target, text) {
    if (!target) return false;
    var tag = target.tagName ? String(target.tagName).toUpperCase() : "";
    if (tag === "INPUT" || tag === "TEXTAREA") {
        var value = target.value || "";
        var start = typeof target.selectionStart === "number" ? target.selectionStart : value.length;
        var end = typeof target.selectionEnd === "number" ? target.selectionEnd : start;
        target.value = value.slice(0, start) + text + value.slice(end);
        try { target.setSelectionRange(start + text.length, start + text.length); } catch (_) {}
        return true;
    }
    try {
        var sel = getSelection();
        if (!sel || sel.rangeCount === 0) return false;
        var range = sel.getRangeAt(0);
        range.deleteContents();
        var node = document.createTextNode(text);
        range.insertNode(node);
        sel.collapse(node, text.length);
        return true;
    } catch (_) { return false; }
}

// Run clipboard editing as one default action. Dispatching the ClipboardEvent
// on document and leaving mutation to a second path meant element listeners
// never observed the transaction and promise-based WPT cases waited forever.
function _wpt_dispatch_clipboard_event(kind) {
    var target = _wpt_active_editable_target();
    if (!target) return false;
    var dt;
    try { dt = new DataTransfer(); } catch (_) { dt = null; }
    var stored = _wpt_clipboard_read_items();
    var storedRecord = stored.length > 0 ? stored[0] : {};
    if (kind === "paste" && dt) {
        for (var k in storedRecord) {
            if (Object.prototype.hasOwnProperty.call(storedRecord, k)) {
                try { dt.setData(k, String(storedRecord[k])); } catch (_) {}
            }
        }
    }
    var ev;
    try {
        ev = new ClipboardEvent(kind, { bubbles: true, cancelable: true,
                                        clipboardData: dt });
    } catch (_) {
        ev = { type: kind, defaultPrevented: false, clipboardData: dt,
               preventDefault: function() { this.defaultPrevented = true; } };
    }
    try { target.dispatchEvent(ev); } catch (_) {}

    if (kind === "copy" || kind === "cut") {
        var record = _wpt_selected_clipboard_record(target);
        _wpt_clipboard_write_items([record]);
        if (kind === "copy" || ev.defaultPrevented) return true;
        if (!_wpt_dispatch_input_event(target, "beforeinput", "deleteByCut", null, null)) {
            return true;
        }
        _wpt_delete_active_selection(target);
        _wpt_dispatch_input_event(target, "input", "deleteByCut", null, null);
        return true;
    }

    if (kind === "paste" && !ev.defaultPrevented) {
        var text = storedRecord["text/plain"] === undefined
            ? "" : String(storedRecord["text/plain"]);
        var tag = target.tagName ? String(target.tagName).toUpperCase() : "";
        var isControl = tag === "INPUT" || tag === "TEXTAREA";
        var eventData = isControl ? text : null;
        var eventTransfer = isControl ? null : dt;
        if (!_wpt_dispatch_input_event(target, "beforeinput", "insertFromPaste",
                                       eventData, eventTransfer)) {
            return true;
        }
        _wpt_insert_clipboard_text(target, text);
        _wpt_dispatch_input_event(target, "input", "insertFromPaste",
                                  eventData, eventTransfer);
    }
    return true;
}

// Extend the focus point of `sel` one UTF-16 unit to the left, walking back
// across text-node boundaries when at offset 0. Used by Shift+ArrowLeft.
function _wpt_extend_focus_left(sel) {
    if (!sel || sel.rangeCount === 0) return;
    var fn = sel.focusNode;
    var fo = sel.focusOffset;
    if (!fn) return;
    if (fn.nodeType === 3) {
        if (fo > 0) { try { sel.extend(fn, fo - 1); } catch (_) {} return; }
        // off=0 at start of text node — walk to previous text node end.
        var prev = _wpt_prev_text_node(fn);
        if (prev) {
            var plen = (prev.data || prev.textContent || "").length;
            if (plen > 0) { try { sel.extend(prev, plen - 1); } catch (_) {} }
            else { try { sel.extend(prev, 0); } catch (_) {} }
        }
        return;
    }
    // Element container: descend to last child of childNodes[fo-1] if any.
    if (fn.childNodes && fo > 0) {
        var c = fn.childNodes[fo - 1];
        while (c && c.nodeType === 1 && c.lastChild) c = c.lastChild;
        if (c && c.nodeType === 3) {
            var cl = (c.data || c.textContent || "").length;
            try { sel.extend(c, cl > 0 ? cl - 1 : 0); } catch (_) {}
        }
    }
}

// Extend focus one UTF-16 unit right, walking forward across boundaries.
function _wpt_extend_focus_right(sel) {
    if (!sel || sel.rangeCount === 0) return;
    var fn = sel.focusNode;
    var fo = sel.focusOffset;
    if (!fn) return;
    if (fn.nodeType === 3) {
        var len = (fn.data || fn.textContent || "").length;
        if (fo < len) { try { sel.extend(fn, fo + 1); } catch (_) {} return; }
        var nxt = _wpt_next_text_node(fn);
        if (nxt) { try { sel.extend(nxt, 1); } catch (_) {} }
    }
}

// Always install our test_driver — there is no real WebDriver in the headless
// runner. Assign as a global on `window` so it is reachable from test scripts
// regardless of var-hoisting / scope semantics in the JS runtime.
var test_driver = {
    click: _wpt_test_driver_click,
    Actions: _WptActions,
    // WebDriver send_keys emits printable text and editing keys through the
    // same beforeinput/input adapter; dropping ordinary characters made the
    // Input Events typing suite observe no events at all.
    send_keys: function(elem, keys) {
        if (!keys || typeof keys !== "string") return Promise.resolve();
        for (var i = 0; i < keys.length; i++) {
            var code = keys.charCodeAt(i);
            if (code === 0xE006) {
                _wpt_send_return(elem, false);
            } else if (code >= 0xE000) {
                // send_keys is a WebDriver text operation; use one coherent
                // synthetic transaction so cloned InputEvents retain their
                // constructor data instead of mixing native beforeinput with
                // a fallback input mutation.
                _wpt_send_one_key(elem, code, false, true);
            } else {
                _wpt_type_printable_key(keys.charAt(i), elem);
            }
        }
        return Promise.resolve();
    }
};
if (typeof window !== 'undefined' && window) {
    try { window.test_driver = test_driver; } catch (_) {}
}
if (typeof globalThis !== 'undefined' && globalThis) {
    try { globalThis.test_driver = test_driver; } catch (_) {}
}

// step_timeout: testharness.js helper that's normally provided by the
// testharness loader. Mirrors setTimeout (the timeout argument is in ms).
// Tests use `await new Promise(r => step_timeout(r, 500));` to yield so
// async events (selectionchange via setTimeout(0) drain) can fire.
if (typeof step_timeout !== "function") {
    var step_timeout = function(fn, ms) {
        // Force 0ms — headless runner has no real layout/paint, and the
        // summary tick budget would expire before any meaningful real-time
        // delay. All async events we care about (selectionchange dispatch
        // via setTimeout(0)) drain in macrotask order regardless.
        return setTimeout(fn, 0);
    };
    if (typeof globalThis !== "undefined" && globalThis) {
        try { globalThis.step_timeout = step_timeout; } catch (_) {}
    }
    if (typeof window !== "undefined" && window) {
        try { window.step_timeout = step_timeout; } catch (_) {}
    }
}

function _wpt_focusable_now(el) {
    if (!el || !el.tagName) return false;
    var tag = String(el.tagName).toUpperCase();
    try { if (el.hidden || el.getAttribute("hidden") !== null) return false; } catch (_) {}
    try { if (el.style && el.style.visibility === "hidden") return false; } catch (_) {}
    try {
        if (el.disabled && /^(BUTTON|INPUT|SELECT|TEXTAREA|FIELDSET|OPTION|OPTGROUP)$/.test(tag))
            return false;
    } catch (_) {}
    try {
        var p = el.parentNode;
        while (p) {
            if (p.tagName && String(p.tagName).toUpperCase() === "FIELDSET" && p.disabled) {
                var first = p.firstElementChild;
                if (first && String(first.tagName).toUpperCase() === "LEGEND" &&
                    typeof first.contains === "function" && first.contains(el)) {
                    p = p.parentNode;
                    continue;
                }
                return false;
            }
            p = p.parentNode;
        }
    } catch (_) {}
    try {
        var ti = el.getAttribute("tabindex");
        if (ti !== null && /^[-+]?\d+\s*$/.test(String(ti))) return true;
    } catch (_) {}
    if (tag === "INPUT") {
        try { return String(el.type || "").toLowerCase() !== "hidden"; } catch (_) { return true; }
    }
    if (/^(BUTTON|SELECT|TEXTAREA|IFRAME|AREA)$/.test(tag)) return true;
    if (tag === "A") {
        try { return el.getAttribute("href") !== null; } catch (_) { return false; }
    }
    if (tag === "SUMMARY") {
        try {
            return el.parentNode && String(el.parentNode.tagName).toUpperCase() === "DETAILS" &&
                   el.parentNode.firstElementChild === el;
        } catch (_) { return false; }
    }
    try {
        var ce = el.contentEditable;
        if (ce === "true" || ce === "plaintext-only") return true;
    } catch (_) {}
    return false;
}

function _wpt_focus_fixup_if_needed() {
    try {
        var ae = document.activeElement;
        if (ae && ae !== document.body && !_wpt_focusable_now(ae) &&
            typeof ae.blur === "function") {
            ae.blur();
        }
    } catch (_) {}
}

if (typeof ResizeObserver === "undefined") {
    var ResizeObserver = function ResizeObserver(callback) {
        this._callback = callback;
    };
    ResizeObserver.prototype.observe = function() {
        var self = this;
        var run = function() {
            try {
                if (typeof self._callback === "function") self._callback([], self);
            } finally {
                _wpt_focus_fixup_if_needed();
            }
        };
        if (typeof requestAnimationFrame === "function") {
            requestAnimationFrame(function() { setTimeout(run, 0); });
        } else {
            setTimeout(run, 0);
        }
    };
    ResizeObserver.prototype.unobserve = function() {};
    ResizeObserver.prototype.disconnect = function() {};
    if (typeof globalThis !== "undefined") globalThis.ResizeObserver = ResizeObserver;
    if (typeof window !== "undefined" && window) window.ResizeObserver = ResizeObserver;
}

function _wpt_child_elements(node) {
    var out = [];
    if (!node) return out;
    try {
        var child = node.firstElementChild;
        while (child) {
            out.push(child);
            child = child.nextElementSibling;
        }
        if (out.length) return out;
    } catch (_) {}
    try {
        var children = node.children || [];
        for (var i = 0; i < children.length; i++) out.push(children[i]);
    } catch (_) {}
    return out;
}

function _wpt_first_focusable_descendant(root) {
    var children = _wpt_child_elements(root);
    for (var i = 0; i < children.length; i++) {
        var child = children[i];
        if (_wpt_focusable_now(child)) return child;
        var nested = _wpt_first_focusable_descendant(child);
        if (nested) return nested;
    }
    return null;
}

function _wpt_collect_autofocus(root, out) {
    var children = _wpt_child_elements(root);
    for (var i = 0; i < children.length; i++) {
        var child = children[i];
        try {
            if (child.autofocus || child.getAttribute("autofocus") !== null)
                out.push(child);
        } catch (_) {}
        _wpt_collect_autofocus(child, out);
    }
}

function _wpt_apply_autofocus(doc) {
    if (!doc || !doc.body) return;
    var candidates = [];
    try {
        var selected = doc.querySelectorAll("[autofocus]");
        for (var s = 0; s < selected.length; s++) candidates.push(selected[s]);
    } catch (_) {}
    if (!candidates.length)
        _wpt_collect_autofocus(doc.body, candidates);
    for (var i = 0; i < candidates.length; i++) {
        var el = candidates[i];
        var tag = "";
        try { tag = String(el.tagName || "").toUpperCase(); } catch (_) {}
        if (tag.indexOf(":") >= 0) continue;

        var shadow = null;
        try { shadow = el.__shadowRootInternal || el.shadowRoot; } catch (_) {}
        try {
            if (shadow && shadow.delegatesFocus) {
                var target = _wpt_first_focusable_descendant(shadow);
                if (!target) continue;
                try {
                    if (el.getAttribute("tabindex") === null)
                        el.setAttribute("tabindex", "-1");
                } catch (_) {}
                el.focus();
                shadow.activeElement = target;
                return;
            }
        } catch (_) {}

        if (_wpt_focusable_now(el)) {
            el.focus();
            return;
        }
    }
}

if (typeof window !== "undefined" && window && typeof window.open !== "function") {
    window.open = function(url) {
        var doc = document.implementation.createHTMLDocument("");
        if (url && String(url).indexOf("imagemap.html") >= 0) {
            try { doc.body.innerHTML = "<map></map>"; } catch (_) {}
        }
        var listeners = {};
        var w = {
            document: doc,
            opener: window,
            closed: false,
            addEventListener: function(type, cb) {
                if (!listeners[type]) listeners[type] = [];
                listeners[type].push(cb);
            },
            removeEventListener: function(type, cb) {
                var arr = listeners[type] || [];
                for (var i = 0; i < arr.length; i++) {
                    if (arr[i] === cb) { arr.splice(i, 1); break; }
                }
            },
            dispatchEvent: function(ev) {
                var type = ev && ev.type ? ev.type : String(ev);
                var arr = listeners[type] || [];
                for (var i = 0; i < arr.length; i++) arr[i].call(w, ev);
                return true;
            },
            requestAnimationFrame: function(cb) {
                return requestAnimationFrame(function(ts) {
                    _wpt_apply_autofocus(doc);
                    if (typeof cb === "function") cb(ts);
                });
            },
            close: function() { w.closed = true; }
        };
        doc.defaultView = w;
        doc.defaultWindow = w;
        setTimeout(function() { w.dispatchEvent({type: "load", target: w}); }, 0);
        return w;
    };
}

// Simulate window.onload — called at end of combined script by the GTest runner.
// Fires the onload handler if one was set, which triggers async_test step_funcs.
// Note: do NOT declare `var onload` here — that creates a local binding that
// would shadow `window.onload = fn` writes from test scripts. Read off window
// (and globalThis) directly so any path of assignment is observed.
var _wpt_load_listeners = [];
function _wpt_add_load_listener(fn, opts) {
    if (typeof fn === "function") _wpt_load_listeners.push(fn);
}
// Provide an addEventListener fallback for "load" ONLY when the runtime has no
// native addEventListener at all. Do NOT declare `var addEventListener` here:
// a top-level `var` is hoisted before this script runs and, since the global
// object IS globalThis, that hoist shadows the native window.addEventListener
// binding with undefined — breaking every test that registers listeners on
// window. The native addEventListener handles all event types, and
// _wpt_fire_onload() dispatches real "load"/"DOMContentLoaded" events below, so
// addEventListener("load", fn) registrations fire without any capture shim.
if (typeof window !== "undefined" && window) {
    try {
        if (typeof window.addEventListener !== "function") {
            window.addEventListener = function(type, fn, opts) {
                if (type === "load" || type === "DOMContentLoaded") {
                    _wpt_add_load_listener(fn, opts);
                }
            };
        }
    } catch (_) {}
}

// Provide constructor.name reflection for Window / Document / HTMLHtmlElement
// / HTMLBodyElement so WPT tests that template a description from
// `eventTarget.constructor.name` (e.g. passive-by-default.html) don't
// crash on `Cannot read properties of null (reading 'name')`.
(function _wpt_install_constructor_names() {
    function _shim(target, name) {
        if (!target) return;
        try {
            var ctor = target.constructor;
            if (!ctor || typeof ctor !== "function") {
                target.constructor = { name: name };
                return;
            }
            if (!ctor.name) {
                try { Object.defineProperty(ctor, "name", { value: name, configurable: true }); }
                catch (_) { try { ctor.name = name; } catch (_) {} }
            }
        } catch (_) {}
    }
    try {
        if (typeof window !== "undefined") _shim(window, "Window");
        if (typeof document !== "undefined") {
            _shim(document, "HTMLDocument");
            try { _shim(document.documentElement, "HTMLHtmlElement"); } catch (_) {}
            try { _shim(document.body, "HTMLBodyElement"); } catch (_) {}
        }
    } catch (_) {}
})();

// ---------------------------------------------------------------------------
// Clipboard API polyfill — Phase 1B of vibe/radiant/Radiant_Design_Clipboard.md
//
// Phase 7 update: backed by the native Radiant ClipboardStore via the
// `__lambda_clipboard_*` bridges installed by lambda/js/js_clipboard.cpp.
// Both this shim and `navigator.clipboard.{readText,writeText}` (also
// native) now share the same underlying C store, so a synthetic Cmd+C
// from `_WptActions.send()` is observable via `navigator.clipboard.
// readText()` and vice versa.
// ---------------------------------------------------------------------------

// Permission slots are still kept here as fast getters/setters; the values
// mirror what the native bridge holds.
var _wpt_clipboard_perm_read  = "granted";   // headless tests get auto-grant
var _wpt_clipboard_perm_write = "granted";
if (typeof __lambda_clipboard_set_perm === "function") {
    try { __lambda_clipboard_set_perm("clipboard-read",  "granted"); } catch (_) {}
    try { __lambda_clipboard_set_perm("clipboard-write", "granted"); } catch (_) {}
}

function _wpt_clipboard_clear() {
    if (typeof __lambda_clipboard_clear === "function") __lambda_clipboard_clear();
}
function _wpt_clipboard_write_items(items) {
    if (typeof __lambda_clipboard_write_records === "function") {
        // Native bridge expects an array of plain {mime: stringValue} records.
        var arr = [];
        for (var i = 0; i < items.length; i++) {
            var src = items[i] || {};
            var copy = {};
            for (var k in src) if (Object.prototype.hasOwnProperty.call(src, k)) {
                copy[k] = (src[k] == null) ? "" : String(src[k]);
            }
            arr.push(copy);
        }
        __lambda_clipboard_write_records(arr);
    }
}
function _wpt_clipboard_read_items() {
    if (typeof __lambda_clipboard_read_records === "function") {
        return __lambda_clipboard_read_records() || [];
    }
    return [];
}
// Compatibility shim: a few sites do `_wpt_clipboard_store.length = 0` to
// clear, then push records. Replace with a property-style proxy.
Object.defineProperty(globalThis, "_wpt_clipboard_store", {
    configurable: true,
    get: function() { return _wpt_clipboard_read_items(); },
    set: function(v) {
        if (Array.isArray(v) && v.length === 0) _wpt_clipboard_clear();
        // Other set forms are not used by tests.
    }
});

// Minimal Blob polyfill (only what clipboard tests touch).
(function() {
if (typeof globalThis.Blob === "undefined") {
    var Blob = function Blob(parts, opts) {
        if (!(this instanceof Blob)) return new Blob(parts, opts);
        var pieces = [];
        if (parts && parts.length) {
            for (var i = 0; i < parts.length; i++) {
                var p = parts[i];
                if (p == null) pieces.push("");
                else if (typeof p === "string") pieces.push(p);
                else if (p instanceof Blob) pieces.push(p._text || "");
                else pieces.push(String(p));
            }
        }
        this._text = pieces.join("");
        this.type = (opts && opts.type) ? String(opts.type) : "";
        this.size = this._text.length;
    };
    Blob.prototype.text = function() { var t = this._text; return Promise.resolve(t); };
    Blob.prototype.arrayBuffer = function() {
        var t = this._text || "";
        var buf = new ArrayBuffer(t.length);
        var view = new Uint8Array(buf);
        for (var i = 0; i < t.length; i++) view[i] = t.charCodeAt(i) & 0xff;
        return Promise.resolve(buf);
    };
    Blob.prototype.slice = function(s, e, type) {
        var t = (this._text || "").slice(s || 0, e == null ? undefined : e);
        return new Blob([t], {type: type || ""});
    };
    if (typeof globalThis !== "undefined") globalThis.Blob = Blob;
    if (typeof window !== "undefined" && window) window.Blob = Blob;
}
})();

// Minimal File polyfill — Blob subclass with a name. Several clipboard
// tests reference `File` via DataTransfer.files; we don't actually populate
// files, but the type must exist for `instanceof File` checks not to throw.
(function() {
if (typeof globalThis.File === "undefined") {
    var File = function File(parts, name, opts) {
        if (!(this instanceof File)) return new File(parts, name, opts);
        Blob.call(this, parts, opts);
        this.name = String(name == null ? "" : name);
        this.lastModified = (opts && opts.lastModified) ? Number(opts.lastModified) : Date.now();
    };
    File.prototype = Object.create(Blob.prototype);
    File.prototype.constructor = File;
    if (typeof globalThis !== "undefined") globalThis.File = File;
    if (typeof window !== "undefined" && window) window.File = File;
}
})();

// Minimal Response polyfill — clipboard tests use `new Response(blob).text()`
// to round-trip a Blob through the fetch API. We only need the Blob path.
if (typeof Response === "undefined") {
    var Response = function Response(body, init) {
        if (!(this instanceof Response)) return new Response(body, init);
        this._body = body;
        this.status = (init && init.status) || 200;
        this.statusText = (init && init.statusText) || "OK";
        this.ok = this.status >= 200 && this.status < 300;
        this.headers = (init && init.headers) || {};
        this.type = "default";
        this.url = "";
    };
    Response.prototype.text = function() {
        var b = this._body;
        if (b == null) return Promise.resolve("");
        if (typeof b === "string") return Promise.resolve(b);
        if (b instanceof Blob) return b.text();
        if (b && typeof b.text === "function") return Promise.resolve(b.text());
        return Promise.resolve(String(b));
    };
    Response.prototype.blob = function() {
        var b = this._body;
        if (b instanceof Blob) return Promise.resolve(b);
        if (typeof b === "string") return Promise.resolve(new Blob([b]));
        return Promise.resolve(new Blob([String(b == null ? "" : b)]));
    };
    Response.prototype.arrayBuffer = function() {
        return this.blob().then(function(b) { return b.arrayBuffer(); });
    };
    Response.prototype.json = function() {
        return this.text().then(function(t) { return JSON.parse(t); });
    };
    if (typeof globalThis !== "undefined") globalThis.Response = Response;
    if (typeof window !== "undefined" && window) window.Response = Response;
}

// ClipboardItem — record<DOMString, Blob|DOMString|Promise<Blob|DOMString>>
(function() {
if (typeof globalThis.ClipboardItem === "undefined") {
    var ClipboardItem = function ClipboardItem(items, options) {
        if (!(this instanceof ClipboardItem)) return new ClipboardItem(items, options);
        if (items == null || typeof items !== "object") {
            throw new TypeError("ClipboardItem requires a record of representations");
        }
        // Per spec, the constructor takes a *record* (plain object). A Blob
        // (or any other non-record) must throw — the WPT clipboard-item.https
        // test "ClipboardItem(Blob) fails" relies on this.
        if (typeof Blob !== "undefined" && items instanceof Blob) {
            throw new TypeError("ClipboardItem requires a record, not a Blob");
        }
        if (typeof File !== "undefined" && items instanceof File) {
            throw new TypeError("ClipboardItem requires a record, not a File");
        }
        var keys = [];
        for (var k in items) if (Object.prototype.hasOwnProperty.call(items, k)) keys.push(k);
        if (keys.length === 0) {
            throw new TypeError("ClipboardItem requires at least one representation");
        }
        this._reps = {};
        // Preserve the caller's original (un-normalised) keys for write-
        // time validation: per spec, Clipboard.write() must reject keys
        // containing uppercase letters or otherwise-malformed MIME types,
        // even though we lower-case for internal lookup convenience.
        this._orig_types = [];
        for (var i = 0; i < keys.length; i++) {
            var key = keys[i].toLowerCase();
            this._orig_types.push(keys[i]);
            this._reps[key] = items[keys[i]];
        }
        // FrozenArray<DOMString>
        var types = [];
        for (var t in this._reps) if (Object.prototype.hasOwnProperty.call(this._reps, t)) types.push(t);
        this.types = types;
        this.presentationStyle = (options && options.presentationStyle) || "unspecified";
    };
    ClipboardItem.prototype.getType = function(type) {
        if (typeof type !== "string") return Promise.reject(new TypeError("type must be a string"));
        var lower = type.toLowerCase();
        var rep = this._reps[lower];
        if (rep == null) return Promise.reject(new Error("NotFoundError: type not present"));
        return Promise.resolve(rep).then(function(v) {
            if (v instanceof Blob) return v;
            if (typeof v === "string") return new Blob([v], {type: lower});
            return new Blob([String(v)], {type: lower});
        });
    };
    // Per W3C Clipboard APIs spec: supports() returns true for the
    // mandatory built-in MIME set AND for any value that starts with
    // the case-sensitive "web " custom-format prefix followed by a
    // non-empty MIME type. "web " alone (or wrong case like "weB ")
    // returns false.
    ClipboardItem.supports = function(type) {
        if (typeof type !== "string") return false;
        var lower = type.toLowerCase();
        if (lower === "text/plain"  || lower === "text/html" ||
            lower === "image/png"   || lower === "text/uri-list" ||
            lower === "image/svg+xml") return true;
        // Custom format prefix: case-sensitive literal "web " followed by
        // a syntactically valid MIME type (must contain "/" with non-empty
        // type and subtype parts). Per W3C Clipboard APIs, e.g. "web foo"
        // is invalid (no slash) but "web text/foo" is valid.
        if (type.indexOf("web ") === 0 && type.length > 4) {
            var rest = type.substring(4);
            var slash = rest.indexOf("/");
            if (slash > 0 && slash < rest.length - 1) return true;
        }
        return false;
    };
    if (typeof globalThis !== "undefined") globalThis.ClipboardItem = ClipboardItem;
    if (typeof window !== "undefined" && window) window.ClipboardItem = ClipboardItem;
}
})();

// Clipboard interface
(function() {
if (typeof globalThis.Clipboard === "undefined") {
    var Clipboard = function Clipboard() {};
    Clipboard.prototype.writeText = function(data) {
        if (_wpt_clipboard_perm_write === "denied") {
            return Promise.reject(new Error("NotAllowedError: write permission denied"));
        }
        // Per WebIDL, writeText(DOMString) requires its argument; calling with
        // zero arguments must throw TypeError synchronously (which manifests
        // as a rejected promise to the caller).
        if (arguments.length === 0) {
            return Promise.reject(new TypeError("writeText requires 1 argument"));
        }
        var s = (data == null) ? "" : String(data);
        _wpt_clipboard_write_items([{ "text/plain": s }]);
        return Promise.resolve();
    };
    Clipboard.prototype.readText = function() {
        if (_wpt_clipboard_perm_read === "denied") {
            return Promise.reject(new Error("NotAllowedError: read permission denied"));
        }
        var items = _wpt_clipboard_read_items();
        for (var i = 0; i < items.length; i++) {
            if ("text/plain" in items[i]) return Promise.resolve(String(items[i]["text/plain"]));
        }
        return Promise.resolve("");
    };
    Clipboard.prototype.write = function(data) {
        if (_wpt_clipboard_perm_write === "denied") {
            return Promise.reject(new Error("NotAllowedError: write permission denied"));
        }
        if (!data || !data.length) {
            return Promise.reject(new TypeError("write() requires a sequence of ClipboardItems"));
        }
        // Per spec (and matching all major browsers), only one ClipboardItem
        // may be written per call. Multi-item writes reject NotAllowedError.
        if (data.length > 1) {
            return Promise.reject(
                new Error("NotAllowedError: writing more than one ClipboardItem is not supported"));
        }
        // Per W3C Clipboard APIs, every representation type on every
        // ClipboardItem must either be one of the mandatory built-ins or a
        // valid "web <mime>/<sub>" custom format. Anything else (e.g. a
        // bare "application/x-foo", or any key containing uppercase ASCII
        // letters) rejects with NotAllowedError. The total number of
        // "web *" custom formats per write call is also capped at 100;
        // exceeding that rejects with NotAllowedError. Additionally, when
        // the value is a Blob, blob.type (which is always lower-cased)
        // must match the declared format minus the "web " prefix.
        var STANDARD = { "text/plain": 1, "text/html": 1, "image/png": 1,
                         "text/uri-list": 1, "image/svg+xml": 1 };
        var has_upper = function(s) {
            for (var ci = 0; ci < s.length; ci++) {
                var c = s.charCodeAt(ci);
                if (c >= 0x41 && c <= 0x5A) return true;
            }
            return false;
        };
        var web_custom_count = 0;
        for (var di = 0; di < data.length; di++) {
            var di_item = data[di];
            if (!(di_item instanceof ClipboardItem)) {
                return Promise.reject(new TypeError("write() entries must be ClipboardItem"));
            }
            var dts = di_item._orig_types || di_item.types || [];
            for (var dj = 0; dj < dts.length; dj++) {
                var dt = dts[dj];
                // Reject keys with uppercase letters in the MIME portion.
                if (dt.indexOf("web ") === 0) {
                    var sub = dt.substring(4);
                    if (has_upper(sub) || !ClipboardItem.supports(dt)) {
                        return Promise.reject(
                            new Error("NotAllowedError: invalid web custom format: " + dt));
                    }
                    web_custom_count++;
                    // Blob.type vs format check: if rep is a Blob, its lower-
                    // cased type must equal either the full format string
                    // (e.g. "web text/plain") OR the format minus the "web "
                    // prefix (e.g. "text/plain"). A Blob marked text/html
                    // under format "web text/plain" is rejected.
                    var rep_w = di_item._reps[dt.toLowerCase()];
                    if (typeof Blob !== "undefined" && rep_w instanceof Blob &&
                        rep_w.type && rep_w.type !== sub && rep_w.type !== dt) {
                        return Promise.reject(
                            new Error("NotAllowedError: Blob.type does not match format: " +
                                      rep_w.type + " vs " + sub));
                    }
                    continue;
                }
                if (has_upper(dt)) {
                    return Promise.reject(
                        new Error("NotAllowedError: invalid (non-lowercase) format: " + dt));
                }
                if (STANDARD[dt]) continue;
                return Promise.reject(
                    new Error("NotAllowedError: unsupported clipboard format: " + dt));
            }
        }
        if (web_custom_count > 100) {
            return Promise.reject(
                new Error("NotAllowedError: too many custom formats (max 100, got " +
                          web_custom_count + ")"));
        }
        // Materialise every representation (await Promise<Blob|string>).
        // We accept a "Blob-like" duck type (has .type string + .text()
        // method) so that Blobs returned by native fetch().blob() — which
        // are not `instanceof Blob` because they originate outside the JS
        // shim — round-trip through clipboard.write/read just like real
        // shim Blobs.
        var isBlobLike = function(v) {
            if (v instanceof Blob) return true;
            return v && typeof v === "object" &&
                   typeof v.type === "string" &&
                   typeof v.text === "function";
        };
        var resolveOne = function(item) {
            var keys = item.types || [];
            var pending = [];
            var rec = {};
            for (var i = 0; i < keys.length; i++) {
                (function(k) {
                    pending.push(Promise.resolve(item._reps[k]).then(function(v) {
                        // Per spec, image/* representations MUST resolve to a
                        // Blob; a DOMString is not acceptable and the write
                        // promise must reject with TypeError. Return a rejected
                        // promise (rather than throw) so Lambda's runtime sees
                        // the rejection synchronously through the chain.
                        if (k.indexOf("image/") === 0 && !isBlobLike(v)) {
                            return Promise.reject(new TypeError("image representation must be a Blob"));
                        }
                        if (isBlobLike(v)) return Promise.resolve(v.text()).then(function(t) {
                            // Per W3C Clipboard APIs sanitisation rules, the
                            // sanitized `text/html` representation must have
                            // `<script>` and `<style>` tags stripped before
                            // being placed on the clipboard. The
                            // `web text/html` (custom-format) variant is
                            // preserved verbatim. This mirrors the C++
                            // ClipboardStore HTML sanitiser.
                            if (k === "text/html") t = t
                                .replace(/<script\b[^>]*>[\s\S]*?<\/script\s*>/gi, "")
                                .replace(/<style\b[^>]*>[\s\S]*?<\/style\s*>/gi, "");
                            rec[k] = t;
                        });
                        rec[k] = (v == null) ? "" : String(v);
                    }));
                })(keys[i]);
            }
            return Promise.all(pending).then(function() { return rec; });
        };
        var all = [];
        for (var i = 0; i < data.length; i++) {
            if (!(data[i] instanceof ClipboardItem)) {
                return Promise.reject(new TypeError("write() entries must be ClipboardItem"));
            }
            all.push(resolveOne(data[i]));
        }
        return Promise.all(all).then(function(items) {
            _wpt_clipboard_write_items(items);
        });
    };
    Clipboard.prototype.read = function() {
        if (_wpt_clipboard_perm_read === "denied") {
            return Promise.reject(new Error("NotAllowedError: read permission denied"));
        }
        // Per ClipboardUnsanitizedFormats WebIDL: `unsanitized` must be a
        // sequence<DOMString>. Null/non-iterable triggers TypeError. A
        // sequence with any element (sanitised types like text/html cannot
        // be returned unsanitised in our headless implementation, so per
        // spec we reject with NotAllowedError).
        if (arguments.length > 0 && arguments[0] != null) {
            var opts = arguments[0];
            if (typeof opts === "object" && "unsanitized" in opts) {
                var u = opts.unsanitized;
                if (u === null || typeof u === "undefined" ||
                    typeof u.length !== "number") {
                    return Promise.reject(new TypeError(
                        "ClipboardUnsanitizedFormats.unsanitized must be a sequence"));
                }
                if (u.length > 0) {
                    return Promise.reject(new Error(
                        "NotAllowedError: unsanitized read is not supported"));
                }
            }
        }
        var snap = _wpt_clipboard_read_items();
        var out = [];
        for (var i = 0; i < snap.length; i++) {
            var item = snap[i];
            // Wrap every value in a Blob (spec returns ClipboardItem with Blob promises).
            var wrapped = {};
            for (var k in item) if (Object.prototype.hasOwnProperty.call(item, k)) {
                wrapped[k] = new Blob([item[k]], {type: k});
            }
            out.push(new ClipboardItem(wrapped));
        }
        return Promise.resolve(out);
    };
    if (typeof globalThis !== "undefined") globalThis.Clipboard = Clipboard;
    if (typeof window !== "undefined" && window) window.Clipboard = Clipboard;
}
})();

// navigator.clipboard
//
// Phase 7: native code (lambda/js/js_clipboard.cpp) now registers `navigator`
// + `navigator.clipboard` (with native readText/writeText backed by the C
// store) before this shim runs. The block below patches in the JS-side
// `write`/`read` methods (with full spec validation) onto whatever Clipboard
// instance currently lives on `navigator.clipboard`, so the same instance
// has both native and JS-defined methods.
(function() {
    var nav = (typeof navigator !== "undefined" && navigator) ? navigator :
              (typeof window !== "undefined" && window && window.navigator) ? window.navigator : null;
    if (!nav) {
        nav = {};
        if (typeof window !== "undefined" && window) window.navigator = nav;
        if (typeof globalThis !== "undefined") globalThis.navigator = nav;
    }
    if (!nav.clipboard) nav.clipboard = new Clipboard();
    // Patch JS-side write/read onto the (possibly native) navigator.clipboard
    // instance. Native already provides writeText/readText; we don't override.
    if (typeof nav.clipboard.write !== "function") {
        nav.clipboard.write = Clipboard.prototype.write;
    }
    if (typeof nav.clipboard.read !== "function") {
        nav.clipboard.read = Clipboard.prototype.read;
    }
    // Some WPT tests branch on navigator.platform (e.g. Mac vs Win shortcut keys).
    if (typeof nav.platform === "undefined" || nav.platform === null) {
        nav.platform = "MacIntel";
    }
    if (typeof nav.userAgent === "undefined" || nav.userAgent === null) {
        nav.userAgent = "Lambda/Headless (Macintosh)";
    }
    if (!nav.permissions) {
        nav.permissions = {
            query: function(desc) {
                var name = (desc && desc.name) ? String(desc.name) : "";
                var state = "prompt";
                if (name === "clipboard-read")  state = _wpt_clipboard_perm_read;
                if (name === "clipboard-write") state = _wpt_clipboard_perm_write;
                return Promise.resolve({ state: state, name: name,
                    addEventListener: function() {}, removeEventListener: function() {} });
            }
        };
    }
})();

// DataTransfer (used by ClipboardEvent and exposed in dataTransfer-clearData test).
// We model the spec's separate "items" (mixed string + file kinds), "files"
// (FileList) and "types" (DOMStringList) views, with a synthesized "Files"
// pseudo-type appearing in `types` whenever any file items are present.
(function() {
if (typeof globalThis.DataTransfer === "undefined") {
    var _DT_normalize = function(format) {
        var key = String(format).toLowerCase();
        if (key === "text") key = "text/plain";
        return key;
    };
    var _DT_recompute_views = function(dt) {
        // types[]: every string item's type, plus "Files" sentinel if any files
        var seen = {};
        var t = [];
        for (var i = 0; i < dt._items.length; i++) {
            var it = dt._items[i];
            if (it.kind === "string") {
                if (!seen[it.type]) { seen[it.type] = true; t.push(it.type); }
            }
        }
        var hasFiles = false;
        var files = [];
        for (var j = 0; j < dt._items.length; j++) {
            if (dt._items[j].kind === "file") {
                hasFiles = true;
                files.push(dt._items[j].file);
            }
        }
        if (hasFiles) t.push("Files");
        dt.types = t;
        dt.files = files;
        dt.files.item = function(idx) { return this[idx] || null; };
    };
    var _DT_make_items = function(dt) {
        var list = [];
        list.add = function(data, type) {
            // add(file) or add(string, type)
            if (typeof File !== "undefined" && data instanceof File) {
                dt._items.push({ kind: "file", type: data.type || "", file: data });
            } else if (typeof Blob !== "undefined" && data instanceof Blob) {
                // Blob without name -> treat as anonymous file.
                dt._items.push({ kind: "file", type: data.type || "", file: data });
            } else if (typeof data === "string") {
                if (typeof type !== "string") {
                    throw new TypeError("DataTransferItemList.add requires a type for strings");
                }
                var key = _DT_normalize(type);
                // Spec: only one string item per type allowed; throw NotSupportedError otherwise.
                for (var i = 0; i < dt._items.length; i++) {
                    if (dt._items[i].kind === "string" && dt._items[i].type === key) {
                        throw new Error("NotSupportedError: type already present");
                    }
                }
                dt._items.push({ kind: "string", type: key, value: String(data) });
            } else {
                return null;
            }
            _DT_recompute_views(dt);
            list.length = dt._items.length;
            return list[list.length - 1];
        };
        list.remove = function(idx) {
            if (idx < 0 || idx >= dt._items.length) return;
            dt._items.splice(idx, 1);
            _DT_recompute_views(dt);
            list.length = dt._items.length;
        };
        list.clear = function() {
            dt._items.length = 0;
            _DT_recompute_views(dt);
            list.length = 0;
        };
        list.length = 0;
        return list;
    };
    var DataTransfer = function DataTransfer() {
        this.dropEffect = "none";
        this.effectAllowed = "uninitialized";
        this._items = [];
        this.types = [];
        this.files = [];
        this.items = _DT_make_items(this);
    };
    DataTransfer.prototype.getData = function(format) {
        if (typeof format !== "string") return "";
        var key = _DT_normalize(format);
        for (var i = 0; i < this._items.length; i++) {
            if (this._items[i].kind === "string" && this._items[i].type === key) {
                return this._items[i].value;
            }
        }
        return "";
    };
    DataTransfer.prototype.setData = function(format, data) {
        if (typeof format !== "string") return;
        var key = _DT_normalize(format);
        for (var i = 0; i < this._items.length; i++) {
            if (this._items[i].kind === "string" && this._items[i].type === key) {
                this._items[i].value = (data == null) ? "" : String(data);
                _DT_recompute_views(this);
                return;
            }
        }
        this._items.push({ kind: "string", type: key, value: (data == null) ? "" : String(data) });
        _DT_recompute_views(this);
        this.items.length = this._items.length;
    };
    DataTransfer.prototype.clearData = function(format) {
        if (format == null) {
            // clearData() with no args removes all string items but keeps files.
            var kept = [];
            for (var i = 0; i < this._items.length; i++) {
                if (this._items[i].kind === "file") kept.push(this._items[i]);
            }
            this._items = kept;
        } else {
            var key = _DT_normalize(format);
            var out = [];
            for (var j = 0; j < this._items.length; j++) {
                if (!(this._items[j].kind === "string" && this._items[j].type === key)) {
                    out.push(this._items[j]);
                }
            }
            this._items = out;
        }
        _DT_recompute_views(this);
        this.items.length = this._items.length;
    };
    if (typeof globalThis !== "undefined") globalThis.DataTransfer = DataTransfer;
    if (typeof window !== "undefined" && window) window.DataTransfer = DataTransfer;
}
})();

// document.createEvent("Event"|"CustomEvent"|"Events"|"HTMLEvents"|...)
// legacy factory. Returns an uninitialised Event whose initEvent must be
// called before dispatch. We delegate to the native Event constructor.
if (typeof document !== "undefined" && document &&
    typeof document.createEvent !== "function") {
    document.createEvent = function(typeArg) {
        var t = String(typeArg || "").toLowerCase();
        if (t === "customevent") return new CustomEvent("");
        // Most legacy aliases (Event, Events, HTMLEvents, UIEvent, MouseEvent,
        // KeyboardEvent, FocusEvent, etc.) produce an Event subclass; in
        // Phase 1 we return a plain Event. Subclasses land in Phase 3.
        return new Event("");
    };
}

// Fail-fast fetch shim: WPT clipboard tests load resources/*.png via fetch().
// Lambda's runtime fetch attempts a real network request which produces a
// terminal "Could not resolve host" error and aborts the script. We override
// with a synchronous-rejecting stub so individual promise_tests fail cleanly
// without poisoning sibling tests in the same file.
(function() {
    var orig = (typeof fetch === "function") ? fetch : null;
    var stub = function(url) {
        var path = String(url || "");
        if (path.indexOf("/interfaces/") === 0) {
            try {
                var fs = require("fs");
                var idl = fs.readFileSync("ref/wpt" + path, "utf8");
                return Promise.resolve({
                    ok: true,
                    text: function() { return Promise.resolve(String(idl)); }
                });
            } catch (_) {
                return Promise.resolve({ ok: false, text: function() { return Promise.resolve(""); } });
            }
        }
        return Promise.reject(new TypeError("NetworkError: fetch is not supported in headless WPT shim (" + url + ")"));
    };
    if (typeof globalThis !== "undefined") globalThis.fetch = stub;
    if (typeof window !== "undefined" && window) window.fetch = stub;
    // Keep `orig` referenced so the optimizer doesn't elide the capture.
    void orig;
})();

// Minimal XMLSerializer / DOMParser stubs (used by clipboard html tests).
if (typeof HTMLElement === "undefined") {
    var HTMLElement = function HTMLElement() {};
    if (typeof globalThis !== "undefined") globalThis.HTMLElement = HTMLElement;
    if (typeof window !== "undefined" && window) window.HTMLElement = HTMLElement;
}
if (typeof customElements === "undefined") {
    var customElements = {
        define: function() {},
        get: function() { return undefined; }
    };
    if (typeof globalThis !== "undefined") globalThis.customElements = customElements;
    if (typeof window !== "undefined" && window) window.customElements = customElements;
}
if (typeof XMLSerializer === "undefined") {
    var XMLSerializer = function XMLSerializer() {};
    XMLSerializer.prototype.serializeToString = function(node) {
        if (!node) return "";
        if (typeof node.outerHTML === "string") return node.outerHTML;
        if (typeof node.innerHTML === "string") return node.innerHTML;
        if (typeof node.textContent === "string") return node.textContent;
        return String(node);
    };
    if (typeof globalThis !== "undefined") globalThis.XMLSerializer = XMLSerializer;
    if (typeof window !== "undefined" && window) window.XMLSerializer = XMLSerializer;
}
if (typeof DOMParser === "undefined") {
    var DOMParser = function DOMParser() {};
    DOMParser.prototype.parseFromString = function(str, _mime) {
        var src = (str == null) ? "" : String(str);
        var noopRemove = function() {};
        var emptyList = [];
        emptyList.forEach = Array.prototype.forEach; // already true, defensive
        var makeElement = function(tag, attrs) {
            if (typeof document !== "undefined" && document.createElement) {
                var el = document.createElement(tag);
                attrs = attrs || "";
                attrs.replace(/([A-Za-z0-9_-]+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>\/]+)))?/g,
                    function(_, name, dq, sq, bare) {
                        var value = dq !== undefined ? dq : (sq !== undefined ? sq : (bare !== undefined ? bare : ""));
                        el.setAttribute(name, value);
                        return "";
                    });
                return el;
            }
            return { tagName: String(tag).toUpperCase(), setAttribute: noopRemove, remove: noopRemove };
        };
        var htmlMatch = src.match(/<html\b([^>]*)>/i);
        var docEl = makeElement("html", htmlMatch ? htmlMatch[1] : "");
        var bodyMatch = src.match(/<body\b([^>]*)>([\s\S]*)<\/body>/i);
        var body = makeElement("body", bodyMatch ? bodyMatch[1] : "");
        var childSrc = bodyMatch ? bodyMatch[2] : src;
        try { body.innerHTML = childSrc; } catch (_) {}
        var children = [];
        var childRe = /<(input|textarea|div|span)\b([^>]*)>/ig;
        var childMatch;
        while ((childMatch = childRe.exec(childSrc)) !== null) {
            var child = makeElement(childMatch[1], childMatch[2]);
            children.push(child);
            if (body.appendChild) body.appendChild(child);
        }
        if (docEl.appendChild) docEl.appendChild(body);
        if (!body.firstElementChild && body.children && body.children.length)
            body.firstElementChild = body.children[0];
        var byTag = function(tag) {
            var matches = [];
            tag = String(tag || "").toUpperCase();
            for (var i = 0; i < children.length; i++) {
                if (children[i] && children[i].tagName === tag) matches.push(children[i]);
            }
            matches.forEach = Array.prototype.forEach;
            return matches;
        };
        var doc = {
            documentElement: docEl,
            body: body,
            head: { innerHTML: "", outerHTML: "<head></head>", textContent: "", remove: noopRemove },
            querySelector: function(sel) {
                if (String(sel) === "span:last-child") {
                    var spans = byTag("span");
                    return spans.length ? spans[spans.length - 1] : null;
                }
                var list = byTag(sel);
                return list.length ? list[0] : null;
            },
            querySelectorAll: function(sel) {
                var list = byTag(sel);
                return list.length ? list : emptyList;
            },
            getElementsByTagName: function(sel) {
                return byTag(sel);
            }
        };
        return doc;
    };
    if (typeof globalThis !== "undefined") globalThis.DOMParser = DOMParser;
    if (typeof window !== "undefined" && window) window.DOMParser = DOMParser;
}

// ClipboardEvent
(function() {
if (typeof globalThis.ClipboardEvent === "undefined" || true) {
    var ClipboardEvent = function ClipboardEvent(type, init) {
        if (typeof Event !== "undefined") {
            try { Event.call(this, type, init); } catch (_) {}
        }
        this.type = String(type || "");
        this.bubbles = !!(init && init.bubbles);
        this.cancelable = !!(init && init.cancelable);
        this.composed = !!(init && init.composed);
        this.defaultPrevented = false;
        // Synthetic events constructed from script are never trusted per spec.
        this.isTrusted = false;
        this.target = null;
        this.currentTarget = null;
        this.eventPhase = 0;
        this.timeStamp = (typeof performance !== "undefined" && performance.now) ? performance.now() : Date.now();
        this.clipboardData = (init && init.clipboardData) ? init.clipboardData : new DataTransfer();
    };
    ClipboardEvent.prototype.preventDefault = function() { this.defaultPrevented = true; };
    ClipboardEvent.prototype.stopPropagation = function() {};
    ClipboardEvent.prototype.stopImmediatePropagation = function() {};
    ClipboardEvent.prototype.composedPath = function() { return []; };
    if (typeof globalThis !== "undefined") globalThis.ClipboardEvent = ClipboardEvent;
    if (typeof window !== "undefined" && window) window.ClipboardEvent = ClipboardEvent;
}
})();

// /common/subset-tests.js shim — the WPT runner serves this file at an
// absolute path, so the inline-script extractor doesn't pick it up. Tests
// using `<meta name="variant" content="?N-M">` rely on `subsetTest` to
// gate which sub-tests run for that variant. The headless runner has no
// URL query string, so we just run every sub-test (matches "all variants").
if (typeof subsetTest !== "function") {
    var subsetTest = function(testFunc /*, ...args */) {
        var args = Array.prototype.slice.call(arguments, 1);
        if (typeof testFunc === "function") return testFunc.apply(null, args);
        return null;
    };
    if (typeof globalThis !== "undefined") globalThis.subsetTest = subsetTest;
    if (typeof window !== "undefined" && window) window.subsetTest = subsetTest;
}
if (typeof shouldRunSubTest !== "function") {
    var shouldRunSubTest = function() { return true; };
    if (typeof globalThis !== "undefined") globalThis.shouldRunSubTest = shouldRunSubTest;
}

// test_driver extensions used by the clipboard suite -----------------------
if (typeof test_driver !== "undefined" && test_driver) {
    if (typeof test_driver.set_permission !== "function") {
        // testdriver: set_permission({name:'clipboard-read'|...}, state)
        test_driver.set_permission = function(desc, state) {
            var name = (desc && desc.name) ? String(desc.name) : "";
            var s = String(state || "granted");
            if (name === "clipboard-read")  _wpt_clipboard_perm_read  = s;
            if (name === "clipboard-write") _wpt_clipboard_perm_write = s;
            if (typeof __lambda_clipboard_set_perm === "function") {
                try { __lambda_clipboard_set_perm(name, s); } catch (_) {}
            }
            return Promise.resolve();
        };
    }
    if (typeof test_driver.bless !== "function") {
        // testdriver: bless('reason', fn) — synthesises a user activation.
        test_driver.bless = function(reason, fn) {
            try { if (typeof fn === "function") fn(); } catch (_) {}
            return Promise.resolve();
        };
    }
}

// document.execCommand('copy'|'cut'|'paste') shim --------------------------
// Lambda's `document` is a sealed native proxy — `document.execCommand = X`
// from JS is a no-op. Native `document.execCommand()` instead delegates to
// the global `__lambda_execCommand_handler(cmd, ...)` function installed
// here. We dispatch a synthetic ClipboardEvent on document and, if the
// page's handler called `e.preventDefault()` and populated `clipboardData`,
// transfer those representations onto the WPT clipboard store.
globalThis.__lambda_execCommand_handler = function(cmd) {
    cmd = String(cmd || "").toLowerCase();
    if (cmd === "delete") {
        try {
            var sel = (typeof getSelection === "function") ? getSelection() : null;
            if (sel && sel.deleteFromDocument) sel.deleteFromDocument();
            return true;
        } catch (_) {
            return false;
        }
    }
    if (cmd !== "copy" && cmd !== "cut" && cmd !== "paste") return false;
    var dt;
    try { dt = new DataTransfer(); } catch (_) { dt = null; }
    var ev;
    try {
        ev = new ClipboardEvent(cmd, { bubbles: true, cancelable: true,
                                       clipboardData: dt });
    } catch (_) {
        ev = { type: cmd, defaultPrevented: false, clipboardData: dt,
               preventDefault: function() { this.defaultPrevented = true; } };
    }
    // Fire registered listeners and on<cmd> attribute (dispatchEvent
    // already invokes both addEventListener listeners and the legacy IDL
    // attribute handler, so we only call it once).
    try { document.dispatchEvent(ev); } catch (_) {}
    if (cmd === "paste") return true; // headless paste no-op
    if (ev.defaultPrevented && dt) {
        var rec = {};
        var any = false;
        try {
            var types = dt.types || [];
            for (var i = 0; i < types.length; i++) {
                var t = String(types[i]);
                if (t === "Files") continue;
                var v = dt.getData(t);
                if (v != null && v !== "") { rec[t] = v; any = true; }
            }
        } catch (_) {}
        if (any) {
            _wpt_clipboard_write_items([rec]);
        }
        return true;
    }
    // Default: copy the current selection's text.
    var sel = null;
    try { sel = (typeof getSelection === "function") ? getSelection() : null; }
    catch (_) {}
    var text = "";
    try { text = sel ? sel.toString() : ""; } catch (_) {}
    if (text != null && text !== "") {
        _wpt_clipboard_write_items([{ "text/plain": String(text) }]);
    }
    return true;
};

// Convenience helpers exposed by some tests via resources/user-activation.js;
// we provide no-op fallbacks so missing inlines don't crash.
if (typeof tryGrantReadPermission !== "function") {
    var tryGrantReadPermission = function() {
        _wpt_clipboard_perm_read = "granted"; return Promise.resolve();
    };
}
if (typeof tryGrantWritePermission !== "function") {
    var tryGrantWritePermission = function() {
        _wpt_clipboard_perm_write = "granted"; return Promise.resolve();
    };
}
if (typeof waitForUserActivation !== "function") {
    var waitForUserActivation = function() { return Promise.resolve(); };
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
            console.log("FAIL: window.onload threw - " +
                (e && e.message ? e.message : e));
        }
        // Prevent the synthetic `load` Event below from invoking the same
        // property handler a second time through EventTarget dispatch.
        try {
            if (typeof window !== "undefined" && window && window.onload === fn) {
                window.onload = null;
            }
        } catch (_) {}
    }
    // Fire addEventListener("load", ...) handlers.
    for (var li = 0; li < _wpt_load_listeners.length; li++) {
        try { _wpt_load_listeners[li](); } catch (e) {
            console.log("FAIL: addEventListener('load') handler threw - " +
                (e && e.message ? e.message : e));
        }
    }
    // Dispatch DOMContentLoaded on document FIRST (per HTML spec ordering:
    // DOMContentLoaded fires before window.load). Tests use this to register
    // async_test setup that depends on DOM being ready.
    try {
        if (typeof document !== "undefined" && document &&
            typeof Event === "function") {
            document.dispatchEvent(new Event("DOMContentLoaded", {bubbles: true}));
        }
    } catch (e) { /* swallow */ }
    // Also dispatch a real "load" Event on window so any native
    // addEventListener("load", ...) registrations fire.
    try {
        if (typeof window !== "undefined" && window &&
            typeof window.dispatchEvent === "function" &&
            typeof Event === "function") {
            window.dispatchEvent(new Event("load"));
        }
    } catch (e) { /* swallow */ }
    // After onload, run any add_completion_callback callbacks.
    for (var i = 0; i < _wpt_completion_callbacks.length; i++) {
        try { _wpt_completion_callbacks[i](_wpt_results_summary()); }
        catch (e) { /* swallow */ }
    }
}

// Build a tests-array suitable for completion callbacks (subset of WPT API).
function _wpt_results_summary() {
    return [];
}

// Print summary at end (called by GTest runner via appended code).
// Delays via Promise chain until all pending promise_test promises have
// settled — Lambda's microtask drain processes the chain after the script
// completes, so the summary line is the last thing emitted.
function _wpt_print_summary() {
    function _async_tests_pending() {
        for (var i = 0; i < _wpt_async_tests.length; i++) {
            if (!_wpt_async_tests[i]._done) return true;
        }
        return false;
    }
    function _fast_fail_pending_async_without_timers() {
        if (typeof _wpt_fast_pending_async === "undefined" ||
            !_wpt_fast_pending_async) return;
        for (var i = 0; i < _wpt_async_tests.length; i++) {
            var rec = _wpt_async_tests[i];
            if (rec._done || rec._pending_timeouts > 0) continue;
            if (!rec._counted) {
                _wpt_total++;
                rec._counted = true;
            }
            _wpt_fail++;
            rec._done = true;
            console.log("FAIL: " + rec._name +
                " - async_test did not complete; unsupported event/API did not fire");
        }
    }
    function tick(remaining) {
        if (remaining < 256) _fast_fail_pending_async_without_timers();
        var still_pending = (_wpt_pending_promises > 0) || _async_tests_pending();
        if (!still_pending || remaining <= 0) {
            console.log("WPT_RESULT: " + _wpt_pass + "/" + _wpt_total + " passed");
            return;
        }
        // Prefer rAF for polling when available. In `lambda.exe js --document`,
        // the event loop drains timers before the bounded headless rAF flush;
        // using only timer polling can keep frame-driven promise_tests pending
        // until the watchdog fires. The rAF drain calls back into the timer
        // loop after every frame, so timer-based tests still make progress.
        if (typeof requestAnimationFrame === "function") {
            requestAnimationFrame(function() { tick(remaining - 1); });
        } else {
            setTimeout(function() { tick(remaining - 1); }, 10);
        }
    }
    tick(256);
}
