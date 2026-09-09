// JSCU29/JSCU30: old global-binding capacities must not become semantics.
let lexical_source = "";
for (let i = 0; i < 1101; i++) {
    lexical_source += "let lexical_binding_" + i + " = " + i + ";";
}
lexical_source += "console.log(lexical_binding_1100);";
(0, eval)(lexical_source);

let var_source = "";
for (let i = 0; i < 513; i++) {
    var_source += "var module_binding_" + i + " = " + i + ";";
}
var_source += "console.log(module_binding_512);";
(0, eval)(var_source);

setTimeout((...args) => console.log(args.length, args[8]), 0,
    0, 1, 2, 3, 4, 5, 6, 7, 8);
