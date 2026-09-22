!function(target, factory) {
  (target = "undefined" != typeof globalThis ? globalThis : target || self)
      .ExternalGlobalBinding = factory();
}(this, function() {
  "use strict";
  return function() { return "ready"; };
});
