<html <head <style "html,body{margin:0}">> <body
    <geomap id:"viewport",width:256,height:192,zoom:1,
        <source id:"places",type:"geojson",data:{type:"FeatureCollection",features:[
            {type:"Feature",id:"west",properties:{kind:"capital"},geometry:{type:"Point",coordinates:[-22.5,0]}},
            {type:"Feature",id:"east",properties:{kind:"city",color:"#00f"},geometry:{type:"Point",coordinates:[22.5,0]}},
            {type:"Feature",id:"hidden",properties:{kind:"village"},geometry:{type:"Point",coordinates:[0,0]}}
        ]}>
        <layer id:"paper",type:"background",paint:{'background-color':"#eef3f6"}>
        <layer id:"cities",type:"circle",source:"places",filter:["!=",["get","kind"],"village"],paint:{
            'circle-color':["match",["get","kind"],"capital","#f00",["coalesce",["get","color"],"#0a0"]],
            'circle-radius':["interpolate",["linear"],["zoom"],0,4,2,12],
            'circle-opacity':["case",["==",["id"],"west"],1,0.5]
        }>
    >
>>
