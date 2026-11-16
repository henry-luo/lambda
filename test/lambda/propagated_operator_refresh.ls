// S7.5.1v6/S3.4: refreshed recursive and forward calls keep only normal outcomes after ^.
fn same(a, b, depth=0) bool^ {
    if (depth > 3) raise error("depth")
    if (a is array) {
        if (not (b is array) or len(a)^ != len(b)^) { return false }
        var i = 0
        while (i < len(a)^) {
            if (not (same(a[i],b[i],depth+1)^)) { return false }
            i = i+1
        }
        return true
    }
    return a == b
}
fn forward(value) bool^ {
    var i = 0
    while (i < 1) {
        if (not (later(value)^) or (later(value)^ and false)) { return false }
        if (later_number(value)^+1 < 1) { return false }
        i = i+1
    }
    return true
}
fn later(value) bool^ => if (value < 0) raise error("negative") else value > 0
fn later_number(value) int^ => if (value < 0) raise error("negative") else value
let deep = same([[[[1]]]],[[[[1]]]]) ^ { ^ }
let negative = forward(-1) ^ { ^ };
[same([[1,2],[]],[[1,2],[]])^,same([[1]],[[2]])^,deep is error,
    forward(1)^,forward(0)^,negative is error]
