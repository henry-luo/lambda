import json: lambda.security.tuf.json
let ordered = json.canonical_text("{\"z\":[true,null,-0],\"a\":123456789012345678901234567890}")^
let escaped = json.canonical_text("{\"a\":\"line\\nquote\\\"slash\\\\\"}")^
let duplicates = json.read("{\"a\":1,\"\\u0061\":2}") ^ { 'denied' }
let comma = json.read("[1,]") ^ { 'denied' }
let floating = json.read("{\"x\":1.0}") ^ { 'denied' }
let surrogate = json.read("\"\\ud800\"") ^ { 'denied' }
let supplementary = json.text(json.read("\"\\ud83d\\ude00\"")^)^;
[ordered, escaped, duplicates, comma, floating, surrogate, supplementary,
 json.increment("99999999999999999999999999999999999")^,
 json.compare("12345678901234567890", "12345678901234567891")]
