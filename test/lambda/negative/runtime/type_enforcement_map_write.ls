type Person = {name: string, age: int}

fn identity(value) => value

pn main() {
    var person: Person = {name: "Ana", age: 30}
    person.age = identity("very old")
}
