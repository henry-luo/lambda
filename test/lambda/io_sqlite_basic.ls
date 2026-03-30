// Test: SQLite database input via input()
// Loads a small test database and verifies schema/data access

let db = input('./test/input/test_rdb.db', 'sqlite')^

// database element name
db.name

// table count
db.table_count

// table_names — array of all table/view names
db.table_names

// data access — first user
db.data.users[0].name
db.data.users[0].age

// data access — second user
db.data.users[1].name
db.data.users[1].score

// data access — third user
db.data.users[2].name

// count all users
len(db.data.users)
