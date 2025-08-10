==================
RST Directives Test
==================

This document contains comprehensive testing of reStructuredText directives.

.. contents:: Table of Contents
   :depth: 3
   :local:

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
