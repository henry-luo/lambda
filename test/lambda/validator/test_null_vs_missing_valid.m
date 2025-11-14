// Valid test cases for null vs missing fields

// Test 1: All required fields present (PersonRequired)
{
    name: "John Doe",
    age: 30,
    email: "john@example.com"
}

// Test 2: Optional field missing (PersonOptional)
{
    name: "Jane Doe"
}

// Test 3: Optional field explicitly null (PersonOptional)
{
    name: "Bob Smith",
    age: null,
    email: null
}

// Test 4: Optional field present with value (PersonOptional)
{
    name: "Alice Wonder",
    age: 25,
    email: "alice@example.com"
}

// Test 5: Mixed - some optional missing, some null (PersonMixed)
{
    id: 1,
    name: "Charlie Brown",
    age: null,
    verified: true
}

// Test 6: Mixed - all optional present (PersonMixed)
{
    id: 2,
    name: "David Johnson",
    nickname: "Dave",
    age: 28,
    verified: false
}
