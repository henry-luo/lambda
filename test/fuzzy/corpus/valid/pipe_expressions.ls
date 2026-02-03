// Pipe expressions - comprehensive coverage

// Basic pipe with current item ~
let doubled = [1, 2, 3] | ~ * 2;
let tripled = [1, 2, 3, 4, 5] | ~ * 3;

// Pipe with field access
let users = [{name: "Alice", age: 30}, {name: "Bob", age: 25}];
let names = users | ~.name;
let ages = users | ~.age;

// Pipe with arithmetic operations
let squares = [1, 2, 3, 4, 5] | ~ ^ 2;
let incremented = [10, 20, 30] | ~ + 1;
let halved = [10, 20, 30] | ~ / 2;

// Pipe with index access ~#
let indexed = ['a', 'b', 'c'] | {index: ~#, value: ~};
let with_positions = [10, 20, 30] | (~ + ~#);

// Using system functions directly (not pipe aggregate)
let total = sum([1, 2, 3, 4, 5]);
let sorted_list = sort([3, 1, 4, 1, 5]);
let reversed_list = reverse([1, 2, 3]);
let first_three = take([1, 2, 3, 4, 5], 3);
let last_two = drop([1, 2, 3, 4, 5], 3);

// Chained pipe transformations
let chain1 = [1, 2, 3] | ~ * 2 | ~ + 1;
let chain2 = sum([1, 2, 3, 4, 5] | ~ ^ 2);
let chain3 = reverse(sort([5, 3, 1, 4, 2]));

// Pipe with map construction
let records = [1, 2, 3] | {id: ~, squared: ~ ^ 2};

// Where clause - basic filtering
let positive = [-2, -1, 0, 1, 2] where ~ > 0;
let large = [1, 5, 10, 15, 20] where ~ >= 10;
let even = [1, 2, 3, 4, 5, 6] where ~ % 2 == 0;
let odd = [1, 2, 3, 4, 5, 6] where ~ % 2 != 0;

// Where with field access
let adults = users where ~.age >= 18;
let named_alice = users where ~.name == "Alice";

// Combined pipe and where
let filtered_doubled = [1, 2, 3, 4, 5] | ~ * 2 where ~ > 5;
let pipe_filter_sum = sum([1, 2, 3, 4, 5] | ~ ^ 2 where ~ > 10);

// Chained pipe transformations with filter
let complex1 = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10] | ~ * 2 where ~ > 6 and ~ < 15;

// Pipe with nested field access
let nested_data = [{user: {name: "A", score: 100}}, {user: {name: "B", score: 85}}];
let nested_scores = nested_data | ~.user.score;

// Pipe on scalar (~ binds to whole value)
let scalar_result = 42 | ~ * 2;
let string_concat = "hello" | ~ ++ " world";

// Pipe with function calls inside
let processed = [1, 2, 3] | abs(~ - 5);
let stringified = [1, 2, 3] | string(~);

// Map iteration with pipe (~ is value, ~# is key)
let map_data = {a: 1, b: 2, c: 3};
let map_transformed = map_data | {key: ~#, val: ~ * 10};

// Complex real-world patterns
let products = [
    {name: "Widget", price: 25, qty: 100},
    {name: "Gadget", price: 50, qty: 30},
    {name: "Gizmo", price: 15, qty: 200}
];
let expensive = products where ~.price > 20 | ~.name;
let total_value = sum(products | ~.price * ~.qty);

// Empty and single-element cases
let empty_pipe = [] | ~ * 2;
let single_pipe = [42] | ~ + 1;

// Final expression
pipe_filter_sum
