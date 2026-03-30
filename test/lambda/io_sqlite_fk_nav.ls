// Test: SQLite FK auto-navigation — forward (many-to-one) and reverse (one-to-many)
// Verifies product.category resolves to a map, category.products is an array

let db = input('./test/input/test_rdb_full.db', 'sqlite')^

// forward FK: product → category
type(db.data.products[0].category)
db.data.products[0].category.name

// all products mapped to their category name
[for (p in db.data.products) p.category.name]

// reverse FK: category → products array
type(db.data.categories[0].products)
len(db.data.categories[0].products)

// electronics products by name (category id=1 has 3 products)
[for (p in db.data.categories[0].products) p.name]

// books products by name (category id=2 has 2 products)
len(db.data.categories[1].products)
[for (p in db.data.categories[1].products) p.name]

// empty category has no products
len(db.data.categories[2].products)

// forward FK resolved name matches category row name
db.data.products[0].category.name == db.data.categories[0].name

// reverse FK: category id accessible from nested product
db.data.categories[1].products[0].category_id
