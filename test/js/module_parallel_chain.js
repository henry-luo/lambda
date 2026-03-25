// ES Module test: transitive import chain (multi-level parallel compilation)
// Import graph: main → chain_mid → chain_base (depth 1→0)
//               main → chain_extra (depth 0, parallel with chain_base)
import { MID_VAL, MID_QUAD } from './module_chain_mid.js';
import { negate, BASE_VAL } from './module_chain_base.js';
import { half, EXTRA_VAL } from './module_chain_extra.js';

// Test transitive chain values computed at load time
console.log("mid_val:", MID_VAL);
console.log("mid_quad:", MID_QUAD);

// Test direct base access
console.log("negate:", negate(7));
console.log("base_val:", BASE_VAL);

// Test extra module (compiled in parallel with base at depth 0)
console.log("half:", half(10));
console.log("extra_val:", EXTRA_VAL);

// Test cross-module computation in main
const sum = MID_VAL + negate(BASE_VAL) + EXTRA_VAL;
console.log("sum:", sum);

const result = half(MID_QUAD) + EXTRA_VAL;
console.log("result:", result);
