// Native events exercise the chart template's own state (S12.1.3).
import chart: lambda.chart.chart
import vega: lambda.chart.vega

let data = [{group:"A", x:2, y:2}, {group:"B", x:5, y:5}, {group:"C", x:8, y:8}]
let spec = vega.convert({data:{values:data},params:[{name:"pick",select:{type:"point",fields:["group"]}},
    {name:"brush",select:{type:"interval",encodings:["x"]}},
    {name:"threshold",value:0,bind:{input:"range",min:0,max:10,step:1}}],
    hconcat:[
        {width:200,height:160,mark:{type:"point",size:200},encoding:{
            x:{field:"x",type:"quantitative",scale:{domain:[0,10]},axis:null},
            y:{field:"y",type:"quantitative",scale:{domain:[0,10]},axis:null},
            color:{condition:{param:"pick",empty:false,value:"red"},value:"grey"}}},
        {width:200,height:160,transform:[{filter:{param:"brush"}},{filter:"datum.x >= threshold"}],mark:{type:"point",size:200},encoding:{
            x:{field:"x",type:"quantitative",scale:{domain:[0,10]},axis:null},
            y:{field:"y",type:"quantitative",scale:{domain:[0,10]},axis:null}}}
    ]});
<html <head <style "body{margin:0} .lambda-chart{display:inline-block;outline:none} .chart-controls{padding:8px} .chart-control{display:inline-block}">>
<body chart.interactive(spec)>>
