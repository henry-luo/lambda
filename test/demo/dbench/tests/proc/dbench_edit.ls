// S12.4.4v4: an optimistic conflict rolls back the entire staged batch.
import sqlite: lambda.io.sqlite
import changes: ~~.~~.mod_edit

pn main() {
    let path="temp/dbench-edit-" ++ (uuid()^) ++ ".db"
    sqlite.execute(path,"CREATE TABLE \"o\"\"dd\" (id INTEGER PRIMARY KEY, \"m\"\"sg\" TEXT COLLATE NOCASE, note TEXT)")^
    sqlite.execute(path,"INSERT INTO \"o\"\"dd\" VALUES (1,'Alpha',NULL),(2,'Second','old')")^
    let fields=[{name:"id",declared:"INTEGER",primary:1},{name:"m\"sg",declared:"TEXT",primary:0},{name:"note",declared:"TEXT",primary:0}]
    let first=changes.plan("o\"dd",fields,[1,"Alpha",null],1,"Beta")^
    print(contains(first.statement_sql,"\"o\"\"dd\"") and not contains(first.statement_sql,"Beta")); print("\n")
    changes.apply_staged(path,[first])^
    print(sqlite.query(path,"SELECT \"m\"\"sg\" FROM \"o\"\"dd\" WHERE id=1")==[["Beta"]]); print("\n")
    let second=changes.plan("o\"dd",fields,[2,"Second","old"],1,"Changed")^
    let stale=changes.plan("o\"dd",fields,[1,"beta",null],1,"Wrong")^
    var conflict=false
    changes.apply_staged(path,[second,stale]) ^ {conflict=true}
    print(conflict and sqlite.query(path,"SELECT \"m\"\"sg\" FROM \"o\"\"dd\" WHERE id=2")==[["Second"]]); print("\n")
    var key_rejected=false
    changes.plan("o\"dd",fields,[1,"Beta",null],0,"bad") ^ {key_rejected=true}
    var nested_rejected=false
    changes.plan("o\"dd",fields,[1,"Beta",null],2,{bad:true}) ^ {nested_rejected=true}
    print(key_rejected and nested_rejected); print("\n")
    let stale_delete=changes.delete_plan("o\"dd",fields,[1,"beta",null])^
    var delete_conflict=false
    changes.apply_staged(path,[stale_delete]) ^ {delete_conflict=true}
    print(delete_conflict and sqlite.query(path,"SELECT COUNT(*) FROM \"o\"\"dd\"")==[[2]]); print("\n")
    let deletion=changes.delete_plan("o\"dd",fields,[2,"Second","old"])^
    changes.apply_staged(path,[deletion])^
    print(sqlite.query(path,"SELECT id FROM \"o\"\"dd\"")==[[1]]); print("\n")
    let insertion=changes.insert_plan("o\"dd",fields,{id:3,'m"sg':"New",note:null})^
    changes.apply_staged(path,[insertion])^
    print(sqlite.query(path,"SELECT id, \"m\"\"sg\", note FROM \"o\"\"dd\" WHERE id=3")==[[3,"New",null]]); print("\n")
    let pending=changes.insert_plan("o\"dd",fields,{id:4,'m"sg':"Pending"})^
    let duplicate=changes.insert_plan("o\"dd",fields,{id:1,'m"sg':"Duplicate"})^
    var insert_conflict=false
    changes.apply_staged(path,[pending,duplicate]) ^ {insert_conflict=true}
    print(insert_conflict and sqlite.query(path,"SELECT COUNT(*) FROM \"o\"\"dd\" WHERE id=4")==[[0]]); print("\n")
    var no_key=false
    changes.insert_plan("o\"dd",fields,{'m"sg':"Bad"}) ^ {no_key=true}
    print(no_key); print("\n")
    sqlite.execute(path,"CREATE TABLE typed (id INTEGER PRIMARY KEY, number INTEGER, data BLOB, note TEXT)")^
    sqlite.execute(path,"INSERT INTO typed VALUES (1,7,X'00FF',NULL)")^
    let typed=[{name:"id",primary:1},{name:"number",primary:0},{name:"data",primary:0},{name:"note",primary:0}]
    let numeric=changes.plan("typed",typed,[1,7,b'\x00FF',null],1,42)^
    changes.apply_staged(path,[numeric])^
    let blob=changes.plan("typed",typed,[1,42,b'\x00FF',null],2,binary(""))^
    changes.apply_staged(path,[blob])^
    let note=changes.plan("typed",typed,[1,42,binary(""),null],3,"present")^
    changes.apply_staged(path,[note])^
    print(sqlite.query(path,"SELECT number,data,note FROM typed")==[[42,binary(""),"present"]]); print("\n")
    let cleared=changes.plan("typed",typed,[1,42,binary(""),"present"],3,null)^
    changes.apply_staged(path,[cleared])^
    print(sqlite.query(path,"SELECT note FROM typed")==[[null]]); print("\n")
    let editor=changes.editor_value(b'\x00FF')^
    var bad_hex=false
    changes.parse_value("hex","0xz") ^ {bad_hex=true}
    var bad_json=false
    changes.parse_value("json","[1]") ^ {bad_json=true}
    print(editor.mode=="hex" and editor.text=="00ff" and changes.parse_value("hex","")^==binary("") and
        changes.parse_value("json","42")^==42 and bad_hex and bad_json); print("\n")
    let base_row=[1,42,binary(""),null]
    let change_number=changes.plan("typed",typed,base_row,1,43)^
    let change_note=changes.plan("typed",typed,base_row,3,"together")^
    let combined=changes.merge_updates(change_number,change_note)^
    let replacement=changes.plan("typed",typed,base_row,1,44)^
    let final_change=changes.merge_updates(combined,replacement)^
    changes.apply_staged(path,[final_change])^
    print(len(final_change.edits)==2 and sqlite.query(path,"SELECT number,note FROM typed")==[[44,"together"]]); print("\n")
    let different_storage=changes.plan("typed",typed,[1,42.0,binary(""),null],3,"other")^
    var stale_merge=false
    changes.merge_updates(combined,different_storage) ^ {stale_merge=true}
    print(stale_merge)
    io.delete(path)^
}
