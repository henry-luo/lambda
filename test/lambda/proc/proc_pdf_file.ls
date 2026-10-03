// Byte construction, dynamic selection, reuse, and rejection of deferred kinds.
fn fragments() => ("L", null, b'\x00FF', "R")

pn main() {
    let selector = "pdf";
    let file = <file format:selector,
        "A" b'\x00FF' "é" null "" fragments() "\n"
    >;
    let mixed = output(file, "./temp/pdf_file_mixed.bin")^;
    let reused = output(file, "./temp/pdf_file_reused.bin", {atomic: true})^;
    let repeated = for (i in 1 to 4) "x";
    let text = <file format:'pdf', "head:" repeated ":tail">;
    let text_size = output(text, "./temp/pdf_file_text.txt")^;
    let text_value = input("./temp/pdf_file_text.txt", "text")^;
    let empty = output(<file format:'pdf', null "">, "./temp/pdf_file_empty.bin")^;
    let no_body = output(<file format:'pdf'>, "./temp/pdf_file_no_body.bin")^;
    let rejected = (<file format:'pdf', "prefix" {unsupported: <node>}>) ^ { "rejected" };
    let rejected_array = (<file format:'pdf', ["a", "b"]>) ^ { "rejected" };
    let ordinary = <file format:'html', "a" null "b">;
    return [mixed, reused, text_size, text_value, empty, no_body,
        rejected, rejected_array, ordinary[0] == "ab", len(ordinary) == 2]
}
