// Typed arrays
const nums: number[] = [1, 2, 3, 4, 5];
console.log(nums.length);
console.log(nums[0]);
console.log(nums[4]);

// Let with type annotation
let sum: number = 0;
for (let i = 0; i < nums.length; i++) {
    sum = sum + nums[i];
}
console.log(sum);
