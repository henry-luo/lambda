const prototype = Intl.DateTimeFormat.prototype;
const descriptor = Object.getOwnPropertyDescriptors(prototype).format;
const format = descriptor.get.call(Intl.DateTimeFormat("en"));

console.log(typeof Intl.DateTimeFormat + "," + typeof descriptor.get + "," + typeof format);
console.log(typeof prototype.formatRange + "," + typeof prototype.formatToParts + "," +
  typeof prototype.formatRangeToParts);
console.log(Intl.DateTimeFormat("en").resolvedOptions().locale);
console.log(typeof format(0));
console.log("OK");
