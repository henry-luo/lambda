import tabs: ~~.mod_tabs

let first={id:1,state:{generation:2,rows:[[1]]},worker:null,uri:"sample"}
let second={id:2,state:{generation:1,rows:[[2]]},worker:null,uri:null}
let all=[first,second]
let updated=tabs.update(all,1,{state:{generation:3,rows:[[3]]}});
[
    tabs.LIMIT==4,
    tabs.get(all,2)==second and tabs.get(all,99)==null,
    tabs.get(updated,1).state.rows==[[3]] and tabs.get(updated,2)==second,
    tabs.remove(updated,1)==[second],
    tabs.accepts(tabs.get(updated,1),{tab:1,generation:3}),
    not tabs.accepts(tabs.get(updated,1),{tab:1,generation:2}),
    not tabs.accepts(tabs.get(updated,1),{tab:2,generation:3}),
    not tabs.accepts(null,{tab:1,generation:3}),
    tabs.neighbor(all,1,1)==2 and tabs.neighbor(all,1,-1)==2 and tabs.neighbor(all,99,1)==null
]
