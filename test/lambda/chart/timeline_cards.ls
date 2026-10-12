import t: lambda.chart.timeline
import radiant
fn height(plan, id) {
    let item=(plan.events |: ~.id==id)[0];
    item.bounds.bottom-item.bounds.top
}
let raw={view:"axis",width:700,height:500,cards:{width:180},toolbar:false,
    events:[{id:"short",at:"1900",title:"Short",content:"One line"},
        {id:"long",at:"1950",title:"Long",content:<div <p "A paragraph with enough words to wrap across several lines inside the card."> <p "A second paragraph adds measured height.">>}]}
let measured=t.layout(raw,{width:700,height:500})
let fixed=t.layout({*:raw,cards:{width:180,height:75}},{width:700,height:500})
let bounded=t.layout({*:raw,cards:{width:250,max_width:160,max_height:90}},{width:700,height:500})
let compact={*:raw,cards:{width:180,read_more:true}}
let collapsed=t.layout(compact,{width:700,height:500})
let expanded=t.layout(compact,{width:700,height:500},{expanded_events:["long"]})
let large=t.layout({*:raw,theme:{font_size:28}},{width:700,height:500})
let recipe=t.layout({*:raw,parts:{card_body:{render:(item,fallback)=><div style:"height:190px;",item.content>}}},{width:700,height:500});
let viewport=radiant.measure_html("<html><body style='margin:0'><div style='width:50vw;height:25vh'></div></body></html>",600,400);
[
 ["rich content controls height",height(measured,"long")>height(measured,"short")+30],
 ["authored dimensions",all([for (item in fixed.events) item.bounds.right-item.bounds.left==180 and item.bounds.bottom-item.bounds.top==75])],
 ["size constraints",all([for (item in bounded.events) item.bounds.right-item.bounds.left==160 and item.bounds.bottom-item.bounds.top<=90])],
 ["read more measured",height(expanded,"long")>height(collapsed,"long")],
 ["font changes measured",height(large,"short")>height(measured,"short")],
 ["custom body measured",height(recipe,"short")>190],
 ["date slot present",contains(format(t.render(raw,{width:700,height:500}),'html'),"lambda-timeline-card-date")],
 ["empty cards measurable",len(t.layout({view:"axis",events:[{id:"empty",at:"1900"}]},{width:700,height:500}).events)==1],
 ["measurement uses supplied viewport",viewport[0].width==300 and viewport[0].height==100]
]
