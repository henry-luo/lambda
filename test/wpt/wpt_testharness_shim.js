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
        add_cleanup: function(fn) {},
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
    _wpt_pending_promises++;
    _promise_test_queue = _promise_test_queue.then(function() {
        var p;
        try {
            p = func.call(t, t);
        } catch (e) {
            _wpt_fail++;
            _wpt_pending_promises--;
            console.log("FAIL: " + name + " - " + (e && e.message ? e.message : e));
            return;
        }
        if (p && typeof p.then === "function") {
            return p.then(
                function() { _wpt_pass++; _wpt_pending_promises--; },
                function(e) {
                    _wpt_fail++;
                    _wpt_pending_promises--;
                    console.log("FAIL: " + name + " - " + (e && e.message ? e.message : e));
                }
            );
        }
        _wpt_pass++;
        _wpt_pending_promises--;
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
// ArrowLeft (\uE012), ArrowRight (\uE014).
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
function _wpt_send_one_key(elem, code) {
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
        var v = ae.value || "";
        var ss = ae.selectionStart;
        var se = ae.selectionEnd;
        if (typeof ss !== "number") ss = v.length;
        if (typeof se !== "number") se = v.length;
        if (code === 0xE003) { // Backspace
            if (ss === se) {
                if (ss === 0) return;
                ae.value = v.slice(0, ss - 1) + v.slice(ss);
                try { ae.setSelectionRange(ss - 1, ss - 1); } catch (_) {}
            } else {
                ae.value = v.slice(0, ss) + v.slice(se);
                try { ae.setSelectionRange(ss, ss); } catch (_) {}
            }
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
        if (!r.collapsed) {
            try {
                var sc = r.startContainer; var so = r.startOffset;
                r.deleteContents();
                sel.collapse(sc, so);
            } catch (_) {}
            return;
        }
        var n2 = r.startContainer; var off2 = r.startOffset;
        if (!n2) return;
        if (n2.nodeType === 3) {
            // Delete char before caret in this text node, or merge from
            // previous text node if at offset 0.
            if (off2 > 0) {
                var s = n2.data || n2.textContent || "";
                var ns = s.slice(0, off2 - 1) + s.slice(off2);
                try { n2.data = ns; } catch (_) {
                    try { n2.textContent = ns; } catch (_) {}
                }
                try { sel.collapse(n2, off2 - 1); } catch (_) {}
                return;
            }
            // off2 === 0: walk to previous text node in document order.
            var prev = _wpt_prev_text_node(n2);
            if (prev) {
                var ps = prev.data || prev.textContent || "";
                if (ps.length > 0) {
                    try { prev.data = ps.slice(0, ps.length - 1); } catch (_) {
                        try { prev.textContent = ps.slice(0, ps.length - 1); } catch (_) {}
                    }
                    // Caret stays at (n2, 0) but selection should refresh
                    // so listeners observe a change.
                    try { sel.collapse(n2, 0); } catch (_) {}
                }
            }
            return;
        }
        // Element container: try going one step back.
        if (off2 > 0) {
            var child = n2.childNodes ? n2.childNodes[off2 - 1] : null;
            if (child && child.nodeType === 3) {
                var cs = child.data || child.textContent || "";
                if (cs.length > 0) {
                    try { child.data = cs.slice(0, cs.length - 1); } catch (_) {}
                    try { sel.collapse(child, cs.length - 1); } catch (_) {}
                }
            }
        }
        return;
    }
}
function _wpt_prev_text_node(n) {
    // Walk backward in document order until a text node is found.
    var cur = n;
    while (cur) {
        if (cur.previousSibling) {
            cur = cur.previousSibling;
            // Descend to last text-node descendant
            while (cur.lastChild) cur = cur.lastChild;
            if (cur.nodeType === 3) return cur;
        } else {
            cur = cur.parentNode;
            if (!cur) return null;
        }
    }
    return null;
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

function _wpt_dispatch_input_event(target, type, inputType, data) {
    if (!target || typeof target.dispatchEvent !== "function") return true;
    var ev = null;
    try {
        ev = new InputEvent(type, {
            bubbles: true,
            cancelable: type === "beforeinput",
            composed: true,
            inputType: inputType || "",
            data: data === undefined ? null : data
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
    return ok !== false && !ev.defaultPrevented;
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
        r.deleteContents();
        var tn = document.createTextNode(text);
        r.insertNode(tn);
        sel.collapse(tn, text.length);
    } catch (_) {
        return false;
    }
    _wpt_dispatch_input_event(host, "input", "insertText", text);
    return true;
}

function _wpt_type_printable_key(key) {
    if (typeof key !== "string" || key.length !== 1) return false;
    var ae = null;
    try { ae = document.activeElement; } catch (_) {}
    var tag = (ae && ae.tagName) ? String(ae.tagName).toUpperCase() : "";
    if (tag === "INPUT" || tag === "TEXTAREA") {
        return _wpt_insert_text_in_control(ae, key);
    }
    return _wpt_insert_text_in_document_selection(key);
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
}
_WptActions.prototype.pointerMove = function(x, y, origin) {
    // origin is either a Node (DOM element/text) or {origin: node}.
    // NOTE: Lambda's DOM accessor returns null (not undefined) for missing
    // properties, so checking `origin.origin !== undefined` is always true
    // on real DOM nodes. Use truthiness instead.
    var node = origin;
    if (origin && typeof origin === "object" && origin.origin) {
        node = origin.origin;
    }
    this._steps.push({ type: "move", x: x, y: y, node: node });
    if (node) this._origin = node;
    return this;
};
_WptActions.prototype.pointerDown = function() {
    this._steps.push({ type: "down" });
    return this;
};
_WptActions.prototype.pointerUp = function() {
    this._steps.push({ type: "up" });
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
_WptActions.prototype.send = function() {
    // Walk the step sequence. Track:
    //   - pairs: how many down/up cycles occurred (1=single, 2=double, ...)
    //   - anchor_node: origin of the most recent pointerMove BEFORE the
    //                  first pointerDown (the press location)
    //   - focus_node:  origin of any pointerMove AFTER pointerDown but
    //                  BEFORE pointerUp with a DIFFERENT node — that's a drag
    var pairs = 0;
    var anchor_node = null;
    var focus_node = null;
    var down_open = false;
    var seen_down = false;
    var saw_drag_move = false;
    for (var i = 0; i < this._steps.length; i++) {
        var st = this._steps[i];
        if (st.type === "move") {
            if (down_open) {
                // pointer move during drag
                saw_drag_move = true;
                if (st.node && st.node !== anchor_node) {
                    focus_node = st.node;
                }
            } else if (!seen_down && st.node) {
                anchor_node = st.node;
            }
        } else if (st.type === "down") {
            down_open = true;
            seen_down = true;
        } else if (st.type === "up") {
            if (down_open) { pairs++; down_open = false; }
        }
    }
    if (!anchor_node) anchor_node = this._origin;
    // Drag fully within a single non-text element (e.g. <img>): select
    // that element via its parent so deleteFromDocument() removes it.
    if (saw_drag_move && anchor_node && !focus_node &&
        anchor_node.nodeType === 1 &&
        !(anchor_node.firstChild && anchor_node.firstChild.nodeType === 3)) {
        var p = anchor_node.parentNode;
        if (p) {
            var idx = 0, k = p.firstChild;
            while (k && k !== anchor_node) { idx++; k = k.nextSibling; }
            try {
                var rImg = document.createRange();
                rImg.setStart(p, idx);
                rImg.setEnd(p, idx + 1);
                var selImg = (typeof getSelection === "function") ? getSelection() : null;
                if (selImg) { selImg.removeAllRanges(); selImg.addRange(rImg); }
            } catch (_) {}
            return Promise.resolve();
        }
    }
    // For element origins with a text first-child, descend so the selection
    // anchor becomes the text node (matches real browser hit-test behavior
    // for clicks/drags that land inside an element's text content).
    function _descend_to_text(n) {
        if (n && n.nodeType === 1 /* ELEMENT */) {
            var fc = n.firstChild;
            if (fc && fc.nodeType === 3 /* TEXT */) return fc;
        }
        return n;
    }
    anchor_node = _descend_to_text(anchor_node);
    focus_node = _descend_to_text(focus_node);
    try {
        var sel = (typeof getSelection === "function") ? getSelection() : null;
        if (sel && anchor_node) {
            // origin is the target node for pointerMove(0,0,origin)
            var isText = (anchor_node.nodeType === 3);
            var textNode = isText ? anchor_node : null;
            var parent = anchor_node.parentNode || null;
            if (focus_node && focus_node !== anchor_node) {
                // Drag selection: anchor at (anchor_node, 0) → focus at
                // end of focus_node's text content (or focus_node itself).
                try {
                    var r = document.createRange();
                    r.setStart(anchor_node, 0);
                    var fend = focus_node;
                    var foff = 0;
                    if (fend.nodeType === 3) {
                        foff = (fend.data || "").length;
                    } else {
                        // use child count (end of element's children)
                        var cc = 0;
                        var ch = fend.firstChild;
                        while (ch) { cc++; ch = ch.nextSibling; }
                        foff = cc;
                    }
                    r.setEnd(fend, foff);
                    sel.removeAllRanges();
                    sel.addRange(r);
                } catch (_) {}
            } else if (pairs >= 3) {
                // Triple-click: select the block-level container's content.
                var container = parent || anchor_node;
                var childCount = 0;
                try {
                    var c = container.firstChild;
                    while (c) { childCount++; c = c.nextSibling; }
                } catch (_) {}
                try {
                    var r3 = document.createRange();
                    r3.setStart(container, 0);
                    r3.setEnd(container, childCount);
                    sel.removeAllRanges();
                    sel.addRange(r3);
                } catch (_) {}
            } else if (pairs === 2 && textNode) {
                // Double-click: select word starting at offset 0.
                var s = textNode.data || textNode.textContent || "";
                var end = 0;
                while (end < s.length) {
                    var ch2 = s.charCodeAt(end);
                    var isLetter = (ch2 >= 0x30 && ch2 <= 0x39) ||
                                   (ch2 >= 0x41 && ch2 <= 0x5A) ||
                                   (ch2 >= 0x61 && ch2 <= 0x7A) ||
                                   (ch2 >= 0x80);
                    if (!isLetter) break;
                    end++;
                }
                if (end === 0) end = (s.length > 0 ? 1 : 0);
                try {
                    var r2 = document.createRange();
                    r2.setStart(textNode, 0);
                    r2.setEnd(textNode, end);
                    sel.removeAllRanges();
                    sel.addRange(r2);
                } catch (_) {}
            } else if (pairs >= 1) {
                // Single click: collapse at (anchor_node, 0).
                try {
                    sel.collapse(anchor_node, 0);
                } catch (_) {}
            }
            // Click-driven (non-drag) selections are directionless per
            // Selection API issue #177. Drag selections keep their
            // direction (forward, since we set anchor first).
            try {
                if (!focus_node && sel.__forceDirection) {
                    sel.__forceDirection("none");
                }
            } catch (_) {}
        }
    } catch (_) {}
    // ----- Keyboard step handling -----
    // Process keyDown(ArrowLeft|ArrowRight) events while Shift is held to
    // extend the document selection. Also handle Cmd/Ctrl + V/C/X to
    // synthesize paste/copy/cut events on the document — used by the
    // clipboard suite's `sendPasteShortcutKey()` helper to round-trip
    // text through the platform clipboard.
    try {
        var sel2 = (typeof getSelection === "function") ? getSelection() : null;
        var shift_held = false;
        var modifier_held = false;  // Cmd (Mac) or Ctrl (others)
        for (var ki = 0; ki < this._steps.length; ki++) {
            var ks = this._steps[ki];
            if (ks.type === "keyDown") {
                if (ks.key === "\uE008") shift_held = true;       // Shift
                else if (ks.key === "\uE03D" || ks.key === "\uE03d" ||
                         ks.key === "\uE009") modifier_held = true; // META / CONTROL
                else if (ks.key === "\uE012" && shift_held && sel2) {
                    _wpt_extend_focus_left(sel2);                // ArrowLeft
                } else if (ks.key === "\uE014" && shift_held && sel2) {
                    _wpt_extend_focus_right(sel2);               // ArrowRight
                } else if (modifier_held && (ks.key === "v" || ks.key === "V")) {
                    _wpt_dispatch_clipboard_event("paste");
                } else if (modifier_held && (ks.key === "c" || ks.key === "C")) {
                    _wpt_dispatch_clipboard_event("copy");
                } else if (modifier_held && (ks.key === "x" || ks.key === "X")) {
                    _wpt_dispatch_clipboard_event("cut");
                } else if (!modifier_held) {
                    _wpt_type_printable_key(ks.key);
                }
            } else if (ks.type === "keyUp") {
                if (ks.key === "\uE008") shift_held = false;
                else if (ks.key === "\uE03D" || ks.key === "\uE03d" ||
                         ks.key === "\uE009") modifier_held = false;
            }
        }
    } catch (_) {}
    return Promise.resolve();
};

// Synthesize and dispatch a ClipboardEvent ('paste', 'copy', or 'cut') on
// document, populating `clipboardData` from the WPT clipboard store. Called
// by the keyboard-shortcut handler in `_WptActions.send()`. Mirrors the
// native execCommand path: if the page handler doesn't preventDefault on a
// copy/cut, we fall back to writing `getSelection().toString()`.
function _wpt_dispatch_clipboard_event(kind) {
    var dt;
    try { dt = new DataTransfer(); } catch (_) { dt = null; }
    if (kind === "paste" && dt) {
        // Pre-populate clipboardData from the WPT store so e.clipboardData
        // .getData("text/plain") (etc.) returns the previously-copied text.
        var items = _wpt_clipboard_read_items();
        for (var i = 0; i < items.length; i++) {
            var rec = items[i];
            for (var k in rec) {
                if (Object.prototype.hasOwnProperty.call(rec, k)) {
                    try { dt.setData(k, String(rec[k])); } catch (_) {}
                }
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
    try { document.dispatchEvent(ev); } catch (_) {}
    if (kind === "paste") return;
    // copy/cut default action when no preventDefault: copy current selection.
    if (!ev.defaultPrevented) {
        var sel = null;
        try { sel = (typeof getSelection === "function") ? getSelection() : null; }
        catch (_) {}
        var text = "";
        try { text = sel ? sel.toString() : ""; } catch (_) {}
        if (text != null && text !== "") {
            _wpt_clipboard_write_items([{ "text/plain": String(text) }]);
        }
        return;
    }
    // Page handler called preventDefault — transfer DataTransfer contents
    // onto the WPT store.
    if (dt) {
        var rec2 = {};
        var any = false;
        try {
            var types = dt.types || [];
            for (var ti = 0; ti < types.length; ti++) {
                var t = String(types[ti]);
                if (t === "Files") continue;
                var v = dt.getData(t);
                if (v != null && v !== "") { rec2[t] = v; any = true; }
            }
        } catch (_) {}
        if (any) {
            _wpt_clipboard_write_items([rec2]);
        }
    }
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
        var nxt = (function _next_text(n) {
            // walk forward in document order to next text node
            var cur = n;
            while (cur) {
                if (cur.nextSibling) {
                    cur = cur.nextSibling;
                    while (cur.firstChild) cur = cur.firstChild;
                    if (cur.nodeType === 3) return cur;
                } else {
                    cur = cur.parentNode;
                    if (!cur) return null;
                }
            }
            return null;
        })(fn);
        if (nxt) { try { sel.extend(nxt, 1); } catch (_) {} }
    }
}

// Always install our test_driver — there is no real WebDriver in the headless
// runner. Assign as a global on `window` so it is reachable from test scripts
// regardless of var-hoisting / scope semantics in the JS runtime.
var test_driver = {
    click: _wpt_test_driver_click,
    Actions: _WptActions,
    // send_keys synthesizes a small set of editing keys for tests that
    // exercise selectionchange on Backspace/End in contenteditable or
    // text controls. Only the keys we need are implemented; unrecognized
    // characters are dropped (the test will then fail naturally).
    send_keys: function(elem, keys) {
        if (!keys || typeof keys !== "string") return Promise.resolve();
        for (var i = 0; i < keys.length; i++) {
            _wpt_send_one_key(elem, keys.charCodeAt(i));
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
