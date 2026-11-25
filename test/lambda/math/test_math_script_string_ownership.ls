// S10.6.1v3: rendering must not mutate strings published in the parsed AST.
import math: lambda.doc.math.math
let tree = parse("\\sum_1^2", "math")^;
let name_before = tree[0].base.name;
let layout = math.render_box(tree)^;
[name_before == "sum", len(name_before) == 3, tree[0].base.name == "sum", layout.height > 0]
