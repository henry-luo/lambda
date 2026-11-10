// S7.1.2/S7.9.3: a checked text slice preserves its string success domain.
fn bracket(text: string, end: any) string^ => "[" ++ slice(text, 0, end) ++ "]"

pn main() {
    print([bracket("abc", 2), bracket("abc", "bad") is error, bracket("xyz", 2),
        bracket("abc", null), bracket("abc", 1.5) is error, bracket("abc", -2)])
}
