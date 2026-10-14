import scenes: ._scenes
import ui: ._ui
import physics: lambda.physics
import chart: lambda.chart.chart

let scene=scenes.bounce();
let trace=physics.simulate(scene,{steps:720,sample_every:4,bodies:scenes.bounce_ids})^;
let samples=[for (row in physics.chart_data(trace)) {
    time:row.time,height:row.height,material:"e = "++string(scenes.bounce_coefficients[int(slice(row.body,5,len(row.body)))])}];
let figure=chart.figure({width:990,height:210,padding:18,data:samples,mark:{kind:"line"},
    encoding:{x:{field:"time",dtype:"quantitative",title:"Time (s)"},
      y:{field:"height",dtype:"quantitative",title:"Centre height (m)",scale:{domain:[0.0,4.0]}},
      color:{field:"material",dtype:"nominal",scale:{domain:[for (e in scenes.bounce_coefficients) "e = "++string(e)],range:scenes.palette}}}});
ui.page("bounce",scene,{
    title:"Bounce lab",
    description:"Five identical balls fall from the same height. Only restitution changes, from an inelastic landing to an almost elastic bounce. Compare the motion with the measured height curves below.",
    actions:[{id:"drop",label:"Drop again",primary:true,restart:true,play:true,request:{operation:"read"}}],
    labels:["BALLS IN THE AIR","HIGHEST CENTRE","TRANSLATIONAL ENERGY"],
    note:"Restitution e: blue 0.00 · green 0.35 · amber 0.60 · coral 0.80 · violet 0.95. The same scene produces the live motion and the sampled traces.",
    chart:figure,chart_note:"A deterministic six-second batch run, sampled every four ticks. Each curve uses positions returned by the native physics engine."
})
