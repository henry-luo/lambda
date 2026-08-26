type Person = {name: string, age: int}

let person = input("test/input/type_enforce_input_schema_valid.json",
    {type: 'json', schema: Person})^;

[person.name, person.age, person.extra]
