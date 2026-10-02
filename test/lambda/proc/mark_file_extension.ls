// D2.9.1–D2.9.2: only .mark selects Mark automatically; explicit Mark ignores the filename.
pn main() {
    let source = "'extension-check'";
    for (filename in ["data.mark", "data.MARK", "data.m", "data.mk", "data.mr",
                  "data.ma", "data.mrk", "data.payload", "data"]) {
        let path = "./temp/mark_extension_" ++ filename;
        output(source, path)^;
        let automatic = input(path)^;
        let explicit = input(path, 'mark')^;
        print([filename, automatic is symbol, explicit == 'extension-check']);
        print("\n");
        io.delete(path)^
    }

    // a conflicting known extension must also yield to the explicit format.
    let path = "./temp/mark_extension_conflicting.json";
    output(source, path)^;
    print(["explicit .json", input(path, 'mark')^ == 'extension-check']);
    print("\n");
    io.delete(path)^
}
