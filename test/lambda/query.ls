// Test query operator ? and .? semantics

// === 1. Basic type query on list ===
"--- type query ---"
let data = (1, "hello", 2, "world", 3.14)
len(data?int)          // 2 (finds 1, 2)
len(data?string)       // 2 (finds "hello", "world")
len(data?float)        // 3 (int is subtype of float, finds 1, 2, 3.14)

// === 2. Self-inclusive .? vs non-inclusive ? on scalars ===
"--- self-inclusive scalar ---"
len(42?int)            // 0 — ? is not self-inclusive
len(42.?int)           // 1 — .? is self-inclusive
(42.?int)[0]           // 42
len("hello"?string)    // 0 — not self-inclusive
len("hello".?string)   // 1 — self-inclusive

// === 3. Element query ===
"--- element query ---"
let page = <div class: "main";
    <p; "text1">
    <span; "text2">
    <div id: "inner"; <p; "text3">>
>

len(page?<p>)          // 2 (finds both <p>)
len(page?<div>)        // 1 (inner div only, not self)
len(page.?<div>)       // 2 (self-inclusive: both divs)
len(page?<span>)       // 1

// === 4. Element query recurses with .? ===
"--- .? recurses ---"
let deep = <div;
    <div;
        <p; "deep">
    >
>
len(deep?<p>)          // 1 (recursive, not self-inclusive)
len(deep.?<p>)         // 1 (recursive, self-inclusive, but <p> is nested)
len(deep.?<div>)       // 2 (.? includes self + inner div)
len(deep?<div>)        // 1 (? excludes self, only inner div)

// === 5. String query in elements ===
"--- string in elements ---"
len(page?string)       // 5 ("main", "text1", "text2", "inner", "text3")

// === 6. Map query ===
"--- map query ---"
let m = {a: 1, b: "two", c: 3, d: {e: 4}}
len(m?int)             // 3 (1, 3, 4)
len(m?string)          // 1 ("two")

// === 7. Array query ===
"--- array query ---"
let arr = [1, "a", [2, "b"], 3]
len(arr?int)           // 3 (1, 2, 3)
len(arr?string)        // 2 ("a", "b")

// === 8. Chained query ===
"--- chained ---"
len(page?<div>?<p>)    // 1 (<p> inside inner div)

// === 9. Query with pipe ===
"--- query + pipe ---"
page?<p> | len(~)      // content length of each <p>
