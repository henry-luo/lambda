const relationalMutationCases = ['unrelated', 'attributes', 'witness', 'positions', 'subtree', 'empty', 'siblings',
    'typed-is', 'typed-where-not', 'internal-siblings', 'leading-siblings', 'leading-wrapped', 'computed-size',
    'prepared-unrelated', 'prepared-witness', 'unrelated-class', 'unrelated-class-list',
    'class-witness', 'class-attribute-witness', 'quirks-class-witness', 'synthetic-class-witness',
    'token-class-witness', 'unrelated-leading-siblings', 'unrelated-leading-wrapped',
    'unrelated-leading-style', 'leading-general'];

function exerciseRelationalMutation(kind) {
    const failures = [], paint = document.getElementById('paint');
    const measure = document.getElementById('measure'), gate = document.getElementById('gate');
    function check(value, label) { if (!value) failures.push(label); }
    function blue(expected, label) {
        check(getComputedStyle(paint).backgroundColor === (expected ? 'rgb(0, 0, 255)' : 'rgb(255, 0, 0)'), label);
        paint.getBoundingClientRect();
    }
    try {
        blue(kind === 'positions' || kind === 'siblings', 'initial match');
        console.log('RELATIONAL_MUTATION_READY');
        if (kind.startsWith('unrelated-class') || kind.endsWith('class-witness') || kind === 'class-attribute-witness') {
            const unrelated = kind.startsWith('unrelated-class');
            const values = kind === 'token-class-witness' ? ['marker' + String.fromCharCode(12) + 'tail',
                'marker', 'marker' + String.fromCharCode(11) + 'tail', 'other'] :
                unrelated ? ['phase-one', 'phase-two', 'phase-three'] :
                ['marker', 'marker extra', 'other'];
            const target = kind === 'synthetic-class-witness' ? document.createElement('span') : measure;
            if (target !== measure) {
                target.className = 'stable';
                measure.appendChild(target);
            }
            const observer = new MutationObserver(function() {});
            observer.observe(target, {attributes:true, attributeFilter:['class'], attributeOldValue:true});
            for (const value of values) {
                const before = target.getAttribute('class');
                if (kind === 'unrelated-class-list') target.classList.add(value);
                else if (kind === 'class-attribute-witness') target.setAttribute('class', value);
                else if (kind === 'synthetic-class-witness') {
                    if (value === 'marker') target.className = value;
                    else if (value === 'marker extra') target.classList.add('extra');
                    else target.setAttributeNS(null, 'class', value);
                } else target.className = value;
                const records = observer.takeRecords();
                check(records.length === 1 && records[0].oldValue === before, 'old class value survives replacement');
                // a stable connected write must also preserve a following subtree insertion proof.
                if (unrelated) measure.appendChild(document.createElement('i'));
                blue(!unrelated && value !== 'other' && (kind !== 'token-class-witness' || value === 'marker'),
                    'class membership changes only actual witnesses');
            }
        } else if (kind.startsWith('prepared-')) {
            const script=document.createElement('script');
            script.type='application/json';script.setAttribute('data-probe','true');
            script.setAttribute('src','#inert');
            if(kind==='prepared-witness')script.className='marker';
            document.head.appendChild(script);
            blue(kind==='prepared-witness','prepared subtree supplies only its actual classes');
            script.remove();
            // a later attribute write must not hide the removed witness from the batch.
            if(kind==='prepared-witness')script.className='not-marker';
            blue(false,'removed witness remains visible to invalidation');
        } else if (kind.startsWith('unrelated-leading-')) {
            for (const size of [80, 120, 160]) {
                if (kind === 'unrelated-leading-style') {
                    measure.style.width = size + 'px';
                } else {
                    const transient = document.createElement('div');
                    document.body.appendChild(transient);
                    transient.getBoundingClientRect();
                    transient.remove();
                }
                check(document.getElementById('lead').getBoundingClientRect().width === 80,
                    'unrelated mutation preserves sibling anchor');
                measure.getBoundingClientRect();
            }
        } else if (kind === 'unrelated' || kind === 'attributes' || kind.startsWith('typed-')) {
            for (const text of ['first measurement', 'second measurement', 'third measurement']) {
                measure.textContent = text;
                measure.getBoundingClientRect();
                measure.firstChild.data = text + ' changed';
                measure.getBoundingClientRect();
            }
            blue(false, 'unrelated text retains ancestor match');
        } else if (kind === 'positions') {
            const spacer = document.createElement('i');
            gate.insertBefore(spacer, gate.firstChild); blue(false, 'existing first child loses match');
            spacer.remove(); blue(true, 'existing first child regains match');
        } else if (kind === 'subtree') {
            const subtree = document.createElement('section'); subtree.className = 'gate';
            subtree.innerHTML = '<b class="marker">marker</b>';
            measure.appendChild(subtree); blue(true, 'inserted descendant supplies match');
            subtree.remove(); blue(false, 'detached descendant removes match');
        } else if (kind === 'empty') {
            const marker = gate.firstChild;
            marker.firstChild.data = ''; blue(true, 'empty text changes ancestor match');
            marker.firstChild.data = 'restored'; blue(false, 'nonempty text restores ancestor match');
            measure.firstChild.data = '';
            check(getComputedStyle(measure).width === '90px' && measure.getBoundingClientRect().width === 106,
                'local empty style updates beside unrelated has');
            measure.firstChild.data = 'restored';
            check(getComputedStyle(measure).width === '70px' && measure.getBoundingClientRect().width === 70,
                'local nonempty style restores removed padding and border');
        } else if (kind === 'siblings') {
            const spacer = document.createElement('i');
            gate.insertBefore(spacer, gate.lastChild); blue(false, 'insertion breaks existing adjacency');
            spacer.remove(); blue(true, 'removal restores existing adjacency');
        } else if (kind === 'internal-siblings' || kind.startsWith('leading-')) {
            const internal = kind === 'internal-siblings';
            const target = document.getElementById(internal ? 'list' : 'lead');
            const parent = internal ? target.firstChild : gate;
            function width(expected, label) {
                check(getComputedStyle(target).width === expected + 'px' &&
                    target.getBoundingClientRect().width === expected, label);
            }
            width(80, 'initial sibling-dependent width');
            const marker = document.createElement(internal ? 'span' : 'b'); marker.className = 'marker';
            parent.appendChild(marker);
            width(120, 'sibling relationship gains match');
            marker.remove();
            width(80, 'sibling relationship loses match');
        } else if (kind === 'computed-size') {
            for (const mode of ['content-box', 'border-box', 'constrained']) {
                const box = document.createElement('div'), borderBox = mode === 'border-box';
                const width = mode === 'constrained' ? 70 : 90, height = mode === 'constrained' ? 30 : 50;
                box.style.cssText = 'width:90px;height:50px;padding:6px;border:2px solid;box-sizing:' +
                    (borderBox ? 'border-box' : 'content-box') +
                    (mode === 'constrained' ? ';max-width:70px;max-height:30px' : '');
                document.body.appendChild(box);
                const style = getComputedStyle(box), rect = box.getBoundingClientRect();
                check(style.width === width + 'px' && style.height === height + 'px', mode + ' CSS sizing box');
                check(rect.width === width + (borderBox ? 0 : 16) && rect.height === height + (borderBox ? 0 : 16),
                    mode + ' border geometry');
                box.style.transform = 'scale(2)';
                check(style.width === width + 'px' && style.height === height + 'px', mode + ' ignores visual transform');
                box.remove();
            }
        }
        if (kind === 'witness') {
            console.log('RELATIONAL_MUTATION_RELATED');
            const flag = document.createElement('b'); flag.setAttribute('data-flag', 'active');
            measure.appendChild(flag); blue(true, 'attribute-bearing inserted witness');
            flag.remove(); flag.setAttribute('data-flag', 'inactive');
            blue(false, 'removed witness changed again in the same batch');
        }
    } catch (error) { failures.push('unexpected: ' + error.name + ': ' + error.message); }
    console.log('RELATIONAL_MUTATION_RESULT', JSON.stringify({failures}));
    document.getElementById('probe').style.background = failures.length ? 'red' : 'green';
}

function relationalMutationHtml(kind) {
    const argument = kind === 'quirks-class-witness' ? ':is([class~="MARKER" i])' :
        kind === 'class-attribute-witness' ? ':where([class~="marker"])' :
        kind.startsWith('prepared-') || kind.includes('class') ? '.marker' : kind === 'attributes' || kind === 'witness' ? '[data-flag="active"]' : kind === 'siblings' ? '.before + .after' :
        '.gate > .marker:' + (kind === 'empty' ? 'empty' : 'first-child');
    const children = kind === 'internal-siblings' ? '<ul id="list"><li><span>text</span></li></ul>' :
        kind === 'positions' || kind === 'empty' ? '<b class="marker">text</b>' :
        kind === 'siblings' ? '<b class="before"></b><b class="after"></b>' : '';
    const typed = kind === 'typed-where-not' ? ':where(ul,ol):not([data-skip])' : ':is(ul,ol)';
    const leading = kind.startsWith('leading-') || kind.startsWith('unrelated-leading-');
    const relativeSibling = kind === 'leading-general' ? '~' : '+';
    const hasSibling = ':has(' + relativeSibling + ' .gate .marker)';
    const siblingRule = leading ? '.lead' +
        (kind.endsWith('wrapped') ? ':is(' + hasSibling + ')' : hasSibling) + '{width:120px}' :
        kind.startsWith('typed-') || kind === 'internal-siblings' ? '.richtext ' + typed + ':has(>li>span+span){width:120px}' : '';
    const primaryRule = kind === 'internal-siblings' || leading ? '' :
        (kind.startsWith('prepared-')?'html':'body')+':has(' + argument + ') #paint{background:blue}';
    return (kind.startsWith('quirks-') ? '' : '<!doctype html>') + '<style>body{margin:0}#probe,#paint{position:fixed;top:0;width:80px;height:80px}' +
        '#probe{left:0;background:red}#paint{left:100px;background:red}' +
        primaryRule + (kind === 'unrelated-leading-style' ? '.decorative:before{content:attr(data-note)}' : '') +
        '#measure{position:absolute;top:100px;width:70px;visibility:hidden}#measure:empty{width:90px;padding:6px;border:2px solid blue}' +
        'ul,ol,.lead{padding:0;margin:0;width:80px}' + siblingRule + '</style>' +
        '<div id="probe"></div><div id="paint"></div><div class="richtext"><div id="measure" class="stable">measurement</div>' +
        '<div id="lead" class="lead"></div><section id="gate" class="gate">' + children + '</section></div>' +
        '<script>(' + exerciseRelationalMutation.toString() + ')(' + JSON.stringify(kind) + ');</script>';
}

module.exports = {relationalMutationCases, relationalMutationHtml};
