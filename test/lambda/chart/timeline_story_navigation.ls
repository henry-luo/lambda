import t: lambda.chart.timeline
import story: lambda.chart.timeline.story
import resource: lambda.chart.timeline.resource
let raw={id:"narrative",view:"story",spacing:"sequence",title:"Our history",description:"An introduction",
    story:{title_slide:true,order:["c","a","b"]},slideshow:{duration:100},
    events:[{id:"a",title:"First"},{id:"b",title:"Second"},{id:"c",title:"Third"}]}
let spec=t.validate(raw)
let initial=t.initial_state(raw)
let next=t.update(initial,{type:"next"},{spec:raw,now:0})
let moved_last=t.update(next.state,{type:"last"},{spec:raw,now:0})
let previous=t.update(moved_last.state,{type:"previous"},{spec:raw,now:0})
let moved_first=t.update(moved_last.state,{type:"first"},{spec:raw,now:0})
let rich={*:raw,story:{title_slide:{id:"intro",title:<em "Welcome">,content:<p "Background">,
    media:[{kind:"image",src:"cover.png",alt:"Cover"}]},order:["c","a","b"],panel_order:["media","text","children"]}}
let richspec=t.validate(rich)
let richhtml=format(t.render(rich,{width:800,height:300}),"html")
let imported=t.convert({title:{unique_id:"opening",text:{headline:"Imported title",text:"Imported introduction"},
    media:{url:"cover.jpg",caption:"The cover"}},events:[{unique_id:"launch",start_date:{year:2020},text:{headline:"Launch"}}]},"timelinejs")
let imported_spec=t.validate(imported)
let played=t.update(initial,{type:"play"},{spec:raw,now:0})
let advanced=t.update(played.state,{type:"tick"},{spec:raw,now:100});
[
 ["initial introduction",initial.active_id=="__title__" and spec.intro.title=="Our history"],
 ["shared sequence",(story.items(spec) |> ~.id) == ["__title__","c","a","b"]],
 ["next follows narrative order",next.state.active_id=="c"],
 ["last follows narrative order",moved_last.state.active_id=="b"],
 ["previous follows narrative order",previous.state.active_id=="a"],
 ["first returns introduction",moved_first.state.active_id=="__title__"],
 ["start at end",t.initial_state({*:raw,story:{*:raw.story,start_at_end:true}}).active_id=="b"],
 ["explicit initial event",t.initial_state({*:raw,story:{*:raw.story,active_id:"a"}}).active_id=="a"],
 ["title slide outside chronology",len(spec.events)==3 and spec.intro.start==null],
 ["title slide not selectable or editable",spec.intro.selectable==false and spec.intro.editable==false],
 ["playback includes introduction",story.plan(spec).length==400 and advanced.state.active_id=="c"],
 ["title-only narrative",story.plan(t.validate({view:"story",spacing:"sequence",story:{title_slide:{id:"intro",title:"Only title"}}})).slides[0].id=="intro"],
 ["rich introduction",richspec.intro.id=="intro" and contains(richhtml,"<em>Welcome</em>") and contains(richhtml,"Background")],
 ["panel ordering",index_of(richhtml,"src=\"cover.png\"")<index_of(richhtml,"Background")],
 ["introduction media request",resource.descriptors(richspec,{active_id:"intro"})[0].event_id=="intro"],
 ["TimelineJS title translation",imported_spec.intro.id=="opening" and imported_spec.intro.title=="Imported title" and imported_spec.intro.media[0].caption=="The cover"],
 ["TimelineJS title initializes story",t.initial_state(imported).active_id=="opening"],
 ["explicit title ID avoids collision",t.validate({*:raw,events:[{id:"__title__"}]} ) is error and
    not (t.validate({*:raw,story:{title_slide:{id:"intro"}},events:[{id:"__title__"}]} ) is error)],
 ["invalid title contract",t.validate({*:raw,story:{title_slide:"Invalid"}}) is error],
 ["unknown active event",t.validate({*:raw,story:{active_id:"removed"}}) is error],
 ["order rejects duplicate or unknown IDs",t.validate({*:raw,story:{order:["a","a"]}}) is error and t.validate({*:raw,story:{order:["missing"]}}) is error],
 ["invalid panel ordering",t.validate({*:raw,story:{panel_order:["media","media"]}}) is error],
 ["source export excludes introduction",len(t.export_data(raw,"json").events)==3],
 ["cards use ordinary chronology",t.initial_state({*:raw,view:"cards",story:{title_slide:true}}).active_id=="a"],
 ["disabled introduction",t.initial_state({*:raw,story:{title_slide:false}}).active_id=="a"]
]
