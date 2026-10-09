// Named parts, displayed picking, and scale alignment share the public chart pipeline.
import chart:lambda.chart.chart
import behavior:lambda.chart.behavior
import parameter:lambda.chart.parameter
import scale:lambda.chart.scale
import picking:lambda.chart.picking
import animation:lambda.chart.animation
import density:lambda.chart.field
import timeline:lambda.chart.timeline
import projection:lambda.chart.projection
import interaction:lambda.chart.interaction
import coordinate:lambda.chart.coordinate
import paths:lambda.chart.path_geometry
import geometry:lambda.chart.geometry
import colors:lambda.slide.color
fn check(label,valid)=>if (valid) null else label
fn elements(node)=>if (node is element) [node,for (child in content(node)) for (value in elements(child)) value] else []
let graph={nodes:[{id:"a",category:"A"},{id:"b",category:"B"}],links:[{id:"ab",source:"a",target:"b",weight:10}]}
let spec={data:graph,width:300,height:200,mark:{kind:"force_graph",iterations:0,labels:false,parts:{
    node:{encoding:{color:{field:"category",dtype:"nominal",scale:{range:["#112233","#445566"]}}},interaction:{element_select:true}},
    link:{encoding:{stroke:{field:"weight",dtype:"quantitative",scale:{domain:[0,10],range:["#ffffff","#000000"]}}},interaction:{element_select:false}}}},
    encoding:{key:{field:"id"}}}
let compiled=behavior.prepare(spec)
let definitions=parameter.definitions(compiled)
let image=chart.render_spec(spec,null,parameter.initial(definitions))
let nodes=elements(image) |: ~["data-chart-part"]=="node"
let links=elements(image) |: ~["data-chart-part"]=="link"
let geometry=picking.collect(<g transform:"translate(20,30)",<circle cx:5,cy:6,r:3,fill:"red",'data-chart-row':"{\"id\":\"a\"}">;
    <path d:"M50 0 L50 100",stroke:"black",fill:"none",'data-chart-row':"{\"id\":\"b\"}">>)
let source=<svg width:100,height:100,<g 'data-chart-frame':"{\"view\":\"plot\"}",
    <circle cx:10,cy:10,r:4,fill:"red",'data-chart-row':"{\"id\":\"a\"}",'data-chart-key':"\"a\"">>>
let target=<svg width:100,height:100,<g 'data-chart-frame':"{\"view\":\"plot\"}",
    <circle cx:90,cy:90,r:4,fill:"red",'data-chart-row':"{\"id\":\"a\",\"new\":true}",'data-chart-key':"\"a\"">>>
let sampled=animation.sample(source,target,150)
let frame=parse((elements(sampled) |: ~["data-chart-frame"]!=null)[0]["data-chart-frame"],'json') ^ {~}
let ctx={plot_w:100.0,plot_h:100.0,encoding:{x:{field:"x"},y:{field:"y"}},x_scale:scale.linear_scale(0,10,0,100),y_scale:scale.linear_scale(0,10,100,0)}
let cells=elements(density.render_density([{x:2,y:2},{x:4,y:4}],ctx,{extent:[[2,4],[2,4]],resolution:2,bandwidth:1})) |: ~.class=="density-cell"
let plan={duration:2000.0,length:1000.0,repeat:2,direction:"alternate",fill:"forwards"}
let sought=timeline.command({phase:"paused",elapsed:2500.0,reversed:false,anchor:null},{command:"seek",position:750},3000,plan)
let resumed=timeline.command(sought,{command:"resume"},3000,plan)
let tick=timeline.command(resumed,{command:"frame"},3250,plan);
let geo=behavior.prepare({data:[{id:"p",lon:0,lat:0}],mark:{kind:"geoshape"},coordinate:{type:"geo",projection:{type:"equirectangular"}},
    encoding:{longitude:{field:"lon"},latitude:{field:"lat"},key:{field:"id"}},interaction:{pan_zoom:true}})
let gd=parameter.definitions(geo)
let gs=parameter.initial(gd)
let gp=projection.configure(200,100,{type:"equirectangular"})
let gf={view:"map",params:gd,behaviors:geo._behaviors,projection:gp,width:200,height:100,encoding:{}}
let down=interaction.update(gs,{type:"pointerdown",x:100,y:50,frame:gf})
let dragged=interaction.update(down,{type:"pointermove",x:110,y:55,frame:gf})
let camera=behavior.value(dragged,geo._behaviors[0],"map")
let zoomed=interaction.update(gs,{type:"wheel",deltaY:-100,x:100,y:50,frame:gf})
let isolated=behavior.prepare({data:[{id:"a",x:1,y:1}],mark:{kind:"point"},encoding:{key:{field:"id"}},interaction:{element_select:true}})
let sd=parameter.definitions(isolated)
let selected=parameter.update_value(parameter.initial(sd),sd,sd[0].name,[{id:"a"}],"cell-one")
let factory=behavior.prepare({data:[{id:"a",x:1,y:2}],mark:{kind:"composite",parts:{dots:{fill:"#123456"}},
    expand:(data,options)=>{layer:[{part:"dots",mark:{kind:"point"},interaction:{element_select:true}}]}},
    encoding:{x:{field:"x",dtype:"quantitative"},y:{field:"y",dtype:"quantitative"},key:{field:"id"}}})
let source_paint=<svg width:100,height:100,<defs <linearGradient id:"old",x1:0,x2:1,<stop offset:0,'stop-color':"#ff0000">;<stop offset:1,'stop-color':"#000000">>>;
    <rect x:0,y:0,width:100,height:100,fill:"url(#old)">>
let target_paint=<svg width:100,height:100,<defs <linearGradient id:"new",x1:1,x2:0,<stop offset:0,'stop-color':"#0000ff">;<stop offset:1,'stop-color':"#ffffff">>>;
    <rect x:0,y:0,width:100,height:100,fill:"url(#new)">>
let painted=animation.sample(source_paint,target_paint,150)
let gradient=(elements(painted) |: name(~)=='linearGradient' and starts_with(~.id,"chart-transition-"))[0]
let rotated=animation.morph(<path d:"M0 0 L20 0 L20 20 L0 20 Z">,<path d:"M20 20 L0 20 L0 0 L20 0 Z">,0.5)
let partial=coordinate.configure({type:"cartesian",transform:[{type:"rotate",angle:0.4,dimensions:["x"]}]},100,80)
let inverse=coordinate.invert(partial,coordinate.project(partial,[0.2,0.7]));
[check("part node records",len(nodes)==2 and nodes[0].fill=="#112233" and nodes[1].fill=="#445566"),
 check("part link scale",len(links)==1 and links[0].stroke=="#000000"),
 check("part key identity",nodes[0]["data-focus-key"]!=links[0]["data-focus-key"]),
 check("part generated scope",len(definitions)==1 and contains(definitions[0].name,"pnode")),
 check("part override false",not any([for (item in compiled._parts.link._behaviors) item.type=="element_select"])),
 check("part timing diagnostic",chart.render_spec({*:spec,mark:{*:spec.mark,parts:{node:{animate:{enter:{duration:-1}}}}}}) is error),
 check("displayed translated glyph",picking.nearest(geometry,[25,36]).row.id=="a"),
 check("displayed path",picking.nearest(geometry,[70,80]).row.id=="b"),
 check("current animation geometry",picking.nearest(frame.geometry,[50,50]).row.new==true and picking.nearest(frame.geometry,[50,50]).distance==0),
 check("density axis domain",cells[0].x==20 and cells[0].y==70 and cells[0].width==10 and cells[0].height==10),
 check("degenerate density",not (density.density([{x:1,y:1}]) is error)),
 check("empty density",len(density.density([],{resolution:2}).cells)==4),
 check("seek rescaled alternate",sought.elapsed==2500 and timeline.position(plan,tick.elapsed).at==625),
 check("geographic navigation grammar",len(gd)==1 and parameter.selection(gd[0])==null and geo._behaviors[0]._geo),
 check("geographic pan parameters",camera.center[0]<0 and camera.center[1]>0 and dragged.gesture!=null),
 check("geographic zoom parameters",behavior.value(zoomed,geo._behaviors[0],"map").scale>gp.scale),
 check("geographic rendering",not (chart.render_spec(geo,null,zoomed) is error)),
 check("generated view scoping",parameter.selected(behavior.value(selected,isolated._behaviors[0],"cell-one"),{id:"a"},false) and
    not parameter.selected(behavior.value(selected,isolated._behaviors[0],"cell-two"),{id:"a"},false)),
 check("factory behavior compilation",len(parameter.definitions(factory))==1 and factory.layer[0].mark.fill=="#123456"),
 check("factory renders parts",not (chart.render_spec(factory,null,parameter.initial(parameter.definitions(factory))) is error)),
 check("gradient interpolation",gradient.x1==0.5 and content(gradient)[0]["stop-color"]=="rgba(127.5,0,127.5,1)"),
 check("gradient endpoint",animation.sample(source_paint,target_paint,300)==target_paint),
 check("rgba interruption colors",(colors.parse("rgba(127.5,0,127.5,1)","test") ^ {~})==[0.5,0.0,0.5,1.0]),
 check("cyclic morph alignment",abs(geometry.area((paths.sample(rotated))[0].points)-400)<0.000001),
 check("partial transform inverse",abs(inverse[0]-0.2)<0.000001 and abs(inverse[1]-0.7)<0.000001)] |: ~!=null
