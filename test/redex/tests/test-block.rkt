#lang racket/base

;; test-block.rkt — Unit tests for block flow layout
;;
;; Tests CSS 2.2 block layout: vertical stacking, width resolution,
;; margin collapsing, padding, and border handling.

(require rackunit
         racket/list
         "../css-layout-lang.rkt"
         "../layout-common.rkt"
         "../layout-dispatch.rkt")

(printf "Running block layout tests...\n")

;; ============================================================
;; Test 1: Single block with fixed width/height
;; ============================================================

(test-case "single block with fixed dimensions"
  (define box
    '(block root (style (width (px 200)) (height (px 100)))
            ()))
  (define view (layout-document box 800 600))
  (check-= (view-width view) 200 0.01)
  (check-= (view-height view) 100 0.01))

;; ============================================================
;; Test 2: Block with auto width fills available space
;; ============================================================

(test-case "block with auto width fills container"
  (define box
    '(block root (style) ()))
  (define view (layout-document box 800 600))
  (check-= (view-width view) 800 0.01))

;; ============================================================
;; Test 3: Block with padding
;; ============================================================

(test-case "block with padding"
  (define box
    '(block root
            (style (width (px 200)) (height (px 100))
                   (padding (edges 10 20 10 20)))
            ()))
  (define view (layout-document box 800 600))
  ;; border-box width = content + padding-left + padding-right
  (check-= (view-width view) 240 0.01)   ; 200 + 20 + 20
  (check-= (view-height view) 120 0.01)) ; 100 + 10 + 10

;; ============================================================
;; Test 4: Block with border-box sizing
;; ============================================================

(test-case "block with border-box sizing"
  (define box
    '(block root
            (style (width (px 200)) (height (px 100))
                   (padding (edges 10 20 10 20))
                   (box-sizing border-box))
            ()))
  (define view (layout-document box 800 600))
  ;; with border-box, the specified width includes padding
  (check-= (view-width view) 200 0.01)
  (check-= (view-height view) 100 0.01))

;; ============================================================
;; Test 5: Vertical stacking of children
;; ============================================================

(test-case "vertical stacking of block children"
  (define box
    '(block root (style (width (px 400)))
            ((block child1 (style (height (px 50))) ())
             (block child2 (style (height (px 30))) ())
             (block child3 (style (height (px 70))) ()))))
  (define view (layout-document box 800 600))
  (define children (view-children view))
  (check-equal? (length children) 3)

  ;; check child positions (stacked vertically)
  (check-= (view-y (first children)) 0 0.01)
  (check-= (view-height (first children)) 50 0.01)

  (check-= (view-y (second children)) 50 0.01)
  (check-= (view-height (second children)) 30 0.01)

  (check-= (view-y (third children)) 80 0.01)
  (check-= (view-height (third children)) 70 0.01)

  ;; parent height = sum of children
  (check-= (view-height view) 150 0.01))

;; ============================================================
;; Test 6: Margin collapsing between siblings
;; ============================================================

(test-case "margin collapsing between siblings"
  (define box
    '(block root (style (width (px 400)))
            ((block child1
                    (style (height (px 50))
                           (margin (edges 0 0 20 0)))  ; margin-bottom: 20
                    ())
             (block child2
                    (style (height (px 30))
                           (margin (edges 10 0 0 0)))  ; margin-top: 10
                    ()))))
  (define view (layout-document box 800 600))
  (define children (view-children view))

  ;; margins collapse: max(20, 10) = 20
  (check-= (view-y (first children)) 0 0.01)
  (check-= (view-y (second children)) 70 0.01))  ; 50 + 20 (collapsed)

;; ============================================================
;; Test 7: Block with percentage width
;; ============================================================

(test-case "block with percentage width"
  (define box
    '(block root (style (width (% 50)))
            ()))
  (define view (layout-document box 800 600))
  (check-= (view-width view) 400 0.01))  ; 50% of 800

;; ============================================================
;; Test 8: Nested blocks
;; ============================================================

(test-case "nested blocks"
  (define box
    '(block outer (style (width (px 400)) (padding (edges 10 10 10 10)))
            ((block inner (style (height (px 50)))
                    ()))))
  (define view (layout-document box 800 600))
  (define children (view-children view))

  ;; outer: 400 + 20 padding = 420 wide
  (check-= (view-width view) 420 0.01)

  ;; inner: fills parent content area (400 - 0 margin)
  (define inner-view (first children))
  (check-= (view-width inner-view) 400 0.01)
  ;; inner positioned at padding offset
  (check-= (view-x inner-view) 10 0.01)
  (check-= (view-y inner-view) 10 0.01))

;; ============================================================
;; Test 9: Min/max width constraints
;; ============================================================

(test-case "min-width constraint"
  (define box
    '(block root (style (width (px 50)) (min-width (px 100)))
            ()))
  (define view (layout-document box 800 600))
  ;; min-width overrides smaller width
  (check-= (view-width view) 100 0.01))

(test-case "max-width constraint"
  (define box
    '(block root (style (width (px 500)) (max-width (px 300)))
            ()))
  (define view (layout-document box 800 600))
  ;; max-width clamps larger width
  (check-= (view-width view) 300 0.01))

;; ============================================================
;; Test 10: display:none produces empty view
;; ============================================================

(test-case "display:none produces empty view"
  (define box '(none hidden-div))
  (define view (layout-document box 800 600))
  (check-= (view-width view) 0 0.01)
  (check-= (view-height view) 0 0.01))

;; ============================================================
;; Test 11: Block with margin
;; ============================================================

(test-case "block children with margins"
  (define box
    '(block root (style (width (px 400)))
            ((block child1
                    (style (height (px 50))
                           (margin (edges 10 20 10 20)))
                    ()))))
  (define view (layout-document box 800 600))
  (define children (view-children view))
  (define child (first children))

  ;; child x offset by margin-left
  (check-= (view-x child) 20 0.01)
  ;; child y offset by margin-top
  (check-= (view-y child) 10 0.01))

;; ============================================================
;; Test 12: Text leaf
;; ============================================================

(test-case "text leaf layout"
  (define box
    '(text t1 (style) "Hello World" 100))
  (define view (layout-document box 800 600))
  (check-= (view-width view) 100 0.01))

;; ============================================================
;; Test 13: Replaced element
;; ============================================================

(test-case "replaced element preserves aspect ratio"
  (define box
    '(replaced img1 (style (width (px 200))) 400 300))
  (define view (layout-document box 800 600))
  ;; width specified as 200, height should be 200 * (300/400) = 150
  (check-= (view-width view) 200 0.01)
  (check-= (view-height view) 150 0.01))

(printf "Block layout tests complete.\n")
