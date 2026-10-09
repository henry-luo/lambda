let land = {type: "Polygon", coordinates: [
    [[-40,-25],[40,-25],[40,25],[-40,25],[-40,-25]],
    [[-10,-10],[10,-10],[10,10],[-10,10],[-10,-10]]
]};
<html
    <head <style "html,body{margin:0;background:#fff}">>
    <body
        <geomap id: "viewport", width: 256, height: 192,
            center: [0,0], zoom: 1, bearing: 0, pitch: 0,
            style: "display:block;position:absolute;left:16px;top:16px;border:4px solid #222;padding:8px",
            <source id: "land", type: "geojson", data: land>
            <source id: "point", type: "geojson", data: {type:"Point",coordinates:[0,0]}>
            <source id: "road", type: "geojson", data: {type:"LineString",coordinates:[[-80,-15],[80,-15]]}>
            <layer id: "paper", type: "background", paint: {'background-color': "#eef3f6"}>
            <layer id: "land", type: "fill", source: "land", paint: {'fill-color': "#00aa00"}>
            <layer id: "road", type: "line", source: "road", paint: {'line-color': "#0000ff",'line-width': 4}>
            <layer id: "point", type: "circle", source: "point", paint: {'circle-color': "#ff0000",'circle-radius': 8}>
        >
        <svg width:32,height:32,style:"position:absolute;left:300px;top:0",
            <rect width:32,height:32,fill:"#00ff00">>
    >
>
