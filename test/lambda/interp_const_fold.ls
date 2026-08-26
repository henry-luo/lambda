// the P3 CONST pass folds pure literal scalar subtrees through the AST walker.
[
  (20 + 1) * 2,
  7 > 2,
  not (5 == 3),
  if (false) 99 else (6 + 7)
]
