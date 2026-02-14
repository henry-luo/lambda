#lang racket/base

;; css-layout-lang.rkt — Core Redex language definition for CSS layout
;;
;; Defines the grammar of the box tree, style structs, available space,
;; and layout result (view tree). This corresponds to Radiant's view.hpp
;; and layout.hpp data structures.

(require redex/reduction-semantics)

(provide CSS-Layout
         ;; re-export useful Redex forms
         (all-from-out redex/reduction-semantics))

;; ============================================================
;; Core CSS Layout Language
;; ============================================================

(define-language CSS-Layout

  ;; === Box Tree ===
  ;; Mirrors Radiant's DomElement with resolved display values.
  ;; After style resolution, before layout, the tree is in this form.
  (Box ::= (block BoxId Styles Children)        ; display: block flow
         | (inline BoxId Styles InlineChildren)  ; display: inline flow
         | (inline-block BoxId Styles Children)  ; display: inline-block
         | (flex BoxId Styles Children)           ; display: flex
         | (grid BoxId Styles GridDef Children)   ; display: grid
         | (table BoxId Styles TableChildren)     ; display: table
         | (text BoxId Styles string number)      ; text leaf (content, measured-width)
         | (replaced BoxId Styles number number)  ; replaced element (intrinsic w, h)
         | (none BoxId))                          ; display: none

  ;; Children types
  (Children ::= (Box ...))
  (InlineChildren ::= (InlineContent ...))
  (InlineContent ::= Box
                    | (text BoxId Styles string number))

  ;; === Table Structure ===
  (TableChildren ::= (TableRowGroup ...))
  (TableRowGroup ::= (row-group BoxId Styles (TableRow ...))
                    | TableRow)
  (TableRow ::= (row BoxId Styles (TableCell ...)))
  (TableCell ::= (cell BoxId Styles natural Children))  ; colspan

  ;; === Grid Definition ===
  ;; Mirrors Radiant's GridProp grid_template_rows/columns
  (GridDef ::= (grid-def (TrackSize ...) (TrackSize ...)))  ; rows, columns

  ;; === Styles (resolved computed values) ===
  ;; Corresponds to Radiant's BlockProp + BoundaryProp + FlexProp + etc.
  ;; Only layout-relevant properties are included.
  (Styles ::= (style StyleProp ...))

  (StyleProp ::=
    ;; Box model (from BoundaryProp)
    (width SizeValue)
    | (height SizeValue)
    | (min-width SizeValue)
    | (min-height SizeValue)
    | (max-width SizeValue)
    | (max-height SizeValue)
    | (margin Edges)
    | (padding Edges)
    | (border-width Edges)
    | (box-sizing BoxSizing)
    ;; Positioning (from PositionProp)
    | (position Position)
    | (top SizeValue)
    | (right SizeValue)
    | (bottom SizeValue)
    | (left SizeValue)
    | (z-index integer)
    | (float FloatValue)
    | (clear ClearValue)
    ;; Block (from BlockProp)
    | (text-align TextAlign)
    | (line-height SizeValue)
    | (text-indent number)
    | (white-space WhiteSpace)
    ;; Inline (from InlineProp)
    | (vertical-align VerticalAlign)
    ;; Flex container (from FlexProp)
    | (flex-direction FlexDir)
    | (flex-wrap FlexWrap)
    | (justify-content JustifyContent)
    | (align-items AlignItems)
    | (align-content AlignContent)
    | (row-gap number)
    | (column-gap number)
    ;; Flex item (from FlexItemProp)
    | (flex-grow number)
    | (flex-shrink number)
    | (flex-basis SizeValue)
    | (align-self AlignSelf)
    | (order integer)
    ;; Grid container (from GridProp)
    | (grid-template-rows (TrackSize ...))
    | (grid-template-columns (TrackSize ...))
    | (grid-auto-flow GridAutoFlow)
    | (justify-items AlignItems)
    ;; Grid item (from GridItemProp)
    | (grid-row-start GridLine)
    | (grid-row-end GridLine)
    | (grid-column-start GridLine)
    | (grid-column-end GridLine)
    ;; Overflow
    | (overflow Overflow))

  ;; === Value Types ===
  (SizeValue ::= auto
               | (px number)
               | (% number)
               | (em number)
               | (fr number)
               | min-content
               | max-content
               | fit-content
               | none)         ; for max-width/max-height "none" = no constraint

  (Edges ::= (edges number number number number))  ; top right bottom left

  ;; === Enum Types ===
  ;; Mirrors CssEnum values from Radiant
  (Position ::= static relative absolute fixed sticky)
  (BoxSizing ::= content-box border-box)
  (FloatValue ::= float-none float-left float-right)
  (ClearValue ::= clear-none clear-left clear-right clear-both)
  (TextAlign ::= left right center justify start end)
  (WhiteSpace ::= normal nowrap pre pre-wrap pre-line)
  (VerticalAlign ::= va-baseline va-top va-middle va-bottom va-text-top va-text-bottom
                    | (va-length number))
  (Overflow ::= visible hidden scroll overflow-auto clip)

  ;; Flex enums (mirrors FlexDirection, FlexWrap, etc.)
  (FlexDir ::= row row-reverse column column-reverse)
  (FlexWrap ::= nowrap wrap wrap-reverse)
  (JustifyContent ::= flex-start flex-end center space-between space-around space-evenly)
  (AlignItems ::= align-start align-end align-center align-baseline align-stretch)
  (AlignContent ::= content-start content-end content-center content-stretch
                   content-space-between content-space-around content-space-evenly)
  (AlignSelf ::= self-auto self-start self-end self-center self-baseline self-stretch)

  ;; Grid enums
  (GridAutoFlow ::= grid-row grid-column grid-row-dense grid-column-dense)
  (GridLine ::= (line integer)    ; explicit line number (1-based)
              | (span integer)    ; span N tracks
              | grid-auto)        ; auto placement

  ;; Track sizing (for grid)
  (TrackSize ::= auto
               | (px number)
               | (fr number)
               | (% number)
               | min-content
               | max-content
               | (minmax SizeValue SizeValue)
               | (repeat natural TrackSize))

  ;; === Available Space ===
  ;; Mirrors Radiant's AvailableSize/AvailableSpace
  (AvailWidth ::= (definite number) | indefinite | av-min-content | av-max-content)
  (AvailHeight ::= (definite number) | indefinite | av-min-content | av-max-content)
  (AvailableSpace ::= (avail AvailWidth AvailHeight))

  ;; === Layout Result (View Tree) ===
  ;; Mirrors Radiant's view tree output (x, y, width, height relative to parent border box)
  (View ::= (view BoxId number number number number ViewChildren)
          ;; id, x, y, width, height, children
          | (view-text BoxId number number number number string))
          ;; id, x, y, width, height, text-content

  (ViewChildren ::= (View ...))

  ;; === Identifiers ===
  (BoxId ::= variable-not-otherwise-mentioned)

  ;; === Numbers ===
  ;; Redex built-in number covers integer and real
  ;; natural is non-negative integer
  )
