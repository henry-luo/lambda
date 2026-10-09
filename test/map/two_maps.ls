import maps: lambda.map

fn viewport(id, left) => maps.interactive(<geomap id:id,width:128,height:96,center:[0,0],zoom:1,
    style:"display:block;position:absolute;left:" ++ string(left) ++ "px;top:16px",
    <source id:"points",type:"geojson",data:{type:"Point",coordinates:[0,0]}>
    <layer id:"paper",type:"background",paint:{'background-color':"#eef3f6"}>
    <layer id:"dots",type:"circle",source:"points",paint:{'circle-color':"#f00",'circle-radius':8}>
>);

<html <head <style "html,body{margin:0}">>
    <body viewport("viewport",16) viewport("other",176)>>
