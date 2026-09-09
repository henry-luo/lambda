// T22-4: text split has a precise string[] success contract; open and ArrayNum
// overloads retain their ItemError result channel.
fn tune22_text_split(source: string) string[] {
    let words: string[] = split(source, ",")
    words
}

fn tune22_open_split(source, separator) array | error {
    split(source, separator)
}

{
    text: tune22_text_split("a,b,c"),
    array_error: tune22_open_split([1, 2, 3], 0) is error,
    text_error: tune22_open_split("a,b", 0) is error,
    null_source: tune22_open_split(null, 0)
}
