// Test: SQLite schema metadata access
// Verifies schema namespace: columns, types, nullable, pk, foreign_keys, view flag

let db = input('./test/input/test_rdb_full.db', 'sqlite')^

// table count (2 tables + 1 view)
db.table_count

// categories columns
len(db.schema.categories.columns)
[db.schema.categories.columns[0].name, db.schema.categories.columns[0].pk]
[db.schema.categories.columns[1].name, db.schema.categories.columns[1].pk]
[db.schema.categories.columns[2].name, db.schema.categories.columns[2].nullable]

// products column count (id, name, category_id, price, weight, in_stock, created_at, tags)
len(db.schema.products.columns)
[db.schema.products.columns[4].name, db.schema.products.columns[4].type, db.schema.products.columns[4].nullable]
[db.schema.products.columns[3].name, db.schema.products.columns[3].nullable]
[db.schema.products.columns[6].name, db.schema.products.columns[6].type]
[db.schema.products.columns[7].name, db.schema.products.columns[7].type]

// products FK — using array to keep strings separate
len(db.schema.products.foreign_keys)
[db.schema.products.foreign_keys[0].column, db.schema.products.foreign_keys[0].ref_table, db.schema.products.foreign_keys[0].ref_column]

// categories index (unique name index)
len(db.schema.categories.indexes)
db.schema.categories.indexes[0].name
db.schema.categories.indexes[0].unique
db.schema.categories.indexes[0].columns

// products index (non-unique price index)
len(db.schema.products.indexes)
db.schema.products.indexes[0].name
db.schema.products.indexes[0].unique
db.schema.products.indexes[0].columns

// views have no indexes
type(db.schema.cheap_products.indexes)

// view schema metadata
[len(db.schema.cheap_products.columns), db.schema.cheap_products.columns[2].name]
db.schema.cheap_products.view

// triggers on products (2 triggers added)
len(db.schema.products.triggers)
[db.schema.products.triggers[0].name, db.schema.products.triggers[0].timing, db.schema.products.triggers[0].event]
[db.schema.products.triggers[1].name, db.schema.products.triggers[1].timing, db.schema.products.triggers[1].event]

// categories has no triggers
type(db.schema.categories.triggers)

// database-level SQL functions
type(db.functions)
(len(db.functions) > 0)

// verify function entry structure (first function has expected fields)
let f0 = db.functions[0]
[type(f0.name), type(f0.type), type(f0.narg), type(f0.builtin)]
