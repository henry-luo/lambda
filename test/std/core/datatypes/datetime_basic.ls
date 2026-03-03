// Test: DateTime Basic Operations
// Layer: 3 | Category: datatype | Covers: datetime literal, access, type

t'2025-01-15'
type(t'2025-01-15')
t'2025-01-15' is datetime
let dt = t'2025-06-15'
dt.year
dt.month
dt.day
t'2025-01-01' == t'2025-01-01'
t'2025-01-01' != t'2025-12-31'
t'2025-06-15 10:30:00'
