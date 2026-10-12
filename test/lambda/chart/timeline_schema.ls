import s: lambda.chart.timeline.schema
let spec = {id:"sample",view:"axis",events:[{id:"late",at:"2026",title:"Late"},{id:"early",at:"2019",title:"Early"},{id:"tie",at:"2019",title:"Tie"}]}
let normalized = s.validate(spec)
let authored = s.validate(<timeline.chart id:"sample",view:'axis',
    <timeline.event id:"late",at:"2026",title:"Late">
    <timeline.event id:"early",at:"2019",title:"Early">
    <timeline.event id:"tie",at:"2019",title:"Tie">
>);
[
 ["element/map equivalence",normalized == authored],
 ["stable chronological ties",(normalized.events |> ~.id) == ["early","tie","late"]],
 ["sequence source order",(s.validate({*:spec,view:"cards"}).events |> ~.id) == ["late","early","tie"]],
 ["reverse chronology stable",(s.validate({*:spec,order:"reverse_chronological"}).events |> ~.id) == ["late","early","tie"]],
 ["symbol ID normalization",s.validate({events:[{id:'a'}]}).events[0].id == "a"],
 ["duplicate normalized IDs",s.validate({events:[{id:'a'},{id:"a"}]}) is error],
 ["unknown root field",s.validate({unexpected:1}) is error],
 ["unknown event field",s.validate({events:[{id:"a",unexpected:1}]}) is error],
 ["unknown child",s.validate(<timeline.chart <timeline.unexpected>>) is error],
 ["singleton duplication",s.validate(<timeline.chart <timeline.window> <timeline.window>>) is error],
 ["slot duplication",s.validate(<timeline.chart <timeline.event id:"a",<timeline.content "A"> <timeline.content "B">>>) is error],
 ["bad view",s.validate({view:"other"}) is error],
 ["axis dates required",s.validate({view:"axis",events:[{id:"a"}]}) is error],
 ["mixed date shapes",s.validate({events:[{at:"2020",from:"2020",to:"2021"}]}) is error],
 ["reversed range",s.validate({events:[{from:"2021",to:"2020"}]}) is error],
 ["missing group",s.validate({events:[{group:"missing"}]}) is error],
 ["group cycle",s.validate({groups:[{id:"a",parent:"b"},{id:"b",parent:"a"}]}) is error],
 ["nested group normalization",s.validate({groups:[{id:"a",groups:[{id:"b"}]}]}).groups[1].parent == "a"],
 ["unknown config field",s.validate({interaction:{typo:true}}) is error],
 ["boolean validation",s.validate({interaction:{pan:1}}) is error],
 ["controlled conflict",s.validate({value:{active_id:null},default_value:{}}) is error],
 ["nonpositive size",s.validate({width:0}) is error],
 ["data/source conflict",s.validate({data:[],source:{url:"x"}}) is error],
 ["record encoding",s.validate({data:[{key:"a",year:"2020",heading:"Hello"}],encoding:{id:{field:"key"},at:{field:"year"},title:{field:"heading"}}}).events[0].title == "Hello"],
 ["curve combination validation",s.validate({coordinate:{kind:"spiral"}}) is error]
]
