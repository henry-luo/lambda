const {test}=require('node:test'),assert=require('node:assert/strict');
const {renderFixtures,loggedResult,rgb,serveFixtureResources}=require('./render_fixture.cjs');
const {cookieExpiryCases,cookieExpiryHtml}=require('./cookie_expiry_fixtures.cjs');
const render=renderFixtures('cookie-expiry');
const origin=serveFixtureResources((request,response)=>{
    const fixture=cookieExpiryCases.find(f=>'/'+f.name===request.url);
    if(!fixture)return response.writeHead(404).end();
    response.writeHead(200,{'Content-Type':'text/html'}).end(cookieExpiryHtml(fixture));
});
for(const fixture of cookieExpiryCases)test('document cookie expiry: '+fixture.name,async()=>{
    const {image,log}=await render(fixture.name,{url:origin()+'/'+fixture.name});
    assert.deepEqual(loggedResult(log,'COOKIE_EXPIRY_RESULT'),{supported:true,remaining:fixture.remaining,session:true});
    assert.deepEqual(rgb(image,40,40),[0,255,0]);
});
