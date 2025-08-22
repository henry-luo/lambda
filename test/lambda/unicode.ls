// Lambda Unicode String Comparison Test Suite
// Tests comprehensive Unicode support across different levels

"===== BASIC ASCII COMPARISON TESTS ====="

// String equality - ASCII
("hello" == "hello")
("hello" == "world")
("hello" != "world")

// String relational operators - ASCII (now supported!)
("abc" < "def")
("def" > "abc")
("abc" <= "abc")
("abc" >= "abc")
("hello" < "world")

"Basic ASCII tests completed"

"===== UNICODE NORMALIZATION TESTS ====="

// NFC vs NFD normalization (café with different encodings)
// Note: These may appear identical but have different byte representations
("café" == "café")

// Combining characters
("naïve" == "naïve")
("résumé" == "résumé")

"Unicode normalization tests completed"

"===== LATIN EXTENDED CHARACTER TESTS ====="

// Basic Latin extended characters
("café" != "cafe")
("naïve" != "naive")
("résumé" != "resume")

// German characters
("Straße" != "Strasse")
("müller" != "muller")

// Scandinavian characters  
("København" != "Kobenhavn")
("Göteborg" != "Goteborg")

"Latin extended tests completed"

"===== UNICODE RELATIONAL COMPARISON TESTS ====="

// Unicode characters in proper collation order
("a" < "á")
("á" < "b")
("n" < "ñ")
("ñ" < "o")

// Case sensitivity in Unicode
("A" < "a")
("Hello" < "hello")

"Unicode relational tests completed"

"===== EMOJI AND SYMBOL TESTS ====="

// Basic emoji comparison
("🍎" == "🍎")
("🍎" != "🍌")
("👋" == "👋")

// Emoji with skin tone modifiers (complex Unicode sequences)
("👋🏽" == "👋🏽")
("👋🏽" != "👋")

// Symbol comparison
("©" == "©")
("®" != "©")

"Emoji and symbol tests completed"

"===== NON-LATIN SCRIPT TESTS ====="

// Greek characters
("α" == "α")
("α" != "β")
("α" < "β")

// Cyrillic characters
("а" == "а")
("а" != "б")
("Москва" == "Москва")

"Non-Latin script tests completed"

"===== EDGE CASES AND ERROR HANDLING ====="

// String vs other types (should still error)
// These operations should return error, not crash
("hello" == 42)
("hello" < true)

"Edge case tests completed"

"===== COMPLEX UNICODE TESTS ====="

// Mixed scripts in same string
("Hello世界" == "Hello世界")
("café東京" == "café東京")

// Longer Unicode strings
("The quick brown fox jumps over the lazy dog" == 
 "The quick brown fox jumps over the lazy dog")

("Thé quick brown fôx jumps över the låzy dög" == 
 "Thé quick brown fôx jumps över the låzy dög")

// Number-like Unicode characters (should be treated as strings, not numbers)
("１２３" == "１２３")
("１２３" != "123")

"Complex Unicode tests completed"

"===== PERFORMANCE TESTS (ASCII FAST PATH) ====="

// These should use the fast ASCII path
("abcdefghijklmnopqrstuvwxyz" == "abcdefghijklmnopqrstuvwxyz")

// These should use the Unicode path  
("abcdéfghijklmnöpqrstüvwxyz" == "abcdéfghijklmnöpqrstüvwxyz")

"Unicode tests completed"
