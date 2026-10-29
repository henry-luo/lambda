import {path,snapshot,current} from '../../lmd/package/navigation/scene.ls';
globalThis.navRequest=async (operation,generation=null)=>{
  const world=document.getElementById('world');
  globalThis.navState=null;
  if(operation==='read')globalThis.navState=await snapshot(world);
  if(operation==='path')globalThis.navState=await path(world,'routes',[0,0],[2,0],{},generation);
  if(operation==='road')globalThis.navState=await path(world,'roads','a','b');
  if(operation==='current')globalThis.navState=await current(world,'routes',generation);
  if(operation==='surface')globalThis.navState=await path(world,'floor',[0.25,0.25],[1.75,0.75]);
  if(operation==='ramp')globalThis.navState=await path(world,'ramp',[0.25,0,0.5],[2.75,1,0.5]);
  if(operation==='bake')globalThis.navState=await path(world,'clearance',[1.25,0,1.25],[2.75,0,2.75],{},generation);
};
