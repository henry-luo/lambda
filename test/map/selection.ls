import maps: lambda.map
import dom
pn selected(detail) {
    let output=dom.query_selector(dom.root_node(detail.node),"#selection");
    dom.set_attribute(output,"data-selected",format(detail.features |> ~.feature_id,'json'))
    let frame=maps.snapshot(detail.node);
    let hits=maps.query_rendered(frame,detail.point);
    dom.set_attribute(output,"data-snapshot",format(hits |> ~.feature_id,'json'))
    dom.set_attribute(output,"data-parity",string(hits==detail.features))
}
pn camera_changed(detail) {
    let output=dom.query_selector(dom.root_node(detail.node),"#selection");
    dom.set_attribute(output,"data-zoom",string(detail.camera.zoom))
}
let image=maps.interactive(<geomap id:"viewport",width:256,height:192,zoom:1,
    style:"display:block",
    <source id:"points",type:"geojson",data:{type:"Feature",id:42,properties:{name:"Center marker"},geometry:{type:"Point",coordinates:[0,0]}}>
    <layer id:"paper",type:"background",paint:{'background-color':"#eef3f6"}>
    <layer id:"dots",type:"circle",source:"points",paint:{'circle-color':"#f00",'circle-radius':8}>
>,{controls:true,feature_list:true,on_select:selected,on_camera:camera_changed,bounds:[-40,-20,40,20],padding:8});
<html <head <style "html,body{margin:0}">> <body image; <output id:"selection">>>
