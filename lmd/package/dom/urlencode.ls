// application/x-www-form-urlencoded serialization, in Lambda.
//
// This was a native `radiant.form_encode`, but it is pure computation over a
// string: no DOM, no engine state, nothing an engine is needed for. Under the
// rule that as little as possible stays native, it belongs here, beside the
// submission policy in submit.ls that is its only caller.
//
// The charset is HTML's: unreserved characters pass through, a space becomes
// '+', and everything else becomes %XX with uppercase hex. The oracle
// test/lambda/dom_derive_urlencode.ls holds this to the native body's answers.

// `/` on two ints yields a float, and slice wants integer bounds, so the digit
// split is written with floor and an explicit remainder rather than `/` and `%`.
fn hex_digit(n) { let i = int(floor(n)); slice("0123456789ABCDEF", i, i + 1) }
fn hex_high(code) { hex_digit(floor(code / 16)) }
fn hex_low(code) { hex_digit(code - floor(code / 16) * 16) }

// Keep set per the URL "form" serializer: ASCII letters and digits plus * - . _
fn is_unreserved(code) {
    (code >= 48 and code <= 57) or (code >= 65 and code <= 90)
        or (code >= 97 and code <= 122) or code == 42 or code == 45
        or code == 46 or code == 95
}

pub fn form_encode(value) {
    let text = string(value);
    let codes = [for (i in 0 to len(text) - 1) ord(slice(text, i, i + 1))];
    join([for (i in 0 to len(text) - 1) encode_char(slice(text, i, i + 1), codes[i])], "")
}

fn pct(byte) { "%" ++ hex_high(byte) ++ hex_low(byte) }

fn div(a, b) { floor(a / b) }
fn rem(a, b) { a - floor(a / b) * b }

// Percent-encoding escapes *bytes*, not code points, so anything above ASCII is
// encoded as its UTF-8 sequence -- `Ü` is %C3%9C, not %DC. Encoding the code
// point instead is the classic way to produce a body a server decodes as
// mojibake, and it is what this said before the oracle compared it to the
// native encoder.
fn utf8_pct(c) {
    if (c < 128) pct(c)
    else if (c < 2048) pct(192 + div(c, 64)) ++ pct(128 + rem(c, 64))
    else if (c < 65536)
        pct(224 + div(c, 4096)) ++ pct(128 + rem(div(c, 64), 64)) ++ pct(128 + rem(c, 64))
    else pct(240 + div(c, 262144)) ++ pct(128 + rem(div(c, 4096), 64)) ++ pct(128 + rem(div(c, 64), 64)) ++ pct(128 + rem(c, 64))
}

fn encode_char(ch, code) {
    if (is_unreserved(code)) ch
    else if (code == 32) "+"
    else utf8_pct(code)
}
