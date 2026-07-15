pn leaf(value) {
    sleep(1)^
    return value
}
pn level5(value) { return leaf(value) }
pn level4(value) { return level5(value) }
pn level3(value) { return level4(value) }
pn level2(value) { return level3(value) }
pn level1(value) { return level2(value) }

pn main() {
    print(level1(73))
}
