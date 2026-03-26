// child_process module tests - Phase 6
import cp from 'child_process';

// Test 1: execSync - simple echo
var result = cp.execSync("echo hello");
console.log(result);

// Test 2: execSync - command with pipe
var result2 = cp.execSync("echo 'line1\nline2\nline3' | wc -l");
console.log("lines:" + result2.trim());

// Test 3: execSync - pwd
var cwd = cp.execSync("pwd");
console.log("has_cwd:" + (cwd.length > 0));

// Test 4: exec - async with callback (chains Test 5 for deterministic order)
cp.exec("echo async_hello", function(err, stdout, stderr) {
    console.log("stdout:" + stdout.trim());
    console.log("err:" + err);
    console.log("stderr_empty:" + (stderr.length === 0));

    // Test 5: exec - error command (nested for ordering)
    cp.exec("exit 1", function(err2, stdout2, stderr2) {
        console.log("has_error:" + (err2 !== null));
    });
});

0;
