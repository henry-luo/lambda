import mark: lambda.sync.mark
import conversion: lambda.sync.document_format
import codec: lambda.sync.codec

let source = parse("<article title=\"one\"><p>hello</p><p>world</p></article>",'xml')^
let replica = mark.from_value("formats","seed",source)^
let native = mark.export_value(replica)^
let raw = conversion.export_raw(replica,'xml')^
let text = conversion.parse_raw(raw,'mark','xml')^
let ordinary = mark.from_value("ordinary","seed",{kind:"element",tag:"user-data",attributes:{a:1},children:[2]})^
let json = conversion.export_raw(ordinary,'json')^
let saved = conversion.export_raw(replica,'mark')^;
[native is element,mark.normalize(native)^ == mark.value(replica)^,
 mark.normalize(text)^ == mark.value(replica)^,
 parse(json,'json')^ == mark.value(ordinary)^,
 conversion.verify_raw(replica,'mark',saved)^,
 conversion.parse_raw("incomplete {",'text','text')^ == "incomplete {",
 conversion.parse_raw("incomplete {",'mark','json') ^ { true }]
