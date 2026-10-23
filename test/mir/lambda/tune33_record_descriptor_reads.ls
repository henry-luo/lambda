// D2.6.4/S9.1.3: a guarded descriptor must follow in-place retags and COW.
pn new_record() { return {link: null, flag: false, value: 1} }
pn read_record(record) { return [record.link, record.flag, record.value] }
pn nested_record(record) { return (record.link).value }
pn main() {
    var record = new_record()
    let saved = record
    print(read_record(record)); print("\n")
    record.link = {value: 7}
    record.flag = true
    print([nested_record(record), read_record(saved)]); print("\n")
    record.link = 9
    record.flag = "changed"
    print(read_record(record)); print("\n")
    record.link = [2, 3]
    record.value = "wide"
    print(read_record(record)); print("\n")
    print(read_record({value: 4, flag: null, extra: 8, link: "other"})); print("\n")
}
