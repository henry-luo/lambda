pn last_code(text: string, begin: int, stop: int) int {
    var code: int = 0
    var index: int = begin
    while (index < stop) {
        code = ord(text[index])
        index = index + 1
    }
    code
}

pn has_missing(text: string, begin: int, stop: int) bool {
    var missing: bool = false
    var index: int = begin
    while (index < stop) {
        missing = missing or (ord(text[index]) == null)
        index = index + 1
    }
    missing
}

pn fold_digits(text: string) int {
    var value: int = 0
    var index: int = 0
    let stop: int = len(text)
    while (index < stop) {
        value = value * 10 + ord(text[index]) - 48
        index = index + 1
    }
    value
}

pn skip_spaces(line: string, begin: int, stop: int) int {
    var index: int = begin
    while (index < stop and line[index] == " ") {
        index = index + 1
    }
    index
}

pn scan_word(line: string, begin: int, stop: int) int {
    var index: int = begin
    while (index < stop and line[index] != " ") {
        index = index + 1
    }
    index
}

pn changing_source(line: string) int {
    var index: int = 0
    let stop: int = 2
    while (index < stop and line[index] == "a") {
        line = "bb"
        index = index + 1
    }
    index
}

pn main() {
    print(last_code("123", 0, 3)); print(" ")
    print(last_code("é", 0, 1)); print(" ")
    print(has_missing("7", 0, 2)); print(" ")
    print(has_missing("9", -1, 1)); print(" ")
    print(fold_digits("99999999999999999999")); print(" ")
    print(skip_spaces("   x", 0, 4)); print(" ")
    print(skip_spaces("é ", 0, 2)); print(" ")
    print(skip_spaces(" ", -1, 1)); print(" ")
    print(scan_word("abc", 0, 5)); print(" ")
    print(scan_word("é x", 0, 3)); print(" ")
    print(changing_source("aa")); print("\n")
}
