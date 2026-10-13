import chart: lambda.chart.chart
let spec={width:300,height:240,coordinate:{type:"cartesian3d",camera:{position:[0,0,4],target:[0,0,0],fov:60}},
    data:{kind:"mesh",vertices:[{x:-1,y:0,z:-1},{x:1,y:0,z:-1},{x:0,y:0,z:1}],faces:[{id:"triangle",vertices:[0,1,2]}]},
    mark:{kind:"mesh",color:"#e04030"}};
<html <head <style "body{margin:0;background:white}.panel{display:inline-block;vertical-align:top}">>;
    <body <div id:"first",class:"panel",chart.interactive(spec)>;<div id:"second",class:"panel",chart.interactive(spec)>>>
