// Test query operator ? and .? semantics

// === 1. Basic type query on list ===
"--- type query ---"
let data = [1, "hello", 2, "world", 3.14]
count(data?int)          // 2 (finds 1, 2)
count(data?string)       // 2 (finds "hello", "world")
count(data?float)        // 3 (int is subtype of float, finds 1, 2, 3.14)

// === 2. Self-inclusive .? vs non-inclusive ? on scalars ===
"--- self-inclusive scalar ---"
count(42?int)            // 0 — ? is not self-inclusive
count(42.?int); // 1 — .? is self-inclusive
42.?int                // 42 — the lone match is the value itself
count("hello"?string)    // 0 — not self-inclusive
count("hello".?string)   // 1 — self-inclusive

// === 3. Element query ===
"--- element query ---"
let page = <div class: "main",
    <p "text1">
    <span "text2">
    <div id: "inner", <p "text3">>
>

count(page?<p>)          // 2 (finds both <p>)
count(page?<div>)        // 1 (inner div only, not self)
count(page.?<div>)       // 2 (self-inclusive: both divs)
count(page?<span>)       // 1

// === 4. Element query recurses with .? ===
"--- .? recurses ---"
let deep = <div
    <div
        <p "deep">
    >
>
count(deep?<p>)          // 1 (recursive, not self-inclusive)
count(deep.?<p>)         // 1 (recursive, self-inclusive, but <p> is nested)
count(deep.?<div>)       // 2 (.? includes self + inner div)
count(deep?<div>)        // 1 (? excludes self, only inner div)

// === 5. String query in elements ===
"--- string in elements ---"
count(page?string)       // 5 ("main", "text1", "text2", "inner", "text3")

// === 6. Map query ===
"--- map query ---"
let m = {a: 1, b: "two", c: 3, d: {e: 4}}
count(m?int)             // 3 (1, 3, 4)
count(m?string)          // 1 ("two")

// === 7. Array query ===
"--- array query ---"
let arr = [1, "a", [2, "b"], 3]
count(arr?int)           // 3 (1, 2, 3)
count(arr?string)        // 2 ("a", "b")

// === 8. Chained query ===
"--- chained ---"
count(page?<div>?<p>)    // 1 (<p> inside inner div)

// === 9. Query with pipe ===
"--- query + pipe ---"
page?<p> |> len(~)      // content length of each <p>

// === 10. Query binds inside an unparenthesized arrow body ===
"--- query precedence ---"
let query_fn = () => [1, "a", 2]?int
type(query_fn)
query_fn()
