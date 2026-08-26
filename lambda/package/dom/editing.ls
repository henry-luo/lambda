// Text-control value editing (F5, ES9). The template decides *which range* is
// replaced by *what text*; radiant.replace_range performs the splice and the
// engine keeps the buffer, the mirrors, the caret and the `input` event that
// follows. Every offset here is a codepoint: Lambda's len/slice/ord are all
// codepoint-indexed, so the waist converts to the buffer's bytes and to the
// IDL's UTF-16 on the way in and out.
//
// An intent this file does not handle returns 'pass' and the native applier
// runs unchanged, so the flip can land one input type at a time.
import radiant

// --- word boundaries -------------------------------------------------------

// Same classification the native scanner used: letters, digits and underscore
// are word characters, and so is any non-ASCII codepoint — a full UCD lookup
// would be the correct answer, but this matches what browsers do for
// double-click and word-delete across most scripts.
fn is_word_char(c) {
    let o = ord(c);
    (o >= 48 and o <= 57) or (o >= 65 and o <= 90) or
        (o >= 97 and o <= 122) or o == 95 or o >= 128
}

// Walk left from `i` while the codepoint before it classifies as `want`.
fn scan_back(text, i, want) {
    if (i <= 0) { 0 }
    else if (is_word_char(slice(text, i - 1, i)) == want) { scan_back(text, i - 1, want) }
    else { i }
}

// Walk right from `i` while the codepoint at it classifies as `want`.
fn scan_fwd(text, i, want) {
    if (i >= len(text)) { len(text) }
    else if (is_word_char(slice(text, i, i + 1)) == want) { scan_fwd(text, i + 1, want) }
    else { i }
}

// A word delete consumes the separators beside the caret first, then the word
// itself, so deleting at "abc   |" removes the spaces and "abc" together.
pub fn word_start(text, pos) { scan_back(text, scan_back(text, pos, false), true) }
pub fn word_end(text, pos) { scan_fwd(text, scan_fwd(text, pos, false), true) }

// Line boundaries, for the cmd/ctrl line deletes. A textarea is the only
// control that can hold a newline, so on a single-line input these land on 0
// and len() — which is the whole value, exactly as they should.
pub fn line_start(text, pos) {
    if (pos <= 0) { 0 }
    else if (slice(text, pos - 1, pos) == "\n") { pos }
    else { line_start(text, pos - 1) }
}

pub fn line_end(text, pos) {
    if (pos >= len(text)) { len(text) }
    else if (slice(text, pos, pos + 1) == "\n") { pos }
    else { line_end(text, pos + 1) }
}

// Every delete is the same shape once its boundary is known: a non-empty
// selection is removed wholesale, otherwise the span between the caret and the
// boundary goes. Writing that once beats six near-identical branches.
pn delete_span(elem, s, e, target) {
    if (s != e) {
        radiant.replace_range(elem, s, e, "")
        'prevent-default'
    }
    else if (target < s) {
        radiant.replace_range(elem, target, s, "")
        'prevent-default'
    }
    else if (target > s) {
        radiant.replace_range(elem, s, target, "")
        'prevent-default'
    }
    else { 'pass' }
}

// --- single-line newline policy --------------------------------------------

// A single-line control cannot hold a newline. Native's paste path turns CR and
// LF into *spaces* rather than dropping them, so a two-line paste stays legible
// on one line instead of running words together; the same rule applies to any
// insertion that reaches a single-line control.
pub fn sanitize(text, multiline) {
    if (multiline) {
        // A textarea's value holds only LF (HTML value normalization), so a
        // CRLF and a bare CR both become one LF. Leaving them alone would put
        // a raw CR in the value — which is what this did before, masked only
        // because the native paste path normalized first.
        replace(replace(text, "\r\n", "\n"), "\r", "\n")
    }
    else {
        // CRLF collapses first, so a Windows line ending yields one space
        // rather than two
        replace(replace(replace(text, "\r\n", " "), "\n", " "), "\r", " ")
    }
}

// --- maxlength -------------------------------------------------------------

// `maxlength` caps what *user input* may add (HTML 4.10.5.5). A deletion is
// never blocked by it, and an over-long insertion is truncated to what fits
// rather than refused outright — which is what makes pasting into a nearly-full
// field keep its first characters instead of dropping the lot.
//
// Native enforces this on paste and on whole-value writes but never on typing,
// so this is ground the native applier did not cover: before this, typing eight
// characters into `maxlength="5"` produced eight.
fn max_len(elem) {
    let raw = radiant.attr(elem, "maxlength");
    if (raw == null or raw == "") { null }
    else {
        let n = int(raw);
        if (type(n) == 'error' or n < 0) { null } else { n }
    }
}

// Trim `data` to the budget remaining once the replaced range is gone.
fn fit(elem, data, text_len, sel_len) {
    let m = max_len(elem);
    if (m == null) { data }
    else {
        let budget = m - (text_len - sel_len);
        if (budget <= 0) { "" }
        else if (len(data) <= budget) { data }
        else { slice(data, 0, budget) }
    }
}

// --- commit (change-on-blur) ------------------------------------------------

// Decide whether losing focus commits a changed value, and if so ask the engine
// to fire `change` (HTML 4.10.5.5: fire it when the value differs from what it
// was when the control gained focus).
//
// This runs on the `commit` hook rather than on `blur`, because `change` has to
// precede `blur` and the decision is made before either is dispatched (ESO42).
// The engine fires the event; the template only answers. The snapshot itself
// stays engine-side — value_at_focus reads it.
pub pn commit(elem) {
    let before = radiant.value_at_focus(elem);
    // No snapshot means this is the first blur after init, which is not a
    // commit — treating it as one would fire `change` on every focus pass.
    if (before != null and before != radiant.get_state(elem, "value")) {
        radiant.request_change(elem)
    }
    else { false }
}

// --- the applier -----------------------------------------------------------

// Returns 'prevent-default' when this applier owned the edit — that is the
// signal the native splice stands down on — or 'pass' to leave it to native.
//
// `multiline` comes from the calling template rather than a primitive: the
// package already matches <input> and <textarea> separately, so the element
// pattern that selected this handler is exactly the question being asked.
pub pn apply(elem, evt, multiline) {
    let t = evt.input_type;
    let s = radiant.selection_start(elem);
    let e = radiant.selection_end(elem);
    let text = radiant.get_state(elem, "value");

    // Every intent that means "replace the range with this text" shares one
    // branch: typing, paste, an IME commit, and an autocorrect replacement all
    // differ only in where the text came from, and all owe the same two policy
    // rules — the single-line newline rule and maxlength.
    //
    // Paste and the IME commit reach here through the same
    // dispatch_form_text_replace the keystroke path uses, so claiming them costs
    // nothing extra and keeps `input` firing from the one engine path that
    // drives validation. The preedit session itself stays native (ESO21), as
    // does the readonly/disabled rejection in te_ime_commit_prepare.
    if (t == "insertText" or t == "insertFromPaste" or
            t == "insertFromComposition" or t == "insertReplacementText") {
        // A non-empty selection is replaced rather than inserted beside, which
        // is also what makes typing over a selection collapse it.
        // sanitize before measuring: the newline policy decides what the text
        // *is*, and only then does maxlength decide how much of it fits
        let data = sanitize(if (evt.data == null) "" else evt.data, multiline);
        radiant.replace_range(elem, s, e, fit(elem, data, len(text), e - s))
        'prevent-default'
    }
    else if (t == "deleteByCut") {
        // Cut removes the selection; the clipboard write itself is native.
        delete_span(elem, s, e, s)
    }
    else if (t == "deleteContentBackward") {
        // one codepoint, not one byte and not one UTF-16 unit: backspace over
        // an astral character removes the whole character
        delete_span(elem, s, e, if (s > 0) s - 1 else s)
    }
    else if (t == "deleteContentForward") {
        delete_span(elem, s, e, if (s < len(text)) s + 1 else s)
    }
    else if (t == "deleteWordBackward") {
        delete_span(elem, s, e, word_start(text, s))
    }
    else if (t == "deleteWordForward") {
        delete_span(elem, s, e, word_end(text, s))
    }
    else if (t == "deleteSoftLineBackward" or t == "deleteHardLineBackward") {
        delete_span(elem, s, e, line_start(text, s))
    }
    else if (t == "deleteSoftLineForward" or t == "deleteHardLineForward") {
        delete_span(elem, s, e, line_end(text, s))
    }
    else if (t == "historyUndo" or t == "historyRedo") {
        // ES17: the engine owns the ring and the cursor and hands over the
        // entry to install — value plus the selection as it stood when that
        // state was recorded. Installing it here rather than natively keeps one
        // writer per edit, and routes the restore through the same splice that
        // every other branch uses, so `input`, revalidation and the restyle all
        // follow without native having to remember them.
        //
        // No sanitize and no maxlength: this is a previously accepted state
        // being put back, not new user input, so the policy already ran when it
        // was first entered. The engine suppresses history pushes around this,
        // so the restore does not record itself as a new entry.
        if (evt.history_value == null) { 'pass' }
        else {
            radiant.replace_range(elem, 0, len(text), evt.history_value)
            radiant.set_selection(elem, evt.history_sel_start, evt.history_sel_end)
            'prevent-default'
        }
    }
    else if (t == "insertLineBreak" or t == "insertParagraph") {
        // Enter reaches a form control as `insertParagraph`; only the rich
        // path normalizes it to `insertLineBreak` for plaintext surfaces, and
        // a text control has no paragraphs, so both mean the same newline here.
        //
        // Only a textarea takes that newline into its value. On a single-line
        // control Enter is not a value edit at all — it is form activation —
        // so this declines and leaves that decision native (F4 territory).
        if (multiline) {
            // a newline is a character like any other as far as maxlength goes
            radiant.replace_range(elem, s, e, fit(elem, "\n", len(text), e - s))
            'prevent-default'
        }
        else { 'pass' }
    }
    else { 'pass' }
}
