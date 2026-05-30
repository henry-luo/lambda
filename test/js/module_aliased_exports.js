// Js52 P1: ESM aliased export specifiers
// Verifies that `export { x as y }` publishes the binding under `y`,
// not under the local name `x`.
import def, { y, renamed, a, bee, cee } from './module_aliased_exports_lib.js';
console.log('default:', def);
console.log('y:', y);
console.log('renamed:', renamed);
console.log('a:', a);
console.log('bee:', bee);
console.log('cee:', cee);
