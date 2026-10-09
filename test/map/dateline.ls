<html <head <style "html,body{margin:0}">>
    <body <geomap id:"viewport",width:256,height:192,center:[179,0],zoom:2,
        <source id:"land",type:"geojson",data:{type:"Polygon",coordinates:[
            [[170,-10],[-170,-10],[-170,10],[170,10],[170,-10]],
            [[175,-5],[-175,-5],[-175,5],[175,5],[175,-5]]] }>
        <source id:"point",type:"geojson",data:{type:"Point",coordinates:[-179,0]}>
        <layer id:"paper",type:"background",paint:{'background-color':"#eef3f6"}>
        <layer id:"land",type:"fill",source:"land",paint:{'fill-color':"#00aa00"}>
        <layer id:"dot",type:"circle",source:"point",paint:{'circle-color':"#ff0000",'circle-radius':5}>
    >>
>
