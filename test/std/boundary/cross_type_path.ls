// Test: Cross-Type Path
// Layer: 3 | Category: boundary | Covers: path equality, path with string, path in collection

// ===== Path equality =====
/a/b == /a/b
/a/b == /a/c
/a/b != /a/c

// ===== Path type check =====
/test is path
/test is string
/test is int

// ===== Path in array =====
let paths = [/a, /b, /c, /d]
len(paths)
paths[0]
paths[3]

// ===== Path in map =====
let routes = {
    home: /index,
    about: /about,
    api: /api/v1
}
routes.home
routes.api

// ===== Path to string =====
str(/hello/world)

// ===== Path comparison =====
/a < /b
/b > /a

// ===== Path in filter =====
let all_paths = [/api/users, /api/posts, /web/home, /web/about]
all_paths | filter((p) => str(p) | starts_with("/api"))

// ===== Path in element =====
let link = <a href: /about/us> "About Us"
link.href

// ===== Multiple paths =====
let p1 = /users
let p2 = /users/123
let p3 = /users/123/profile
p1
p2
p3

// ===== Path with http scheme =====
let url = /http://example.com/api
url is path
str(url)
