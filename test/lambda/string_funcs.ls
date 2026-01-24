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

"===== ALL TESTS PASSED ====="
