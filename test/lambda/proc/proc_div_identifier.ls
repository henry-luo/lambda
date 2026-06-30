pn project(div) {
    div[0] = 42
    print("param-assign=")
    print(div[0])
    print("\n")
}

pn main() {
    var div = [1, 2, 3];
    div[1] = 99
    print("local-assign=")
    print(div[1])
    print("\n")
    project(div)
}
