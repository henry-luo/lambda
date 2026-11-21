const {scriptResultFixture}=require('./script_result_fixture.cjs');
const cases=[
 ['asynchronous pull waits for its chunk',`
 const reader=new ReadableStream({async pull(c){await Promise.resolve();if(typeof gc==='function')gc();c.enqueue('value');c.close()}}).getReader();
 const first=await reader.read(),last=await reader.read();check(first.value==='value'&&!first.done&&last.done);`],
 ['concurrent reads serialize pull calls',`
 let active=0,max=0,count=0;
 const reader=new ReadableStream({async pull(c){active++;max=Math.max(max,active);await Promise.resolve();c.enqueue(++count);active--;if(count===3)c.close()}}).getReader();
 const values=await Promise.all([reader.read(),reader.read(),reader.read(),reader.read()]);
 check(max===1&&count===3&&values.map(x=>x.done?'end':x.value).join(',')==='1,2,3,end');`],
 ['pushed data and closure settle pending reads',`
 let controller;const reader=new ReadableStream({start(c){controller=c}}).getReader();let settled=false;
 const first=reader.read().then(x=>{settled=true;return x});await Promise.resolve();check(!settled);
 const second=reader.read(),third=reader.read();if(typeof gc==='function')gc();controller.enqueue('later');controller.close();
 check((await first).value==='later'&&(await second).done&&(await third).done);`],
 ['asynchronous start gates pull',`
 let started=false,pulls=0;
 const reader=new ReadableStream({async start(){await Promise.resolve();started=true},pull(c){check(started);pulls++;c.enqueue(7);c.close()}}).getReader();
 const result=await reader.read();check(started&&pulls===1&&result.value===7&&!result.done&&(await reader.read()).done);`],
 ['pull errors reject pending and future reads',`
 for(const asyncThrow of [false,true]){
 const marker={tag:'original'};let count=0;const reader=new ReadableStream({pull(){count++;if(asyncThrow)return Promise.reject(marker);throw marker}}).getReader();
 const reads=[reader.read(),reader.read()];for(const promise of reads){let rejected=false;try{await promise}catch(error){rejected=error===marker}check(rejected)}
 let future=false;try{await reader.read()}catch(error){future=error===marker}check(future&&count===1);
 }
 let c;const marker={tag:'controller'};const reader=new ReadableStream({start(controller){c=controller}}).getReader();const pending=reader.read();c.error(marker);
 let rejected=false;try{await pending}catch(error){rejected=error===marker}check(rejected);`],
 ['asynchronous start failure rejects the read',`
 const marker={tag:'start'};let pulled=false;
 const reader=new ReadableStream({async start(){await Promise.resolve();throw marker},pull(){pulled=true}}).getReader();
 let rejected=false;try{await reader.read()}catch(error){rejected=error===marker}check(rejected&&!pulled);`],
 ['framed response decoder survives asynchronous pull',`
 const bytes=Uint8Array.from([0,0,0,0,2,8,1,128,0,0,0,2,13,10]);
 const body=new Response(bytes).body;let input,buffer=new Uint8Array(0);
 const decoded=new ReadableStream({start(){input=body.getReader()},async pull(c){
 let frame;for(;;){if(frame===undefined&&buffer.byteLength>=5){let size=0;for(let i=1;i<5;i++)size=(size<<8)+buffer[i];frame={flags:buffer[0],size}}
 if(frame!==undefined&&buffer.byteLength>=frame.size+5)break;const part=await input.read();if(part.done)break;
 const next=new Uint8Array(buffer.length+part.value.length);next.set(buffer);next.set(part.value,buffer.length);buffer=next;}
 if(frame===undefined){if(buffer.byteLength===0)c.close();else c.error(new Error('partial'));return}
 const data=buffer.subarray(5,5+frame.size);buffer=buffer.subarray(5+frame.size);c.enqueue({flags:frame.flags,data});
 }}).getReader();
 const message=await decoded.read(),trailer=await decoded.read(),end=await decoded.read();
 check(!message.done&&message.value.flags===0&&Array.from(message.value.data).join(',')==='8,1');
 check(!trailer.done&&trailer.value.flags===128&&Array.from(trailer.value.data).join(',')==='13,10'&&end.done);`],
];
function fixtureHtml(source){return scriptResultFixture(source,'STREAM_FIXTURE_ERROR','STREAM_FIXTURE_RESULT')}
module.exports={cases,fixtureHtml};
