// Test: for-clause where/order/limit on RDB table data (in-memory evaluation)
// Verifies Lambda filter/sort/limit work on eagerly loaded RDB arrays

let db = input('./test/input/test_rdb_full.db', 'sqlite')^

// basic filter — products under $20
[for (p in db.data.products where p.price < 20) p.name]

// filter by bool column
[for (p in db.data.products where p.in_stock) p.id]

// order by price — note: for..order by returns sorted array directly (no outer [])
for (p in db.data.products order by p.price) p.name

// order by descending
for (p in db.data.products order by p.price desc) p.name

// limit
for (p in db.data.products order by p.price limit 3) p.name

// combined where + order by + limit
for (p in db.data.products where p.in_stock order by p.price limit 2) p.name

// for over FK reverse array with filter
[for (p in db.data.categories[0].products where p.price < 20) p.name]

// nested FK: product category names via forward FK
[for (p in db.data.products where p.price < 20) p.category.name]
