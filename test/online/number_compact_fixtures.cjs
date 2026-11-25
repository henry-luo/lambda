const {scriptResultFixture}=require('./script_result_fixture.cjs');
const fixtures=[
 {name:'English compact defaults select precision and carry across units',body:`
    const format=new Intl.NumberFormat('en',{notation:'compact'});
    return {values:[0,0.000123,0.1234,1.234,12.34,123.45,999.4,999.5,1000,1234,12345,123456,999499,999500,1e6,1234567890,1e15,-1234].map(x=>format.format(x))};`,
    expected:{values:['0','0.00012','0.12','1.2','12','123','999','1K','1K','1.2K','12K','123K','999K','1M','1M','1.2B','1000T','-1.2K']}},
 {name:'English long compact patterns retain units and parts',body:`
    const format=new Intl.NumberFormat('en-GB',{notation:'compact',compactDisplay:'long'});
    return {values:[1000,1234,12000,1e6,-1e9,1e12].map(x=>format.format(x)),parts:format.formatToParts(1234).map(p=>[p.type,p.value])};`,
    expected:{values:['1 thousand','1.2 thousand','12 thousand','1 million','-1 billion','1 trillion'],
        parts:[['integer','1'],['decimal','.'],['fraction','2'],['literal',' '],['compact','thousand']]}},
 {name:'explicit fraction precision rounds compact values before choosing the final unit',body:`
    const precise=new Intl.NumberFormat('en',{notation:'compact',minimumFractionDigits:2,maximumFractionDigits:2});
    const whole=new Intl.NumberFormat('en',{notation:'compact',maximumFractionDigits:0});
    return {precise:[1000,1234,999.999].map(x=>precise.format(x)),whole:[999.5,999500].map(x=>whole.format(x))};`,
    expected:{precise:['1.00K','1.23K','1.00K'],whole:['1K','1M']}},
 {name:'compact grouping defaults to min2 and honors explicit modes',body:`
    const options=[undefined,false,true,'auto','always','min2','true','false'];
    return {values:options.map(useGrouping=>new Intl.NumberFormat('en',{notation:'compact',useGrouping}).format(1e15)),
        next:options.map(useGrouping=>new Intl.NumberFormat('en',{notation:'compact',useGrouping}).format(1e16))};`,
    expected:{values:['1000T','1000T','1,000T','1000T','1,000T','1000T','1000T','1000T'],
        next:['10,000T','10000T','10,000T','10,000T','10,000T','10,000T','10,000T','10,000T']}},
 {name:'percent formatting scales decimal positions without overflowing finite values',body:`
    const format=new Intl.NumberFormat('en',{style:'percent',useGrouping:false});
    const huge=format.format(Number.MAX_VALUE),precise=new Intl.NumberFormat('en',{style:'percent',minimumFractionDigits:2});
    return {values:[0,-0,0.125,-0.125,1,12.34,NaN,Infinity,-Infinity].map(x=>format.format(x)),
        precise:[0.125,0.0009].map(x=>precise.format(x)),finite:!huge.includes('∞'),digits:huge.length-1};`,
    expected:{values:['0%','-0%','13%','-13%','100%','1234%','NaN%','∞%','-∞%'],precise:['12.50%','0.09%'],finite:true,digits:311}},
 {name:'compact and percent compose through format parts',body:`
    const format=new Intl.NumberFormat('en',{style:'percent',notation:'compact'});
    return {values:[0.001234,0.1234,12.34,1234].map(x=>format.format(x)),parts:format.formatToParts(12.34).map(p=>p.value)};`,
    expected:{values:['0.12%','12%','1.2K%','123K%'],parts:['1','.','2','K','%']}},
 {name:'resolved options expose compact default precision and percent style',body:`
    const compact=new Intl.NumberFormat('en',{notation:'compact'}).resolvedOptions();
    const percent=new Intl.NumberFormat('en',{style:'percent'}).resolvedOptions();
    const explicit=new Intl.NumberFormat('en',{notation:'compact',maximumFractionDigits:1}).resolvedOptions();
    return {compact:[compact.style,compact.notation,compact.compactDisplay,compact.useGrouping,compact.minimumFractionDigits,compact.maximumFractionDigits,compact.minimumSignificantDigits,compact.maximumSignificantDigits,compact.roundingPriority],
        percent:[percent.style,percent.notation,percent.minimumFractionDigits,percent.maximumFractionDigits],
        explicit:[explicit.maximumFractionDigits,explicit.roundingPriority,'maximumSignificantDigits' in explicit]};`,
    expected:{compact:['decimal','compact','short','min2',0,0,1,2,'morePrecision'],percent:['percent','standard',0,0],explicit:[1,'auto',false]}},
 {name:'trailing zero stripping uses the rounded compact value',body:`
    const format=new Intl.NumberFormat('en',{notation:'compact',minimumFractionDigits:2,maximumFractionDigits:2,trailingZeroDisplay:'stripIfInteger'});
    return {values:[1000,1500,-1e6].map(x=>format.format(x))};`,expected:{values:['1K','1.50K','-1M']}},
 {name:'standard decimal behavior and option validation remain intact',body:`
    function error(callback){try{callback();return 'none'}catch(e){return e.name}}
    return {decimal:new Intl.NumberFormat('en',{minimumIntegerDigits:4,minimumFractionDigits:2}).format(1.5),
        parts:new Intl.NumberFormat('en',{style:'percent'}).formatToParts(-0.125).map(p=>[p.type,p.value]),
        invalid:error(()=>new Intl.NumberFormat('en',{compactDisplay:'unknown'})),group:error(()=>new Intl.NumberFormat('en',{useGrouping:'unknown'}))};`,
    expected:{decimal:'0,001.50',parts:[['minusSign','-'],['integer','13'],['percentSign','%']],invalid:'RangeError',group:'RangeError'}},
 {name:'bound format and private precision survive coercion collection and option mutation',body:`
    let calls=0;const format=new Intl.NumberFormat('en',{get notation(){calls++;if(typeof gc==='function')gc();return 'compact'}});
    const bound=format.format,options=format.resolvedOptions();options.notation='standard';
    return {calls,bound:bound({valueOf(){if(typeof gc==='function')gc();return 1234}}),private:format.resolvedOptions().notation};`,
    expected:{calls:1,bound:'1.2K',private:'compact'}},
];
function fixtureHtml(fixture){return scriptResultFixture(`
    const result=(function(){${fixture.body}})();window.__numberCompactResult=result;
    console.log('NUMBER_COMPACT_DETAILS',JSON.stringify(result));
    check(JSON.stringify(result)===${JSON.stringify(JSON.stringify(fixture.expected))});`,
    'NUMBER_COMPACT_ERROR','NUMBER_COMPACT_RESULT');}
module.exports={fixtures,fixtureHtml};
