// Lambda Validator Test Data: Type References
// Tests type references and recursive type definitions

// ========== Simple Type References ==========

type Username = string
type Age = int
type Email = string

type User = {
    username: Username,
    age: Age,
    email: Email
}

let valid_user: User = {
    username: "alice",
    age: 30,
    email: "alice@example.com"
}

// ========== Nested Type References ==========

type Address = {
    street: string,
    city: string,
    zip: int
}

type Company = {
    name: string,
    address: Address
}

type Employee = {
    name: string,
    company: Company
}

let valid_employee: Employee = {
    name: "Bob",
    company: {
        name: "TechCorp",
        address: {
            street: "123 Tech Ave",
            city: "San Francisco",
            zip: 94102
        }
    }
}

// ========== Recursive Type References ==========

// Linked list node
type ListNode = {
    value: int,
    next: ListNode?
}

let valid_list_single: ListNode = {
    value: 1,
    next: null
}

let valid_list_multi: ListNode = {
    value: 1,
    next: {
        value: 2,
        next: {
            value: 3,
            next: null
        }
    }
}

// Tree node
type TreeNode = {
    value: string,
    children: [TreeNode]*
}

let valid_tree_leaf: TreeNode = {
    value: "leaf",
    children: []
}

let valid_tree_branch: TreeNode = {
    value: "root",
    children: [
        {value: "child1", children: []},
        {value: "child2", children: [
            {value: "grandchild", children: []}
        ]}
    ]
}

// JSON value (mutually recursive)
type JsonValue = null | bool | int | float | string | [JsonValue] | {string: JsonValue}

let valid_json_simple: JsonValue = 42
let valid_json_array: JsonValue = [1, "two", true, null]
let valid_json_nested: JsonValue = {
    name: "Alice",
    age: 30,
    active: true,
    tags: ["user", "admin"],
    metadata: {
        created: "2025-01-01",
        updated: null
    }
}

// ========== Forward References ==========

// Type defined after it's used
type Manager = {
    name: string,
    reports: [Employee]  // Employee defined earlier
}

let valid_manager: Manager = {
    name: "Carol",
    reports: [
        {name: "Bob", company: {name: "TechCorp", address: {street: "123 Tech Ave", city: "SF", zip: 94102}}}
    ]
}

// ========== Type Aliases ==========

type UserID = int
type ProductID = string
type Quantity = int

type OrderItem = {
    product: ProductID,
    quantity: Quantity
}

type Order = {
    user: UserID,
    items: [OrderItem]+
}

let valid_order: Order = {
    user: 12345,
    items: [
        {product: "PROD-001", quantity: 2},
        {product: "PROD-002", quantity: 1}
    ]
}

// ========== Invalid Cases ==========

// Wrong type through reference
let invalid_user_age: User = {
    username: "charlie",
    age: "thirty",  // Error: Age is int, got string
    email: "charlie@example.com"
}

// Missing field in nested reference
let invalid_employee_incomplete: Employee = {
    name: "Diana",
    company: {
        name: "StartupInc"
        // Error: Missing 'address' field
    }
}

// Recursive type with wrong field
let invalid_list_wrong: ListNode = {
    value: "not int",  // Error: Expected int, got string
    next: null
}

// Tree with wrong child type
let invalid_tree: TreeNode = {
    value: "root",
    children: [
        {value: "child", children: []},
        "wrong"  // Error: Expected TreeNode, got string
    ]
}

// JSON value with inconsistent types
let invalid_json_map: JsonValue = {
    key: "value",
    123: "numeric key"  // Error: Keys must be strings
}

// Type alias mismatch
let invalid_order_quantity: Order = {
    user: 12345,
    items: [
        {product: "PROD-001", quantity: "two"}  // Error: Quantity is int, got string
    ]
}

// ========== Undefined Type References ==========

// This would cause a compilation error, not validation error:
// type UndefinedRef = {field: NonExistentType}  // Error: Type 'NonExistentType' not found

// ========== Circular References (edge case) ==========

// Direct circular reference (should be handled gracefully)
type A = {next: B}
type B = {next: A}

let valid_circular: A = {
    next: {
        next: {
            next: {
                next: null  // Terminate somehow
            }
        }
    }
}
