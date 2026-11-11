// atomic editor transactions and bounded selective compensating undo.
import c: .core
import text: .text
import mark: .mark
import codec: .codec
import workspace: .workspace_commands

fn owner(command) => if (command.node == null) c.root else command.node

pub fn execute(replica, actor, commands) any^ {
    if (not (commands is array)) raise error("sync change: commands must be an array")
    if (replica.profile == 'workspace') { return workspace.execute(replica,actor,commands)^ }
    var result = replica
    var i = 0
    while (i < len(commands)) {
        let command = commands[i]
        if (not contains(['text','set','remove','splice','tag','order','scalar','blob','resolve','root'],command.kind)^)
            raise error("sync change: unknown command")
        let target = owner(command)
        if (command.kind == 'text') { result = text.splice(result,actor,command.start,command.count,command.value,target)^ }
        else if (command.kind == 'set') { result = mark.set(result,actor,command.key,command.value,target)^ }
        else if (command.kind == 'remove') { result = mark.remove_field(result,actor,command.key,target)^ }
        else if (command.kind == 'splice') { result = mark.splice(result,actor,command.start,command.count,command.values,target)^ }
        else if (command.kind == 'tag') { result = mark.tag(result,actor,command.value,target)^ }
        else if (command.kind == 'order') { result = mark.reorder(result,actor,command.keys,target)^ }
        else if (command.kind == 'scalar') { result = mark.replace_scalar(result,actor,command.value,target)^ }
        else if (command.kind == 'blob') { result = mark.set_blob(result,actor,command.key,command.value,target)^ }
        else if (command.kind == 'resolve') { result = mark.resolve_field(result,actor,command.key,command.alternative,target)^ }
        else if (command.kind == 'root') { result = mark.replace_root(result,actor,command.value)^ }

        i = i+1
    }
    return result
}

pub fn batch(before, after) any^ {
    if (len(before.pending) != 0 or len(after.pending) != 0 or not c.dominates(after.clock,before.clock)^)
        raise error("sync change: transaction requires complete causal endpoints")
    let operations = sort([for (op in after.log where not c.covered(before.clock,op.id)^) op],(op)=>[op.time,op.id])^
    let replayed = c.receive(before,operations)^
    if (c.compact(replayed) != c.compact(after)) raise error("sync change: transaction packets unavailable or endpoints differ")
    let body = {base:before.clock,frontier:after.clock,operations:operations}
    return {id:codec.digest([before.document,before.profile,before.epoch,body])^,*:body}
}

pub fn transaction(replica, actor, commands) any^ {
    let updated = execute(replica,actor,commands)^
    let packet = batch(replica,updated)^
    return {replica:updated,batch:packet,undo:{before:c.snapshot(replica)^,after:c.snapshot(updated)^,batch:packet}}
}

fn restore_registers(current, before, after, actor, meta) any^ {
    let previous = if (meta) before.meta else before.cells
    let final = if (meta) after.meta else after.cells
    let present = if (meta) current.meta else current.cells
    let touched = unique([for (op in after.receipts where not c.covered(before.clock,op.id)^)
        for (cell in final where cell.id == op.id) [cell.node,cell.key]])
    var result = current
    var i = 0
    while (i < len(touched)) {
        let target = touched[i]
        if (c.node(before,target[0])^ == null) { i = i+1; continue }
        let old = [for (cell in previous where cell.node == target[0] and cell.key == target[1]) cell]
        let final_entries = [for (cell in final where cell.node == target[0] and cell.key == target[1]) cell]
        let now = [for (cell in present where cell.node == target[0] and cell.key == target[1]) cell]
        if (now != final_entries or len(old) > 1) raise error("sync change: undo field changed; explicit resolution required")
        if (meta) {
            let value = if (len(old) == 1) old[0].value else
                (if (target[1] == "tag") c.node(before,target[0])^.tag else
                    unique([for (atom in c.sequence(before,target[0],'keys')^) atom.value.value]))
            result = c.set_meta(result,actor,target[0],target[1],value)^
        } else {
            result = c.set_field(result,actor,target[0],target[1],if (len(old) == 1) old[0].value else c.absent())^
        }
        i = i+1
    }
    return result
}

pub fn undo(replica, actor, token) any^ {
    if (replica.profile == 'workspace') raise error("sync workspace: compensate using explicit workspace commands")
    let before = c.restore(token.before)^
    let after = c.restore(token.after)^
    // the pinned endpoints retain payloads; the live compacted state need not.
    if (not c.dominates(replica.clock,after.clock)^ or
        token.batch.base != before.clock or token.batch.frontier != after.clock or
        token.batch.id != codec.digest([before.document,before.profile,before.epoch,
            {base:token.batch.base,frontier:token.batch.frontier,operations:token.batch.operations}])^)
        raise error("sync change: invalid undo token")
    if (c.compact(c.receive(before,token.batch.operations)^) != c.compact(after))
        raise error("sync change: undo endpoints do not match transaction")
    c.merge(replica,after)^
    var result = restore_registers(replica,before,after,actor,false)^
    result = restore_registers(result,before,after,actor,true)^
    let introduced = [for (atom in after.atoms where not c.covered(before.clock,atom.id)^ and
        not contains(result.dead,atom.id)^) atom]
    var i = 0
    while (i < len(introduced)) {
        let atom = introduced[i]
        result = c.delete_ids(result,actor,atom.node,atom.slot,[atom.id])^
        i = i+1
    }
    let removed = [for (atom in before.atoms where not contains(before.dead,atom.id)^ and
        contains(after.dead,atom.id)^) atom]
    i = 0
    while (i < len(removed)) {
        let atom = removed[i]
        result = c.insert_after(result,actor,atom.node,atom.slot,atom.left,atom.value)^
        i = i+1
    }
    return result
}
