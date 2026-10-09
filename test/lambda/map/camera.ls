import maps: lambda.map
fn near(a,b) => abs(a-b) < 0.000001
let c = {center:[103.858,1.283],width:640,height:360,zoom:12.5,bearing:37};
let p = maps.project(c,[103.86,1.29]);
let q = maps.unproject(c,p);
let anchored = maps.update(c,{type:"zoom",zoom:14,point:[120,70]});
let fixed = maps.project(anchored,maps.unproject(c,[120,70]));
let bounds = maps.fit_bounds({width:640,height:360},[170,-10,-170,10],20);
[
    maps.project({width:256,height:192},[0,0]),
    near(q[0],103.86) and near(q[1],1.29),
    near(fixed[0],120) and near(fixed[1],70),
    near(abs(bounds.center[0]),180),
    maps.project({center:[179,0],zoom:1,width:256,height:192},[-179,0])[0] < 140,
    maps.project({zoom:23},[0,0]) is error,
    maps.project({pitch:30},[0,0]) is error,
    maps.fit_bounds({},[0,20,0,-20]) is error,
    maps.update({}, {type:"pan",dx:16,dy:0}).center[0] == -11.25
]
