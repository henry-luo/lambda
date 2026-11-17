// S7.10.7: text split stays string[]; open overloads return array or null.
fn tune22_text_split(source: string) string[] {
    let words: string[] = split(source, ",")
    words
}

fn tune22_open_split(source, separator) array? {
    split(source, separator)
}

{
    text: tune22_text_split("a,b,c"),
    array_absent: tune22_open_split([1, 2, 3], 0) is null,
    text_absent: tune22_open_split("a,b", 0) is null,
    null_source: tune22_open_split(null, 0),
    valid_empty_source: tune22_open_split(null, ",")
}
