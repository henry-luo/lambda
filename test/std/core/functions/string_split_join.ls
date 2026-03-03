// Test: String Split and Join
// Layer: 3 | Category: function | Covers: split, join

[split("a,b,c", ",")]
[split("hello world", " ")]
[split("one", ",")]
join(["a", "b", "c"], ", ")
join(["hello", "world"], " ")
join(["one"], ",")
join([], ",")
