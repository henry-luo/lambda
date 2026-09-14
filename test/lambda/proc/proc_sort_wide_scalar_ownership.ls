fn sort_dynamic(value: any) any {
    sort(value)
}

fn sort_desc_dynamic(value: any) any {
    sort(value, "desc")
}

fn sort_key_dynamic(value: any) any {
    sort(value, (item) => item)
}

pn main() {
    var source_i64: i64?[] = [9007199254740995i64, 43i64]
    let sorted_i64: any = sort_dynamic(source_i64)
    let descending_i64: any = sort_desc_dynamic(source_i64)
    let keyed_i64: any = sort_key_dynamic(source_i64)
    source_i64[0] = 9007199254740997i64

    var source_u64: u64?[] = [18446744073709551615u64, 44u64]
    let sorted_u64: any = sort_dynamic(source_u64)
    source_u64[0] = 9007199254740999u64

    print(string([sorted_i64[1], descending_i64[0], keyed_i64[1], sorted_u64[1]]) ++ "\n")
}
