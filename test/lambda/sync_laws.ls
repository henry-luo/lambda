import c: lambda.sync.core
import t: lambda.sync.text

fn permutations(items) any^ => if (len(items) == 0) [[]] else
    [for (i in 0 to (len(items)-1))
        for (tail in permutations([for (j,item in items where j != i) item])^) [items[i],*tail]]

fn deliver(initial, operations) any^ {
    var replica = initial
    var i = 0
    while (i < len(operations)) {
        replica = c.receive(replica,[operations[i]])^
        i = i+1
    }
    return replica
}

let initial = t.empty("schedule")
let alice = t.remove(t.insert(initial,"alice",0,"ab")^,"alice",0,1)^
let bob = t.insert(initial,"bob",0,"Z!")^
let target = c.merge(alice,bob)^
let schedules = permutations(target.log)^
let outcomes = [for (schedule in schedules) c.compact(deliver(initial,schedule)^)]
let partial = c.receive(initial,[alice.log[2]])^
let recovered = c.receive(c.compact(partial),target.log)^
let cc = c.empty("register-laws",'mark','map',null)
let a = c.set_field(cc,"alice",c.root,"n",c.scalar(1))^
let b = c.set_field(cc,"bob",c.root,"n",c.scalar(2))^
let z = c.set_field(cc,"charlie",c.root,"n",c.scalar(3))^
let resolved = c.set_field(c.merge(a,b)^,"alice",c.root,"n",c.scalar(4))^
let entries = c.field(c.merge(resolved,z)^,c.root,"n")
let words = ["","a","b","aa","ab","ba","bb","aaa","aab","aba","abb","baa","bab","bba","bbb"];
[len(schedules) == 120,
 all([for (outcome in outcomes) outcome == c.compact(target)]),
 t.read(target)^ == "Z!b",
 c.compact(c.receive(target,[*target.log,*target.log])^) == c.compact(target),
 c.merge(partial,partial)^ == partial,
 c.compact(recovered) == c.compact(target),
 len(partial.pending) == 1 and len(recovered.pending) == 0,
 c.compact(c.merge(c.merge(a,b)^,z)^) == c.compact(c.merge(a,c.merge(b,z)^)^),
 c.compact(c.merge(resolved,z)^) == c.compact(c.merge(z,resolved)^),
 sort([for (entry in entries) entry.value.value]) == [3,4],
 c.compact(c.merge(c.compact(resolved),a)^) == c.compact(resolved),
 all([for (before in words) for (after in words)
     t.read(t.replace(t.from_text("diff","seed",before)^,"import",after)^)^ == after])]
