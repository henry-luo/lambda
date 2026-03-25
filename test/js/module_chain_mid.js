// ES Module: mid-level module (imports from base, depth 1)
import { double, BASE_VAL } from './module_chain_base.js';

export const MID_VAL = double(BASE_VAL);
export const MID_QUAD = double(double(5));
