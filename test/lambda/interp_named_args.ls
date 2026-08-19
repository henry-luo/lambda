fn rect(left, top, width, height) => [left, top, width, height]
fn config(host = "localhost", port = 8080, debug = false) => [host, port, debug]

[
  rect(height: 50, width: 100, top: 20, left: 10),
  rect(0, 0, width: 200, height: 100),
  config(port: 3000),
  config(host: "example.com", debug: true)
]
