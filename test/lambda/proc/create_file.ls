// Exclusive file publication preserves existing files and is outside SQLite scopes.
import sqlite: lambda.io.sqlite

pn show(value) { print(value); print("\n") }
pn compete(path,content) any^ { return io.create_file(path,content)^ }
pn rolled_back(db,path) any^ {
    open tx = (sqlite.existing(db)^) {
        sqlite.execute(tx,"INSERT INTO t VALUES (1)")^
        io.create_file(path,"external")^
        raise error("roll back")
    }
    return null
}
pn main() {
    let root="temp/create-file-" ++ (uuid()^)
    io.mkdir(root)^
    let file=root ++ "/café space.txt", data="Hello café\n"
    let created: any^ = io.create_file(file,data)
    if (created is error) { io.delete(root)^; print("unsupported"); return null }
    show(created==len(binary(data)) and (input(file,'text')^)==data)^
    let again: any^ = io.create_file(file,"replacement")
    show(again is error and (input(file,'text')^)==data)^
    let raw=root ++ "/raw.bin", content=b'\x00FF504B00'
    show((io.create_file(raw,content)^)==5 and (input(raw,'binary')^)==content)^
    let empty=root ++ "/empty.txt"
    show((io.create_file(empty,"")^)==0 and exists(empty))^
    let invalid: any^ = io.create_file(root ++ "/missing/child","x")
    let dir: any^ = io.create_file(root,"x")
    let remote: any^ = io.create_file("https://example.invalid/new","x")
    let nul: any^ = io.create_file(root ++ "/bad" ++ chr(0) ++ "name","x")
    let reserved: any^ = io.create_file(root ++ "/.lambda-stage-reserved","x")
    show(invalid is error and dir is error and remote is error and nul is error and reserved is error and not exists(root ++ "/missing"))^
    let bad_value: any^ = parse("{",'json')
    let bad_content: any^ = io.create_file(root ++ "/error.txt",bad_value)
    show(bad_content is error and not exists(root ++ "/error.txt"))^
    let link=root ++ "/link"
    io.symlink(file,link)^
    let linked: any^ = io.create_file(link,"bad")
    show(linked is error and (input(file,'text')^)==data)^
    let race=root ++ "/race.txt"
    let a=start(compete,[race,"one"]), b=start(compete,[race,"two"])
    let first: any^ = wait(a), second: any^ = wait(b)
    let actual=input(race,'text')^
    show(((first is error and not (second is error)) or (second is error and not (first is error))) and
        (actual=="one" or actual=="two"))^
    let db=root ++ "/company.db", published=root ++ "/outside.txt"
    open tx = (sqlite.create(db)^) { sqlite.execute(tx,"CREATE TABLE t(n INTEGER)")^ }
    let aborted: any^ = rolled_back(db,published)
    show(aborted is error and len(sqlite.query(db,"SELECT n FROM t")^)==0 and (input(published,'text')^)=="external")^
    io.delete(root)^
}
