async function leaf(n) { {let scratch = new Array(16).fill(n); await 0; return scratch[0];} }
async function main() { let sum=0; for(let i=0;i<80;i++) sum+=await leaf(i); console.log(sum); }
main();
