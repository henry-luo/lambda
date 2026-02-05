// Path expression tests
// Paths use scheme.segment.segment notation

"Basic path expressions"
file.etc.hosts
file.usr.local.bin
http.api.example.com
https.secure.api.example.com
sys.config

"Path with 6+ segments (tests extended path_build)"
file.home.user.documents.projects.lambda.test

"Path in let binding"
let config_path = file.etc.config
config_path

"Path in array"
[file.a, file.b, http.x]

"Path in map"
{input: file.data.input, output: file.data.output}

"Path with quoted segments (dots in filenames)"
file.etc.'nginx.conf'
file.home.user.'config.json'
http.'api.github.com'.users

"Dynamic path segment with subscript"
let segment = "config"
let dynamic_path = file.etc[segment]
dynamic_path

"Wildcard patterns (single segment match)"
file.src.*
http.api.users.*

"Wildcard patterns (recursive match)"
file.src.**
http.api.**
