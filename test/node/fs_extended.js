// fs extended sync API tests
import fs from 'fs';

// fs.constants
console.log(fs.constants.F_OK === 0);
console.log(fs.constants.R_OK === 4);
console.log(fs.constants.W_OK === 2);
console.log(fs.constants.X_OK === 1);

// fs.copyFileSync + realpathSync
fs.writeFileSync('./temp/fs_test_src.txt', 'copy me');
fs.copyFileSync('./temp/fs_test_src.txt', './temp/fs_test_dst.txt');
console.log(fs.readFileSync('./temp/fs_test_dst.txt', 'utf8'));

// fs.accessSync - should not throw for existing file
try {
    fs.accessSync('./temp/fs_test_src.txt');
    console.log('access ok');
} catch(e) {
    console.log('access failed');
}

// fs.chmodSync
fs.chmodSync('./temp/fs_test_src.txt', 0o644);
console.log('chmod ok');

// fs.lstatSync
var stat = fs.lstatSync('./temp/fs_test_src.txt');
console.log(stat.isFile);
console.log(stat.isDirectory);

// fs.realpathSync
var rp = fs.realpathSync('./temp/fs_test_src.txt');
console.log(typeof rp === 'string');

// fs.rmSync - single file
fs.rmSync('./temp/fs_test_src.txt');
console.log(fs.existsSync('./temp/fs_test_src.txt'));

// fs.rmSync - recursive directory
fs.mkdirSync('./temp/fs_rm_test');
fs.writeFileSync('./temp/fs_rm_test/file.txt', 'data');
fs.rmSync('./temp/fs_rm_test', { recursive: true });
console.log(fs.existsSync('./temp/fs_rm_test'));

// cleanup
fs.unlinkSync('./temp/fs_test_dst.txt');
console.log('done');
