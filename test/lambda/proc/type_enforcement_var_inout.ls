// A `var` parameter borrows its caller's annotated root. Its checked write
// must preserve the map contract and publish the accepted update to caller.
type Person = {name: string, age: int}

pn birthday(var person: Person) {
    person.age = person.age + 1
}

pn main() {
    var person: Person = {name: "Ana", age: 30}
    birthday(person)
    print(person.age ++ "\n")
}
