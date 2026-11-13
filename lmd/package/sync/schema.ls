// shared admission checks, independent of transport and the merge engine.
pub let root = ["",0]

pub fn lookup(entries, id) {
    let found = [for (entry in entries where entry.id == id) entry]
    if (len(found) == 0) null else found[0]
}

// Causal indexes are validated/sorted by (actor, numeric counter), never by serialized text.
pub fn lookup_id(entries, id) {
    var lo = 0
    var hi = len(entries)
    while (lo < hi) {
        let mid = (lo+hi) div 2
        let entry = entries[mid]
        if (entry.id == id) { return entry }
        if (entry.id[0] < id[0] or (entry.id[0] == id[0] and entry.id[1] < id[1])) { lo = mid+1 }
        else { hi = mid }
    }
    return null
}

pub fn record(value, required, optional=[]) any^ {
    if (not (value is map)) raise error("sync: expected record")
    let names = [for (key at value) string(key)]
    if (not all([for (key in required) contains(names,key)^]) or
        not all([for (key in names) contains([*required,*optional],key)^]))
        raise error("sync: invalid record fields")
    return true
}

pub fn fields(value, names) any^ {
    if (not (value is map) or sort([for (key at value) string(key)]) != sort(names))
        raise error("sync: invalid record fields")
    return true
}

pub fn natural(value) => value is int and value >= 0 and value < 9007199254740991
pub fn actor(value) => value is string and value != "" and len(value) <= 256
pub fn id(value, allow_root=false) any^ => value is array and len(value) == 2 and
    ((actor(value[0]^)^ and natural(value[1]^)^ and value[1] > 0) or (allow_root and value == ["",0]))

pub fn clock_get(clock, author) {
    let found = [for (entry in clock where entry.actor == author) entry.counter]
    if (len(found) == 0) 0 else found[0]
}

pub fn clock(value) any^ {
    if (not (value is array)) raise error("sync: invalid causal context")
    var i = 0
    var previous = ""
    while (i < len(value)) {
        let entry = value[i]
        fields(entry,["actor","counter"])^
        if (not actor(entry.actor)^ or not natural(entry.counter)^ or entry.counter <= 0 or entry.actor <= previous)
            raise error("sync: invalid causal context entry")
        previous = entry.actor
        i = i+1
    }
    return true
}

pub fn scalar(value) => value is null or value is bool or value is int or value is float or
    value is integer or value is decimal or value is string or value is symbol or value is binary or value is datetime

pub fn descriptor(value) any^ {
    if (not (value is map)) raise error("sync: invalid value descriptor")
    if (value.kind == 'scalar') {
        fields(value,["kind","value"])^
        if (not scalar(value.value)) raise error("sync: scalar descriptor contains a container")
        if (value.value is binary and len(value.value) > 65536) raise error("sync: inline binary limit; use a blob reference")
    } else if (value.kind == 'node') {
        fields(value,["kind","id"])^
        if (not id(value.id)) raise error("sync: invalid child ID")
    } else if (value.kind == 'blob') {
        fields(value,["kind","manifest","size","hash","media_type"])^
        if (not (value.manifest is binary) or len(value.manifest) != 32 or not natural(value.size)^ or
            not (value.hash is binary) or len(value.hash) != 32 or not (value.media_type is string))
            raise error("sync: invalid blob descriptor")
    } else if (value.kind == 'workspace') {
        fields(value,["kind","event"])^
        if (not (value.event is map)) raise error("sync: invalid workspace event")
    } else if (value.kind == 'absent') { fields(value,["kind"])^ }
    else { raise error("sync: invalid value descriptor") }
    return true
}
