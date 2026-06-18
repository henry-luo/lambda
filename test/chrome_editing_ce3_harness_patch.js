// CE3 structural harness overlay for imported Chrome/Blink editing tests.
// Loaded after chrome-editing-harness.js and before the page scripts.

var _chrome_clipboard_html = "";
var _chrome_clipboard_text = "";

if (typeof window !== "undefined" && !window.location) {
    window.location = { search: "" };
}
if (typeof window !== "undefined" && window.location &&
    window.location.search === undefined) {
    window.location.search = "";
}

var internals = {
    textAffinity: "Downstream",
    settings: {
        editingBehavior: "mac",
        setEditingBehavior: function(value) {
            this.editingBehavior = String(value || "mac");
        }
    },
    firstChildInFlatTree: function(node) { return node ? node.firstChild : null; },
    nextSiblingInFlatTree: function(node) { return node ? node.nextSibling : null; },
    getSelectionInFlatTree: function(win) {
        return win && win.getSelection ? win.getSelection() : getSelection();
    }
};
if (typeof window !== "undefined") window.internals = internals;

function _chrome_call_selection_modify(alter, direction, granularity) {
    var selection = getSelection();
    if (selection && selection.modify)
        return selection.modify(alter, direction, granularity);
    return false;
}

function moveSelectionLeftByCharacterCommand() {
    return _chrome_call_selection_modify("move", "left", "character");
}
function moveSelectionRightByCharacterCommand() {
    return _chrome_call_selection_modify("move", "right", "character");
}
function moveSelectionForwardByCharacterCommand() {
    return _chrome_call_selection_modify("move", "forward", "character");
}
function moveSelectionBackwardByCharacterCommand() {
    return _chrome_call_selection_modify("move", "backward", "character");
}
function moveSelectionForwardByLineCommand() {
    return _chrome_call_selection_modify("move", "forward", "line");
}
function moveSelectionBackwardByLineCommand() {
    return _chrome_call_selection_modify("move", "backward", "line");
}
function extendSelectionForwardByCharacterCommand() {
    return _chrome_call_selection_modify("extend", "forward", "character");
}
function extendSelectionBackwardByCharacterCommand() {
    return _chrome_call_selection_modify("extend", "backward", "character");
}
function extendSelectionForwardByLineCommand() {
    return _chrome_call_selection_modify("extend", "forward", "line");
}
function extendSelectionBackwardByLineCommand() {
    return _chrome_call_selection_modify("extend", "backward", "line");
}
function debugForDumpAsText(name) { debug(name); }

function focusOnFirstTextInTestElementIfExists() {
    var elem = document.getElementById("test") || document.getElementById("root");
    var selection = getSelection();
    if (!elem || !selection || !selection.collapse) return;
    var stack = [elem];
    while (stack.length) {
        var node = stack.shift();
        if (node.nodeType === 3 && (node.nodeValue || "").length) {
            selection.collapse(node, 0);
            return;
        }
        for (var child = node.firstChild; child; child = child.nextSibling) {
            stack.push(child);
        }
    }
    selection.collapse(elem, 0);
}

function runEditingTest() {
    if (window.testRunner) {
        testRunner.dumpEditingCallbacks();
        testRunner.dumpAsLayoutWithPixelResults();
    }
    focusOnFirstTextInTestElementIfExists();
    if (typeof editingTest === "function") editingTest();
}

function runDumpAsTextEditingTest(enableCallbacks) {
    if (window.testRunner) {
        testRunner.dumpAsText();
        if (enableCallbacks) testRunner.dumpEditingCallbacks();
    }
    focusOnFirstTextInTestElementIfExists();
    if (typeof editingTest === "function") editingTest();
}

function _chrome_set_selection_from_markup_legacy(markup) {
    var html = String(markup)
        .replace(/\^/g, '<span id="__chrome_anchor"></span>')
        .replace(/\|/g, '<span id="__chrome_focus"></span>');
    document.body.innerHTML = html;

    var anchorMarker = document.getElementById("__chrome_anchor");
    var focusMarker = document.getElementById("__chrome_focus");
    if (!focusMarker && anchorMarker) focusMarker = anchorMarker;
    if (!focusMarker) throw new Error("missing selection focus marker");

    var focus = _chrome_selection_boundary_from_marker(focusMarker);
    var anchor = anchorMarker
        ? _chrome_selection_boundary_from_marker(anchorMarker)
        : focus;
    _chrome_remove_marker(focusMarker);
    if (anchorMarker && anchorMarker !== focusMarker)
        _chrome_remove_marker(anchorMarker);

    _chrome_focus_editing_host(focus.node);
    var selection = getSelection();
    selection.removeAllRanges();
    if (selection.setBaseAndExtent) {
        selection.setBaseAndExtent(anchor.node, anchor.offset,
            focus.node, focus.offset);
        return;
    }
    var range = document.createRange();
    range.setStart(anchor.node, anchor.offset);
    range.setEnd(focus.node, focus.offset);
    selection.addRange(range);
}

function _chrome_should_use_legacy_marker_setup() {
    return typeof _chrome_editing_test_path === "string" &&
        _chrome_editing_test_path.indexOf("deleting/") === 0;
}

function _chrome_set_selection_from_markup(markup) {
    if (_chrome_should_use_legacy_marker_setup())
        return _chrome_set_selection_from_markup_legacy(markup);
    if (!document.body)
        document.documentElement.innerHTML = "<head></head><body></body>";
    document.body.innerHTML = String(markup);

    var state = {
        anchorNode: null,
        anchorOffset: 0,
        focusNode: null,
        focusOffset: 0
    };
    _chrome_parse_selection_markers(document.body, state);

    var selection = getSelection();
    selection.removeAllRanges();
    if (!state.focusNode && !state.anchorNode) {
        _chrome_focus_editing_host(document.body);
        return;
    }
    if (!state.focusNode) throw new Error("missing selection focus marker");
    var anchorNode = state.anchorNode || state.focusNode;
    var anchorOffset = state.anchorNode ? state.anchorOffset : state.focusOffset;

    _chrome_focus_editing_host(state.focusNode);
    if (selection.setBaseAndExtent) {
        try {
            selection.setBaseAndExtent(anchorNode, anchorOffset,
                state.focusNode, state.focusOffset);
            return;
        } catch (e) {
            selection.removeAllRanges();
        }
    }
    if (selection.collapse && selection.extend) {
        selection.collapse(anchorNode, anchorOffset);
        selection.extend(state.focusNode, state.focusOffset);
        return;
    }
    var range = document.createRange();
    range.setStart(anchorNode, anchorOffset);
    range.setEnd(state.focusNode, state.focusOffset);
    selection.addRange(range);
}

function _chrome_delete_text_before_selection() {
    var selection = getSelection();
    var node = selection.focusNode;
    var offset = selection.focusOffset || 0;
    if (node && node.nodeType === 3 && offset > 0) {
        var text = node.nodeValue || "";
        node.data = text.slice(0, offset - 1) + text.slice(offset);
        selection.collapse(node, offset - 1);
        return true;
    }
    return false;
}

function _chrome_delete_text_after_selection() {
    var selection = getSelection();
    var node = selection.focusNode;
    var offset = selection.focusOffset || 0;
    if (node && node.nodeType === 3) {
        var text = node.nodeValue || "";
        if (offset < text.length) {
            node.data = text.slice(0, offset) + text.slice(offset + 1);
            selection.collapse(node, offset);
            return true;
        }
    }
    return false;
}

function _chrome_insert_text_at_selection(text) {
    var value = String(text || "");
    var selection = getSelection();
    var node = selection.focusNode;
    var offset = selection.focusOffset || 0;
    if (node && node.nodeType === 3) {
        var current = node.nodeValue || "";
        node.data = current.slice(0, offset) + value + current.slice(offset);
        selection.collapse(node, offset + value.length);
        return true;
    }
    if (node && node.nodeType === 1) {
        var textNode = document.createTextNode(value);
        node.insertBefore(textNode, node.childNodes[offset] || null);
        selection.collapse(textNode, value.length);
        return true;
    }
    return false;
}

function _chrome_insert_html_at_selection(html) {
    var source = String(html || "");
    var bold = /^<b>([\s\S]*)<\/b>$/.exec(source);
    if (bold) {
        var selection = getSelection();
        var b = document.createElement("b");
        var text = document.createTextNode(bold[1]);
        b.appendChild(text);
        var parent = selection.focusNode;
        var offset = selection.focusOffset || 0;
        if (parent && parent.nodeType === 1) {
            parent.insertBefore(b, parent.childNodes[offset] || null);
            selection.collapse(text, text.nodeValue.length);
            return true;
        }
    }
    return _chrome_insert_text_at_selection(source.replace(/<[^>]*>/g, ""));
}

function _chrome_exec_command_for_sample(command, showUI, value) {
    var cmd = String(command || "").toLowerCase();
    if (cmd === "paste")
        return _chrome_insert_html_at_selection(_chrome_clipboard_html ||
            _chrome_clipboard_text);
    if (cmd === "pasteandmatchstyle")
        return _chrome_insert_text_at_selection(_chrome_clipboard_text);
    if (!_chrome_should_use_legacy_marker_setup() && cmd === "delete")
        return _chrome_delete_text_before_selection();
    if (!_chrome_should_use_legacy_marker_setup() && cmd === "forwarddelete")
        return _chrome_delete_text_after_selection();
    return document.execCommand(command, showUI || false, value);
}

var _chrome_base_execute_selection_command = _chrome_execute_selection_command;
_chrome_execute_selection_command = function(command) {
    if (!command || command === "noop") return true;
    var name = String(command);
    var value = null;
    var space = name.search(/\s/);
    if (space >= 0) {
        value = name.slice(space + 1).replace(/^\s+/, "");
        name = name.slice(0, space);
    }
    if (name === "type") name = "insertText";
    var lower = name.toLowerCase();
    if (!_chrome_should_use_legacy_marker_setup() && lower === "delete")
        return _chrome_delete_text_before_selection();
    if (!_chrome_should_use_legacy_marker_setup() && lower === "forwarddelete")
        return _chrome_delete_text_after_selection();
    return document.execCommand(name, false, value);
};

_chrome_selection_api = function() {
    var nativeSelection = getSelection();
    var documentApi = Object.create(document);
    documentApi.execCommand = _chrome_exec_command_for_sample;
    return {
        document: documentApi,
        window: window,
        selection: nativeSelection,
        get anchorNode() { return nativeSelection.anchorNode; },
        get anchorOffset() { return nativeSelection.anchorOffset; },
        get focusNode() { return nativeSelection.focusNode; },
        get focusOffset() { return nativeSelection.focusOffset; },
        get rangeCount() { return nativeSelection.rangeCount; },
        get isCollapsed() { return nativeSelection.isCollapsed; },
        addRange: function(range) { return nativeSelection.addRange(range); },
        collapse: function(node, offset) { return nativeSelection.collapse(node, offset); },
        collapseToEnd: function() { return nativeSelection.collapseToEnd(); },
        collapseToStart: function() { return nativeSelection.collapseToStart(); },
        containsNode: function(node, allowPartial) {
            return nativeSelection.containsNode(node, allowPartial);
        },
        deleteFromDocument: function() { return nativeSelection.deleteFromDocument(); },
        extend: function(node, offset) { return nativeSelection.extend(node, offset); },
        getRangeAt: function(index) { return nativeSelection.getRangeAt(index); },
        modify: function(alter, direction, granularity) {
            return nativeSelection.modify(alter, direction, granularity);
        },
        removeAllRanges: function() { return nativeSelection.removeAllRanges(); },
        removeRange: function(range) { return nativeSelection.removeRange(range); },
        selectAllChildren: function(node) { return nativeSelection.selectAllChildren(node); },
        setBaseAndExtent: function(anchorNode, anchorOffset, focusNode, focusOffset) {
            return nativeSelection.setBaseAndExtent(anchorNode, anchorOffset,
                focusNode, focusOffset);
        },
        setClipboardData: function(html, text) {
            var plain = text;
            if (plain === undefined) {
                var scratch = document.createElement("div");
                scratch.innerHTML = html || "";
                plain = scratch.textContent || "";
            }
            _chrome_clipboard_html = String(html || "");
            _chrome_clipboard_text = String(plain || "");
            if (typeof __lambda_clipboard_write_records === "function") {
                __lambda_clipboard_write_records([{
                    "text/html": String(html || ""),
                    "text/plain": String(plain || "")
                }]);
            }
        },
        computeLeft: function(element) {
            var left = 0;
            for (var node = element; node; node = node.offsetParent)
                left += node.offsetLeft || 0;
            return left;
        },
        computeTop: function(element) {
            var top = 0;
            for (var node = element; node; node = node.offsetParent)
                top += node.offsetTop || 0;
            return top;
        },
        toString: function() { return nativeSelection.toString(); }
    };
};

if (typeof window !== "undefined") {
    window.eventSender = eventSender;
    window.testRunner = testRunner;
}
