// S2.6.1v2/S2.6.4: math atoms retain token boundaries inside array children.
import util: lambda.doc.math.util

let atoms = parse("2xy", {type: "math", flavor: "latex"})^;
[len(atoms), atoms[0]];
[util.content_items(<group value: "attribute", ["a", "b"]>), util.text_of(<group ["a", "b"]>)];
[format(atoms, {type: "math", flavor: "ascii"}), format(atoms, {type: "math", flavor: "latex"})];

let function_word = parse("sin(x)", {type: "math", flavor: "latex"})^;
[format(function_word, {type: "math", flavor: "ascii"}), format(function_word, {type: "math", flavor: "latex"})]
