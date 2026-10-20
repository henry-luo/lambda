// S2.4.6, PTH81-PTH84: wildcard paths stay lazy until `#`; a force reads
// exactly the files a spelled step enters
"a wildcard path is a reference"
\.test.input.wild_tree.*.name
"one level, nothing read"
\.test.input.wild_tree.*#
"the closure includes its base, nothing read"
\.test.input.wild_tree.**#
"a step after the wildcard enters each file"
\.test.input.wild_tree.**.name#
\.test.input.wild_tree.*.name#
"after # the steps act on path values: metadata, no file read"
\.test.input.wild_tree.*#.name
"two item steps: a file's top-level items, a directory's entries"
\.test.input.wild_tree.*.*#
"** twice: directories deduplicated, files searched"
\.test.input.wild_tree.sub.**.**#
"a wildcard inside a document enumerates the parsed value"
\.test.input.wild_tree.'a.json'.*#
\.test.input.wild_tree.'a.json'.items.*#
\.test.input.wild_tree.'a.json'.**#
"iteration enumerates the same closure"
; [for (x in \.test.input.wild_tree.**) x]

