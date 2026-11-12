import r: lambda.sync.retention

fn header(id, parents, at, arrival=null, group="doc") any^ => {id:id,parents:parents,group:group,
    saved_at:datetime(at)^.unix,arrived_at:datetime(if (arrival == null) at else arrival)^.unix}
let a = header("a",[],"2026-11-01T09:00:00Z")
let b = header("b",["a"],"2026-11-01T23:00:00Z")
let c = header("c",["b"],"2026-11-02T11:00:00Z")
let d = header("d",["c"],"2026-11-02T23:00:00Z")
let all_versions = [a,b,c,d]
let now = datetime("2026-11-03T12:00:00Z")^.unix
let daily = r.daily("UTC",0,0)
let selected = r.select(all_versions,daily,now)^
let pinned = r.select(all_versions,daily,now,["a"])^
let late = {*:a,arrived_at:now}
let grace = r.select([late,b,c,d],r.daily("UTC",86400000,0),now)^
let today = header("e",["d"],"2026-11-03T01:00:00Z")
let concurrent = header("x",["a"],"2026-11-01T13:00:00Z")
let month = {*:daily,rules:[{unit:'month',count:null}]}
let tiered = {*:daily,rules:[{unit:'day',count:1},{unit:'month',count:null}]}
let invalid:any^ = r.select(all_versions,{*:daily,timezone:"Invalid/Zone"},now);
[
 r.select(all_versions,r.permanent,now)^.candidates == [],
 selected.retained == ["b","d"], selected.candidates == ["a","c"],
 pinned.retained == ["a","b","d"], contains(grace.retained,"a")^,
 contains(r.select([*all_versions,today],daily,now)^.retained,"e")^,
 contains(r.select([*all_versions,concurrent],daily,now)^.retained,"x")^,
 r.select(all_versions,month,datetime("2026-12-01T00:00:00Z")^.unix)^.retained == ["d"],
 r.select(all_versions,tiered,now)^.retained == ["d"],invalid is error,
 r.bucket(datetime("2026-11-01T05:30:00Z")^.unix,'day',"America/New_York")^ ==
    r.bucket(datetime("2026-11-01T06:30:00Z")^.unix,'day',"America/New_York")^,
 r.bucket(datetime("2026-03-08T06:59:00Z")^.unix,'day',"America/New_York")^ ==
    r.bucket(datetime("2026-03-08T07:01:00Z")^.unix,'day',"America/New_York")^,
 r.select([a,{*:b,saved_at:a.saved_at-1000}],daily,now)^.retained == ["b"]
]
