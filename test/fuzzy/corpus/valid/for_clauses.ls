// For expression clauses - comprehensive coverage

let data = [5, 2, 8, 1, 9, 3, 7, 4, 6, 10];
let users = [
    {name: "Alice", age: 30, active: true},
    {name: "Bob", age: 25, active: false},
    {name: "Charlie", age: 35, active: true},
    {name: "Diana", age: 28, active: true},
    {name: "Eve", age: 22, active: false}
];

// Basic for expression
let basic = for (x in data) x * 2;

// Where clause - filtering
let filtered = for (x in data where x > 5) x;
let active_users = for (u in users where u.active) u.name;
let complex_filter = for (x in data where x > 3 and x < 8) x;

// Let clause - intermediate bindings
let with_let = for (x in data, let sq = x * x) sq;
let multi_let = for (x in data, let sq = x * x, let cube = sq * x) cube;
let let_before_where = for (x in data, let doubled = x * 2 where doubled > 10) doubled;

// Order by clause - ascending (default)
let sorted_asc = for (x in data order by x) x;
let sorted_by_field = for (u in users order by u.age) u.name;

// Order by clause - descending
let sorted_desc = for (x in data order by x desc) x;
let sorted_field_desc = for (u in users order by u.age desc) u.name;

// Order by with keyword variants
let ascending_explicit = for (x in data order by x asc) x;
let descending_explicit = for (x in data order by x descending) x;

// Limit clause
let first_five = for (x in data limit 5) x;
let top_three = for (x in data order by x desc limit 3) x;

// Offset clause
let skip_two = for (x in data offset 2) x;
let skip_five = for (x in data offset 5) x;

// Combined limit and offset (pagination)
let page_one = for (x in data limit 3 offset 0) x;
let page_two = for (x in data limit 3 offset 3) x;
let page_three = for (x in data limit 3 offset 6) x;

// All clauses combined
let complex1 = for (x in data 
    where x > 2 
    order by x 
    limit 5) x;

let complex2 = for (x in data,
    let sq = x * x
    where sq > 10
    order by sq desc
    limit 3) sq;

let complex3 = for (u in users
    where u.active
    order by u.age desc
    limit 2
    offset 0) u.name;

// User processing example
let user_report = for (u in users
    where u.active
    order by u.age) {name: u.name, age: u.age};

// Multiple iteration variables
let pairs = for (x in [1, 2], y in [10, 20]) x + y;
let products = for (a in [1, 2, 3], b in [1, 2, 3]) a * b;

// Nested for expressions
let matrix = for (i in 1 to 3) (for (j in 1 to 3) i * j);

// For with conditional body
let conditional = for (x in data) if (x % 2 == 0) x else -x;
let filtered_conditional = for (x in data where x > 3) if (x < 8) x * 2 else x;

// For with range
let range_basic = for (i in 1 to 10) i;
let range_squared = for (i in 1 to 10) i * i;
let range_filtered = for (i in 1 to 20 where i % 3 == 0) i;

// Empty and edge cases
let empty_result = for (x in data where x > 100) x;
let single_result = for (x in data where x == 5) x;

// Aggregation after for
let sum_squares = sum(for (x in data) x * x);
let count_active = len(for (u in users where u.active) u);

// Complex real-world pattern
let leaderboard = for (u in users,
    let score = u.age * 10
    where u.active
    order by score desc
    limit 3)
  {name: u.name, score: score};

// Final expression
leaderboard
