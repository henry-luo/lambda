import tikz: lambda.doc.tikz.tikz
let result = tikz.render("\\begin{tikzpicture}\\node[fill=blue,minimum width=200px,minimum height=200px] (a) at (0,0) {A};\\end{tikzpicture}") ^ { ^.message };
result
