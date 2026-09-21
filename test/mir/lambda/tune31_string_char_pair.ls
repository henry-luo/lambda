// Tune31 Phase III B2: two typed indexed characters compare without results.

pn tune31_char_pair_equal(left: string, left_index: int,
        right: string, right_index: int) bool {
    left[left_index] == right[right_index]
}

pn main() {
    print(tune31_char_pair_equal("ab", 1, "xb", 1)); print(" ")
    print(tune31_char_pair_equal("éx", 0, "éy", 0)); print(" ")
    print(tune31_char_pair_equal("éx", 0, "ey", 0)); print(" ")
    print(tune31_char_pair_equal("x", -1, "y", -1)); print(" ")
    print(tune31_char_pair_equal("x", 4, "y", 0)); print("\n")
}
