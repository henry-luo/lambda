// S12.4.4v4: each successful SQL statement outside open commits independently.
import sqlite: lambda.io.sqlite
pn main() {
    let db = "temp/sqlite-autocommit-fixture.db"
    sqlite.execute(db, "DROP TABLE IF EXISTS entries")^
    sqlite.execute(db, "CREATE TABLE entries (id INTEGER PRIMARY KEY, text TEXT NOT NULL, raw BLOB)")^
    sqlite.execute(db, "INSERT INTO entries(id, text, raw) VALUES (?1, ?2, ?3)", [1, "first", b'\x007F'])^
    sqlite.execute(db, "INSERT INTO entries(id, text, raw) VALUES (?1, ?2, ?3)", [2, "O'Reilly", null])^
    let rows = sqlite.query(db, "SELECT id, text, raw FROM entries ORDER BY id")^
    print(len(rows) == 2 and rows[0][0] == 1 and rows[0][1] == "first"); print("\n")
    print(rows[0][2] == b'\x007F' and rows[1][1] == "O'Reilly" and rows[1][2] == null); print("\n")
    let embedded = "a" ++ "\u0000" ++ "b"
    let nul_result = sqlite.query(db, "SELECT ?1", [embedded])^
    print(nul_result[0][0] == embedded and len(nul_result[0][0]) == 3); print("\n")
    let empty_binary = sqlite.query(db, "SELECT ?1", [binary("")])^
    print(empty_binary[0][0] == binary("")); print("\n")
    let empty = sqlite.query(db, "SELECT id FROM entries WHERE id = ?1", [9])^
    print(len(empty) == 0); print("\n")
    let duplicate_names = sqlite.query(db, "SELECT 1 AS n, 2 AS n")^
    print(duplicate_names[0][0] == 1 and duplicate_names[0][1] == 2); print("\n")
    let large = sqlite.query(db, "SELECT 9223372036854775807")^
    print(large[0][0] == 9223372036854775807i64); print("\n")
    let scalars = sqlite.query(db, "SELECT ?1, ?2, ?3", [true, 1.5, -7])^
    print(scalars[0][0] == 1 and scalars[0][1] == 1.5 and scalars[0][2] == -7); print("\n")
    let sized = sqlite.query(db, "SELECT ?1, ?2", [5u8, 1.5f32])^
    print(sized[0][0] == 5 and sized[0][1] == 1.5); print("\n")
    var invalid_number = false
    sqlite.query(db, "SELECT ?1", [18446744073709551615u64]) ^ { invalid_number = true }
    print(invalid_number); print("\n")
    var invalid_float = false
    sqlite.query(db, "SELECT ?1", [nan]) ^ { invalid_float = true }
    print(invalid_float); print("\n")
    let tiny = sqlite.query(db, "SELECT 5e-320")^
    print(tiny[0][0] == 5.0e-320); print("\n")
    var missing = false
    sqlite.query(db, "SELECT ?1", []) ^ { missing = true }
    print(missing); print("\n")
    let document = temp('sqlite_document_guard', {n:1})^
    var mixed = false
    open d = temp.'sqlite_document_guard' {
        sqlite.execute(db, "DELETE FROM entries") ^ { mixed = true }
        put d.n = 2
    }
    print(mixed and temp.'sqlite_document_guard'#n == 2); print("\n")
    var rejected = false
    sqlite.execute(db, "INSERT INTO entries(id, text) VALUES (3, NULL)") ^ { rejected = true }
    print(rejected); print("\n")
    let row_count = sqlite.query(db, "SELECT COUNT(*) FROM entries")^
    print(row_count[0][0] == 2); print("\n")
    var multi = false
    sqlite.execute(db, "INSERT INTO entries(id, text) VALUES (4, 'x'); DELETE FROM entries") ^ { multi = true }
    print(multi); print("\n")
    let after = sqlite.query(db, "SELECT COUNT(*) FROM entries")^
    print(after[0][0] == 2); print("\n")
    var invalid_text = false
    sqlite.query(db, "INSERT INTO entries(id, text) VALUES (4, 'x') RETURNING CAST(x'80' AS TEXT)") ^ { invalid_text = true }
    print(invalid_text); print("\n")
    let final_count = sqlite.query(db, "SELECT COUNT(*) FROM entries")^
    print(final_count[0][0] == 2)
    sqlite.execute(db, "DROP TABLE entries")^
}
