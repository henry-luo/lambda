type Person = {name: string, age: int}

pn main() {
    let person = input("test/input/type_enforce_input_schema_invalid.json",
        {type: 'json', schema: Person})^
    person
}
