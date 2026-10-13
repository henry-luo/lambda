// A self call inside a for comprehension is not a tail call, even when the
// comprehension is the function's result and the function has a real tail
// call elsewhere. The JIT's TCO used to turn it into a jump back to the
// function's start, returning one item's value instead of the comprehension.

// a recursive scan with a tail call on one branch and comprehensions on others
fn scan(n) {
    if (n is array or n is list) [for (item in n, key in scan(item)) key]
    else if (not (n is element)) []
    else if (name(n) == 'wrapped') scan(content(n) or [])
    else {
        let own = if (name(n) == 'hit') [string(n.id)] else [];
        own ++ [for (child in content(n) or [], found in scan(child)) found]
    }
}

fn mapped(n) {
    if (n is array) [for (item in n) mapped(item)]
    else if (n < 0) mapped(0 - n)
    else n * 10
}

let miss = <miss <inner "x">>
let first = <hit id: "a">
let second = <hit id: "b">
"scan, empty first:"; scan([miss, first])
"scan, all:"; scan([first, miss, <wrapped second>])
let nested = <wrapped <deep second>>
"scan, element:"; scan(<root miss first nested>)
"mapped:"; mapped([1, -2, [3, -4]])
