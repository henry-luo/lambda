// Parent access shorthand (..) tests
// expr .. is shorthand for expr.parent
// expr .._.. is shorthand for expr.parent.parent

// Test 1: parent access on a map with 'parent' field
"Parent access on map"
let node = {name: "child", parent: {name: "root", parent: null}}
node ..

"Parent name via .."
(node ..).name

// Test 2: double parent access on nested structure
"Double parent access"
let deep = {name: "leaf", parent: {name: "mid", parent: {name: "top", parent: null}}}
(deep .._.. ).name

// Test 3: parent access on path - get parent directory
"Path parent access: single"
let p = /home.user.documents
p ..

"Path parent access: double"
p .._..

// Test 4: parent access chained with member access
"Chained parent value"
let tree = {value: 10, parent: {value: 20, parent: {value: 30, parent: null}}}
(tree ..).value

"Double parent value"
(tree .._..).value
