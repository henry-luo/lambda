// Js52 P1: helper module for module_aliased_exports.js
// Tests `export { x as y }` aliasing in named exports.
const x = 42;
export default x;
export const y = 'hello';
export { x as renamed };

// Multiple aliases at once
const a = 1, b = 2, c = 3;
export { a, b as bee, c as cee };
