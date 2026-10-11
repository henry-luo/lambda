import geometry: ~~.mod_geometry
let rect = {left:0.25,top:0.25,right:0.75,bottom:0.75}
geometry.valid(geometry.full())
not geometry.valid({left:0,top:0,right:0,bottom:1})
geometry.bounds(rect,20,12) == {left:5,top:3,right:15,bottom:9}
geometry.bounds({left:0.01,top:0.01,right:0.02,bottom:0.02},20,20) == {left:0,top:0,right:1,bottom:1}
geometry.aspect(200,100,1) == {left:0.25,right:0.75,top:0,bottom:1}
geometry.move(rect,1,-1) == {left:0.5,right:1,top:0,bottom:0.5}
geometry.drag(rect,"e",0.5,0,0,200,100).right == 1
geometry.drag(rect,"nw",-1,-1,0,200,100).left == 0
geometry.valid(geometry.drag(rect,"se",-1,-1,0,200,100))
geometry.fit(200,100,100,100) == 0.5
geometry.inverse(50,70,10,20,2) == {x:20,y:25}
let expanded = geometry.drag(rect,"e",0.1,0,1,100,100);
(expanded.right > rect.right and expanded.top < rect.top and geometry.valid(expanded))
let corner = geometry.drag(rect,"nw",0,-0.1,1,100,100);
(corner.left < rect.left and corner.top < rect.top and geometry.valid(corner))
all([for (handle in ["n","s","e","w","ne","nw","se","sw"])
    geometry.valid(geometry.drag(rect,handle,1,-1,1.5,200,100))])
all([for (handle in ["n","s","e","w","ne","nw","se","sw"])
    (let result = geometry.drag(rect,handle,-0.1,0.1,1.5,200,100),
    abs((result.right-result.left)*200/((result.bottom-result.top)*100)-1.5) < 0.00001)])
