// LR09-33: an array of numeric arrays is packed as an N-D array, whose items
// are its leading-axis rows (S11.1.1v3). `++` (S10.6.1) and spread (S12.3.5v2)
// joined its flat leaves instead: `[[1.0, 2.0]] ++ [[3.0]]` was `[1, 2, 3]`,
// and `[*m]` padded the rows with one null per leaf.

let lit = [[0.0, 1.0], [2.0, 3.0]];
let ints = [[0, 1], [2, 3]];
let built = [for (x in [1.0, 2.0]) [x, x]];

"float rows:"; lit ++ lit
"int rows:"; ints ++ ints
"comprehension rows:"; built ++ [[3.0, 4.0], [5.0, 6.0]]
"row count:"; len(built ++ [[3.0, 4.0], [5.0, 6.0]])
"ragged:"; [[1.0, 2.0]] ++ [[3.0]]
"scalar first:"; [9.0] ++ lit
"scalar last:"; lit ++ [9.0]
"generic rows:"; lit ++ [[1, "x"]]
"empty and null:"; [[] ++ lit, lit ++ null]
"equals literal:"; (lit ++ lit) == [[0.0, 1.0], [2.0, 3.0], [0.0, 1.0], [2.0, 3.0]]
"keeps rank:"; (lit ++ lit) is float[][]

"spread:"; [*lit, *lit]
"spread ints:"; [*ints]
"spread comprehension:"; [for (r in [lit, ints]) *r]
"spread content:"; <row *lit>

let cube = [[[1, 2], [3, 4]], [[5, 6], [7, 8]]];
"3-d join:"; cube ++ cube
"3-d spread:"; [*cube]
let m = [[1, 2, 3], [4, 5, 6]];
"transposed spread:"; [*transpose(m)]
"transposed join:"; transpose(m) ++ m[0 to 0]

"unique:"; unique(lit ++ lit)
"except:"; except(lit ++ [[4.0, 5.0]], lit)
