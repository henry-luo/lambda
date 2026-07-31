// Test: Procedural statements outside a procedure
// Layer: 2 | Category: negative/semantic | Covers: var/assignment/while/break/continue/return at
// module scope, all of which must report E224 rather than being silently swallowed.
//
// Regression guard for the diagnostics bug where these guards logged without recording a semantic
// error: error_count stayed 0, so MIR ran against the holey AST and buried the real message under
// invented follow-on errors about the binding the guard had just refused to create.

var x = 1
x = x + 1

while (true) { 1 }
break
continue
return 5
