// Editing a plain contenteditable (F13).
//
// This is the DOM twin of editing.ls: the same edit decisions, applied to a text
// node in the document tree rather than to a text control's flat value. The two
// exist separately because the data models genuinely differ — a value buffer
// with codepoint offsets versus a DOM tree of text nodes and boundaries — not
// because the rules do.
//
// What stays native is the geometry: resolving the transaction's boundaries to a
// single text node (element-offset-to-child, edge-text descent, host
// containment), splicing that node, and the UTF-16 conversion. The waist hands
// this module a resolved `{node, start, end}` in codepoints and takes a
// replacement back.
//
// Scope: single-text-node ranges only. That is not a simplification — it is the
// shape the native DOM edit path has always supported; a range spanning two text
// nodes falls through unclaimed today and still does.
import radiant

pub pn apply_fn(host, evt) {
    let t = evt.input_type;
    // Reserving the composition session changes nothing in the document — the
    // preedit has not been proposed yet. Claiming it is still meaningful: the
    // engine reports a claim through the dispatch verdict as well as through
    // the apply epoch, so "handled, no change" is expressible.
    if (t == "compositionStart") { 'prevent-default' }
    else {
    let node = radiant.dom_edit_node(host);
    // No resolved text node means there is nothing to splice — the caret sits at
    // an element boundary, which for an insertion means one has to be created.
    // That is a different operation, so it has its own primitive; for anything
    // else there is nothing this module can do and native keeps the case.
    if (node == null) {
        if (t == "insertText" or t == "insertReplacementText" or
                t == "insertCompositionText" or t == "insertFromComposition") {
            let data = if (evt.data == null) "" else evt.data;
            // An empty composition replacement at an element boundary has
            // nothing to insert and no text node to put a caret in. It is still
            // this template's transaction, which the verdict channel can now say.
            if (len(data) == 0) { 'prevent-default' }
            else if (radiant.dom_insert_at_boundary(host, data) == null) { 'pass' }
            // The caret lands at the end of what was created. Composition would
            // prefer it at `composition_caret`, but there is no node to address
            // until the insertion exists; native has the same limit here.
            else { 'prevent-default' }
        }
        else { 'pass' }
    }
    else {
        let s = radiant.dom_edit_start(host);
        let e = radiant.dom_edit_end(host);
        // Typing over a selection replaces it; typing at a caret inserts. Both
        // are the same range replacement, which is why they share a branch here
        // exactly as they do in editing.ls.
        if (t == "insertText" or t == "insertReplacementText") {
            let data = if (evt.data == null) "" else evt.data;
            if (radiant.dom_replace_range(host, s, e, data) == null) { 'pass' }
            else { 'prevent-default' }
        }
        // F13.3. A composition edit replaces the range the IME named with the
        // preedit text, and leaves the caret *inside* it: `composition_caret` is
        // where the IME wants the cursor within a run it is still composing, not
        // after it. `dom_replace_range` always parks the caret at the end, so
        // the position is set afterwards through its own primitive — a fifth
        // argument to the splice miscompiles (see §3.15).
        //
        // The unchanged case is not a micro-optimisation. An IME resends the
        // same preedit on every keystroke of a multi-key sequence, so replacing
        // identical text would fire a DOM mutation and a repaint per keystroke
        // for no visible change; placing the caret alone still claims the edit.
        else if (t == "insertCompositionText" or t == "insertFromComposition" or
                 t == "deleteCompositionText") {
            let data = if (evt.data == null) "" else evt.data;
            let caret = if (t == "insertCompositionText" and evt.composition_caret != null)
                            evt.composition_caret else len(data);
            let value = radiant.dom_edit_text(host);
            let unchanged = value != null and slice(value, s, e) == data;
            if (unchanged) {
                if (radiant.dom_set_caret(host, s + caret)) { 'prevent-default' } else { 'pass' }
            }
            else if (radiant.dom_replace_range(host, s, e, data) == null) { 'pass' }
            else {
                radiant.dom_set_caret(host, s + caret)
                'prevent-default'
            }
        }
        // F13.2. A non-collapsed range deletes itself; a collapsed one extends by
        // one **codepoint**, which is where this diverges from the native path
        // it replaces: that decremented a UTF-16 offset, so a backspace over an
        // astral character split the surrogate pair and left half of it behind.
        // editing.ls has always stated the codepoint rule for text controls;
        // the DOM side now agrees.
        else if (t == "deleteContentBackward" or t == "deleteContentForward") {
            let back = t == "deleteContentBackward";
            let value = radiant.dom_edit_text(host);
            let ds = if (s != e) s else if (back and s > 0) s - 1 else s;
            let de = if (s != e) e else if (not back and e < len(value)) e + 1 else e;
            if (ds == de) { 'pass' }
            else if (radiant.dom_replace_range(host, ds, de, "") == null) { 'pass' }
            else { 'prevent-default' }
        }
        else { 'pass' }
    }
    }
}
