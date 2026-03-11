// Test: Match Dispatch
// Layer: 4 | Category: integration | Covers: match, type patterns, closures, higher-order

// ===== Define types for dispatch =====
type AddOp { a: int, b: int }
type MulOp { a: int, b: int }
type NegOp { value: int }
type SquareOp { value: int }

// ===== Dispatch function =====
fn evaluate(op) => match op {
    case AddOp: op.a + op.b
    case MulOp: op.a * op.b
    case NegOp: -op.value
    case SquareOp: op.value * op.value
    default: error("Unknown operation")
}

// ===== Execute operations =====
evaluate(<AddOp a: 3, b: 4>)
evaluate(<MulOp a: 5, b: 6>)
evaluate(<NegOp value: 42>)
evaluate(<SquareOp value: 7>)

// ===== Batch operations =====
let operations = [
    <AddOp a: 1, b: 2>,
    <MulOp a: 3, b: 4>,
    <NegOp value: 5>,
    <SquareOp value: 6>,
    <AddOp a: 10, b: 20>
]
let results = operations | map(evaluate)
results

// ===== Accumulate results =====
results | sum()

// ===== Filter by type =====
operations | filter((op) => op is AddOp) | map(evaluate)
operations | filter((op) => op is MulOp) | map(evaluate)

// ===== Chain operations =====
fn chain_ops(value: int, ops: list) {
    ops | reduce(fn(acc, op) => match op {
        case 'add': acc + 1
        case 'double': acc * 2
        case 'negate': -acc
        case 'square': acc * acc
        default: acc
    }, value)
}
chain_ops(3, ('double', 'add', 'square'))
chain_ops(5, ('negate', 'double', 'add'))

// ===== Match with computed predicates =====
fn classify_number(n: int) => match n {
    case 0: "zero"
    case int: if (n > 0) {
        if (n % 2 == 0) "positive even"
        else "positive odd"
    } else {
        if (n % 2 == 0) "negative even"
        else "negative odd"
    }
}
classify_number(0)
classify_number(4)
classify_number(7)
classify_number(-6)
classify_number(-3)

// ===== Dispatch table =====
let handlers = {
    greet: fn(name) => "Hello, " & name,
    farewell: fn(name) => "Goodbye, " & name,
    shout: fn(name) => upper(name) & "!"
}
fn dispatch(action: string, arg: string) => handlers.(action)(arg)
dispatch("greet", "Alice")
dispatch("farewell", "Bob")
dispatch("shout", "Charlie")
