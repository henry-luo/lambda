// Unicode scalar positions, sharing the same RGA as structured child sequences.
import c: .core
import seq: .sequence

pub fn empty(document) => c.empty(document,'text','text',null)

pub fn read(replica, owner=c.root) any^ =>
    join([for (item in c.sequence(replica,owner,'items')^) item.value.value],"")^

pub fn insert(replica, actor, index, value, owner=c.root) any^ {
    if (not (value is string)) raise error("sync: text insertion requires a string")
    let values = [for (i in 0 to (len(value)-1)) c.scalar(value[i])]
    return seq.insert(replica,actor,owner,'items',index,values)^
}

pub fn remove(replica, actor, start, count, owner=c.root) any^ =>
    seq.remove(replica,actor,owner,'items',start,count)^

pub fn splice(replica, actor, start, count, value, owner=c.root) any^ =>
    insert(remove(replica,actor,start,count,owner)^,actor,start,value,owner)^

pub fn replace(replica, actor, value, owner=c.root) any^ {
    if (not (value is string)) raise error("sync: external text requires a string")
    let values = [for (i in 0 to (len(value)-1)) value[i]]
    return seq.replace_scalars(replica,actor,owner,'items',values)^
}

pub fn from_text(document, actor, value) any^ => insert(empty(document),actor,0,value)^

pub fn reconcile(replica, actor, value, owner=c.root) any^ {
    if (not (value is string)) raise error("sync: external text requires a string")
    if (seq.correspondence(read(replica,owner)^,value)^.ambiguous)
        raise error("sync: ambiguous text correspondence")
    return replace(replica,actor,value,owner)^
}
