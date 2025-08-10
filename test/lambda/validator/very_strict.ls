// Very strict schema to force validation errors
type StrictPersonType = {
    name: string,
    age: int,
    email: string
}

type Document = {
    person: StrictPersonType,
    valid: bool
}
