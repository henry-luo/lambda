function plain(value) {
    return "plain:" + value;
}

var dynamicPlain = plain;
var receiver = {
    prefix: "recv",
    method: function (value) {
        return this.prefix + ":" + value;
    }
};

async function exerciseCallSpills() {
    console.log(dynamicPlain(await Promise.resolve(1)));
    console.log(receiver.method(await Promise.resolve(2)));
    console.log(receiver?.method(await Promise.resolve(3)));
    console.log(receiver[await Promise.resolve("method")](await Promise.resolve(4)));
    console.log(receiver?.[await Promise.resolve("method")]?.(await Promise.resolve(5)));

    var optionalPlain = dynamicPlain;
    console.log(optionalPlain?.(await Promise.resolve(6)));

    var selected = receiver[await Promise.resolve("method")];
    console.log(typeof selected);
}

exerciseCallSpills().then(function () {
    console.log("done");
});
