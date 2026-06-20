'use strict';

const { internalBinding } = require('internal/test/binding');
const publicConstants = require('constants');

function keys(obj) {
    return Object.keys(obj).sort().join(',');
}

function nullProto(obj) {
    return Object.getPrototypeOf(obj) === null;
}

const constants = internalBinding('constants');
const processConstants = process.binding('constants');
const uv = internalBinding('uv');
const processUv = process.binding('uv');

console.log('top:', keys(constants));
console.log('os:', keys(constants.os));
console.log('null roots:', [
    constants,
    constants.crypto,
    constants.fs,
    constants.internal,
    constants.os,
    constants.os.dlopen,
    constants.os.errno,
    constants.os.priority,
    constants.os.signals,
    constants.trace,
    constants.zlib,
].every(nullProto));
console.log('fs dirent type:', typeof constants.fs.UV_DIRENT_UNKNOWN);
console.log('errno type:', typeof constants.os.errno.ENOENT);
console.log('signal type:', typeof constants.os.signals.SIGTERM);
console.log('process constants:', keys(processConstants));
console.log('public frozen:', Object.isFrozen(publicConstants));
console.log('public fs type:', typeof publicConstants.UV_DIRENT_UNKNOWN);
console.log('public errno match:', publicConstants.ENOENT === constants.os.errno.ENOENT);
console.log('public signal match:', publicConstants.SIGTERM === constants.os.signals.SIGTERM);

const enoent = uv.UV_ENOENT;
let uvAssignThrew = false;
try {
    uv.UV_ENOENT = 1;
} catch (err) {
    uvAssignThrew = String(err).indexOf('TypeError') >= 0;
}
console.log('uv readonly:', uvAssignThrew && uv.UV_ENOENT === enoent);
console.log('uv errname:', uv.errname(uv.UV_ENOENT));
console.log('process uv errname:', typeof processUv.errname);
