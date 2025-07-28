# Valid Markdown with Schema Violations

This markdown has valid syntax but violates the doc_schema.ls structure.

## Invalid Elements for Document Schema

This document contains an iframe element, which is not allowed in the schema:

<iframe src="https://example.com" width="500" height="300"></iframe>

## Script Tags Not Allowed

The schema doesn't allow script elements:

<script>alert('This is not allowed');</script>

## Custom HTML Elements

The schema doesn't define these custom elements:

<custom-element>Custom content</custom-element>

<my-widget data-value="123">Widget content</my-widget>

## Unsupported Attributes

This paragraph has attributes not defined in the schema:

<p onclick="handleClick()" data-custom="value" style="color: red;">
Content with unsupported attributes
</p>

## Invalid Structure

The schema expects a specific document structure, but this violates it with nested elements in wrong places.
