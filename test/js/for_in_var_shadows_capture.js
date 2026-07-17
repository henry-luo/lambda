function build_copy() {
  function key(value) {
    return value;
  }

  function copy_missing(target, source) {
    for (var key in source) {
      if (!(key in target)) target[key] = source[key];
    }
    return target;
  }

  return copy_missing;
}

var Plugin = function Plugin() {};
build_copy()(Plugin, {
  targetTest: function targetTest() { return true; },
  prop: 'css'
});

console.log(
  'for-in-var-shadow:',
  typeof Plugin.targetTest,
  Plugin.targetTest(),
  Plugin.prop,
  Object.keys(Plugin).join(',')
);
