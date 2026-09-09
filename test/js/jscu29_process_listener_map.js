let delivered = 0;
for (let i = 0; i < 33; i++) {
    process.on('exit', () => { delivered++; });
}
process.on('exit', () => console.log(delivered));
process.exit(0);
