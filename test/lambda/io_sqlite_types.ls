// Test: SQLite type mapping and value conversion
// Verifies: int, float, bool, string, null, datetime, decimal, JSON auto-parse

let db = input('./test/input/test_rdb_full.db', 'sqlite')^

// INT type — product id
type(db.data.products[0].id)
db.data.products[0].id

// STRING type — product name
type(db.data.products[0].name)

// FLOAT type — price
type(db.data.products[0].price)
db.data.products[0].price

// DECIMAL type — weight
type(db.data.products[0].weight)
db.data.products[0].weight

// NULL decimal
type(db.data.products[4].weight)

// BOOL type — in_stock
type(db.data.products[0].in_stock)
db.data.products[0].in_stock
// in_stock = 0 (Novel, row 2)
db.data.products[2].in_stock

// DATETIME type
type(db.data.products[0].created_at)

// NULL value
type(db.data.products[2].tags)

// NULL datetime
type(db.data.products[4].created_at)

// JSON array auto-parse
type(db.data.products[0].tags)
db.data.products[0].tags

// JSON object auto-parse
type(db.data.products[1].tags)
db.data.products[1].tags

// Integer FK column
type(db.data.products[0].category_id)
db.data.products[0].category_id

// Nullable TEXT column with value
type(db.data.categories[0].description)

// Nullable TEXT column with NULL
type(db.data.categories[2].description)
