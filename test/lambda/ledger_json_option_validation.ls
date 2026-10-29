type JsonRecord { value: int }
let bad_compact = format(1, {type:'json',compact:2}) ^ { ^.message }
let bad_indent = format(1, {type:'json',indent:-1}) ^ { ^.message };
[format(<p a:1, "hi">, {type:'json',compact:true}) == "{\"$\":\"p\",\"a\":1,\"_\":[\"hi\"]}",
 format(<JsonRecord value:3>, {type:'json',compact:true}) == "{\"@\":\"JsonRecord\",\"value\":3}",
 bad_compact == "format: compact must be bool",
 bad_indent == "format: indent must be a non-negative integer"]
