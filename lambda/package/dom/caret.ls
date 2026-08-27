// Keyboard caret navigation policy (F9, ES18 scope rule).
//
// Which key with which modifier moves the caret where is policy, and it is
// platform-specific — Alt+Left means a word on macOS, Ctrl+Left means one
// everywhere else. That decision used to live twice in native, once for text
// controls in event.cpp and once for contenteditable in editing_controller.cpp,
// and not merely in two places: in two *vocabularies*, one naming WHATWG
// operations and one passing signed deltas. This is the single rule set.
//
// It binds at <body> because the caret is document-scoped (DocState::sel), not a
// property of any one control — a control-bound template could never have
// reached contenteditable, which is exactly how the split arose.
//
// What stays native is the geometry that resolves a named operation: over the
// live buffer for a text control (form_caret_operation_destination) and over the
// view tree for a rich surface (editing_controller_apply_caret_operation), plus
// the extend-versus-collapse split in both.
import radiant

// Word-wise movement takes Alt or Ctrl. Native took only Alt on text controls
// and only Ctrl on rich surfaces, so each platform's users had it working on
// exactly one of the two. Accepting both is what the merge is for.
fn word_mod(alt, ctrl) { alt or ctrl }

fn horizontal(key, alt, ctrl, meta) {
    let back = key == "ArrowLeft";
    if (word_mod(alt, ctrl)) { if (back) "moveWordBackward" else "moveWordForward" }
    else if (meta) { if (back) "moveLineStart" else "moveLineEnd" }
    else { if (back) "moveCharacterBackward" else "moveCharacterForward" }
}

// Vertical motion is the one place the surfaces genuinely differ, so it is the
// one place the rule branches on them. A single-line <input> has no lines to
// move between and collapses to the ends of its value, which is what Chrome
// does; a rich surface moves by line.
fn vertical(key, surface) {
    let up = key == "ArrowUp";
    if (surface == "rich") { if (up) "moveLineBackward" else "moveLineForward" }
    else { if (up) "moveLineStart" else "moveLineEnd" }
}

// Cmd+Home/End reaches the document boundary on a rich surface; a text control
// has no document to reach, so it stays at the value boundary.
fn boundary(key, surface, meta) {
    let home = key == "Home";
    if (meta and surface == "rich") { if (home) "moveDocumentStart" else "moveDocumentEnd" }
    else { if (home) "moveLineStart" else "moveLineEnd" }
}

// `null` means this key is not caret navigation, and the handler declines so the
// engine's other keydown work — undo, clipboard, the modified deletes — runs.
pub fn operation_for(surface, key, alt, ctrl, meta) {
    if (surface == null) { null }
    else if (key == "ArrowLeft" or key == "ArrowRight") { horizontal(key, alt, ctrl, meta) }
    else if (key == "ArrowUp" or key == "ArrowDown") { vertical(key, surface) }
    else if (key == "Home" or key == "End") { boundary(key, surface, meta) }
    else { null }
}

pub pn navigate(body, evt) {
    let op = operation_for(radiant.caret_surface(body), evt.key, evt.alt, evt.ctrl, evt.meta);
    if (op == null) { 'pass' }
    else { radiant.caret_operation(body, op, evt.shift) }
}
