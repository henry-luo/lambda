import fs from 'fs';

const path = './temp/number_model_bigint_fs_position_range.txt';
fs.writeFileSync(path, 'abc');

const fd = fs.openSync(path, 'r');
const buf = Buffer.alloc(1);

function report(label, fn) {
    try {
        fn();
        console.log(label + ': ok');
    } catch (e) {
        console.log(label + ': ' + e.name + ' ' + e.code);
    }
}

try {
    report('readSync -1n', function() {
        fs.readSync(fd, buf, 0, 1, -1n);
    });
    report('readSync -2n', function() {
        fs.readSync(fd, buf, 0, 1, -2n);
    });
    report('readSync too large', function() {
        fs.readSync(fd, buf, 0, 1, 1n << 63n);
    });
    report('readvSync too large', function() {
        fs.readvSync(fd, [buf], 1n << 63n);
    });
} finally {
    fs.closeSync(fd);
    fs.unlinkSync(path);
}
