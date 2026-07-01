pn choose(flag) {
    var offset = 10
    if (flag) {
        offset + 1
    } else {
        offset + 2
    }
}

pn choose_nested(flag) {
    var offset = 20
    if (flag) {
        if (offset > 10) {
            offset + 3
        } else {
            offset + 4
        }
    } else {
        offset + 5
    }
}

pn main() {
    print([choose(true), choose(false), choose_nested(true), choose_nested(false)])
}
