// Path expression tests
// Paths use /, ., .. for file, relative, parent schemes

"Basic path expressions"
/etc.hosts
/usr.local.bin
http.api.example.com
https.secure.api.example.com
sys.config

"Path with 6+ segments (tests extended path_build)"
/home.user.documents.projects.lambda.test

"Path in let binding"
let config_path = /etc.config
config_path

"Path in array"
[/a, /b, http.x]

"Path in map"
{input: /data.input, output: /data.output}

"Path with quoted segments (dots in filenames)"
/etc.'nginx.conf'
/home.user.'config.json'
http.'api.github.com'.users

"Dynamic path segment with subscript"
let segment = "config"
let dynamic_path = /etc[segment]
dynamic_path

"Wildcard patterns (single segment match)"
/src.*
http.api.users.*

"Wildcard patterns (recursive match)"
/src.**
http.api.**

"Quoted wildcard (literal asterisk, not a wildcard)"
/data.'*'
/data.'**'

"Path type checking"
type(/etc.hosts)

"File content (lazy loading)"
let hosts = /etc.hosts
len(hosts)

"exists() function - directory exists"
exists(/etc)

"exists() function - file exists"
exists(/etc.hosts)

"exists() function - non-existent path"
exists(/this_path_does_not_exist)

"len() on directory - counts entries"
(len(/etc) > 0)

"Path iteration - for loop over directory"
let etc_items = for (item in /etc) item
(len(etc_items) > 0)

"Path iteration - collect types"
let item_types = for (item in /etc) type(item)
item_types[0]

"Relative path with . scheme"
let rel_dir = .test.input.dir
rel_dir

"exists() on relative path"
exists(.test.input.dir)

"len() on relative directory"
len(.test.input.dir)

"Relative path iteration with single wildcard (*) - list paths"
for (item in .test.input.dir.*) item

"Relative path iteration with recursive wildcard (**) - list paths"
for (item in .test.input.dir.**) item

"Wildcard finds nested items"
let single_wildcard = for (item in .test.input.dir.*) item
let recursive_wildcard = for (item in .test.input.dir.**) item
(len(recursive_wildcard) > len(single_wildcard))

"Path property: name"
let file_path = .test.input.'test.json'
file_path.name

"Path property: is_dir on directory"
let dir_path = .test.input.dir
dir_path.is_dir

"Path property: is_file on directory (should be false)"
dir_path.is_file

"Path property: is_file on file"
file_path.is_file

"Path property: is_dir on file (should be false)"
file_path.is_dir

"Path property: is_link on regular file"
file_path.is_link

"Path property: size on file (returns bytes)"
(file_path.size > 0)

"Path property: modified on file (returns datetime)"
type(file_path.modified)

"Path property: name from iteration"
let names = for (item in .test.input.dir.*) item.name
(len(names) > 0)

"Path property: filter using is_dir"
let first_item = .test.input.dir.child_dir
first_item.is_dir

"Parent path with .. scheme"
let parent_path = ..
parent_path

"Parent path with segment"
..test.input.dir

"Parent path type checking"
type(..test)

'exists() on parent path'
// exists(..Lambda.test.input.dir)

"Parent path in let binding"
let parent_dir = ..Lambda.test
parent_dir

"Parent path with quoted segment"
..Lambda.'README.md'

"Path ++ string: append segment"
let base = /etc
base ++ "hosts"

"Path ++ string: multiple appends"
let p1 = /home
let p2 = p1 ++ "user"
p2 ++ "documents"

"Path ++ symbol: append symbol segment"
let dir = /var
dir ++ 'log'

"Path ++ relative path: concat relative path"
let abs = /home.user
let rel = .documents.file
abs ++ rel

"Path ++ parent path: concat parent path"
let base_path = /home.user.projects
let parent_rel = ..shared.lib
base_path ++ parent_rel

"Path type preserved after ++"
type(/etc ++ "hosts")

"Chained ++ operations"
/home ++ "user" ++ "documents" ++ "file.txt"
