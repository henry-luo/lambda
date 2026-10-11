// numeric and inherited paint-server attributes use the same path as parsed SVG strings.
view numeric_gradient_probe: <numeric_gradient_probe> {
    <svg width:"240",height:"160",viewBox:"0 0 240 160",
        <defs
            <linearGradient id:"numeric",x1:1,y1:0,x2:0,y2:0,
                <stop offset:0,["stop-color"]:"red"> <stop offset:0.5,["stop-color"]:"green"> <stop offset:1,["stop-color"]:"blue">>
            <linearGradient id:"inherited",href:"#numeric">
            <radialGradient id:"radial",cx:0.25,cy:0.5,r:0.25,fx:0.25,fy:0.5,
                <stop offset:0,["stop-color"]:"red"> <stop offset:1,["stop-color"]:"blue">>>
        <rect x:10,y:10,width:100,height:40,fill:"url(#numeric)">
        <rect x:10,y:60,width:100,height:40,fill:"url(#inherited)">
        <rect x:130,y:10,width:100,height:100,fill:"url(#radial)">>
}
<html <head <style "body{margin:0;background:white}">> <body apply(<numeric_gradient_probe>)>>
