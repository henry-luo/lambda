// Copyright (c) 2019 The Khronos Group Inc. MIT-style license: LICENSE.Khronos.txt.
// Assertion ports from the pinned tests named in conformance-manifest.json; not the complete upstream harness.
const result = document.getElementById('result');
let checks = 0;
function check(value, message) { if (!value) throw new Error(message);checks++; }
try {
    const gl = document.getElementById('canvas').getContext('webgl2');
    check(gl !== null, 'WebGL2 exists');
    function error(code, message) { check(gl.getError() === code, message); }
    const cases = [Uint8Array, Int8Array, Int16Array, Uint16Array, Uint32Array, Int32Array, Float32Array, Float64Array];
    for (const Type of cases) {
        const buffer = gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
        const view = new Type([0,1,2,3]);
        for (const range of [[0,5],[5,0],[3,2]]) {
            gl.bufferData(gl.ARRAY_BUFFER, view, gl.STATIC_DRAW, range[0], range[1]);error(gl.INVALID_VALUE, 'bufferData source range');
        }
        gl.bufferData(gl.ARRAY_BUFFER, view, gl.STATIC_DRAW, 2);
        const copied = new Type(2);gl.getBufferSubData(gl.ARRAY_BUFFER, 0, copied);
        check(copied[0] === 2 && copied[1] === 3, 'bufferData element offset');
        gl.bufferData(gl.ARRAY_BUFFER, 8 * Type.BYTES_PER_ELEMENT, gl.STATIC_DRAW);
        for (const range of [[0,5],[5,0],[3,2]]) {
            gl.bufferSubData(gl.ARRAY_BUFFER, 0, view, range[0], range[1]);error(gl.INVALID_VALUE, 'bufferSubData source range');
        }
        gl.bufferSubData(gl.ARRAY_BUFFER, 2 * Type.BYTES_PER_ELEMENT, view, 1, 1);
        const selected = new Type([9,9,9]);gl.getBufferSubData(gl.ARRAY_BUFFER, 2 * Type.BYTES_PER_ELEMENT, selected, 1, 1);
        check(selected[0] === 9 && selected[1] === 1 && selected[2] === 9, 'subdata and readback element ranges');
        gl.bufferSubData(gl.ARRAY_BUFFER, 0, view, 4);error(gl.NO_ERROR, 'zero-length source succeeds');
        gl.deleteBuffer(buffer);check(!gl.isBuffer(buffer), 'deleted buffer');
    }
    const resources = [
        ['Buffer','ARRAY_BUFFER'], ['Framebuffer','FRAMEBUFFER'], ['Renderbuffer','RENDERBUFFER'], ['Texture','TEXTURE_2D']
    ];
    for (const [kind,target] of resources) {
        const object = gl['create'+kind]();check(!gl['is'+kind](object), 'unbound '+kind);
        gl['bind'+kind](gl[target],object);check(gl['is'+kind](object), 'bound '+kind);
        gl['delete'+kind](object);check(!gl['is'+kind](object), 'deleted '+kind);
    }
    const program=gl.createProgram();check(gl.isProgram(program), 'program is immediately live');gl.deleteProgram(program);check(!gl.isProgram(program), 'deleted program');
    const shader=gl.createShader(gl.VERTEX_SHADER);check(gl.isShader(shader), 'shader is immediately live');gl.deleteShader(shader);check(!gl.isShader(shader), 'deleted shader');
    const texture=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,texture);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL,true);gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL,true);
    gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA8,1,2,0,gl.RGBA,gl.UNSIGNED_BYTE,new Uint8Array([255,0,0,128, 0,0,255,255]));
    const framebuffer=gl.createFramebuffer();gl.bindFramebuffer(gl.FRAMEBUFFER,framebuffer);gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,texture,0);
    check(gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE, 'texture framebuffer');
    const pixels=new Uint8Array(8);gl.readPixels(0,0,1,2,gl.RGBA,gl.UNSIGNED_BYTE,pixels);
    check(pixels[0] === 0 && pixels[2] === 255 && pixels[4] === 128 && pixels[7] === 128, 'flip and premultiply typed upload');
    gl.bindFramebuffer(gl.FRAMEBUFFER,null);gl.deleteFramebuffer(framebuffer);gl.deleteTexture(texture);
    error(gl.NO_ERROR,'selected conformance has no residual errors');
    result.setAttribute('data-result','passed');result.setAttribute('data-checks',String(checks));
} catch (e) { result.setAttribute('data-result',String(e.stack || e));console.error(e.stack || e); }
