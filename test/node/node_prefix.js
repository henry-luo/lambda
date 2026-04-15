// Test node: prefix for module imports
import path from 'node:path';
import os from 'node:os';

// Verify path module works with node: prefix
console.log(path.basename('/foo/bar.txt'));
console.log(path.sep);

// Verify os module works with node: prefix
console.log(typeof os.platform());
console.log(typeof os.arch());
