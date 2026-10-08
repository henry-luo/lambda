// Test: Query Expressions
// Layer: 2 | Category: statement | Covers: ?T recursive, .?T self-inclusive, [T] child, chaining

type child = <child>;
type a = <a>;
type b = <b>;
type c = <c>;

// ===== Basic recursive query =====
let tree = <root
    <item "one">
    <group
        <item "two">
        <item "three">>>
tree?<item>

// ===== Self-inclusive query =====
let doc = <items
    <item "first">
    <item "second">>
doc.?<items>

// ===== Child-level query =====
let parent = <parent
    <child "a">
    <child "b">
    <other "c">>
parent[child]

// ===== Nested query =====
let deep = <root
    <level1
        <level2
            <target "found">>>>
deep?<target>

// ===== Query with attribute filter =====
let data = <data
    <item status: "active", "one">
    <item status: "inactive", "two">
    <item status: "active", "three">>
data?<item> |: ~.status == "active" |> string(~[0])

// ===== Query on complex tree =====
let html = <html
    <head
        <title "Page">>
    <body
        <div
            <p "Hello">
            <p "World">>
        <div
            <p "More">>>>
html?<p> |> string(~[0])

// ===== Multiple child types =====
let mix = <container
    <a "alpha">
    <b "beta">
    <a "alpha2">
    <c "gamma">>
mix[a]
mix[b]
mix[c]

// ===== Query result count =====
let items = <list
    <item "1">
    <item "2">
    <item "3">
    <item "4">
    <item "5">>
count(items?<item>)
