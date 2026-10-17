import draw: lambda.editor.draw
import model: lambda.editor.draw.model
import geom: lambda.editor.draw.geometry
let a = draw.vertex("a","rect",{x:80,y:100,width:120,height:80})^
let b = draw.vertex("b","rect",{x:480,y:100,width:120,height:80})^
let source = {kind:'boundary',vertex:"a",side:'east'}
let target = {kind:'boundary',vertex:"b",side:'west'}
let e = draw.edge("e",source,target)^
let parallel = draw.edge("p",source,target)^
let st = draw.exec(draw.open_session(draw.new_document({width:900,height:700}))^,{kind:'insert',objects:[a,b,e,parallel]})^;
draw.route(st.doc,e).points != draw.route(st.doc,parallel).points
draw.route(st.doc,e) == draw.route(st.doc,e)
let way = draw.exec(st,{kind:'set',id:"e",properties:{waypoints:[{x:300,y:280},{x:400,y:280}]}})^
let edge = draw.lookup(way.doc,"e").shape;
contains(draw.route(way.doc,edge).points,{x:300,y:280}) and contains(draw.route(way.doc,edge).points,{x:400,y:280})
let curve = draw.edge("c",source,target,{segments:[[200,140,280,320,400,320,480,140]],'end-arrow':"diamond"})^
let curved = draw.exec(st,{kind:'insert',objects:[curve]})^
let r = draw.route(curved.doc,curve);
contains(r.path," C ") and len(r.points)>32
r.bounds.height>120
draw.hit(curved.doc,r.points[32],2.0) == "c"
let moved = draw.exec(curved,{kind:'move',ids:["b"],dx:20,dy:40})^
let m = draw.route(moved.doc,draw.lookup(moved.doc,"c").shape);
m.points[last] == draw.resolve_endpoint(moved.doc,draw.lookup(moved.doc,"c").shape,"to")
m.handles.to == m.points[last]
draw.decode(parse(format(draw.encode(moved.doc)^,'json'),'json')^)^ == moved.doc
let loop = draw.edge("loop",source,{kind:'boundary',vertex:"a",side:'north'})^;
draw.exec(st,{kind:'insert',objects:[loop]}) ^ { true } ~ { false }
let loops = draw.exec(draw.open_session(draw.new_document({'allow-loops':true}))^,{kind:'insert',objects:[a,loop]})^
let lr = draw.route(loops.doc,loop);
len(lr.points)>=5 and lr.bounds.width>40 and lr.bounds.height>40
len(draw.incident_edges(loops.doc,"a")) == 1 and draw.neighbors(loops.doc,"a") == ["a"]
let loop2 = draw.edge("loop2",source,{kind:'boundary',vertex:"a",side:'north'})^
let more = draw.exec(loops,{kind:'insert',objects:[loop2]})^;
draw.route(more.doc,loop).points != draw.route(more.doc,loop2).points
let copied = draw.exec(curved,{kind:'duplicate',ids:["a","b"]})^;
len(draw.vertices(copied.doc))==4 and len(draw.edges(copied.doc))==6
len(draw.validate(copied.doc)) == 0
let malformed = draw.edge("bad",source,target,{segments:[[0,0,1]]})^;
draw.exec(st,{kind:'insert',objects:[malformed]}) ^ { true } ~ { false }

let reversed = draw.exec(curved,{kind:'reverse',id:"c"})^;
draw.route(reversed.doc,draw.lookup(reversed.doc,"c").shape).points[0] == r.points[last]
draw.route(reversed.doc,draw.lookup(reversed.doc,"c").shape).points[last] == r.points[0]
let parallel_points = draw.route(st.doc,e).points;
all([for (i in 1 to len(parallel_points)-1) parallel_points[i].x == parallel_points[i-1].x or parallel_points[i].y == parallel_points[i-1].y])
