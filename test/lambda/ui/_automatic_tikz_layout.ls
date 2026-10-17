import tikz: lambda.doc.tikz.tikz

let picture = tikz.render("\\begin{tikzpicture}" ++
  "\\node (a) at (0,0) {Alpha};" ++
  "\\node (b) at (3,0) {Beta};" ++
  "\\draw[-{Latex}] (a) -- (b);\\end{tikzpicture}")^;

<html <body id: "result", <div id: "picture", picture>>>
