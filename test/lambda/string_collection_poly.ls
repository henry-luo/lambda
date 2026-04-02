// Test polymorphic collection support for contains, index_of, last_index_of
// and native String* variants for starts_with, ends_with, ord

"===== contains: string ====="
"str contains sub:"; contains("hello world", "world")
"str not contains:"; contains("hello world", "xyz")
"str contains single char:"; contains("hello", "h")

"===== contains: list ====="
"list contains int:"; contains([1, 2, 3], 2)
"list not contains:"; contains([1, 2, 3], 4)
"list contains string:"; contains(["a", "b", "c"], "b")
"list contains bool:"; contains([true, false], false)
"empty list:"; contains([], 1)

"===== index_of: string ====="
"str index:"; index_of("hello world", "world")
"str not found:"; index_of("hello world", "xyz")

"===== index_of: list ====="
"list index int:"; index_of([10, 20, 30], 20)
"list index not found:"; index_of([10, 20, 30], 40)
"list index string:"; index_of(["a", "b", "c"], "c")
"list index first:"; index_of([1, 2, 1, 2], 2)

"===== last_index_of: string ====="
"str last index:"; last_index_of("hello world hello", "hello")
"str last not found:"; last_index_of("hello", "xyz")

"===== last_index_of: list ====="
"list last index:"; last_index_of([1, 2, 3, 2, 1], 2)
"list last not found:"; last_index_of([1, 2, 3], 4)
"list last first el:"; last_index_of([1, 2, 3, 2, 1], 1)

"===== starts_with: string (native path) ====="
"sw basic:"; starts_with("hello world", "hello")
"sw no match:"; starts_with("hello world", "world")
"sw empty prefix:"; starts_with("hello", "")

"===== ends_with: string (native path) ====="
"ew basic:"; ends_with("hello world", "world")
"ew no match:"; ends_with("hello world", "hello")
"ew empty suffix:"; ends_with("hello", "")

"===== ord: string (native path) ====="
"ord A:"; ord("A")
"ord a:"; ord("a")
"ord emoji:"; ord("😀")

"===== chained: index_of on list ====="
"find index in list:"; index_of(["apple", "banana", "cherry"], "banana")
