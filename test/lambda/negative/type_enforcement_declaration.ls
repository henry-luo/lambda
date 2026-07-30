type Person = {name: string, age: int}

let wrong_scalar: int = "not an integer"
let wrong_field: Person = {name: "Ana", age: "not an integer"}
let wrong_null: int = null

fn wrong_return() int {
    "not an integer"
}

pn wrong_member_write() {
    var person: Person = {name: "Ana", age: 30}
    person.age = "very old"
}
