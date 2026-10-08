const http = require('http');
const fs = require('fs');
const path = require('path');
const puppeteer = require('puppeteer');
const crypto = require('crypto');
const output = process.argv[2] || 'temp/webgl2/browser';
fs.mkdirSync(output,{recursive:true});
(async () => {
 const root=process.cwd();
 const server=http.createServer((req,res)=>{
  const name=path.resolve(root,'.'+decodeURIComponent(req.url.split('?')[0]));
  if(!name.startsWith(root+path.sep)) {res.writeHead(403);res.end();return;}
  try {res.setHeader('Content-Type',name.endsWith('.js')?'text/javascript':name.endsWith('.html')?'text/html':'application/octet-stream');res.end(fs.readFileSync(name));}catch(e){res.writeHead(404);res.end(e.message);}
 });
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 let browser;
 try {
 browser=await puppeteer.launch({executablePath:process.env.CHROME_HEADLESS_SHELL,headless:true,args:['--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
 const page=await browser.newPage();await page.setViewport({width:848,height:650,deviceScaleFactor:1});
 page.on('console',msg=>console.log('browser:',msg.text()));page.on('pageerror',e=>console.log('browser error:',e.message));
 await page.evaluateOnNewDocument(()=>{
  globalThis.webglAudit={calls:{},queries:{},shaders:[]};
  const prototype=WebGL2RenderingContext.prototype;
  for(const name of Object.getOwnPropertyNames(prototype)) {
   const descriptor=Object.getOwnPropertyDescriptor(prototype,name);if(typeof descriptor.value!=='function')continue;
   const original=descriptor.value;Object.defineProperty(prototype,name,{...descriptor,value:function(...args){
    webglAudit.calls[name]=(webglAudit.calls[name]||0)+1;
    if(name==='getParameter'||name==='getExtension')webglAudit.queries[String(args[0])]=true;
    if(name==='shaderSource')webglAudit.shaders.push(args[1]);
    return Reflect.apply(original,this,args);
   }});
   Object.defineProperty(prototype[name],'length',{value:original.length});
  }
 });
 await page.goto('http://127.0.0.1:'+server.address().port+'/test/demo/scene3d/three-gallery.html');
 await page.waitForFunction('globalThis.threeGalleryReady === true',{timeout:30000});
 await page.screenshot({path:path.join(output,'three-gallery-chromium.png')});
 const gallery=await page.evaluate(()=>webglAudit);
 const canvasRect=await page.$eval('#garden',canvas=>{const rect=canvas.getBoundingClientRect();return {x:rect.x,y:rect.y,width:rect.width,height:rect.height};});
 const results=[];
 for(const fixture of ['api','khronos-selected','loss','three-lifecycle']) {
  await page.goto('http://127.0.0.1:'+server.address().port+'/test/webgl/'+fixture+'.html');
  await page.waitForFunction('document.getElementById("result").hasAttribute("data-result") && document.getElementById("result").getAttribute("data-result") !== "pending"',{timeout:30000});
  const result=await page.evaluate(()=>({result:document.getElementById('result').getAttribute('data-result'),checks:document.getElementById('result').getAttribute('data-checks'),audit:webglAudit}));
  results.push({fixture,...result});
  if(result.result!=='passed')throw new Error(fixture+': '+result.result);
 }
 const calls={...gallery.calls};
 for(const result of results)for(const [name,count] of Object.entries(result.audit.calls)) calls[name]=(calls[name]||0)+count;
 const shaders=[...new Set(gallery.shaders)];
 const report={threeVersion:'0.186.1',browser:await browser.version(),viewport:{width:848,height:650,deviceScaleFactor:1},browserGl:'ANGLE SwiftShader',canvasRect,galleryCalls:gallery.calls,selectedCalls:calls,galleryQueries:Object.keys(gallery.queries),shaderCorpus:shaders.map(source=>({sha256:crypto.createHash('sha256').update(source).digest('hex'),bytes:Buffer.byteLength(source)})),fixtures:results.map(({audit,...result})=>result)};
 fs.writeFileSync(path.join(output,'manifest.json'),JSON.stringify(report,null,2)+'\n');
 console.log('capture complete');
 } finally {if(browser)await browser.close();server.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
