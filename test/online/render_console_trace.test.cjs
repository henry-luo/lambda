const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,loggedResult}=require('./render_fixture.cjs');
const {fixtures,fixtureHtml}=require('./console_trace_fixtures.cjs');
const render=renderFixtures('console-trace');
for(const backend of ['ast','mir'])for(const [index,f]of fixtures.entries())test(backend+': '+f.name,async()=>{
 const {log}=await render(backend+'-'+index,fixtureHtml(f),{...process.env,JS_EXEC_BACKEND:backend});
 assert.deepEqual(loggedResult(log,'CONSOLE_TRACE_DETAILS'),f.expected);
 if(f.output)assert.ok(log.includes(f.output),'console must emit its message');
 if(f.quiet)assert.ok(!log.includes(f.quiet),'a successful assertion must stay quiet');
});
