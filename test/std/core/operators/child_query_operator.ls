// Test: Child Query Operator
// Layer: 2 | Category: operator | Covers: [T] child-level type query

// ===== Array child query =====
let arr = [1, "hello", 3, "world", true]
len(arr[int])
len(arr[string])
len(arr[bool])

// ===== Array child vs recursive =====
let nested = [1, [2, 3], "a", [4, "b"]]
len(nested[int])
len(nested?int)
len(nested[array])

// ===== Map child query =====
let m = {name: "Alice", age: 30, active: true}
len(m[string])
len(m[int])
len(m[bool])

// ===== Element child query =====
let el = <div class: "main";
    <p; "hello">
    <img src: "photo.jpg">
    "some text">
len(el[element])

// ===== Chained child query =====
let doc = <div;
    <div id: "inner";
        <p; "deep text">
    >
    <span; "shallow">
>
type div_t = <div>
type p_t = <p>
len(doc[div_t][p_t])

// ===== Empty results =====
len(arr[map])
len(m[element])

// ===== Mixed child + recursive =====
let html = <html;
    <head; <title; "Page">>
    <body;
        <div; <p; "text1"> <p; "text2">>
        <div; <p; "text3">>
    >
>
type body_t = <body>
len(html[body_t]?<p>)
