// Deep multi-level user function call test for stack trace

let level5 = (x) => {
    error("Error at level 5")
}

let level4 = (x) => {
    level5(x + 1)
}

let level3 = (x) => {
    level4(x + 1)
}

let level2 = (x) => {
    level3(x + 1)
}

let level1 = (x) => {
    level2(x + 1)
}

level1(1)
