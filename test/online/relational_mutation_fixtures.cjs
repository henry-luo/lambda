const relationalMutationCases = ['unrelated', 'attributes', 'witness', 'positions', 'subtree', 'empty', 'siblings',
    'typed-is', 'typed-where-not', 'internal-siblings', 'leading-siblings', 'leading-wrapped', 'computed-size'];

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
        if (kind === 'unrelated' || kind === 'attributes' || kind.startsWith('typed-')) {
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
    const argument = kind === 'attributes' || kind === 'witness' ? '[data-flag="active"]' : kind === 'siblings' ? '.before + .after' :
        '.gate > .marker:' + (kind === 'empty' ? 'empty' : 'first-child');
    const children = kind === 'internal-siblings' ? '<ul id="list"><li><span>text</span></li></ul>' :
        kind === 'positions' || kind === 'empty' ? '<b class="marker">text</b>' :
        kind === 'siblings' ? '<b class="before"></b><b class="after"></b>' : '';
    const typed = kind === 'typed-where-not' ? ':where(ul,ol):not([data-skip])' : ':is(ul,ol)';
    const siblingRule = kind.startsWith('leading-') ? '.lead' +
        (kind === 'leading-wrapped' ? ':is(:has(+ .gate .marker))' : ':has(+ .gate .marker)') + '{width:120px}' :
        kind.startsWith('typed-') || kind === 'internal-siblings' ? '.richtext ' + typed + ':has(>li>span+span){width:120px}' : '';
    const primaryRule = kind === 'internal-siblings' || kind.startsWith('leading-') ? '' :
        'body:has(' + argument + ') #paint{background:blue}';
    return '<!doctype html><style>body{margin:0}#probe,#paint{position:fixed;top:0;width:80px;height:80px}' +
        '#probe{left:0;background:red}#paint{left:100px;background:red}' +
        primaryRule +
        '#measure{position:absolute;top:100px;width:70px;visibility:hidden}#measure:empty{width:90px;padding:6px;border:2px solid blue}' +
        'ul,ol,.lead{padding:0;margin:0;width:80px}' + siblingRule + '</style>' +
        '<div id="probe"></div><div id="paint"></div><div class="richtext"><div id="measure">measurement</div>' +
        '<div id="lead" class="lead"></div><section id="gate" class="gate">' + children + '</section></div>' +
        '<script>(' + exerciseRelationalMutation.toString() + ')(' + JSON.stringify(kind) + ');</script>';
}

module.exports = {relationalMutationCases, relationalMutationHtml};
