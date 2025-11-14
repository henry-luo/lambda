// Schema for testing missing fields vs null values
// Tests the distinction between:
// 1. Field missing entirely (not in map structure)
// 2. Field present but null

type PersonRequired = {
    name: string,       // required field (no ?)
    age: int,           // required field (no ?)
    email: string       // required field (no ?)
}

type PersonOptional = {
    name: string,       // required field
    age: int?,          // optional field (can be missing or null)
    email: string?      // optional field (can be missing or null)
}

type PersonMixed = {
    id: int,            // required
    name: string,       // required
    nickname: string?,  // optional
    age: int?,          // optional
    verified: bool      // required
}
