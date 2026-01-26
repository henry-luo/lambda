// Deep multi-level user function call test for stack trace

let level5 = fn(x) {
    error("Error at level 5")
}

let level4 = fn(x) {
    level5(x + 1)
}

let level3 = fn(x) {
    level4(x + 1)
}

let level2 = fn(x) {
    level3(x + 1)
}

let level1 = fn(x) {
    level2(x + 1)
}

level1(1)
