import checks: .mod_macro_recovery_checks
[for(source in [
 "\\end{a}", "\\begin{a}\\end{b}\\end{a}", "{\\end{a}}",
 "\\begin{a}{\\end{a}}\\end{a}", "\\begin{a}[\\end{a}]\\end{a}",
 "\\count0=0\\begin{a}{\\global\\count0=7}Z\\end{a}\\the\\count0",
 "\\begin{document}\\end{bad}\\end{document}",
 "\\newenvironment{mine}{A}{B}\\begin{a}{\\begin{mine}Y\\end{mine}}\\end{a}",
 "\\newenvironment{mine}[1]{(#1)}{B}\\begin{a}{\\begin{mine}{Q}Y\\end{mine}}\\end{a}",
 "\\newenvironment{mine}[1][D]{(#1)}{B}\\begin{a}{\\begin{mine}[Q]Y\\end{mine}}\\end{a}",
 "\\newenvironment{mine}{A}{B}\\newenvironment{inner}{C}{D}\\begin{a}{\\begin{mine}Y\\begin{inner}Z\\end{inner}W\\end{mine}}\\end{a}",
 "\\newenvironment{mine}{A}{B}\\begin{a}{\\begin{mine}Y\\end{wrong}Z\\end{mine}}\\end{a}",
 "\\newenvironment{mine}{\\global\\count0=7}{B}\\count0=0\\begin{a}{\\begin{mine}Y\\end{mine}}\\end{a}\\the\\count0"
],mode in [false,true]) checks.check(source,false,mode)^];
[for(source in [
 "\\begin{document}\\begin{a}{X}{Y}Z\\end{a}\\end{document}",
 "\\newenvironment{mine}{A}{B}\\begin{document}\\textbf{X\\begin{mine}Y\\end{mine}Z}\\end{document}",
 "\\newenvironment{mine}[1]{(#1)}{B}\\begin{document}\\begin{a}{\\begin{mine}{Q}Y\\end{mine}}\\end{a}\\end{document}",
 "\\newenvironment{mine}[1][D]{(#1)}{B}\\begin{document}\\begin{a}{\\begin{mine}[Q]Y\\end{mine}}\\end{a}\\end{document}",
 "\\newenvironment{mine}{A}{B}\\newenvironment{inner}{C}{D}\\begin{document}\\begin{a}{\\begin{mine}Y\\begin{inner}Z\\end{inner}W\\end{mine}}\\end{a}\\end{document}"
],mode in [false,true]) checks.check(source,true,mode)^]
