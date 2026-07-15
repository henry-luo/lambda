import { worker, immediate, fail } from "./concurrency_lambda_module.ls";

console.log(worker(1) instanceof Promise);
console.log(immediate.length);
Promise.resolve().then(() => console.log("js-microtask"));
immediate(4).then((value) => console.log("immediate", value));
worker(5).then((value) => console.log("worker", value));
fail().catch((error) => console.log("reject", error.message));
(async () => console.log("await", await worker(7)))();
