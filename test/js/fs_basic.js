// fs module tests - Phase 4
import fs from 'fs';

// Clean up from any interrupted prior run.
if (fs.existsSync("./temp/fs_test1.txt")) {
    fs.unlinkSync("./temp/fs_test1.txt");
}
if (fs.existsSync("./temp/fs_testdir")) {
    var staleFiles = fs.readdirSync("./temp/fs_testdir");
    for (var i = 0; i < staleFiles.length; i++) {
        fs.unlinkSync("./temp/fs_testdir/" + staleFiles[i]);
    }
    fs.rmdirSync("./temp/fs_testdir");
}

// Test 1: writeFileSync + readFileSync
fs.writeFileSync("./temp/fs_test1.txt", "hello world");
var content = fs.readFileSync("./temp/fs_test1.txt", "utf8");
console.log(content);

// Test 2: existsSync
console.log(fs.existsSync("./temp/fs_test1.txt"));
console.log(fs.existsSync("./temp/nonexistent_file_xyz.txt"));

// Test 3: statSync
var stat = fs.statSync("./temp/fs_test1.txt");
console.log(stat.size);
console.log(typeof stat.size);
console.log(stat.isFile());
console.log(stat.isDirectory());
var statBig = fs.statSync("./temp/fs_test1.txt", { bigint: true });
console.log(typeof statBig.size);
console.log(typeof statBig.mtimeMs);
console.log(statBig.isFile());
console.log(typeof fs.lstatSync("./temp/fs_test1.txt", { bigint: true }).ino);
var fd = fs.openSync("./temp/fs_test1.txt", "r");
console.log(typeof fs.fstatSync(fd, { bigint: true }).size);
fs.closeSync(fd);
console.log(typeof fs.statfsSync("./temp").bsize);
console.log(typeof fs.statfsSync("./temp", { bigint: true }).bsize);

// Test 4: appendFileSync
fs.appendFileSync("./temp/fs_test1.txt", " appended");
var content2 = fs.readFileSync("./temp/fs_test1.txt", "utf8");
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
