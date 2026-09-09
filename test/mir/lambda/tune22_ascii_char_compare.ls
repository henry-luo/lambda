// T22-6: string indexing compared with a one-byte literal keeps the character
// semantic boundary while avoiding an intermediate String result.
pn char_is_zero(text: string, index: int) bool {
    text[index] == "0"
}

pn main() {
    print(char_is_zero("0x", 0)); print(" ")
    print(char_is_zero("0x", 1)); print(" ")
    print(char_is_zero("é0", 0)); print(" ")
    print(char_is_zero("é0", 1)); print(" ")
    print(char_is_zero("", 0)); print(" ")
    print(char_is_zero("0x", -1)); print("\n")
}
