<html <head <style "html,body{margin:0}">> <body
    <geomap id:"viewport",width:256,height:192,
        <source id:"point",type:"geojson",data:{type:"Point",coordinates:[0,0]}>
        <layer id:"paper",type:"background",paint:{'background-color':"#0f0"}>
        <layer id:"invalid",type:"circle",source:"point",paint:{
            'circle-radius':["case",true,8,["number","bad"]]
        }>
    >
>>
