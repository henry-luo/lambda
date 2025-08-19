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
