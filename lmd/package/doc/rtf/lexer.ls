// RTF lexical units retain group boundaries so formatting state can be scoped (D9.3.4).
fn char_at(source, at) any^ => if (at >= len(source)) "" else slice(source, at, at + 1)
fn alpha(c) => c != "" and ((c >= "a" and c <= "z") or (c >= "A" and c <= "Z"))
fn digit(c) => c != "" and c >= "0" and c <= "9"
fn word_end(source, at) any^ => if (alpha(char_at(source, at))) word_end(source, at + 1) else at
fn number_end(source, at) any^ => if (digit(char_at(source, at))) number_end(source, at + 1) else at
// Match the native parser's bounded signed control parameters.
fn number_value(source, at, stop, value = 0) any^ {
    if (at >= stop) value else {
        let d = ord(char_at(source, at)) - ord("0");
        number_value(source, at + 1, stop, if (value > (2147483647 - d) / 10) 2147483647 else value * 10 + d)
    }
}
fn text_end(source, at) any^ {
    let c = char_at(source, at);
    if (c == "" or c == "{" or c == "}" or c == "\\") at
    else text_end(source, at + 1)
}
fn hex(c) any^ => if (c == "") null else index_of("0123456789abcdef", lower(c))
fn control(source, at) any^ {
    let begin = at + 1;
    let c = char_at(source, begin);
    if (alpha(c)) {
        let end = word_end(source, begin);
        let negative = char_at(source, end) == "-";
        let digits = if (negative) end + 1 else end;
        let number_stop = number_end(source, digits);
        let has_number = negative or number_stop > digits;
        let value = if (has_number and number_stop > digits) number_value(source, digits, number_stop) else 0;
        {kind:"control", word:slice(source, begin, end),
            parameter:if (has_number) (if (negative) -value else value) else null,
            end:if (char_at(source, number_stop) == " ") number_stop + 1 else number_stop}
    } else if (c == "'") {
        let hi = hex(char_at(source, begin + 1));
        let lo = hex(char_at(source, begin + 2));
        if (hi != null and lo != null) {kind:"hex", value:hi * 16 + lo, end:begin + 3}
        else {kind:"symbol", value:c, end:begin + 1}
    } else ({kind:"symbol", value:c, end:if (c == "") begin else begin + 1})
}
// Return one token and its source offset; callers can stream large documents.
pub fn next(source, at) any^ {
    let c = char_at(source, at);
    if (c == "") null
    else if (c == "{") {kind:"open", end:at + 1}
    else if (c == "}") {kind:"close", end:at + 1}
    else if (c == "\\") control(source, at)
    else {
        let end = text_end(source, at);
        {kind:"text", value:slice(source, at, end), end:end}
    }
}
