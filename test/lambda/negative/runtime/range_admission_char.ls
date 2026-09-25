// S11.1.3: a character range admits one-codepoint strings in its interval.
fn dyn(v) => v
fn c(ch: "a" to "e") { ch }
"bound: " ++ string(c(dyn("z")))
