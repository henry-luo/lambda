var video = document.createElement("video");
var audio = document.createElement("audio");
var canPlay = HTMLMediaElement.prototype.canPlayType;
var descriptor = Object.getOwnPropertyDescriptor(HTMLMediaElement.prototype, "canPlayType");
console.log(video.canPlayType === canPlay && audio.canPlayType === canPlay);
console.log(descriptor.writable && descriptor.enumerable && descriptor.configurable);
console.log(canPlay.name, canPlay.length, Object.hasOwn(canPlay, "prototype"));
function typeError(action) {
  try { action(); return false; } catch (error) { return error instanceof TypeError; }
}
console.log(typeError(function () { new canPlay("video/mp4"); }));
var invalidReceivers = [null, undefined, {}, document, document.createElement("div"),
  document.createTextNode(""), document.createElementNS("http://www.w3.org/2000/svg", "video"),
  Object.create(HTMLVideoElement.prototype), new Proxy(video, {})];
console.log(invalidReceivers.every(function (receiver) {
  return typeError(function () { canPlay.call(receiver, "video/mp4"); });
}));
console.log(typeError(function () { video.canPlayType(); }),
  typeError(function () { video.canPlayType(Symbol("mime")); }));
var conversionHint = "";
var mime = {};
mime[Symbol.toPrimitive] = function (hint) { conversionHint = hint; return "not/a-media-type"; };
console.log(video.canPlayType(mime) === "", conversionHint);
var conversionError = new Error("MIME conversion");
try { video.canPlayType({ toString: function () { throw conversionError; } }); }
catch (error) { console.log(error === conversionError); }
var invalidTypes = [undefined, null, false, 12, "", "video", "video/", "/mp4",
  "video /mp4", "video/mp4/extra", "video/mp4\0", "text/html", "application/octet-stream",
  "video/mp4; codecs=\"not-a-codec\""];
console.log(invalidTypes.every(function (type) { return video.canPlayType(type) === ""; }));
var mp4 = video.canPlayType("video/mp4");
console.log(mp4 === "" || mp4 === "maybe");
console.log(video.canPlayType(" \tVIDEO/MP4 \r\n") === mp4,
  audio.canPlayType("video/mp4") === mp4,
  video.canPlayType("video/mp4; nonsense=value") === mp4);
console.log(video.canPlayType("video/mp4; codecs=\"not-a-codec\"; codecs=\"avc1.42E01E\"") === "");
var hevc = video.canPlayType("video/mp4; codecs=hvc1");
console.log(hevc === "" || hevc === "maybe" || hevc === "probably");
Object.setPrototypeOf(video, null);
console.log(canPlay.call(video, "video/mp4") === mp4);
