fn append_value(prefix: array, value: any) array => prefix ++ [value]

pn main() {
    let base: array = [1, "x"]
    let appended = append_value(base, 3)
    print(len(base)); print(" "); print(len(appended)); print(" ")
    print(appended[2]); print("\n")

    var changed: array = appended
    changed[0] = 9
    print(base[0]); print(" "); print(appended[0]); print(" ")
    print(changed[0]); print("\n")

    let spliced = append_value(base, (4, 5))
    print(len(spliced)); print(" "); print(spliced[2]); print(" ")
    print(spliced[3]); print("\n")

    let numeric = append_value(base, [6, 7])
    print(len(numeric)); print(" "); print(numeric[2] is array); print("\n")
    let numeric_base = append_value([1, 2], 3)
    print(len(numeric_base)); print(" "); print(numeric_base[0]); print(" ")
    print(numeric_base[2]); print("\n")

    let wide_float = append_value(base, 123456789.125)
    print(wide_float[2] == 123456789.125); print(" ")
    print(base[0]); print("\n")
    let wide_base: array = [123456789.125, "x"]
    let copied_wide = append_value(wide_base, 4)
    print(copied_wide[0] == 123456789.125); print(" ")
    print(wide_base[0] == 123456789.125); print("\n")

    let record = {v: 1}
    let record_base: array = [record, "x"]
    let record_result = append_value(record_base, record)
    var record_changed: array = record_result
    record_changed[0].v = 9
    print(record_base[0].v); print(" "); print(record_result[0].v); print(" ")
    print(record_changed[0].v); print("\n")
}
