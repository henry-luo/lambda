// D2.5.2v3: persistent i64?/u64? Map fields own an inline TypedItem payload.
type I64Row = {value: i64?}
type U64Row = {value: u64?}

fn dynamic(value: any) any { value }

pn make_i64() I64Row {
    var row: I64Row = {value: null}
    row.value = dynamic(9007199254740995i64)
    row
}

pn make_u64() U64Row {
    var row: U64Row = {value: null}
    row.value = dynamic(18446744073709551615u64)
    row
}

fn overwrite_i64(value: i64) i64 { value }
fn overwrite_u64(value: u64) u64 { value }

pn main() {
    var signed: I64Row = make_i64()
    var unsigned: U64Row = make_u64()
    var literal_i64: I64Row = {value: 9007199254740989i64}
    var literal_u64: U64Row = {value: 18446744073709551613u64}
    let signed_scratch = overwrite_i64(9007199254740997i64)
    let unsigned_scratch = overwrite_u64(9007199254740999u64)

    var copy: I64Row = signed
    copy.value = dynamic(null)
    copy.value = dynamic(9007199254740991i64)
    let copy_scratch = overwrite_i64(9007199254740993i64)

    print(string([signed.value, unsigned.value, literal_i64.value, literal_u64.value,
        signed_scratch, unsigned_scratch]) ++ "\n")
    print(string([signed.value, copy.value, copy_scratch]) ++ "\n")
}
