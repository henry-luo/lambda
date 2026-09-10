type Value = {value: int} | {text: string}
fn read(values: Value[]) int => len(values)
pn checked(values) any^ { return read(values) }

pn main() {
    let values = [{value: 1}, {text: "two"}]
    var total = 0
    var i = 0
    while (i < 20) {
        total = total + read(values)
        i = i + 1
    }
    var alias = values
    alias.push({invalid: true})
    var failed = false
    checked(alias) ^ { failed = true }
    var typed: Value[] = values
    let before = typed
    var rejected = false
    typed.push({invalid: true}) ^ { rejected = true }
    typed.push({value: 3})
    print([total, len(values), len(alias), failed,
        rejected, len(before), len(typed)])
}
