// Interface declaration (should be stripped, no runtime effect)
interface Point {
    x: number;
    y: number;
}

// Type alias (should be stripped, no runtime effect)
type ID = number;

// Code after stripped declarations should still work
const p = { x: 10, y: 20 };
console.log(p.x);
console.log(p.y);
console.log(p.x + p.y);
