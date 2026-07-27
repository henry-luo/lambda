import fs from 'fs';

var root = './temp/jube_fs_directory_registry';
if (fs.existsSync(root + '/nested/entry.txt')) fs.unlinkSync(root + '/nested/entry.txt');
if (fs.existsSync(root + '/nested')) fs.rmdirSync(root + '/nested');
if (fs.existsSync(root)) fs.rmdirSync(root);

fs.mkdirSync(root + '/nested', { recursive: true });
fs.writeFileSync(root + '/nested/entry.txt', 'entry');
var entries = fs.readdirSync(root + '/nested', { encoding: 'utf8' });
console.log(entries.length);
console.log(entries[0]);
try {
    fs.readdirSync(root, { encoding: 'not-an-encoding' });
} catch (error) {
    console.log(error.code);
}
fs.unlinkSync(root + '/nested/entry.txt');
fs.rmdirSync(root + '/nested');
fs.rmdirSync(root);

