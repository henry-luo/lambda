import json: lambda.security.tuf.json
let node = json.read("{\"n\":123456789012345678901234567890,\"s\":\"a\\u0000b\\n\\t\\\"\\\\😀\"}")^
let document = json.document(node)^;
[json.canonical(json.read(document)^)^ == json.canonical(node)^,
 not contains(document, chr(0)), contains(document, "123456789012345678901234567890")]
