// Test: SQLite auto-detection by file extension
// Verifies that .db files are automatically detected as sqlite

let db = input('./test/input/test_rdb.db')^

// should detect and load same as explicit 'sqlite'
db.name
db.table_count
db.data.users[0].name
