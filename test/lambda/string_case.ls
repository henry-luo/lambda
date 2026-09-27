// S17.7.2 (SP23): lower and upper map by Unicode full case mapping,
// locale-independent, as ECMAScript's toLowerCase/toUpperCase do; they had
// mapped ASCII letters only. LambdaJS shares the one implementation.

'1. ASCII'
"1.1"; [lower("Hello World"), upper("Hello World")]
"1.2"; [lower("abc 123"), upper("ABC 123")]

'2. beyond ASCII'
"2.1"; [lower("ÉCOLE"), upper("école")]
"2.2"; [lower("ΑΒΓ"), upper("αβγ")]
"2.3"; [lower("ДОМ"), upper("дом")]

'3. special casing: a mapping may change the length'
"3.1"; [upper("ß"), upper("straße")]
"3.2"; [upper("ﬁ"), upper("ﬀ"), upper("ŉ")]
"3.3"; [lower("İ"), len(lower("İ"))]
"3.4"; [upper("ᾳ"), upper("ǅ"), lower("ǅ")]

'4. final sigma'
"4.1"; [lower("ΣΑΣ"), lower("ΣΑΣ ΑΣ"), lower("Σ"), lower("ΑΣ'Β")]

'5. symbols, null and caseless text'
"5.1"; [lower('ÉCOLE'), upper('straße')]
"5.2"; [lower(null), upper(null)]
"5.3"; [upper("日本"), lower("123 ?!")]
