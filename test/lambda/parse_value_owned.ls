let json = "{\"first\":[1,\"héllo\\u0000tail\"],\"\":1,\"nul\\u0000key\":2,\"日本\":3,\"last\":true}"
let source = "<node id:1 nested:{x:[b'0000FF',t'2026-11-17',900719925474099312345n,1.2300m]} 'symbol' \"child\">"
let value = parse(source,'mark')^
let rendered = format(value,'mark')^
let json_value = parse(binary(json)^,'json')^
let churn = [for (i in 1 to 256) parse("{\"x\":[1,2,3],\"y\":\"value\"}",'json')^];
[format(parse(rendered,'mark')^,'mark')^ == rendered,
 format(parse(format(json_value,'json')^,'json')^,'json')^ == format(json_value,'json')^,
 [for (key at json_value) string(key)] == ["first","","nul\u0000key","日本","last"],
 json_value[""] == 1,
 len(churn)^ == 256,
 parse("{broken",'json') ^ { ^ is error }]
