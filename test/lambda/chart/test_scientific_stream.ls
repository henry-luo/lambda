import stream: lambda.chart.stream
let uniform={kind:"grid",x:[0,1,2],y:[0,1],vertex_fields:{u:[[1,1,1],[1,1,1]],v:[[0,0,0],[0,0,0]]}};
let options={seeds:[{id:"s",x:0.5,y:0.5}],max_step:0.1,initial_step:0.05,max_length:4};
let straight=stream.trace(uniform,options);
let rotation={kind:"grid",x:[-1,0,1],y:[-1,0,1],vertex_fields:{u:[[1,1,1],[0,0,0],[-1,-1,-1]],v:[[-1,0,1],[-1,0,1],[-1,0,1]]}};
let circle=stream.trace(rotation,{*:options,direction:"forward",seeds:[{x:0.5,y:0}],closure_tolerance:0.02,max_step:0.03,initial_step:0.02});
let hole=stream.trace({*:uniform,cell_mask:[[false,true]]},{*:options,direction:"forward",max_step:2,initial_step:2,min_step:0.0001});
let limited=stream.trace(uniform,{*:options,max_steps:1,direction:"forward"});
let checks={
    straight:len(straight.paths)==1 and all([for (p in straight.paths[0].points) abs(p.y-0.5)<1e-12]),
    boundary:straight.paths[0].forward=="boundary" and straight.paths[0].backward=="boundary" and
        abs(straight.paths[0].points[0].x)<1e-10 and abs(straight.paths[0].points[len(straight.paths[0].points)-1].x-2)<1e-10,
    length:all([for (i,p in straight.paths[0].points) i==0 or p.length>straight.paths[0].points[i-1].length]),
    circle:circle.paths[0].forward=="closed" and all([for (p in circle.paths[0].points) abs(sqrt(p.x*p.x+p.y*p.y)-0.5)<0.0001]),
    hole:hole.paths[0].forward=="masked" and abs(hole.paths[0].points[len(hole.paths[0].points)-1].x-1)<1e-8,
    budget:limited.paths[0].incomplete and limited.paths[0].forward=="step_limit",
    outside:stream.trace(uniform,{*:options,seeds:[{x:10,y:10}]}).paths[0].forward=="outside",
    stagnation:stream.trace({*:uniform,vertex_fields:{u:[[0,0,0],[0,0,0]],v:[[0,0,0],[0,0,0]]}},options).paths[0].forward=="stagnation",
    invalid:stream.trace(uniform,{*:options,min_step:0}) is error,
    components:stream.trace(uniform,{*:options,components:["u","missing"]}) is error
};
[for (label,passed in checks where passed!=true) string(label)]
