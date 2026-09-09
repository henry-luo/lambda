// Direct eval inherits caller lexical and private bindings through dynamic
// journals. The private loop crosses the former 256-entry metadata limit.
function bridge_lexical_probe() {
    let bridgeLexical = 42;
    console.log(eval('bridgeLexical'));
}
bridge_lexical_probe();

let private_source = 'class ManyPrivateBindings {';
for (let i = 0; i < 257; i++) {
    private_source += '#privateBinding' + i + ' = ' + i + ';';
}
private_source += 'evaluate() { return eval("#privateBinding256 in this"); }';
private_source += '}';
private_source += 'console.log(new ManyPrivateBindings().evaluate());';
eval(private_source);
