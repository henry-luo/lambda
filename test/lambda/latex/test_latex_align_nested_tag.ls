// D10.2.2v2: captured native trees for nested alignment and equation tags.
parse("\\begin{document}\\begin{align}\\begin{split}x+y\\end{split}\\end{align}\\end{document}","latex")^;
parse("\\begin{document}\\begin{align}x\\tag{1}\\end{align}\\end{document}","latex")^
