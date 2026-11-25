const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,rgb}=require('./render_fixture.cjs');
const render=renderFixtures('snapshot-ready-tasks');

test('snapshot finishes ready continuations without running future timers',async()=>{
    const html=`<!doctype html><style>body{margin:0}#result{width:100px;height:100px;background:red}</style>
    <div id=result></div><script>
    let count=0;
    function step(){
        count++;
        if(count<32)setImmediate(step);
        else {document.getElementById('result').style.background='lime';console.log('READY_CHAIN_DONE',count)}
    }
    setTimeout(()=>setImmediate(step),1000);
    setTimeout(()=>{document.getElementById('result').style.background='blue';console.log('FUTURE_TIMER_RAN')},1200);
    </script>`;
    const {image,log}=await render('ready-chain',html,{...process.env,
        LAMBDA_POST_LOAD_SETTLE_MS:'1000',LAMBDA_CAPTURE_LOAD_CLOCK:'elapsed'});
    assert.match(log,/READY_CHAIN_DONE 32/);
    assert.ok(!log.includes('FUTURE_TIMER_RAN'));
    assert.deepEqual(rgb(image,30,30),[0,255,0]);
});
