// D7.2.5: authoring choices share the effect vocabulary used by the compiler.
pub let catalog = [
    {id:'appear',label:"Appear",family:'entrance'},
    {id:'blinds-in',label:"Blinds",family:'entrance'},
    {id:'checkerboard-in',label:"Checkerboard",family:'entrance'},
    {id:'dissolve-in',label:"Dissolve In",family:'entrance'},
    {id:'fly-in',label:"Fly In",family:'entrance'},
    {id:'fade-in',label:"Fade In",family:'entrance'},
    {id:'zoom-in',label:"Zoom In",family:'entrance'},
    {id:'wipe-in',label:"Wipe In",family:'entrance'},
    {id:'highlight',label:"Fill Color",family:'emphasis'},
    {id:'grow',label:"Grow / Shrink",family:'emphasis'},
    {id:'line-color',label:"Line Color",family:'emphasis'},
    {id:'spin',label:"Spin",family:'emphasis'},
    {id:'transparency',label:"Transparency",family:'emphasis'},
    {id:'pulse',label:"Pulse",family:'emphasis'},
    {id:'disappear',label:"Disappear",family:'exit'},
    {id:'fade-out',label:"Fade Out",family:'exit'},
    {id:'fly-out',label:"Fly Out",family:'exit'},
    {id:'zoom-out',label:"Zoom Out",family:'exit'},
    {id:'wipe-out',label:"Wipe Out",family:'exit'},
    {id:'motion',label:"Motion Path",family:'motion'}]
pub fn descriptor(kind) => [for (entry in catalog where entry.id == kind) entry][0]
pub fn defaults(kind) => {kind:kind,duration:if (kind in ['appear','disappear']) 0.0 else 800.0,delay:0.0,easing:'ease-in-out',repeat:1,
    *:if (kind in ['fly-in','fly-out']) {from:'bottom',distance:120.0}
    else if (kind in ['wipe-in','wipe-out','blinds-in','checkerboard-in']) {from:'left'}
    else if (kind in ['zoom-in','zoom-out']) {scale:0.3}
    else if (kind == 'grow') {scale:1.5}
    else if (kind == 'pulse') {scale:1.15}
    else if (kind == 'spin') {angle:360.0}
    else if (kind in ['highlight','line-color']) {color:"#f5b942"}
    else if (kind == 'transparency') {opacity:0.5}
    else if (kind == 'motion') {path:[[0.0,0.0,160.0,0.0]],orient:false} else {}}

pub let paths = [
    {id:"right",label:"Line Right",path:[[0.0,0.0,160.0,0.0]]},
    {id:"left",label:"Line Left",path:[[0.0,0.0,-160.0,0.0]]},
    {id:"up",label:"Line Up",path:[[0.0,0.0,0.0,-160.0]]},
    {id:"down",label:"Line Down",path:[[0.0,0.0,0.0,160.0]]},
    {id:"arc",label:"Arc",path:[[0.0,0.0,0.0,-160.0,160.0,-160.0,160.0,0.0]]},
    {id:"loop",label:"Loop",path:[[0.0,0.0,240.0,-240.0,240.0,240.0,0.0,0.0]]}]
pub fn path_preset(id) => [for (entry in paths where entry.id == id) entry][0]
pub fn path_name(path) => [for (entry in paths where entry.path == path) entry.id][0]
