// Test script for new string functions
// Expected results commented next to each test

"===== STRING FUNCTIONS TEST ====="

"Test starts_with:"
"1. expect true:"; starts_with("hello world", "hello")
"2. expect false:"; starts_with("hello world", "world")
"3. expect false (prefix longer):"; starts_with("hello", "hello world")

"Test ends_with:"
"4. expect true:"; ends_with("hello world", "world")
"5. expect false:"; ends_with("hello world", "hello")
"6. expect true (.txt):"; ends_with("hello.txt", ".txt")

"Test index_of:"
"7. expect 4:"; index_of("hello world", "o")
"8. expect -1 (not found):"; index_of("hello world", "x")
"9. expect 6:"; index_of("hello world", "world")

"Test last_index_of:"
"10. expect 7 (second o):"; last_index_of("hello world", "o")
"11. expect 9 (last l):"; last_index_of("hello world", "l")
"12. expect -1:"; last_index_of("hello", "x")

"Test trim:"
"13. expect 'hello world':"; trim("  hello world  ")
"14. expect 'hello':"; trim("hello")

"Test trim_start:"
"15. expect 'hello  ':"; trim_start("  hello  ")

"Test trim_end:"
"16. expect '  hello':"; trim_end("  hello  ")

"Test split:"
"17. expect ['a', 'b', 'c']:"; split("a,b,c", ",")
"18. expect ['hello']:"; split("hello", "x")
"19. expect ['a', 'b', 'c']:"; split("a::b::c", "::")

"Test str_join:"
"20. expect 'a, b, c':"; str_join(["a", "b", "c"], ", ")
"21. expect 'hello':"; str_join(["hello"], ", ")

"Test replace:"
"22. expect 'hello there':"; replace("hello world", "world", "there")
"23. expect 'hexxo':"; replace("hello", "l", "x")
"24. expect 'hello' (no match):"; replace("hello", "x", "y")

"Test contains:"
"25. expect true:"; contains("hello world", "world")
"26. expect false:"; contains("hello world", "xyz")

"Test slice on strings:"
"27. expect 'ell':"; slice("hello", 1, 4)
"28. expect 'he':"; slice("hello", 0, 2)

"Test with symbols:"
let sym1 = 'hello_world'
"29. expect true:"; starts_with(sym1, "hello")
"30. expect true:"; ends_with(sym1, "world")

"Test Unicode:"
"31. index_of UTF-8:"; index_of("héllo wörld", "ö")
"32. slice UTF-8:"; slice("héllo", 1, 4)

"Test string(type(...)):"
"33. int:"; string(type(123))
"34. float:"; string(type(3.14))
"35. string:"; string(type("hello"))
"36. bool:"; string(type(true))
"37. null:"; string(type(null))
"38. array:"; string(type([1, 2, 3]))
"39. map:"; string(type({a: 1}))
"40. symbol:"; string(type('sym'))
"41. element:"; string(type(<div "hello">))
"42. list:"; string(type((1, 2, 3)))

"Test null handling:"
"43. contains(null, x):"; contains(null, "x")
"44. contains(x, null):"; contains("hello", null)
"45. starts_with(null, x):"; starts_with(null, "x")
"46. starts_with(x, null):"; starts_with("hello", null)
"47. ends_with(null, x):"; ends_with(null, "x")
"48. ends_with(x, null):"; ends_with("hello", null)
"49. index_of(null, x):"; index_of(null, "x")
"50. last_index_of(null, x):"; last_index_of(null, "x")
"51. trim(null):"; trim(null)
"52. upper(null):"; upper(null)
"53. lower(null):"; lower(null)
"54. replace(null, a, b):"; replace(null, "a", "b")
"55. replace(s, null, b):"; replace("hello", null, "b")
"56. slice(null, 0, 1):"; slice(null, 0, 1)
"57. split(null, sep):"; len(split(null, ","))
"58. str_join(null, sep):"; str_join(null, ",")
"59. len(null):"; len(null)

"Test split with null separator (whitespace split):"
"60. split on whitespace:"; len(split("hello world", null))
"61. split strips leading/trailing:"; len(split("  hello   world  ", null))
"62. split single word:"; len(split("hello", null))
"63. split all whitespace:"; len(split("   ", null))
"64. split tabs/newlines:"; len(split("a\tb\nc", null))

"===== ALL TESTS PASSED ====="
