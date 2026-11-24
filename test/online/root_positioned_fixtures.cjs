const rootPositionedCases = ['static', 'sync-insert', 'timer-insert', 'timer-grow',
    'timer-shrink', 'fixed-child', 'unpositioned-root', 'definite-root',
    'removed-child', 'percent-child'];

function rootPositionedHtml(mode) {
    const insert = mode === 'sync-insert' || mode === 'timer-insert';
    const small = mode === 'timer-shrink';
    const staticContent = mode === 'static';
    const initialHeight = insert ? 'auto' : small || staticContent ? '1200px' : '0px';
    const rootStyle = mode === 'unpositioned-root' ? '' : 'position:relative;';
    const rootHeight = mode === 'definite-root' ? 'height:800px;' : '';
    const childStyle = mode === 'fixed-child' ? 'fixed' : 'absolute';
    const footerHeight = mode === 'percent-child' ? '20%' : '100px';
    const mutation = insert
        ? "var child=document.createElement('div');child.style='height:1200px;background:blue';main.appendChild(child);"
        : `main.style.height='${small ? 200 : 1200}px';`;
    const remove = mode === 'removed-child' ? "document.getElementById('removed').remove();" : '';
    const script = staticContent ? '' : `<script>
        var main=document.getElementById('main');
        document.documentElement.getBoundingClientRect();
        ${mode === 'sync-insert' ? mutation : 'setTimeout(function(){' + remove + mutation + '},0);'}
    </script>`;
    return `<!doctype html><style>
        html{${rootStyle}${rootHeight}min-height:100%}
        body{margin:0 0 100px}#main{background:blue}
        footer{position:${childStyle};bottom:0;height:${footerHeight};width:100%;background:green}
        #removed{position:absolute;bottom:100px;height:30px;width:100%;background:red}
    </style><main id=main style="height:${initialHeight}"></main>
    ${mode === 'removed-child' ? '<aside id=removed></aside>' : ''}<footer></footer>${script}`;
}

function rootPositionedExpected(mode) {
    return ['timer-shrink', 'fixed-child', 'unpositioned-root', 'definite-root'].includes(mode)
        ? [0, 128, 0] : [0, 0, 255];
}

module.exports = {rootPositionedCases, rootPositionedHtml, rootPositionedExpected};
