// The legacy command surface (F14.1, ES20).
//
// `document.execCommand` and the keyboard accelerators are two entry points to
// one command set. They used to be two implementations of very little:
// execCommand was a native bridge that handled exactly `insertHTML`, and the
// formatting intents keymap.ls names — formatBold and its siblings — had no
// applier anywhere at all. Both land here now, which is what makes Cmd+B and
// execCommand('bold') incapable of diverging: the F9/F11 rule, one rule set
// reached through two entry points.
//
// Scope is the subset real editors invoke, not the full deprecated surface. A
// command this module does not implement declines, and `execCommand` reports
// false for it — the same answer `queryCommandSupported` would give.
import dom

// The name a page passes to execCommand, mapped to the WHATWG intent the
// keyboard path already produces. Matching case-insensitively is not
// politeness: execCommand names are defined that way, and editors spell them
// inconsistently ("insertHTML" and "inserthtml" are the same command).
pub fn canonical(name) {
    let n = lower(name);
    if (n == "bold") "formatBold"
    else if (n == "italic") "formatItalic"
    else if (n == "underline") "formatUnderline"
    else if (n == "strikethrough") "formatStrikeThrough"
    else if (n == "inserthtml") "insertHTML"
    else if (n == "inserttext") "insertText"
    else if (n == "delete") "deleteContentBackward"
    else if (n == "forwarddelete") "deleteContentForward"
    else if (n == "insertparagraph") "insertParagraph"
    else if (n == "insertlinebreak") "insertLineBreak"
    else null
}

// Which element an inline formatting command wraps its range in. Null for a
// command that is not a wrap, which is also how `apply` tells the two apart.
fn format_tag(intent) {
    if (intent == "formatBold") "b"
    else if (intent == "formatItalic") "i"
    else if (intent == "formatUnderline") "u"
    else if (intent == "formatStrikeThrough") "s"
    else null
}

pub fn is_format(intent) { format_tag(intent) != null }

// A formatting command toggles: applied to a run that already carries the
// format, it removes it. The state is read off the tree on every call rather
// than cached (ES16), so a page's own DOM edits cannot leave it stale.
pn toggle_format(host, tag) {
    let node = dom.edit_node(host);
    if (node == null) { false }
    else {
        let s = dom.edit_start(host);
        let e = dom.edit_end(host);
        // A collapsed selection has no run to format. A browser remembers the
        // command and applies it to the next keystroke; that is stored state
        // with no channel through this waist, so the command declines rather
        // than reporting a change it did not make.
        if (s == e) { false }
        else if (dom.dom_range_format(host, tag)) {
            dom.dom_unwrap_range(host, s, e, tag)
        }
        else { dom.dom_wrap_range(host, s, e, tag) }
    }
}

// The one applier, reached from `execCommand` and from the keyboard. Named
// `run` rather than `apply` because `apply` is the template-matching keyword.
// Returns
// whether the command actually changed the document.
pub pn run(host, intent, value) {
    let tag = format_tag(intent);
    if (tag != null) { toggle_format(host, tag) }
    else if (intent == "insertHTML") {
        let html = if (value == null) "" else value;
        if (len(html) == 0) { false } else { dom.dom_insert_html(host, html) }
    }
    // insertText is the same range replacement dom_edit.ls performs for typed
    // text, addressed by command name instead of by input type. It is here, not
    // delegated there, because the two entry points differ only in how the
    // payload arrives — and an editor calling execCommand('insertText') expects
    // exactly what typing would have done.
    else if (intent == "insertText") {
        let data = if (value == null) "" else value;
        dom.dom_replace_dom_range(host, data)
    }
    else if (intent == "deleteContentBackward" or
             intent == "deleteContentForward") {
        dom.dom_delete_dom_range(host)
    }
    else if (intent == "insertParagraph") {
        dom.edit_split_block(host)
    }
    else if (intent == "insertLineBreak") {
        dom.edit_insert_break(host)
    }
    else { false }
}

// The `execCommand` entry point. Declining leaves the call reporting false,
// which is what a browser does for a command it does not support.
pub pn exec(host, evt) {
    let intent = canonical(evt.command);
    if (intent == null) { 'pass' }
    else if (run(host, intent, evt.value)) { 'prevent-default' }
    else { 'pass' }
}
