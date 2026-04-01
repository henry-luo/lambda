// Numeric enum with auto-incrementing values
enum Direction {
    Up,
    Down,
    Left,
    Right
}

console.log(Direction.Up);
console.log(Direction.Down);
console.log(Direction.Left);
console.log(Direction.Right);

// Enum with explicit initializer
enum Color {
    Red = 1,
    Green = 2,
    Blue = 4
}

console.log(Color.Red);
console.log(Color.Green);
console.log(Color.Blue);

// Enum with mixed auto and explicit values
enum Status {
    Active,
    Paused = 10,
    Stopped
}

console.log(Status.Active);
console.log(Status.Paused);
console.log(Status.Stopped);
