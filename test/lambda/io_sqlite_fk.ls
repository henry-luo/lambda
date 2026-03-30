// Test: SQLite schema — foreign keys with link_name and reverse_fks
// Verifies FK navigation metadata is exposed in schema namespace

let db = input('./test/input/test_rdb_full.db', 'sqlite')^

// FK with link_name — products.category_id → categories.id
let fk = db.schema.products.foreign_keys[0]
[fk.column, fk.ref_table, fk.ref_column, fk.link_name]

// reverse FK — categories receives incoming FK from products
len(db.schema.categories.reverse_fks)
let rfk = db.schema.categories.reverse_fks[0]
[rfk.from_table, rfk.from_column, rfk.column, rfk.link_name]

// table with no reverse FKs
type(db.schema.products.reverse_fks)
