// Generated controls preserve typed values and reactive focus (S12.1.3).
import chart: lambda.chart.chart
import vega: lambda.chart.vega
let spec=vega.convert({width:240,height:100,data:{values:[{x:2},{x:8}]},mark:"point",
    params:[{name:"enabled",value:true,bind:{input:"checkbox"}},
        {name:"amount",value:2,bind:{input:"select",options:[2,8]}},
        {name:"mode",value:false,bind:{input:"radio",options:[false,true]}},
        {name:"label",value:"start",bind:{input:"text"}}],
    transform:[{filter:{param:"enabled"}}],encoding:{x:{field:"x",type:"quantitative",axis:null},
        y:{value:30},color:{value:"grey"}}})
view <report> state values:null {
    <div apply(~.chart);
        <output id:"amount",string(values.amount)>
        <output id:"mode",string(values.mode)>
        <output id:"label",values.label>>
}
on chart_change(evt) {values=evt}
let model=chart.model(spec);
<html <head <style "body{margin:0}.lambda-chart{display:inline-block}.chart-control{display:block;padding:5px}">>
<body apply(<report chart:model>)>>
