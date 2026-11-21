const cases=[
 {name:'closed dialog uses the UA hidden default',visible:false},
 {name:'author block display overrides the closed default',display:'block',visible:true},
 {name:'author inline dialog exposes its fixed child',display:'inline',child:true,visible:true},
 {name:'offscreen inline dialog preserves viewport fixed paint',display:'inline',child:true,spacer:true,visible:true},
 {name:'offscreen ordinary inline preserves viewport fixed paint',tag:'span',display:'inline',child:true,spacer:true,visible:true},
 {name:'offscreen block preserves viewport fixed paint',tag:'div',display:'block',child:true,spacer:true,visible:true},
 {name:'offscreen inline preserves transformed fixed paint',tag:'span',display:'inline',child:true,spacer:true,childStyle:'transform:translateZ(0);z-index:99999',visible:true},
 {name:'mask none leaves inline fixed descendants ungrouped',display:'inline',child:true,spacer:true,ownerStyle:'mask-image:none',visible:true},
 {name:'clearing a radial mask releases the stacking context',display:'inline',child:true,spacer:true,ownerStyle:'mask-image:radial-gradient(black,transparent)',clearMask:true,visible:true},
 {name:'unset filter leaves inline fixed descendants ungrouped',display:'inline',child:true,spacer:true,ownerStyle:'filter:unset',visible:true},
 {name:'unset backdrop leaves inline fixed descendants ungrouped',display:'inline',child:true,spacer:true,ownerStyle:'backdrop-filter:unset',visible:true},
 {name:'initial clip path does not clip fixed descendants',display:'inline',child:true,spacer:true,ownerStyle:'clip-path:initial',visible:true},
 {name:'unset clip path does not clip fixed descendants',display:'inline',child:true,spacer:true,ownerStyle:'clip-path:unset',visible:true},
 {name:'inherited none clip path does not clip fixed descendants',display:'inline',child:true,spacer:true,ownerStyle:'clip-path:inherit',visible:true},
 {name:'author flex display overrides the closed default',display:'flex',visible:true},
 {name:'author display none hides an open dialog',display:'none',open:true,visible:false},
 {name:'explicit initial display uses the CSS initial role',display:'initial',child:true,visible:true},
 {name:'explicit unset display uses the CSS initial role',display:'unset',child:true,visible:true},
 {name:'author all reset overrides the closed default',reset:'all:unset',child:true,visible:true},
 {name:'late author display invalidates earlier hidden layout',late:true,visible:true},
 {name:'closing preserves an explicit author display',display:'block',open:true,close:true,visible:true},
];
function fixtureHtml(f){
 const style=(f.ownerStyle?f.ownerStyle+';':'')+(f.reset?f.reset+';':'')+'margin:0;border:0;padding:0;'+
   (f.display?'display:'+f.display+';':'')+
   (f.child?'':'position:fixed;left:0;top:0;width:30px;height:30px;background:red;');
 return '<!doctype html><style>html,body{margin:0;background:lime}#probe{'+style+'}'+
  '#child{position:fixed;left:0;top:0;width:30px;height:30px;background:red;'+(f.childStyle||'')+'}</style>'+
  (f.spacer?'<div style="height:2000px"></div>':'')+'<'+(f.tag||'dialog')+' id=probe'+(f.open?' open':'')+'>'+(f.child?'<div id=child></div>':'')+'</'+(f.tag||'dialog')+'>'+ 
  (f.late||f.close||f.clearMask?'<script>const p=document.getElementById("probe");p.getBoundingClientRect();'+
   (f.clearMask?'p.style.maskImage="none";':f.late?'p.style.display="block";':'p.close();')+'</script>':'');
}
module.exports={cases,fixtureHtml};
