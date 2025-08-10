// Simple schema with only strings - no numbers
type Person = {
    name: string
}

type Document = {
    person: Person,
    message: string
}
