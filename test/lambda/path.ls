// Path expression tests
// Paths use scheme.segment.segment notation

"Basic path expressions"
file.etc.hosts
file.usr.local.bin
http.api.example.com
https.secure.api.example.com
sys.config

"Path in let binding"
let config_path = file.etc.config
config_path

"Path in array"
[file.a, file.b, http.x]

"Path in map"
{input: file.data.input, output: file.data.output}
