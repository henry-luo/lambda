import t: lambda.chart.timeline
import bookmark: lambda.chart.timeline.bookmark
let raw={id:"history / 城市",view:"story",spacing:"sequence",story:{bookmarks:true,title_slide:{id:"intro",title:"Welcome"}},
    events:[{id:"first + /",title:"First"},{id:"époque",title:"Second"}]}
let spec=t.validate(raw)
let initial=t.initial_state(raw)
let address=t.bookmark(raw,"époque")
let restored=t.update(initial,{type:"bookmark",fragment:address},{spec:raw,now:0})
let controlled={*:raw,value:{active_id:"intro"}}
let request=t.update(t.initial_state(controlled),{type:"bookmark",fragment:address},{spec:controlled,now:0})
let next=t.update(initial,{type:"next"},{spec:raw,now:0});
[
 ["encoded chart and event",address=="#timeline=history+%2F+%E5%9F%8E%E5%B8%82/%C3%A9poque"],
 ["reserved characters",t.bookmark(raw,"first + /")=="#timeline=history+%2F+%E5%9F%8E%E5%B8%82/first+%2B+%2F"],
 ["resolve authored ID",bookmark.resolve(spec,address)=="époque"],
 ["introduction address",bookmark.resolve(spec,t.bookmark(raw,"intro"))=="intro"],
 ["restore active event",restored.state.active_id=="époque"],
 ["bookmark notification",(restored.notifications |: ~.name=="timeline_bookmark")[0].detail.fragment==address],
 ["manual navigation publishes address",(next.notifications |: ~.name=="timeline_bookmark")[0].detail.fragment==t.bookmark(raw,"first + /")],
 ["controlled bookmark requests activation",request.state.active_id=="intro" and request.requests[0].value=="époque" and len(request.notifications |: ~.name=="timeline_bookmark")==0],
 ["unrelated fragments ignored",t.update(initial,{type:"bookmark",fragment:"#other-chart"},{spec:raw,now:0}).state==initial],
 ["missing addressed event diagnosed",t.update(initial,{type:"bookmark",fragment:"#timeline=history+%2F+%E5%9F%8E%E5%B8%82/removed"},{spec:raw,now:0}) is error],
 ["unknown event cannot be linked",t.bookmark(raw,"removed") is error],
 ["authored chart name required",t.validate({events:[],story:{bookmarks:true}}) is error],
 ["authored stable event IDs required",t.validate({id:"unstable",events:[{title:"No ID"}],story:{bookmarks:true}}) is error],
 ["adapter IDs retain unstable provenance",t.validate(t.convert([{title:"Date",cardTitle:"No ID"}],"chrono")).events[0].unstable_id==true],
 ["ordinary bookmark link presentation",contains(format(t.render(raw,{width:600,height:200}),"html"),"lambda-timeline-bookmark")]
]
