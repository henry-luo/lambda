// ES Module test: parallel import of 3 independent modules (all depth 0)
import { rgb, RED, BLUE } from './module_parallel_colors.js';
import { area, perimeter, UNIT } from './module_parallel_geometry.js';
import { repeat, upper, SEPARATOR } from './module_parallel_strings.js';

// Test color module
console.log("rgb:", rgb(255, 128, 0));
console.log("colors:", RED, BLUE);

// Test geometry module
console.log("area:", area(5, 3));
console.log("perimeter:", perimeter(5, 3));
console.log("unit:", UNIT);

// Test string module
console.log("repeat:", repeat("ab", 3));
console.log("upper:", upper("hello"));
console.log("sep:", SEPARATOR);

// Test cross-module usage
const label = upper(RED) + SEPARATOR + rgb(0, 0, 0);
console.log("combined:", label);

const desc = repeat("*", area(2, 2));
console.log("stars:", desc);
