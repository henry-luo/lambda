// Test child-level query: expr[T] where T is a type
// Yields the run T* of direct attributes/children matching T (S8.2.4):
// null for none, the match for one, a list for more -- counted with `count(q)` (S8.3.3v3)

// === 1. Array child query ===
"--- array child query ---"
let arr = [1, "hello", 3, "world", true]
count(arr[int])          // 2
count(arr[string])       // 2
count(arr[bool])         // 1

// === 2. Array vs recursive query ===
"--- array child vs recursive ---"
let nested = [1, [2, 3], "a", [4, "b"]]
count(nested[int])       // 1 — only direct children
count(nested?int)        // 4 — recursive (1, 2, 3, 4) — now handles typed ArrayInt
count(nested[array])     // 2 — direct array children

// === 3. Map child query ===
"--- map child query ---"
let m = {name: "Alice", age: 30, active: true}
count(m[string])         // 1
count(m[int])            // 1
count(m[bool])           // 1

// === 4. Element child query ===
"--- element child query ---"
let el = <div class: "main",
    <p "hello">
    <img src: "photo.jpg">
    "some text">

count(el[string])        // 2 — "main" attr + "some text" text child
count(el[element])       // 2 — <p> and <img>

// === 5. Element child query by tag type ===
"--- element tag type query ---"
type p = <p>
type img_t = <img>

count(el[p])             // 1
count(el[img_t])         // 1

// === 6. Child query does NOT recurse ===
"--- no recursion ---"
let deep = <div
    <div id: "inner",
        <p "deep text">
    >
    <span "shallow">
>

type div_t = <div>

count(deep[div_t])       // 1 — inner div only, not self
count(deep[string])      // 0 — no direct string children (strings are inside <div> and <span>)
count(deep?string)       // 3 — recursive finds "inner", "deep text", "shallow"

// === 7. Chained child query ===
"--- chained child ---"
count(deep[div_t][p])    // 1 — finds <p> inside inner div via chaining

// === 8. Mixed child + recursive query ===
"--- mixed child + recursive ---"
let html = <html
    <head <title "Page">>
    <body
        <div <p "text1"> <p "text2">>
        <div <p "text3">>
    >
>
type body = <body>

count(html[body]?<p>)    // 3 — all <p> inside <body> (recursive)

// === 9. Empty results ===
"--- empty results ---"
count(arr[map])          // 0 — no maps in array
count(m[element])        // 0 — no elements in map

// === 10. Spreadable ===
"--- spreadable ---"
let x = [1, "a", 2, "b"]
let ints = x[int]
let strs = x[string]
len(ints)              // 2 — spreadable array of ints
len(strs)              // 2 — spreadable array of strings

// === 11. Typed array child query ===
"--- typed array child query ---"
let typed_arr = [10, 20, 30]
count(typed_arr[int])    // 3 — ArrayInt items match int type
count(typed_arr[float])  // 3 — ints are subtype of float
