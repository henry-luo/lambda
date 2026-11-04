// Create-only SQLite publication follows S12.4.4v4 and D11.1.2.
import sqlite: lambda.io.sqlite

pn returned(path) int^ {
    open tx = (sqlite.create(path)^) {
        sqlite.execute(tx,"CREATE TABLE value (n INTEGER)")^
        sqlite.execute(tx,"INSERT INTO value VALUES (42)")^
        return 7
    }
    return 0
}
pn bad_return(path, value) int^ {
    open tx = (sqlite.create(path)^) {
        sqlite.execute(tx,"CREATE TABLE value (n INTEGER)")^
        return value
    }
    return 0
}
pn poisoned(path) any^ {
    open tx = (sqlite.create(path)^) {
        sqlite.execute(tx,"CREATE TABLE value (n INTEGER)")^
        sqlite.execute(tx,"INSERT INTO missing VALUES (1)") ^ { null }
    }
    return null
}
pn waiting(path, parent) any^ {
    open tx = (sqlite.create(path)^) {
        sqlite.execute(tx,"CREATE TABLE value (n INTEGER)")^
        send(parent,"ready")^
        sleep(10000)^
    }
    return null
}
pn competing(path, parent, id) any^ {
    open tx = (sqlite.create(path)^) {
        sqlite.execute(tx,"CREATE TABLE value (n INTEGER)")^
        sqlite.execute(tx,"INSERT INTO value VALUES (?1)",[id])^
        send(parent,"ready")^
        receive()^
    }
    return id
}
pn main() {
    let suffix=uuid()^
    let path="temp/sqlite-create-" ++ suffix ++ ".db"
    let bad="temp/sqlite-create-bad-" ++ suffix ++ ".db"
    let cancelled="temp/sqlite-create-cancel-" ++ suffix ++ ".db"
    let race="temp/sqlite-create-race-" ++ suffix ++ ".db"
    let dotted="./temp/sqlite-create-dot-" ++ suffix ++ ".db"
    let created: any^ = returned(path)
    if (created is error) { print("unsupported"); return null }
    print(created==7); print("\n")
    print((sqlite.query(path,"SELECT n FROM value")^)[0][0]==42); print("\n")
    var existing=false
    returned(path) ^ { existing=true }
    print(existing and (sqlite.query(path,"SELECT n FROM value")^)[0][0]==42); print("\n")
    var invalid=false
    bad_return(bad,"bad") ^ { invalid=true }
    print(invalid and not exists(bad)); print("\n")
    var rollback_only=false
    poisoned(bad) ^ { rollback_only=true }
    print(rollback_only and not exists(bad)); print("\n")
    let child=start(waiting,[cancelled,self()])
    receive()^
    cancel(child)
    let cancellation: any^ = wait(child)
    print(cancellation is error and not exists(cancelled)); print("\n")
    let a=start(competing,[race,self(),1]), b=start(competing,[race,self(),2])
    receive()^; receive()^
    send(a,"go")^; send(b,"go")^
    let first: any^ = wait(a)
    let second: any^ = wait(b)
    let both_ok=(first is error and not (second is error)) or (second is error and not (first is error))
    let winner=(sqlite.query(race,"SELECT n FROM value")^)[0][0]
    print(both_ok and (winner==1 or winner==2)); print("\n")
    var memory_rejected=false, reserved_rejected=false
    sqlite.create(":memory:") ^ { memory_rejected=true }
    sqlite.create("temp/.lambda-stage-reserved") ^ { reserved_rejected=true }
    print(memory_rejected and reserved_rejected); print("\n")
    let dotted_result=returned(dotted)^
    print(dotted_result==7 and (sqlite.query(dotted,"SELECT n FROM value")^)[0][0]==42)
    io.delete(dotted)^
    io.delete(path)^
    io.delete(race)^
}
