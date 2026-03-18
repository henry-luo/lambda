// test_math_optimize.ls — Test math box coalescing optimization
// Coverage: optimize.ls — coalesce (merge adjacent styled spans)

import opt: lambda.package.math.optimize

"===== MATH OPTIMIZE TESTS ====="

// ---- null passthrough ----
"1. null:"; opt.coalesce(null) == null

// ---- single element box (no merge needed) ----
let box1 = {
    element: <span class: "mord" "x">,
    height: 0.7, depth: 0.0, width: 0.5,
    type: "mord", italic: 0.0, skew: 0.0
}
let r1 = opt.coalesce(box1)
"2. single el:"; r1.element[0]
"3. single height:"; r1.height
"4. single type:"; r1.type

// ---- two mergeable spans ----
let box2 = {
    element: <span class: "ML__mathit"
        <span class: "mord" "x">
        <span class: "mord" "y">>,
    height: 0.7, depth: 0.0, width: 1.0,
    type: "mord", italic: 0.0, skew: 0.0
}
let r2 = opt.coalesce(box2)
"5. merged child count:"; len(r2.element)
"6. merged text:"; r2.element[0][0]

// ---- non-mergeable spans (different classes) ----
let box3 = {
    element: <span class: "ML__mathit"
        <span class: "mord" "x">
        <span class: "mbin" "+">>,
    height: 0.7, depth: 0.0, width: 1.2,
    type: "mord", italic: 0.0, skew: 0.0
}
let r3 = opt.coalesce(box3)
"7. no merge count:"; len(r3.element)

// ---- span with style (should not merge) ----
let box4 = {
    element: <span class: "ML__mathit"
        <span class: "mord", style: "color:red" "a">
        <span class: "mord" "b">>,
    height: 0.7, depth: 0.0, width: 1.0,
    type: "mord", italic: 0.0, skew: 0.0
}
let r4 = opt.coalesce(box4)
"8. styled no merge:"; len(r4.element)

// ---- preserves box metrics ----
let box5 = {
    element: <span class: "mord" "z">,
    height: 0.8, depth: 0.2, width: 0.6,
    type: "mord", italic: 0.1, skew: 0.05
}
let r5 = opt.coalesce(box5)
"9. preserve height:"; r5.height
"10. preserve depth:"; r5.depth
"11. preserve width:"; r5.width
"12. preserve italic:"; r5.italic
"13. preserve skew:"; r5.skew

// ---- three mergeable spans ----
let box6 = {
    element: <span class: "ML__cmr"
        <span class: "mord" "a">
        <span class: "mord" "b">
        <span class: "mord" "c">>,
    height: 0.7, depth: 0.0, width: 1.5,
    type: "mord", italic: 0.0, skew: 0.0
}
let r6 = opt.coalesce(box6)
"14. triple merge count:"; len(r6.element)
"15. triple merge text:"; r6.element[0][0]

"===== ALL MATH OPTIMIZE TESTS DONE ====="
