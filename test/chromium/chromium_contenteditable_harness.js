// Protocol-only legacy assertion adapter for the Chromium contenteditable
// smoke set. It records assertions and page result text; it never selects an
// edit target, mutates DOM content, or repairs Selection (D7.2.5).
function _chromium_protocol_record(ok, name, detail) {
    _wpt_total++;
    if (ok) {
        _wpt_pass++;
        return;
    }
    _wpt_failure(name || "Chromium assertion", detail || "assertion failed");
}

function description(text) {
    console.log("DESCRIPTION: " + String(text));
}

function shouldBe(expression, expected_expression) {
    try {
        var actual = eval(expression);
        var expected = eval(expected_expression);
        _chromium_protocol_record(actual === expected, expression,
            "got " + String(actual) + ", expected " + String(expected));
    } catch (error) {
        _chromium_protocol_record(false, expression, error);
    }
}

function shouldBeEqualToString(expression, expected) {
    try {
        var actual = eval(expression);
        _chromium_protocol_record(actual === expected, expression,
            "got " + String(actual) + ", expected " + String(expected));
    } catch (error) {
        _chromium_protocol_record(false, expression, error);
    }
}

function _chromium_protocol_collect_page_result() {
    // Imported range probes report their result through ordinary page text.
    // Reading that result is harness protocol, and deliberately has no edit
    // side effect.
    if (_wpt_total !== 0 || typeof document === "undefined") return;
    var console_node = document.getElementById("console");
    if (!console_node) return;
    var result = String(console_node.textContent || "");
    if (result.indexOf("Test Failed.") >= 0) {
        _chromium_protocol_record(false, "page result", result);
    } else if (result.indexOf("Success.") >= 0) {
        _chromium_protocol_record(true, "page result", "");
    } else {
        _chromium_protocol_record(false, "page result", "missing explicit page result");
    }
}

function _wpt_after_onload() {
    _chromium_protocol_collect_page_result();
}
