// Parent navigation and ordinary parent-field access.

// Test 1: parent access on a map with 'parent' field
"Parent field on map"
let node = {name: "child", parent: {name: "root", parent: null}}
node.parent

"Parent name via member";
(node.parent).name

// Test 2: double parent access on nested structure
"Double parent field"
let deep = {name: "leaf", parent: {name: "mid", parent: {name: "top", parent: null}}};
(deep.parent.parent).name

// Test 3: parent access on path - get parent directory
"Path parent access: single"
let p = /.home.user.documents
p.~~

"Path parent access: double"
p.~~.~~

// Test 4: parent access chained with member access
"Chained parent field value"
let tree = {value: 10, parent: {value: 20, parent: {value: 30, parent: null}}};
(tree.parent).value

"Double parent field value";
(tree.parent.parent).value
