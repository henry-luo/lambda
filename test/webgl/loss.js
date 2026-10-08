const result = document.getElementById('result');
const canvas = document.getElementById('canvas');
const gl = canvas.getContext('webgl2', { preserveDrawingBuffer: true });
const extension = gl.getExtension('WEBGL_lose_context');
const oldBuffer = gl.createBuffer();
gl.bindBuffer(gl.ARRAY_BUFFER, oldBuffer);
gl.bufferData(gl.ARRAY_BUFFER, 16, gl.STATIC_DRAW);
let lost = false;
let lostBuffer = null;
function check(value, message) { if (!value) throw new Error(message); }
function fail(error) { result.setAttribute('data-result', String(error.stack || error)); }
canvas.addEventListener('webglcontextlost', function (event) {
    try {
        check(event.cancelable && !event.bubbles, 'loss event flags');
        check(event instanceof WebGLContextEvent && event instanceof Event, 'loss event prototype');
        check(gl.isContextLost(), 'context is lost during event');
        check(gl.getError() === gl.CONTEXT_LOST_WEBGL, 'single loss error');
        check(gl.getError() === gl.NO_ERROR, 'loss error consumed');
        lostBuffer = gl.createBuffer();
        check(lostBuffer instanceof WebGLBuffer && !gl.isBuffer(lostBuffer), 'creation while lost produces an invalid wrapper');
        event.preventDefault();
        lost = true;
        setTimeout(function () { extension.restoreContext(); }, 0);
    } catch (error) { fail(error); }
});
canvas.addEventListener('webglcontextrestored', function (event) {
    try {
        check(lost && !gl.isContextLost(), 'restored after loss');
        check(event.cancelable && !event.bubbles && event.isTrusted, 'restored event flags');
        check(canvas.getContext('webgl2') === gl, 'context wrapper identity after restore');
        check(gl.isBuffer(oldBuffer) === false && !gl.isBuffer(lostBuffer), 'old and lost-created resources stay invalid');
        gl.bindBuffer(gl.ARRAY_BUFFER, lostBuffer);check(gl.getError() === gl.INVALID_OPERATION, 'lost-created resource cannot bind');
        gl.bindBuffer(gl.ARRAY_BUFFER, oldBuffer);
        check(gl.getError() === gl.INVALID_OPERATION, 'stale resource cannot bind');
        const fresh = gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER, fresh);
        gl.bufferData(gl.ARRAY_BUFFER, 32, gl.STATIC_DRAW);
        check(gl.getBufferParameter(gl.ARRAY_BUFFER, gl.BUFFER_SIZE) === 32, 'new resource usable');
        gl.clearColor(0,0,1,1);gl.clear(gl.COLOR_BUFFER_BIT);
        result.setAttribute('data-result', 'passed');
    } catch (error) { fail(error); }
});
try { check(extension !== null, 'loss extension');extension.loseContext();check(gl.isContextLost(), 'immediate loss state'); } catch (error) { fail(error); }
