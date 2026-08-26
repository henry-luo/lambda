// The IME composition session (F7, ES18).
//
// The session binds to the html body/page rather than to a control, and that is
// the point rather than a convenience: a document has at most one composition
// at a time, so the session's cardinality is the document's. Binding it to the
// control would put it back on FormControlProp — the transient owner whose
// lifetime already cost us the open dropdown (ESO28) and the undo ring (ESO43).
//
// The preedit is not part of any control's value. It is a separate document
// state the renderer draws inline, so the engine still owns the buffer and the
// painting; what lives here is the session policy.
//
// Commit *content* is not handled here — it arrives at the applier as
// `insertFromComposition` and obeys the same newline and maxlength rules as
// typing (F6).
import radiant

// A composition starting while another is still open means the previous one was
// orphaned — the platform can drop a session without ending it. Begin from a
// clean slate rather than appending to a stale preedit.
pub pn begin(body) {
    radiant.clear_ime_preedit(body)
}

pub pn update(body, evt, target) {
    let text = if (evt.data == null) "" else evt.data;
    if (len(text) == 0) {
        radiant.clear_ime_preedit(body)
    }
    else {
        radiant.set_ime_preedit(body, text, evt.composition_caret)
        // The placeholder must go while composing even though the value is
        // still empty — the user is visibly typing into the control, and a
        // placeholder drawn under the preedit reads as garbage.
        if (target != null) { radiant.set_state(target, "placeholder_shown", false) }
        else { false }
    }
}

// End covers both commit and cancel: either way the session is over and the
// preedit stops being drawn. A commit's text lands through the applier.
pub pn end(body) {
    radiant.clear_ime_preedit(body)
}
