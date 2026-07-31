type Person = {name: string, age: int}

pn wrong_bracket_write() {
    var person: Person = {name: "Ana", age: 30}
    person["age"] = "very old"
}
