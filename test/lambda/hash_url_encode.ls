// hash and url_encode (WebScrape E7, Archive AR-E7): SHA-2 digests as
// binary, of bytes or of text's UTF-8, and RFC 3986 percent-encoding.

"=== hash ===";
// FIPS 180-2 test vectors
[hash("abc", 'sha256'), hash("", 'sha256'), hash("abc", 'sha512')];
[hash(b'\x616263', "sha256") == hash("abc", 'sha256'), hash("é", 'sha256') == hash(b'\xC3A9', 'sha256'),
 len(hash("abc", 'sha384')), len(hash("abc", 'sha512')), hash(null, 'sha256') == hash("", 'sha256')];
[(hash("abc", 'md5') ^ { ^.message }), (hash(42, 'sha256') ^ { ^.message })]

"=== url_encode ===";
[url_encode("a b&c=d/é~._-"), url_encode("ABCxyz0189"), url_encode(""), (url_encode(42) ^ { "refused" })]
