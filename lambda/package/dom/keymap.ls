// Keyboard edit-intent policy (F11).
//
// Which key with which modifier means which WHATWG `beforeinput` intent. This
// existed twice in native — a twelve-branch table in editing_intent.cpp for rich
// surfaces and its own branches in event.cpp for text controls — the same
// two-vocabularies duplication F9 collapsed for caret navigation.
//
// The intents themselves have been package policy since F5: they land in
// editing.apply. Only the mapping in front of them was still native. What stays
// native is resolving the name back to a type and filling the payload — reading
// the clipboard for a paste, and declining the paste when there is nothing on it.
import radiant

// Cmd on macOS, Ctrl elsewhere: both are the primary accelerator, and native
// already treated them as one.
fn primary(ctrl, meta) { ctrl or meta }

fn deletion(k, alt, ctrl, meta) {
    let back = k == "backspace";
    if (meta) { if (back) "deleteSoftLineBackward" else "deleteSoftLineForward" }
    else if (alt or ctrl) { if (back) "deleteWordBackward" else "deleteWordForward" }
    else { if (back) "deleteContentBackward" else "deleteContentForward" }
}

// Compared case-insensitively: the engine names a letter key by what it would
// type, so Cmd+Shift+Z arrives as "Z" and Cmd+Z as "z". Matching only the
// lowercase form silently loses every shifted accelerator, redo among them.
pub fn intent_for(key, shift, alt, ctrl, meta) {
    let cmd = primary(ctrl, meta);
    let k = lower(key);
    if (cmd and k == "z") { if (shift) "historyRedo" else "historyUndo" }
    else if (cmd and k == "y") { "historyRedo" }
    else if (cmd and k == "b") { "formatBold" }
    else if (cmd and k == "i") { "formatItalic" }
    else if (cmd and k == "u") { "formatUnderline" }
    else if (cmd and k == "v") { "insertFromPaste" }
    else if (cmd and k == "x") { "deleteByCut" }
    else if (cmd and k == "c") { "copy" }
    else if (cmd and k == "a") { "selectAll" }
    else if (k == "enter") { if (shift) "insertLineBreak" else "insertParagraph" }
    else if (k == "tab") { if (shift) "formatOutdent" else "formatIndent" }
    else if (k == "backspace" or k == "delete") { deletion(k, alt, ctrl, meta) }
    else { null }
}

pub pn resolve(body, evt) {
    let name = intent_for(evt.key, evt.shift, evt.alt, evt.ctrl, evt.meta);
    if (name == null) { 'pass' }
    else { radiant.key_intent(body, name) }
}
