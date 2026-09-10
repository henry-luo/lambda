type Result = {text: string, count: int}

fn result(text: string, count: int) Result => {text: text, count: count}

fn render(depth: int) Result {
    if (depth <= 0) result("x", 1)
    else {
        let child = render(depth - 1)
        result("(" ++ child.text ++ ")", child.count + 1)
    }
}

fn project(depth: int) string {
    let rendered = render(depth)
    rendered.text ++ ":" ++ string(rendered.count)
}

// Both the boxed adapter and an escaping record preserve the public value.
let callback = result
let escaped = render(2);
[project(4), render(1).count, escaped, callback("z", 3)]
