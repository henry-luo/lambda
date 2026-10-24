// D3.4.6/S9.1.2: consume live descriptors and preserve COW on stores.
pn make(key, child) { return {key: key, child: child} }
pn compare(record, key: float) { return [key < record.key, key > record.key] }
pn update(var record, value) { record.key = value; return record.key }
pn snapshot(value) {
    var record = make(1.0, null)
    let old = record
    let actual = update(record, value)
    return [old.key, record.key, actual]
}
pn replace(var record) { record = make(4.0, null); return 5.0 }
pn ordered_store() {
    var record = make(1.0, null)
    let old = record
    record.key = replace(record)
    return [old.key, record.key]
}
pn main() {
    print([compare(make(3.0, null), 2.0), compare(make(1.0, null), 2.0)]); print("\n")
    print([compare(make(2.0, null), 2.0), compare(make(3, null), 2.0)]); print("\n")
    print([compare(make(null, null), 2.0), compare({other: 3.0}, 2.0)]); print("\n")
    print([snapshot("changed"), snapshot(null), snapshot({leaf: 9})]); print("\n")
    print(ordered_store()); print("\n")
}
