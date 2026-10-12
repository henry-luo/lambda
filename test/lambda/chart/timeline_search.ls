import search: lambda.chart.timeline.search
import timeline: lambda.chart.timeline
import layout: lambda.chart.timeline.layout
import radiant

type Caption { label: string?, kind: "history" | "archive", string* }
let caption=<Caption label:null,kind:"history", "original">
let replaced=radiant.with_content(caption,["new",null," text"])
let rich=<p id:"authored",title:"École", "An E\u0301" <em class:"emphasis", "cole"> " opens.">
let rich_result=search.highlight(rich,"ecole",{highlight:true,diacritics:"ignore"})
let qualified=<timeline.content label:"retained", "École">
let qualified_result=search.highlight(qualified,"ecole",{highlight:true,diacritics:"ignore"})

let options={diacritics:"ignore",highlight:true}
let spec={id:"search",search:{*:options,mode:"filter",fields:["title","content"]},
    groups:[{id:"g",title:"Group"}],events:[
        {id:"a",at:"2000",title:"École",group:"g",tags:["education"]},
        {id:"b",at:"2001",title:"Straße",content:<em "A school">},
        {id:"c",at:"2002",title:"界🙂",tags:["culture"]}]}
let normalized=timeline.validate(spec)
let first=timeline.initial_state(spec)
let query=timeline.update(first,{type:"search",query:"ecole"},{spec:spec}).state
let found=layout.visible(normalized,query)
let highlighted=search.highlight("École and école","ecole",options)
let filtered=layout.visible(normalized,{filters:{from:"2001",tags:["culture"]}});
[
 ["accent sensitive default",len(search.spans("École","ecole"))==0],
 ["Unicode simple folding",len(search.spans("ẞtraße","ß"))==2],
 ["case sensitive policy",len(search.spans("École","école",{case_sensitive:true}))==0],
 ["precomposed accents",search.spans("École","ecole",options)==[{from:0,to:5}]],
 ["decomposed accents",search.spans("E\u0301cole","ecole",options)==[{from:0,to:6}]],
 ["trailing combining mark retained",search.spans("e\u0301x","e",options)==[{from:0,to:2}]],
 ["code point offsets",search.spans("界🙂École","ecole",options)==[{from:2,to:7}]],
 ["canonical normalization expansion",search.spans("각","각",options)==[{from:0,to:1}]],
 ["literal query",len(search.spans("a.b a?b","a.b"))==1],
 ["empty normalized query",len(search.spans("abc","\u0301",options))==0],
 ["highlight preserves authored text",name(highlighted[1])=='mark' and content(highlighted[1])[0]=="École" and content(highlighted[3])[0]=="école"],
 ["source remains immutable",normalized.events[0].title=="École"],
 ["filter mode",(found.events |> ~.id)==["a"]],
 ["match IDs",found.matches==["a"]],
 ["rich fields searched",search.matches(normalized.events[1],"school",normalized.search)],
 ["date and tag filters",(filtered.events |> ~.id)==["c"]],
 ["invalid filter date diagnosed",layout.visible(normalized,{filters:{from:"not-a-date"}}) is error],
 ["filter bounds diagnosed",layout.visible(normalized,{filters:{from:"2002",to:"2000"}}) is error],
 ["no cross field match",not search.matches({title:"one",content:"two"},"one two",{fields:["title","content"]})],
 ["predicate applies with empty query",not search.matches({id:"a"},"",{predicate:(item,query)=>false})],
 ["rich tag retained",name(rich_result)=='p'],
 ["rich attributes retained",rich_result.id=="authored" and rich_result.title=="École"],
 ["cross inline first fragment",content(rich_result)[1]==<mark class:"lambda-timeline-match", "E\u0301">],
 ["cross inline child retained",name(content(rich_result)[2])=='em' and content(rich_result)[2].class=="emphasis"],
 ["cross inline second fragment",content(content(rich_result)[2])[0]==<mark class:"lambda-timeline-match", "cole">],
 ["rich source remains immutable",content(rich)[0]=="An E\u0301" and content(content(rich)[1])[0]=="cole"],
 ["rich HTML keeps inline structure",contains(format(rich_result,'html'),"<p id=\"authored\" title=\"École\">An <mark class=\"lambda-timeline-match\">E\u0301</mark><em class=\"emphasis\"><mark class=\"lambda-timeline-match\">cole</mark></em> opens.</p>")],
 ["qualified name retained",name(qualified_result)==name(qualified)],
 ["qualified attributes retained",qualified_result.label=="retained"],
 ["nominal identity retained",replaced is Caption],
 ["full attribute contracts retained",replaced.label==null and replaced.kind=="history"],
 ["replacement content normalizes",content(replaced)==["new text"]],
 ["nominal source remains immutable",content(caption)==["original"]],
 ["declared content enforced",radiant.with_content(caption,[<mark "new">]) is error],
 ["unmatched rich value retained",search.highlight(rich,"absent",{highlight:true})==rich],
 ["disabled rich highlighting",search.highlight(rich,"ecole",{})==rich]
]
