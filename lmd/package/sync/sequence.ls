// RGA adapters shared by text, arrays, map key order and element content.
import c: .core

pub fn check_range(replica, owner, slot, start, count) any^ {
    let items = c.sequence(replica,owner,slot)^
    if (not (start is int) or not (count is int) or start < 0 or count < 0 or start+count > len(items))
        raise error("sync: sequence range out of bounds")
    return items
}

pub fn insert(replica, actor, owner, slot, index, values) any^ {
    let items = check_range(replica,owner,slot,index,0)^
    var result = replica
    var left = if (index == 0) null else items[index-1].id
    var i = 0
    while (i < len(values)) {
        let id = c.next_id(result,actor)
        result = c.insert_after(result,actor,owner,slot,left,values[i])^
        left = id
        i = i + 1
    }
    return result
}

pub fn remove(replica, actor, owner, slot, start, count) any^ {
    let items = check_range(replica,owner,slot,start,count)^
    let ids = [for (i in start to (start+count-1)) items[i].id]
    return c.delete_ids(replica,actor,owner,slot,ids)^
}

// quadratic LCS is an intentionally simple correspondence oracle for the prototype.
fn lengths(before, after) any^ {
    var rows = [fill(len(after)+1,0)]
    var i = len(before)-1
    while (i >= 0) {
        var row = [0]
        var j = len(after)-1
        while (j >= 0) {
            let score = if (before[i] == after[j]) rows[0][j+1]+1 else max(rows[0][j],row[0])
            row = [score,*row]
            j = j-1
        }
        rows = [row,*rows]
        i = i-1
    }
    return rows
}

pub fn correspondence(before, after) any^ {
    let rows = lengths(before,after)^
    let backwards = lengths(reverse(before),reverse(after))^
    // every pair on any optimal alignment: more than LCS length means non-uniqueness.
    let possible = [for (i in 0 to (len(before)-1)) for (j in 0 to (len(after)-1)
        where before[i] == after[j] and backwards[len(before)-i][len(after)-j]+1+rows[i+1][j+1] == rows[0][0]) [i,j]]
    var edits = []
    var i = 0
    var j = 0
    while (i < len(before) or j < len(after)) {
        if (i < len(before) and j < len(after) and before[i] == after[j]) {
            edits = [*edits,{kind:'keep',before:i,after:j}]
            i = i+1
            j = j+1
        } else if (i < len(before) and (j == len(after) or rows[i+1][j] >= rows[i][j+1])) {
            edits = [*edits,{kind:'delete',before:i,after:j}]
            i = i+1
        } else {
            edits = [*edits,{kind:'insert',before:i,after:j}]
            j = j+1
        }
    }
    return {changes:edits,ambiguous:len(possible) > rows[0][0]}
}

pub fn difference(before, after) any^ => correspondence(before,after)^.changes

pub fn replace_scalars(replica, actor, owner, slot, values) any^ {
    let items = c.sequence(replica,owner,slot)^
    let changes = difference([for (item in items) item.value.value],values)^
    let removed = [for (change in changes where change.kind == 'delete') items[change.before].id]
    var result = c.delete_ids(replica,actor,owner,slot,removed)^
    var left = null
    var i = 0
    while (i < len(changes)) {
        let change = changes[i]
        if (change.kind == 'keep') { left = items[change.before].id }
        if (change.kind == 'insert') {
            let id = c.next_id(result,actor)
            result = c.insert_after(result,actor,owner,slot,left,c.scalar(values[change.after]))^
            left = id
        }
        i = i+1
    }
    return result
}
