// Editing a plain contenteditable (F13).
//
// This is the DOM twin of editing.ls: the same edit decisions, applied to a text
// node in the document tree rather than to a text control's flat value. The two
// exist separately because the data models genuinely differ — a value buffer
// with codepoint offsets versus a DOM tree of text nodes and boundaries — not
// because the rules do.
//
// What stays native is the geometry: resolving the edit's boundaries to a
// single text node (element-offset-to-child, edge-text descent, host
// containment), splicing that node, and the UTF-16 conversion. The waist hands
// this module a resolved `{node, start, end}` in codepoints and takes a
// replacement back.
//
// The descriptor registry owns every input family. It validates one context
// and dispatches one package plan for keyboard, IME, clipboard, and drag input.
import dom
import commands: lambda.dom.commands

pub pn apply_fn(host, evt) {
    let target = if (evt.edit_token == null or evt.edit_token == 0) null
                 else dom.edit_target(host, evt.edit_token);
    let edit_result = if (target == null)
        { claimed: false }
    else commands.execute(target, evt, evt.input_type, evt.data);
    commands.verdict(edit_result)
}
