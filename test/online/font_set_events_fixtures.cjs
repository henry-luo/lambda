const {scriptResultFixture}=require('./script_result_fixture.cjs');
const fixtures=[
 {name:'document fonts inherits the shared EventTarget methods',body:`
    const fonts=document.fonts;
    return {stable:fonts===document.fonts,target:fonts instanceof EventTarget,
        methods:['addEventListener','removeEventListener','dispatchEvent'].every(name=>
            fonts[name]===EventTarget.prototype[name]&&!Object.hasOwn(fonts,name)),
        ready:fonts.ready instanceof Promise&&fonts.ready===document.fonts.ready};`,
    expected:{stable:true,target:true,methods:true,ready:true}},
 {name:'font listeners support removal duplicate suppression and once',body:`
    const fonts=document.fonts,calls=[];
    function listener(event){calls.push(this===fonts&&event.target===fonts&&event.currentTarget===fonts)}
    fonts.addEventListener('loadingdone',listener);fonts.addEventListener('loadingdone',listener);
    fonts.dispatchEvent(new Event('loadingdone'));fonts.removeEventListener('loadingdone',listener);
    fonts.dispatchEvent(new Event('loadingdone'));
    const object={handleEvent(event){calls.push(this===object&&event.target===fonts)}};
    fonts.addEventListener('loadingerror',object,{once:true});
    fonts.dispatchEvent(new Event('loadingerror'));fonts.dispatchEvent(new Event('loadingerror'));
    return {calls};`,expected:{calls:[true,true]}},
 {name:'font event cancellation signals and unrelated targets remain independent',body:`
    const fonts=document.fonts,other=new EventTarget(),controller=new AbortController();let aborted=0,foreign=0;
    fonts.addEventListener('loading',()=>aborted++,{signal:controller.signal});controller.abort();
    other.addEventListener('loading',()=>foreign++);
    fonts.addEventListener('loading',event=>event.preventDefault());
    const event=new Event('loading',{cancelable:true});
    const canceled=fonts.dispatchEvent(event)===false;
    other.dispatchEvent(new Event('loading'));if(typeof gc==='function')gc();
    return {canceled,prevented:event.defaultPrevented,aborted,foreign,stable:document.fonts===fonts};`,
    expected:{canceled:true,prevented:true,aborted:0,foreign:1,stable:true}},
];
function fixtureHtml(fixture){return scriptResultFixture(`
    const result=(function(){${fixture.body}})();window.__fontSetEventsResult=result;
    console.log('FONT_SET_EVENTS_DETAILS',JSON.stringify(result));
    check(JSON.stringify(result)===${JSON.stringify(JSON.stringify(fixture.expected))});`,
    'FONT_SET_EVENTS_ERROR','FONT_SET_EVENTS_RESULT');}
module.exports={fixtures,fixtureHtml};
