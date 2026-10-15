// S12.1.1v3: procedural preparation precedes pure element construction.
pn load_parts() { return "prepared bytes\n" }
fn pure_parts() => "pure bytes"
function build(f: function) => <doc f()^>

pn main() {
    let parts = load_parts();
    let file = <file format:'pdf', parts>;
    let written = output(file, "temp/pdf_prepared_parts.bin", {atomic: true})^;
    let observed = input("temp/pdf_prepared_parts.bin", 'text')^;
    let erased: any = pure_parts;
    let child = <doc erased()^>;
    let attribute = <doc value: erased()^>;
    let polymorphic = build(pure_parts);
    return [written == 15, observed == "prepared bytes\n", child[0] == "pure bytes",
        attribute.value == "pure bytes", polymorphic[0] == "pure bytes"]
}
