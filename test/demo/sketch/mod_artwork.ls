// the starter is editable vector data, using the same objects as user drawings.
import m: .mod_model

fn ellipse(id,x,y,w,h,fill) => m.make_object(id,"ellipse",{x:x,y:y,width:w,height:h},{fill:fill,stroke:"none"})
fn polygon(id,pts,fill) => m.path_object(id,pts,{fill:fill,stroke:"none",closed:true})
fn caption(id,x,y,text,size,color) => m.make_object(id,"text",{x:x,y:y,width:len(text)*size*.64,height:size*1.25},
    {text:text,font_size:size,fill:color,stroke:"none"})
fn hill(id,base,amplitude,phase,color) => polygon(id,[
    *[for (i in 0 to 48) {x:80.0+i*800.0/48.0,y:base+amplitude*math.sin(i/48.0*math.pi*1.5+phase)}],
    {x:880.0,y:654.0},{x:80.0,y:654.0}],color)

pub fn starter() => {*:m.blank(),title:"Somewhere, slowly",seq:30,layers:[
    {id:"l1",name:"Paper & frame",visible:true,locked:true,opacity:1.0,objects:[
        m.make_object("o2","rect",{x:80.0,y:218.0,width:800.0,height:436.0},{fill:"#f1d4af",stroke:"none"}),
        caption("o3",80.0,53.0,"FIELD NOTES / 001",15.0,"#70716a"),
        caption("o4",80.0,685.0,"TAKE THE LONG WAY HOME.",17.0,"#284f59"),
        caption("o5",80.0,725.0,"A small study in slowing down",13.0,"#85857d")
    ]},
    {id:"l6",name:"Sun & rolling hills",visible:true,locked:false,opacity:1.0,objects:[
        ellipse("o7",545.0,264.0,212.0,212.0,"#dd734b"),
        hill("o8",480.0,65.0,1.6,"#a6b3a0"),
        hill("o9",540.0,65.0,3.5,"#588878"),
        hill("o10",600.0,50.0,1.0,"#284f59"),
        m.path_object("o11",[{x:480.0,y:581.0},{x:460.0,y:596.0},{x:465.0,y:609.0},{x:502.0,y:620.0},
            {x:540.0,y:637.0},{x:540.0,y:654.0}],{stroke:"#edb96b",size:9.0})
    ]},
    {id:"l12",name:"Ink & lettering",visible:true,locked:false,opacity:1.0,objects:[
        caption("o13",77.0,99.0,"Somewhere, slowly.",56.0,"#284f59"),
        m.path_object("o14",[{x:336.0,y:324.0},{x:350.0,y:318.0},{x:364.0,y:324.0},{x:378.0,y:317.0},{x:391.0,y:321.0}],{stroke:"#284f59",size:2.5}),
        m.path_object("o15",[{x:402.0,y:293.0},{x:412.0,y:288.0},{x:423.0,y:293.0},{x:434.0,y:287.0},{x:444.0,y:291.0}],{stroke:"#284f59",size:2.0}),
        caption("o16",781.0,725.0,"SK / 26",13.0,"#85857d")
    ]}
]}
