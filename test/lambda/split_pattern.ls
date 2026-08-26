// LR09-8 / S9.4.1: `split` follows ECMAScript's String.prototype.split.
// Two defects were fixed here. (1) The pattern path never suspended
// list_push's adjacent-string merging, so every pattern split collapsed into
// one element and the keep-delimiters form returned the input verbatim.
// (2) One cursor served as both the pending-segment start and the search
// resume point, so a zero-length match stepped over a character that then
// appeared in no segment at all.
// Every row below is the value Node reports for the same call.
type digit = \(d)
type digits = \(d+)
type ws = \(s+)
type anydigits = \(d*)

let out = {
    // pattern delimiter — doc/Lambda_Sys_Func.md examples
    p_digit: split("a1b2c3", digit),                    // ["a","b","c",""]
    p_digits: split("a1b22c333", digits),               // ["a","b","c",""]
    p_ws: split("hello   world", ws),                   // ["hello","world"]
    p_nomatch: split("no-match", digit),                // ["no-match"]
    p_keep: split("a1b2c3", digit, true),               // keep delimiters

    // ECMAScript's `e == p` rule: a match ending on the segment start yields
    // no segment, which is why a zero-width pattern produces neither a leading
    // nor a trailing empty (Python's re.split would give ['','a','b','']).
    p_zero_len: split("ab", anydigits),                 // ["a","b"]
    p_zero_len_utf8: split("日本", anydigits),           // whole codepoints

    // an empty subject yields [] when the delimiter matches the empty string,
    // and [""] otherwise
    p_empty_nomatch: split("", digit),                  // [""]
    p_empty_match: split("", anydigits),                // []
    s_empty_subject: split("", ","),                    // [""]
    s_empty_sep: split("abc", ""),                      // ["a","b","c"]

    // the string and pattern paths must agree on every edge
    trailing_str: split("a,b,", ","),
    trailing_pat: split("a1b1", digit),
    leading_str: split(",a,b", ","),
    leading_pat: split("1a1b", digit),
    nomatch_str: split("abc", ","),
    nomatch_pat: split("abc", digit),
    keep_str: split("a,b,c", ",", true)
}
out
