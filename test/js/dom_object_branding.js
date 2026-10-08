var button = document.getElementById('button');
var div = document.getElementById('div');
var link = document.getElementById('link');
var fragment = document.createDocumentFragment();

console.log(Object.prototype.toString.call(button));
console.log(Object.prototype.toString.call(div));
console.log(Object.prototype.toString.call(fragment));
console.log(button instanceof Element);
console.log(button instanceof HTMLElement);
console.log(button instanceof HTMLButtonElement);
console.log(Object.prototype.toString.call(link));
console.log(link instanceof HTMLElement);
console.log(link instanceof HTMLLinkElement);

// stylesheet owners must expose the specialized HTML interface as well.
var style = document.createElement('style');
console.log(Object.prototype.toString.call(style));
console.log(style instanceof HTMLElement);
console.log(style instanceof HTMLStyleElement);

var iframe = document.createElement('iframe');
console.log(Object.prototype.toString.call(iframe));
console.log(iframe instanceof HTMLElement);
console.log(iframe instanceof HTMLIFrameElement);

var comment = new Comment('brand');
console.log(comment instanceof Comment);
console.log(comment instanceof CharacterData);
console.log(comment instanceof Node);
console.log(comment.data);

var browserUrl = new URL('https://example.test/path?kind=browser');
console.log(browserUrl.hostname);
console.log(browserUrl.searchParams.get('kind'));
