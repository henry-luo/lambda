// Foreign-key checks read the active transaction without allowing unsafe pragmas.
import sqlite: lambda.io.sqlite

pn rejected_pragma() any^ {
    open tx = (sqlite.database(":memory:")^) {
        sqlite.query(tx, "PRAGMA foreign_keys = OFF")^
    }
    return null
}

pn main() {
    open tx = (sqlite.database(":memory:")^) {
        sqlite.execute(tx, "CREATE TABLE parent (id INTEGER PRIMARY KEY)")^
        sqlite.execute(tx, "CREATE TABLE child (parent_id INTEGER REFERENCES parent(id) DEFERRABLE INITIALLY DEFERRED)")^
        sqlite.execute(tx, "CREATE TABLE pragma_foreign_key_check (value INTEGER)")^
        print(len(sqlite.query(tx, "PRAGMA main.foreign_key_check")^) == 0); print("\n")
        sqlite.execute(tx, "INSERT INTO child VALUES (7)")^
        let broken=sqlite.query(tx, "PRAGMA main.foreign_key_check")^
        print(len(broken)==1 and broken[0][0]=="child"); print("\n")
        sqlite.execute(tx, "INSERT INTO parent VALUES (7)")^
        print(len(sqlite.query(tx, "PRAGMA main.foreign_key_check")^) == 0); print("\n")
    }
    let denied: any^ = rejected_pragma()
    print(denied is error)
}
