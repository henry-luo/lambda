if ([1, 2, 3] gt 0) "mask branch" else "no";
if ([1, 2, 3]) "container branch" else "no";
for (x in [1, 2] where ([1, 2] gt 0)) x

pn lint_while() {
    var done = false
    while ([1, 2, 3] gt 0) {
        done = true
        break
    }
}
