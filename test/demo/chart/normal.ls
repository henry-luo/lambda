// ./lambda.exe view test/demo/chart/normal.ls
import chart: lambda.chart.chart
import vega: lambda.chart.vega
import gallery: .mod_gallery
import catalog: .mod_catalog

fn has_keyframes(entry) => entry.previous != null or entry.spec.timeline != null
let motion = catalog.charts |: has_keyframes(~);
fn animation(entry) {
    let source = catalog.spec(entry);
    let frames = if (source.timeline != null) source.timeline.keyframes else
        [{at:0,spec:{data:entry.previous.data}},{at:1000,spec:{data:source.data}}];
    // Fixed domains make the keyed movement visible across both data snapshots.
    {*:source,encoding:{*:source.encoding,y:{*:source.encoding.y,scale:{domain:[0,7]}}},
        timeline:{autoplay:true,duration:2400,keyframes:frames}}
}
let scatter = (catalog.charts |: ~.label == "Scatter / Bubble Chart")[0];
let rows = scatter.data.values;
let selection = {name:"pick",select:{type:"point",fields:["channel"]}};
let axes = {*:scatter.encoding,size:{*:scatter.encoding.size,legend:null}};
let linked = vega.convert({data:{values:rows},params:[{name:"brush",select:{type:"interval",encodings:["x"]}},
    {name:"minimum",value:0,bind:{input:"range",name:"Minimum conversions",min:0,max:350,step:10}}],
    hconcat:[{width:440,height:260,title:"Brush here",mark:{type:"point",size:100},encoding:axes},
        {width:440,height:260,title:"Filtered points",transform:[{filter:{param:"brush"}},{filter:"datum.conversions >= minimum"}],
            mark:{type:"point",size:100},encoding:axes}]});
let scatter_spec = catalog.spec(scatter);
let zoom = {*:scatter_spec,width:440,height:280,title:"Pan and zoom",
    encoding:{*:scatter_spec.encoding,size:{*:scatter_spec.encoding.size,legend_enabled:false}},interaction:{pan_zoom:true,tooltip:true}};
let selected_chart = vega.convert({*:scatter,width:440,height:280,title:"Pick a channel",params:[{*:selection,bind:"legend"}],
    encoding:{*:axes,color:{condition:{param:"pick",empty:false,value:"#317e78"},field:"channel",type:"nominal"},
        opacity:{condition:{param:"pick",value:1},value:0.2}}});
let examples = catalog.charts |: not catalog.graph(~) and not has_keyframes(~);

gallery.page("Normal charts","Play a transition, pick a mark, or brush a range. This gallery uses the same chart specifications as the Lambda chart dashboard.",
    <div
        <h2 class:"section-heading","Charts in motion">
        <div class:"gallery",for (i,entry in motion) gallery.animated("animation-" ++ string(i),entry.label,
            if (entry.previous != null) "Keyed points move between two data snapshots. Playback starts automatically."
            else "Bars morph into points. Replay, pause, seek, or reverse the transition.",animation(entry))>
        <h2 class:"section-heading","Explore the data">
        <div class:"gallery",
            gallery.panel("selection","Point & legend selection","Click a point or legend item; Shift-click to add channels. Double-click the plot to clear.",chart.interactive(selected_chart));
            gallery.panel("zoom","Pan & zoom","Drag the plot to pan, scroll over it to zoom, and double-click to reset.",chart.interactive(zoom));
            gallery.panel("linked","Linked brushing & input controls","Drag a horizontal range in the left plot to filter the right. Adjust the slider for a second filter. Double-click to clear the brush.",chart.interactive(linked),true)
        >
        <h2 class:"section-heading",string(len(examples)) ++ " dashboard examples">
        <p class:"caption","Hover or focus marks to inspect them. Click to select; Shift-click to add marks; double-click to clear. Each chart keeps its own selection.">
        <div class:"gallery dashboard-gallery",for (i,entry in examples)
            gallery.panel("example-" ++ string(i),entry.label,"",chart.interactive(catalog.live(catalog.spec(entry))))>
    >)
