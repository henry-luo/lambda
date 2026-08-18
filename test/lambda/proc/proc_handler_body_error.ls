pn fail() int^ { raise error("body") }

pn wrapper() any^ {
    var value = null
    fail() ^ { value = ^ }
    fail() ^ { value = fail() }
}

pn main() {
    wrapper() ^ { print(^.message) }
}
