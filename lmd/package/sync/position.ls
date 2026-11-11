// stable gaps for cursors/selections; deleted RGA anchors remain resolvable.
import c: .core
import seq: .sequence
import v: .schema

pub fn at(replica, index, bias='right', owner=c.root, slot='items') any^ {
    let items = seq.check_range(replica,owner,slot,index,0)^
    if (not contains(['left','right'],bias)) raise error("sync: invalid position bias")
    return {document:replica.document,epoch:replica.epoch,node:owner,slot:slot,bias:bias,
        left:if (index == 0) null else items[index-1].id,
        right:if (index == len(items)) null else items[index].id}
}

pub fn resolve(replica, position) any^ {
    v.fields(position,["document","epoch","node","slot","bias","left","right"])^
    if (position.document != replica.document or position.epoch != replica.epoch or
        not contains(['left','right'],position.bias)^ or not contains(['items','keys'],position.slot)^ or
        c.node(replica,position.node)^ == null) raise error("sync: incompatible position")
    let all = c.walk(replica,position.node,position.slot)^
    let anchor = if (position.bias == 'left') position.left else position.right
    if (anchor == null) { return if (position.bias == 'left') 0 else len(c.sequence(replica,position.node,position.slot)^) }
    if (not any([for (item in all) item.id == anchor])) raise error("sync: position anchor is absent")
    var result = 0
    var i = 0
    while (i < len(all)) {
        let item = all[i]
        let visible = not contains(replica.dead,item.id)^
        if (item.id == anchor) { return result + (if (position.bias == 'left' and visible) 1 else 0) }
        if (visible) { result = result+1 }
        i = i+1
    }
    return result
}
