// path module basic tests
import path from 'path';

// path.basename
console.log(path.basename('/foo/bar/baz.txt'));
console.log(path.basename('/foo/bar/baz.txt', '.txt'));

// path.dirname
console.log(path.dirname('/foo/bar/baz.txt'));
console.log(path.dirname('/foo'));

// path.extname
console.log(path.extname('index.html'));
console.log(path.extname('index.'));
console.log(path.extname('index') === '');
console.log(path.extname('.index') === '');

// path.isAbsolute
console.log(path.isAbsolute('/foo/bar'));
console.log(path.isAbsolute('foo/bar'));

// path.sep
console.log(path.sep);

// path.delimiter
console.log(path.delimiter);

// path.join
console.log(path.join('foo', 'bar', 'baz'));
console.log(path.join('/foo', 'bar', 'baz'));
console.log(path.join('foo', 'bar'));

// path.normalize
console.log(path.normalize('/foo/bar//baz/asdf/quux/..'));

// path.parse
const parsed = path.parse('/home/user/dir/file.txt');
console.log(parsed.root);
console.log(parsed.dir);
console.log(parsed.base);
console.log(parsed.ext);
console.log(parsed.name);

// path.format
const formatted = path.format({ dir: '/home/user/dir', base: 'file.txt' });
console.log(formatted);
