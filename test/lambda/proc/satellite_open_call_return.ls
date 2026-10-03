// A native-int function whose body ends in a direct call to another module
// function. In a satellite image that callee lies outside the image, so the
// call is a dynamic edge yielding an `any` Item, and narrowing it to the int
// return lane is the declared-return admission. The lowering used to request
// a plain representation move and aborted the process once `forward_column`
// compiled under AUTO. The loop runs long enough for that compile to happen.
type Result = {value: string, column: int}
type Link = {value: int, next: Link?}

fn forward_column(value: int) int {
    let rendered = result("forward", value)
    column({value: rendered.value, column: rendered.column})
}

fn result(value: string, column: int) Result => {value: value, column: column}
fn column(value: Result) int => value.column
fn link(value: int, next: Link?) Link => {value: value, next: next}

pn main() {
    var total = 0
    var i = 0
    while (i < 20000) {
        let item = result("test", i)
        total = total + forward_column(item.column)
        i = i + 1
    }
    let chain = link(3, link(2, null))
    print([total, chain.value, chain.next.value, chain.next.next])
}
