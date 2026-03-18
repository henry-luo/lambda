// Test: Query Operator
// Layer: 2 | Category: operator | Covers: ? recursive, .? self-inclusive

// ===== Type query on list =====
let data = [1, "hello", 2, "world", 3.14]
len(data?int)
len(data?string)
len(data?float)

// ===== Self-inclusive .? vs non-inclusive ? =====
len(42?int)
len(42.?int)
(42.?int)[0]
len("hello"?string)
len("hello".?string)

// ===== Element query =====
let page = <div class: "main";
    <p; "text1">
    <span; "text2">
    <div id: "inner"; <p; "text3">>
>
len(page?<p>)
len(page?<div>)
len(page.?<div>)
len(page?<span>)

// ===== Deep recursion =====
let deep = <div;
    <div;
        <p; "deep">
    >
>
len(deep?<p>)
len(deep.?<div>)
len(deep?<div>)

// ===== Map query =====
let m = {a: 1, b: "two", c: 3, d: {e: 4}}
len(m?int)
len(m?string)

// ===== Array query =====
let arr = [1, "a", [2, "b"], 3]
len(arr?int)
len(arr?string)

// ===== Chained query =====
len(page?<div>?<p>)
