import w: lambda.sync.workspace
import c: lambda.sync.core
import text: lambda.sync.text
import rules: lambda.sync.workspace_rules
import change: lambda.sync.change

let content_data = w.content("data")^
let edited = w.transact(w.start("workspace-errors")^,"seed",[
    {kind:'create',type:'dir',parent:w.root,name:"dir"},
    {kind:'create',type:'file',parent:w.root,name:"file",content:content_data.reference}
])^
let base = edited.endpoint.replica
let malicious = {action:'create',type:'dir',parent:w.root,name:"tampered",revision:null}
let first = base.atoms[0]
let corrupted = {*:base,atoms:[{*:first,value:{kind:'workspace',event:malicious}},base.atoms[1]]};
all([
    w.transact(edited.endpoint,"x",[{kind:'move',entry:["seed",2],parent:["seed",2]}]) ^ { ^.message != "" },
    w.transact(edited.endpoint,"x",[{kind:'create',type:'dir',parent:["seed",2],name:"child"}]) ^ { ^.message != "" },
    w.transact(edited.endpoint,"x",[{kind:'create',type:'dir',parent:w.root,name:"../bad"}]) ^ { ^.message != "" },
    w.transact(edited.endpoint,"x",[{kind:'delete',entry:w.root}]) ^ { ^.message != "" },
    w.transact(edited.endpoint,"x",[{kind:'restore',entry:["seed",2],deletions:[["seed",1]]}]) ^ { ^.message != "" },
    w.transact(edited.endpoint,"x",[{kind:'publish',entry:["seed",1],content:content_data.reference}]) ^ { ^.message != "" },
    c.set_field(base,"x",w.root,"bad",c.scalar(1)) ^ { ^.message == "sync workspace: only immutable root events are allowed" },
    c.insert_after(text.empty("text"),"x",w.root,'items',null,{kind:'workspace',event:malicious}) ^ {
        ^.message == "sync workspace: event outside workspace profile" },
    c.validate(c.compact(corrupted)) ^ { ^.message == "sync workspace: event receipt mismatch" },
    c.insert_after(base,"x",w.root,'items',null,{kind:'workspace',event:
        {action:'place',entry:["missing",1],parent:w.root,name:"bad"}}) ^ { ^.message != "" },
    change.undo(base,"undo",edited.undo) ^ { ^.message == "sync workspace: compensate using explicit workspace commands" },
    w.payload(content_data.reference,[{id:content_data.reference.manifest,bytes:binary("wrong")^}]) ^ {
        ^.message == "sync workspace: missing or corrupt content" },
    rules.revision(base.document,base.epoch,["seed",2],[hash("missing",'sha256')^],content_data.reference)^.parents != [],
    rules.revision("space-a",0,["same",1],[],content_data.reference)^.id !=
        rules.revision("space-b",0,["same",1],[],content_data.reference)^.id,
    rules.revision("space-a",0,["same",1],[],content_data.reference)^.id !=
        rules.revision("space-a",1,["same",1],[],content_data.reference)^.id,
    c.insert_after(base,"x",w.root,'items',null,{kind:'workspace',event:{action:'publish',entry:["seed",2],
        revision:rules.revision("other",0,["seed",2],[],content_data.reference)^}}) ^ {
            ^.message == "sync workspace: revision file mismatch" },
    c.validate(base)^
])
