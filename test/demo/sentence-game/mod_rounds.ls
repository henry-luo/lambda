// keep round drafts separate from the shared book and monotonic deal seed.
pub fn entry(st,id) => [for (saved in st.book where saved.round == id) saved][0]
pub fn finished(st,id) => entry(st,id) != null
fn snapshot(st,fields) => reduce([{},*fields],(draft,key)=>{*:draft,[key]:st[key]})
pub fn jump(st,target,total,fields,create,completed_phase) {
    if (not contains([for (id in 1 to total) id],target) or (target == st.round and st.phase != "done")) st
    else {
        let current = {*:snapshot(st,fields),phase:if (st.phase == "done") completed_phase else st.phase}
        let visits = {*:st.visits,[string(st.round)]:current}
        let draft = visits[string(target)] or snapshot(create(st.seed,target),fields)
        {*:st,*:draft,round:target,visits:visits,seed:max(st.seed,draft.seed)}
    }
}
pub fn requested(st,value,total,fields,create,completed_phase) =>
    if (contains([for (id in 1 to total) string(id)],value)) jump(st,int(value),total,fields,create,completed_phase) else st
pub fn pending(st,total) {
    let candidates = [for (offset in 1 to total) (st.round+offset-1)%total+1];
    [for (id in candidates where not finished(st,id)) id][0]
}
pub fn record(st,sentence) => [for (saved in sort(
    [*[for (saved in st.book where saved.round != st.round) saved],{*:sentence,round:st.round}],(saved)=>saved.round)) saved]
