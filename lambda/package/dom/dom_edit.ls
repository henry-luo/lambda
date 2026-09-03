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
// Ordinary text keeps the single-node fast path below. Structural commands and
// a range that spans nodes use the raw range waist, which owns the tree surgery.
import dom
import commands: lambda.package.dom.commands

pub pn apply_fn(host, evt) {
    let t = evt.input_type;
    // Reserving the composition session changes nothing in the document — the
    // preedit has not been proposed yet. Claiming it is still meaningful: the
    // engine reports a claim through the dispatch verdict as well as through
    // the apply epoch, so "handled, no change" is expressible.
    if (t == "compositionStart") { 'prevent-default' }
    else {
    // F14.2. Paragraph and line-break commands are structural even when the
    // selection currently resolves to one text node, so send both keyboard and
    // execCommand paths through one package applier.
    if (t == "insertParagraph" or t == "insertLineBreak") {
        if (commands.run(host, t, evt.data)) { 'prevent-default' } else { 'pass' }
    }
    else {
    let node = dom.edit_node(host);
    // No resolved text node means there is nothing to splice — the caret sits at
    // an element boundary, which for an insertion means one has to be created.
    // That is a different operation, so it has its own primitive; for anything
    // else there is nothing this module can do and native keeps the case.
    if (node == null) {
        if (t == "insertText" or t == "insertReplacementText" or
                t == "insertCompositionText" or t == "insertFromComposition") {
            let data = if (evt.data == null) "" else evt.data;
            // An empty composition replacement at an element boundary has
            // nothing to insert and no text node to put a caret in. The package
            // still claims it through the verdict channel.
            if (len(data) == 0) { 'prevent-default' }
            else if ((t == "insertText" or t == "insertReplacementText") and
                     dom.dom_replace_dom_range(host, data)) {
                'prevent-default'
            }
            else if (t == "insertText" or t == "insertReplacementText") { 'pass' }
            else if (dom.edit_insert_at_boundary(host, data) == null) { 'pass' }
            // The caret lands at the end of what was created. Composition would
            // prefer it at `composition_caret`, but there is no node to address
            // until the insertion exists; native has the same limit here.
            else { 'prevent-default' }
        }
        else if (t == "deleteContentBackward" or t == "deleteContentForward") {
            if (dom.dom_delete_dom_range(host)) { 'prevent-default' } else { 'pass' }
        }
        // Clipboard and text-drag payloads are already native-provided plain
        // text. Their policy is only to replace the selected DOM range; rich
        // HTML remains available to an author through DataTransfer rather than
        // being parsed a second time by this default applier.
        else if (t == "insertFromPaste" or t == "insertFromDrop") {
            let data = if (evt.data == null) "" else evt.data;
            if (len(data) == 0) { 'pass' }
            else if (dom.dom_replace_dom_range(host, data)) { 'prevent-default' }
            else { 'pass' }
        }
        else if (t == "deleteByCut" or t == "deleteByDrag") {
            if (dom.dom_delete_dom_range(host)) { 'prevent-default' } else { 'pass' }
        }
        else { 'pass' }
    }
    else {
        let s = dom.edit_start(host);
        let e = dom.edit_end(host);
        // Typing over a selection replaces it; typing at a caret inserts. Both
        // are the same range replacement, which is why they share a branch here
        // exactly as they do in editing.ls.
        if (t == "insertText" or t == "insertReplacementText") {
            let data = if (evt.data == null) "" else evt.data;
            if (dom.dom_replace_range(host, s, e, data) == null) { 'pass' }
            else { 'prevent-default' }
        }
        // The raw range waist deliberately covers cross-text-node clipboard
        // and drag selections; the single-node splice above is only a fast
        // path for ordinary typing. Empty means an HTML-only transfer, which
        // this plain-text default declines for an author DataTransfer handler.
        else if (t == "insertFromPaste" or t == "insertFromDrop") {
            let data = if (evt.data == null) "" else evt.data;
            if (len(data) == 0) { 'pass' }
            else if (dom.dom_replace_dom_range(host, data)) { 'prevent-default' }
            else { 'pass' }
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
            let value = dom.dom_edit_text(host);
            let unchanged = value != null and slice(value, s, e) == data;
            if (unchanged) {
                if (dom.set_caret(host, s + caret)) { 'prevent-default' } else { 'pass' }
            }
            else if (dom.dom_replace_range(host, s, e, data) == null) { 'pass' }
            else {
                dom.set_caret(host, s + caret)
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
            let value = dom.dom_edit_text(host);
            let ds = if (s != e) s else if (back and s > 0) s - 1 else s;
            let de = if (s != e) e else if (not back and e < len(value)) e + 1 else e;
            if (ds == de) { 'pass' }
            else if (dom.dom_replace_range(host, ds, de, "") == null) { 'pass' }
            else { 'prevent-default' }
        }
        // Cut and move-drag delete exactly the range geometry that native
        // captured before dispatch. They differ only in who put the copied
        // text on the clipboard, so sharing the raw delete avoids a second
        // tree-surgery policy branch.
        else if (t == "deleteByCut" or t == "deleteByDrag") {
            if (dom.dom_delete_dom_range(host)) { 'prevent-default' } else { 'pass' }
        }
        // F14.1. The formatting intents arrive here on the keyboard path — the
        // ordinary edit dispatch sends them to `domedit` exactly as it does for
        // an insert — and reaches the same applier `execCommand` does. Delegating
        // rather than reimplementing is the whole point: Cmd+B and
        // execCommand('bold') cannot drift if there is only one of them.
        else if (commands.is_format(t)) {
            if (commands.run(host, t, evt.data)) { 'prevent-default' } else { 'pass' }
        }
        else { 'pass' }
    }
    }
    }
}
