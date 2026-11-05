// Test-side adapter for the Lambda demo's native physics request events.
globalThis.phWorld = () => document.getElementById('world');
globalThis.phState = () => JSON.parse(phWorld().getAttribute('data-physics-state'));
globalThis.phBins = () => JSON.parse(phWorld().getAttribute('data-bins'));
globalThis.phCount = () => phBins().reduce((total, bin) => total + bin.length, 0);
globalThis.phRequest = request => {
    document.getElementById('physics-demo').dispatchEvent(new CustomEvent('physicsrequest', {detail: request}));
    return request.operation === 'query' ? JSON.parse(phWorld().getAttribute('data-query')) : phState();
};
globalThis.phAction = (id, resume = true) => {
    const button = document.getElementById(id);
    if (button.getAttribute('data-restart') === 'true') phRequest({operation: 'reset'});
    phRequest(JSON.parse(button.getAttribute('data-request')));
    if (resume && button.getAttribute('data-play-after') === 'true') phRequest({operation: 'play'});
};
