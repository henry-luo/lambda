// Explicit frames verify geometry, timing, identity and playback without a wall clock (S12.1.1v2).
import chart: lambda.chart.chart
import animation: lambda.chart.animation
import timeline: lambda.chart.timeline
import affine: lambda.chart.svg_transform
import coord: lambda.chart.coordinate
fn check(label, valid) => if (valid) null else label
fn near(a,b) => abs(a-b)<0.0001
fn elements(node) => if (node is element) [node, for (child in content(node)) for (item in elements(child)) item] else []
fn keyed(image,key) => (elements(image) |: ~["data-chart-key"]==format(key,'json'))[0]
fn policy(node,phase) => (parse(node["data-chart-animation"],'json') ^ {~})[phase]
let encoding={x:{field:"x",dtype:"quantitative",scale:{domain:[0,10]},axis_enabled:false},y:{field:"y",dtype:"quantitative",scale:{domain:[0,10]},axis_enabled:false},key:{field:"id"}}
let previous={width:200,height:160,padding:10,data:[{id:"a",x:2,y:2},{id:"b",x:8,y:5}],mark:{kind:"point"},encoding:encoding,
    animate:{enter:{type:"fade",duration:1000},update:{type:"morph",duration:1000},exit:{type:"fade",duration:1000}}}
let target={*:previous,data:[{id:"c",x:3,y:3},{id:"a",x:8,y:8}]}
let old=chart.render_spec(previous)
let next=chart.render_spec(target)
let halfway=chart.render_frame(target,{time_ms:500,previous:previous})
let finished=chart.render_frame(target,{time_ms:1000,previous:previous})
let first=chart.render_frame(target,{time_ms:0,previous:previous})
let timed={*:target,encoding:{*:encoding,enter_duration:{field:"duration"}},data:[{id:"a",x:2,y:2,duration:800},{id:"b",x:3,y:4,duration:1200}],
    animate:{enter:{type:"grow_x",delay:{expr:"wait"},duration:(datum,index,group)=>200+index*100}},params:[{name:"wait",value:50}]}
let timedframe=chart.render_frame(timed,450)
let grouped={*:target,data:[{id:"a",x:1,y:1,region:"b"},{id:"b",x:2,y:2,region:"a"},{id:"c",x:3,y:3,region:"b"}],
    animate:{enter:{type:"fade",duration:100},group:{by:["region"],stagger:20,order:"ascending",mode:"sequence"}}}
let groups=chart.render_frame(grouped,50)
let story={*:target,timeline:{repeat:2,direction:"alternate",keyframes:[{at:0,spec:previous},{at:1000,spec:target}]}}
let plan=timeline.configure(story)
let start=timeline.initial(story)
let playing=timeline.command(start,{command:"play"},100,plan)
let tick=timeline.command(playing,{command:"frame"},350,plan)
let paused=timeline.command(tick,{command:"pause"},400,plan)
let resumed=timeline.command(paused,{command:"resume"},800,plan)
let sought=timeline.command(paused,{command:"seek",position:750},500,plan)
let reversed=timeline.command(resumed,{command:"reverse"},850,plan)
let cancelled=timeline.command(tick,{command:"cancel"},400,plan)
let morph=animation.morph(<path d:"M0 0 L10 0 L10 10 Z">,<path d:"M0 0 L20 0 L20 10 L0 10 Z">,0.5)
let open_path=<path d:"M0 0 L10 0">
let closed_path=<path d:"M0 0 L10 0 L10 10 Z">
let holes=<path d:"M0 0 L20 0 L20 20 L0 20 Z M5 5 L5 10 L10 10 L10 5 Z">
let explicit=<svg width:100,height:100,<path d:"M0 0 L10 0",fill:"none",stroke:"#ff0000">>
let changed=<svg width:200,height:100,<path d:"M0 0 L20 20 L30 30",fill:"none",stroke:"#0000ff">>
let interpolated=animation.sample(explicit,changed,150)
let projected=coord.warp(<g transform:"translate(10,20)",<circle cx:5,cy:10,r:3>>,coord.configure({type:"cartesian"},100,100))
let disabled=chart.render_frame({*:target,animate:false},{time_ms:100,previous:previous});
[check("key survives reorder",near(keyed(halfway,"a").cx,(keyed(old,"a").cx+keyed(next,"a").cx)/2)),
 check("key y interpolation",near(keyed(halfway,"a").cy,(keyed(old,"a").cy+keyed(next,"a").cy)/2)),
 check("target datum metadata",(parse(keyed(halfway,"a")["data-chart-row"],'json') ^ {~}).x==8),
 check("enter remains pickable",keyed(halfway,"c")!=null),check("exit not pickable",keyed(halfway,"b")==null),
 check("exit is decorative",len(elements(halfway) |: ~["pointer-events"]=="none")>0),
 check("completed geometry",near(keyed(finished,"a").cx,keyed(next,"a").cx)),
 check("initial geometry",near(keyed(first,"a").cx,keyed(old,"a").cx)),
 check("field timing",policy(keyed(timedframe,"a"),"enter").duration==800),
 check("parameter timing",policy(keyed(timedframe,"a"),"enter").delay==50),
 check("field timing progress",near(policy(keyed(timedframe,"a"),"enter").progress,0.5)),
 check("group source order",policy(keyed(groups,"a"),"enter").delay==120 and policy(keyed(groups,"c"),"enter").delay==120),
 check("group sort",policy(keyed(groups,"b"),"enter").delay==0),
 check("morph resamples",morph is string and starts_with(morph,"M")),
 check("open_path closed_path fallback",animation.morph(open_path,closed_path,0.5)==null),
 check("open_path closed_path error",animation.morph(open_path,closed_path,0.5,"error") is error),
 check("hole mismatch fallback",animation.morph(closed_path,holes,0.5)==null),
 check("same holes supported",animation.morph(holes,holes,0.5) is string),
 check("path color interpolation",(elements(interpolated) |: name(~)=='path')[0].stroke=="rgba(127.5,0,127.5,1)"),
 check("viewport interpolation",interpolated.width==150 and interpolated.viewBox=="0 0 150 100"),
 check("affine transform order",affine.project(affine.matrix("translate(10 20) scale(2)"),[3,4])==[16,28]),
 check("affine rotation",near(affine.project(affine.matrix("rotate(90 10 10)"),[20,10])[1],20)),
 check("invalid affine",affine.matrix("banana(2)") is error),
 check("affine projection",near((elements(projected) |: name(~)=='circle')[0].cx,15) and near((elements(projected) |: name(~)=='circle')[0].cy,30)),
 check("animation false",near(keyed(disabled,"a").cx,keyed(next,"a").cx)),
 check("pure reproducibility",chart.render_frame(target,{time_ms:500,previous:previous})==halfway),
 check("reduced motion",chart.render_frame(target,{time_ms:10,previous:previous,reduced_motion:true})==chart.render_frame(target,{time_ms:1000,previous:previous,reduced_motion:true})),
 check("timeline alternate",timeline.position(plan,1250).at==750),
 check("timeline terminal",timeline.position(plan,2000).at==0 and timeline.position(plan,2000).complete),
 check("timeline normal repeat",timeline.position({*:plan,direction:"normal"},1250).at==250),
 check("timeline reverse",timeline.position({*:plan,direction:"reverse"},250).at==750),
 check("timeline reverse alternate",timeline.position({*:plan,direction:"reverse_alternate"},1250).at==250),
 check("timeline fill none",timeline.position({*:plan,fill:"none"},2000).at==0),
 check("timeline infinite",not timeline.position({*:plan,repeat:"infinite"},999999).complete),
 check("timeline duration rescale",timeline.position({*:plan,duration:2000},1000).at==500),
 check("static final keyframe",chart.render_spec(story)==chart.render_spec(target)),
 check("timeline frame",near(keyed(chart.render_frame(story,500),"a").cx,keyed(halfway,"a").cx)),
 check("play and tick",tick.elapsed==250 and tick.phase=="playing"),
 check("pause",paused.elapsed==300 and paused.phase=="paused"),
 check("resume",resumed.elapsed==300 and resumed.phase=="playing"),
 check("seek",sought.position==750),check("reverse command",reversed.reversed and reversed.elapsed==350),
 check("cancel",cancelled.phase=="idle" and cancelled.elapsed==0),
 check("end notification",timeline.command(playing,{command:"frame"},2100,plan).notification=="end"),
 check("bad seek",timeline.command(paused,{command:"seek",position:-1},500,plan) is error),
 check("duplicate key",chart.render_spec({*:target,data:[target.data[0],target.data[0]]}) is error),
 check("missing update key",chart.render_spec({*:target,encoding:{x:encoding.x,y:encoding.y}}) is error),
 check("negative duration",chart.render_spec({*:target,animate:{enter:{duration:-1}}}) is error),
 check("bad easing",chart.render_frame({*:target,animate:{enter:{easing:"invalid"}}},100) is error),
 check("invalid easing callback",chart.render_frame({*:target,animate:{enter:{easing:(t)=>2.0}}},100) is error),
 check("negative time",chart.render_frame(target,-1) is error),
 check("invalid timeline times",timeline.configure({timeline:{keyframes:[{at:1,spec:target}]}}) is error),
 check("duplicate timeline times",timeline.configure({timeline:{keyframes:[{at:0,spec:previous},{at:0,spec:target}]}}) is error),
 check("recursive timeline",timeline.configure({timeline:{keyframes:[{at:0,spec:story}]}}) is error)] |: ~!=null
