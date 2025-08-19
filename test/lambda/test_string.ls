// Comprehensive test of the normalize system function with utf8proc backend
let text = "é"  // e with acute accent (could be composed or decomposed)

// Test different normalization forms
let nfc_result = normalize(text, 'nfc')    // Canonical Composition
let nfd_result = normalize(text, 'nfd')    // Canonical Decomposition  
let nfkc_result = normalize(text, 'nfkc')  // Compatibility Composition
let nfkd_result = normalize(text, 'nfkd')  // Compatibility Decomposition

// Test with another string containing complex Unicode
let complex = "ḱṷṓn"
let complex_nfc = normalize(complex, 'nfc')

let result = normalize("José Café", 'nfc')

// Output results
[nfc_result, nfd_result, nfkc_result, nfkd_result, complex_nfc, result]

// Test substring and contains system functions
let str = "Hello, World!"

// Test substring function
"original string:"; str
"substring(0, 5):"; str.substring(0, 5)
"substring(7, 12):"; str.substring(7, 12)
"substring(0, -1):"; str.substring(0, -1)
"substring(-6, -1):"; str.substring(-6, -1)
"substring(5, 5):"; str.substring(5, 5)
"substring(0, 0):"; str.substring(0, 0)
"substring(0, 100):"; str.substring(0, 100)

// Test contains function
"contains Hello:"; str.contains("Hello")
"contains World:"; str.contains("World")
"contains hello:"; str.contains("hello")
"contains xyz:"; str.contains("xyz")
"contains comma:"; str.contains(",")
