// D7.5.2, D7.5.8: signed role labels are opaque; native/URI paths are distinct.
import uri: lambda.security.tuf.path
import tuf: lambda.security.tuf
import json: lambda.security.tuf.json
let encoded = "1...%2F%E5%90%8D%23%3Frole.json"
let labels = json.read("{\"é\":1,\"e\\u0301\":2}")^;
[uri.component("../名#?role")^, tuf.metadata_path({consistent: true}, "../名#?role", "1")^,
 uri.role_name("../名#?role"), uri.role_name("x\n"), uri.role_name("a" ++ chr(0)),
 uri.target_path("docs/名 file%20name.pkg")^,
 uri.component("%20")^, uri.target_path("literal%25name.pkg")^,
 [for (value in [encoded, "a%2Fb.json", "..%2Fopaque.json", "a%23b%3Fc%40d.json", "%25name", "a~b"]) uri.repository_path(value)],
 [for (value in ["../x", "/x", "a//b", "%2E/x", "%41", "%2f", "%00", "%0A", "%7F", "%C2%80", "%C0%AF", "%ED%A0%80", "%FF", "%", "%GG"]) uri.repository_path(value)],
 uri.target_name("nonexistent target"), uri.target_name("x?query"),
 tuf.artifact_path({consistent: false}, "docs/名 file%20name.pkg", json.read("{\"length\":0,\"hashes\":{\"sha256\":\"aa\"}}")^)^,
 "é" == "e" ++ chr(769), json.get(labels, "é").text, json.get(labels, "e" ++ chr(769)).text]
