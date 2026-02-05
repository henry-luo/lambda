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

"Quoted wildcard (literal asterisk, not a wildcard)"
file.data.'*'
file.data.'**'

"Path type checking"
type(file.etc.hosts)

"File content (lazy loading)"
let hosts = file.etc.hosts
len(hosts)

"exists() function - directory exists"
exists(file.etc)

"exists() function - file exists"
exists(file.etc.hosts)

"exists() function - non-existent path"
exists(file.this_path_does_not_exist)

"len() on directory - counts entries"
(len(file.etc) > 0)

"Path iteration - for loop over directory"
let etc_items = for (item in file.etc) item
(len(etc_items) > 0)

"Path iteration - collect types"
let item_types = for (item in file.etc) type(item)
item_types[0]

"Relative path with cwd scheme"
let rel_dir = cwd.test.input.dir
rel_dir

"exists() on relative path"
exists(cwd.test.input.dir)

"len() on relative directory"
len(cwd.test.input.dir)

"Relative path iteration with single wildcard (*) - list paths"
for (item in cwd.test.input.dir.*) item

"Relative path iteration with recursive wildcard (**) - list paths"
for (item in cwd.test.input.dir.**) item

"Wildcard finds nested items"
let single_wildcard = for (item in cwd.test.input.dir.*) item
let recursive_wildcard = for (item in cwd.test.input.dir.**) item
(len(recursive_wildcard) > len(single_wildcard))
