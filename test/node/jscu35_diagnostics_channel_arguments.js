// JSCU35: diagnostics_channel tracing preserves an unbounded rest argument list.
const diagnostics = require("diagnostics_channel");
const channel = diagnostics.tracingChannel("jscu35_arguments");
const values = [];
for (let i = 0; i < 17; i++) {
    values.push(i);
}
channel.traceSync((...args) => {
    console.log(args.length, args[16]);
}, {}, undefined, ...values);
