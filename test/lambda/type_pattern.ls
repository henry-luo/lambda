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

// ============================================================
// Section 11: Discriminated Unions (Tagged Unions)
// ============================================================
'Section 11: Discriminated Unions'

type TextNode = { tag: string, content: string }
type ImageNode = { tag: string, src: string, alt: string }
type LinkNode = { tag: string, href: string, text: string }
type ContentNode = TextNode | ImageNode | LinkNode

type Document = {
    title: string,
    nodes: ContentNode[1+]
}

let text_node = { tag: "text", content: "Hello World" }
let image_node = { tag: "img", src: "/photo.jpg", alt: "A photo" }
let link_node = { tag: "a", href: "https://example.com", text: "Click here" }

"11.1"; (text_node is TextNode)        // true
"11.2"; (image_node is ImageNode)      // true
"11.3"; (link_node is LinkNode)        // true
"11.4"; (text_node is ContentNode)     // true - matches TextNode in union

let doc = {
    title: "My Document",
    nodes: [text_node, image_node, link_node]
}
"11.5"; (doc is Document)              // true - all nodes match union

let bad_node = { tag: "unknown" }      // doesn't match any union member
"11.6"; (bad_node is ContentNode)      // false - incomplete

// ============================================================
// Section 12: Optional Fields with Complex Types
// ============================================================
'Section 12: Optional Complex Fields'

type Address = {
    street: string,
    city: string,
    zip: string?,                      // optional
    country: string?                   // optional
}

type Person = {
    name: string,
    age: int,
    address: Address?,                 // optional complex type
    phone: string?
}

type Team = {
    name: string,
    lead: Person,
    members: Person[1+]
}

let person_full = {
    name: "Alice",
    age: 30,
    address: { street: "123 Main St", city: "Boston", zip: "02101", country: "USA" },
    phone: "555-1234"
}

let person_minimal = {
    name: "Bob",
    age: 25,
    address: null,
    phone: null
}

let person_partial_addr = {
    name: "Charlie",
    age: 35,
    address: { street: "456 Oak Ave", city: "Cambridge", zip: null, country: null },
    phone: null
}

"12.1"; (person_full is Person)        // true
"12.2"; (person_minimal is Person)     // true - optional fields are null
"12.3"; (person_partial_addr is Person) // true - nested optional fields

let team = {
    name: "Engineering",
    lead: person_full,
    members: [person_minimal, person_partial_addr]
}
"12.4"; (team is Team)                 // true

let team_no_members = {
    name: "Solo",
    lead: person_full,
    members: []                        // needs at least 1
}
"12.5"; (team_no_members is Team)      // false - members need 1+

// ============================================================
// Section 13: Nested Occurrence Patterns
// ============================================================
'Section 13: Nested Occurrence'

type Tag = { name: string, value: string }
type TagGroup = Tag[2, 5]              // 2-5 tags per group
type TagCollection = TagGroup[1+]      // at least 1 group of tags

let tag_group_2 = [
    { name: "env", value: "prod" },
    { name: "region", value: "us-east" }
]

let tag_group_3 = [
    { name: "app", value: "api" },
    { name: "version", value: "1.0" },
    { name: "tier", value: "backend" }
]

let tag_group_1 = [{ name: "solo", value: "tag" }]  // only 1, need 2+

"13.1"; (tag_group_2 is TagGroup)      // true - 2 tags
"13.2"; (tag_group_3 is TagGroup)      // true - 3 tags
"13.3"; (tag_group_1 is TagGroup)      // false - only 1, need 2-5

let collection = [tag_group_2, tag_group_3]
"13.4"; (collection is TagCollection)  // true - 2 valid groups

let bad_collection = [tag_group_1, tag_group_2]
"13.5"; (bad_collection is TagCollection) // false - first group invalid

// ============================================================
// Section 14: Graph-like Structures
// ============================================================
'Section 14: Graph Structures'

type NodeId = string
type Edge = { from: NodeId, to: NodeId, weight: float? }
type Node = { id: NodeId, label: string, data: map? }

type Graph = {
    nodes: Node[1+],
    edges: Edge*
}

let graph = {
    nodes: [
        { id: "A", label: "Start", data: { color: "green" } },
        { id: "B", label: "Middle", data: null },
        { id: "C", label: "End", data: { color: "red" } }
    ],
    edges: [
        { from: "A", to: "B", weight: 1.5 },
        { from: "B", to: "C", weight: 2.0 },
        { from: "A", to: "C", weight: null }
    ]
}

"14.1"; (graph is Graph)               // true

let graph_no_edges = {
    nodes: [{ id: "X", label: "Isolated", data: null }],
    edges: []
}
"14.2"; (graph_no_edges is Graph)      // true - edges are optional (*)

let graph_no_nodes = {
    nodes: [],                         // needs at least 1
    edges: []
}
"14.3"; (graph_no_nodes is Graph)      // false - nodes need 1+

// ============================================================
// Section 15: Configuration Patterns
// ============================================================
'Section 15: Config Patterns'

type DatabaseConfig = {
    host: string,
    port: int,
    username: string,
    password: string?,
    options: map?
}

type CacheConfig = {
    provider: string,
    ttl: int,
    maxSize: int?
}

type LogConfig = {
    level: string,
    outputs: string[1+]
}

type AppConfig = {
    name: string,
    version: string,
    database: DatabaseConfig,
    cache: CacheConfig?,               // optional section
    logging: LogConfig
}

let full_config = {
    name: "MyApp",
    version: "2.0.0",
    database: {
        host: "localhost",
        port: 5432,
        username: "admin",
        password: "secret",
        options: { ssl: true, timeout: 30 }
    },
    cache: {
        provider: "redis",
        ttl: 3600,
        maxSize: 1000
    },
    logging: {
        level: "info",
        outputs: ["stdout", "file"]
    }
}

"15.1"; (full_config is AppConfig)     // true

let minimal_config = {
    name: "SimpleApp",
    version: "1.0.0",
    database: {
        host: "db.example.com",
        port: 3306,
        username: "user",
        password: null,
        options: null
    },
    cache: null,                       // optional
    logging: {
        level: "error",
        outputs: ["stderr"]
    }
}
"15.2"; (minimal_config is AppConfig)  // true

let bad_config = {
    name: "BrokenApp",
    version: "0.1.0",
    database: {
        host: "localhost",
        port: 5432,
        username: "admin",
        password: null,
        options: null
    },
    cache: null,
    logging: {
        level: "debug",
        outputs: []                    // needs at least 1
    }
}
"15.3"; (bad_config is AppConfig)      // false - logging.outputs needs 1+

// ============================================================
// Section 16: Nested Maps with Different Value Types
// ============================================================
'Section 16: Heterogeneous Nesting'

type Metric = { name: string, value: float, unit: string }
type MetricGroup = { category: string, metrics: Metric[1+] }
type Dashboard = {
    title: string,
    groups: MetricGroup[2+],           // at least 2 groups
    refreshInterval: int?
}

let dashboard = {
    title: "System Metrics",
    groups: [
        {
            category: "CPU",
            metrics: [
                { name: "usage", value: 45.5, unit: "%" },
                { name: "temperature", value: 65.0, unit: "C" }
            ]
        },
        {
            category: "Memory",
            metrics: [
                { name: "used", value: 8.5, unit: "GB" },
                { name: "free", value: 7.5, unit: "GB" }
            ]
        },
        {
            category: "Disk",
            metrics: [
                { name: "read_iops", value: 1500.0, unit: "ops/s" }
            ]
        }
    ],
    refreshInterval: 5000
}

"16.1"; (dashboard is Dashboard)       // true - 3 groups, all valid

let single_group_dashboard = {
    title: "Simple",
    groups: [
        { category: "Test", metrics: [{ name: "x", value: 1.0, unit: "u" }] }
    ],
    refreshInterval: null
}
"16.2"; (single_group_dashboard is Dashboard) // false - only 1 group, need 2+

// ============================================================
// Section 17: Array Index Patterns
// ============================================================
'Section 17: Fixed Size Arrays'

type RGB = int[3]                      // exactly 3 ints
type RGBA = int[4]                     // exactly 4 ints
type Color = RGB | RGBA

let red_rgb = [255, 0, 0]
let blue_rgba = [0, 0, 255, 128]
let invalid_color = [255, 0]           // only 2

"17.1"; (red_rgb is RGB)               // true
"17.2"; (blue_rgba is RGBA)            // true
"17.3"; (red_rgb is Color)             // true - matches RGB
"17.4"; (blue_rgba is Color)           // true - matches RGBA
"17.5"; (invalid_color is Color)       // false - neither 3 nor 4 elements

// Int-based vectors (note: float arrays in unions have known limitations)
type IntVec2 = int[2]
type IntVec3 = int[3]
type IntVec4 = int[4]
type IntVector = IntVec2 | IntVec3 | IntVec4

let iv2 = [1, 2]
let iv3 = [1, 2, 3]
let iv4 = [1, 2, 3, 4]
let iv5 = [1, 2, 3, 4, 5]

"17.6"; (iv2 is IntVector)             // true
"17.7"; (iv3 is IntVector)             // true
"17.8"; (iv4 is IntVector)             // true
"17.9"; (iv5 is IntVector)             // false - 5 elements, no match

// ============================================================
// Section 18: Deeply Nested Optional Chains
// ============================================================
'Section 18: Deep Optional Chains'

type Metadata = { created: int, modified: int? }
type FileInfo = { name: string, size: int, meta: Metadata? }
type Folder = { name: string, files: FileInfo*, subfolders: map? }
type Volume = { label: string, root: Folder }
type StorageSystem = { volumes: Volume[1+] }

let storage = {
    volumes: [
        {
            label: "C:",
            root: {
                name: "root",
                files: [
                    { name: "config.sys", size: 1024, meta: { created: 1000, modified: 2000 } },
                    { name: "autoexec.bat", size: 512, meta: null }
                ],
                subfolders: {
                    windows: { name: "Windows", files: [], subfolders: null },
                    users: { name: "Users", files: [], subfolders: null }
                }
            }
        },
        {
            label: "D:",
            root: {
                name: "data",
                files: [],
                subfolders: null
            }
        }
    ]
}

"18.1"; (storage is StorageSystem)     // true - complex nested structure

let empty_storage = { volumes: [] }
"18.2"; (empty_storage is StorageSystem) // false - volumes need 1+

// ============================================================
// Section 19: Schema Evolution Patterns
// ============================================================
'Section 19: Schema Versioning'

// V1 schema
type UserV1 = { name: string, email: string }

// V2 schema adds optional fields
type UserV2 = { 
    name: string, 
    email: string, 
    phone: string?,
    verified: bool?
}

// V3 schema adds nested profile
type Profile = { bio: string?, avatar: string? }
type UserV3 = {
    name: string,
    email: string,
    phone: string?,
    verified: bool?,
    profile: Profile?
}

let user_v1 = { name: "Alice", email: "alice@example.com" }
let user_v2 = { name: "Bob", email: "bob@example.com", phone: "555-1234", verified: true }
let user_v3 = { 
    name: "Charlie", 
    email: "charlie@example.com", 
    phone: null, 
    verified: true,
    profile: { bio: "Developer", avatar: "/img/charlie.png" }
}

"19.1"; (user_v1 is UserV1)            // true
"19.2"; (user_v1 is UserV2)            // true - missing optional fields treated as null (Lambda semantics)
"19.3"; (user_v2 is UserV2)            // true
"19.4"; (user_v3 is UserV3)            // true

// Backward compatibility check - V2 data should validate against V3
let user_v2_as_v3 = { 
    name: "Bob", 
    email: "bob@example.com", 
    phone: "555-1234", 
    verified: true,
    profile: null                      // new optional field
}
"19.5"; (user_v2_as_v3 is UserV3)      // true - V2 data with null profile

// ============================================================
// Section 20: Complex Real-world: Order System
// ============================================================
'Section 20: Order System'

type Money = { amount: float, currency: string }
type ProductItem = { sku: string, name: string, price: Money }
type OrderLine = { product: ProductItem, quantity: int }
type ShippingAddress = {
    line1: string,
    line2: string?,
    city: string,
    state: string?,
    postal: string,
    country: string
}
type Payment = {
    method: string,
    amount: Money,
    status: string
}
type Order = {
    id: string,
    customerId: string,
    lines: OrderLine[1+],
    shipping: ShippingAddress,
    payments: Payment[1+],
    notes: string?
}

let order = {
    id: "ORD-2024-001",
    customerId: "CUST-123",
    lines: [
        {
            product: { sku: "WIDGET-01", name: "Blue Widget", price: { amount: 29.99, currency: "USD" } },
            quantity: 2
        },
        {
            product: { sku: "GADGET-02", name: "Red Gadget", price: { amount: 49.99, currency: "USD" } },
            quantity: 1
        }
    ],
    shipping: {
        line1: "123 Main Street",
        line2: "Apt 4B",
        city: "Boston",
        state: "MA",
        postal: "02101",
        country: "USA"
    },
    payments: [
        { method: "credit_card", amount: { amount: 109.97, currency: "USD" }, status: "completed" }
    ],
    notes: "Gift wrap please"
}

"20.1"; (order is Order)               // true - complete valid order

let order_no_lines = {
    id: "ORD-EMPTY",
    customerId: "CUST-456",
    lines: [],                         // needs at least 1
    shipping: {
        line1: "456 Oak Ave",
        line2: null,
        city: "Cambridge",
        state: "MA",
        postal: "02139",
        country: "USA"
    },
    payments: [
        { method: "cash", amount: { amount: 0.0, currency: "USD" }, status: "pending" }
    ],
    notes: null
}
"20.2"; (order_no_lines is Order)      // false - lines need 1+

let order_no_payment = {
    id: "ORD-NOPAY",
    customerId: "CUST-789",
    lines: [
        {
            product: { sku: "TEST-01", name: "Test Item", price: { amount: 10.0, currency: "USD" } },
            quantity: 1
        }
    ],
    shipping: {
        line1: "789 Pine St",
        line2: null,
        city: "Somerville",
        state: "MA",
        postal: "02143",
        country: "USA"
    },
    payments: [],                      // needs at least 1
    notes: null
}
"20.3"; (order_no_payment is Order)    // false - payments need 1+

"===== END OF ADVANCED TYPE PATTERN TESTS ====="
