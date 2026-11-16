import workspace: lambda.sync.workspace
import core: lambda.sync.core
import codec: lambda.sync.codec
let reference = workspace.content("value")^.reference
let endpoint = workspace.transact(workspace.start("validation-reuse")^,"writer",[
    {kind:'create',type:'file',parent:workspace.root,name:"file",content:reference}])^.endpoint
let known = endpoint.replica
let atom = known.atoms[0]
let evidence = known.receipts[0]
let candidates = [known,
    {*:known,protocol:float(known.protocol)^},
    {*:known,time:float(known.time)^},
    {*:known,epoch:float(known.epoch)^},
    {*:known,atoms:[{*:atom,value:{*:atom.value,event:{*:atom.value.event,name:"forged"}}}]},
    {*:known,receipts:[{*:evidence,hash:hash("forged",'sha256')^}]},
    {*:known,nodes:[{*:known.nodes[0],kind:'file'}]},
    {*:known,atoms:[{*:atom,time:atom.time+1}]}]
let checked = [for (candidate in candidates) {
    let full = core.validate(candidate) ^ { ^ }
    let reused = core.validate_known(candidate,known) ^ { ^ };
    (full is error) == (reused is error)
}];
all([*checked,core.validate_known(known,known)^,
    codec.encode(known)^ == codec.encode(endpoint.replica)^])
