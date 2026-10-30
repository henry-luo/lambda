import {agent_attach,agent_target,agent_snapshot,agent_avoidance,agent_cancel,agent_release} from '../../lmd/package/navigation/scene.ls';
import {step} from '../../lmd/package/scene3d/runtime.ls';
const world=document.getElementById('world');
globalThis.agentEvents=[];
for(const type of ['navigationpathchanged','navigationstatechanged','navigationlinkrequired','navigationarrived'])
    world.addEventListener(type,e=>agentEvents.push({type:e.type,detail:e.detail}));
globalThis.agentRequest=async operation=>{
    globalThis.agentState=null;
    if(operation==='attach')agentState=await agent_attach(world,'a0','walkable',{body:'walker0',skin:0.01});
    if(operation==='target')agentState=await agent_target(world,'a0',[6.5,0,6.5]);
    if(operation==='read')agentState=await agent_snapshot(world,'a0');
    if(operation==='configure')agentState=await agent_avoidance(world,'a0',{priority:0.75,time_horizon:1.5});
    if(operation==='step')agentState=await step(world,60);
    if(operation==='cancel')agentState=await agent_cancel(world,'a0');
    if(operation==='release')agentState=await agent_release(world,'a0');
};
