=======================
reStructuredText Test Document
=======================

This document tests all major reStructuredText features to ensure proper parsing and rendering.

.. contents:: Table of Contents
   :depth: 2
   :local:

Headings
========

Different heading levels using various underline characters:

Primary Heading
===============

Secondary Heading
-----------------

Tertiary Heading
`````````````````

Quaternary Heading
''''''''''''''''''

Quinary Heading
"""""""""""""""

Senary Heading
::::::::::::::

Text Formatting
===============

Basic text formatting:

This is normal text with **strong emphasis** and *emphasis*.

This is ``literal text`` (monospace).

Here's a transition:

----

Paragraphs and Line Blocks
==========================

This is a normal paragraph.
It can span multiple lines
and will be reflowed.

This is another paragraph separated by a blank line.

::

   This is a literal block.
   It preserves    whitespace
   and line breaks exactly.
   
      Even indentation is preserved.

Lists
=====

Bullet Lists
------------

- First item
- Second item

  - Nested item
  - Another nested item

- Third item

* Alternative bullet style
+ Yet another bullet style

Enumerated Lists
----------------

1. First numbered item
2. Second numbered item

   a. Nested alphabetic
   b. Another alphabetic

3. Third numbered item

A. Uppercase letters
B. Second uppercase

i. Roman numerals
ii. Second roman
iii. Third roman

Definition Lists
----------------

Term 1
    Definition of term 1.

Term 2
    Definition of term 2.
    Can span multiple lines.

Another term
    Another definition.

Tables
======

Simple Tables
-------------

====== ====== ======
Header Header Header
====== ====== ======
Data   Data   Data
More   More   More
====== ====== ======

Grid Tables
-----------

+----------+----------+----------+
| Header 1 | Header 2 | Header 3 |
+==========+==========+==========+
| Row 1    | Cell     | Cell     |
+----------+----------+----------+
| Row 2    | Cell     | Cell     |
+----------+----------+----------+

Code Blocks and Literals
========================

Literal Blocks
--------------

End a paragraph with double colon::

    This is a literal block.
    Code and examples go here.
    
    def hello_world():
        print("Hello, World!")

Another way to create literal blocks is with the literal block marker:

::

    Another literal block.
    All whitespace is preserved.

Directives
==========

Code Directive
--------------

.. code:: python

   def fibonacci(n):
       if n <= 1:
           return n
       return fibonacci(n-1) + fibonacci(n-2)

Note Directive
--------------

.. note::
   This is a note directive.
   It contains important information.

Warning Directive
-----------------

.. warning::
   This is a warning directive.
   Pay attention to this content.

Image Directive
---------------

.. image:: example.png
   :alt: Example image
   :width: 300px

Figure Directive
----------------

.. figure:: example.png
   :alt: Example figure
   :width: 400px
   
   This is the figure caption.

Comments
========

This section has comments that should not be rendered.

.. This is a comment.
   It spans multiple lines.
   And should not appear in the output.

Regular text continues here.

.. Another comment.

Links and References
===================

External Links
--------------

This is an external link to `Python <https://python.org>`_.

Another way: Python_ is a programming language.

.. _Python: https://python.org

Internal References
-------------------

See the section on Headings_.

.. _Headings: #headings

Anonymous Links
---------------

This is an `anonymous link`__.

__ https://example.com

Inline Markup
=============

Emphasis and Strong
-------------------

*This is emphasized text.*

**This is strong text.**

***This is both emphasized and strong.***

Literal Text
------------

``This is literal text`` with fixed-width font.

Roles
-----

:emphasis:`Emphasis role`
:strong:`Strong role`
:literal:`Literal role`

Field Lists
===========

:Author: John Doe
:Date: 2025-01-01
:Version: 1.0
:Status: Draft

Option Lists
============

-a            Output all files
-b            Be more verbose
--version     Show version
--help        Show help message

Footnotes
=========

This text has a footnote [1]_.

.. [1] This is the footnote text.

Auto-numbered footnote [#]_.

.. [#] This is an auto-numbered footnote.

Citations
=========

According to [Smith2020]_, this is important.

.. [Smith2020] Smith, John. "Important Research." Journal of Science, 2020.

Substitutions
=============

Replace |version| with the current version.

.. |version| replace:: 1.0.0

Unicode and Special Characters
=============================

Unicode characters: α, β, γ, δ, ε

Math symbols: ∑, ∫, ∞, ≤, ≥, ≠

Arrows: →, ←, ↑, ↓, ↔

Special RST characters: \*, \_, \`, \\

Escape sequences work: \*not emphasized\*

Complex Nested Structures
=========================

1. First item with nested content:

   - Bullet point
   - Another bullet point
   
   ::
   
       Code block inside list item
       with preserved formatting
   
   Back to regular text.

2. Second item with table:

   ====== ======
   Col 1  Col 2
   ====== ======
   Data   Data
   ====== ======

3. Third item with directive:

   .. note::
      This note is inside a list item.

Block Quotes
============

This is regular text.

    This is a block quote.
    It is indented from the left margin.
    
        This is a nested block quote.
    
    Back to the first level quote.

Back to regular text.

Admonitions
===========

.. attention::
   Pay attention to this.

.. caution::
   Be careful here.

.. danger::
   This is dangerous.

.. error::
   An error occurred.

.. hint::
   Here's a helpful hint.

.. important::
   This is important information.

.. note::
   This is a note.

.. tip::
   Here's a useful tip.

.. warning::
   This is a warning.

Final Section
=============

This concludes the comprehensive reStructuredText test document.
All major features should be tested above.

..
   Final comment at the end.
   This should not be visible.
