const helperSource=`var marker='helper';
export function drive(factory){const values=[],callers=[];for(const value of factory()){values.push(value);callers.push(marker)}return {values,callers,after:marker}}
export function close(factory){const iterator=factory(),values=[iterator.next().value,iterator.next().value];const result=iterator.return('closed');return {values,result,after:marker}}
export async function driveAsync(factory){const values=[],callers=[];for await(const value of factory()){values.push(value);callers.push(marker)}return {values,callers,after:marker}}
export function* foreign(){yield marker}
var guard=(object,brand)=>{if(!brand.has(object))throw TypeError('missing private brand')};
export var read=(object,brand,method)=>(guard(object,brand),method),add=(object,brand)=>brand.add(object);
export var run=(self,args,body)=>new Promise((resolve,reject)=>{const iterator=body.apply(self,args);const step=result=>result.done?resolve(result.value):Promise.resolve(result.value).then(value=>step(iterator.next(value)),reject);step(iterator.next())});`;
const fixtures=[
 {name:'custom-element-private-helper',declarations:`var guard,method,rows=[],instances=[];
 var Probe=class extends HTMLElement{constructor(){super();add(this,guard);instances.push(this)}
 connectedCallback(){return run(this,null,function*(){const value=read(this,guard,method).call(this);rows.push({branded:guard.has(this),same:this===instances[instances.length-1],value});yield value})}};
 guard=new WeakSet;method=function(){return 42};`,
 body:`customElements.define('module-brand-probe',Probe);await new Promise(resolve=>setTimeout(resolve,0));return {rows};`,
 expected:{rows:[{branded:true,same:true,value:42}]}},
 {name:'compact-tail-yield',declarations:`var marker='owner';function* source(){yield marker}`,
 body:'return drive(source);',expected:{values:['owner'],callers:['helper'],after:'helper'}},
 {name:'yield-close-restores-caller',declarations:`var marker='owner',closed='';function* source(){try{yield marker;marker='updated';yield marker}finally{closed=marker}}`,
 body:'const result=close(source);return {result,closed,owner:marker};',
 expected:{result:{values:['owner','updated'],result:{value:'closed',done:true},after:'helper'},closed:'updated',owner:'updated'}},
 {name:'cross-module-delegation',declarations:`var marker='owner';function* source(){yield marker;yield* foreign();yield marker}`,
 body:'return drive(source);',expected:{values:['owner','helper','owner'],callers:['helper','helper','helper'],after:'helper'}},
 {name:'parameter-default-and-live-binding',declarations:`var marker='owner';function* source(arg=marker){yield arg;yield marker;marker='updated';yield marker}`,
 body:'return drive(source);',expected:{values:['owner','owner','updated'],callers:['helper','helper','helper'],after:'helper'}},
 {name:'async-generator-await',declarations:`var marker='owner';async function* source(){await Promise.resolve();yield marker;await Promise.resolve();marker='updated';yield marker}`,
 body:'return driveAsync(source);',expected:{values:['owner','updated'],callers:['helper','helper'],after:'helper'}},
];
function mainSource(fixture){return `import{drive,close,driveAsync,foreign,read,add,run}from'./helpers.js';
 ${fixture.declarations}
 (async()=>{${fixture.body}})().then(value=>{window.generatorModuleResult=value;console.log('GENERATOR_MODULE_DETAILS',JSON.stringify(value))},
 error=>console.log('GENERATOR_MODULE_DETAILS',JSON.stringify({error:String(error)})));`;}
function serveGeneratorModule(request,response){
 const [,name,file]=new URL(request.url,'http://fixture.invalid').pathname.split('/'),fixture=fixtures.find(f=>f.name===name);
 if(!fixture)return response.writeHead(404).end();
 response.writeHead(200,{'Content-Type':'text/javascript'}).end(file==='helpers.js'?helperSource:mainSource(fixture));
}
function fixtureHtml(fixture,origin){return `<!doctype html><body><module-brand-probe></module-brand-probe><script type=module src='${origin}/${fixture.name}/main.js'></script>`;}
module.exports={fixtures,mainSource,helperSource,serveGeneratorModule,fixtureHtml};
