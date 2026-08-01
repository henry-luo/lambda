// Identifier-minified bundles reuse short names in independent IIFE wrappers.
// Each wrapper must retain its own var binding rather than sharing a name-keyed
// promoted module slot with a sibling wrapper.
(function() {
  var e = function() { return "first"; };
  globalThis.first_iife_value = function() { return e(); };
})();

(function() {
  var e = function() { return "second"; };
  globalThis.second_iife_value = function() { return e(); };
})();

console.log(first_iife_value() + ":" + second_iife_value());
