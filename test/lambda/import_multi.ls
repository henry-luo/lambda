// test importing multiple modules in one script
import .mod_vars, .mod_compute

// from mod_vars
[name, version]

// from mod_compute
[doubled, total]

// cross-module: mix values from both modules
name ++ " v" ++ string(version) ++ " has " ++ string(total) ++ " items"
