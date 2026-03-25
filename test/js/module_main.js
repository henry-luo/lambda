// ES Module import test: named imports, default import, and usage
import greet from './module_utils.js';
import { add, multiply, PI } from './module_utils.js';

// Test named function imports
console.log("add:", add(3, 4));
console.log("multiply:", multiply(5, 6));

// Test named constant import
console.log("PI:", PI);

// Test default import
console.log(greet("World"));

// Test using imports in expressions
const result = add(multiply(2, 3), 4);
console.log("expr:", result);
