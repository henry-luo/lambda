// Test: SQLite data iteration and functional operations
// Verifies for-in iteration, array comprehensions, len(), and data integrity

let db = input('./test/input/test_rdb_full.db', 'sqlite')^

// iterate categories
[for (c in db.data.categories) c.name]

// iterate products — extract names
[for (p in db.data.products) p.name]

// product prices
[for (p in db.data.products) p.price]

// all row data for categories
len(db.data.categories)
db.data.categories[0]
db.data.categories[1]
db.data.categories[2]

// products — verify all 5 rows accessible by index
[db.data.products[0].name, db.data.products[1].name, db.data.products[2].name, db.data.products[3].name, db.data.products[4].name]

// JSON deep access — product 0 tags array element
[db.data.products[0].tags[0], db.data.products[0].tags[1]]

// JSON deep access — product 1 tags object key
db.data.products[1].tags.color
