// Phase II API gate: context modes, branded resources, buffer views, GLSL and actual indexed pixels.
const result = document.getElementById('result');
let checks = 0;
function check(condition, message) { if (!condition) throw new Error(message); checks++; }
function error(gl, expected, message) { check(gl.getError() === expected, message); }
try {
    const canvas = document.getElementById('first');
    const gl = canvas.getContext('webgl2', { preserveDrawingBuffer: true, antialias: true });
    check(gl !== null, 'native WebGL2 context');
    check(gl === canvas.getContext('webgl2'), 'context identity');
    check(canvas.getContext('2d') === null, 'exclusive context mode');
    check(gl.canvas === canvas, 'canvas identity');
    check(gl instanceof WebGL2RenderingContext, 'context brand and prototype');
    check(WebGLContextEvent.length === 1, 'context event required constructor arity');
    check(new WebGLContextEvent('custom', { statusMessage: 'ready' }).statusMessage === 'ready', 'context event initialization');
    check(gl.texImage3D.length === 10, 'WebIDL method arity');
    check(gl.getContextAttributes().preserveDrawingBuffer === true, 'context options');
    check(gl.drawingBufferWidth === 128 && gl.drawingBufferHeight === 128, 'bitmap dimensions');
    const other = document.getElementById('second').getContext('webgl2', { antialias: false });
    check(other !== gl, 'multiple canvas contexts');
    other.clearColor(0, 1, 0, 1); other.clear(other.COLOR_BUFFER_BIT);
    const buffer = gl.createBuffer();
    check(buffer instanceof WebGLBuffer && !gl.isBuffer(buffer), 'buffer becomes an object after binding');
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    check(gl.getParameter(gl.ARRAY_BUFFER_BINDING) === buffer, 'binding wrapper identity');
    check(gl.isBuffer(buffer), 'live bound buffer');
    const bytes = new Uint8Array([90, 91, 1, 2, 3, 4, 5, 6, 92, 93]);
    gl.bufferData(gl.ARRAY_BUFFER, bytes.subarray(2, 8), gl.STATIC_DRAW);
    check(gl.getBufferParameter(gl.ARRAY_BUFFER, gl.BUFFER_SIZE) === 6, 'upload view byte range');
    gl.bufferData(gl.ARRAY_BUFFER, bytes, gl.STATIC_DRAW, 3, 2);
    check(gl.getBufferParameter(gl.ARRAY_BUFFER, gl.BUFFER_SIZE) === 2, 'WebGL2 upload element range');
    gl.bufferSubData(gl.ARRAY_BUFFER, 2, new Uint8Array([1]));
    error(gl, gl.INVALID_VALUE, 'subdata range validation');
    other.bindBuffer(other.ARRAY_BUFFER, buffer);
    error(other, other.INVALID_OPERATION, 'cross-context resource rejection');
    check(other.isBuffer(buffer) === false, 'cross-context identity query is false');
    error(other, other.NO_ERROR, 'cross-context identity query has no error');
    let threw = false;
    try { gl.bindBuffer(gl.ARRAY_BUFFER, {}); } catch (e) { threw = e instanceof TypeError; }
    check(threw, 'forged resource rejection');
    check(gl.getParameter(0xdead) === null, 'invalid query returns null');
    error(gl, gl.INVALID_ENUM, 'invalid query error');
    gl.enable(0x8db9);error(gl, gl.INVALID_ENUM, 'desktop-only capability rejected');
    check(gl.getParameter(gl.READ_BUFFER) === gl.BACK && gl.getParameter(gl.DRAW_BUFFER0) === gl.BACK, 'default buffer selection');
    check(gl.getParameter(gl.COMPRESSED_TEXTURE_FORMATS) instanceof Uint32Array, 'compressed-format result type');
    const texture = gl.createTexture();gl.bindTexture(gl.TEXTURE_2D, texture);
    const unpack = gl.createBuffer();gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, unpack);gl.bufferData(gl.PIXEL_UNPACK_BUFFER, 4, gl.STATIC_DRAW);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array(4));
    error(gl, gl.INVALID_OPERATION, 'client upload cannot be interpreted as PBO offset');
    gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, null);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Float32Array(4));
    error(gl, gl.INVALID_OPERATION, 'upload view element type validation');
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array(3));
    error(gl, gl.INVALID_OPERATION, 'short texture data validation');
    let badData = false;try { gl.readPixels(0,0,1,1,gl.RGBA,gl.UNSIGNED_BYTE,'abcd'); } catch (e) { badData = e instanceof TypeError; }
    // browsers also select the PBO-offset overload; neither path may write into a string.
    check(badData || gl.getError() === gl.INVALID_OPERATION, 'readback requires a writable view');
    const pack = gl.createBuffer();gl.bindBuffer(gl.PIXEL_PACK_BUFFER, pack);gl.bufferData(gl.PIXEL_PACK_BUFFER, 4, gl.STATIC_READ);
    gl.readPixels(0,0,1,1,gl.RGBA,gl.UNSIGNED_BYTE,new Uint8Array(4));error(gl, gl.INVALID_OPERATION, 'client readback cannot be interpreted as PBO offset');
    gl.bindBuffer(gl.PIXEL_PACK_BUFFER, null);
    gl.deleteBuffer(pack);gl.deleteBuffer(unpack);gl.deleteTexture(texture);
    const vertexSource = '#version 300 es\nprecision highp float;\nlayout(location=0) in vec2 position;\nvoid main(){gl_Position=vec4(position,0.0,1.0);}';
    const fragmentSource = '#version 300 es\nprecision mediump float;\nuniform vec4 tint;out vec4 color;\nvoid main(){color=tint;}';
    function shader(type, source) {
        const object = gl.createShader(type); gl.shaderSource(object, source); gl.compileShader(object);
        check(gl.getShaderSource(object) === source, 'original shader source');
        check(gl.getShaderParameter(object, gl.COMPILE_STATUS), gl.getShaderInfoLog(object)); return object;
    }
    const vertex = shader(gl.VERTEX_SHADER, vertexSource);
    const fragment = shader(gl.FRAGMENT_SHADER, fragmentSource);
    const program = gl.createProgram(); gl.attachShader(program, vertex); gl.attachShader(program, fragment); gl.linkProgram(program);
    check(gl.getProgramParameter(program, gl.LINK_STATUS), gl.getProgramInfoLog(program));
    check(gl.getProgramParameter(program, gl.ACTIVE_UNIFORMS) === 1, 'uniform reflection count');
    check(gl.getActiveUniform(program, 0).name === 'tint', 'uniform reflection name');
    check(gl.getAttribLocation(program, 'position') === 0, 'attribute reflection');
    const location = gl.getUniformLocation(program, 'tint');
    check(location instanceof WebGLUniformLocation, 'uniform location brand');
    gl.useProgram(program); gl.uniform4fv(location, new Float32Array([9, 1, 0, 0, 1, 9]), 1, 4);
    const vertices = gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER, vertices);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1,-1, 1,-1, 0,1]), gl.STATIC_DRAW);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);gl.enableVertexAttribArray(0);
    const indices = gl.createBuffer();gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indices);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array([0,1,2]), gl.STATIC_DRAW);
    gl.clearColor(0,0,0,1);gl.clear(gl.COLOR_BUFFER_BIT);
    gl.drawElements(gl.TRIANGLES, 3, gl.UNSIGNED_SHORT, 0);
    const pixel = new Uint8Array(4);gl.readPixels(64,64,1,1,gl.RGBA,gl.UNSIGNED_BYTE,pixel);
    check(pixel[0] === 255 && pixel[1] === 0 && pixel[2] === 0 && pixel[3] === 255, 'indexed triangle readback');
    error(gl, gl.NO_ERROR, 'valid draw has no errors');
    const outside = new Uint8Array([9,9,9,9]);gl.readPixels(-1,-1,1,1,gl.RGBA,gl.UNSIGNED_BYTE,outside);
    check(outside[0] === 9 && outside[3] === 9, 'out-of-bounds destination remains untouched');
    const broken = gl.createShader(gl.FRAGMENT_SHADER);gl.shaderSource(broken, '#version 300 es\nvoid main(){invalid_token;}');gl.compileShader(broken);
    check(!gl.getShaderParameter(broken,gl.COMPILE_STATUS) && gl.getShaderInfoLog(broken).length > 0, 'driver compile failure diagnostic');gl.deleteShader(broken);
    gl.drawElements(gl.TRIANGLES, 4, gl.UNSIGNED_SHORT, 0);
    error(gl, gl.INVALID_OPERATION, 'index range validation');
    gl.linkProgram(program);gl.uniform4f(location,0,1,0,1);
    error(gl, gl.INVALID_OPERATION, 'uniform invalid after relink');
    gl.deleteBuffer(buffer);check(!gl.isBuffer(buffer), 'deleted buffer');
    canvas.width = 64;check(gl === canvas.getContext('webgl2'), 'context survives resize');
    check(gl.drawingBufferWidth === 64, 'resized bitmap width');
    gl.clearColor(1,0,0,1);gl.clear(gl.COLOR_BUFFER_BIT);
    result.setAttribute('data-result','passed');result.setAttribute('data-checks', String(checks));
} catch (e) {
    result.setAttribute('data-result',String(e.stack || e));console.error('WebGL API gate:', e.stack || e);
}
