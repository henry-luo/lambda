// @expect-error: E224
// S12.1.1v3: attributes and content stay pure inside a procedure.
pn load_parts() { return "prepared" }
type Box { value: string }
pn main() {
    let attribute = <doc value: load_parts()>;
    let content = <file format:'pdf', load_parts()>;
    let nominal = <Box value: load_parts()>;
    return [attribute, content, nominal]
}
