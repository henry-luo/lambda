// ./lambda.exe view test/demo/chart/scientific.ls
import chart: lambda.chart.chart
import gallery: .mod_gallery
let axis=[-1.0,-0.5,0.0,0.5,1.0];
let surface={kind:"grid",x:axis,y:axis,vertex_fields:{height:[for (y in axis) [for (x in axis) x*x-y*y]]}};
let field={kind:"grid",x:axis,y:axis,vertex_fields:{u:[for (y in axis) [for (x in axis) 0-y]],v:[for (y in axis) [for (x in axis) x]]}};
let signal={kind:"signal",sample_rate:64,unit:"V",samples:[for (i in 0 to 127) math.sin(2*math.pi*8*i/64)+0.4*math.sin(2*math.pi*18*i/64)]};
let color={field:"height",dtype:"quantitative",scale:{scheme:"viridis"}};
let plot={width:420,height:300,padding:12};
gallery.page("Scientific charts","Meshes, fields, frequencies, and spatial data in Lambda. Drag the saddle surface to explore it in three dimensions.",
        <div class:"gallery",
            gallery.panel("surface","Saddle surface","Drag to orbit · Shift-drag to pan · Scroll to zoom",chart.interactive({*:plot,data:surface,coordinate:{type:"cartesian3d",aspect:"equal"},mark:{kind:"surface"},encoding:{z:{field:"height",dtype:"quantitative"},color:color}}));
            gallery.panel("scalar","Scalar field","Contour bands preserve the supplied mesh topology.",chart.figure({*:plot,data:surface,mark:{kind:"contour_band",levels:[-1,-0.5,0,0.5,1]},encoding:{color:color}}));
            gallery.panel("flow","Steady rotational flow","Paths follow the field in data coordinates.",chart.figure({*:plot,data:field,mark:{kind:"streamline",direction:"forward",max_length:2.5,rtol:0.0001,
                seeds:[{x:0.25,y:0},{x:0.5,y:0},{x:0.75,y:0}],arrows:2},encoding:{color:{field:"magnitude",dtype:"quantitative",scale:{scheme:"viridis"}}}}));
            gallery.panel("spectrum","Frequency spectrum","Two tones at 8 Hz and 18 Hz; amplitudes in volts.",chart.figure({*:plot,data:signal,transform:[{type:"signal",method:"spectrum"}],mark:{kind:"line"},
                encoding:{x:{field:"frequency",dtype:"quantitative",title:"Frequency (Hz)"},y:{field:"amplitude",dtype:"quantitative",title:"Amplitude (V)"}}}));
            gallery.panel("spectrogram","Time-frequency power","Windowed power estimates with explicit cell edges.",chart.figure({*:plot,data:signal,transform:[{type:"signal",method:"spectrogram",segment_length:32,overlap:24,display:{type:"db",reference:1,floor:-60}}],
                mark:{kind:"mesh",association:"cell"},encoding:{color:{field:"display_value",dtype:"quantitative",title:"PSD (dB / Hz)",scale:{scheme:"viridis"}}}}));
            gallery.panel("voxels","Occupied voxels","Only exposed faces enter the depth-buffered scene.",chart.figure({*:plot,coordinate:{type:"cartesian3d",camera:{type:"orthographic"}},
                data:{kind:"voxels",x:[0,1,2],y:[0,1,2],z:[0,1,2],occupied:[[[true,true],[true,false]],[[true,false],[false,false]]]},mark:{kind:"voxel",color:"#327a9e",lighting:"lambert"}}))
        >)
