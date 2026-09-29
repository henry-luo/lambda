// S7.1.1v3: two indexed characters compare as bytes when both are ASCII.
pn same_at(left: string, right: string, li: int, ri: int) bool {
    return left[li] == right[ri]
}

pn main() {
    print(same_at("A", "A", 0, 0)); print(" ")
    print(same_at("A", "B", 0, 0)); print(" ")
    print(same_at("é", "é", 0, 0)); print(" ")
    print(same_at("é", "e", 0, 0)); print(" ")
    print(same_at("", "", 0, 0)); print(" ")
    print(same_at("", "x", 0, 0)); print(" ")
    print(same_at("x", "x", -1, 0)); print(" ")
    print(same_at("😀", "😀", 0, 0)); print("\n")
}
