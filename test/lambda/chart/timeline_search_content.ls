import search: lambda.chart.timeline.search
import timeline: lambda.chart.timeline
import layout: lambda.chart.timeline.layout
import radiant

type TextOnly { id: string, kind: "history" | "archive", string* }
let typed=<TextOnly id:"typed",kind:"history", "École opens">
let marked=search.highlight(typed,"ecole",{highlight:true,diacritics:"ignore"})
let mixed=["A",42,true,<em "Z">]
let mixed_result=search.highlight(mixed,"42true",{highlight:true})
let hidden=<p "ab" <script "secret"> <style "secret"> <template "secret"> "cd">
let hidden_result=search.highlight(hidden,"bc",{highlight:true})
let labels={tags:["one","two"]}
let callback=(item,context)=>context.chart_id ++ ":" ++ context.locale
fn failed_callback(item,context) error => error("search failure")
let spec=timeline.validate({id:"callback",search:{fields:["title"]},events:[{id:"event",title:callback}]})
view <search_badge> { <em "Visible component"> }
[
 ["rich arrays have no invented spaces",search.spans(["one",<em "two">],"onetwo")==[{from:0,to:6}]],
 ["rich arrays reject invented spaces",search.spans(["one",<em "two">],"one two")==[]],
 ["rendered scalar text shares offsets",search.spans(mixed,"42true")==[{from:1,to:7}]],
 ["numeric fragment highlighted",contains(format(mixed_result,'html'),">42</mark>")],
 ["boolean fragment highlighted",contains(format(mixed_result,'html'),">true</mark>")],
 ["mixed source unchanged",mixed==["A",42,true,<em "Z">]],
 ["typed presentation succeeds",marked is element],
 ["typed presentation has same tag",name(marked)==name(typed)],
 ["typed presentation retains full attributes",marked.id=="typed" and marked.kind=="history"],
 ["presentation is a new structural value",not (marked is TextOnly) and not (marked is object)],
 ["typed presentation contains a mark",content(marked)[0]==<mark class:"lambda-timeline-match", "École">],
 ["typed source keeps its contract",typed is TextOnly and content(typed)==["École opens"]],
 ["typed replacement still enforced",radiant.with_content(typed,[<mark "École">]) is error],
 ["nonvisual content is not searched",not search.matches({content:hidden},"secret",{fields:["content"]})],
 ["nonvisual nodes retain source content",content(hidden_result)[2]==<script "secret">],
 ["visible offsets skip nonvisual nodes",contains(format(hidden_result,'html'),">b</mark><script>secret</script><style>secret</style><template>secret</template><mark class=" )],
 ["tags do not join with spaces",not search.matches(labels,"one two",{fields:["tags"]})],
 ["tags do not concatenate",not search.matches(labels,"onetwo",{fields:["tags"]})],
 ["individual tag matches",search.matches(labels,"two",{fields:["tags"]})],
 ["card callback context is searched",len(layout.visible(spec,{query:"callback:en-US"}).matches)==1],
 ["presented components are searched",search.matches({content:<search_badge>},"Visible component",{fields:["content"]})],
 ["callback error remains a value error",search.matches({title:failed_callback},"x",{fields:["title"]}) is error],
 ["predicate error remains a value error",search.matches({},"",{predicate:failed_callback}) is error],
 ["predicate result is checked",search.matches({},"",{predicate:(item,query)=>42}) is error],
 ["filter mode propagates callback error",layout.visible({*:spec,search:{fields:["title"],mode:"filter"},events:[{*:spec.events[0],title:failed_callback}]},{query:"x"}) is error]
]
