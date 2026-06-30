// Test type-keyword field names in map literals and member assignment.

pn test_list_field_assignment() {
    var sched = {list: null, count: 0}
    sched.list = "ready"
    sched.count = sched.count + 1
    print("list:" ++ string(sched.list) ++ "," ++ string(sched.count) ++ "\n")
}

pn test_other_type_keyword_fields() {
    var rec = {map: 1, string: "old", int: 10}
    rec.map = 2
    rec.string = "new"
    rec.int = 20
    print("types:" ++ string(rec.map) ++ "," ++ rec.string ++ "," ++ string(rec.int) ++ "\n")
}

pn main() {
    test_list_field_assignment()
    test_other_type_keyword_fields()
}
