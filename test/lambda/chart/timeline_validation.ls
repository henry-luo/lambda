import timeline: lambda.chart.timeline

let spec={id:"valid",view:"axis",width:800,height:400,events:[{id:"a",at:"2000"},{id:"b",at:"2020"}]}
let domain={*:spec,scales:[{domain:["1900","2100"]}]}
let plan=timeline.layout(domain,{width:800,height:400});
[
 ["duplicate scale IDs",timeline.validate({scales:[{id:"time"},{id:'time'}]}) is error],
 ["secondary scale ID required",timeline.validate({scales:[{},{}]}) is error],
 ["unknown primary scale",timeline.validate({time_scale:"missing"}) is error],
 ["quantitative primary rejected",timeline.validate({scales:[{kind:"linear"}]}) is error],
 ["unknown axis scale",timeline.validate({axes:[{scale:"missing"}]}) is error],
 ["unknown series scale",timeline.validate({series:[{value_scale:"missing"}]}) is error],
 ["secondary scale accepted",not (timeline.validate({scales:[{}, {id:"values",kind:"log",domain:[1,100]}],axes:[{scale:"values"}]}) is error)],
 ["numeric unit required without data",timeline.validate({scales:[{kind:"numeric"}]}) is error],
 ["calendar domain validated",timeline.validate({scales:[{domain:["bad","2100"]}]}) is error],
 ["increasing domain",timeline.validate({scales:[{domain:["2100","1900"]}]}) is error],
 ["explicit domain drives default window",plan.window.from<timeline.initial_state(spec).window.from and plan.window.to>timeline.initial_state(spec).window.to],
 ["invalid zone without data",timeline.validate({scales:[{timezone:"Nowhere/Invalid"}]}) is error],
 ["alternate calendar diagnosed",timeline.validate({scales:[{calendar:"hebrew"}]}) is error],
 ["fold policy validated",timeline.validate({scales:[{ambiguous_time:"guess"}]}) is error],
 ["window bounds validated eagerly",timeline.validate({window:{from:"2020",to:"2000"}}) is error],
 ["span bounds validated eagerly",timeline.validate({window:{min_span:10,max_span:1}}) is error],
 ["root size contradiction",timeline.validate({min_width:800,max_width:200}) is error],
 ["max width applied",timeline.layout({*:spec,max_width:600},{width:800,height:400}).width==600],
 ["min height applied",timeline.layout({*:spec,min_height:500},{width:800,height:400}).height==500],
 ["card size contradiction",timeline.validate({cards:{min_width:300,max_width:100}}) is error],
 ["unknown controlled field",timeline.validate({value:{unknown:true}}) is error],
 ["controlled ID array required",timeline.validate({value:{selected_ids:"a"}}) is error],
 ["controlled projection types",timeline.validate({default_value:{query:1}}) is error],
 ["canonical window required",timeline.validate({value:{window:{from:"2000",to:"2020"}}}) is error],
 ["explicit null active accepted",not (timeline.validate({value:{active_id:null,selected_ids:[]}}) is error)],
 ["model validates source",timeline.model({view:"invalid"}) is error],
 ["search field names required",timeline.validate({search:{fields:1}}) is error],
 ["theme font size measured",timeline.layout({*:spec,theme:{font_size:24}},{width:800,height:400}).font.font_size==24]
]
