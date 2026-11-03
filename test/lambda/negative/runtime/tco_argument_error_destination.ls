// S7.7.3: an argument error bypasses scalar-record destination writes.
type Result = {text: string, count: int}
fn result(text: string, count: int) Result => {text: text, count: count}
fn render(depth) Result^ => result(depth, 1)
fn parts(index: int, count: int, depth, prefix: string) Result^ {
    if (index >= count) result(prefix, index)
    else {
        let child: Result^ = render(depth)
        parts(index + 1, count, depth, prefix ++ child.text)
    }
}
parts(0, 1, 1, "[")^
