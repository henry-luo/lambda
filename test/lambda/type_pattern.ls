// ============================================================
// type_pattern.ls - Sophisticated Nested Type Pattern Tests
// ============================================================
// Tests complex nested data structures and type patterns
// covering 3+ levels of nesting, arrays of maps, maps of arrays,
// tree-like structures, and real-world schema patterns.

"===== ADVANCED TYPE PATTERN TESTS ====="

// ============================================================
// Section 1: Basic Nested Maps (3+ levels)
// ============================================================
'Section 1: Nested Maps'

type Coord = { lat: float, lon: float }
type Location = { name: string, coord: Coord }
type Route = { 
    start: Location,
    end: Location,
    waypoints: Location*
}

let simple_route = {
    start: { name: "Home", coord: { lat: 42.3601, lon: -71.0589 } },
    end: { name: "Work", coord: { lat: 42.3651, lon: -71.0549 } },
    waypoints: []
}

let complex_route = {
    start: { name: "Airport", coord: { lat: 42.3656, lon: -71.0096 } },
    end: { name: "Hotel", coord: { lat: 42.3554, lon: -71.0655 } },
    waypoints: [
        { name: "Gas Station", coord: { lat: 42.3600, lon: -71.0400 } },
        { name: "Coffee Shop", coord: { lat: 42.3580, lon: -71.0550 } }
    ]
}

"1.1"; (simple_route is Route)        // true
"1.2"; (complex_route is Route)       // true

// Partial/invalid routes
let route_missing_coord = {
    start: { name: "Home" },          // missing coord
    end: { name: "Work", coord: { lat: 42.0, lon: -71.0 } },
    waypoints: []
}
"1.3"; (route_missing_coord is Route) // false - start missing coord

let route_bad_waypoint = {
    start: { name: "A", coord: { lat: 1.0, lon: 1.0 } },
    end: { name: "B", coord: { lat: 2.0, lon: 2.0 } },
    waypoints: [{ name: "Middle" }]   // missing coord
}
"1.4"; (route_bad_waypoint is Route)  // false - waypoint missing coord

// ============================================================
// Section 2: Arrays of Complex Types with Occurrence
// ============================================================
'Section 2: Arrays with Occurrence'

type Point = { x: int, y: int }
type Polygon = Point[3+]              // at least 3 points
type Triangle = Point[3]              // exactly 3 points
type Line = Point[2]                  // exactly 2 points

let triangle_pts = [{ x: 0, y: 0 }, { x: 1, y: 0 }, { x: 0, y: 1 }]
let line_pts = [{ x: 0, y: 0 }, { x: 5, y: 5 }]
let polygon_pts = [{ x: 0, y: 0 }, { x: 2, y: 0 }, { x: 2, y: 2 }, { x: 0, y: 2 }]

"2.1"; (triangle_pts is Triangle)      // true - exactly 3 points
"2.2"; (triangle_pts is Polygon)       // true - at least 3 points
"2.3"; (line_pts is Line)              // true - exactly 2 points
"2.4"; (line_pts is Polygon)           // false - need at least 3

let empty_pts = []
"2.5"; (empty_pts is Polygon)          // false - need at least 3
"2.6"; (empty_pts is Triangle)         // false - need exactly 3

// ============================================================
// Section 3: Deep Nesting (4+ levels) with Inline Occurrence
// ============================================================
'Section 3: Deep Nesting'

// Test inline occurrence in map fields - this should work!
type Trip = { 
    id: string,
    route: Route,
    passengers: string[1+]            // inline occurrence in map field
}

let deep_trip = {
    id: "TRIP-001",
    route: {
        start: { name: "Home", coord: { lat: 42.3601, lon: -71.0589 } },
        end: { name: "Office", coord: { lat: 42.3651, lon: -71.0549 } },
        waypoints: [
            { name: "Coffee Shop", coord: { lat: 42.3620, lon: -71.0570 } }
        ]
    },
    passengers: ["Alice", "Bob"]
}

"3.1"; (deep_trip is Trip)             // true - 4 levels deep

let trip_no_passengers = {
    id: "TRIP-002",
    route: {
        start: { name: "A", coord: { lat: 1.0, lon: 1.0 } },
        end: { name: "B", coord: { lat: 2.0, lon: 2.0 } },
        waypoints: []
    },
    passengers: []                     // empty - needs at least 1
}
"3.2"; (trip_no_passengers is Trip)    // false - passengers need 1+

// ============================================================
// Section 4: Mixed Map/Array Nesting
// ============================================================
'Section 4: Mixed Structures'

type Dimensions = { width: int, height: int }
type Product = {
    name: string,
    dimensions: Dimensions,
    tags: string*
}
type Warehouse = {
    id: string,
    inventory: Product[1+]            // inline occurrence
}

let warehouse = {
    id: "WH-001",
    inventory: [
        { name: "Widget", dimensions: { width: 10, height: 5 }, tags: ["small", "blue"] },
        { name: "Gadget", dimensions: { width: 20, height: 15 }, tags: ["medium"] }
    ]
}

"4.1"; (warehouse is Warehouse)        // true

let empty_warehouse = {
    id: "WH-002",
    inventory: []                      // empty - needs at least 1 product
}
"4.2"; (empty_warehouse is Warehouse)  // false - need at least 1 product

let warehouse_bad_product = {
    id: "WH-003",
    inventory: [
        { name: "Item", dimensions: { width: 5 }, tags: [] }  // missing height
    ]
}
"4.3"; (warehouse_bad_product is Warehouse) // false - missing height

// ============================================================
// Section 5: Nested Arrays (Array of Arrays)
// ============================================================
'Section 5: Nested Arrays'

type Matrix = int*[2+]                // at least 2 rows of ints

let matrix_2x3 = [[1, 2, 3], [4, 5, 6]]
let matrix_3x3 = [[1, 2, 3], [4, 5, 6], [7, 8, 9]]
let matrix_1x3 = [[1, 2, 3]]

"5.1"; (matrix_2x3 is Matrix)          // true - 2 rows
"5.2"; (matrix_3x3 is Matrix)          // true - 3 rows  
"5.3"; (matrix_1x3 is Matrix)          // false - only 1 row, need 2+

let empty_matrix = []
"5.4"; (empty_matrix is Matrix)        // false - need at least 2 rows

type PointGrid = Point*[1+]            // at least 1 row of points

let point_grid = [
    [{ x: 0, y: 0 }, { x: 1, y: 0 }],
    [{ x: 0, y: 1 }, { x: 1, y: 1 }]
]
"5.5"; (point_grid is PointGrid)       // true - array of point arrays

// ============================================================
// Section 6: Recursive-like Structures (Tree)
// ============================================================
'Section 6: Tree Structures'

// Note: Lambda doesn't support true recursive types, but we can define fixed-depth trees
type LeafNode = { value: int }
type BranchNode = { 
    value: int,
    left: LeafNode,
    right: LeafNode
}
type Tree2 = {
    value: int,
    left: BranchNode,
    right: BranchNode
}

let leaf1 = { value: 1 }
let leaf2 = { value: 2 }
let leaf3 = { value: 3 }
let leaf4 = { value: 4 }

let branch1 = { value: 10, left: leaf1, right: leaf2 }
let branch2 = { value: 20, left: leaf3, right: leaf4 }

let tree = { value: 100, left: branch1, right: branch2 }

"6.1"; (leaf1 is LeafNode)             // true
"6.2"; (branch1 is BranchNode)         // true
"6.3"; (tree is Tree2)                 // true

// Invalid tree
let bad_branch = { value: 5, left: { value: 1 } }  // missing right
"6.4"; (bad_branch is BranchNode)      // false - missing right

// ============================================================
// Section 7: Union Types with Complex Nesting
// ============================================================
'Section 7: Unions with Nesting'

type Circle = { kind: string, radius: float }
type Rectangle = { kind: string, width: float, height: float }
type Shape = Circle | Rectangle

type Drawing = {
    name: string,
    shapes: Shape[1+]                  // inline occurrence with union element type
}

let circle = { kind: "circle", radius: 5.0 }
let rect = { kind: "rect", width: 10.0, height: 20.0 }

"7.1"; (circle is Circle)              // true
"7.2"; (rect is Rectangle)             // true
"7.3"; (circle is Shape)               // false - TODO: union validation needs work
"7.4"; (rect is Shape)                 // false - TODO: union validation needs work

let drawing = {
    name: "My Drawing",
    shapes: [circle, rect]
}
"7.5"; (drawing is Drawing)            // false - TODO: union validation needs work

let empty_drawing = {
    name: "Empty",
    shapes: []                         // needs at least 1 shape
}
"7.6"; (empty_drawing is Drawing)      // false - shapes need 1+

// ============================================================
// Section 8: Event/Log-style Nested Structures
// ============================================================
'Section 8: Event Structures'

type Event = {
    kind: string,
    timestamp: int,
    data: map
}

type EventLog = Event[1+]

let events = [
    { kind: "click", timestamp: 1000, data: { x: 100, y: 200 } },
    { kind: "scroll", timestamp: 1001, data: { delta: 50 } },
    { kind: "keypress", timestamp: 1002, data: { key: "Enter" } }
]

"8.1"; (events is EventLog)            // true

let mixed_events = [
    { kind: "click", timestamp: 1000, data: { x: 100 } },
    { kind: 123, timestamp: 1001, data: {} }  // type should be string
]
"8.2"; (mixed_events is EventLog)      // false - second event has wrong type

// ============================================================
// Section 9: Boundary Cases
// ============================================================
'Section 9: Boundary Cases'

type EmptyAllowed = int[0, 10]         // 0 to 10 ints
type ZeroExact = int[0]                // exactly 0 ints (always empty)
type LargeRange = int[5, 100]          // 5 to 100 ints

"9.1"; ([] is EmptyAllowed)            // true - 0 is allowed
"9.2"; ([1,2,3] is EmptyAllowed)       // true - 3 is in range
"9.3"; ([1,2,3,4,5,6,7,8,9,10,11] is EmptyAllowed) // false - 11 > 10
"9.4"; ([] is ZeroExact)               // true - exactly 0
"9.5"; (["a"] is ZeroExact)            // false - has 1, need 0

let five_ints = [1, 2, 3, 4, 5]
let four_ints = [1, 2, 3, 4]
"9.6"; (five_ints is LargeRange)       // true - 5 is minimum
"9.7"; (four_ints is LargeRange)       // false - 4 < 5

// ============================================================
// Section 10: Real-world Complex Schema
// ============================================================
'Section 10: Real-world Schema'

type HttpHeader = { name: string, value: string }

type HttpRequest = {
    method: string,
    url: string,
    headers: HttpHeader*,
    body: string?
}

type HttpResponse = {
    status: int,
    headers: HttpHeader[1+],           // inline occurrence
    body: string?
}

type ApiCall = {
    request: HttpRequest,
    response: HttpResponse,
    duration_ms: int
}

type ApiLog = ApiCall[1+]

let api_log = [
    {
        request: {
            method: "GET",
            url: "/api/users",
            headers: [{ name: "Accept", value: "application/json" }],
            body: null
        },
        response: {
            status: 200,
            headers: [
                { name: "Content-Type", value: "application/json" },
                { name: "Cache-Control", value: "max-age=3600" }
            ],
            body: "[{\"id\": 1, \"name\": \"Alice\"}]"
        },
        duration_ms: 45
    },
    {
        request: {
            method: "POST",
            url: "/api/users",
            headers: [
                { name: "Content-Type", value: "application/json" },
                { name: "Authorization", value: "Bearer token123" }
            ],
            body: "{\"name\": \"Bob\"}"
        },
        response: {
            status: 201,
            headers: [{ name: "Location", value: "/api/users/2" }],
            body: "{\"id\": 2, \"name\": \"Bob\"}"
        },
        duration_ms: 120
    }
]

"10.1"; (api_log is ApiLog)            // true - complex nested validation

// Test with missing required nested field
let bad_api_log = [
    {
        request: {
            method: "GET",
            url: "/api/test",
            headers: []
        },
        response: {
            status: 200,
            headers: [],                // empty - needs at least 1
            body: "OK"
        },
        duration_ms: 10
    }
]
"10.2"; (bad_api_log is ApiLog)        // false - response.headers needs 1+

"===== END OF ADVANCED TYPE PATTERN TESTS ====="
