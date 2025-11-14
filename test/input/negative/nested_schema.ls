type Address = {
    street: string,
    city: string,
    zipcode: string?
}

type Person = {
    name: string,
    age: int,
    address: Address
}

type Document = {
    person: Person
}
