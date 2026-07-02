// This small fixture becomes "large" when RADIANT_JS_EXTERNAL_SCRIPT_BYTES=128.
// The guard exists for multi-megabyte browser bundles found on online docs sites.
window.largeExternalScriptSkipRepro = [
  "alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta",
  "iota", "kappa", "lambda", "mu", "nu", "xi", "omicron", "pi"
].join("-");
