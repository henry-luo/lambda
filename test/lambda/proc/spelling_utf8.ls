import utf8: lambda.io.utf8

fn rejected(bytes) => (utf8.decode(bytes) ^ { null }) == null

pn main() {
    [
        utf8.decode(binary("cat"))^ == "cat",
        utf8.decode(binary("café"))^ == "café",
        utf8.decode(binary("élégant"))^ == "élégant",
        utf8.decode(binary("中"))^ == "中",
        utf8.decode(binary("😀"))^ == "😀",
        utf8.decode(b'\x00')^ == "\u0000",
        utf8.decode(b'\x7F')^ == chr(127),
        utf8.decode(b'\xC280')^ == chr(128),
        utf8.decode(b'\xDFBF')^ == chr(2047),
        utf8.decode(b'\xE0A080')^ == chr(2048),
        utf8.decode(b'\xF0908080')^ == chr(65536),
        utf8.decode(b'\xF48FBFBF')^ == chr(1114111),
        rejected(b'\x80'),
        rejected(b'\xC0AF'),
        rejected(b'\xC241'),
        rejected(b'\xE08080'),
        rejected(b'\xEDA080'),
        rejected(b'\xE282'),
        rejected(b'\xF0808080'),
        rejected(b'\xF4908080'),
        rejected(b'\xF5808080'),
        rejected(b'\xFF')
    ]
}
