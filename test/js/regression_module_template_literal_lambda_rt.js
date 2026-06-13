// Regression: ES modules containing template literals crashed with SIGSEGV.
//
// Root cause: jm_transpile_template_literal emits MIR that reads the global
// Context* `_lambda_rt` (via the import-resolver) to obtain a pool pointer
// for stringbuf_new. transpile_js_to_mir_core_len (the non-module entry)
// sets `_lambda_rt = context` before invoking the JIT'd js_main, but
// transpile_js_module_to_mir did not — leaving _lambda_rt as whatever the
// previous test left it (NULL on the very first run, stale Context* after).
// Dereferencing rt->pool then SIGSEGV'd.
//
// Fix: set _lambda_rt = (Context*)context around the js_main call in
// transpile_js_module_to_mir, restoring the previous value afterwards so
// nested module loads don't leak.
//
// Surfaced by Js54 P7 admission: lifting the resizable-arraybuffer skip
// exposed 17 language/module-code/top-level-await/syntax/*template_literal*.js
// tests that all hit this path. Those tests use just `await ``;` at the top
// level of a module — the template literal alone is enough to trigger it.

// This file is loaded as a module by the gtest runner. Any template literal
// here exercises the fixed path.
const greeting = `hello`;
const interp = `value is ${42}`;
const multi = `${1}-${2}-${3}`;

if (greeting !== "hello") throw new Error("plain template literal broken");
if (interp !== "value is 42") throw new Error("interpolated template literal broken");
if (multi !== "1-2-3") throw new Error("multi-interp template literal broken");

console.log("module template literal regression: all assertions passed");
