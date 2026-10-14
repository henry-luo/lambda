// ./lambda.exe view test/demo/chart/scientific.ls
import chart: lambda.chart.chart
let axis=[-1.0,-0.5,0.0,0.5,1.0];
let surface={kind:"grid",x:axis,y:axis,vertex_fields:{height:[for (y in axis) [for (x in axis) x*x-y*y]]}};
let field={kind:"grid",x:axis,y:axis,vertex_fields:{u:[for (y in axis) [for (x in axis) 0-y]],v:[for (y in axis) [for (x in axis) x]]}};
let signal={kind:"signal",sample_rate:64,unit:"V",samples:[for (i in 0 to 127) math.sin(2*math.pi*8*i/64)+0.4*math.sin(2*math.pi*18*i/64)]};
let color={field:"height",dtype:"quantitative",scale:{scheme:"viridis"}};
let plot={width:420,height:300,padding:12};
fn panel(title,caption,image) => <section <h2 title>;<p caption>;image>;
<html
    <head <meta charset:"utf-8">;<title "Lambda scientific plotting">;
        <style "body{margin:0;padding:32px;background:#edf1f4;color:#172b3a;font-family:Arial,sans-serif}h1{margin:0 0 8px}header p{color:#536778;margin:0 0 28px}.gallery{display:grid;grid-template-columns:repeat(2,450px);gap:24px}section{background:white;border-radius:12px;padding:16px;border:1px solid #d6e0e6}h2{margin:0 0 8px;font-size:19px}section p{font-size:13px;color:#536778;margin:0 0 12px}">>
    <body
        <header <h1 "Scientific plotting">;<p "Meshes, fields, frequencies, and spatial data in Lambda.">>
        <div class:"gallery",
            panel("Saddle surface","Drag to orbit · Shift-drag to pan · Scroll to zoom",chart.interactive({*:plot,data:surface,coordinate:{type:"cartesian3d",aspect:"equal"},mark:{kind:"surface"},encoding:{z:{field:"height",dtype:"quantitative"},color:color}}));
            panel("Scalar field","Contour bands preserve the supplied mesh topology.",chart.figure({*:plot,data:surface,mark:{kind:"contour_band",levels:[-1,-0.5,0,0.5,1]},encoding:{color:color}}));
            panel("Steady rotational flow","Paths follow the field in data coordinates.",chart.figure({*:plot,data:field,mark:{kind:"streamline",direction:"forward",max_length:2.5,rtol:0.0001,
                seeds:[{x:0.25,y:0},{x:0.5,y:0},{x:0.75,y:0}],arrows:2},encoding:{color:{field:"magnitude",dtype:"quantitative",scale:{scheme:"viridis"}}}}));
            panel("Frequency spectrum","Two tones at 8 Hz and 18 Hz; amplitudes in volts.",chart.figure({*:plot,data:signal,transform:[{type:"signal",method:"spectrum"}],mark:{kind:"line"},
                encoding:{x:{field:"frequency",dtype:"quantitative",title:"Frequency (Hz)"},y:{field:"amplitude",dtype:"quantitative",title:"Amplitude (V)"}}}));
            panel("Time-frequency power","Windowed power estimates with explicit cell edges.",chart.figure({*:plot,data:signal,transform:[{type:"signal",method:"spectrogram",segment_length:32,overlap:24,display:{type:"db",reference:1,floor:-60}}],
                mark:{kind:"mesh",association:"cell"},encoding:{color:{field:"display_value",dtype:"quantitative",title:"PSD (dB / Hz)",scale:{scheme:"viridis"}}}}));
            panel("Occupied voxels","Only exposed faces enter the depth-buffered scene.",chart.figure({*:plot,coordinate:{type:"cartesian3d",camera:{type:"orthographic"}},
                data:{kind:"voxels",x:[0,1,2],y:[0,1,2],z:[0,1,2],occupied:[[[true,true],[true,false]],[[true,false],[false,false]]]},mark:{kind:"voxel",color:"#327a9e",lighting:"lambert"}}))
        >
    >
>
