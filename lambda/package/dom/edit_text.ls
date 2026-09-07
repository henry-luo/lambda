// Package-owned text, deletion, composition, and plain-drop policy (D7.2.5).
// All mutations below use the invocation's generic range primitives; command
// aliases and platform input both reach this one family through commands.ls.
import dom
import format: lambda.package.dom.edit_format
import session: lambda.package.dom.edit_session
import structure: lambda.package.dom.edit_structure

fn replacement_value(value) {
    if (value == null) "" else value
}

// A collapsed typing mark is consumed by ordinary text replacement rather
// than by a second styled-insertion path.
pub pn replace_range(host, edit_context, value) {
    let token = edit_context.token;
    let replacement = replacement_value(value);
    let node = dom.edit_node(host, token);
    if (node == null) structure.replace_range(host, token, replacement)
    else {
        let start_offset = dom.edit_start(host, token);
        let end_offset = dom.edit_end(host, token);
        let caret = dom.dom_replace_range(host, token, start_offset, end_offset,
                                          replacement);
        let mark = if (edit_context.collapsed) session.typing_format(host) else null;
        if (caret == null) false
        else if (mark == null or len(replacement) == 0) true
        else if (not format.apply_typing_mark(host, token, node, start_offset,
                                              len(replacement))) false
        else dom.set_caret(host, token, start_offset + len(replacement))
    }
}

// Range deletion and a collapsed one-codepoint deletion share the same range
// waist. Cross-node deletion keeps the generic DOM Range path rather than
// reconstructing the host text in package code.
pub pn delete(host, edit_context, backward) {
    let token = edit_context.token;
    let node = dom.edit_node(host, token);
    if (node == null) structure.replace_range(host, token, "")
    else {
        let start_offset = dom.edit_start(host, token);
        let end_offset = dom.edit_end(host, token);
        let text = dom.dom_edit_text(host, token);
        let delete_start = if (start_offset != end_offset) start_offset
                           else if (backward and start_offset > 0) start_offset - 1 else start_offset;
        let delete_end = if (start_offset != end_offset) end_offset
                         else if (not backward and end_offset < len(text)) end_offset + 1 else end_offset;
        if (delete_start == delete_end) false
        else dom.dom_replace_range(host, token, delete_start, delete_end, "") != null
    }
}

// IME updates own a compatible no-op when the preedit text is unchanged. The
// explicit caret update remains observable without inventing a text mutation.
pub pn composition(host, edit_context, value, requested_caret) {
    let token = edit_context.token;
    let replacement = replacement_value(value);
    let node = dom.edit_node(host, token);
    if (node == null) {
        if (len(replacement) == 0) true
        else dom.edit_insert_at_boundary(host, token, replacement) != null
    }
    else {
        let start_offset = dom.edit_start(host, token);
        let end_offset = dom.edit_end(host, token);
        let caret = if (requested_caret == null) len(replacement)
                    else requested_caret;
        let text = dom.dom_edit_text(host, token);
        let unchanged = text != null and slice(text, start_offset, end_offset) == replacement;
        if (unchanged) dom.set_caret(host, token, start_offset + caret)
        else if (dom.dom_replace_range(host, token, start_offset, end_offset, replacement) == null)
            false
        else dom.set_caret(host, token, start_offset + caret)
    }
}

// A plain drop is ordinary selection replacement. Rich drop transport is
// intentionally left to the shared clipboard/fragment family when supplied.
pub pn insert_drop(host, edit_context, value) {
    let replacement = replacement_value(value);
    if (len(replacement) == 0) false
    else structure.replace_range(host, edit_context.token, replacement)
}
