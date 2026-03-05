// Test: Truthiness
// Layer: 2 | Category: statement | Covers: truthy/falsy values, or idiom, conditional contexts

// ===== Integer 0 is truthy =====
if (0) "truthy" else "falsy"

// ===== Positive integer truthy =====
if (1) "truthy" else "falsy"

// ===== Negative integer truthy =====
if (-1) "truthy" else "falsy"

// ===== Empty string is falsy / null =====
// (empty string "" is null in Lambda)
if ("") "truthy" else "falsy"

// ===== Non-empty string truthy =====
if ("hello") "truthy" else "falsy"

// ===== Null is falsy =====
if (null) "truthy" else "falsy"

// ===== Boolean true =====
if (true) "truthy" else "falsy"

// ===== Boolean false =====
if (false) "truthy" else "falsy"

// ===== Empty array is truthy =====
if ([]) "truthy" else "falsy"

// ===== Non-empty array truthy =====
if ([1, 2, 3]) "truthy" else "falsy"

// ===== Empty map is truthy =====
if ({}) "truthy" else "falsy"

// ===== Non-empty map truthy =====
if ({a: 1}) "truthy" else "falsy"

// ===== Error is falsy =====
if (error("test")) "truthy" else "falsy"

// ===== Float 0.0 truthy =====
if (0.0) "truthy" else "falsy"

// ===== Or idiom for defaults =====
null or "default"
false or "fallback"
"" or "empty fallback"
"value" or "fallback"
0 or "zero fallback"

// ===== And short-circuit =====
true and "reached"
false and "unreached"
null and "unreached"

// ===== Truthiness in filter =====
[null, 1, "", "hello", false, true, 0] | filter((x) => x)

// ===== Nested or =====
null or false or "" or "found"
