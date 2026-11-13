import schema: lambda.sync.schema
let entries = sort([for (actor in ["z","a:","a","é","🦀",""])
    for (counter in [0,1,2,9,10,100]) {id:[actor,counter],value:actor ++ string(counter)}],(entry)=>entry.id)^
let targets = [*[for (entry in entries) entry.id],["missing",1],["a",3],["é",101],["",-1]]
all([all([for (id in targets) schema.lookup_id(entries,id) == schema.lookup(entries,id)]),
    schema.lookup_id([], ["a",1]) == null])
