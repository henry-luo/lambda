// parametric apex development is independent from geometric interpretation (PG13.3).
import c: ._common
import lsys: .lsystem
import turtle: .turtle3d
import rng: .rng
import colors: lambda.color

pub fn normalize(parameters={}) map^ {
    let checked=c.require(parameters is map and c.fields(parameters,["iterations","length","radius","decay","angle","phyllotaxis","bark","leaf_color","leaf_length","leaf_width","force"]),"unknown apex parameter")^
    let p={iterations:5,length:0.8,radius:0.075,decay:0.76,angle:38.0,phyllotaxis:137.507764,
        bark:"#648356",leaf_color:"#8da864",leaf_length:0.48,leaf_width:0.22,*:parameters}
    let dimensions=c.require(c.bounded(p.iterations,0,7,true) and c.bounded(p.length,0.1,4.0) and c.bounded(p.radius,0.01,0.5) and
        c.bounded(p.decay,0.5,0.9) and c.bounded(p.angle,5,80) and c.bounded(p.phyllotaxis,-360,360) and
        c.bounded(p.leaf_length,0.01,2) and c.bounded(p.leaf_width,0.01,2),"invalid apex parameters")^
    let bark=colors.parse(p.bark)^,leaf=colors.parse(p.leaf_color)^;
    {*:p,force:turtle.force(c.value(p.force,{}))^}
}
pub fn skeleton(k:u64,parameters={}) map^ {
    let p=normalize(parameters)^
    let productions={A:(m,ctx)=> {
        let turn=rng.between(ctx.key,0.85,1.15)*p.angle;
        [["F",m[1],m[2]],"[",["&",turn],["/",p.phyllotaxis],
            ["A",m[1]*p.decay,m[2]*0.68],"]",["/",p.phyllotaxis],["L",p.leaf_length,p.leaf_width],
            ["A",m[1]*p.decay,m[2]*0.68]]
    }}
    let sequence=lsys.derive([["A",p.length,p.radius]],productions,p.iterations,{key:k,max_symbols:10000})^
    let sk=turtle.skeleton(sequence,{radius:p.radius,bark:p.bark,leaf_color:p.leaf_color,force:p.force})^;
    {*:sk,parameters:p,sequence:sequence}
}
