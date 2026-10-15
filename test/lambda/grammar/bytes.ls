// Binary grammar matches one byte per character; text grammar remains Unicode (GM14).
grammar records {
    Records = Entry*
    Entry = value: data, _nul
    data := !"\u0000"+
    _nul := "\u0000"
}
grammar symbols {
    Pair = label: word, value: payload
    word := 'ok'
    payload := !"\n"+
    space := " "*
}
let source = b'\xFF0080C3A900'
let tree = parse(source,records,{spans:true})^
let text = parse("é\u0000",records,{spans:true})^
let pair = parse(binary("ok payload"),symbols)^
let repeated = [for (i in 1 to 30) parse(source,records,{spans:true})^]
let bad = parse(b'\xFF',records) ^ { ^.message };
[tree[0].value == b'\xFF',tree[1].value == b'\x80C3A9',tree[1].start == 2 and tree[1].end == 6,
 text[0].value == "é" and text[0].end == 3,pair.label == 'ok' and pair.value == binary("payload"),
 all([for (result in repeated) result[1].value == b'\x80C3A9']),
 contains(bad,"\\u0000") and contains(bad,"at 1:2"),
 binary([0,127,128,255])^ == b'\x007F80FF',binary([])^ == null,
 binary([256]) ^ { ^ is error },binary([-1]) ^ { ^ is error },binary([1.5]) ^ { ^ is error }]
