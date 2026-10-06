// libc++ keeps unused locale facets through RE2's logging vtables. Deny their
// host requests at the embedding boundary instead of linking ambient providers.
addToLibrary({
  $lambdaUnavailable: (capability) => {
    throw new WebAssembly.RuntimeError(`lambda-wasm excludes ${capability}`);
  },
  _tzset_js__deps: ['$lambdaUnavailable'],
  _tzset_js: (timezone, daylight, standard, summer) => lambdaUnavailable('timezone discovery'),
  // libc's startup constructor accepts an unavailable environment and leaves
  // environ NULL. Return the ABI error without inventing host variables.
  environ_get__deps: [],
  environ_get: (environment, buffer) => {{{ cDefs.ENOSYS }}},
  environ_sizes_get__deps: [],
  environ_sizes_get: (count, size) => {{{ cDefs.ENOSYS }}},
  _setitimer_js__deps: ['$lambdaUnavailable'],
  _setitimer_js__postset: '',
  _setitimer_js: (which, timeout) => lambdaUnavailable('timer scheduling'),
});
