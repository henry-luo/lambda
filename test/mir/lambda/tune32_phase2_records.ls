pn tune32_phase2_record(x, y, child) {
    return {x: x, y: y, child: child}
}

pn tune32_phase2_record_read(record) {
    return record.y
}

pn tune32_phase2_record_effect(mark, value) {
    print(mark)
    return value
}

pn tune32_phase2_record_formula(record) {
    return record.x * 2.0 + record.y
}

pn tune32_phase2_record_null(record) {
    return record.child == null
}

pn main() {
    var record = tune32_phase2_record(true, 1.25, {value: "child"})
    let snapshot = record
    print(tune32_phase2_record_read(record) ++ "\n")
    print(record.child.value ++ "\n")
    record.y = "changed"
    print(tune32_phase2_record_read(record) ++ "\n")
    print(tune32_phase2_record_read(snapshot) ++ "\n")
    print((tune32_phase2_record_read({other: 9}) == null) ++ "\n")
    print((tune32_phase2_record_read(null) == null) ++ "\n")
    print((tune32_phase2_record_read(7) == null) ++ "\n")
    let ordered = {x: tune32_phase2_record_effect("A", 1),
        y: tune32_phase2_record_effect("B", "two"),
        child: tune32_phase2_record_effect("C", null)}
    print(ordered.y ++ "\n")
    let selected = [record, snapshot]
    print(selected[1].y ++ "\n")
    let wide = tune32_phase2_record(9223372036854775807i64, 1.5, null)
    let allocate = [tune32_phase2_record(0, 0.0, null),
        tune32_phase2_record(1, 1.0, null)]
    print(wide.x ++ "\n")
    print(wide.y ++ "\n")
    print(len(allocate) ++ "\n")
    let capture = () => snapshot.y
    print(capture() ++ "\n")
    print(tune32_phase2_record_formula(tune32_phase2_record(1.5, 2.25, null)) ++ "\n")
    print((tune32_phase2_record_formula(tune32_phase2_record("bad", 2.25, null)) is error) ++ "\n")
    print((tune32_phase2_record_formula(tune32_phase2_record(null, 2.25, null)) == null) ++ "\n")
    print((tune32_phase2_record_formula(7) == null) ++ "\n")
    print(tune32_phase2_record_null(tune32_phase2_record(0, 0, null)) ++ "\n")
    print(tune32_phase2_record_null(tune32_phase2_record(0, 0, false)) ++ "\n")
    print(tune32_phase2_record_null(tune32_phase2_record(0, 0, 0)) ++ "\n")
    print(tune32_phase2_record_null(tune32_phase2_record(0, 0, "")) ++ "\n")
    print(tune32_phase2_record_null(tune32_phase2_record(0, 0, [])) ++ "\n")
    print(tune32_phase2_record_null({other: 1}) ++ "\n")
    print(tune32_phase2_record_null(tune32_phase2_record(0, 0, type(null))) ++ "\n")
    print(tune32_phase2_record_null({child: error("poison")}) ++ "\n")
}
