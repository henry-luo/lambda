// Test: SQLite schema metadata access
// Verifies schema namespace: columns, types, nullable, pk, foreign_keys, view flag

let db = input('./test/input/test_rdb_full.db', 'sqlite')^

// table count (2 tables + 1 view)
db.table_count

// categories columns
len(db.schema.categories.columns)
db.schema.categories.columns[0].name
1
db.schema.categories.columns[0].pk
db.schema.categories.columns[1].name
2
db.schema.categories.columns[1].pk

// products column count (id, name, category_id, price, in_stock, created_at, tags)
len(db.schema.products.columns)

// products FK — using array to keep strings separate
len(db.schema.products.foreign_keys)
[db.schema.products.foreign_keys[0].column, db.schema.products.foreign_keys[0].ref_table, db.schema.products.foreign_keys[0].ref_column]

// view flag
db.schema.cheap_products.view
