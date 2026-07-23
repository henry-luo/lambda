delete Array.prototype.push;
try {
    console.log([1].push(2));
} catch (error) {
    console.log(error.name);
}
