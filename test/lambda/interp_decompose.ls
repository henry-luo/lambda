// Regression: a lexical let block remains AST_NODE_LIST at module scope.

"=== lexical positional decomposition ===";
(let a, b = [3, 4], a + b)

"=== lexical named decomposition ===";
(let width, height at {width: 7, height: 6}, width * height)
