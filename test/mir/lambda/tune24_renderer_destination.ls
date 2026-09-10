type Result = {text: string, count: int}

fn result(text: string, count: int) Result => {text: text, count: count}

fn parts(index: int, count: int, depth: int, prefix: string) Result {
    if (index >= count) result(prefix, index)
    else {
        let child = render(depth)
        parts(index + 1, count, depth, prefix ++ child.text)
    }
}

fn render(depth: int) Result {
    if (depth <= 0) result("λ🙂", 1)
    else {
        let child = parts(0, 3, depth - 1, "[")
        result(child.text, child.count)
    }
}

// Observing, duplicating or dropping a prefix forbids draining it early.
fn observed(prefix: string) Result => result(prefix, len(prefix))
fn duplicated(prefix: string) Result => {text: prefix ++ prefix, count: 2}
fn dropped(prefix: string, keep: bool) Result { result(if (keep) prefix else "z", 1) }

fn retained(depth: int) string {
    let child = render(depth)
    child.text ++ ":" ++ child.text
}

// The child exists on both paths, but only one path consumes its text.
fn selected(keep: bool) Result {
    let child = render(0)
    if (keep) result(child.text, 1) else result("z", 2)
}

let callback = render;
[render(1), parts(0, 2000, 0, "p").count,
 len(parts(0, 2000, 0, "p").text), observed("abc"), duplicated("ab"),
 dropped("abc", false), retained(0), callback(0), selected(true), selected(false)]
