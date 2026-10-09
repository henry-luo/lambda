<html <head <style "html,body{margin:0}">>
    <body <geomap id:"viewport",width:256,height:192,center:[0,0],zoom:1,
        <source id:"a",type:"geojson",data:{type:"Point",coordinates:[0,0]}>
        <source id:"b",type:"geojson",data:{type:"Point",coordinates:[20,0]}>
        <layer id:"paper",type:"background",paint:{'background-color':"#fff"}>
        <layer id:"green",type:"circle",source:"a",paint:{'circle-color':"#00aa00",'circle-radius':25}>
        <layer id:"red",type:"circle",source:"a",paint:{'circle-color':"#ff0000",'circle-radius':16,'circle-opacity':0.5}>
        <layer id:"blue",type:"circle",source:"b",paint:{'circle-color':"#0000ff",'circle-radius':10}>
    >>
>
