// MT5 fixture: a hoisted inner function declaration writes through to its
// module-var slot. Writing only the local register and the shared scope env
// left a nested closure resolving the module var to garbage (Stage 4C
// LambdaJS bug #4). The arrow below captures `helper` from the enclosing IIFE
// scope, so the hoist site must emit a js_set_module_var store.
// Checked by hoisted_modvar_write_through.mir-check.

(function () {
  function helper(x) { return x + 1; }
  const useHelper = () => helper(41);
  console.log(useHelper());
})();
