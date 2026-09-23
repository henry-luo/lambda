// Test: Query Operator
// Layer: 2 | Category: operator | Covers: ? recursive, .? self-inclusive

// ===== Type query on list =====
let data = [1, "hello", 2, "world", 3.14]
count(data?int)
count(data?string)
count(data?float)

// ===== Self-inclusive .? vs non-inclusive ? =====
count(42?int)
count(42.?int);
42.?int
count("hello"?string)
count("hello".?string)

// ===== Element query =====
let page = <div class: "main",
    <p "text1">
    <span "text2">
    <div id: "inner", <p "text3">>
>
count(page?<p>)
count(page?<div>)
count(page.?<div>)
count(page?<span>)

// ===== Deep recursion =====
let deep = <div
    <div
        <p "deep">
    >
>
count(deep?<p>)
count(deep.?<div>)
count(deep?<div>)

// ===== Map query =====
let m = {a: 1, b: "two", c: 3, d: {e: 4}}
count(m?int)
count(m?string)

// ===== Array query =====
let arr = [1, "a", [2, "b"], 3]
count(arr?int)
count(arr?string)

// ===== Chained query =====
count(page?<div>?<p>)
