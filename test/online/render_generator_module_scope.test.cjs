const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,loggedResult,serveFixtureResources}=require('./render_fixture.cjs');
const {fixtures,serveGeneratorModule,fixtureHtml}=require('./generator_module_scope_fixtures.cjs');
const render=renderFixtures('generator-module-scope'),origin=serveFixtureResources(serveGeneratorModule);
for(const backend of ['ast','mir'])for(const fixture of fixtures)test(backend+': '+fixture.name,async()=>{
 const {log}=await render(backend+'-'+fixture.name,fixtureHtml(fixture,origin()),{
  ...process.env,JS_EXEC_BACKEND:backend,LAMBDA_POST_LOAD_SETTLE_MS:'1000',
 });
 assert.deepEqual(loggedResult(log,'GENERATOR_MODULE_DETAILS'),fixture.expected);
});
