var Interface = SecurityPolicyViolationEvent;
console.log(Interface.name === 'SecurityPolicyViolationEvent' && Interface.length === 1);
var defaults = new Interface('securitypolicyviolation');
console.log(defaults instanceof Interface && defaults instanceof Event);
console.log(Object.getPrototypeOf(Interface.prototype) === Event.prototype);
console.log(defaults.type === 'securitypolicyviolation' && !defaults.isTrusted);
console.log(defaults.documentURI === '' && defaults.blockedURI === '' && defaults.referrer === '');
console.log(defaults.disposition === 'enforce' && defaults.statusCode === 0 && defaults.lineNumber === 0);
var init = {bubbles: true, cancelable: false, composed: true, documentURI: 'https://example.com/',
    blockedURI: 'inline', effectiveDirective: 'script-src', violatedDirective: 'old-directive',
    originalPolicy: "script-src 'self'", disposition: 'report', statusCode: 403,
    lineNumber: 12, columnNumber: 7, sample: 'source', sourceFile: 'script.js', referrer: 'refer'};
var event = new Interface('securitypolicyviolation', init);
console.log(event.bubbles && !event.cancelable && event.composed && !event.isTrusted);
console.log(Object.prototype.toString.call(event) === '[object SecurityPolicyViolationEvent]');
console.log(event.documentURI === init.documentURI && event.blockedURI === 'inline' && event.referrer === 'refer');
console.log(event.effectiveDirective === 'script-src' && event.violatedDirective === 'old-directive');
console.log(event.disposition === 'report' && event.statusCode === 403 && event.lineNumber === 12 && event.columnNumber === 7);
console.log(event.sample === 'source' && event.sourceFile === 'script.js' && event.originalPolicy === init.originalPolicy);
init.sample = 'changed';
event.sample = 'changed';
console.log(event.sample === 'source' && !Object.hasOwn(event, 'sample'));
var getter = Object.getOwnPropertyDescriptor(Interface.prototype, 'blockedURI').get;
console.log(getter.call(event) === 'inline');
try { getter.call(new Event('x')); console.log(false); } catch (e) { console.log(e instanceof TypeError); }
try { Interface('x'); console.log(false); } catch (e) { console.log(e instanceof TypeError); }
try { new Interface(); console.log(false); } catch (e) { console.log(e instanceof TypeError); }
try { new Interface('x', {disposition: 'invalid'}); console.log(false); } catch (e) { console.log(e instanceof TypeError); }
var converted = new Interface('x', {documentURI: 'https://example.com/', statusCode: 65537, lineNumber: 12.9, columnNumber: 4.9});
console.log(converted.documentURI === 'https://example.com/' && converted.statusCode === 1 && converted.lineNumber === 12 && converted.columnNumber === 4);
class Derived extends Interface {}
var derived = new Derived('derived');
console.log(derived instanceof Derived && derived instanceof Interface && derived.type === 'derived');
console.log('SECURITY_POLICY_VIOLATION_EVENT_DONE');
