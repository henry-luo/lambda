import timeline: lambda.chart.timeline

type TextOnly { id: string, kind: "history" | "archive", string* }
fn query_button(id, query, title) => <button id:id,type:"button",
    'data-timeline-action':format({type:"search",query:query},'json'),title>
let spec={id:"content-ui",width:640,search:{fields:["title","content","tags"],highlight:true,diacritics:"ignore"},
    toolbar:{slots:{after:[query_button("numbers","42true","Numbers"),query_button("joined-tags","one two","Joined tags"),
        query_button("tag","two","One tag"),query_button("callback","content-ui:en-US","Callback"),query_button("clear","","Clear")]}},
    events:[{id:"typed",title:"School",content:<TextOnly id:"typed-content",kind:"history", "École opens">},
        {id:"mixed",title:"Scalars",content:<p id:"mixed-content",*["A",42,true,<em "Z">]>},
        {id:"tags",title:"Labels",tags:["one","two"]},
        {id:"callback",title:(item,context)=>context.chart_id ++ ":" ++ context.locale}]}
let model=timeline.model(spec);
<html <head <style "body{font-family:Arial;margin:16px}textonly{display:block}">> <body apply(model)>>
