const diagnostics = require('diagnostics_channel');

console.log(diagnostics === require('node:diagnostics_channel'));
console.log(typeof diagnostics.channel, typeof diagnostics.tracingChannel, typeof diagnostics.subscribe);
const channel = diagnostics.channel('jube-registry');
console.log(typeof channel.publish, channel.hasSubscribers);
