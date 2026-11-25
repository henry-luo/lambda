const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,loggedResult}=require('./render_fixture.cjs');
const {scriptResultFixture}=require('./script_result_fixture.cjs');
const render=renderFixtures('error-source-trace');
const html=scriptResultFixture(`let caught=0,touched=0;
 for(let i=0;i<6;i++){try{const value=null;value.member}catch(error){if(error.name==='TypeError')caught++}}
 try{throw {get name(){touched++;return 'guest'},get message(){touched++;return 'guest'}}}catch(error){}
 console.log('ERROR_SOURCE_DETAILS',JSON.stringify({caught,touched,answer:6*7}));
 check(caught===6&&touched===0);`,'ERROR_SOURCE_ERROR','ERROR_SOURCE_RESULT');
for(const setting of [undefined,'','0','2'])test('error source tracing: '+String(setting),async()=>{
 const env={...process.env,JS_EXEC_BACKEND:'ast'};delete env.LAMBDA_JS_ERROR_SOURCE_TRACE;
 if(setting!==undefined)env.LAMBDA_JS_ERROR_SOURCE_TRACE=setting;
 const {log}=await render('case-'+String(setting),html,env);
 assert.deepEqual(loggedResult(log,'ERROR_SOURCE_DETAILS'),{caught:6,touched:0,answer:42});
 const traces=log.split('\n').filter(line=>line.includes('js-error-source:'));
 assert.equal(traces.length,setting==='2'?2:0);
 for(const line of traces)assert.match(line,/source=value\.member/);
});
