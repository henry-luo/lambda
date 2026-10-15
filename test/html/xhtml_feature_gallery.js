// D2.10.1: inspect namespace and name identity without reparsing the source as HTML.
(function () {
    var html_ns = 'http://www.w3.org/1999/xhtml';
    var svg_ns = 'http://www.w3.org/2000/svg';
    var math_ns = 'http://www.w3.org/1998/Math/MathML';
    var probe_ns = 'urn:lambda:xhtml:probe';
    var clicks = 0;

    function element(id) { return document.getElementById(id); }
    function in_namespace(id, uri, local) {
        var node = element(id);
        return !!node && node.namespaceURI === uri && node.localName === local;
    }

    function run_checks() {
        var checks = [];
        function check(label, test) {
            var passed = false;
            var detail = '';
            try { passed = !!test(); }
            catch (error) { detail = ': ' + error.message; }
            checks.push({label: label + detail, passed: passed});
        }

        check('Document root is XHTML html', function () {
            return document.documentElement.localName === 'html' && document.documentElement.namespaceURI === html_ns;
        });
        check('Standards mode is active', function () { return document.compatMode === 'CSS1Compat'; });
        check('Default namespace creates HTML sections', function () { return in_namespace('syntax', html_ns, 'section'); });
        check('h:section emits an HTML section name', function () { return in_namespace('namespaces', html_ns, 'section'); });
        check('A scoped prefix resolves to XHTML', function () { return in_namespace('scoped-prefix', html_ns, 'span'); });
        check('Ordinary attributes retain names and values', function () {
            return element('prefixed-child').getAttribute('class') === 'sample-inline' &&
                element('prefixed-child').getAttribute('style') === 'font-weight:700';
        });
        check('Qualified attributes retain their namespace', function () {
            return element('prefixed-child').getAttributeNS(probe_ns, 'flag') === 'preserved';
        });
        check('Custom XML elements retain their namespace', function () { return in_namespace('custom-namespace', probe_ns, 'note'); });
        check('xmlns="" clears the element namespace', function () { return in_namespace('no-namespace', null, 'div'); });
        check('A prefixed child re-enters XHTML', function () { return in_namespace('namespace-reentry', html_ns, 'span'); });
        check('XML preserves uppercase element names', function () { return in_namespace('case-upper', html_ns, 'DIV'); });
        check('CSS type selectors respect XML case sensitivity', function () {
            var upper_match = element('case-upper').matches('DIV');
            var lower_match = element('case-lower').matches('DIV');
            var upper_border = getComputedStyle(element('case-upper')).borderLeftWidth;
            var lower_border = getComputedStyle(element('case-lower')).borderLeftWidth;
            if (!upper_match || lower_match || upper_border !== '4px' || lower_border !== '0px') {
                throw new Error('matches: ' + upper_match + '/' + lower_match + '; borders: ' + upper_border + '/' + lower_border);
            }
            return true;
        });
        check('XML attribute lookup is case-sensitive', function () {
            return element('case-upper').getAttribute('CaseAttr') === 'Preserved' && element('case-upper').getAttribute('caseattr') === null;
        });
        check('The five predefined XML entities decode', function () {
            return element('entities').textContent === '5 < 7 & 9 > 3; "quoted" \'single\'';
        });
        check('Decimal and hexadecimal references decode', function () { return element('numeric-entities').textContent === 'A\u00a0B 🌱'; });
        check('CDATA stays literal text', function () {
            return element('cdata-text').textContent === '<strong>CDATA is text & not markup</strong>' &&
                element('cdata-text').querySelector('strong') === null;
        });
        check('Preformatted indentation survives parsing', function () { return element('preserved-space').textContent === 'alpha\n  beta\n    gamma'; });
        check('Self-closing non-void elements stay empty', function () {
            return element('empty-node').textContent === '' && element('empty-node').nextElementSibling.id === 'empty-next';
        });
        check('Inline JavaScript CDATA executes', function () { return window.xhtmlInlineReady === true; });
        check('Prefixed SVG resolves to the SVG namespace', function () { return in_namespace('svg-example', svg_ns, 'svg'); });
        check('SVG names and attributes preserve case', function () {
            return in_namespace('gallery-gradient', svg_ns, 'linearGradient') && element('svg-example').getAttribute('viewBox') === '0 0 380 190';
        });
        check('xlink:href retains expanded attribute identity', function () {
            return element('svg-use').getAttributeNS('http://www.w3.org/1999/xlink', 'href') === '#gallery-symbol';
        });
        check('foreignObject contains HTML in XHTML namespace', function () {
            return in_namespace('foreign-object', svg_ns, 'foreignObject') && in_namespace('embedded-html', html_ns, 'div');
        });
        check('Inline and display MathML retain their namespace', function () {
            return in_namespace('inline-math', math_ns, 'math') && in_namespace('display-math', math_ns, 'math');
        });
        check('The linked stylesheet participates in layout', function () {
            return Math.round(element('external-style-probe').getBoundingClientRect().width) === 128;
        });
        check('The inline CDATA stylesheet participates in CSS', function () {
            return getComputedStyle(element('inline-style-probe')).borderBottomWidth === '3px';
        });
        check('Dynamic elements use the requested XHTML namespace', function () {
            var node = element('dynamic-probe');
            if (!node) {
                node = document.createElementNS(html_ns, 'span');
                node.id = 'dynamic-probe';
                node.textContent = 'Created with createElementNS.';
                element('dynamic-holder').appendChild(node);
            }
            return node.namespaceURI === html_ns && node.localName === 'span';
        });

        var list = element('check-list');
        list.textContent = '';
        var passed = 0;
        var failures = [];
        for (var i = 0; i < checks.length; i++) {
            var item = document.createElementNS(html_ns, 'li');
            item.className = checks[i].passed ? 'check-pass' : 'check-fail';
            item.textContent = (checks[i].passed ? 'PASS — ' : 'FAIL — ') + checks[i].label;
            list.appendChild(item);
            if (checks[i].passed) passed++;
            else failures.push(checks[i].label);
        }
        var summary = element('check-summary');
        summary.textContent = passed + ' / ' + checks.length + ' checks passed';
        summary.setAttribute('data-failures', String(checks.length - passed));
        summary.setAttribute('data-failed-checks', failures.join('; '));
    }

    element('rerun-checks').addEventListener('click', run_checks);
    element('counter-button').addEventListener('click', function () {
        clicks++;
        element('counter-value').textContent = String(clicks);
    });
    element('read-form').addEventListener('click', function () {
        element('form-result').textContent = element('reader-name').value + ' · ' + element('reader-format').value +
            ' · Updates ' + (element('reader-updates').checked ? 'on' : 'off');
    });
    // include stylesheet/layout checks after page resources and the first frame are ready.
    window.addEventListener('load', function () { requestAnimationFrame(run_checks); });
})();
