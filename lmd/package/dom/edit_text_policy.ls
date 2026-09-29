// Shared pure text policy for form controls and model-edit fallbacks (D7.2.5).

fn is_word_char(c) {
    let o = ord(c);
    (o >= 48 and o <= 57) or (o >= 65 and o <= 90) or
        (o >= 97 and o <= 122) or o == 95 or o >= 128
}

fn scan_back(text, i, want) {
    if (i <= 0) 0
    else if (is_word_char(slice(text, i - 1, i)) == want) scan_back(text, i - 1, want)
    else i
}

fn scan_fwd(text, i, want) {
    if (i >= len(text)) len(text)
    else if (is_word_char(slice(text, i, i + 1)) == want) scan_fwd(text, i + 1, want)
    else i
}

pub fn word_start(text, pos) => scan_back(text, scan_back(text, pos, false), true)
pub fn word_end(text, pos) => scan_fwd(text, scan_fwd(text, pos, false), true)

pub fn line_start(text, pos) {
    if (pos <= 0) 0
    else if (slice(text, pos - 1, pos) == "\n") pos
    else line_start(text, pos - 1)
}

pub fn line_end(text, pos) {
    if (pos >= len(text)) len(text)
    else if (slice(text, pos, pos + 1) == "\n") pos
    else line_end(text, pos + 1)
}

pub fn sanitize(text, multiline) =>
    if (multiline) replace(replace(text, "\r\n", "\n"), "\r", "\n")
    else replace(replace(replace(text, "\r\n", " "), "\n", " "), "\r", " ")

pub fn fit_insertion(text, current_length, replaced_length, limit) {
    if (limit == null) text
    else {
        let available = max(0, limit - (current_length - replaced_length));
        slice(text, 0, min(len(text), available))
    }
}
