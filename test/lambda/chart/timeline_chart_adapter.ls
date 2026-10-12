import chart: lambda.chart.chart
import timeline: lambda.chart.timeline
import adapter: lambda.chart.timeline.chart_adapter

let data=[{key:"a",at:"2000",heading:"First"},{key:"b",at:"2020",heading:"Last"}]
let encoding={id:{field:"key"},at:{field:"at"},title:{field:"heading"}}
let spec={id:"chart-history",width:640,height:360,padding:20,data:data,mark:{kind:"timeline"},encoding:encoding,
    timeline_options:{view:"axis",cards:{visibility:"all"}}}
let direct=chart.render_spec(spec)
let authored=chart.render(<chart id:"chart-history",width:640,height:360,timeline_options:spec.timeline_options,
    <data values:data> <mark type:"timeline"> <encoding <id field:"key"> <at field:"at"> <title field:"heading">>>)
let canonical=timeline.validate(adapter.specification(spec))
let concat=chart.render_spec({concat:"hconcat",data:data,children:[spec,{*:spec,id:"second-history"}]});
[
 ["chart history SVG",name(direct)=='svg' and direct.width==640],
 ["element grammar",authored==direct],
 ["canonical identity",canonical.events[0].id=="a" and canonical.events[1].id=="b"],
 ["separate animation vocabulary",adapter.specification(spec).slideshow==null],
 ["x channel chronology alias",timeline.validate(adapter.specification({*:spec,encoding:{id:encoding.id,x:encoding.at,title:encoding.title}})).events[0].start==canonical.events[0].start],
 ["x2 duration alias",timeline.validate(adapter.specification({*:spec,data:[{key:"a",at:"2000",end:"2020"}],encoding:{id:encoding.id,x:encoding.at,x2:{field:"end"}}})).events[0].kind=="range"],
 ["concat composes presenter",concat is element],
 ["source data immutable",data[0].heading=="First"],
 ["invalid options diagnosed",chart.render_spec({*:spec,timeline_options:{view:"invalid"}}) is error]
]
