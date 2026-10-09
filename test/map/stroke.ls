<html
    <head <style "html,body{margin:0;background:#fff}">>
    <body
        <geomap id:"viewport",width:256,height:192,zoom:1,
            <source id:"road",type:"geojson",data:{type:"LineString",coordinates:[[-30,0],[0,0],[0,20]]}>
            <source id:"zero",type:"geojson",data:{type:"LineString",coordinates:[[-30,-20],[30,-20]]}>
            <layer id:"paper",type:"background",paint:{'background-color':"#ffffff"}>
            <layer id:"road",type:"line",source:"road",paint:{'line-color':"#0000ff",'line-width':16}>
            <layer id:"zero",type:"line",source:"zero",paint:{'line-color':"#0000ff",'line-width':0}>
        >
    >
>
