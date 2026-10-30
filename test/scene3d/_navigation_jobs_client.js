import {bake_start,bake_poll,bake_publish,bake_cancel,bake_release,bake_export,path,snapshot} from '../../lmd/package/navigation/scene.ls';
import {pause} from '../../lmd/package/scene3d/runtime.ls';
globalThis.jobRequest=async (operation,token=null,options={},asset='routes')=>{
  const world=document.getElementById('world');
  if(operation==='pause')globalThis.jobClock=await pause(world);
  if(operation==='start')globalThis.jobState=await bake_start(world,asset,options);
  if(operation==='poll')globalThis.jobState=await bake_poll(world,token);
  if(operation==='publish')globalThis.jobState=await bake_publish(world,token);
  if(operation==='cancel')globalThis.jobState=await bake_cancel(world,token);
  if(operation==='release')globalThis.jobState=await bake_release(world,token);
  if(operation==='export')globalThis.savedBake=await bake_export(world,token);
  if(operation==='path'){const endpoints=globalThis.jobEndpoints||{start:[1,0,1],end:[3,0,3]};globalThis.jobPath=await path(world,'routes',endpoints.start,endpoints.end,endpoints.options||{});}
  if(operation==='read')globalThis.jobCache=await snapshot(world);
};
