const proxy = new Proxy(Array.prototype, {});
delete proxy.push;
try {
    console.log([1].push(2));
} catch (error) {
    console.log(error.name);
}
