// Invalid test cases for null vs missing fields

// Test 1: Required field missing (PersonRequired)
// SHOULD FAIL: Missing required field 'age'
{
    name: "John Doe",
    email: "john@example.com"
}

// Test 2: Required field is null (PersonRequired)
// SHOULD FAIL: Required field 'age' cannot be null
{
    name: "Jane Smith",
    age: null,
    email: "jane@example.com"
}

// Test 3: All required fields null (PersonRequired)
// SHOULD FAIL: Multiple required fields are null
{
    name: null,
    age: null,
    email: null
}

// Test 4: Required field missing (PersonMixed)
// SHOULD FAIL: Missing required field 'verified'
{
    id: 1,
    name: "Bob Jones"
}

// Test 5: Required field null (PersonMixed)
// SHOULD FAIL: Required field 'name' cannot be null
{
    id: 2,
    name: null,
    verified: true
}

// Test 6: Multiple required fields missing (PersonMixed)
// SHOULD FAIL: Missing 'name' and 'verified'
{
    id: 3,
    nickname: "Test",
    age: 25
}
