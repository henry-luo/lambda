// Phase 2 Feature Test - Comprehensive PDF Rendering Validation
// Tests: operators, colors, fonts, shapes, text arrays, coordinates

"\n========================================\n"
"PHASE 2 PDF RENDERING TEST\n"
"========================================\n\n"

// Test 1: Simple PDF with basic text
"[1] Testing basic PDF parsing...\n"
let simple_pdf = input('./test/input/test.pdf', 'pdf')
let version = simple_pdf.version
"  Version: " + version + "\n"
let simple_objs = simple_pdf.objects
"  Objects: " + str(len(simple_objs)) + "\n"

// Test 2: Advanced PDF
"\n[2] Testing advanced PDF...\n"
let advanced_pdf = input('./test/input/advanced_test.pdf', 'pdf')
let adv_objs = advanced_pdf.objects
"  Objects: " + str(len(adv_objs)) + "\n"

// Test 3: PDF with shapes
"\n[3] Testing shapes PDF...\n"
let shapes_pdf = input('./test/input/simple_test.pdf', 'pdf')
let shape_objs = shapes_pdf.objects
"  Objects: " + str(len(shape_objs)) + "\n"

// Test 4: Display PDF structure
"\n[4] PDF Structure Details:\n"
format(simple_pdf, 'json')
