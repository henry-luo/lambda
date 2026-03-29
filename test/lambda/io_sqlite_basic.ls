// Test: SQLite database input via input()
// Loads a small test database and verifies schema/data access

let db = input('./test/input/test_rdb.db', 'sqlite')^

// database element name
"name:"
db.name

// table count
"tables:"
db.table_count

// data access — first user
"first user:"
db.data.users[0].name
db.data.users[0].age

// data access — second user
"second user:"
db.data.users[1].name
db.data.users[1].score

// data access — third user
"third user:"
db.data.users[2].name

// count all users
"user count:"
len(db.data.users)
