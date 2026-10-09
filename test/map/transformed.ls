import maps: lambda.map
let image=maps.interactive(<geomap id:"viewport",width:256,height:192,zoom:1,
    style:"display:block;position:absolute;left:256px;top:16px;transform:rotate(90deg);transform-origin:0 0",
    <source id:"points",type:"geojson",data:{type:"Point",coordinates:[0,0]}>
    <layer id:"paper",type:"background",paint:{'background-color':"#eef3f6"}>
    <layer id:"dots",type:"circle",source:"points",paint:{'circle-color':"#f00",'circle-radius':8}>
>);
<html <head <style "html,body{margin:0}">> <body image>>
