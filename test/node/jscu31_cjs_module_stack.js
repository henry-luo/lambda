const fs = require('fs');
const prefix = 'temp/jscu31_cjs_module_stack_';
const count = 161;

for (let i = 0; i < count; i++) {
    const path = prefix + i + '.js';
    const source = i === 0
        ? 'module.exports = { current: module };'
        : "const child = require('./jscu31_cjs_module_stack_" + (i - 1) +
          ".js'); module.exports = { current: module, child: child };";
    fs.writeFileSync(path, source);
}

let cursor = require('../../temp/jscu31_cjs_module_stack_160.js');
let linked = 1;
while (cursor.child) {
    if (cursor.child.current.parent === cursor.current) linked++;
    cursor = cursor.child;
}
console.log(linked);

for (let i = 0; i < count; i++) {
    fs.unlinkSync(prefix + i + '.js');
}
