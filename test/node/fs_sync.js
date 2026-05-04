// fs basic tests — core sync operations + fd operations
var fs = require('node:fs');

// writeFileSync + readFileSync
fs.writeFileSync('./temp/fs_basic_test.txt', 'hello fs');
var content = fs.readFileSync('./temp/fs_basic_test.txt', 'utf8');
console.log('readFileSync:', content);

// existsSync
console.log('exists true:', fs.existsSync('./temp/fs_basic_test.txt'));
console.log('exists false:', fs.existsSync('./temp/nonexistent_xyz.txt'));

// appendFileSync
fs.appendFileSync('./temp/fs_basic_test.txt', ' appended');
var content2 = fs.readFileSync('./temp/fs_basic_test.txt', 'utf8');
console.log('appendFileSync:', content2);

// statSync
var stat = fs.statSync('./temp/fs_basic_test.txt');
console.log('stat isFile:', stat.isFile());
console.log('stat isDirectory:', stat.isDirectory());
console.log('stat size > 0:', stat.size > 0);

// mkdirSync + readdirSync
fs.mkdirSync('./temp/fs_basic_dir');
fs.writeFileSync('./temp/fs_basic_dir/a.txt', 'a');
fs.writeFileSync('./temp/fs_basic_dir/b.txt', 'b');
var entries = fs.readdirSync('./temp/fs_basic_dir');
console.log('readdirSync count:', entries.length);

// renameSync
fs.renameSync('./temp/fs_basic_dir/a.txt', './temp/fs_basic_dir/c.txt');
console.log('rename exists c:', fs.existsSync('./temp/fs_basic_dir/c.txt'));
console.log('rename gone a:', fs.existsSync('./temp/fs_basic_dir/a.txt'));

// truncateSync
fs.writeFileSync('./temp/fs_trunc.txt', 'long content here');
fs.truncateSync('./temp/fs_trunc.txt', 4);
var trunc = fs.readFileSync('./temp/fs_trunc.txt', 'utf8');
console.log('truncateSync:', trunc);

// cleanup
fs.unlinkSync('./temp/fs_basic_test.txt');
fs.unlinkSync('./temp/fs_basic_dir/b.txt');
fs.unlinkSync('./temp/fs_basic_dir/c.txt');
fs.rmdirSync('./temp/fs_basic_dir');
fs.unlinkSync('./temp/fs_trunc.txt');
console.log('cleanup done');
