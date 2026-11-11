import c: lambda.sync.core
import t: lambda.sync.text
import m: lambda.sync.mark

fn fails(outcome:any) => outcome is error
let base = t.from_text("validation","seed","ab")^
let next = t.insert(base,"author",1,"X")^
let op = next.log[0]
let unobserved = {*:op,context:[]}
let backwards = {*:op,time:1}
let forged = {*:op,value:c.scalar({nested:"not atomic"})}
let corrupt_atom = {*:base.atoms[0],left:base.atoms[1].id}
let bad_state = {*:base,atoms:[corrupt_atom,base.atoms[1]]}
let saved = c.snapshot(next)^
let corrupt_saved = {*:saved,replica:{*:saved.replica,time:999}}
let tree = m.from_value("tree-validation","seed",{nested:{a:1}})^
let child = m.child(tree,"nested")^
let child_op = m.set(tree,"author","b",2,child)^.log[0];
all([c.validate(base)^,c.validate(c.compact(next))^,c.validate(tree)^,
 fails(c.receive(base,[unobserved])),fails(c.receive(base,[backwards])),
 fails(c.receive(base,[forged])),fails(c.receive(tree,[{*:child_op,context:[]}])) ,
 fails(c.validate(bad_state)),fails(c.restore(corrupt_saved)),
 fails(c.validate({*:base,receipts:take(base.receipts,1)})),
 fails(c.validate({*:base,log:[]})),
 fails(c.merge(base,{*:base,atoms:[{*:base.atoms[0],value:c.scalar("Q")},base.atoms[1]]})),
 fails(c.receive(base,[{*:op,extra:true}])),
 fails(c.receive(base,[{*:op,context:[{actor:"seed",counter:1}],left:base.atoms[1].id}]))])
