type Record = {count: int, label: string}
fn consume(record: Record) int => record.count
pn rejected() any^ { return consume({count: 3.5, label: "wrong"}) }
pn main() {
    var failed = false
    rejected() ^ { failed = true }
    print(failed)
}
