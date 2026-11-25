// S1.8, D10.2.2v2: direct and indirect token-list expansion have distinct rules.
let cases=["\\def\\x{A}\\toks0={\\x}\\edef\\r{\\the\\toks0}\\message{CASE:\\meaning\\r}",
"\\def\\x{A}\\toks0={\\x}\\edef\\r{\\expandafter\\relax\\the\\toks0}\\message{CASE:\\meaning\\r}",
"\\def\\x{A}\\edef\\r{\\expandafter\\relax\\unexpanded{\\x}}\\message{CASE:\\meaning\\r}",
"\\def\\x{A}\\edef\\r{\\unexpanded{\\x}}\\message{CASE:\\meaning\\r}"];
fn show(src) any^ {let r=parse(src,"tex")^; [for (m in r.messages where contains(m,"CASE:")^) m]}
[for(src in cases) show(src)^]
