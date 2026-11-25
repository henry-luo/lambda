const {scriptResultFixture}=require('./script_result_fixture.cjs');
const cases=[
 ['service-worker container inherits real EventTarget listener behavior',`
 const target=navigator.serviceWorker;let calls=0,otherCalls=0;
 const listener=function(event){calls++;check(this===target&&event.target===target&&event.currentTarget===target&&event.data.kind==='probe')};
 target.addEventListener('message',listener);const other=new EventTarget();other.addEventListener('message',()=>otherCalls++);
 check(target instanceof EventTarget&&!Object.hasOwn(target,'addEventListener'));
 if(typeof gc==='function')gc();target.dispatchEvent(new MessageEvent('message',{data:{kind:'probe'}}));
 target.removeEventListener('message',listener);target.dispatchEvent(new MessageEvent('message',{data:{kind:'removed'}}));
 check(calls===1&&otherCalls===0);`],
 ['listener dispatch preserves container identity and pending readiness',`
 const target=navigator.serviceWorker,ready=target.ready;let calls=0,settled=false;
 check(target===navigator.serviceWorker&&ready===target.ready&&ready instanceof Promise);
 target.addEventListener('message',()=>calls++,{once:true});ready.then(()=>settled=true);
 target.dispatchEvent(new Event('message'));target.dispatchEvent(new Event('message'));
 await Promise.resolve();await Promise.resolve();check(calls===1&&!settled);
 const registrations=await target.getRegistrations();check(Array.isArray(registrations)&&registrations.length===0);`],
];
function fixtureHtml(source){return scriptResultFixture(source,'SERVICE_WORKER_EVENTS_ERROR','SERVICE_WORKER_EVENTS_RESULT','',true)}
module.exports={cases,fixtureHtml};
