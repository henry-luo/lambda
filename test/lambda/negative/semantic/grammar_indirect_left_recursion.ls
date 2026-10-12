// GM8: left recursion through another rule is rejected at load time
grammar g {
    a = b, "x" | "y"
    b = a, "z" | "w"
    space := " "*
}
1
