const {scriptResultFixture}=require('./script_result_fixture.cjs');
const cases=[
 ['asynchronous forks preserve ordered shared chunks for unequal consumers',`
 let count=0,active=0,max=0;
 const source=new ReadableStream({async pull(c){active++;max=Math.max(max,active);
  await Promise.resolve();if(typeof gc==='function')gc();c.enqueue({value:++count});active--;if(count===3)c.close()}});
 const [left,right]=source.tee();check(source.locked&&!left.locked&&!right.locked);
 const a=left.getReader(),b=right.getReader(),first=await a.read(),other=await b.read();
 check(first.value===other.value&&first.value.value===1);
 const rest=await Promise.all([a.read(),a.read(),a.read()]);
 check(rest.map(x=>x.done?'end':x.value.value).join(',')==='2,3,end');
 check((await b.read()).value===rest[0].value&&(await b.read()).value===rest[1].value&&(await b.read()).done&&max===1);`],
 ['both pending branches receive later push and close',`
 let c;const source=new ReadableStream({start(controller){c=controller}}),[left,right]=source.tee();
 const a=left.getReader(),b=right.getReader(),one=a.read(),two=b.read();let done=false;
 one.then(()=>{done=true});await Promise.resolve();check(!done);
 c.enqueue('later');c.close();check((await one).value==='later'&&(await two).value==='later');
 check((await a.read()).done&&(await b.read()).done);await Promise.all([a.closed,b.closed]);`],
 ['source errors reject both branches with the original reason',`
 for(const early of [false,true]){
 const reason={tag:'source'};let c;const source=new ReadableStream({start(controller){c=controller;if(early)c.error(reason)}});
 const [left,right]=source.tee(),a=left.getReader(),b=right.getReader();
 const caught=p=>p.then(()=>false,error=>error===reason);
 const closed=[caught(a.closed),caught(b.closed)],reads=[caught(a.read()),caught(b.read())];
 if(!early)c.error(reason);check((await Promise.all([...closed,...reads])).every(Boolean));
 }`],
 ['cancellation waits for both branches and preserves reason order',`
 const first={branch:1},second={branch:2};let reasons,finish;
 const source=new ReadableStream({cancel(value){reasons=value;return new Promise(resolve=>{finish=resolve})}});
 const [left,right]=source.tee();let settled=false;
 const a=left.cancel(first).then(()=>{settled=true});await Promise.resolve();check(!settled&&reasons===undefined);
 const b=right.cancel(second);await Promise.resolve();check(!settled&&reasons[0]===first&&reasons[1]===second);
 if(typeof gc==='function')gc();finish();await Promise.all([a,b]);check(settled&&source.locked);`],
 ['one canceled branch leaves the other readable until closure',`
 let c,canceled=false;const source=new ReadableStream({start(controller){c=controller},cancel(){canceled=true}});
 const [left,right]=source.tee();let settled=false;const cancellation=left.cancel('unused').then(()=>{settled=true});
 const reader=right.getReader(),pending=reader.read();await Promise.resolve();check(!settled&&!canceled);
 c.enqueue(9);c.close();check((await pending).value===9&&(await reader.read()).done);
 await cancellation;check(settled&&!canceled);`],
 ['closed sources fork without fake chunks and locked sources reject tee',`
 const source=new ReadableStream({start(c){c.close()}}),[left,right]=source.tee();
 check((await left.getReader().read()).done&&(await right.getReader().read()).done);
 let locked=false,branded=false;try{source.tee()}catch(e){locked=e instanceof TypeError}
 try{source.tee.call({})}catch(e){branded=e instanceof TypeError}check(locked&&branded);
 const held=new ReadableStream({}),reader=held.getReader();let rejected=false;
 try{held.tee()}catch(e){rejected=e instanceof TypeError}check(held.locked&&rejected);reader.releaseLock();check(!held.locked);`],
 ['reader release rejects its pending reads while preserving the stream',`
 let c;const source=new ReadableStream({start(controller){c=controller}}),reader=source.getReader();
 const closed=reader.closed.catch(e=>e instanceof TypeError),pending=reader.read().catch(e=>e instanceof TypeError);
 reader.releaseLock();check(!source.locked&&await pending&&await closed);
 const next=source.getReader();c.enqueue(4);c.close();check((await next.read()).value===4&&(await next.read()).done);
 await next.closed;let released=false;try{await reader.read()}catch(e){released=e instanceof TypeError}check(released);`],
 ['source cancellation failure rejects both branch cancellations',`
 const reason={tag:'cancel-failure'};
 const source=new ReadableStream({async cancel(){await Promise.resolve();throw reason}}),[left,right]=source.tee();
 const caught=p=>p.then(()=>false,error=>error===reason);
 check((await Promise.all([caught(left.cancel('left')),caught(right.cancel('right'))])).every(Boolean));`],
 ['a source error settles the canceled branch without canceling the source',`
 let c,canceled=false;const reason={tag:'failure'};
 const source=new ReadableStream({start(controller){c=controller},cancel(){canceled=true}}),[left,right]=source.tee();
 const cancellation=left.cancel('unused'),reader=right.getReader();
 const pending=reader.read().then(()=>false,error=>error===reason);c.error(reason);
 await cancellation;check(await pending&&!canceled);`],
 ['cancellation discards queued data and prevents late enqueue',`
 let c,cancels=0;const source=new ReadableStream({start(controller){c=controller;c.enqueue(1);c.close()},cancel(){cancels++}});
 const reader=source.getReader();let locked=false;try{await source.cancel()}catch(e){locked=e instanceof TypeError}
 await reader.cancel();check(locked&&cancels===1&&(await reader.read()).done);await reader.closed;
 let rejected=false;try{c.enqueue(2)}catch(e){rejected=e instanceof TypeError}check(rejected);
 check((await reader.read()).done);`],
 ['the source cancel algorithm is captured and validated during construction',`
 let gets=0,calls=0;const underlying={get cancel(){gets++;return function(reason){calls++;check(this===underlying&&reason==='original')}}};
 const source=new ReadableStream(underlying);check(gets===1);
 Object.defineProperty(underlying,'cancel',{value(){throw new Error('replacement')}});
 await source.cancel('original');check(calls===1&&gets===1);
 for(const invalid of [4,'bad',{}]){let rejected=false;try{new ReadableStream({cancel:invalid})}catch(e){rejected=e instanceof TypeError}check(rejected)}`],
];
function fixtureHtml(source){return scriptResultFixture(source,'TEE_FIXTURE_ERROR','TEE_FIXTURE_RESULT','',true)}
module.exports={cases,fixtureHtml};
