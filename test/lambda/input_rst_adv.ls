// Comprehensive test for RST input functionality - following PandocSchema
// This test covers basic RST syntax and verifies schema-compliant output

print("Testing RST Input Functionality")

// Test 1: Basic document structure
print("Test 1: Basic document structure")
let simple_rst = "This is a simple RST document."
let simple_result = lambda.input_rst(simple_rst)
print("Simple RST result:", simple_result)

// Test 2: Headers with levels
print("\nTest 2: Headers with level attributes")
let header_rst = """
Main Title
==========

This is content under the main title.

Subtitle
--------

Content under subtitle.

Sub-subtitle
~~~~~~~~~~~~

Content under sub-subtitle.
"""
let header_result = lambda.input_rst(header_rst)
print("Header RST result:", header_result)

// Test 3: Code blocks (should output <code> not <pre><code>)
print("\nTest 3: Code blocks")
let code_rst = """
Here's a code example::

    def hello():
        print("Hello, world!")
        return True

Back to normal text.
"""
let code_result = lambda.input_rst(code_rst)
print("Code block result:", code_result)

// Test 4: Lists with attributes
print("\nTest 4: Bullet and enumerated lists")
let list_rst = """
Bullet list:

* First item
* Second item
* Third item

Enumerated list:

1. First numbered item
2. Second numbered item
3. Third numbered item

Alphabetic list:

a. First alpha item
b. Second alpha item
c. Third alpha item
"""
let list_result = lambda.input_rst(list_rst)
print("List result:", list_result)

// Test 5: Tables with proper structure
print("\nTest 5: Tables")
let table_rst = """
Simple table:

====== ====== ======
Header Header Header
====== ====== ======
Data   Data   Data
More   Data   Here
====== ====== ======
"""
let table_result = lambda.input_rst(table_rst)
print("Table result:", table_result)

// Test 6: Definition lists
print("\nTest 6: Definition lists")
let def_rst = """
Term 1
    Definition of term 1.

Term 2
    Definition of term 2.
    With multiple lines.
"""
let def_result = lambda.input_rst(def_rst)
print("Definition list result:", def_result)

// Test 7: Directives with attributes
print("\nTest 7: Directives")
let directive_rst = """
.. note::

   This is a note directive.
   It has indented content.

.. warning:: Be careful!

   This warning has arguments.
"""
let directive_result = lambda.input_rst(directive_rst)
print("Directive result:", directive_result)

// Test 8: Inline formatting
print("\nTest 8: Inline formatting")
let inline_rst = """
This text has *emphasis*, **strong emphasis**, and ``literal text``.

There are also references_ and links.

.. _references: http://example.com
"""
let inline_result = lambda.input_rst(inline_rst)
print("Inline formatting result:", inline_result)

// Test 9: Mixed complex content
print("\nTest 9: Complex mixed content")
let complex_rst = """
Main Document
=============

Introduction
------------

This document demonstrates various RST features:

1. **Headers** with proper level attributes
2. *Lists* with bullet and enumeration styles
3. ``Code blocks`` without pre-wrapping

Example Section
~~~~~~~~~~~~~~~

Here's some code::

    function test() {
        return "schema-compliant";
    }

And a simple table:

===== =====
Col 1 Col 2
===== =====
Data  More
===== =====

.. note::

   This note shows directive handling
   with proper name attributes.

Final paragraph with `inline code` and regular text.
"""
let complex_result = lambda.input_rst(complex_rst)
print("Complex content result:", complex_result)

// Test 10: Edge cases
print("\nTest 10: Edge cases")
let edge_rst = """
Empty lines and whitespace handling:



Multiple empty lines above.

Trailing spaces:    

Mixed indentation and formatting.
"""
let edge_result = lambda.input_rst(edge_rst)
print("Edge cases result:", edge_result)

print("\nAll RST input tests completed!")
print("Results should show <doc><meta/><body>...</body></doc> structure")
print("Headers should have 'level' attributes")
print("Lists should have style/type attributes")
print("Code blocks should be <code> elements (not <pre><code>)")
print("Tables should have thead/tbody structure")
print("Directives should have 'name' attributes")
