// D11.2.4, S12.1.2v3: flat wire length must not consume parser call depth.
import json: lambda.security.tuf.json
import meta: lambda.security.tuf.metadata
let text = join([for (i in 1 to 5000) "a"], "")
let spaces = join([for (i in 1 to 5000) " "], "")
let array_text = "[" ++ join([for (i in 1 to 1200) "0"], ",") ++ "]"
let object_text = "{" ++ join([for (i in 1 to 600) "\"k" ++ string(i) ++ "\":0"], ",") ++ "}"
let numeral = join([for (i in 1 to 5000) "9"], "");
[len(json.text(json.read("\"" ++ text ++ "\"")^)^),
 json.read(spaces ++ "true")^.value,
 len(json.items(json.read(array_text)^)^), len(json.members(json.read(object_text)^)^),
 len(json.positive(json.read(numeral)^)^),
 json.read("\"" ++ text ++ "\\z\"") ^ { 'denied' }, len(meta.unhex("")^)]
