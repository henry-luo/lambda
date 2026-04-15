// fs module tests - Phase 4
import fs from 'fs';

// Test 1: writeFileSync + readFileSync
fs.writeFileSync("./temp/fs_test1.txt", "hello world");
var content = fs.readFileSync("./temp/fs_test1.txt");
console.log(content);

// Test 2: existsSync
console.log(fs.existsSync("./temp/fs_test1.txt"));
console.log(fs.existsSync("./temp/nonexistent_file_xyz.txt"));

// Test 3: statSync
var stat = fs.statSync("./temp/fs_test1.txt");
console.log(stat.size);
console.log(stat.isFile);
console.log(stat.isDirectory);

// Test 4: appendFileSync
fs.appendFileSync("./temp/fs_test1.txt", " appended");
var content2 = fs.readFileSync("./temp/fs_test1.txt");
console.log(content2);

// Test 5: readdirSync
fs.mkdirSync("./temp/fs_testdir");
fs.writeFileSync("./temp/fs_testdir/a.txt", "aaa");
fs.writeFileSync("./temp/fs_testdir/b.txt", "bbb");
var files = fs.readdirSync("./temp/fs_testdir");
console.log(files.length);

// Test 6: renameSync
fs.renameSync("./temp/fs_testdir/a.txt", "./temp/fs_testdir/c.txt");
console.log(fs.existsSync("./temp/fs_testdir/a.txt"));
console.log(fs.existsSync("./temp/fs_testdir/c.txt"));

// Test 7: unlinkSync
fs.unlinkSync("./temp/fs_testdir/c.txt");
console.log(fs.existsSync("./temp/fs_testdir/c.txt"));

// Test 8: async readFile
fs.readFile("./temp/fs_test1.txt", function(err, data) {
    if (err) {
        console.log("error:" + err);
    } else {
        console.log("async:" + data);
    }
    // Cleanup after async completes
    fs.unlinkSync("./temp/fs_testdir/b.txt");
    fs.rmdirSync("./temp/fs_testdir");
    fs.unlinkSync("./temp/fs_test1.txt");
});
0;
