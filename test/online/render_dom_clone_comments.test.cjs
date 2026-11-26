const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb, loggedResult} = require('./render_fixture.cjs');
const render = renderFixtures('dom-clone-comments');

test('DOM queries stop at parsed template content boundaries', async () => {
    const {log} = await render('template-query-boundaries', `<!doctype html>
        <main><template id="outer"><section id="in-outer"><template id="inner">
            <b id="in-inner"></b></template></section></template></main>
        <script>
            var main=document.querySelector('main'),outer=document.getElementById('outer');
            var checks={
                document:document.querySelectorAll('template').length===1&&
                    document.querySelector('#in-outer')===null&&document.getElementById('in-inner')===null,
                element:main.querySelectorAll('template').length===1&&outer.querySelector('section')===null
            };
            var fragment=outer.content,inner=fragment.querySelector('template');
            checks.fragment=fragment.querySelectorAll('template').length===1&&
                fragment.getElementById('in-outer')===fragment.querySelector('section')&&
                fragment.getElementById('in-inner')===null&&fragment.querySelector('b')===null;
            checks.inner=inner.id==='inner'&&inner.content.getElementById('in-inner')===inner.content.querySelector('b');
            console.log('TEMPLATE_QUERY',JSON.stringify(checks));
        </script>`);
    const match = log.match(/TEMPLATE_QUERY (\{[^\n]+\})/);
    assert(match, log.slice(-3000));
    assert.deepEqual(JSON.parse(match[1]), {document:true,element:true,fragment:true,inner:true});
});

test('HTML template markup replaces the existing content fragment', async () => {
    const {log} = await render('template-inner-html', `<!doctype html><body>
        <template id="parsed"><!--anchor-->Text<b>Parsed</b></template>
        <script>
            const template = document.createElement('template');
            const content = template.content;
            template.innerHTML = '<!--before--><span>Initial label</span>';
            const old = content.lastChild;
            const checks = {
                initial: content.firstChild?.nodeType === 8 && old?.textContent === 'Initial label' &&
                    template.childNodes.length === 0
            };
            const markup = '<strong>Updated label</strong><template><i>Detail</i></template>';
            template.innerHTML = markup;
            const nested = content.lastChild?.content;
            const copy = template.cloneNode(true);
            const foreign = document.implementation.createHTMLDocument('');
            const imported = foreign.importNode(template, true);
            checks.replace = content === template.content && content.childNodes.length === 2 &&
                content.firstChild?.textContent === 'Updated label' && old?.parentNode === null;
            checks.nested = nested?.firstChild?.textContent === 'Detail';
            checks.serialize = template.innerHTML === markup &&
                template.outerHTML === '<template>' + markup + '</template>' &&
                new XMLSerializer().serializeToString(template).endsWith('>' + markup + '</template>');
            checks.clone = copy.childNodes.length === 0 && copy.content !== content &&
                copy.content.lastChild?.content?.firstChild?.textContent === 'Detail' &&
                imported.innerHTML === markup && template.cloneNode(false).innerHTML === '';
            try { content.appendChild(template); }
            catch (error) { checks.cycle = error.name === 'HierarchyRequestError'; }
            try {
                const range = document.createRange();
                range.setStart(content, 0);
                range.insertNode(template);
            } catch (error) { checks.rangeCycle = error.name === 'HierarchyRequestError'; }
            const copyContent = copy.content;
            foreign.adoptNode(copy);
            copy.innerHTML = '<b>Adopted</b>';
            checks.adopt = copy.ownerDocument === foreign && copy.content === copyContent &&
                copyContent.firstChild?.textContent === 'Adopted';
            document.body.appendChild(template);
            template.setHTMLUnsafe('<section id="templateInertProbe"><em>Unsafe markup</em></section>');
            checks.unsafeMarkup = content.firstChild?.localName === 'section' &&
                content.querySelector('em')?.textContent === 'Unsafe markup' && !content.isConnected &&
                content.parentNode === null && window.templateInertProbe === undefined;
            template.innerHTML = '';
            checks.empty = content === template.content && content.childNodes.length === 0 &&
                template.innerHTML === '';
            const parsed = document.getElementById('parsed');
            const initial = parsed.innerHTML;
            checks.parsed = parsed.content.childNodes.length === 3 && parsed.innerHTML === initial &&
                initial === '<!--anchor-->Text<b>Parsed</b>';
            console.log('TEMPLATE_MARKUP', JSON.stringify(checks));
        </script>`);
    assert.deepEqual(loggedResult(log, 'TEMPLATE_MARKUP'), {
        initial: true, replace: true, nested: true, serialize: true, clone: true,
        cycle: true, rangeCycle: true, adopt: true, unsafeMarkup: true, empty: true, parsed: true
    });
});

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
