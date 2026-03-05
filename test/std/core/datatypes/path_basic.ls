// Test: Path Basic
// Layer: 1 | Category: datatype | Covers: path types, schemes, wildcards

// ===== Path literals =====
/etc.hosts
/usr.local.bin

// ===== Path type check =====
type(/etc.hosts)

// ===== Path with different schemes =====
http.api.example.com
https.secure.api

// ===== Relative path =====
.test.input.dir

// ===== Path with quoted segments =====
/etc.'nginx.conf'
/home.user.'config.json'

// ===== Path in let binding =====
let config = /etc.config
config

// ===== Wildcard patterns =====
/src.*
/src.**

// ===== Path in collections =====
[/a, /b, http.x]
{input: /data.input, output: /data.output}

// ===== Dynamic segment =====
let segment = "config"
let dynamic = /etc[segment]
dynamic

// ===== Path with 6+ segments =====
/home.user.documents.projects.lambda.test

// ===== Path exists =====
exists(/etc)
exists(/this_path_does_not_exist)
