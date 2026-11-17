import t: lambda.sync.text
import p: lambda.sync.protocol
import codec: lambda.sync.codec

// independent whole-packet encoding checks exact limits, escaping and final `more` size.
fn reference(full, limit, max_ops) any^ {
    var selected = []
    var i = 0
    while (i < len(full.body.operations)^ and i < max_ops) {
        let candidate = [*selected,full.body.operations[i]]
        let packet = {*:full,body:{*:full.body,operations:candidate,more:i+1<len(full.body.operations)^}}
        if (len(binary(codec.encode(packet)^))^ > limit) {
            if (i == 0) raise error("sync protocol: operation exceeds batch limit")
            return {*:full,body:{*:full.body,operations:selected,more:true}}
        }
        selected = candidate
        i = i+1
    }
    return {*:full,body:{*:full.body,operations:selected,more:i<len(full.body.operations)^}}
}
fn outcome(value) => if (value is error) value.message else value
let initial = t.empty("batch-λ-😀-\"\\")
let changed = t.insert(initial,"author-λ",0,"λ😀\n\"\\abcdefghi")^
let have = p.have(initial)^
let full = p.reply(changed,have,p.default_schema,128,1048576)^
let sizes = [for (count in 1 to len(full.body.operations)^)
    len(binary(codec.encode({*:full,body:{*:full.body,operations:take(full.body.operations,count),
        more:count<len(full.body.operations)^}})^))^]
let limits = unique([1024,1048576,*[for (size in sizes) *[max(1024,size-1),max(1024,size),max(1024,size+1)]]])
let checks = [for (count in [1,3,128]) for (limit in limits)
    outcome(p.reply(changed,have,p.default_schema,count,limit) ^ { ^ }) ==
    outcome(reference(full,limit,count) ^ { ^ })];
all([len(checks)^ > 50,all(checks),
    not p.reply(changed,have,p.default_schema,128,sizes[len(sizes)^-1])^.body.more,
    p.reply(changed,have,p.default_schema,128,sizes[len(sizes)^-1]-1)^.body.more])
