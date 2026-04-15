// url module basic tests
import url from 'url';

// url.parse
const parsed = url.parse('https://user:pass@example.com:8080/path?query=1#hash');
console.log(parsed.protocol);
console.log(parsed.hostname);
console.log(parsed.port);
console.log(parsed.pathname);
console.log(parsed.search);
console.log(parsed.hash);

// url.resolve
console.log(url.resolve('https://example.com/a/b', '/c'));
console.log(url.resolve('https://example.com/a/b', 'c'));

// url.format
const formatted = url.format({
    protocol: 'https:',
    hostname: 'example.com',
    pathname: '/path',
    search: '?q=1'
});
console.log(formatted);
