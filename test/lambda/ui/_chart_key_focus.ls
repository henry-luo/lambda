import chart:lambda.chart.chart
import dom
let rows=[{id:"a",x:2,y:2},{id:"b",x:8,y:8}]
let encoding={x:{field:"x",dtype:"quantitative",scale:{domain:[0,10]}},y:{field:"y",dtype:"quantitative",scale:{domain:[0,10]}},key:{field:"id"}}
let model=chart.model({data:rows,width:240,height:190,params:[{name:"keep",value:true}],
    transform:[{type:"filter",test:"keep || datum.id != 'a'"}],
    mark:{kind:"point",size:180},encoding:encoding,interaction:{element_select:true}})
view <demo> {
    <html <body apply(~.model)>>
}
on keydown(evt) {
    if (evt.key=="ArrowRight") {
        dom.dispatch(dom.closest(evt.target,".lambda-chart"),{type:"chart_action",detail:{command:"set",param:"keep",value:false}})
        return 'handled'
    }
    'pass'
}
apply(<demo model:model>)
