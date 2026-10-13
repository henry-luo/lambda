let missing=<scene3d <'rigid-body' id:"body",type:'dynamic',<collider id:"shape",shape:'box'>>>;
let scaled=<scene3d <physics><group scale:[2.0,1.0,1.0],
    <'rigid-body' id:"body",type:'dynamic',<collider id:"shape",shape:'box'>>>>;
let ccd=<scene3d <physics><'rigid-body' id:"body",type:'dynamic',ccd:true,<collider id:"shape",shape:'box'>>>;
let valid=<scene3d <physics><'rigid-body' id:"body",type:'dynamic',<collider id:"shape",shape:'box'>>>;
let checks={missing:physics_simulate(missing,{}) is error,
    scale:physics_simulate(scaled,{}) is error,ccd:physics_simulate(ccd,{}) is error,
    ticks:physics_simulate(valid,{steps:-1}) is error,
    selected:physics_simulate(valid,{bodies:["missing"]}) is error,
    command:physics_simulate(valid,{commands:[{tick:0,commands:[{type:"impulse",body:"missing"}]}]}) is error};
[for (key,passed in checks where passed!=true) string(key)]
