// Test: Match Expression
// Layer: 3 | Category: statement | Covers: match with patterns

match 42 {
    case 0: "zero"
    case 42: "forty-two"
    default: "other"
}
1
match 3.14 {
    case int: "integer"
    case float: "float"
    case string: "string"
    default: "other"
}
1
let score = 85;
match score {
    case 90 to 100: "A"
    case 80 to 89: "B"
    case 70 to 79: "C"
    default: "F"
}
1
match null {
    case null: "is null"
    default: "not null"
}
1
match "hello" {
    case int: "int"
    case float: "float"
    default: "default"
}
