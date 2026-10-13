// Bogacki-Shampine RK3(2) integrates arc length in the declared field metric.
import mesh: .mesh
import util: .util
import geometry: .geometry

fn option(options,key,fallback) => if (options[key]!=null) options[key] else fallback
fn distance(a,b,metric) => math.hypot((b[0]-a[0])*metric[0],(b[1]-a[1])*metric[1])
fn evaluate(prepared,p,s,direction) {
    let sample=mesh.locate(prepared,p,s.components);
    let vector=if (sample!=null) [sample.values[s.components[0]],sample.values[s.components[1]]] else null;
    let magnitude=if (vector!=null) math.hypot(vector[0]*s.metric_scale[0],vector[1]*s.metric_scale[1]) else null;
    if (sample==null) null else {*:sample,magnitude:magnitude,
        tangent:if (magnitude<=s.stagnation) [0.0,0.0] else [direction*vector[0]/magnitude,direction*vector[1]/magnitude]}
}
fn advance(p,h,terms) => [for (axis in 0 to 1) p[axis]+h*sum([for (term in terms) term[0]*term[1][axis]])]
fn rk(prepared,p,h,s,direction) {
    let first=evaluate(prepared,p,s,direction);
    let k1=first.tangent;
    let k2=evaluate(prepared,advance(p,h,[[0.5,k1]]),s,direction).tangent;
    let k3=if (k2!=null) evaluate(prepared,advance(p,h,[[0.75,k2]]),s,direction).tangent else null;
    let q=if (k3!=null) advance(p,h,[[2.0/9,k1],[1.0/3,k2],[4.0/9,k3]]) else null;
    let terminal=if (q!=null) evaluate(prepared,q,s,direction) else null;
    let low=if (terminal!=null) advance(p,h,[[7.0/24,k1],[0.25,k2],[1.0/3,k3],[0.125,terminal.tangent]]) else null;
    {first:first,terminal:terminal,point:q,error:if (low!=null) distance(q,low,s.metric_scale) else null}
}
fn sample_record(p,e,length) => {x:p[0],y:p[1],magnitude:e.magnitude,length:length,face:e.face,face_id:e.face_id}

fn integrate(prepared,s,direction,points,h,length=0.0,steps=0,occupied=[]) {
    let previous=points[len(points)-1];
    let p=[previous.x,previous.y];
    let first=evaluate(prepared,p,s,direction);
    let step=min(h,s.max_length-length);
    if (steps>=s.max_steps or len(points)>=s.max_vertices) {points:points,reason:"step_limit",steps:steps,incomplete:true}
    else if (length>=s.max_length) {points:points,reason:"max_length",steps:steps,incomplete:false}
    else if (first==null) {points:points,reason:"boundary",steps:steps,incomplete:false}
    else if (first.magnitude<=s.stagnation) {points:points,reason:"stagnation",steps:steps,incomplete:false}
    else {
        let result=rk(prepared,p,step,s,direction);
        let trial=if (result.point!=null) result.point else advance(p,step,[[1.0,first.tangent]]);
        let boundary=mesh.segment_boundary(prepared,p,trial,s.components);
        let tolerance=s.atol+s.rtol*max([distance([0,0],p,s.metric_scale),distance([0,0],trial,s.metric_scale)]);
        let acceptable=result.error!=null and result.error<=tolerance;
        if (not acceptable and step>s.min_step) integrate(prepared,s,direction,points,max(s.min_step,step/2),length,steps+1,occupied)
        else if (boundary!=null) {
            let endpoint=evaluate(prepared,boundary.point,s,direction);
            {points:if (boundary.t==0) points else [*points,sample_record(boundary.point,if (endpoint!=null) endpoint else first,
                length+distance(p,boundary.point,s.metric_scale))],reason:boundary.reason,steps:steps+1,incomplete:false}
        }
        else if (not acceptable) {points:points,reason:"tolerance_unmet",steps:steps+1,incomplete:true}
        else {
            let traveled=distance(p,trial,s.metric_scale);
            let next=sample_record(trial,result.terminal,length+traveled);
            let closed=len(points)>8 and any([for (i,old in points where i<len(points)-4 and length-old.length>4*s.max_step and
                distance(trial,[old.x,old.y],s.metric_scale)<s.closure_tolerance,
                let tangent=evaluate(prepared,[old.x,old.y],s,direction).tangent)
                sum([for (axis in 0 to 1) tangent[axis]*result.terminal.tangent[axis]*s.metric_scale[axis]**2])>0.9]);
            let crowded=any([for (old in occupied) distance(trial,[old.x,old.y],s.metric_scale)<s.spacing]);
            let factor=if (result.error==0) 2.0 else max(0.5,min(2.0,0.9*(tolerance/result.error)**(1.0/3)));
            if (closed or crowded or traveled==0) {points:[*points,next],reason:if (closed) "closed" else if (crowded) "occupied" else "stagnation",steps:steps+1,incomplete:false}
            else integrate(prepared,s,direction,[*points,next],max(s.min_step,min(s.max_step,step*factor)),length+traveled,steps+1,occupied)
        }
    }
}

fn trace_seeds(prepared,seeds,s,index=0,paths=[],used=0,vertices=0) {
    if (index>=len(seeds)) {paths:paths,metadata:{settings:s,seeds:len(seeds),steps:used,vertices:vertices},diagnostics:{incomplete:any(paths |> ~.incomplete)}}
    else if (used>=s.max_total_steps or vertices>=s.max_total_vertices) error("stream.trace: total work limit exceeded")
    else {
        let seed=seeds[index];
        let p=[seed.x,seed.y];
        let e=evaluate(prepared,p,s,1);
        let occupied=if (s.automatic) [for (path in paths) for (point in path.points) point] else [];
        let skipped=e==null or any([for (point in occupied) distance(p,[point.x,point.y],s.metric_scale)<s.spacing]);
        let settings={*:s,max_steps:min(s.max_steps,s.max_total_steps-used),max_vertices:min(s.max_vertices,s.max_total_vertices-vertices)};
        let initial=if (e!=null) [sample_record(p,e,0.0)] else [];
        let forward=if (skipped or s.direction=="backward") null else integrate(prepared,settings,1,initial,s.initial_step,0.0,0,occupied);
        let backward=if (skipped or s.direction=="forward") null else integrate(prepared,
            {*:settings,max_steps:max(0,settings.max_steps-option(forward,"steps",0)),max_vertices:max(1,settings.max_vertices-len(forward.points)+1)},-1,initial,s.initial_step,0.0,0,occupied);
        let back_length=if (backward!=null and len(backward.points)>0) backward.points[len(backward.points)-1].length else 0.0;
        let points=if (skipped) [] else [for (point in reverse(backward.points)) {*:point,length:back_length-point.length},
            for (i,point in forward.points where backward==null or i>0) {*:point,length:back_length+point.length}];
        let path={id:seed.id,seed:seed,points:points,forward:if (skipped) if (e==null) "outside" else "occupied" else forward.reason,
            backward:if (skipped) if (e==null) "outside" else "occupied" else backward.reason,
            incomplete:forward.incomplete==true or backward.incomplete==true};
        trace_seeds(prepared,seeds,s,index+1,[*paths,path],used+option(forward,"steps",0)+option(backward,"steps",0),vertices+len(points))
    }
}

pub fn trace(field,options={}) {
    let prepared=mesh.prepare(field,options);
    let metric=option(options,"metric_scale",[1.0,1.0]);
    let extent=prepared.index.extent;
    let span=if (prepared is error) 1.0 else math.hypot((extent[0][1]-extent[0][0])*metric[0],(extent[1][1]-extent[1][0])*metric[1]);
    let basis=if (span>0) span else 1.0;
    let density=option(options,"density",1.0);
    let s={components:option(options,"components",["u","v"]),direction:option(options,"direction","both"),metric_scale:metric,
        atol:option(options,"atol",basis*1e-7),rtol:option(options,"rtol",1e-5),min_step:option(options,"min_step",basis*1e-7),
        max_step:option(options,"max_step",basis/30),initial_step:option(options,"initial_step",basis/100),
        max_length:option(options,"max_length",basis*4),max_steps:option(options,"max_steps",10000),max_vertices:option(options,"max_vertices",10000),
        max_total_steps:option(options,"max_total_steps",100000),max_total_vertices:option(options,"max_total_vertices",100000),
        max_seeds:option(options,"max_seeds",1024),stagnation:option(options,"stagnation",1e-12),
        closure_tolerance:option(options,"closure_tolerance",basis/100),spacing:basis/(16*density),automatic:options.seeds==null};
    if (prepared is error) prepared
    else if (prepared.dimension!=2) error("stream.trace: requires a planar field")
    else if (not (metric is array) or len(metric)!=2 or not all(metric |> mesh.finite(~) and ~>0)) error("stream.trace: invalid metric_scale")
    else if (not (s.components is array) or len(s.components)!=2 or not all(s.components |> contains(prepared.vertex_fields,~))) error("stream.trace: requires two vertex component fields")
    else if (not contains(["forward","backward","both"],s.direction) or not mesh.finite(density) or density<=0) error("stream.trace: invalid direction or density")
    else if (not all([for (key in ["atol","min_step","max_step","initial_step","max_length","closure_tolerance"]) mesh.finite(s[key]) and s[key]>0]) or
        not all([for (key in ["rtol","stagnation"]) mesh.finite(s[key]) and s[key]>=0]) or s.min_step>s.max_step or s.initial_step<s.min_step or s.initial_step>s.max_step)
        error("stream.trace: invalid tolerance or integration step")
    else if (not all([for (key in ["max_steps","max_vertices","max_total_steps","max_total_vertices","max_seeds"]) mesh.positive_int(s[key])])) error("stream.trace: invalid work limit")
    else if (s.automatic and ceil(8*density)**2>s.max_seeds) error("stream.trace: seed limit exceeded")
    else {
        let cells=int(ceil(8*density));
        let seeds=if (s.automatic) [for (j in 0 to (cells-1)) for (i in 0 to (cells-1))
            {id:[j,i],x:extent[0][0]+(i+0.5)/cells*(extent[0][1]-extent[0][0]),y:extent[1][0]+(j+0.5)/cells*(extent[1][1]-extent[1][0])}]
            else if (options.seeds is array) [for (i,p in options.seeds) {*:p,id:if (p.id!=null) p.id else i}] else null;
        if (seeds==null or len(seeds)>s.max_seeds or not all([for (seed in seeds) mesh.finite(seed.x) and mesh.finite(seed.y)]) or
            len(util.unique_vals(seeds |> ~.id))!=len(seeds)) error("stream.trace: invalid seeds or seed limit")
        else trace_seeds(prepared,seeds,s)
    }
}
