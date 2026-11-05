// S12.4.4v4: reviewed SQLite statements commit once and failures roll back.
import sqlite: lambda.io.sqlite
import changes: ~~.~~.mod_edit

pn main() {
    let path="temp/dbench-write-" ++ (uuid()^) ++ ".db"
    sqlite.execute(path,"CREATE TABLE items (id INTEGER PRIMARY KEY, value TEXT)")^
    changes.execute_sql(path,"INSERT INTO items VALUES (?1,?2)",[1,"O'Reilly"])^
    print(sqlite.query(path,"SELECT id,value FROM items")==[[1,"O'Reilly"]]); print("\n")
    var multi=false
    changes.execute_sql(path,"UPDATE items SET value='bad'; DROP TABLE items",[]) ^ {multi=true}
    print(multi and sqlite.query(path,"SELECT value FROM items")==[["O'Reilly"]]); print("\n")
    var transaction=false
    changes.execute_sql(path,"BEGIN",[]) ^ {transaction=true}
    var rows=false
    changes.execute_sql(path,"SELECT * FROM items",[]) ^ {rows=true}
    print(transaction and rows); print("\n")
    changes.execute_sql(path,"CREATE INDEX item_value ON items(value)",[])^
    print(sqlite.query(path,"SELECT name FROM sqlite_schema WHERE type='index' AND name='item_value'")==[["item_value"]])
    io.delete(path)^
}
