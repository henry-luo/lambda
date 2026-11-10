// S11.4.10/D4.2.5: structural membership proves values, not field offsets.
// A write in the arm revokes this fact before a later receiving boundary.
type Box = {value: int}
type Alternative = Box | {text: string}

pn observed(record: any) int^ {
    if (record is Box) {
        return record.value
    }
    return 0
}

pn revoked(record: any) int^ {
    if (record is Box) {
        record.value = "bad"
        let field: int = record.value
        return field^
    }
    return 0
}

pn union_value(record: any) any {
    if (record is Alternative) { return record.value }
    return false
}

pn main() {
    print([observed({extra: true, value: 7}), revoked({value: 4}) is error,
        observed({value: "bad"})])
    print("\n")
    print([union_value({text: "x"}) == null, union_value({value: 8})])
    print("\n")
}
