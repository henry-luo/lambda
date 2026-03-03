// Test: Circular Import
// Layer: 2 | Category: negative | Covers: self-import produces error, no crash

// Attempt to import self - should produce error without crashing
import './circular_import.ls'
42
