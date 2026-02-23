// Default field values
type Config {
    host: string = "localhost",
    port: int = 8080,
    debug: bool = false
}

// Partial override: specify some, default the rest
let c1 = {Config host: "example.com"}
c1.host
c1.port
c1.debug

// Full override
let c2 = {Config host: "api.io", port: 443, debug: true}
c2.host
c2.port
c2.debug

// Inherit with defaults
type Shape {
    color: string = "black"
}
type Circle : Shape {
    radius: int = 10
}
let c3 = {Circle color: "red"}
c3.color
c3.radius
