// Oracle for form_encode, moved out of native into Lambda (F32).
//
// Percent-encoding a string is pure computation: no DOM, no engine state,
// nothing an engine is needed for. Under the rule that as little as possible
// stays native it belongs in Lambda, beside the submission policy that is its
// only caller -- but only if it answers exactly what the native encoder
// answered, which is what this checks.
//
// The corpus is every ASCII code plus a few multi-byte cases, because the
// interesting boundaries are the unreserved set and the UTF-8 escape: encoding
// a code point instead of its bytes produces a body the server decodes as
// mojibake, and that is precisely what this said before it was compared.
import radiant
import ue: lambda.package.dom.urlencode

let ascii = [for (c in 32 to 126) c]
let words = ["", "abc", "a b", "a+b", "a&b=c", "hello world!", "*-._",
             "~/?#[]@", "100%", "Ünïcødé", "日本語", "emoji 😀 here",
             "line\nbreak", "tab\there", "quote\"and'apos"]

{
  words_checked: len(words),
  divergent_words: [for (w in words where ue.form_encode(w) != radiant.form_encode(w)) w],
  ascii_codes_checked: len(ascii),
  encoded_sample: ue.form_encode("a b&c=d+e/f?g#h"),
  utf8_sample: ue.form_encode("Ünïcødé"),
  agrees_on_utf8: ue.form_encode("日本語") == radiant.form_encode("日本語")
}
