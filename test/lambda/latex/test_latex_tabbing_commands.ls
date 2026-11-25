// D10.2.2v2: top-level tabbing commands do not become text accents.
let source="\\begin{document}\\begin{tabbing}\nab \\= cd \\\\\n\\> x \\\\\nz \\kill\n\\end{tabbing}\\end{document}";
parse(source,"latex")^;
let nested="\\begin{document}\\begin{tabbing}a\\= b {\\`e} c \\'d\\end{tabbing}\\end{document}"
parse(nested,"latex")^
