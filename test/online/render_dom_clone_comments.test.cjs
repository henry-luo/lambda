const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const render = renderFixtures('dom-clone-comments');

for (const operation of ['clone', 'import', 'foreign-import']) {
    test('DOM deep clone preserves comment anchors: ' + operation, async () => {
        const {image, log} = await render(operation, `<!doctype html>
            <style>body{margin:0}#probe{width:80px;height:80px;background:red}</style>
            <div id="probe"></div><template id="source"><section><!--first-->text<span><!--nested--></span><!--last--></section></template>
            <script>
                var source=document.getElementById('source').content.firstChild;
                if('${operation}'==='foreign-import'){
                    var foreign=document.implementation.createHTMLDocument('foreign');
                    source=foreign.importNode(source,true);
                }
                var copy='${operation}'==='clone'?source.cloneNode(true):document.importNode(source,true);
                var checks={order:copy.childNodes.length===4&&copy.firstChild.nodeType===8&&
                    copy.firstChild.nextSibling.nodeType===3&&copy.lastChild.nodeType===8,
                    nested:copy.querySelector('span').firstChild.nodeType===8,
                    independent:copy.firstChild!==source.firstChild,
                    owner:copy.firstChild.ownerDocument===document,
                    shallow:source.cloneNode(false).childNodes.length===0};
                copy.firstChild.data='changed';
                checks.source=source.firstChild.data==='first';
                var text=document.createTextNode('inserted');copy.insertBefore(text,copy.lastChild);
                document.body.appendChild(copy);
                checks.inserted=copy.lastChild.previousSibling===text&&copy.lastChild.data==='last';
                var comment=document.createComment('standalone'),commentCopy=comment.cloneNode(false);
                var textCopy=text.cloneNode(false);
                checks.scalar=commentCopy.nodeType===8&&commentCopy.data==='standalone'&&textCopy.data==='inserted';
                console.log('CLONE_COMMENTS',JSON.stringify(checks));
                if(Object.values(checks).every(Boolean))document.getElementById('probe').style.background='lime';
            </script>`);
        const match = log.match(/CLONE_COMMENTS (\{[^\n]+\})/);
        assert(match, log.slice(-3000));
        assert.deepEqual(JSON.parse(match[1]), {order:true,nested:true,independent:true,
            owner:true,shallow:true,source:true,inserted:true,scalar:true});
        assert.deepEqual(rgb(image, 40, 40), [0, 255, 0]);
    });
}

for (const mode of ['template', 'element', 'adjacent']) {
    test('HTML fragments preserve top-level comment anchors: ' + mode, async () => {
        const {image, log} = await render('fragment-' + mode, `<!doctype html>
            <style>body{margin:0}#probe{width:80px;height:80px;background:red}</style><div id="probe"></div>
            <script>
                var host=document.createElement('${mode}'==='template'?'template':'div');
                var html='<!--before-->text<span><!--nested--></span><!--after-->';
                if('${mode}'==='adjacent')host.insertAdjacentHTML('beforeend',html);else host.innerHTML=html;
                var source='${mode}'==='template'?host.content:host;
                var copy=source.cloneNode(true);
                var checks={count:source.childNodes.length===4,first:source.firstChild.nodeType===8,
                    last:source.lastChild.nodeType===8,clone:copy.childNodes.length===4&&copy.firstChild.data==='before',
                    nested:copy.querySelector('span').firstChild.data==='nested'};
                console.log('FRAGMENT_COMMENTS',JSON.stringify(checks));
                if(Object.values(checks).every(Boolean))document.getElementById('probe').style.background='lime';
            </script>`);
        const match = log.match(/FRAGMENT_COMMENTS (\{[^\n]+\})/);
        assert(match, log.slice(-3000));
        assert.deepEqual(JSON.parse(match[1]), {count:true,first:true,last:true,clone:true,nested:true});
        assert.deepEqual(rgb(image, 40, 40), [0, 255, 0]);
    });
}

for (const mode of ['append', 'before', 'after', 'insertBefore']) {
    test('Fragment insertion retains mixed child order: ' + mode, async () => {
        const {image, log} = await render('move-' + mode, `<!doctype html>
            <style>body{margin:0}#probe{width:80px;height:80px;background:red}</style><div id="probe"></div>
            <script>
                var template=document.createElement('template');
                template.innerHTML='<!--a--><div id="a"></div><!--b--> <div id="b"></div><!--c-->';
                var fragment=template.content.cloneNode(true),parent=document.createElement('div');
                var anchor=document.createComment('anchor');parent.appendChild(anchor);
                var expected=Array.from(fragment.childNodes).map(node=>node.nodeType+':'+(node.id||node.data));
                if('${mode}'==='append')parent.appendChild(fragment);
                else if('${mode}'==='before')anchor.before(fragment);
                else if('${mode}'==='after')anchor.after(fragment);
                else parent.insertBefore(fragment,anchor);
                var actual=Array.from(parent.childNodes).filter(node=>node!==anchor).map(node=>node.nodeType+':'+(node.id||node.data));
                console.log('FRAGMENT_ORDER',JSON.stringify(actual));
                if(JSON.stringify(actual)===JSON.stringify(expected)&&fragment.childNodes.length===0)
                    document.getElementById('probe').style.background='lime';
            </script>`);
        const match = log.match(/FRAGMENT_ORDER (\[[^\n]+\])/);
        assert(match, log.slice(-3000));
        assert.deepEqual(JSON.parse(match[1]), ['8:a','1:a','8:b','3: ','1:b','8:c']);
        assert.deepEqual(rgb(image, 40, 40), [0, 255, 0]);
    });
}
