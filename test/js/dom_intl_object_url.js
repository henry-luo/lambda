var displayNames = new Intl.DisplayNames(['en'], { type: 'language' });
console.log(displayNames.of('ja'));

var blob = new Blob(['self.onmessage = function() {};'], { type: 'text/javascript' });
var firstUrl = URL.createObjectURL(blob);
var secondUrl = URL.createObjectURL(blob);
// object URLs now carry the document origin rather than the retired mock scheme payload.
var objectOrigin = location.origin || 'null';
console.log(firstUrl.indexOf('blob:' + objectOrigin + '/') === 0);
console.log(firstUrl !== secondUrl);
URL.revokeObjectURL(firstUrl);
console.log('object-url-revoked');
