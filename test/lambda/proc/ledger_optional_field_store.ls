type Rec = {a:int, opt?:int, tail:string}
pn add_field(var r:Rec) { r.opt = 7 }
pn main() {
    var record:Rec = {a:1,tail:"z"}
    let old = record
    add_field(record)
    print([record.opt,old.opt,len(record),len(old),record.tail])
}
