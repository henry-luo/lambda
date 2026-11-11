// command lowering keeps transaction-local aliases out of replicated state.
import c: .core
import v: .schema
import tree: .workspace_tree
import rules: .workspace_rules

fn resolve(value, aliases) any^ {
    if (value is string) {
        let found = v.lookup(aliases,value)^
        if (found == null) raise error("sync workspace: unknown transaction alias")
        return found.value
    }
    if (not v.id(value,true)^) raise error("sync workspace: invalid entry reference")
    return value
}

pub fn execute(replica, actor, commands) any^ {
    if (replica.profile != 'workspace' or not (commands is array)) raise error("sync workspace: invalid command group")
    var result = replica
    var aliases = []
    var i = 0
    while (i < len(commands)) {
        let command = commands[i]
        if (not contains(['create','move','delete','restore','publish'],command.kind)^)
            raise error("sync workspace: unknown command")
        let events = tree.events(result)^
        var event = null
        if (command.kind == 'create') {
            v.record(command,["kind","type","parent","name"],["content","document","as"])^
            let id = c.next_id(result,actor)
            event = {action:'create',type:command.type,parent:resolve(command.parent,aliases)^,name:command.name,
                revision:if (command.type == 'file') rules.revision(result.document,result.epoch,id,[],command.content,command.document)^ else null}
            if (command.type == 'dir' and (command.content != null or command.document != null))
                raise error("sync workspace: directory cannot contain a file revision")
            if (command.as != null) {
                if (not v.actor(command.as)^ or v.lookup(aliases,command.as)^ != null) raise error("sync workspace: duplicate or invalid alias")
                aliases = [*aliases,{id:command.as,value:id}]
            }
        } else {
            let entry = resolve(command.entry,aliases)^
            if (command.kind == 'move') {
                v.record(command,["kind","entry"],["parent","name"])^
                let entries = tree.placements(events)^.entries
                let current = v.lookup(entries,entry)^
                if (current == null) raise error("sync workspace: missing moved entry")
                let parent = if (command.parent == null) current.parent else resolve(command.parent,aliases)^
                if (tree.ancestor(entries,parent,entry)^) raise error("sync workspace: local move would create a cycle")
                event = {action:'place',entry:entry,parent:parent,name:if (command.name == null) current.name else command.name}
            } else if (command.kind == 'delete') {
                v.fields(command,["kind","entry"])^
                event = {action:'delete',entry:entry}
            } else if (command.kind == 'restore') {
                v.record(command,["kind","entry"],["deletions"])^
                let ids = if (command.deletions == null) [for (deleted in tree.deletions(events,entry)^) deleted.id] else command.deletions
                event = {action:'restore',entry:entry,deletions:sort(ids)^}
            } else if (command.kind == 'publish') {
                v.record(command,["kind","entry","content"],["parents","document"])^
                let parents = if (command.parents == null) [for (head in tree.heads(events,entry)^) head.id] else command.parents
                event = {action:'publish',entry:entry,revision:rules.revision(result.document,result.epoch,entry,parents,command.content,command.document)^}
            }
        }
        rules.shape(event)^
        result = c.insert_after(result,actor,c.root,'items',null,{kind:'workspace',event:event})^
        i = i+1
    }
    return result
}
