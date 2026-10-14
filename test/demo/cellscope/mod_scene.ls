import s: lambda.scene3d
import model: .mod_model

pub fn viewport(id) element^ => s.scene([
    s.camera({id:"camera",position:model.camera(model.initial()),fov:model.fov(model.initial())}),
    s.light('ambient',{color:"#ffffff",intensity:0.5}),
    s.light('directional',{position:[-4.0,7.0,8.0],color:"#fff2de",intensity:0.75}),
    s.light('directional',{position:[5.0,-3.0,2.0],color:"#d7e9ff",intensity:0.2}),
    s.group([],{id:"imported-specimen"})
],{id:id,camera:"camera",width:720,height:480,viewBox:"0 0 720 480",background:"transparent",
    tabindex:"0",role:"img",'aria-label':"Interactive cell specimen",style:"display:block;width:100%;height:420px;touch-action:none"})
