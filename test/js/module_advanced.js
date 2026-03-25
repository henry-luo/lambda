// ES Module test: import aliases, namespace imports, closures with imports
import { square as sq, cube, E } from './module_math.js';
import greet from './module_utils.js';
import { add } from './module_utils.js';

// Test import alias
console.log("sq:", sq(5));
console.log("cube:", cube(3));
console.log("E:", E);

// Test using imports inside closures
function makeAdder(n) {
    return function(x) {
        return add(x, n);
    };
}
const add5 = makeAdder(5);
console.log("closure:", add5(10));

// Test imports in higher-order functions
const nums = [1, 2, 3, 4];
const squared = nums.map(sq);
console.log("mapped:", squared.join(" "));

// Test default import with named imports
console.log(greet("Module"));
