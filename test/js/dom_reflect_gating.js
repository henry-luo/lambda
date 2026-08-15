// C0.5 regression: IDL reflection must gate reads and writes on the same
// element/property pairs. The setter used to apply the IDL->attribute name
// mapping on any element, so an unsupported pair wrote a content attribute
// the getter then refused to read back.
function show(name, value) { console.log(name + '=' + JSON.stringify(value)); }

var lbl = document.getElementById('lbl');
var div = document.getElementById('div');
var img = document.getElementById('img');
var inp = document.getElementById('inp');
var frm = document.getElementById('frm');

// supported pairs round-trip through the content attribute
lbl.htmlFor = 'field';
show('lbl.htmlFor', lbl.htmlFor);
show('lbl@for', lbl.getAttribute('for'));

img.alt = 'picture';
show('img.alt', img.alt);
show('img@alt', img.getAttribute('alt'));

inp.readOnly = true;
show('inp.readOnly', inp.readOnly);
show('inp@readonly', inp.getAttribute('readonly'));

inp.maxLength = 12;
show('inp.maxLength', inp.maxLength);
show('inp@maxlength', inp.getAttribute('maxlength'));

frm.acceptCharset = 'utf-8';
show('frm.acceptCharset', frm.acceptCharset);
show('frm@accept-charset', frm.getAttribute('accept-charset'));

inp.formAction = '/submit';
show('inp.formAction', inp.formAction);
show('inp@formaction', inp.getAttribute('formaction'));

// global reflected attributes apply to every element
div.contentEditable = 'true';
show('div.contentEditable', div.contentEditable);
show('div@contenteditable', div.getAttribute('contenteditable'));

// unsupported pairs are plain expandos: value read back verbatim, no attribute
div.htmlFor = 'nope';
show('div.htmlFor', div.htmlFor);
show('div@for', div.getAttribute('for'));

div.formAction = 'nope';
show('div.formAction', div.formAction);
show('div@formaction', div.getAttribute('formaction'));

div.readOnly = true;
show('div.readOnly', div.readOnly);
show('div@readonly', div.getAttribute('readonly'));

div.maxLength = 5;
show('div.maxLength', div.maxLength);
show('div@maxlength', div.getAttribute('maxlength'));

div.alt = 'nope';
show('div.alt', div.alt);
show('div@alt', div.getAttribute('alt'));
