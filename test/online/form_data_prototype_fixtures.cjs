const {scriptResultFixture}=require('./script_result_fixture.cjs');
const fixtures=[
 {name:'FormData publishes shared iterable methods on its prototype',body:`
    const data=new FormData(),names=['append','delete','get','getAll','has','set','entries','keys','values','forEach'];
    return {published:names.every(name=>typeof FormData.prototype[name]==='function'),
        inherited:names.every(name=>data[name]===FormData.prototype[name]&&!Object.hasOwn(data,name)),
        iterable:FormData.prototype[Symbol.iterator]===FormData.prototype.entries,instance:data instanceof FormData};`,
    expected:{published:true,inherited:true,iterable:true,instance:true}},
 {name:'borrowed FormData methods retain live iteration and callback ownership',body:`
    const data=new FormData(),prototype=FormData.prototype;
    prototype.append.call(data,'a','1');prototype.append.call(data,'b','2');
    const iterator=prototype.entries.call(data),first=iterator.next().value;
    prototype.append.call(data,'c','3');if(typeof gc==='function')gc();
    const owner={},calls=[];prototype.forEach.call(data,function(value,key,form){calls.push(this===owner&&form===data&&value===form.get(key))},owner);
    return {first,rest:Array.from(iterator),keys:Array.from(prototype.keys.call(data)),values:Array.from(prototype.values.call(data)),calls};`,
    expected:{first:['a','1'],rest:[['b','2'],['c','3']],keys:['a','b','c'],values:['1','2','3'],calls:[true,true,true]}},
 {name:'prototype replacement affects existing and new FormData instances',body:`
    const old=new FormData(),saved=FormData.prototype.entries;let calls=0;
    FormData.prototype.entries=function(){calls++;return saved.call(this)};
    old.append('a','1');const fresh=new FormData();fresh.append('b','2');
    const values=[Array.from(old.entries()),Array.from(fresh.entries())];FormData.prototype.entries=saved;
    return {calls,values};`,expected:{calls:2,values:[[['a','1']],[['b','2']]]}},
 {name:'native formdata events share the intrinsic after global replacement',body:`
    const Constructor=FormData,form=document.createElement('form');let eventData,error='none';
    form.innerHTML='<input name="a" value="1">';document.body.appendChild(form);
    form.addEventListener('formdata',event=>{eventData=event.formData;try{eventData.append('b','2')}catch(e){error=e.name}});
    globalThis.FormData=function(){};if(typeof gc==='function')gc();
    const data=new Constructor(form);globalThis.FormData=Constructor;
    return {error,shared:Object.getPrototypeOf(eventData)===Constructor.prototype,
        inherited:eventData.entries===Constructor.prototype.entries&&!Object.hasOwn(eventData,'entries'),
        snapshot:Array.from(data),event:Array.from(eventData),separate:eventData!==data};`,
    expected:{error:'none',shared:true,inherited:true,snapshot:[['a','1'],['b','2']],event:[['a','1'],['b','2']],separate:true}},
 {name:'published FormData methods reject receivers without the native brand',body:`
    const prototype=FormData.prototype;function error(callback){try{callback();return 'none'}catch(e){return e.name}}
    return {callable:typeof prototype.entries==='function',append:error(()=>prototype.append.call({},'a','1')),
        forged:error(()=>prototype.get.call({_fd_entries:[]},'a')),
        inherited:error(()=>prototype.entries.call(Object.create(prototype))),
        callback:error(()=>prototype.forEach.call({},()=>{}))};`,
    expected:{callable:true,append:'TypeError',forged:'TypeError',inherited:'TypeError',callback:'TypeError'}},
];
function fixtureHtml(fixture){return scriptResultFixture(`
    const result=(function(){${fixture.body}})();window.__formDataPrototypeResult=result;
    console.log('FORM_DATA_PROTO_DETAILS',JSON.stringify(result));
    check(JSON.stringify(result)===${JSON.stringify(JSON.stringify(fixture.expected))});`,
    'FORM_DATA_PROTO_ERROR','FORM_DATA_PROTO_RESULT');}
module.exports={fixtures,fixtureHtml};
