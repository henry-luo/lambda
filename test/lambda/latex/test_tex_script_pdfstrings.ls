import md5: lambda.doc.latex.tex_engine.md5
import pdf: lambda.doc.latex.tex_engine.pdfstrings

"md5 vectors:"; [for (s in ["", "a", "abc", "message digest", "abcdefghijklmnopqrstuvwxyz",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
    "12345678901234567890123456789012345678901234567890123456789012345678901234567890", "é"]) md5.digest(s)^]
"md5 padding boundaries:"; [for (n in [55, 56, 63, 64, 65, 119, 120])
    md5.digest(join([for (i in 1 to n) "a"], ""))^]
"pdf string escaping:"; pdf.escape_string("a b(c)\\d~ é\t")^
"pdf name escaping:"; pdf.escape_name("a b#c/d(e)<f>[g]{h}%i é!~")^
"pdf hex encoding:"; pdf.escape_hex("a b é")^
"pdf hex decoding:"; [pdf.unescape_hex("414 2zz43 4")^, pdf.unescape_hex("414")^, pdf.unescape_hex("")^]
