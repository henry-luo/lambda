// PARKED 2026-10-07 — LR02-32: `tree[<child>]` is null instead of the lone match (S8.2.4v3).
// Test discovery skips `_` scripts. When fixed, rename this file to query_depth.ls
// and query_depth.expected.pending (the spec-correct output) to query_depth.expected.
// Test: Query Depth
// Layer: 3 | Category: boundary | Covers: deep nesting, wide trees, many results

// ===== Shallow query =====
let shallow = <root
    <item "a">
    <item "b">
    <item "c">>
count(shallow?<item>)

// ===== Deep nesting query =====
let deep = <l0
    <l1
        <l2
            <l3
                <l4
                    <target "found">>>>>>
deep?<target>
count(deep?<target>)

// ===== Wide tree query =====
let wide = <root
    <item "1">
    <item "2">
    <item "3">
    <item "4">
    <item "5">
    <item "6">
    <item "7">
    <item "8">
    <item "9">
    <item "10">>
count(wide?<item>)

// ===== Mixed depth query =====
let mixed = <root
    <item "shallow">
    <group
        <item "mid">
        <sub
            <item "deep">>>>
count(mixed?<item>)

// ===== Child query vs recursive query =====
let tree = <root
    <child "direct">
    <group
        <child "nested">>>
count(tree[<child>])
count(tree?<child>)

// ===== Query returning no matches =====
let no_match = <root
    <a "one">
    <b "two">>
count(no_match?<nonexistent>)

// ===== Self-inclusive query =====
let self_test = <items
    <items
        <item "inner">>
    <item "outer">>
self_test.?<items>

// ===== Query and transform =====
let data = <catalog
    <product name: "A", <price "10">>
    <product name: "B", <price "20">>
    <product name: "C", <price "30">>>
data?<product> |> ~.name

// ===== Nested query chain =====
let doc = <html
    <body
        <div class: "main",
            <p "paragraph 1">
            <p "paragraph 2">>
        <div class: "side",
            <p "sidebar">>>>
count(doc?<p>)
