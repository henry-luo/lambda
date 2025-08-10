==================
RST Directives Test
==================

This document contains comprehensive testing of reStructuredText directives.

.. contents:: Table of Contents
   :depth: 3
   :local:

Heading Levels
==============

Testing all heading levels with different underline characters:

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

Basic RST Elements
==================

Text Formatting
---------------

This is normal text with **strong emphasis**, *emphasis*, and ***both strong and emphasis***.

This text contains ``literal text in double backticks`` and regular text.

Basic inline markup: *emphasized*, **strong**, and ``literal`` text.

Comments
--------

.. This is a comment that should not appear in the output
   Comments can span multiple lines and are completely
   ignored by the parser.

.. Another comment here

Transition Lines
----------------

Text above the transition.

----

Text below the transition.

=====

Another transition with equals signs.

*****

Yet another transition with asterisks.

Definition Lists
---------------

term 1
    Definition of the first term. This can be a longer
    explanation that spans multiple lines.

term 2
    Definition of the second term.

another term
    And its definition.

Literal Blocks
--------------

Here's a literal block introduced with double colon::

    This is a literal block.
    It preserves whitespace and formatting.
        Even indentation is preserved.
    No markup processing occurs here.

Another way to create a literal block is with explicit marker::

    def example_function():
        """This is preserved exactly as typed."""
        return "literal content"

Grid Tables
-----------

+---------------+---------------+---------------+
| Header 1      | Header 2      | Header 3      |
+===============+===============+===============+
| Row 1, Col 1  | Row 1, Col 2  | Row 1, Col 3  |
+---------------+---------------+---------------+
| Row 2, Col 1  | Row 2, Col 2  | Row 2, Col 3  |
+---------------+---------------+---------------+

+--------+--------+
| A      | B      |
+========+========+
| 1      | 2      |
+--------+--------+
| 3      | 4      |
+--------+--------+

Inline Markup
-------------

This paragraph contains ``literal text in double backticks``, 
*emphasis*, **strong emphasis**, and reference_ links.

Here's more ``code`` and another reference_target_.

External references like Python_ and reStructuredText_ are common.

.. _reference: http://example.com/reference
.. _reference_target: http://example.com/target  
.. _Python: http://python.org
.. _reStructuredText: http://docutils.sourceforge.net/rst.html

Code Directives
===============

.. code-block:: python
   :linenos:
   :emphasize-lines: 2, 3

   def hello_world():
       print("Hello, World!")
       return True

.. code:: javascript
   :number-lines:

   function greet(name) {
       console.log(`Hello, ${name}!`);
   }

Admonition Directives
====================

.. note::
   This is a note admonition. It contains important information
   that should be highlighted to readers.

.. warning::
   This is a warning about potential issues or problems.

.. danger::
   This indicates a dangerous situation that could cause harm.

.. attention::
   Pay attention to this important information.

.. caution::
   Exercise caution when following these instructions.

.. error::
   This describes an error condition.

.. hint::
   Here's a helpful hint for users.

.. important::
   This information is critically important.

.. tip::
   Here's a useful tip to make things easier.

Image and Figure Directives
===========================

.. image:: /path/to/image.png
   :alt: Alternative text
   :height: 200px
   :width: 300px
   :scale: 50%
   :align: center

.. figure:: /path/to/figure.jpg
   :figclass: align-center
   :scale: 75%

   This is the figure caption. It can span multiple lines
   and contain formatting like **bold** and *italic* text.

Table Directives
================

.. table:: Example Table
   :widths: auto
   :align: center

   ============  =============  ============
   Column 1      Column 2       Column 3
   ============  =============  ============
   Row 1, Col 1  Row 1, Col 2   Row 1, Col 3
   Row 2, Col 1  Row 2, Col 2   Row 2, Col 3
   Row 3, Col 1  Row 3, Col 2   Row 3, Col 3
   ============  =============  ============

.. csv-table:: CSV Table Example
   :header: "Name", "Age", "City"
   :widths: 15, 10, 15

   "Alice", 30, "New York"
   "Bob", 25, "London"
   "Charlie", 35, "Tokyo"

.. list-table:: List Table Example
   :widths: 25 25 50
   :header-rows: 1

   * - Name
     - Position
     - Description
   * - Alice
     - Manager
     - Responsible for team coordination
   * - Bob
     - Developer
     - Writes code and fixes bugs

Math Directives
===============

.. math::

   E = mc^2

.. math::
   :label: quadratic

   ax^2 + bx + c = 0

The solution to equation :eq:`quadratic` is:

.. math::

   x = \frac{-b \pm \sqrt{b^2 - 4ac}}{2a}

Container Directives
===================

.. container:: custom-class

   This content is wrapped in a container with the class "custom-class".
   
   It can contain multiple paragraphs and other elements.

.. sidebar:: Sidebar Title
   :subtitle: Optional subtitle

   This is sidebar content that appears alongside the main text.
   
   It can contain any reStructuredText elements.

Inclusion Directives
===================

.. include:: /path/to/included/file.rst

.. literalinclude:: /path/to/source/file.py
   :language: python
   :lines: 1-10
   :emphasize-lines: 3-5

Raw Content Directives
======================

.. raw:: html

   <div class="custom-html">
   <p>This is raw HTML content.</p>
   </div>

.. raw:: latex

   \begin{equation}
   \int_{-\infty}^{\infty} e^{-x^2} dx = \sqrt{\pi}
   \end{equation}

Replacement Directives
=====================

.. |date| date::
.. |time| date:: %H:%M

This document was generated on |date| at |time|.

.. |logo| image:: /path/to/logo.png
          :height: 20px

Welcome to our site |logo|.

Topic and Rubric Directives
===========================

.. topic:: Special Topic

   This is a special topic that stands out from regular content.
   
   Topics can contain multiple paragraphs and other elements.

.. rubric:: Document Rubric

This is a rubric - a heading that's not part of the document structure.

Meta Directives
===============

.. meta::
   :description: Comprehensive RST test document
   :keywords: reStructuredText, directives, testing

.. title:: Custom Document Title

Custom Directives
================

.. highlight:: python
   :linenothreshold: 5

.. default-role:: code

Now `inline code` will be formatted as code by default.

.. role:: custom(emphasis)

This text uses a :custom:`custom role` for formatting.

Substitution Definitions
========================

.. _reference-label:

Reference Target
================

This section can be referenced using reference-label_.

.. [1] This is a footnote with a numeric label.
.. [#] This is an auto-numbered footnote.
.. [#label] This is an auto-numbered footnote with a custom label.
.. [*] This is an auto-symbol footnote.

Here's a reference to footnote [1]_, auto-numbered [#]_, 
labeled auto-numbered [#label]_, and auto-symbol [*]_.

Complex Nested Directives
=========================

.. container:: outer-container

   .. note::
      
      This note is inside a container.
      
      .. code-block:: python
         
         # This code block is inside the note
         def nested_function():
             return "Nested!"
      
      .. warning::
         
         Even warnings can be nested inside notes!

.. figure:: /path/to/complex-figure.png
   :align: center
   
   .. container:: caption-container
   
      Complex figure caption with:
      
      - **Bold text**
      - *Italic text*  
      - ``Code text``
      
      .. note::
         
         Even notes in figure captions!

Advanced RST Feature Testing
============================

Edge Cases and Combinations
---------------------------

.. This is a comment before a definition list

complex term with ``literal``
    This definition contains **bold**, *italic*, and ``code`` markup.
    It also references external_link_ and has a literal block::
    
        # Example code in definition
        print("Hello from definition!")

.. Another comment between elements

----

.. Comment after transition

Mixed content with ``double backticks`` and references_.
More content with trailing_underscore_ references.

Complex Grid Table with Markup
-------------------------------

+------------------+------------------+------------------+
| **Header 1**     | *Header 2*       | ``Header 3``     |
+==================+==================+==================+
| Cell with        | Cell with        | Cell with        |
| ``literal``      | *emphasis*       | reference_       |
+------------------+------------------+------------------+
| Multi-line cell  | Cell with::      | Cell with        |
| content that     |                  | - List item 1    |
| spans multiple   |     code block   | - List item 2    |
| lines            |     in cell      |                  |
+------------------+------------------+------------------+

.. _external_link: http://example.com/external
.. _references: http://example.com/references  
.. _trailing_underscore: http://example.com/underscore

Lists and Enumerations
======================

Bullet Lists
------------

- First item with dash
- Second item

  - Nested item
  - Another nested item

- Third item

* Alternative bullet with asterisk  
* Second asterisk item

+ Plus sign bullets
+ Another plus item

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

#. Auto-numbered
#. Second auto-numbered

Simple Tables
=============

Simple Table Format
-------------------

====== ====== ======
Header Header Header  
====== ====== ======
Data   Data   Data
More   More   More
====== ====== ======

References and Links
====================

External Links
--------------

This is an external link to `Python <https://python.org>`_.

Another way: Python_ is a programming language.

Anonymous Links
---------------

This is an `anonymous link`__.

Another `anonymous link`__.

__ https://example.com/first
__ https://example.com/second

Internal References
-------------------

See the section on `Basic RST Elements`_.

Cross-references to Headings_ work too.

.. _Headings: #heading-levels

Footnotes and Citations  
=======================

Footnotes
---------

This text has a footnote [1]_.

Auto-numbered footnote [#]_.

Named auto-numbered footnote [#note1]_.

Auto-symbol footnote [*]_.

.. [1] This is a numbered footnote.
.. [#] This is an auto-numbered footnote.
.. [#note1] This is a named auto-numbered footnote.
.. [*] This is an auto-symbol footnote.

Citations
---------

According to [Smith2020]_, this is important research.

Multiple citations [Jones2019]_ and [Brown2021]_ support this.

.. [Smith2020] Smith, John. "Important Research." Journal of Science, 2020.
.. [Jones2019] Jones, Mary. "Related Work." Conference Proceedings, 2019.
.. [Brown2021] Brown, David. "Recent Findings." Nature, 2021.

Field Lists and Options
=======================

Field Lists
-----------

:Author: John Doe
:Date: 2025-01-01  
:Version: 1.0.0
:Status: Draft
:Email: john@example.com
:License: MIT
:Keywords: reStructuredText, documentation, testing

Option Lists
------------

-a            Output all files
-b            Be more verbose  
--version     Show version number
--help        Show help message
--output=FILE Write output to FILE
-v, --verbose Enable verbose output

Substitutions and Special Characters
====================================

Substitutions
-------------

Replace |version| with the current version.

The |date| shows when this was generated.

.. |version| replace:: 1.0.0
.. |date| date:: %Y-%m-%d

Unicode and Escapes
-------------------

Unicode characters: α, β, γ, δ, ε

Math symbols: ∑, ∫, ∞, ≤, ≥, ≠

Arrows: →, ←, ↑, ↓, ↔

Escaped RST characters: \*not emphasized\*, \_not_emphasis\_, \`not literal\`

Regular asterisks: * and underscores: _ work normally here.

Block Quotes and Roles
======================

Block Quotes
------------

This is regular paragraph text.

    This is a block quote.
    It is indented from the left margin.
    
        This is a nested block quote.
        Even more indented.
    
    Back to the first level quote.

Back to regular text.

Roles
-----

:emphasis:`Emphasis role text`

:strong:`Strong role text` 

:literal:`Literal role text`

:subscript:`subscript text`

:superscript:`superscript text`

:title-reference:`Title Reference`
