export {};
function check(value) { if (!value) throw new Error('module eval boundary'); console.log(true); }
let value = 3;
function update() {
  eval('value += 4; var privateValue = 11;');
  return typeof privateValue;
}
check(update() === 'undefined' && value === 7);
check(eval('value') === 7);
function captured() { return () => { value++; return value; }; }
const increment = captured();
check(increment() === 8 && increment() === 9);
let tdz = false;
try { eval('later'); let later = 1; } catch (error) { tdz = error instanceof ReferenceError; }
check(tdz);
globalThis.moduleEvalShadow = 1;
const sloppyOuter = Function("function read() {'use strict'; return moduleEvalShadow;} eval('var moduleEvalShadow = 7'); return read();");
check(sloppyOuter() === 7 && globalThis.moduleEvalShadow === 1);
const evalClosure = Function("const read = eval(\"'use strict'; (function() { return moduleEvalShadow; })\"); eval('var moduleEvalShadow = 13'); return read();");
check(evalClosure() === 13);
const writeShadow = Function("function write() {'use strict'; moduleEvalShadow++; return moduleEvalShadow;} eval('var moduleEvalShadow = 19'); return write();");
check(writeShadow() === 20 && globalThis.moduleEvalShadow === 1);
delete globalThis.moduleEvalShadow;
check(typeof moduleEvalShadow === 'undefined');
console.log('MODULE_EVAL_BOUNDARIES_DONE');
