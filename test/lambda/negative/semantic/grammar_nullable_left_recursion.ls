// GM8: a left-recursive alternative must begin with the rule itself
grammar g {
    c = "q"?, c, "!" | "c"
    space := " "*
}
1
