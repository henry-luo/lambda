// Lambda Unicode String Comparison Test Suite
// Tests comprehensive Unicode support across different levels

"===== BASIC ASCII COMPARISON TESTS ====="

// String equality - ASCII
"hello equals hello:"; ("hello" == "hello")
"hello equals world:"; ("hello" == "world")
"hello not equals world:"; ("hello" != "world")

// String relational operators - ASCII (now supported!)
"abc less than def:"; ("abc" < "def")
"def greater than abc:"; ("def" > "abc")
"abc less than or equal abc:"; ("abc" <= "abc")
"abc greater than or equal abc:"; ("abc" >= "abc")
"hello less than world:"; ("hello" < "world")

"Basic ASCII tests completed"

"===== UNICODE NORMALIZATION TESTS ====="

// NFC vs NFD normalization (café with different encodings)
// Note: These may appear identical but have different byte representations
"café equals café (normalization):"; ("café" == "café")

// Combining characters
"naïve equals naïve:"; ("naïve" == "naïve")
"résumé equals résumé:"; ("résumé" == "résumé")

"Unicode normalization tests completed"

"===== LATIN EXTENDED CHARACTER TESTS ====="

// Basic Latin extended characters
"café not equals cafe:"; ("café" != "cafe")
"naïve not equals naive:"; ("naïve" != "naive")
"résumé not equals resume:"; ("résumé" != "resume")

// German characters
"Straße not equals Strasse:"; ("Straße" != "Strasse")
"müller not equals muller:"; ("müller" != "muller")

// Scandinavian characters  
"København not equals Kobenhavn:"; ("København" != "Kobenhavn")
"Göteborg not equals Goteborg:"; ("Göteborg" != "Goteborg")

"Latin extended tests completed"

"===== UNICODE RELATIONAL COMPARISON TESTS ====="

// Unicode characters in proper collation order
"a less than á:"; ("a" < "á")
"á less than b:"; ("á" < "b")
"n less than ñ:"; ("n" < "ñ")
"ñ less than o:"; ("ñ" < "o")

// Case sensitivity in Unicode
"A less than a:"; ("A" < "a")
"Hello less than hello:"; ("Hello" < "hello")

"Unicode relational tests completed"

"===== EMOJI AND SYMBOL TESTS ====="

// Basic emoji comparison
"apple emoji equals apple emoji:"; ("🍎" == "🍎")
"apple not equals banana emoji:"; ("🍎" != "🍌")
"waving hand equals waving hand:"; ("👋" == "👋")

// Emoji with skin tone modifiers (complex Unicode sequences)
"waving hand medium tone equals same:"; ("👋🏽" == "👋🏽")
"waving hand medium tone not equals default:"; ("👋🏽" != "👋")

// Symbol comparison
"copyright equals copyright:"; ("©" == "©")
"registered not equals copyright:"; ("®" != "©")

"Emoji and symbol tests completed"

"===== NON-LATIN SCRIPT TESTS ====="

// Greek characters
"alpha equals alpha:"; ("α" == "α")
"alpha not equals beta:"; ("α" != "β")
"alpha less than beta:"; ("α" < "β")

// Cyrillic characters
"cyrillic a equals cyrillic a:"; ("а" == "а")
"cyrillic a not equals cyrillic b:"; ("а" != "б")
"Moscow equals Moscow:"; ("Москва" == "Москва")

"Non-Latin script tests completed"

"===== EDGE CASES AND ERROR HANDLING ====="

// String vs other types (should still error)
// These operations should return error, not crash
"hello equals 42 (type error):"; ("hello" == 42)
"hello less than true (type error):"; ("hello" < true)

"Edge case tests completed"

"===== COMPLEX UNICODE TESTS ====="

// Mixed scripts in same string
"Hello世界 equals Hello世界:"; ("Hello世界" == "Hello世界")
"café東京 equals café東京:"; ("café東京" == "café東京")

// Longer Unicode strings
"long English string equals same:"; ("The quick brown fox jumps over the lazy dog" == 
 "The quick brown fox jumps over the lazy dog")

"long accented string equals same:"; ("Thé quick brown fôx jumps över the låzy dög" == 
 "Thé quick brown fôx jumps över the låzy dög")

// Number-like Unicode characters (should be treated as strings, not numbers)
"fullwidth 123 equals fullwidth 123:"; ("１２３" == "１２３")
"fullwidth 123 not equals ascii 123:"; ("１２３" != "123")

"Complex Unicode tests completed"

"===== PERFORMANCE TESTS (ASCII FAST PATH) ====="

// These should use the fast ASCII path
"long ASCII string equals same:"; ("abcdefghijklmnopqrstuvwxyz" == "abcdefghijklmnopqrstuvwxyz")

// These should use the Unicode path  
"long Unicode string equals same:"; ("abcdéfghijklmnöpqrstüvwxyz" == "abcdéfghijklmnöpqrstüvwxyz")

"Unicode tests completed"
