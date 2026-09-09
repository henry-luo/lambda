// T22-4: only the total text overload proves the string[] declaration boundary.
pn tune22_split_words(text: string) string[] {
    return split(text, ",")
}

pn main() {
    let words: string[] = split("red,green,blue", ",")
    print(len(words)); print("\n")
    print(len(tune22_split_words("red,green,blue"))); print("\n")
}
