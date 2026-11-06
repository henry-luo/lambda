import sqlite: lambda.io.sqlite
import sql: ~~.~~.mod_sql

pn main() {
    let path="temp/dbench-sql-" ++ (uuid()^) ++ ".db"
    sqlite.execute(path,"CREATE TABLE \"odd\"\"table\" (id INTEGER PRIMARY KEY, name TEXT, value TEXT)")^
    sqlite.execute(path,"INSERT INTO \"odd\"\"table\" VALUES (1, 'O''Reilly', NULL), (2, 'O''Reilly', 'yes'), (3, 'O''Reilly', 'yes'), (4, 'different', 'yes')")^
    let options=[{column:"name",op:"=",value:"O'Reilly"},{column:"value",op:"<>",value:null}]
    let first=sql.table_page("sqlite","odd\"table",["id","name","value"],options,[],["id"],0,1)^
    let second=sql.table_page("sqlite","odd\"table",["id","name","value"],options,[],["id"],1,1)^
    let a=sqlite.query(path,first.sql,first.params)^
    let b=sqlite.query(path,second.sql,second.params)^
    print(a==[[2,"O'Reilly","yes"]] and b==[[3,"O'Reilly","yes"]]); print("\n")
    let injection=sql.table_page("sqlite","odd\"table",["id","name","value"],
        [{column:"name",op:"=",value:"' OR 1=1 --"}],[],["id"],0,10)^
    print(len(sqlite.query(path,injection.sql,injection.params)^)==0); print("\n")
    let remaining=sqlite.query(path,"SELECT COUNT(*) FROM \"odd\"\"table\"")^
    print(remaining[0][0]==4); print("\n")
    sqlite.execute(path,"CREATE TABLE pairs (a INTEGER, b TEXT, note TEXT, PRIMARY KEY(a,b))")^
    sqlite.execute(path,"INSERT INTO pairs VALUES (1,'A','yes'),(1,'B','yes'),(2,'A','yes'),(2,'B','no')")^
    let initial=sql.table_page("sqlite","pairs",["a","b","note"],[{column:"note",op:"=",value:"yes"}],[],["a","b"],0,1)^
    let seen=sqlite.query(path,initial.sql,initial.params)^
    sqlite.execute(path,"INSERT INTO pairs VALUES (0,'Z','yes')")^
    let keyset=sql.table_page("sqlite","pairs",["a","b","note"],[{column:"note",op:"=",value:"yes"}],[],["a","b"],1,2,null,[1,"A"])^
    print(seen==[[1,"A","yes"]] and sqlite.query(path,keyset.sql,keyset.params)^==[[1,"B","yes"],[2,"A","yes"]]); print("\n")
    print(keyset.keyset and keyset.params==["yes",1,1,"A",2]); print("\n")
    let descending=sql.table_page("sqlite","pairs",["a","b","note"],[{column:"note",op:"=",value:"yes"}],
        [{column:"b",direction:"DESC"}],["a","b"],1,2,null,["B",1])^
    print(sqlite.query(path,descending.sql,descending.params)^==[[1,"A","yes"],[2,"A","yes"]])
    io.delete(path)^
}
