const {scriptResultFixture}=require('./script_result_fixture.cjs');
const fixtures=[];
for(const kind of ['detached','iframe'])for(const chained of [false,true])fixtures.push({
    name:kind+(chained?' chained stream':' separate stream control'),
    body:`const parentDocument=document,marker=document.createElement('p');
        marker.id='parent-marker';document.body.appendChild(marker);
        ${kind==='iframe'
            ? `const frame=document.createElement('iframe');document.body.appendChild(frame);const other=frame.contentDocument;`
            : `const other=document.implementation.createHTMLDocument('child');`}
        const opened=other.open();
        ${chained?'opened':'other'}.write('<p id="child-marker">child</p>');other.close();
        return {same:${chained?'opened===other':'null'},parent:!!parentDocument.getElementById('parent-marker'),
            child:!!other.getElementById('child-marker'),leaked:!!parentDocument.getElementById('child-marker')};`,
    expected:{same:chained?true:null,parent:true,child:true,leaked:false},
});
function fixtureHtml(fixture) {
    return scriptResultFixture(`const result=(function(){${fixture.body}})();
        console.log('DOCUMENT_OPEN_DETAILS',JSON.stringify(result));
        check(JSON.stringify(result)===${JSON.stringify(JSON.stringify(fixture.expected))});`,
        'DOCUMENT_OPEN_ERROR','DOCUMENT_OPEN_RESULT');
}
module.exports={fixtures,fixtureHtml};
