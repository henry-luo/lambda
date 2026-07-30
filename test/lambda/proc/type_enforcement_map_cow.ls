type Person = {name: string, age: int}

pn main() {
    var person: Person = {name: "Ana", age: 30}
    let snapshot = person
    person.age = 31
    print(snapshot)
    print("\n")
    print(person)
}
