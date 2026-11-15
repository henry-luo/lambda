import checks: .mod_macro_recovery_checks
// constructor mode retains incomplete argument tokens, as the native scanner does.
[for(source in [
 "\\foo{", "\\foo{a", "\\foo{{a", "\\foo{a\\bar", "\\foo{a%comment",
 "\\foo[", "\\foo[a", "\\foo[a}", "\\foo[a}z", "\\foo[a{b}c",
 "\\foo[a{b}c]z", "\\foo{a}{b", "\\def\\m{xyz}\\foo{\\m",
 "\\def\\m#1{X}\\foo{\\m{a", "\\begin{document}\\textbf{a",
 "\\begin{document}\\textbf[a}z"
]) checks.check(source,false,true)^];
// passthrough environment arguments use the same scanner in both modes.
[for(source in ["\\begin{a}{", "\\begin{a}[x}z"],mode in [false,true])
    checks.check(source,false,mode)^]
