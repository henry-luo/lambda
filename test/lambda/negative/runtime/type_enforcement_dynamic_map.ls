type Person = {name: string, age: int}

fn dynamic_person() any { {name: "Ana", age: "very old"} }

pn main() {
    var person: Person = dynamic_person()
}
