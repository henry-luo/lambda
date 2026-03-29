// Test: SQLite view access
// Verifies views are accessible in both schema and data namespaces

let db = input('./test/input/test_rdb_full.db', 'sqlite')^

// view flag in schema
db.schema.cheap_products.view

// view row count (Widget 9.99, Novel 14.99, Cable 4.99 → all < 20)
len(db.data.cheap_products)

// view data access — first cheap product
db.data.cheap_products[0].name
db.data.cheap_products[0].price

// view rows have same fields as SELECT columns
db.data.cheap_products[1].name
db.data.cheap_products[1].price

// third cheap product
db.data.cheap_products[2].name
db.data.cheap_products[2].price

// iteration over view data
[for (p in db.data.cheap_products) p.name]
