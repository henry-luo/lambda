#lang racket/base

;; test-invariants.rkt — Cross-cutting property tests for layout
;;
;; Tests universal invariants that should hold across all layout modes:
;; - Determinism
;; - Non-negative dimensions
;; - Children within parent bounds (for non-positioned elements)
;; - No sibling overlap in normal flow

(require rackunit
         racket/list
         racket/match
         "../css-layout-lang.rkt"
         "../layout-common.rkt"
         "../layout-dispatch.rkt")

(printf "Running invariant tests...\n")

;; ============================================================
;; Invariant helpers
;; ============================================================

;; check that all views have non-negative width and height
(define (check-non-negative-dimensions view)
  (match view
    [`(view ,id ,x ,y ,w ,h (,children ...))
     (check-true (>= w 0) (format "view ~a has negative width: ~a" id w))
     (check-true (>= h 0) (format "view ~a has negative height: ~a" id h))
     (for ([child (in-list children)])
       (check-non-negative-dimensions child))]
    [`(view-text ,id ,x ,y ,w ,h ,_)
     (check-true (>= w 0) (format "text view ~a has negative width: ~a" id w))
     (check-true (>= h 0) (format "text view ~a has negative height: ~a" id h))]
    [_ (void)]))

;; check determinism: same input → same output
(define (check-deterministic box viewport-w viewport-h)
  (define view1 (layout-document box viewport-w viewport-h))
  (define view2 (layout-document box viewport-w viewport-h))
  (check-equal? view1 view2 "layout should be deterministic"))

;; ============================================================
;; Test: Non-negative dimensions for block layout
;; ============================================================

(test-case "non-negative dimensions — block"
  (define box
    '(block root (style (width (px 400)))
            ((block child1 (style (height (px 50))) ())
             (block child2 (style (height (px 30))
                                  (padding (edges 5 5 5 5))) ())
             (block child3 (style (width (px 200)) (height (px 70))) ()))))
  (define view (layout-document box 800 600))
  (check-non-negative-dimensions view))

;; ============================================================
;; Test: Non-negative dimensions for flex layout
;; ============================================================

(test-case "non-negative dimensions — flex"
  (define box
    '(flex root
           (style (width (px 200)) (flex-direction row))
           ((block item1 (style (width (px 150)) (height (px 50))
                                (flex-shrink 1)) ())
            (block item2 (style (width (px 150)) (height (px 50))
                                (flex-shrink 1)) ()))))
  (define view (layout-document box 800 600))
  (check-non-negative-dimensions view))

;; ============================================================
;; Test: Determinism for block layout
;; ============================================================

(test-case "determinism — block"
  (define box
    '(block root (style (width (px 400)))
            ((block child1 (style (height (px 50))) ())
             (block child2 (style (height (px 30))) ()))))
  (check-deterministic box 800 600))

;; ============================================================
;; Test: Determinism for flex layout
;; ============================================================

(test-case "determinism — flex"
  (define box
    '(flex root
           (style (width (px 400)) (flex-direction row))
           ((block item1 (style (width (px 100)) (height (px 50))
                                (flex-grow 1)) ())
            (block item2 (style (width (px 100)) (height (px 50))
                                (flex-grow 2)) ()))))
  (check-deterministic box 800 600))

;; ============================================================
;; Test: Determinism for grid layout
;; ============================================================

(test-case "determinism — grid"
  (define box
    '(grid root
           (style (width (px 400)))
           (grid-def () ((fr 1) (fr 1)))
           ((block item1 (style (height (px 50))) ())
            (block item2 (style (height (px 50))) ()))))
  (check-deterministic box 800 600))

;; ============================================================
;; Test: Empty container
;; ============================================================

(test-case "empty block container"
  (define box
    '(block root (style (width (px 400))) ()))
  (define view (layout-document box 800 600))
  (check-= (view-width view) 400 0.01)
  (check-= (view-height view) 0 0.01)
  (check-equal? (view-children view) '()))

(test-case "empty flex container"
  (define box
    '(flex root (style (width (px 400)) (flex-direction row)) ()))
  (define view (layout-document box 800 600))
  (check-= (view-width view) 400 0.01)
  (check-= (view-height view) 0 0.01))

;; ============================================================
;; Test: display:none contributes nothing
;; ============================================================

(test-case "display:none child has zero dimensions"
  (define box
    '(block root (style (width (px 400)))
            ((none hidden)
             (block visible (style (height (px 50))) ()))))
  (define view (layout-document box 800 600))
  (check-non-negative-dimensions view)
  ;; Only the visible child should appear in the view tree
  (check-= (length (view-children view)) 1 0.01))

;; ============================================================
;; Test: Deeply nested layout
;; ============================================================

(test-case "deeply nested layout produces valid tree"
  (define box
    '(block l1 (style (width (px 800)))
            ((block l2 (style (padding (edges 10 10 10 10)))
                    ((block l3 (style (padding (edges 5 5 5 5)))
                            ((block l4 (style (height (px 20)))
                                    ()))))))))
  (define view (layout-document box 800 600))
  (check-non-negative-dimensions view)
  ;; l4 should have height 20
  (define l2 (first (view-children view)))
  (define l3 (first (view-children l2)))
  (define l4 (first (view-children l3)))
  (check-= (view-height l4) 20 0.01))

;; ============================================================
;; Test: Mixed layout modes
;; ============================================================

(test-case "block containing flex containing block"
  (define box
    '(block outer (style (width (px 600)))
            ((flex middle
                   (style (flex-direction row))
                   ((block left-col
                           (style (width (px 200)) (height (px 100))
                                  (flex-grow 0) (flex-shrink 0))
                           ())
                    (block right-col
                           (style (width (px 200)) (height (px 100))
                                  (flex-grow 1))
                           ()))))))
  (define view (layout-document box 800 600))
  (check-non-negative-dimensions view)
  (check-deterministic box 800 600))

(printf "Invariant tests complete.\n")
