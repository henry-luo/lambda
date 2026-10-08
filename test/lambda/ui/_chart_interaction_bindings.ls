// Each apply owns its selection stores and camera (S9.1.4, D6.2.3v2).
import chart: lambda.chart.chart
import vega: lambda.chart.vega
let rows=[{group:"A",x:2,y:2},{group:"B",x:8,y:8}]
let legend=vega.convert({width:240,height:180,data:{values:rows},
    params:[{name:"pick",select:{type:"point",fields:["group"]},bind:"legend"}],
    mark:{type:"point",size:200},encoding:{
        x:{field:"x",type:"quantitative",scale:{domain:[0,10]},axis:null},
        y:{field:"y",type:"quantitative",scale:{domain:[0,10]},axis:null},
        color:{condition:{param:"pick",empty:false,value:"red"},field:"group",type:"nominal"}}})
let camera=vega.convert({width:200,height:160,data:{values:rows},
    params:[{name:"camera",select:{type:"interval",encodings:["x"]},bind:"scales"}],
    mark:{type:"point",size:200},encoding:{
        x:{field:"x",type:"quantitative",scale:{domain:[0,10]},axis:null},
        y:{field:"y",type:"quantitative",scale:{domain:[0,10]},axis:null}}})
view <camera_report> state projection: null {
    <div apply(~.chart);
        <output id:"camera-domain", if (projection.camera.x == null) "empty" else string(projection.camera.x[0])>>
}
on chart_change(evt) { projection = evt }
let camera_model = chart.model(camera)
let words=<chart width:240,height:160,params:[{name:"pick",select:{type:"point",fields:["text"]}}],
    <data values:[{text:"Lambda",weight:3},{text:"Charts",weight:2}]>
    <mark type:"wordcloud",seed:17,font_size:24>
    <encoding <color condition:{param:"pick",empty:false,value:"red"},value:"grey">>>;
<html <head <style "body{margin:0}.lambda-chart{display:inline-block;outline:none}.fixture{position:absolute}#one{left:0;top:0}#two{left:260px;top:0}#camera{left:0;top:210px}#words{left:260px;top:210px}">>
<body <div id:"one",class:"fixture",chart.interactive(legend)>
    <div id:"two",class:"fixture",chart.interactive(legend)>
    <div id:"camera",class:"fixture",apply(<camera_report chart:camera_model>)>
    <div id:"words",class:"fixture",chart.interactive(words)>>>
