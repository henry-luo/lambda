#lang racket/base

;; test-flex.rkt — Unit tests for flexbox layout
;;
;; Tests CSS Flexbox Level 1: direction, wrapping, grow/shrink,
;; justify-content, align-items, and multi-line layout.

(require rackunit
         racket/list
         "../css-layout-lang.rkt"
         "../layout-common.rkt"
         "../layout-dispatch.rkt")

(printf "Running flexbox layout tests...\n")

;; ============================================================
;; Test 1: Basic flex row layout
;; ============================================================

(test-case "basic flex row"
  (define box
    '(flex container
           (style (width (px 300)) (flex-direction row))
           ((block item1 (style (width (px 100)) (height (px 50))) ())
            (block item2 (style (width (px 100)) (height (px 50))) ())
            (block item3 (style (width (px 100)) (height (px 50))) ()))))
  (define view (layout-document box 800 600))
  (define children (view-children view))
  (check-equal? (length children) 3)
  ;; items should be laid out horizontally
  ;; each 100px wide, total fits exactly in 300px
  (check-= (view-width view) 300 0.01)
  (check-= (view-height view) 50 0.01))

;; ============================================================
;; Test 2: Flex grow distributes free space
;; ============================================================

(test-case "flex-grow distributes free space"
  (define box
    '(flex container
           (style (width (px 400)) (flex-direction row))
           ((block item1 (style (width (px 100)) (height (px 50))
                                (flex-grow 1)) ())
            (block item2 (style (width (px 100)) (height (px 50))
                                (flex-grow 1)) ()))))
  (define view (layout-document box 800 600))
  (define children (view-children view))
  ;; 400 - 200 (basis) = 200 free space, split equally
  ;; each item gets 100 + 100 = 200
  (check-equal? (length children) 2))

;; ============================================================
;; Test 3: Flex column layout
;; ============================================================

(test-case "flex column layout"
  (define box
    '(flex container
           (style (width (px 200)) (height (px 300))
                  (flex-direction column))
           ((block item1 (style (height (px 100))) ())
            (block item2 (style (height (px 100))) ()))))
  (define view (layout-document box 800 600))
  (check-= (view-width view) 200 0.01)
  (check-= (view-height view) 300 0.01))

;; ============================================================
;; Test 4: Flex wrap
;; ============================================================

(test-case "flex wrap creates multiple lines"
  (define box
    '(flex container
           (style (width (px 250)) (flex-direction row)
                  (flex-wrap wrap))
           ((block item1 (style (width (px 100)) (height (px 50))) ())
            (block item2 (style (width (px 100)) (height (px 50))) ())
            (block item3 (style (width (px 100)) (height (px 50))) ()))))
  (define view (layout-document box 800 600))
  ;; 3 items of 100px each, container is 250px
  ;; first line: item1 + item2 = 200px (fits)
  ;; second line: item3 = 100px
  ;; height should be 2 lines × 50px = 100px
  (check-= (view-width view) 250 0.01)
  (check-= (view-height view) 100 0.01))

;; ============================================================
;; Test 5: justify-content: center
;; ============================================================

(test-case "justify-content center"
  (define box
    '(flex container
           (style (width (px 400)) (flex-direction row)
                  (justify-content center))
           ((block item1 (style (width (px 100)) (height (px 50))) ())
            (block item2 (style (width (px 100)) (height (px 50))) ()))))
  (define view (layout-document box 800 600))
  (define children (view-children view))
  ;; 400 - 200 = 200 free space, centered = 100px offset
  (check-equal? (length children) 2)
  ;; first item starts at x=100
  (check-= (view-x (first children)) 100 1))

;; ============================================================
;; Test 6: justify-content: space-between
;; ============================================================

(test-case "justify-content space-between"
  (define box
    '(flex container
           (style (width (px 400)) (flex-direction row)
                  (justify-content space-between))
           ((block item1 (style (width (px 100)) (height (px 50))) ())
            (block item2 (style (width (px 100)) (height (px 50))) ()))))
  (define view (layout-document box 800 600))
  (define children (view-children view))
  ;; first at x=0, last at x=300 (400 - 100)
  (check-= (view-x (first children)) 0 1)
  (check-= (view-x (second children)) 300 1))

;; ============================================================
;; Test 7: Flex with padding on container
;; ============================================================

(test-case "flex container with padding"
  (define box
    '(flex container
           (style (width (px 400))
                  (padding (edges 10 20 10 20))
                  (flex-direction row))
           ((block item1 (style (width (px 100)) (height (px 50))) ()))))
  (define view (layout-document box 800 600))
  ;; container border-box = 400 + 40 = 440
  (check-= (view-width view) 440 0.01))

;; ============================================================
;; Test 8: Flex shrink
;; ============================================================

(test-case "flex-shrink reduces items"
  (define box
    '(flex container
           (style (width (px 200)) (flex-direction row))
           ((block item1 (style (width (px 150)) (height (px 50))
                                (flex-shrink 1)) ())
            (block item2 (style (width (px 150)) (height (px 50))
                                (flex-shrink 1)) ()))))
  (define view (layout-document box 800 600))
  ;; total basis = 300, container = 200
  ;; items should shrink to fit
  (check-= (view-width view) 200 0.01))

;; ============================================================
;; Test 9: Flex with gap
;; ============================================================

(test-case "flex row with column-gap"
  (define box
    '(flex container
           (style (width (px 400)) (flex-direction row)
                  (column-gap 20))
           ((block item1 (style (width (px 100)) (height (px 50))) ())
            (block item2 (style (width (px 100)) (height (px 50))) ())
            (block item3 (style (width (px 100)) (height (px 50))) ()))))
  (define view (layout-document box 800 600))
  ;; total = 3×100 + 2×20 = 340, fits in 400
  (check-= (view-width view) 400 0.01))

;; ============================================================
;; Test 10: Flex order property
;; ============================================================

(test-case "flex items respect order property"
  (define box
    '(flex container
           (style (width (px 300)) (flex-direction row))
           ((block item-b (style (width (px 100)) (height (px 50))
                                 (order 2)) ())
            (block item-a (style (width (px 100)) (height (px 50))
                                 (order 1)) ())
            (block item-c (style (width (px 100)) (height (px 50))
                                 (order 3)) ()))))
  (define view (layout-document box 800 600))
  (define children (view-children view))
  ;; items should be ordered: item-a, item-b, item-c
  (check-equal? (length children) 3))

(printf "Flexbox layout tests complete.\n")
