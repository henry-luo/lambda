type Result = {value: string, column: int}
type Link = {value: int, next: Link?}

// the result of this forward call must retain its member contract.
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
    while (i < 20) {
        let item = result("test", i)
        total = total + forward_column(item.column)
        i = i + 1
    }
    let chain = link(3, link(2, null))
    print([total, chain.value, chain.next.value, chain.next.next])
}
