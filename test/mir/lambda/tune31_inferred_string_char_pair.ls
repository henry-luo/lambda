// Tune32 T32-2: exact inferred string lanes reuse the shared pair helper.

pn tune31_inferred_char_pair(left, left_index: int, right, right_index: int) bool {
    left[left_index] == right[right_index]
}

pn main() {
    print(tune31_inferred_char_pair("ab", 1, "xb", 1)); print("\n")
}
